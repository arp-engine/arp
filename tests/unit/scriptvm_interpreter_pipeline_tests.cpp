// ArpScript Interpreter pipeline unit tests.
//
// Tests Source -> frontend -> Typed IR -> bytecode lowering -> VM execution.
// Each test wraps its logic in a `test()` function that returns the expected
// value, and runSource calls runFunctionByName("test", ...).
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/interpreter/bytecode_lowerer.h"
#include "scriptvm/interpreter/eval.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace arp::scriptvm;
namespace core = arp::core;

namespace {

int g_failures = 0;

void require(bool cond, const char* label)
{
    if (!cond) {
        std::printf("FAIL: %s\n", label);
        ++g_failures;
    }
}

void testBorrowedLookupCache()
{
    arp::core::BorrowedLookupCache<int, int, 2u> cache;
    int first = 7, second = 11;
    const int* result = &second;
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(!cache.find(-1, result) && result == &second,
        "borrowed cache: miss does not invent a value");
    cache.insert(-1, &first);
    cache.insert(0, nullptr);
    require(cache.find(-1, result) && result == &first,
        "borrowed cache: exact signed key and borrowed identity");
    first = 19;
    require(cache.find(-1, result) && result == &first && *result == 19,
        "borrowed cache: stable relation observes owner payload replacement without copying");
    require(cache.find(0, result) && result == nullptr,
        "borrowed cache: missing dictionary result retained");
    cache.insert(1, &second);
    require(!cache.find(-1, result) && cache.find(1, result) && result == &second,
        "borrowed cache: eviction only affects hit rate");
    cache.clear();
    cache.insert(-1, &second);
    require(cache.find(-1, result) && result == &second && !cache.find(0, result),
        "borrowed cache: rebind discards old owners and missing results");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "borrowed cache: fixed stack budget has no allocation or quota bypass");
}

void testEmptyFrameCarrier()
{
    const auto& undefined = Value::undefined();
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    Frame frame;
    require(frame.returnValue.inlineScalar()
            && frame.returnValue.inlineScalar()->kind() == ScriptScalar::Kind::Nil,
        "empty frame: unused return carrier remains inline");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "empty frame: standby continuation frame allocates no return carrier");
    require(frame.returnValue.boxed().is_undefined()
            && frame.returnValue.boxed().storageIdentity() == undefined.storageIdentity(),
        "empty frame: explicit ABI materialization retains original undefined identity");
    auto owner = core::makeShared<int>(7);
    std::weak_ptr<int> weak = owner;
    Value payload{owner};
    const auto identity = payload.storageIdentity();
    frame.returnValue = payload;
    frame.replaceReturnValue = true;
    Frame copied(frame);
    Frame moved(std::move(copied));
    require(moved.replaceReturnValue && moved.returnValue.boxed().storageIdentity() == identity,
        "empty frame: real constructor override copy/move retains exact carrier");
    frame.reset(nullptr);
    require(frame.returnValue.inlineScalar() && !frame.replaceReturnValue,
        "empty frame: pooled reset returns to non-owning inline state");
    payload = makeNil();
    owner.reset();
    require(!weak.expired(), "empty frame: copied actual override keeps ownership");
    moved.reset(nullptr);
    require(
        weak.expired(), "empty frame: reset releases actual override ownership deterministically");
}

void testIntegerCacheQuotaScope()
{
    constexpr int key = 60001;
    Value reflectionWarm{100000};
    (void)reflectionWarm.ptr<int>();
    require(
        findCachedIntValue(key) == nullptr, "integer cache: fixture exercises a cold cache entry");
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    arp::core::CpuMemoryQuotaScope scope(quota);
    // Cold cache ownership still belongs to the thread, outside invocation
    // quota. Warm reads only share that same possibly host-mutated carrier.
    Value value = makeInt(key);
    require(quota->liveBytes() == 0u && arp::core::currentCpuMemoryQuota() == quota,
        "integer cache: cold allocation excludes invocation quota and restores its scope");
    const int original = *value.ptr<int>();
    *value.ptr<int>() = -123;
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    Value warmed = makeInt(key);
    require(warmed.storageIdentity() == value.storageIdentity() && *warmed.ptr<int>() == -123,
        "integer cache: warm hit retains original identity and actual mutated payload");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before
            && quota->liveBytes() == 0u && arp::core::currentCpuMemoryQuota() == quota,
        "integer cache: warm hit neither allocates nor changes active quota");
    *value.ptr<int>() = original;
    bool refused = false;
    try {
        (void)makeInt(-100000);
    } catch (const std::bad_alloc&) {
        refused = true;
    }
    require(refused && arp::core::currentCpuMemoryQuota() == quota && quota->liveBytes() == 0u,
        "integer cache: uncached allocation remains subject to invocation quota");
}

// A result that is NOT ok (error.code != None), used by runSourceExpectErrors
// to signal that compilation failed without incrementing g_failures.
EvalResult makeErrorResult()
{
    EvalResult r;
    r.error.code = ScriptDiagnosticCode::UnexpectedToken;
    return r;
}

// Canonical pipeline: source -> Typed IR -> bytecode -> run `test()`.
// Returns the eval result.
EvalResult runSource(const char* src, const char* label)
{
    auto cr = arp::scriptvm::test::buildInterpreter(src, "test.arp", "interpreter-pipeline-tests");
    if (!cr.ok()) {
        std::printf("FAIL: %s -- compile error: %s\n", label, cr.error.message.c_str());
        ++g_failures;
        return EvalResult{};
    }
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    return runFunctionByName(cr.program, "test", args);
}

// Like runSource but does NOT increment g_failures on compile errors.
// Used for tests that test features not yet fully implemented.
EvalResult runSourceExpectErrors(const char* src)
{
    auto cr = arp::scriptvm::test::buildInterpreter(
        src, "test.arp", "interpreter-pipeline-negative-tests");
    if (!cr.ok())
        return makeErrorResult();
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    return runFunctionByName(cr.program, "test", args);
}

void requireInt(const Value& v, int expected, const char* label)
{
    int actual = 0;
    if (!tryGetInt(v, actual)) {
        std::printf("FAIL: %s (not an int)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, want %d)\n", label, actual, expected);
        ++g_failures;
    }
}

void requireStr(const Value& v, const char* expected, const char* label)
{
    arp::core::pmr::String s;
    if (!tryGetString(v, s)) {
        std::printf("FAIL: %s (not a string)\n", label);
        ++g_failures;
        return;
    }
    if (s != arp::core::pmr::String(expected, &arp::core::defaultCpuMemoryResource())) {
        std::printf("FAIL: %s (got '%s', want '%s')\n", label, s.c_str(), expected);
        ++g_failures;
    }
}

void testYieldIntegerCacheBorrow()
{
    constexpr int coldKey = 62001;
    require(findCachedIntValue(coldKey) == nullptr, "yield cache: cold fixture");
    IntegerValueCacheBorrow unbound;
    require(!unbound.valid(), "yield cache: binding is lazy");
    const auto cache = IntegerValueCacheBorrow::current();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    {
        core::CpuMemoryQuotaScope scope(quota);
        ExecutionSlot slot{ScriptScalar(coldKey)};
        auto value = slot.takeValue(cache);
        require(value.storageIdentity() == makeInt(coldKey).storageIdentity()
                && quota->liveBytes() == 0 && core::currentCpuMemoryQuota() == quota,
            "yield cache: cold boxing uses original thread ownership and restores active quota");
        *value.ptr<int>() = -91;
        ExecutionSlot again{ScriptScalar(coldKey)};
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        auto actual = again.takeValue(cache);
        require(actual.storageIdentity() == value.storageIdentity() && *actual.ptr<int>() == -91
                && core::defaultCpuAllocator().stats().allocationCount == before,
            "yield cache: warm read shares the live mutated carrier without allocation");
        *value.ptr<int>() = coldKey;
        bool refused = false;
        try {
            ExecutionSlot outside{ScriptScalar(-62001)};
            (void)outside.takeValue(cache);
        } catch (const std::bad_alloc&) {
            refused = true;
        }
        require(refused && core::currentCpuMemoryQuota() == quota,
            "yield cache: out-of-cache integers retain invocation allocation policy");
    }
    int native = 13;
    const int constant = 17;
    for (const Value& input : {Value::native_reference(&native),
             Value{constant},
             makeString(std::string_view("owned")),
             Value{std::int64_t(1) << 40}}) {
        Frame frame;
        frame.push(input);
        const auto actual = frame.pop(&cache);
        require(actual.storageIdentity() == input.storageIdentity() && frame.stack.empty(),
            "yield cache: actual boxed/native/cv/wide carriers transfer unchanged");
    }

    auto built = arp::scriptvm::test::buildInterpreter(
        "def produce(seed: int) -> int:\n    var i: int = 0\n    while i < 3:\n"
        "        yield seed + 0\n        i = i + 1\n"
        "def echo(seed: int) -> int:\n    var last: int = 0\n    for value in produce(seed):\n"
        "        last = value\n    return last\n"
        "def test(token: any, seed: int) -> int:\n    var last: int = 0\n"
        "    for value in produce(seed):\n        last = value\n        var ignored: any = token + token\n"
        "    return last\n",
        "yield-cache.arp",
        "yield-cache");
    require(built.ok(), "yield cache: source");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(
        ValidatedInterpreterProgram::create(built.program, artifact).ok(), "yield cache: snapshot");
    if (!artifact.valid())
        return;

    struct State {
        Vm* vm;
        Vm* other;
        const Program* program;
        int key;
        int visits = 0;
    };

    struct Probe {
        State* state;
    };

    arp::reflection::registration::class_<Probe>("YieldCacheReentryProbe")
        .method("__add__", [](const Probe& left, const Probe&) -> Value {
            auto& state = *left.state;
            ++state.visits;
            const auto* actual = findCachedIntValue(state.key);
            require(actual != nullptr, "yield cache: carrier published before host callback");
            if (state.visits == 1) {
                core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
                args.emplace_back(Value{state.key + 10});
                VmError a, b;
                requireInt(state.vm->runFunctionByName(*state.program, "echo", args, &a),
                    state.key + 10,
                    "yield cache: same VM public reentry binds its own view");
                requireInt(state.other->runFunctionByName(*state.program, "echo", args, &b),
                    state.key + 10,
                    "yield cache: other VM reentry binds its own view");
                require(
                    a.code == ScriptDiagnosticCode::None && b.code == ScriptDiagnosticCode::None,
                    "yield cache: nested synchronous execution completes");
                if (actual)
                    *const_cast<Value*>(actual)->ptr<int>() = -77;
            } else if (actual)
                requireInt(*actual, -77, "yield cache: view observes host mutation after reentry");
            return makeInt(7);
        });
    for (bool validated : {false, true}) {
        const int key = validated ? 62003 : 62002;
        require(findCachedIntValue(key) == nullptr,
            "yield cache: source execution starts with cold key");
        Vm vm, other;
        const auto& program = validated ? artifact.program() : built.program;
        if (validated)
            require(vm.bindProgram(artifact).ok() && other.bindProgram(artifact).ok(),
                "yield cache: bind");
        State state{&vm, &other, &program, key};
        core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
        args.emplace_back(Value{Probe{&state}});
        args.emplace_back(Value{key});
        VmError error;
        auto result = vm.runFunctionByName(program, "test", args, &error);
        require(error.code == ScriptDiagnosticCode::None && state.visits == 3,
            "yield cache: all real producer yields and host calls execute");
        requireInt(result, -77, "yield cache: subsequent yields read actual mutated cache payload");
        auto original = makeInt(key);
        require(result.storageIdentity() == original.storageIdentity(),
            "yield cache: final result retains actual yield carrier");
        *original.ptr<int>() = key;
    }
}

void testVarDecl()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 42\n"
                      "    return x\n";
    auto r = runSource(src, "var decl");
    require(r.ok(), "var decl: ok");
    requireInt(r.value, 42, "var decl: value 42");
}

void testArithmetic()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 3 + 4 * 2 - 1\n"
                      "    return x\n";
    auto r = runSource(src, "arithmetic");
    require(r.ok(), "arithmetic: ok");
    requireInt(r.value, 10, "arithmetic: 3+4*2-1=10");
}

void testExtendedArithmetic()
{
    const char* src = "def test() -> int:\n"
                      "    var power: int = 2 ** 3\n"
                      "    var floor: int = -7 // 2\n"
                      "    return power + floor\n";
    auto r = runSource(src, "extended arithmetic");
    require(r.ok(), "extended arithmetic: ok");
    requireInt(r.value, 4, "extended arithmetic: 2**3 + (-7//2) = 4");
}

void testIf()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 5\n"
                      "    if x > 3:\n"
                      "        x = 10\n"
                      "    else:\n"
                      "        x = 0\n"
                      "    return x\n";
    auto r = runSource(src, "if");
    require(r.ok(), "if: ok");
    requireInt(r.value, 10, "if: x=10");
}

void testElif()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 3\n"
                      "    if x > 5:\n"
                      "        x = 1\n"
                      "    elif x > 2:\n"
                      "        x = 2\n"
                      "    else:\n"
                      "        x = 0\n"
                      "    return x\n";
    auto r = runSource(src, "elif");
    require(r.ok(), "elif: ok");
    requireInt(r.value, 2, "elif: x=2");
}

void testWhile()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 0\n"
                      "    var i: int = 5\n"
                      "    while i > 0:\n"
                      "        x = x + i\n"
                      "        i = i - 1\n"
                      "    return x\n";
    auto r = runSource(src, "while");
    require(r.ok(), "while: ok");
    requireInt(r.value, 15, "while: sum 1..5=15");
}

void testForRange()
{
    const char* src = "def test() -> int:\n"
                      "    var sum: int = 0\n"
                      "    for i: int in range(5):\n"
                      "        sum = sum + i\n"
                      "    return sum\n";
    auto r = runSource(src, "for range");
    require(r.ok(), "for: ok");
    requireInt(r.value, 10, "for: sum 0..4=10");
}

void testBreakContinue()
{
    const char* src = "def test() -> int:\n"
                      "    var sum: int = 0\n"
                      "    for i: int in range(10):\n"
                      "        if i == 3:\n"
                      "            continue\n"
                      "        if i == 7:\n"
                      "            break\n"
                      "        sum = sum + i\n"
                      "    return sum\n";
    auto r = runSource(src, "break/continue");
    require(r.ok(), "break: ok");
    // i=0,1,2 (skip 3), 4,5,6 (break at 7): 0+1+2+4+5+6=18
    requireInt(r.value, 18, "break: sum=18");
}

void testFunctionCall()
{
    const char* src = "def double(x: int) -> int:\n"
                      "    return x * 2\n"
                      "def test() -> int:\n"
                      "    var result: int = double(21)\n"
                      "    return result\n";
    auto r = runSource(src, "function call");
    require(r.ok(), "func: ok");
    requireInt(r.value, 42, "func: double(21)=42");
}

void testClassInheritance()
{
    const char* src = "class Importer:\n"
                      "    def read(self) -> int:\n"
                      "        return 7\n"
                      "class GlbImporter(Importer):\n"
                      "    def value(self) -> int:\n"
                      "        return self.read()\n"
                      "def test() -> int:\n"
                      "    var importer: Importer = GlbImporter()\n"
                      "    return importer.read()\n";
    auto r = runSource(src, "class inheritance");
    require(r.ok(), "class inheritance: ok");
    requireInt(r.value, 7, "class inheritance: base method result");
}

void testFibonacci()
{
    const char* src = "def fib(n: int) -> int:\n"
                      "    if n < 2:\n"
                      "        return n\n"
                      "    return fib(n - 1) + fib(n - 2)\n"
                      "def test() -> int:\n"
                      "    var result: int = fib(10)\n"
                      "    return result\n";
    auto r = runSource(src, "fibonacci");
    require(r.ok(), "fib: ok");
    requireInt(r.value, 55, "fib: fib(10)=55");
}

void testStringConcat()
{
    const char* src = "def test() -> string:\n"
                      "    var a: string = \"hello\"\n"
                      "    var b: string = \" world\"\n"
                      "    var c: string = a + b\n"
                      "    return c\n";
    auto r = runSource(src, "string concat");
    require(r.ok(), "concat: ok");
    requireStr(r.value, "hello world", "concat: value");
}

void testComparison()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 5\n"
                      "    if x < 10:\n"
                      "        x = 1\n"
                      "    else:\n"
                      "        x = 0\n"
                      "    return x\n";
    auto r = runSource(src, "comparison");
    require(r.ok(), "cmp: ok");
    requireInt(r.value, 1, "cmp: 5<10 -> x=1");
}

void testCompoundAssign()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 5\n"
                      "    x += 3\n"
                      "    x -= 1\n"
                      "    x *= 2\n"
                      "    return x\n";
    auto r = runSource(src, "compound assign");
    require(r.ok(), "compound: ok");
    // (5+3-1)*2 = 14
    requireInt(r.value, 14, "compound: (5+3-1)*2=14");
}

void testListConstruction()
{
    const char* src = "def test() -> int:\n"
                      "    var arr: [int] = [1, 2, 3]\n"
                      "    var x: int = arr[1]\n"
                      "    return x\n";
    auto r = runSource(src, "list construction");
    require(r.ok(), "list: ok");
    requireInt(r.value, 2, "list: arr[1]=2");
}

void testDictConstruction()
{
    const char* src = "def test() -> int:\n"
                      "    var m: {string: int} = {\"a\": 1, \"b\": 2}\n"
                      "    var x: int = m[\"b\"]\n"
                      "    return x\n";
    auto r = runSource(src, "dict construction");
    require(r.ok(), "dict: ok");
    requireInt(r.value, 2, "dict: m[b]=2");
}

void testClosureCapture()
{
    const char* src = "def make_adder(base: int) -> () -> int:\n"
                      "    var count: int = base\n"
                      "    def inc() -> int:\n"
                      "        count = count + 1\n"
                      "        return count\n"
                      "    return inc\n"
                      "def test() -> int:\n"
                      "    var f: () -> int = make_adder(10)\n"
                      "    return f()\n";
    auto r = runSource(src, "closure");
    require(r.ok(), "closure: ok");
    requireInt(r.value, 11, "closure: make_adder(10)() -> 11");
}

void testClosureSharedState()
{
    // Multiple calls to the same closure share the captured variable.
    const char* src = "def make_counter(start: int) -> () -> int:\n"
                      "    var count: int = start\n"
                      "    def inc() -> int:\n"
                      "        count = count + 1\n"
                      "        return count\n"
                      "    return inc\n"
                      "def test() -> int:\n"
                      "    var c: () -> int = make_counter(0)\n"
                      "    var a: int = c()\n"
                      "    var b: int = c()\n"
                      "    var d: int = c()\n"
                      "    return a + b + d\n";
    auto r = runSource(src, "closure shared");
    require(r.ok(), "closure2: ok");
    // 1 + 2 + 3 = 6
    requireInt(r.value, 6, "closure2: 1+2+3=6");
}

void testClosureIndependentInstances()
{
    // Each closure instance has its own captured variable.
    const char* src = "def make_counter(start: int) -> () -> int:\n"
                      "    var count: int = start\n"
                      "    def inc() -> int:\n"
                      "        count = count + 1\n"
                      "        return count\n"
                      "    return inc\n"
                      "def test() -> int:\n"
                      "    var a: () -> int = make_counter(10)\n"
                      "    var b: () -> int = make_counter(100)\n"
                      "    return a() + b()\n";
    auto r = runSource(src, "closure independent");
    require(r.ok(), "closure3: ok");
    // a() = 11, b() = 101 -> 112
    requireInt(r.value, 112, "closure3: 11+101=112");
}

void testUnary()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 5\n"
                      "    var y: int = -x\n"
                      "    return y\n";
    auto r = runSource(src, "unary");
    require(r.ok(), "unary: ok");
    requireInt(r.value, -5, "unary: -5");
}

void testLogicalAnd()
{
    const char* src = "def test() -> int:\n"
                      "    var t: bool = true\n"
                      "    var f: bool = false\n"
                      "    var x: int = 0\n"
                      "    if t and not f:\n"
                      "        x = 1\n"
                      "    else:\n"
                      "        x = 0\n"
                      "    return x\n";
    auto r = runSource(src, "logical and");
    require(r.ok(), "and: ok");
    requireInt(r.value, 1, "and: true and not false -> 1");
}

void testLogicalOr()
{
    const char* src = "def test() -> int:\n"
                      "    var t: bool = false\n"
                      "    var f: bool = false\n"
                      "    var x: int = 0\n"
                      "    if t or f:\n"
                      "        x = 1\n"
                      "    else:\n"
                      "        x = 0\n"
                      "    return x\n";
    auto r = runSource(src, "logical or");
    require(r.ok(), "or: ok");
    requireInt(r.value, 0, "or: false or false -> 0");
}

void testNestedIf()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 5\n"
                      "    if x > 0:\n"
                      "        if x > 3:\n"
                      "            x = 100\n"
                      "        else:\n"
                      "            x = 50\n"
                      "    else:\n"
                      "        x = 0\n"
                      "    return x\n";
    auto r = runSource(src, "nested if");
    require(r.ok(), "nested: ok");
    requireInt(r.value, 100, "nested: x=100");
}

void testReturnVoid()
{
    const char* src = "def doNothing() -> void:\n"
                      "    pass\n"
                      "def test() -> int:\n"
                      "    doNothing()\n"
                      "    var x: int = 42\n"
                      "    return x\n";
    auto r = runSource(src, "return void");
    require(r.ok(), "void: ok");
    requireInt(r.value, 42, "void: x=42");
}

void testFibLarge()
{
    const char* src = "def fib(n: int) -> int:\n"
                      "    if n < 2:\n"
                      "        return n\n"
                      "    return fib(n - 1) + fib(n - 2)\n"
                      "def test() -> int:\n"
                      "    var result: int = fib(15)\n"
                      "    return result\n";
    auto r = runSource(src, "fib 15");
    require(r.ok(), "fib15: ok");
    requireInt(r.value, 610, "fib15: fib(15)=610");
}

void testBytesPipeline()
{
    const char* src = "def size(data: bytes) -> int:\n"
                      "    return len(data)\n"
                      "def test() -> int:\n"
                      "    var payload: bytes = \"AZ\" as bytes\n"
                      "    return size(payload) + payload[1]\n";
    auto r = runSource(src, "bytes pipeline");
    require(r.ok(), "bytes pipeline: ok");
    requireInt(r.value, 92, "bytes pipeline: len + indexed byte");
}

void testInlineDirectCalls()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def inc(x: int) -> int:\n    return x + 1\n"
        "def test() -> int:\n    var i: int = 0\n    var x: int = 100000\n"
        "    while i < 1000:\n        x = inc(x)\n        i = i + 1\n    return x\n",
        "inline-calls.arp",
        "inline-calls");
    ValidatedInterpreterProgram artifact;
    require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "inline calls: compile");
    if (!artifact.valid())
        return;
    Vm vm;
    require(vm.bindProgram(artifact).ok(), "inline calls: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    VmError error;
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        101000,
        "inline calls: warm result");
    const auto before = arp::core::defaultCpuAllocator().stats();
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        101000,
        "inline calls: repeated result");
    require(arp::core::defaultCpuAllocator().stats().allocationCount - before.allocationCount <= 5,
        "inline calls: allocation is independent of internal call count");
    vm.setRecursionLimit(1);
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::RecursionLimitExceeded,
        "inline calls: recursion policy preserved");
    error = {};
    vm.setRecursionLimit(1024);
    vm.setInstructionBudget(20);
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
        "inline calls: nested instruction policy preserved");
    Vm limited;
    require(limited.bindProgram(artifact).ok(), "inline calls: limited VM bind");
    limited.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1u));
    error = {};
    (void)limited.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::ResourceLimitExceeded,
        "inline calls: frame allocation remains subject to the core quota");
}

void testInlineIndirectCalls()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "class SlotBase:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "class SlotDerived(SlotBase):\n    def step(self, value: int) -> int:\n        return value + 2\n"
        "def test() -> int:\n    var object: SlotBase = SlotDerived()\n"
        "    var i: int = 0\n    var total: int = 100000\n"
        "    while i < 1000:\n        total = object.step(total)\n        i = i + 1\n"
        "    return total\n"
        "def closure_test() -> int:\n    var offset: int = 3\n"
        "    def step(value: int) -> int:\n        return value + offset\n"
        "    var i: int = 0\n    var total: int = 100000\n"
        "    while i < 1000:\n        total = step(total)\n        i = i + 1\n"
        "    return total\n",
        "inline-indirect.arp",
        "inline-indirect");
    ValidatedInterpreterProgram artifact;
    require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "inline indirect: compile");
    if (!artifact.valid())
        return;
    Vm vm;
    require(vm.bindProgram(artifact).ok(), "inline indirect: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    VmError error;
    for (const char* function : {"test", "closure_test"}) {
        const int expected = function[0] == 't' ? 102000 : 103000;
        requireInt(vm.runFunctionByName(artifact.program(), function, args, &error),
            expected,
            "inline indirect: override/capture result");
        const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
        requireInt(vm.runFunctionByName(artifact.program(), function, args, &error),
            expected,
            "inline indirect: repeated override/capture result");
        require(error.code == ScriptDiagnosticCode::None
                && arp::core::defaultCpuAllocator().stats().allocationCount - before <= 30,
            "inline indirect: warmed allocation independent of internal call count");
    }
    vm.setRecursionLimit(1);
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::RecursionLimitExceeded,
        "inline method: recursion budget retained");
    vm.setRecursionLimit(1024);
    vm.setInstructionBudget(40);
    error = {};
    (void)vm.runFunctionByName(artifact.program(), "closure_test", args, &error);
    require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
        "inline closure: nested instruction budget retained");
}

void testActualMethodReceiverLookup()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "class ReceiverLookupA:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "class ReceiverLookupB:\n    def step(self, value: int) -> int:\n        return value + 2\n"
        "def test(object: ReceiverLookupA, value: int) -> int:\n    return object.step(value)\n",
        "actual-receiver.arp",
        "actual-receiver");
    require(built.ok(), "actual receiver: source");
    if (!built.ok())
        return;
    auto& caller = built.program.functions[built.program.funcIndex.at("test")];
    caller.paramTypes[0] = RuntimeType::Any;
    caller.paramClassNames[0].clear();
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "actual receiver: snapshot");
    if (!artifact.valid())
        return;
    auto& resource = arp::core::defaultCpuMemoryResource();
    const arp::core::pmr::String classA("ReceiverLookupA", &resource);
    const arp::core::pmr::String classB("ReceiverLookupB", &resource);
    arp::core::pmr::Vector<arp::core::pmr::String> fields{&resource};
    auto created = ScriptSemanticRuntime::makeScriptInstance(classA, fields);
    require(created.ok(), "actual receiver: create actual instance");
    if (!created.ok())
        return;
    const auto* original = created.value.ptr<arp::reflection::instance>();
    const arp::reflection::instance cvInstance(*original);
    Value cvCarrier{cvInstance};
    Value nullInstance{arp::reflection::instance(nullptr)};
    Value nativeInstance = Value::native_reference(created.value.ptr<arp::reflection::instance>());
    auto* classInfo = arp::reflection::db::instance().find_class(classA);
    require(classInfo != nullptr, "actual receiver: registered class metadata");
    if (!classInfo)
        return;
    const Value inputs[]{created.value, cvCarrier, nullInstance, nativeInstance};
    for (bool renamed : {false, true}) {
        classInfo->set_name(renamed ? classB : classA);
        for (const auto& input : inputs) {
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "actual receiver: bind");
            arp::core::pmr::Vector<Value> args{&resource};
            args.push_back(input);
            args.push_back(Value{100000});
            const auto identity = args[0].storageIdentity();
            for (int budget = -1; budget <= 12; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "actual receiver: cv/native/null-class and short budget preserve original diagnostics");
                if (a.code == ScriptDiagnosticCode::None) {
                    const int expectedValue = renamed
                            && (input.is<arp::reflection::instance>()
                                && input.ptr<arp::reflection::instance>()->class_() != nullptr)
                        ? 100002
                        : 100001;
                    requireInt(expected,
                        expectedValue,
                        "actual receiver: original dispatch sees current actual class name");
                    requireInt(actual,
                        expectedValue,
                        "actual receiver: current name is observed without class-pointer caching");
                }
                require(args[0].storageIdentity() == identity,
                    "actual receiver: exact receiver carrier retained");
            }
        }
    }
    classInfo->set_name(classA);
}

void testInlineStrings()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(text: string) -> int:\n    var i: int = 0\n    var total: int = 0\n"
        "    while i < 1000:\n        if text == \"gamma\":\n"
        "            total = total + len(\"12345678901234567890\")\n"
        "        i = i + 1\n    return total\n"
        "def literal() -> string:\n    return \"unchanged\"\n",
        "inline-strings.arp",
        "inline-strings");
    ValidatedInterpreterProgram artifact;
    require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "inline strings: compile");
    if (!artifact.valid())
        return;
    Vm vm;
    require(vm.bindProgram(artifact).ok(), "inline strings: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.emplace_back(makeString(std::string_view("gamma")));
    VmError error;
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        20000,
        "inline strings: result");
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        20000,
        "inline strings: warm result");
    require(error.code == ScriptDiagnosticCode::None
            && arp::core::defaultCpuAllocator().stats().allocationCount - before <= 5,
        "inline strings: compare and length allocation independent of loop count");
    *args[0].ptr<arp::core::pmr::String>() = "other";
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        0,
        "inline strings: observe mutable imported string");
    args.clear();
    auto returned = vm.runFunctionByName(artifact.program(), "literal", args, &error);
    returned.ptr<arp::core::pmr::String>()->append(" changed");
    requireStr(vm.runFunctionByName(artifact.program(), "literal", args, &error),
        "unchanged",
        "inline strings: host return cannot mutate immutable literal");
    vm.setInstructionBudget(5);
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    // Missing arguments remain rejected before executing any literal.
    require(error.code == ScriptDiagnosticCode::WrongArgCount,
        "inline strings: public argument contract preserved");
    args.emplace_back(makeString(std::string_view("gamma")));
    error = {};
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
        "inline strings: instruction budget preserved");
}

void testImmutableHandlerFilters()
{
    const char* sources[]{
        "def child() -> int:\n    raise \"child failure\"\n    return 0\n"
        "def test() -> int:\n    try:\n        try:\n            return child()\n"
        "        except as inner:\n            raise inner\n    except as outer:\n        return len(outer)\n    return 0\n",
        "def values() -> int:\n    try:\n        yield 1\n        raise \"after resume\"\n"
        "    except as error:\n        yield len(error)\n    return 0\n"
        "def test() -> int:\n    var total: int = 0\n    for value in values():\n"
        "        total = total + value\n    return total\n",
        "def test() -> int:\n    try:\n        raise \"first\"\n"
        "    except as error:\n        raise \"second\"\n    return 0\n"};
    for (const char* source : sources)
        for (int variant = 0; variant < 5; ++variant) {
            auto built = arp::scriptvm::test::buildInterpreter(
                source, "handler-filter.arp", "handler-filter");
            require(built.ok(), "handler filter: nested/child/rethrow/resumed generator source");
            if (!built.ok())
                continue;
            for (auto& function : built.program.functions) {
                int firstTarget = -1;
                for (auto& instruction : function.code)
                    if (instruction.op == Opcode::PushHandler) {
                        if (firstTarget < 0)
                            firstTarget = instruction.a;
                        if (variant == 1 || variant == 3)
                            instruction.s = "UserRaise";
                        if (variant == 2)
                            instruction.s = "DivisionByZero";
                        if (variant == 4)
                            instruction.s = "UnknownDiagnostic";
                    }
                if (variant >= 3 && firstTarget >= 0) {
                    // Unreachable duplicate: the original runtime selects the first
                    // instruction for the target, including an invalid first name.
                    function.code.emplace_back(Opcode::PushHandler,
                        firstTarget,
                        0,
                        0,
                        core::pmr::String(variant == 3 ? "UnknownDiagnostic" : "UserRaise"));
                }
            }
            built.program.bytecodeFingerprint =
                computeInterpreterBytecodeFingerprint(built.program);
            ValidatedInterpreterProgram artifact;
            require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
                "handler filter: invalid names retained as runtime failures");
            if (!artifact.valid())
                continue;
            for (std::size_t index = 0; index < built.program.functions.size(); ++index) {
                const auto& original = built.program.functions[index];
                const auto& immutable = artifact.program().functions[index];
                const auto* filters = artifact.handlerFilters(&immutable);
                require(filters && !artifact.handlerFilters(&original),
                    "handler filter: owned boundary independent of stack proof");
                if (!filters)
                    continue;
                const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
                bool missingValid = true;
                require(filters->lookup(-1, missingValid) == ScriptDiagnosticCode::None
                        && !missingValid,
                    "handler filter: missing target rejected");
                for (const auto& instruction : original.code)
                    if (instruction.op == Opcode::PushHandler) {
                        bool valid = false;
                        const auto code = filters->lookup(instruction.a, valid);
                        require(valid == (variant != 4)
                                && code
                                    == (variant == 1 || variant == 3
                                            ? ScriptDiagnosticCode::UserRaise
                                            : variant == 2 ? ScriptDiagnosticCode::DivisionByZero
                                                           : ScriptDiagnosticCode::None),
                            "handler filter: untyped/typed/mismatched/invalid and duplicate first-wins metadata");
                    }
                require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
                    "handler filter: lookup allocates nothing");
            }
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "handler filter: bind");
            core::pmr::Vector<Value> arguments{&core::defaultCpuMemoryResource()};
            for (int budget = -1; budget <= 90; ++budget) {
                auto aSession = core::makeShared<ScriptExecutionSession>();
                auto bSession = core::makeShared<ScriptExecutionSession>();
                aSession->remainingInstructions = bSession->remainingInstructions = budget;
                raw.setExecutionSession(aSession);
                owned.setExecutionSession(bSession);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", arguments, &a);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), "test", arguments, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                        && aSession->remainingInstructions == bSession->remainingInstructions,
                    "handler filter: every budget/PC/traceback/session and resumed handler agree");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "handler filter: original result");
            }
            raw.setExecutionSession(nullptr);
            owned.setExecutionSession(nullptr);
            for (const std::uint64_t bytes : {1u, 256u, 4096u, 1024u * 1024u}) {
                raw.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(bytes));
                owned.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(bytes));
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", arguments, &a);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), "test", arguments, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "handler filter: quota and unwind allocation order preserved");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "handler filter: quota result");
            }
            // Source mutation must not change the owned proof or handler bytes.
            for (auto& function : built.program.functions)
                for (auto& instruction : function.code)
                    if (instruction.op == Opcode::PushHandler)
                        instruction.s = "HostError";
            owned.setMemoryQuota(nullptr);
            VmError after;
            (void)owned.runFunctionByName(artifact.program(), "test", arguments, &after);
            require(after.code
                    == (variant == 4 ? ScriptDiagnosticCode::InvalidOpcode
                            : variant == 2 || source == sources[2] ? ScriptDiagnosticCode::UserRaise
                                                                   : ScriptDiagnosticCode::None),
                "handler filter: snapshot metadata survives mutable source changes");
        }
}

void testUnobservedCatchLocal()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(n: int, initial: any) -> int:\n    var i: int = 0\n    var caught: int = 0\n"
        "    while i < n:\n        try:\n            if i % 2 == 0:\n"
        "                raise \"a long diagnostic that requires an independent owned string buffer\"\n"
        "            raise \"short\"\n        except as error:\n            caught = caught + 1\n"
        "        i = i + 1\n    return caught\n",
        "catch-private.arp",
        "catch-private");
    require(built.ok(), "private catch: real source");
    if (!built.ok())
        return;
    const int functionIndex = built.program.funcIndex.at("test");
    ValidatedInterpreterProgram compiled;
    require(ValidatedInterpreterProgram::create(built.program, compiled).ok(),
        "private catch: source snapshot");
    if (!compiled.valid())
        return;
    auto proofLocal = [&](const ValidatedInterpreterProgram& artifact) {
        const auto& function = artifact.program().functions[functionIndex];
        const auto found =
            std::find_if(function.code.begin(), function.code.end(), [](const auto& instruction) {
                return instruction.op == Opcode::PushHandler;
            });
        bool valid = false;
        int slot = -1;
        if (found != function.code.end())
            artifact.handlerFilters(&function)->lookup(found->a, valid, &slot);
        return valid ? slot : -1;
    };
    require(proofLocal(compiled) >= 0,
        "private catch: exception regions retain independent unobserved proof");
    auto compare = [&](const Program& original,
                       const ValidatedInterpreterProgram& artifact,
                       bool allocations) {
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "private catch: bind");
        core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
        core::pmr::String nativeText("original native text");
        const core::pmr::String constantText("original const text");
        Value shared = makeString(std::string_view("original shared text"));
        const Value initial[]{makeNil(),
            shared,
            Value{constantText},
            Value::native_reference(&nativeText),
            Value{37}};
        args.push_back(Value{4});
        args.push_back(makeNil());
        for (const auto& carrier : initial) {
            args[1] = carrier;
            for (int budget = -1; budget <= 190; ++budget) {
                auto aSession = core::makeShared<ScriptExecutionSession>(),
                     bSession = core::makeShared<ScriptExecutionSession>();
                aSession->remainingInstructions = bSession->remainingInstructions = budget;
                raw.setExecutionSession(aSession);
                owned.setExecutionSession(bSession);
                VmError a, b;
                const auto expected = raw.runFunctionByName(original, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                        && aSession->remainingInstructions == bSession->remainingInstructions
                        && aSession->recursionDepth == bSession->recursionDepth,
                    "private catch: every budget, handler StoreLocal PC, traceback and session preserved");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "private catch: result");
            }
        }
        require(*shared.ptr<core::pmr::String>() == "original shared text"
                && nativeText == "original native text" && constantText == "original const text",
            "private catch: old shared/native/cv carriers never mutated");
        raw.setExecutionSession(nullptr);
        owned.setExecutionSession(nullptr);
        for (std::size_t limit : {0u, 1u, 2u, 4u, 65536u}) {
            raw.setMaxFrameValues(limit);
            owned.setMaxFrameValues(limit);
            VmError a, b;
            const auto expected = raw.runFunctionByName(original, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "private catch: original temporary stack/frame limits and error PC");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "private catch: limited result");
        }
        for (std::uint64_t bytes : {1u, 256u, 4096u, 1024u * 1024u}) {
            Vm first, second;
            require(second.bindProgram(artifact).ok(), "private catch: quota bind");
            auto qa = core::makeShared<core::CpuMemoryQuota>(bytes),
                 qb = core::makeShared<core::CpuMemoryQuota>(bytes);
            first.setMemoryQuota(qa);
            second.setMemoryQuota(qb);
            for (int attempt = 0; attempt != 2; ++attempt) {
                VmError a, b;
                const auto expected = first.runFunctionByName(original, "test", args, &a);
                const auto actual = second.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                        && qa->liveBytes() == qb->liveBytes(),
                    "private catch: cold/warm quota allocation, failure order and live bytes");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "private catch: quota result");
            }
        }
        if (allocations) {
            args[1] = makeNil();
            std::uint64_t counts[2]{};
            for (int repeat = 0; repeat != 3; ++repeat)
                for (int size = 0; size != 2; ++size) {
                    args[0] = Value{size == 0 ? 16 : 64};
                    VmError error;
                    const auto before = core::defaultCpuAllocator().stats().allocationCount;
                    const auto result =
                        owned.runFunctionByName(artifact.program(), "test", args, &error);
                    counts[size] = core::defaultCpuAllocator().stats().allocationCount - before;
                    require(
                        error.code == ScriptDiagnosticCode::None, "private catch: allocation run");
                    requireInt(
                        result, size == 0 ? 16 : 64, "private catch: retains every actual throw");
                }
            require(counts[0] == counts[1] && counts[1] < 16,
                "private catch: real owned payload reused, allocations independent of catch count");
        }
    };
    compare(built.program, compiled, true);
    // A non-parameter local may initially contain an aliased/native/cv payload.
    // Only the actual unique owned string from a later catch can be reused.
    auto manual = built.program;
    auto& function = manual.functions[functionIndex];
    function.locals = 5;
    function.maxStack = 2;
    function.code.clear();
    function.debugLocals.clear();
    function.safepoints.clear();
    function.exceptionRegions.clear();
    const Instruction instructions[]{{Opcode::LoadLocal, 1},
        {Opcode::StoreLocal, 4},
        {Opcode::LoadInt, 0, 0, 0},
        {Opcode::StoreLocal, 2},
        {Opcode::LoadInt, 0, 0, 0},
        {Opcode::StoreLocal, 3},
        {Opcode::LoadLocal, 2},
        {Opcode::LoadLocal, 0},
        Instruction(Opcode::IntCmpLt),
        {Opcode::JumpIfFalse, 18},
        {Opcode::PushHandler, 14},
        {Opcode::LoadString, 0, 0, 0, "payload"},
        Instruction(Opcode::Throw),
        Instruction(Opcode::PopHandler),
        {Opcode::StoreLocal, 4},
        {Opcode::LocalAddIntConst, 3, 0, 1},
        {Opcode::LocalAddIntConst, 2, 0, 1},
        {Opcode::Jump, 6},
        {Opcode::LoadLocal, 3},
        Instruction(Opcode::ReturnInt)};
    function.code.assign(std::begin(instructions), std::end(instructions));
    for (int pc = 0; pc != static_cast<int>(function.code.size()); ++pc)
        function.code[pc].pos = SourcePos("catch-carriers.arp", pc + 1, 1);
    manual.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(manual);
    ValidatedInterpreterProgram carriers;
    require(ValidatedInterpreterProgram::create(manual, carriers).ok(),
        "private catch: actual carrier fixture");
    if (carriers.valid()) {
        require(proofLocal(carriers) == 4, "private catch: private local proof");
        compare(manual, carriers, true);
    }
    for (int kind = 0; kind != 7; ++kind) {
        auto rejected = manual;
        auto& edited = rejected.functions[functionIndex];
        if (kind == 0) {
            edited.code.emplace_back(Opcode::LoadLocal, 4);
            edited.code.emplace_back(Opcode::Pop);
        }
        if (kind == 1) {
            edited.code.emplace_back(Opcode::AddrLocal, 4);
            edited.code.emplace_back(Opcode::Pop);
        }
        if (kind == 2) {
            edited.code.emplace_back(Opcode::CallValue, 0);
        }
        if (kind == 3) {
            edited.code.emplace_back(Opcode::LocalAddIntConst, 4, 0, 1);
        }
        if (kind == 4)
            edited.isGenerator = true;
        if (kind == 5) {
            BytecodeSafepoint point;
            point.pc = 0;
            point.id = "catch-observation";
            edited.safepoints.push_back(std::move(point));
        }
        if (kind == 6)
            edited.code[14].a = 1;
        const auto plan = InterpreterHandlerFilterPlan::prepare(edited);
        bool valid = false;
        int local = 99;
        plan.lookup(14, valid, &local);
        require(valid && local == -1,
            "private catch: read/capture/call/update/generator/safepoint/parameter proof rejected");
    }
}

void testImmutableRaisePayload()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def literal() -> string:\n    try:\n"
        "        raise \"a long immutable diagnostic copied before unwinding and retained as an independent catch payload\"\n"
        "    except as error:\n        return error\n    return \"\"\n"
        "def test(payload: any) -> string:\n    try:\n        raise payload\n"
        "    except as error:\n        return error\n    return \"\"\n",
        "raise-payload.arp",
        "raise-payload");
    require(built.ok(), "raise payload: source");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "raise payload: snapshot");
    if (!artifact.valid())
        return;
    Vm raw, owned;
    require(owned.bindProgram(artifact).ok(), "raise payload: bind");
    core::pmr::Vector<Value> arguments{&core::defaultCpuMemoryResource()};
    core::pmr::String nativeText("native strings preserve the original cast rejection");
    const core::pmr::String constantText("const owned string");
    const Value carriers[]{makeString(std::string_view("mutable owned string")),
        Value{constantText},
        Value::native_reference(&nativeText),
        Value{314},
        makeNil()};
    for (const char* name : {"literal", "test"})
        for (int state = 0; state < 5; ++state) {
            arguments.clear();
            if (std::strcmp(name, "test") == 0)
                arguments.push_back(carriers[state]);
            for (int budget = -1; budget <= 15; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, name, arguments, &a);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), name, arguments, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "raise payload: original budget/PC and owned/native/cv/nonstring semantics");
                if (a.code == ScriptDiagnosticCode::None) {
                    core::pmr::String expectedText, actualText;
                    require(tryGetString(expected, expectedText) && tryGetString(actual, actualText)
                            && expectedText == actualText,
                        "raise payload: original diagnostic bytes");
                    if (!arguments.empty())
                        require(actual.storageIdentity() != arguments[0].storageIdentity(),
                            "raise payload: catch retains independent owned Value identity");
                    require(actual.storageIdentity() != expected.storageIdentity(),
                        "raise payload: catch result independent across invocations");
                }
            }
        }
    raw.setInstructionBudget(-1);
    owned.setInstructionBudget(-1);
    arguments.clear();
    VmError a, b;
    const auto first = owned.runFunctionByName(artifact.program(), "literal", arguments, &b);
    const auto* function =
        &artifact.program().functions[artifact.program().funcIndex.at("literal")];
    const auto position =
        std::find_if(function->code.begin(), function->code.end(), [](const auto& instruction) {
            return instruction.op == Opcode::LoadString && !instruction.s.empty();
        });
    require(position != function->code.end(), "raise payload: immutable literal source");
    if (position != function->code.end()) {
        auto literal = ExecutionSlot::borrowLiteral(position->s);
        require(literal.immutableLiteral() == &position->s
                && ExecutionSlot(first).immutableLiteral() == nullptr,
            "raise payload: only an immutable literal exposes the borrowed bytes");
        const auto bytes = position->s;
        *first.ptr<core::pmr::String>() = "mutated catch result";
        const auto second = owned.runFunctionByName(artifact.program(), "literal", arguments, &b);
        require(*second.ptr<core::pmr::String>() == bytes && position->s == bytes,
            "raise payload: modifying catch result does not change Program or subsequent raises");
    }
    // Successful and failing quota runs must keep the original materialization
    // PC, not shift a failure from Throw into handler payload creation.
    for (const std::uint64_t bytes : {1u, 256u, 4096u, 1024u * 1024u}) {
        raw.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(bytes));
        owned.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(bytes));
        (void)raw.runFunctionByName(built.program, "literal", arguments, &a);
        (void)owned.runFunctionByName(artifact.program(), "literal", arguments, &b);
        require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
            "raise payload: quota follows original Throw materialization order");
    }
}

void testIndependentHandlerExpressions()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(n: int, seed: int) -> int:\n    var i: int = 0\n    var total: int = seed\n"
        "    while i < n:\n        try:\n            if i % 3 == 0:\n"
        "                total = total + 10 / (n - i - 1)\n            else:\n                total = total + i\n"
        "        except as error:\n            total = total + 1\n        i = i + 1\n    return total\n",
        "handler-expressions.arp",
        "handler-expressions");
    require(built.ok(), "handler expressions: source");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    for (auto& type : function.paramTypes)
        type = RuntimeType::Any;
    const int originalMaxStack = function.maxStack;
    for (int variant = 0; variant < 5; ++variant) {
        auto source = built.program;
        auto& edited = source.functions[source.funcIndex.at("test")];
        if (variant == 1 || variant == 2)
            for (auto& instruction : edited.code)
                if (instruction.op == Opcode::PushHandler)
                    instruction.s = variant == 1 ? "DivisionByZero" : "HostError";
        if (variant >= 3)
            edited.maxStack = variant - 3;
        source.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(source);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(source, artifact).ok(),
            "handler expressions: snapshot");
        if (!artifact.valid())
            continue;
        const auto* immutable =
            &artifact.program().functions[artifact.program().funcIndex.at("test")];
        const auto* plan = artifact.independentScalarExpressions(immutable);
        require(!artifact.hasVerifiedStackBound(immutable) && plan && plan->independentStackSpans,
            "handler expressions: independent qualification does not claim whole-frame proof");
        if (!plan)
            continue;
        require(plan->scratchSize > 0
                && std::any_of(plan->entries.begin(),
                    plan->entries.end(),
                    [](const auto& entry) {
                        return entry.count > 0;
                    }),
            "handler expressions: original temporary stack demand retained");
        if (variant == 0) {
            const auto found =
                std::find_if(plan->entries.begin(), plan->entries.end(), [&](const auto& entry) {
                    return entry.count == 3 && entry.evaluator
                        && plan->nodes[entry.offset].operation
                        == ScalarExpressionPlan::Operation::Local
                        && plan->nodes[entry.offset + 1].operation
                        == ScalarExpressionPlan::Operation::Constant
                        && plan->nodes[entry.offset + 2].operation
                        == ScalarExpressionPlan::Operation::Add;
                });
            require(
                found != plan->entries.end(), "independent handler: specialized addition located");
            if (found != plan->entries.end()) {
                const int operand = plan->nodes[found->offset].operand;
                const int expected = 7 + plan->nodes[found->offset + 1].operand;
                core::pmr::String literal("retained immutable stack prefix");
                Value prefix = makeString(std::string_view("retained boxed prefix"));
                ExecutionSlotBuffer locals, stack;
                locals.resize(static_cast<std::size_t>(immutable->locals));
                stack.reserve(4);
                for (const auto terminal : {ScalarExpressionPlan::Terminal::Store,
                         ScalarExpressionPlan::Terminal::Push,
                         ScalarExpressionPlan::Terminal::BranchFalse,
                         ScalarExpressionPlan::Terminal::BranchTrue}) {
                    auto entry = *found;
                    entry.terminal = terminal;
                    entry.destination = terminal == ScalarExpressionPlan::Terminal::Store
                        ? immutable->locals - 1
                        : 123;
                    for (const int budget : {-1, 0, entry.cost - 1, entry.cost, entry.cost + 1}) {
                        for (auto& local : locals)
                            local = ScriptScalar(7);
                        stack.clear();
                        stack.push_back(ExecutionSlot(prefix));
                        stack.push_back(ExecutionSlot::borrowLiteral(literal));
                        std::int64_t remaining = budget;
                        int pc = 41, last = -1;
                        const auto before = core::defaultCpuAllocator().stats().allocationCount;
                        const bool success =
                            plan->executeIndependent(entry, locals, stack, remaining, pc, last);
                        const bool enough = budget < 0 || budget >= entry.cost;
                        require(success == enough
                                && stack[0].boxed().storageIdentity() == prefix.storageIdentity()
                                && stack[1].immutableLiteral() == &literal
                                && core::defaultCpuAllocator().stats().allocationCount == before,
                            "independent handler: four terminals retain actual prefix carriers without allocation or materialization");
                        if (!enough) {
                            require(
                                remaining == budget && pc == 41 && last == -1 && stack.size() == 2,
                                "independent handler: short budget leaves original frame state untouched");
                        } else {
                            require(remaining == (budget < 0 ? budget : budget - entry.cost)
                                    && last == entry.lastPc
                                    && pc
                                        == (terminal == ScalarExpressionPlan::Terminal::BranchTrue
                                                ? entry.destination
                                                : entry.nextPc),
                                "independent handler: exact cost, next PC and terminal PC committed");
                            if (terminal == ScalarExpressionPlan::Terminal::Store)
                                require(*locals[entry.destination].scalarPointer<int>() == expected,
                                    "independent handler: original integer store");
                            if (terminal == ScalarExpressionPlan::Terminal::Push)
                                require(stack.size() == 3
                                        && *stack.back().scalarPointer<int>() == expected,
                                    "independent handler: original integer push");
                        }
                    }
                    locals[operand] = prefix;
                    stack.clear();
                    stack.push_back(ExecutionSlot(prefix));
                    std::int64_t remaining = entry.cost;
                    int pc = 41, last = -1;
                    require(!plan->executeIndependent(entry, locals, stack, remaining, pc, last)
                            && remaining == entry.cost && pc == 41 && last == -1
                            && stack.size() == 1
                            && stack[0].boxed().storageIdentity() == prefix.storageIdentity(),
                        "independent handler: actual wrong payload refuses before any commit");
                }
            }
        }
        for (const auto& entry : plan->entries)
            if (entry.count > 0) {
                require(entry.evaluator && !entry.floating && entry.leafCost == 0
                        && entry.stepLocal < 0 && entry.loopCondition < 0 && entry.appendLocal < 0
                        && entry.rangeBody < 0,
                    "handler expressions: pure one-shot integer spans only");
            }
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "handler expressions: bind");
        core::pmr::Vector<Value> arguments{&core::defaultCpuMemoryResource()};
        arguments.emplace_back(Value{7});
        arguments.emplace_back(Value{0});
        int host = 6;
        const Value payloads[]{Value{0},
            Value{2147483647},
            Value{4.5},
            makeString(std::string_view("wrong")),
            Value::native_reference(&host)};
        for (int state = 0; state < 5; ++state) {
            arguments[1] = payloads[state];
            for (int budget = -1; budget <= 220; ++budget) {
                auto aSession = core::makeShared<ScriptExecutionSession>();
                auto bSession = core::makeShared<ScriptExecutionSession>();
                aSession->remainingInstructions = bSession->remainingInstructions = budget;
                raw.setExecutionSession(aSession);
                owned.setExecutionSession(bSession);
                VmError a, b;
                const auto expected = raw.runFunctionByName(source, "test", arguments, &a);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), "test", arguments, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                        && aSession->remainingInstructions == bSession->remainingInstructions,
                    "handler expressions: budget/checked arithmetic/type/filter/low stack/PC/traceback replay");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(
                        actual, *expected.ptr<int>(), "handler expressions: original result");
            }
        }
        raw.setExecutionSession(nullptr);
        owned.setExecutionSession(nullptr);
        arguments[1] = Value{0};
        for (std::size_t limit : {0u, 1u, 5u, 8u, 65536u}) {
            raw.setMaxFrameValues(limit);
            owned.setMaxFrameValues(limit);
            VmError a, b;
            (void)raw.runFunctionByName(source, "test", arguments, &a);
            (void)owned.runFunctionByName(artifact.program(), "test", arguments, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "handler expressions: dynamic frame limit remains enforced");
        }
        raw.setMaxFrameValues(65536u);
        owned.setMaxFrameValues(65536u);
        for (std::uint64_t bytes : {1u, 256u, 4096u, 1024u * 1024u}) {
            raw.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(bytes));
            owned.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(bytes));
            VmError a, b;
            (void)raw.runFunctionByName(source, "test", arguments, &a);
            (void)owned.runFunctionByName(artifact.program(), "test", arguments, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "handler expressions: active quota uses original instruction path");
        }
        raw.setMemoryQuota(nullptr);
        owned.setMemoryQuota(nullptr);
        for (int value : {1, 9}) {
            *arguments[0].ptr<int>() = value;
            VmError a, b;
            const auto expected = raw.runFunctionByName(source, "test", arguments, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", arguments, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "handler expressions: actual imported payload changes observed on every invocation");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(
                    actual, *expected.ptr<int>(), "handler expressions: actual payload result");
        }
    }
    function.maxStack = originalMaxStack;
    int spanPc = -1;
    for (int pc = 0; pc + 3 < static_cast<int>(function.code.size()); ++pc)
        if (function.code[pc].op == Opcode::LoadLocal && function.code[pc + 1].op == Opcode::LoadInt
            && function.code[pc + 2].op == Opcode::IntAdd
            && function.code[pc + 3].op == Opcode::StoreLocal) {
            spanPc = pc;
            break;
        }
    require(spanPc >= 0, "handler expressions: exact negative span located");
    if (spanPc >= 0) {
        function.code.emplace_back(Opcode::Jump, spanPc + 1);
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram blocked;
        require(ValidatedInterpreterProgram::create(built.program, blocked).ok(),
            "handler expressions: valid incoming interior edge");
        if (blocked.valid()) {
            const auto* plan = blocked.independentScalarExpressions(
                &blocked.program().functions[blocked.program().funcIndex.at("test")]);
            require(!plan || plan->entries[spanPc].count == 0,
                "handler expressions: interior edge splits the independent span");
        }
    }
}

void testValidatedArtifact()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test() -> int:\n    return 42\n", "artifact.arp", "artifact-tests");
    require(built.ok(), "artifact: compile");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "artifact: private snapshot validation");
    if (!artifact.valid())
        return;
    require(artifact.owner().get() != &built.program, "artifact: does not alias mutable source");
    Vm vm;
    require(vm.bindProgram(artifact).ok(), "artifact: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    VmError error;
    requireInt(
        vm.runFunctionByName(artifact.program(), "test", args, &error), 42, "artifact: result");
    require(error.code == ScriptDiagnosticCode::None, "artifact: first invocation");
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (int i = 0; i < 10; ++i) {
        requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
            42,
            "artifact: repeated result");
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    require(after.allocationCount - before.allocationCount < 100,
        "artifact: repeated entry must not serialize/hash module");
    built.program.bytecodeFingerprint = "sha256:corrupt";
    (void)vm.runFunctionByName(built.program, "test", args, &error);
    require(error.code == ScriptDiagnosticCode::TypedIrInvalid,
        "artifact: mutable entry still detects corruption");
    ValidatedInterpreterProgram rejected;
    require(!ValidatedInterpreterProgram::create(built.program, rejected).ok() && !rejected.valid(),
        "artifact: corrupt fingerprint rejected at snapshot boundary");
    built.program.functions.clear();
    artifact = {}; // VM keeps its own immutable owner alive.
    error = {};
    // Retain owner before rebinding so the test can exercise snapshot lifetime.
    auto fresh = arp::scriptvm::test::buildInterpreter(
        "def test() -> int:\n    return 7\n", "other.arp", "other-artifact");
    ValidatedInterpreterProgram other;
    require(fresh.ok() && ValidatedInterpreterProgram::create(fresh.program, other).ok(),
        "artifact: replacement build");
    if (!other.valid())
        return;
    const auto owner = other.owner();
    require(vm.bindProgram(other).ok(), "artifact: replacement bind");
    other = {};
    fresh.program.functions.clear();
    requireInt(vm.runFunctionByName(*owner, "test", args, &error),
        7,
        "artifact: owned lifetime survives source and wrapper release");
    require(error.code == ScriptDiagnosticCode::None, "artifact: replacement invocation");
    vm.setInstructionBudget(0);
    (void)vm.runFunctionByName(*owner, "test", args, &error);
    require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
        "artifact: validated entry preserves instruction budget");

    Vm globalsVm;
    auto firstGlobals = arp::scriptvm::test::buildInterpreter(
        "var g: int = 3\ndef test() -> int:\n    return g\n", "globals.arp", "globals-first");
    auto secondGlobals = arp::scriptvm::test::buildInterpreter(
        "var g: int = 9\ndef test() -> int:\n    return g\n", "globals.arp", "globals-second");
    ValidatedInterpreterProgram first, second;
    require(firstGlobals.ok() && secondGlobals.ok()
            && ValidatedInterpreterProgram::create(firstGlobals.program, first).ok()
            && ValidatedInterpreterProgram::create(secondGlobals.program, second).ok(),
        "artifact: global modules compile");
    if (!first.valid() || !second.valid())
        return;
    error = {};
    require(globalsVm.bindProgram(first).ok(), "artifact: first global binding");
    requireInt(globalsVm.runFunctionByName(first.program(), "test", args, &error),
        3,
        "artifact: first globals initialized");
    require(globalsVm.bindProgram(second).ok(), "artifact: second global binding");
    requireInt(globalsVm.runFunctionByName(second.program(), "test", args, &error),
        9,
        "artifact: replacement globals isolated");
    require(globalsVm.bindProgram(first).ok(), "artifact: original global rebind");
    requireInt(globalsVm.runFunctionByName(first.program(), "test", args, &error),
        3,
        "artifact: original global graph retained");
    require(!globalsVm.bindProgram({}).ok(), "artifact: empty binding rejected");
}

void testVerifiedReleaseExecution()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(n: int) -> int:\n    var i: int = 0\n    var s: int = 0\n"
        "    while i < n:\n        s = s + i\n        i = i + 1\n    return s\n"
        "def caller(n: int) -> int:\n    return test(n)\n",
        "verified.arp",
        "verified-release");
    ValidatedInterpreterProgram artifact;
    require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "verified: compile and qualify");
    if (!artifact.valid())
        return;
    const auto index = artifact.program().funcIndex.at("test");
    const auto& function = artifact.program().functions[index];
    require(artifact.hasVerifiedStackBound(&function), "verified: loop CFG proof");
    require(!artifact.hasVerifiedStackBound(&built.program.functions[index]),
        "verified: proof cannot authorize mutable source");
    const auto mutableFunction = function;
    for (const Function* foreign : {static_cast<const Function*>(nullptr),
             &mutableFunction,
             static_cast<const Function*>(&built.program.functions[index])}) {
        require(!artifact.hasScalarDispatch(foreign)
                && artifact.scalarExpressions(foreign) == nullptr
                && artifact.independentScalarExpressions(foreign) == nullptr
                && artifact.handlerFilters(foreign) == nullptr,
            "verified: public metadata boundary rejects null, mutable source and copied private proof");
    }
    require(artifact.hasVerifiedStackBound(
                &artifact.program().functions[artifact.program().funcIndex.at("caller")]),
        "verified: static callee CFG and single-result return prove caller stack effect");
    Vm vm;
    require(vm.bindProgram(artifact).ok(), "verified: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.emplace_back(Value{20});
    VmError error;
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        190,
        "verified: specialized result");
    vm.setInstructionBudget(20);
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
        "verified: bounded loop budget retained");
    vm.setInstructionBudget(-1);
    error = {};
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        190,
        "verified: disabling exhausted budget starts an unlimited invocation");
    require(error.code == ScriptDiagnosticCode::None,
        "verified: old zero balance cannot poison unlimited invocation");
    auto session = arp::core::makeShared<ScriptExecutionSession>();
    session->begin(0, 1024);
    vm.setExecutionSession(session);
    error = {};
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
        "verified: shared session budget overrides unlimited VM");
    vm.setExecutionSession(nullptr);
    vm.setMaxFrameValues(0);
    error = {};
    (void)vm.runFunctionByName(artifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::ResourceLimitExceeded,
        "verified: frame quota retained");
    vm.setMaxFrameValues(65536);

    // A valid fingerprint cannot turn malformed operands into proven facts.
    auto malformed = built.program;
    auto& badFunction = malformed.functions[index];
    badFunction.code.front() = Instruction{Opcode::LoadLocal, badFunction.locals};
    malformed.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(malformed);
    ValidatedInterpreterProgram rejected;
    require(!ValidatedInterpreterProgram::create(malformed, rejected).ok() && !rejected.valid(),
        "verified: rehashed invalid local rejected");
    malformed = built.program;
    malformed.functions[index].code = {Instruction{Opcode::IntAdd}, Instruction{Opcode::ReturnInt}};
    malformed.functions[index].debugLocals.clear();
    malformed.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(malformed);
    require(ValidatedInterpreterProgram::create(malformed, rejected).ok()
            && !rejected.hasVerifiedStackBound(&rejected.program().functions[index]),
        "verified: underflow CFG cannot acquire stack proof");
    if (rejected.valid()) {
        Vm checked;
        require(checked.bindProgram(rejected).ok(), "verified: unproven checked binding");
        error = {};
        (void)checked.runFunctionByName(rejected.program(), "test", args, &error);
        require(error.code == ScriptDiagnosticCode::StackUnderflow,
            "verified: unproven operands still diagnose underflow");
    }
    // Even with a stack proof, imported Values are not a type proof.
    auto typed = built.program;
    typed.functions[index].code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadInt, 0, 0, 1},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::ReturnInt}};
    typed.functions[index].debugLocals.clear();
    typed.functions[index].paramTypes[0] = RuntimeType::Any;
    typed.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(typed);
    ValidatedInterpreterProgram typedArtifact;
    require(ValidatedInterpreterProgram::create(typed, typedArtifact).ok(),
        "verified: typed stack fixture");
    if (!typedArtifact.valid())
        return;
    require(typedArtifact.hasVerifiedStackBound(&typedArtifact.program().functions[index]),
        "verified: stack proof independent of imported types");
    require(vm.bindProgram(typedArtifact).ok(), "verified: typed fixture bind");
    args[0] = makeString(std::string_view("wrong"));
    error = {};
    (void)vm.runFunctionByName(typedArtifact.program(), "test", args, &error);
    require(error.code == ScriptDiagnosticCode::BadCast,
        "verified: host type mismatch cannot bypass dynamic check");
    auto inconsistent = function;
    inconsistent.code = {Instruction{Opcode::LoadBool, 1},
        Instruction{Opcode::JumpIfFalse, 3},
        Instruction{Opcode::LoadInt},
        Instruction{Opcode::ReturnInt}};
    require(InterpreterBytecodeLowerer::analyzeStack(inconsistent).status
            != TypedIrScalarStackStatus::Valid,
        "verified: unequal CFG joins not proven");
}

void testOperationSpecializationBoundaries()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(a: int, b: int) -> int:\n    return a + b\n",
        "operation.arp",
        "operation-specialization");
    require(built.ok(), "operation: fixture");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.debugLocals.clear();
    function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
    function.code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::Add},
        Instruction{Opcode::Return, 1}};
    const auto execute = [&](Opcode opcode, const Value& lhs, const Value& rhs, auto check) {
        function.code[2].op = opcode;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "operation: snapshot");
        if (!artifact.valid())
            return;
        require(artifact.hasVerifiedStackBound(
                    &artifact.program().functions[artifact.program().funcIndex.at("test")]),
            "operation: imported payload type is independent of stack proof");
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.push_back(lhs);
        args.push_back(rhs);
        for (bool owned : {false, true}) {
            Vm vm;
            if (owned)
                require(vm.bindProgram(artifact).ok(), "operation: bind");
            VmError error;
            const auto result = vm.runFunctionByName(
                owned ? artifact.program() : built.program, "test", args, &error);
            check(result, error);
        }
    };
    const Opcode operations[] = {Opcode::Add, Opcode::Sub, Opcode::Mul, Opcode::Div, Opcode::Mod};
    const double expected[] = {-5.5, -9.5, -15.0, -3.75, -1.5};
    for (int i = 0; i < 5; ++i) {
        execute(
            operations[i], Value{-7.5}, Value{2}, [&](const Value& result, const VmError& error) {
                require(error.code == ScriptDiagnosticCode::None && result.is<double>()
                        && *result.ptr<double>() == expected[i],
                    "operation: checked/owned mixed float result");
            });
    }
    execute(Opcode::Div, Value{1.0}, Value{-0.0}, [](const Value&, const VmError& error) {
        require(error.code == ScriptDiagnosticCode::DivisionByZero,
            "operation: host negative zero divisor");
    });
    execute(Opcode::Mul,
        makeString(std::string_view("wrong")),
        Value{2},
        [](const Value&, const VmError& error) {
            require(error.code == ScriptDiagnosticCode::BadCast,
                "operation: incorrect Any carrier still diagnosed");
        });
    execute(Opcode::Add,
        makeString(std::string_view("a")),
        makeString(std::string_view("b")),
        [](const Value& result, const VmError& error) {
            require(error.code == ScriptDiagnosticCode::None && result.is<arp::core::pmr::String>()
                    && *result.ptr<arp::core::pmr::String>() == "ab",
                "operation: string dynamic semantics retained");
        });
    execute(Opcode::IntCmpLt,
        Value{std::int64_t(1) << 54},
        Value{(std::int64_t(1) << 54) + 1},
        [](const Value& result, const VmError& error) {
            require(error.code == ScriptDiagnosticCode::None && result.is<bool>()
                    && *result.ptr<bool>(),
                "operation: legacy IntCmp semantics retain wide payload support");
        });
}

void testScalarExpressionPlans()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(a: int, b: int) -> int:\n    return (a + b) % 5\n",
        "expression.arp",
        "expression");
    require(built.ok(), "expression plan: source");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.locals = 3;
    function.maxStack = 2;
    function.debugLocals.clear();
    function.code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::LoadInt, 0, 0, 5},
        Instruction{Opcode::IntMod},
        Instruction{Opcode::StoreLocal, 2},
        Instruction{Opcode::LoadLocal, 2},
        Instruction{Opcode::ReturnInt}};
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "expression plan: snapshot");
    if (!artifact.valid())
        return;
    const auto* owned = &artifact.program().functions[artifact.program().funcIndex.at("test")];
    const auto* plan = artifact.scalarExpressions(owned);
    require(plan && plan->entries[0].count == 5 && plan->entries[0].nextPc == 6
            && plan->scratchSize == 2 && plan->entries[1].count == 0,
        "expression plan: exact private sequence, stack bound and terminal");
    require(artifact.scalarExpressions(&function) == nullptr,
        "expression plan: raw function cannot claim private plan");
    if (!plan)
        return;
    int locals[]{100000, 7, 0}, scratch[2], result = 0;
    require(plan->evaluate(
                plan->entries[0],
                [&](int slot) {
                    return &locals[slot];
                },
                scratch,
                result)
            && result == 2 && locals[0] == 100000 && locals[1] == 7,
        "expression plan: shared arithmetic evaluation has no local effects");
    locals[0] = std::numeric_limits<int>::max();
    locals[1] = 1;
    require(!plan->evaluate(
                plan->entries[0],
                [&](int slot) {
                    return &locals[slot];
                },
                scratch,
                result),
        "expression plan: overflow requires exact original-path diagnostic");
    require(!plan->evaluate(
                plan->entries[0],
                [](int) -> const int* {
                    return nullptr;
                },
                scratch,
                result),
        "expression plan: runtime payload guard cannot be inferred from metadata");
    function.code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::Jump, 5},
        Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::StoreLocal, 2},
        Instruction{Opcode::LoadLocal, 2},
        Instruction{Opcode::ReturnInt}};
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram boundary;
    require(ValidatedInterpreterProgram::create(built.program, boundary).ok(),
        "expression plan: branch boundary fixture");
    if (boundary.valid()) {
        const auto* blocked = boundary.scalarExpressions(
            &boundary.program().functions[boundary.program().funcIndex.at("test")]);
        require(blocked == nullptr || blocked->entries[3].count == 0,
            "expression plan: no fusion across an interior incoming edge");
    }
    require(plan->entries[0].count == 5,
        "expression plan: source mutation leaves derived snapshot intact");
}

void testIntegerExpressionExecution()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(a: int, b: int) -> int:\n    return (a / b) % 5\n",
        "expression-execution.arp",
        "expression-execution");
    require(built.ok(), "expression execution: source");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.locals = 3;
    function.maxStack = 2;
    function.debugLocals.clear();
    function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
    function.code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::IntDiv},
        Instruction{Opcode::LoadInt, 0, 0, 5},
        Instruction{Opcode::IntMod},
        Instruction{Opcode::StoreLocal, 2},
        Instruction{Opcode::LoadLocal, 2},
        Instruction{Opcode::ReturnInt}};
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "expression execution: snapshot");
    if (!artifact.valid())
        return;
    const Value inputs[][2]{{Value{100000}, Value{7}},
        {Value{11}, Value{0}},
        {Value{std::numeric_limits<int>::min()}, Value{-1}},
        {Value{1.5}, Value{2}},
        {makeString(std::string_view("wrong")), Value{2}}};
    for (const auto& input : inputs) {
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.assign(std::begin(input), std::end(input));
        const auto original = args[0].storageIdentity();
        Vm checked, optimized;
        require(optimized.bindProgram(artifact).ok(), "expression execution: bind");
        for (int budget = -1; budget <= 12; ++budget) {
            checked.setInstructionBudget(budget);
            optimized.setInstructionBudget(budget);
            VmError expectedError, actualError;
            const auto expected =
                checked.runFunctionByName(built.program, "test", args, &expectedError);
            const auto actual =
                optimized.runFunctionByName(artifact.program(), "test", args, &actualError);
            require(expectedError.code == actualError.code
                    && expectedError.message == actualError.message
                    && expectedError.traceback == actualError.traceback,
                "expression execution: every intermediate budget and fault preserves diagnostic/traceback");
            if (expectedError.code == ScriptDiagnosticCode::None) {
                int a = 0, b = 0;
                require(tryGetInt(expected, a) && tryGetInt(actual, b) && a == b,
                    "expression execution: checked and planned results agree");
            }
            require(args[0].storageIdentity() == original,
                "expression execution: imported carrier ownership unchanged");
        }
        if (input[0].is<int>())
            require(*input[0].ptr<int>() == *args[0].ptr<int>(),
                "expression execution: input scalar alias retained");
    }
    auto branch = arp::scriptvm::test::buildInterpreter(
        "def test(n: int) -> int:\n    var i: int = 0\n    var s: int = 0\n    while i < n:\n        if i % 3 == 0:\n            s = s + i\n        else:\n            s = s - i\n        i = i + 1\n    return s\n",
        "expression-branch.arp",
        "expression-branch");
    ValidatedInterpreterProgram branchArtifact;
    require(branch.ok() && ValidatedInterpreterProgram::create(branch.program, branchArtifact).ok(),
        "expression execution: branch snapshot");
    if (!branchArtifact.valid())
        return;
    Vm checked, optimized;
    require(optimized.bindProgram(branchArtifact).ok(), "expression execution: branch bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.emplace_back(Value{7});
    for (int budget = -1; budget <= 150; ++budget) {
        checked.setInstructionBudget(budget);
        optimized.setInstructionBudget(budget);
        VmError expectedError, actualError;
        auto expected = checked.runFunctionByName(branch.program, "test", args, &expectedError);
        auto actual =
            optimized.runFunctionByName(branchArtifact.program(), "test", args, &actualError);
        require(expectedError.code == actualError.code
                && expectedError.traceback == actualError.traceback,
            "expression execution: branch targets and each instruction budget agree");
        if (expectedError.code == ScriptDiagnosticCode::None)
            requireInt(actual, *expected.ptr<int>(), "expression execution: branch result");
    }
}

void testRepeatedExpressionReplay()
{
    const char* sources[]{
        "def test(n: int, seed: int) -> int:\n    var i: int = 0\n    var total: int = seed\n"
        "    while i < n:\n        total = total * 2 + 1\n        i = i + 1\n    return total\n",
        "def test(n: int, seed: int) -> int:\n    var i: int = seed\n    var total: int = 0\n"
        "    while total < n:\n        total = total + 1\n        i = i + 1\n    return total\n",
        "def test(n: int, seed: int) -> int:\n    var i: int = 0\n    var total: int = seed\n"
        "    while i < total:\n        total = total / 2\n        i = i + 1\n    return total\n"};
    for (const auto* source : sources) {
        auto built = arp::scriptvm::test::buildInterpreter(
            source, "repeated-expression.arp", "repeated-expression");
        require(built.ok(), "repeated expression: fixture");
        if (!built.ok())
            return;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "repeated expression: snapshot");
        if (!artifact.valid())
            return;
        const Value inputs[][2]{{Value{8}, Value{3}},
            {Value{4}, Value{536870911}},
            {Value{4}, Value{2147483646}},
            {Value{8.5}, Value{3}},
            {Value{4}, Value{1.5}},
            {Value{0}, Value{3}}};
        for (const auto& input : inputs) {
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.assign(std::begin(input), std::end(input));
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "repeated expression: bind");
            for (int budget = -1; budget <= 200; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError expectedError, actualError;
                const auto expected =
                    raw.runFunctionByName(built.program, "test", args, &expectedError);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), "test", args, &actualError);
                require(expectedError.code == actualError.code
                        && expectedError.message == actualError.message
                        && expectedError.traceback == actualError.traceback,
                    "repeated expression: budget and overflow after prior commits replay at the original PC");
                if (expectedError.code == ScriptDiagnosticCode::None) {
                    ScriptScalar a, b;
                    require(ScriptScalar::read(expected, a) && ScriptScalar::read(actual, b)
                            && a.kind() == b.kind() && a.floating() == b.floating(),
                        "repeated expression: actual payload and nonintegral limits retain semantics");
                }
            }
        }
    }
}

void testRightExpressionHandlers()
{
    const char* expressions[]{
        "a + b * 2", "a - b * 2", "a / (b - 1)", "a % (b + 1)", "a < b * 2", "a + b / 2"};
    for (const auto* expression : expressions) {
        arp::core::pmr::String source("def test(a: int, b: int) -> int:\n    return ",
            &arp::core::defaultCpuMemoryResource());
        source += expression;
        source += "\n";
        // Comparisons use the existing branch terminal; boolean returns retain
        // their original Value kind on the ordinary interpreter path.
        if (std::string_view(expression).find('<') != std::string_view::npos) {
            source = "def test(a: int, b: int) -> int:\n    if ";
            source += expression;
            source += ":\n        return 1\n    return 0\n";
        }
        auto built = arp::scriptvm::test::buildInterpreter(
            source, "right-expression.arp", "right-expression");
        require(built.ok(), "right expression: source");
        if (!built.ok())
            continue;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "right expression: snapshot");
        if (!artifact.valid())
            continue;
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        require(plan && plan->entries[0].evaluator && plan->entries[0].calculationCount == 0,
            "right expression: exact checked handler replaces temporary storage");
        const Value inputs[][2]{{Value{13}, Value{3}},
            {Value{-17}, Value{-5}},
            {Value{7}, Value{1}},
            {Value{2147483647}, Value{2147483647}},
            {Value{-2147483647 - 1}, Value{0}},
            {Value{1.5}, Value{3}},
            {makeString(std::string_view("bad")), Value{0}}};
        for (const auto& input : inputs) {
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "right expression: bind");
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.assign(std::begin(input), std::end(input));
            for (int budget = -1; budget <= 16; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "right expression: original error order, division/overflow/type and each budget");
                if (a.code == ScriptDiagnosticCode::None) {
                    ScriptScalar left, right;
                    require(ScriptScalar::read(expected, left) && ScriptScalar::read(actual, right)
                            && left.kind() == right.kind()
                            && (left.kind() == ScriptScalar::Kind::Bool
                                    ? left.boolean() == right.boolean()
                                    : left.floating() == right.floating()),
                        "right expression: actual scalar result");
                }
            }
        }
    }
}

void testIntegerExpressionTerminals()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(a: int, b: int) -> int:\n    return a + b\n",
        "expression-terminal.arp",
        "expression-terminal");
    require(built.ok(), "expression terminal: source");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
    function.debugLocals.clear();
    function.maxStack = 2;
    function.code = {Instruction{Opcode::LocalAddIntConst, 0, 0, 1},
        Instruction{Opcode::LocalSubIntConst, 1, 0, 1},
        Instruction{Opcode::Jump, 3},
        Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::ReturnInt}};
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "expression terminal: snapshot");
    if (!artifact.valid())
        return;
    const auto* plan = artifact.scalarExpressions(
        &artifact.program().functions[artifact.program().funcIndex.at("test")]);
    require(plan && plan->entries[0].cost == 3 && plan->entries[0].count == 3
            && plan->entries[0].stepLocal == 1 && plan->entries[0].nextPc == 3
            && plan->entries[3].cost == 4
            && plan->entries[3].terminal == ScalarExpressionPlan::Terminal::Return,
        "expression terminal: cost derives from original instructions, independent of node count");
    const Value inputs[][2]{{Value{100000}, Value{7}},
        {Value{std::numeric_limits<int>::max()}, Value{0}},
        {Value{7}, Value{std::numeric_limits<int>::min()}},
        {Value{1.5}, Value{2}},
        {Value{2}, makeString(std::string_view("wrong"))}};
    for (const auto& input : inputs) {
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.assign(std::begin(input), std::end(input));
        const auto firstIdentity = args[0].storageIdentity(),
                   secondIdentity = args[1].storageIdentity();
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "expression terminal: bind");
        for (int budget = -1; budget <= 10; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            VmError expectedError, actualError;
            const auto expected =
                raw.runFunctionByName(built.program, "test", args, &expectedError);
            const auto actual =
                owned.runFunctionByName(artifact.program(), "test", args, &actualError);
            require(expectedError.code == actualError.code
                    && expectedError.message == actualError.message
                    && expectedError.traceback == actualError.traceback,
                "expression terminal: failing tail replays original commits and fault PC");
            if (expectedError.code == ScriptDiagnosticCode::None) {
                int a = 0, b = 0;
                require(tryGetInt(expected, a) && tryGetInt(actual, b) && a == b,
                    "expression terminal: direct return ABI result agrees");
            }
            require(args[0].storageIdentity() == firstIdentity
                    && args[1].storageIdentity() == secondIdentity,
                "expression terminal: imported aliases retain identity");
        }
    }
}

void testFloatingExpressionExecution()
{
    const char* sources[]{"def test(a: float, b: float) -> float:\n    return a * 0.5 + 1.0\n",
        "def test(a: float, b: float) -> float:\n    return (a / b) % 3.0\n",
        "def test(a: float, b: float) -> int:\n    if a * 0.5 > 1.5:\n        return 1\n    return 0\n"};
    for (const auto* source : sources) {
        auto built = arp::scriptvm::test::buildInterpreter(
            source, "expression-float.arp", "expression-float");
        require(built.ok(), "floating expression: source");
        if (!built.ok())
            continue;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "floating expression: snapshot");
        if (!artifact.valid())
            continue;
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        bool floatingEntry = false;
        if (plan)
            for (const auto& entry : plan->entries)
                floatingEntry |= entry.floating && entry.count != 0;
        require(floatingEntry, "floating expression: exact homogeneous float plan prepared");
        const Value inputs[][2]{{Value{100000.125}, Value{7.0}},
            {Value{-0.0}, Value{2.0}},
            {Value{7.0}, Value{-0.0}},
            {Value{std::numeric_limits<double>::infinity()}, Value{2.0}},
            {Value{std::numeric_limits<double>::quiet_NaN()}, Value{2.0}},
            {Value{2}, Value{7.0}},
            {makeString(std::string_view("wrong")), Value{2.0}}};
        for (const auto& input : inputs) {
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.assign(std::begin(input), std::end(input));
            const auto identity = args[0].storageIdentity();
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "floating expression: bind");
            for (int budget = -1; budget <= 20; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError expectedError, actualError;
                const auto expected =
                    raw.runFunctionByName(built.program, "test", args, &expectedError);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), "test", args, &actualError);
                require(expectedError.code == actualError.code
                        && expectedError.message == actualError.message
                        && expectedError.traceback == actualError.traceback,
                    "floating expression: exact budget, zero divisor and dynamic diagnostic agree");
                if (expectedError.code == ScriptDiagnosticCode::None) {
                    if (expected.is<double>()) {
                        require(actual.is<double>(), "floating expression: result payload type");
                        if (actual.is<double>()) {
                            const double a = *expected.ptr<double>(), b = *actual.ptr<double>();
                            require((std::isnan(a) && std::isnan(b))
                                    || (a == b && std::signbit(a) == std::signbit(b)),
                                "floating expression: operation order, IEEE values and signed zero preserved");
                        }
                    } else
                        requireInt(
                            actual, *expected.ptr<int>(), "floating expression: branch result");
                }
                require(args[0].storageIdentity() == identity,
                    "floating expression: imported alias unchanged");
            }
        }
    }
}

void testScalarArgumentLeafCalls()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def arithmetic(a: int) -> int:\n    return a + 1\n"
        "def test(a: int) -> int:\n    return arithmetic(a) + 2\n",
        "expression-call.arp",
        "expression-call");
    require(built.ok(), "scalar leaf: source");
    if (!built.ok())
        return;
    const auto calleeIndex = built.program.funcIndex.at("arithmetic");
    const auto callerIndex = built.program.funcIndex.at("test");
    built.program.functions[callerIndex].paramTypes[0] = RuntimeType::Any;
    // Make the callee metadata quota observably different from its caller.
    built.program.functions[calleeIndex].locals = 16;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(
        ValidatedInterpreterProgram::create(built.program, artifact).ok(), "scalar leaf: snapshot");
    if (!artifact.valid())
        return;
    const auto* plan = artifact.scalarExpressions(&artifact.program().functions[calleeIndex]);
    require(plan && plan->argumentLeaf
            && artifact.hasVerifiedStackBound(&artifact.program().functions[callerIndex]),
        "scalar leaf: exact leaf and call stack effect proved from immutable instructions");
    const auto* callerPlan = artifact.scalarExpressions(&artifact.program().functions[callerIndex]);
    require(callerPlan && callerPlan->entries[0].leafCost == 4 && callerPlan->entries[0].cost == 9
            && callerPlan->entries[0].lastPc == 4 && callerPlan->entries[0].leafFrameValues == 16,
        "scalar leaf: flattened interpreted expression preserves callee cost, original caller PC and admission");
    const Value inputs[]{Value{100000},
        Value{std::numeric_limits<int>::max()},
        Value{std::int64_t(100000)},
        Value{1.5},
        makeString(std::string_view("wrong"))};
    for (bool sharedSession : {false, true})
        for (int limit : {1, 2})
            for (std::size_t frameLimit : {std::size_t(4), std::size_t(32)})
                for (const auto& input : inputs) {
                    Vm raw, owned;
                    require(owned.bindProgram(artifact).ok(), "scalar leaf: bind");
                    auto rawSession = arp::core::makeShared<ScriptExecutionSession>();
                    auto ownedSession = arp::core::makeShared<ScriptExecutionSession>();
                    if (sharedSession) {
                        raw.setExecutionSession(rawSession);
                        owned.setExecutionSession(ownedSession);
                    }
                    raw.setRecursionLimit(limit);
                    owned.setRecursionLimit(limit);
                    raw.setMaxFrameValues(frameLimit);
                    owned.setMaxFrameValues(frameLimit);
                    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
                    args.push_back(input);
                    const auto identity = args[0].storageIdentity();
                    for (int budget = -1; budget <= 16; ++budget) {
                        raw.setInstructionBudget(budget);
                        owned.setInstructionBudget(budget);
                        rawSession->begin(budget, limit);
                        ownedSession->begin(budget, limit);
                        VmError expectedError, actualError;
                        const auto expected =
                            raw.runFunctionByName(built.program, "test", args, &expectedError);
                        const auto actual =
                            owned.runFunctionByName(artifact.program(), "test", args, &actualError);
                        require(expectedError.code == actualError.code
                                && expectedError.message == actualError.message
                                && expectedError.traceback == actualError.traceback,
                            "scalar leaf: intermediate callee budgets, recursion and metadata quota preserve diagnostics");
                        if (expectedError.code == ScriptDiagnosticCode::None)
                            requireInt(actual, *expected.ptr<int>(), "scalar leaf: exact result");
                        require(
                            rawSession->remainingInstructions == ownedSession->remainingInstructions
                                && rawSession->recursionDepth == 0
                                && ownedSession->recursionDepth == 0,
                            "scalar leaf: shared budget consumption and logical frame scope preserved");
                        require(args[0].storageIdentity() == identity,
                            "scalar leaf: host carrier unchanged");
                    }
                }
    Vm bridged;
    require(bridged.bindProgram(artifact).ok(), "scalar leaf: bridged bind");
    int bridgeCalls = 0;
    bridged.setCallBridge(
        [](void* data,
            const arp::core::pmr::String&,
            const arp::core::pmr::Vector<Value>&,
            VmError*) -> Value {
            ++*static_cast<int*>(data);
            return Value{77};
        },
        &bridgeCalls);
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{100000});
    VmError error;
    requireInt(bridged.runFunctionByName(artifact.program(), "test", args, &error),
        79,
        "scalar leaf: dispatcher bridge remains authoritative");
    require(bridgeCalls == 1 && error.code == ScriptDiagnosticCode::None,
        "scalar leaf: bridge actually invoked");
}

void testScalarCaptureLeafCalls()
{
    const auto requireResult = [](const Value& actual, const Value& expected) {
        ScriptScalar a, b;
        require(ScriptScalar::read(actual, a) && ScriptScalar::read(expected, b)
                && a.kind() == b.kind()
                && (a.kind() == ScriptScalar::Kind::Float ? a.floating() == b.floating()
                                                          : a.integer() == b.integer()),
            "capture leaf: exact result type and payload, including dynamic arithmetic replay");
    };
    auto built =
        arp::scriptvm::test::buildInterpreter("def leaf(a: int) -> int:\n    return a + 1\n"
                                              "def test(a: int, b: int) -> int:\n    return b\n",
            "capture-leaf.arp",
            "capture-leaf");
    require(built.ok(), "capture leaf: source");
    if (!built.ok())
        return;
    const int target = built.program.funcIndex.at("leaf");
    auto& callee = built.program.functions[target];
    auto& caller = built.program.functions[built.program.funcIndex.at("test")];
    callee.captureCount = 1;
    callee.locals = 16; // Make the callee admission observably different.
    callee.paramTypes[0] = RuntimeType::Any;
    caller.paramTypes[0] = caller.paramTypes[1] = RuntimeType::Any;
    caller.code = {Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::CallValue, 0, 1},
        Instruction{Opcode::ReturnInt}};
    caller.maxStack = 2;
    caller.debugLocals.clear();
    const Opcode operations[]{Opcode::IntAdd,
        Opcode::IntSub,
        Opcode::IntMul,
        Opcode::IntDiv,
        Opcode::IntMod,
        Opcode::Add};
    const Value inputs[][2]{{Value{100000}, Value{3}},
        {Value{-17}, Value{-3}},
        {Value{std::numeric_limits<int>::max()}, Value{1}},
        {Value{std::numeric_limits<int>::min()}, Value{-1}},
        {Value{13}, Value{0}},
        {Value{13}, Value{1.5}},
        {Value{13}, Value{std::int64_t(3)}},
        {Value{13}, makeString(std::string_view("wrong"))}};
    for (const auto operation : operations)
        for (bool capturedFirst : {false, true}) {
            callee.code = {Instruction{capturedFirst ? Opcode::LoadCapture : Opcode::LoadLocal, 0},
                Instruction{capturedFirst ? Opcode::LoadLocal : Opcode::LoadCapture, 0},
                Instruction{operation},
                Instruction{Opcode::ReturnInt}};
            built.program.bytecodeFingerprint =
                computeInterpreterBytecodeFingerprint(built.program);
            ValidatedInterpreterProgram artifact;
            require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
                "capture leaf: snapshot");
            if (!artifact.valid())
                continue;
            const auto* plan = artifact.scalarExpressions(&artifact.program().functions[target]);
            require(plan && plan->captureLeaf.valid() && plan->captureLeaf.cost == 4
                    && plan->captureLeaf.capturedFirst == capturedFirst
                    && !artifact.hasVerifiedStackBound(
                        &artifact.program().functions[artifact.program().funcIndex.at("test")]),
                "capture leaf: callee proof is independent of unqualified indirect caller");
            auto cell = arp::core::makeShared<ScriptCell>(Value{3});
            ScriptClosure closure{target, {}};
            closure.captures.push_back(cell);
            Value closureValue{std::move(closure)};
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.push_back(closureValue);
            args.push_back(Value{100000});
            for (bool shared : {false, true})
                for (int recursion : {1, 2})
                    for (std::size_t frameLimit : {std::size_t(4), std::size_t(32)}) {
                        Vm raw, owned;
                        require(owned.bindProgram(artifact).ok(), "capture leaf: bind");
                        auto aSession = arp::core::makeShared<ScriptExecutionSession>();
                        auto bSession = arp::core::makeShared<ScriptExecutionSession>();
                        if (shared) {
                            raw.setExecutionSession(aSession);
                            owned.setExecutionSession(bSession);
                        }
                        raw.setRecursionLimit(recursion);
                        owned.setRecursionLimit(recursion);
                        raw.setMaxFrameValues(frameLimit);
                        owned.setMaxFrameValues(frameLimit);
                        for (const auto& input : inputs) {
                            args[1] = input[0];
                            cell->value = input[1];
                            const auto captureIdentity = cell->value.storageIdentity();
                            const auto argumentIdentity = args[1].storageIdentity();
                            for (int budget = -1; budget <= 13; ++budget) {
                                raw.setInstructionBudget(budget);
                                owned.setInstructionBudget(budget);
                                aSession->begin(budget, recursion);
                                bSession->begin(budget, recursion);
                                VmError a, b;
                                const auto expected =
                                    raw.runFunctionByName(built.program, "test", args, &a);
                                const auto actual =
                                    owned.runFunctionByName(artifact.program(), "test", args, &b);
                                require(a.code == b.code && a.message == b.message
                                        && a.traceback == b.traceback,
                                    "capture leaf: budget/callee errors and admission replay preserve PC/traceback");
                                if (a.code == ScriptDiagnosticCode::None)
                                    requireResult(actual, expected);
                                require(aSession->remainingInstructions
                                            == bSession->remainingInstructions
                                        && aSession->recursionDepth == 0
                                        && bSession->recursionDepth == 0,
                                    "capture leaf: shared instruction accounting and logical frame preserved");
                                require(cell->value.storageIdentity() == captureIdentity
                                        && args[1].storageIdentity() == argumentIdentity,
                                    "capture leaf: actual argument/captured carriers are not changed");
                            }
                        }
                    }
            // A new type/value in the same cell must be observed, never bound to
            // its original number. Missing/null captures retain the original error.
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "capture leaf: mutable capture bind");
            int host = 3;
            for (int state = 0; state != 5; ++state) {
                auto* actualClosure = closureValue.ptr<ScriptClosure>();
                actualClosure->captures.assign(1, cell);
                args[1] = Value{100000};
                if (state == 0)
                    cell->value = Value{7};
                if (state == 1)
                    cell->value = Value::native_reference(&host);
                if (state == 2)
                    cell->value = makeNil();
                if (state == 3)
                    actualClosure->captures[0].reset();
                if (state == 4)
                    actualClosure->captures.clear();
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "capture leaf: actual cell rebinding/native/nil/missing/null retain errors");
                if (a.code == ScriptDiagnosticCode::None)
                    requireResult(actual, expected);
            }
            closureValue.ptr<ScriptClosure>()->captures.assign(1, cell);
            cell->value = Value{3};
            // Cold and warmed allocator quota paths both keep the original frame.
            for (std::size_t quotaBytes : {std::size_t(1), std::size_t(1024 * 1024)}) {
                Vm aVm, bVm;
                require(bVm.bindProgram(artifact).ok(), "capture leaf: quota bind");
                aVm.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(quotaBytes));
                bVm.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(quotaBytes));
                for (int attempt = 0; attempt != 2; ++attempt) {
                    VmError a, b;
                    const auto expected = aVm.runFunctionByName(built.program, "test", args, &a);
                    const auto actual = bVm.runFunctionByName(artifact.program(), "test", args, &b);
                    require(
                        a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                        "capture leaf: allocator quota preserves original frame admission");
                    if (a.code == ScriptDiagnosticCode::None)
                        requireResult(actual, expected);
                }
            }
            auto rejected = built.program;
            auto& wrong = rejected.functions[target];
            wrong.code[0] = Instruction{Opcode::LoadInt, 0, 0, 3};
            rejected.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(rejected);
            require(validateInterpreterBytecode(rejected).ok(),
                "capture leaf: rejected operand fixture remains valid bytecode");
            require(!InterpreterBytecodeLowerer::isScalarCaptureLeaf(wrong),
                "capture leaf: a different independently valid operand shape is not inferred");
            wrong = built.program.functions[target];
            wrong.code.push_back(Instruction{Opcode::LoadNil});
            rejected.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(rejected);
            require(validateInterpreterBytecode(rejected).ok(),
                "capture leaf: unreachable extension fixture remains valid bytecode");
            require(!InterpreterBytecodeLowerer::isScalarCaptureLeaf(wrong),
                "capture leaf: additional unreachable code is not admitted");
            auto wrongArity = built.program;
            wrongArity.functions[target].params = 2;
            wrongArity.functions[target].fixedParams = 2;
            wrongArity.functions[target].paramTypes.push_back(RuntimeType::Any);
            wrongArity.functions[target].nullableParams.push_back(0);
            wrongArity.functions[target].paramClassNames.emplace_back();
            wrongArity.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(wrongArity);
            ValidatedInterpreterProgram wrongArtifact;
            require(ValidatedInterpreterProgram::create(wrongArity, wrongArtifact).ok(),
                "capture leaf: wrong arity snapshot");
            if (wrongArtifact.valid()) {
                Vm rawArity, ownedArity;
                require(
                    ownedArity.bindProgram(wrongArtifact).ok(), "capture leaf: wrong arity bind");
                VmError a, b;
                (void)rawArity.runFunctionByName(wrongArity, "test", args, &a);
                (void)ownedArity.runFunctionByName(wrongArtifact.program(), "test", args, &b);
                require(a.code == ScriptDiagnosticCode::WrongArgCount && a.code == b.code
                        && a.message == b.message && a.traceback == b.traceback,
                    "capture leaf: dynamically selected callee still checks actual argument count");
            }
        }
    auto source = arp::scriptvm::test::buildInterpreter(
        "def test(a: int) -> int:\n    var offset: int = 3\n"
        "    def step(value: int) -> int:\n        return value + offset\n"
        "    return step(a)\n",
        "capture-source.arp",
        "capture-source");
    ValidatedInterpreterProgram artifact;
    require(source.ok() && ValidatedInterpreterProgram::create(source.program, artifact).ok(),
        "capture leaf: source lowering remains authoritative");
    if (!artifact.valid())
        return;
    bool qualified = false;
    for (const auto& function : artifact.program().functions) {
        const auto* plan = artifact.scalarExpressions(&function);
        qualified |= plan && plan->captureLeaf.valid();
    }
    require(
        qualified, "capture leaf: real captured source qualifies without declaration inference");
}

void testExtendedExpressionOperations()
{
    const Opcode operations[]{Opcode::BitAnd,
        Opcode::BitOr,
        Opcode::BitXor,
        Opcode::BitClear,
        Opcode::ShiftLeft,
        Opcode::ShiftRight,
        Opcode::Pow,
        Opcode::FloorDiv};
    const Value inputs[][2]{{Value{7}, Value{2}},
        {Value{-7}, Value{2}},
        {Value{std::numeric_limits<int>::min()}, Value{-1}},
        {Value{1}, Value{32}},
        {Value{7}, Value{0}},
        {Value{1.5}, Value{2}},
        {Value{std::int64_t(7)}, Value{2}}};
    for (const auto op : operations) {
        auto built = arp::scriptvm::test::buildInterpreter(
            "def test(a: int, b: int) -> int:\n    return a + b\n",
            "expression-extended.arp",
            "expression-extended");
        require(built.ok(), "extended expression: source");
        if (!built.ok())
            continue;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
        function.locals = 3;
        function.debugLocals.clear();
        function.code = {Instruction{Opcode::LoadLocal, 0},
            Instruction{Opcode::LoadLocal, 1},
            Instruction{op},
            Instruction{Opcode::StoreLocal, 2},
            Instruction{Opcode::LoadLocal, 2},
            Instruction{Opcode::ReturnInt}};
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "extended expression: snapshot");
        if (!artifact.valid())
            continue;
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        require(plan && plan->entries[0].count == 3, "extended expression: exact plan qualified");
        require(artifact.hasScalarDispatch(
                    &artifact.program().functions[artifact.program().funcIndex.at("test")]),
            "extended expression: shared scalar dispatcher admits pow/floor/bitwise/shift, including partial-budget replay");
        for (const auto& input : inputs) {
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "extended expression: bind");
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.assign(std::begin(input), std::end(input));
            for (int budget = -1; budget <= 10; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError expectedError, actualError;
                const auto expected =
                    raw.runFunctionByName(built.program, "test", args, &expectedError);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), "test", args, &actualError);
                require(expectedError.code == actualError.code
                        && expectedError.message == actualError.message
                        && expectedError.traceback == actualError.traceback,
                    "extended expression: shift/overflow/zero and every budget preserve diagnostics");
                if (expectedError.code == ScriptDiagnosticCode::None) {
                    ScriptScalar a, b;
                    require(ScriptScalar::read(expected, a) && ScriptScalar::read(actual, b)
                            && a.kind() == b.kind()
                            && (a.kind() == ScriptScalar::Kind::Float ? a.floating() == b.floating()
                                                                      : a.integer() == b.integer()),
                        "extended expression: signed bitwise, pow/floor and wide fallback agree");
                }
            }
        }
    }
}

void testBoundMethodStackProof()
{
    {
        Value receiver{100000}, prefix{200000}, boxedArgument{300000};
        ExecutionSlotBuffer stack{&arp::core::defaultCpuMemoryResource()};
        stack.emplace_back(prefix);
        stack.emplace_back(ScriptScalar(7));
        stack.emplace_back(boxedArgument);
        const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
        {
            ExecutionArgumentsView arguments(stack, 2u, receiver);
            auto self = arguments[0], first = arguments[1], second = arguments[2];
            require(arguments.size() == 3u
                    && self.boxed().storageIdentity() == receiver.storageIdentity()
                    && first.scalarPointer<int>() && *first.scalarPointer<int>() == 7
                    && second.boxed().storageIdentity() == boxedArgument.storageIdentity(),
                "method argument view: prepend receiver and preserve ordered inline/boxed arguments");
            arguments.finishConsumption();
        }
        require(stack.size() == 1u && stack[0].boxed().storageIdentity() == prefix.storageIdentity()
                && arp::core::defaultCpuAllocator().stats().allocationCount == before,
            "method argument view: consume only original suffix, retain prefix identity and allocate nothing");
    }
    auto built = arp::scriptvm::test::buildInterpreter(
        "class Base:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "class Derived(Base):\n    def step(self, value: int) -> int:\n        return value + 2\n"
        "def test(n: int, seed: int) -> int:\n    var receiver: Base = Derived()\n"
        "    var i: int = 0\n    var total: int = seed\n    while i < n:\n"
        "        total = (total + receiver.step(i)) % 1000000007\n"
        "        i = i + 1\n    return total\n",
        "method-proof.arp",
        "method-proof");
    require(built.ok(), "bound method proof: inherited override fixture");
    if (!built.ok())
        return;
    const int caller = built.program.funcIndex.at("test");
    for (auto& type : built.program.functions[caller].paramTypes)
        type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "bound method proof: snapshot");
    if (!artifact.valid())
        return;
    require(artifact.hasVerifiedStackBound(&artifact.program().functions[caller]),
        "bound method proof: fallback and all immutable overrides have independent one-result CFG proofs");
    for (Value limit : {Value{5}, Value{0.5}}) {
        for (Value seed : {Value{0}, Value{2147483647}, Value::undefined()}) {
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.push_back(limit);
            args.push_back(seed);
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "bound method proof: bind");
            for (int budget = -1; budget <= 160; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "bound method proof: every caller/callee budget and actual payload failure retains original traceback");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual,
                        *expected.ptr<int>(),
                        "bound method proof: real override dispatch remains authoritative");
            }
            raw.setInstructionBudget(-1);
            owned.setInstructionBudget(-1);
            for (int depth : {0, 1, 3}) {
                raw.setRecursionLimit(depth);
                owned.setRecursionLimit(depth);
                VmError a, b;
                (void)raw.runFunctionByName(built.program, "test", args, &a);
                (void)owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "bound method proof: logical callee recursion admission is preserved");
            }
        }
    }
    for (int rejection = 0; rejection < 3; ++rejection) {
        auto program = built.program;
        auto& function = program.functions[caller];
        if (rejection == 0) {
            // The fixture dispatches Derived, but Base is still a real possible
            // target. A zero-result path there must disqualify the whole caller.
            auto& base = program.functions[program.funcIndex.at("Base.step")];
            base.results = 0;
            base.code.clear();
            base.code.emplace_back(Opcode::Return, 0);
            base.debugLocals.clear();
        } else if (rejection == 1) {
            int callPc = -1;
            for (int pc = 0; pc < static_cast<int>(function.code.size()); ++pc)
                if (function.code[pc].op == Opcode::CallValue)
                    callPc = pc;
            require(callPc >= 0, "bound method proof: fixture contains CallValue");
            function.code.emplace_back(Opcode::Jump, callPc);
        } else {
            program.constructorIndex["Derived"] = program.funcIndex.at("Base.step");
        }
        program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(program);
        ValidatedInterpreterProgram blocked;
        require(ValidatedInterpreterProgram::create(program, blocked).ok(),
            "bound method proof: negative snapshot");
        if (blocked.valid())
            require(!blocked.hasVerifiedStackBound(&blocked.program().functions[caller]),
                "bound method proof: zero-result override, alternate predecessor or explicit constructor retains checked path");
    }
}

void testSelectedArgumentLeafCalls()
{
    for (bool method : {false, true})
        for (bool last : {false, true})
            for (const char* operation : {" + 2", " * 2", " / 0"}) {
                auto& resource = arp::core::defaultCpuMemoryResource();
                arp::core::pmr::String source{&resource};
                source += method
                    ? "class Picker:\n    def step(self, a: int, b: int) -> int:\n        return "
                    : "def step(a: int, b: int) -> int:\n    return ";
                source += last ? "b" : "a";
                source += operation;
                source += "\ndef test(a: int, b: int) -> int:\n";
                if (method)
                    source += "    var object: Picker = Picker()\n";
                source +=
                    "    var i: int = 0\n    var total: int = 7\n    while i < 2:\n        total = total + ";
                source += method ? "object.step(a, b)\n" : "step(a, b)\n";
                source += "        i = i + 1\n    return total\n";
                auto built = arp::scriptvm::test::buildInterpreter(std::move(source),
                    arp::core::pmr::String("selected-leaf.arp", &resource),
                    arp::core::pmr::String("selected-leaf", &resource));
                require(built.ok(), "selected leaf: method/static multi-argument source");
                if (!built.ok())
                    continue;
                const int callee = built.program.funcIndex.at(method ? "Picker.step" : "step");
                built.program.functions[callee].locals = 24;
                for (auto& function : built.program.functions)
                    for (auto& type : function.paramTypes)
                        type = RuntimeType::Any;
                built.program.bytecodeFingerprint =
                    computeInterpreterBytecodeFingerprint(built.program);
                ValidatedInterpreterProgram artifact;
                require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
                    "selected leaf: snapshot");
                if (!artifact.valid())
                    continue;
                const auto* leaf =
                    artifact.scalarExpressions(&artifact.program().functions[callee]);
                require(leaf && leaf->argumentLeaf,
                    "selected leaf: exact used parameter and unused slots independently proved");
                for (const Value& selected : {Value{100000},
                         Value{std::numeric_limits<int>::max()},
                         Value{std::int64_t(100000)},
                         Value{1.5}}) {
                    const Value unused = makeString(std::string_view("unused carrier"));
                    arp::core::pmr::Vector<Value> args{&resource};
                    args.push_back(last ? unused : selected);
                    args.push_back(last ? selected : unused);
                    Vm raw, owned;
                    require(owned.bindProgram(artifact).ok(), "selected leaf: bind");
                    const auto compare = [&] {
                        VmError a, b;
                        const auto expected =
                            raw.runFunctionByName(built.program, "test", args, &a);
                        const auto actual =
                            owned.runFunctionByName(artifact.program(), "test", args, &b);
                        require(a.code == b.code && a.message == b.message
                                && a.traceback == b.traceback,
                            "selected leaf: mixed payload, overflow, zero divisor and every budget retain callee PC/traceback");
                        if (a.code == ScriptDiagnosticCode::None)
                            requireInt(actual,
                                *expected.ptr<int>(),
                                "selected leaf: selected slot, stack prefix and unused argument order");
                        require(args[last ? 1 : 0].storageIdentity() == selected.storageIdentity()
                                && args[last ? 0 : 1].storageIdentity() == unused.storageIdentity(),
                            "selected leaf: used/unused host carrier identity unchanged");
                    };
                    for (int budget = -1; budget <= 55; ++budget) {
                        raw.setInstructionBudget(budget);
                        owned.setInstructionBudget(budget);
                        compare();
                    }
                    raw.setInstructionBudget(-1);
                    owned.setInstructionBudget(-1);
                    for (int depth : {0, 1, 2}) {
                        raw.setRecursionLimit(depth);
                        owned.setRecursionLimit(depth);
                        compare();
                    }
                    raw.setRecursionLimit(-1);
                    owned.setRecursionLimit(-1);
                    for (std::size_t limit : {1u, 2u, 3u}) {
                        raw.setMaxCallArguments(limit);
                        owned.setMaxCallArguments(limit);
                        compare();
                    }
                    raw.setMaxCallArguments(4096u);
                    owned.setMaxCallArguments(4096u);
                    for (std::size_t limit : {8u, 23u, 24u}) {
                        raw.setMaxFrameValues(limit);
                        owned.setMaxFrameValues(limit);
                        compare();
                    }
                    raw.setMaxFrameValues(65536u);
                    owned.setMaxFrameValues(65536u);
                    auto a = arp::core::makeShared<ScriptExecutionSession>();
                    auto b = arp::core::makeShared<ScriptExecutionSession>();
                    raw.setExecutionSession(a);
                    owned.setExecutionSession(b);
                    for (int depth : {1, 2})
                        for (int budget = -1; budget <= 55; ++budget) {
                            a->begin(budget, depth);
                            b->begin(budget, depth);
                            compare();
                            require(a->remainingInstructions == b->remainingInstructions
                                    && a->recursionDepth == 0 && b->recursionDepth == 0,
                                "selected leaf: logical frame and full original instruction cost remain shared");
                        }
                }
            }
    auto built = arp::scriptvm::test::buildInterpreter(
        "def step(a: int, b: int) -> int:\n    return b + 2\n"
        "def test(n: int, a: int, b: int) -> int:\n    var i: int = 0\n    var total: int = 0\n"
        "    while i < n:\n        total = total + step(a, b)\n        i = i + 1\n    return total\n",
        "selected-leaf-quota.arp",
        "selected-leaf-quota");
    require(built.ok(), "selected leaf quota: cold callee fixture");
    if (!built.ok())
        return;
    for (auto& function : built.program.functions)
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "selected leaf quota: snapshot");
    if (!artifact.valid())
        return;
    Vm raw, owned;
    require(owned.bindProgram(artifact).ok(), "selected leaf quota: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{0});
    args.push_back(makeString(std::string_view("unused")));
    args.push_back(Value{100000});
    VmError a, b;
    requireInt(raw.runFunctionByName(built.program, "test", args, &a),
        0,
        "selected leaf quota: warm caller only");
    requireInt(owned.runFunctionByName(artifact.program(), "test", args, &b),
        0,
        "selected leaf quota: warm owned caller only");
    args[0] = Value{1};
    raw.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1u));
    owned.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1u));
    (void)raw.runFunctionByName(built.program, "test", args, &a);
    (void)owned.runFunctionByName(artifact.program(), "test", args, &b);
    require(a.code == ScriptDiagnosticCode::ResourceLimitExceeded && a.code == b.code
            && a.message == b.message && a.traceback == b.traceback,
        "selected leaf quota: active quota retains original cold callee allocation and failure PC");
}

void testClosedCalculationLoop()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(value: int, n: int) -> int:\n    var total: int = 0\n    var i: int = 0\n"
        "    while i < n:\n        total = (total + (value % 7) * i + value) % 103\n"
        "        i = i + 1\n    return total\n",
        "calculation-loop.arp",
        "calculation-loop");
    require(built.ok(), "calculation loop: fixture compiles");
    if (!built.ok())
        return;
    for (auto& function : built.program.functions)
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "calculation loop: snapshot");
    if (!artifact.valid())
        return;
    const auto& caller = artifact.program().functions[artifact.program().funcIndex.at("test")];
    const auto* plan = artifact.scalarExpressions(&caller);
    require(artifact.hasScalarDispatch(&caller),
        "calculation loop: independent scalar dispatcher is exercised");
    require(plan
            && std::any_of(plan->entries.begin(),
                plan->entries.end(),
                [](const auto& entry) {
                    return entry.calculationLoop && entry.count > 5;
                }),
        "calculation loop: longer checked expression and original self edge proved");
    int backing = 100000;
    const int cv = 100000;
    auto& resource = core::defaultCpuMemoryResource();
    for (Value input : {Value{100000},
             Value{std::numeric_limits<int>::max()},
             Value{-17},
             Value{std::int64_t(100000)},
             Value{1.5},
             Value{cv},
             Value::native_reference(&backing)}) {
        core::pmr::Vector<Value> args{&resource};
        args.push_back(input);
        args.push_back(Value{3});
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "calculation loop: bind");
        auto a = core::makeShared<ScriptExecutionSession>(),
             b = core::makeShared<ScriptExecutionSession>();
        for (bool session : {false, true}) {
            raw.setExecutionSession(session ? a : nullptr);
            owned.setExecutionSession(session ? b : nullptr);
            for (int budget = -1; budget <= 140; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                a->begin(budget, 2);
                b->begin(budget, 2);
                VmError x, y;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &x);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &y);
                require(x.code == y.code && x.message == y.message && x.traceback == y.traceback,
                    "calculation loop: wrong actual payload, native/cv and every budget replay exact PC");
                if (x.code == ScriptDiagnosticCode::None)
                    requireInt(actual,
                        *expected.ptr<int>(),
                        "calculation loop: each original checked result");
                if (session)
                    require(a->remainingInstructions == b->remainingInstructions
                            && a->recursionDepth == 0 && b->recursionDepth == 0,
                        "calculation loop: original instruction charge and session frame lifetime");
                require(args[0].storageIdentity() == input.storageIdentity(),
                    "calculation loop: imported carrier preserved");
            }
        }
        raw.setExecutionSession({});
        owned.setExecutionSession({});
        raw.setInstructionBudget(-1);
        owned.setInstructionBudget(-1);
        auto qa = core::makeShared<core::CpuMemoryQuota>(1),
             qb = core::makeShared<core::CpuMemoryQuota>(1);
        raw.setMemoryQuota(qa);
        owned.setMemoryQuota(qb);
        VmError x, y;
        raw.runFunctionByName(built.program, "test", args, &x);
        owned.runFunctionByName(artifact.program(), "test", args, &y);
        require(x.code == y.code && x.message == y.message && x.traceback == y.traceback
                && qa->liveBytes() == qb->liveBytes(),
            "calculation loop: quota retains original allocation/failure order");
    }
}

void testClosedClosureLeafLoop()
{
    auto source = arp::scriptvm::test::buildInterpreter(
        "def test(n: int) -> int:\n    var offset: int = 3\n"
        "    def step(value: int) -> int:\n        return value + offset\n"
        "    var i: int = 0\n    var total: int = 0\n    while i < n:\n"
        "        total = (total + step(i)) % 103\n        i = i + 1\n    return total\n",
        "source-closure-loop.arp",
        "source-closure-loop");
    require(source.ok(), "closed closure: real source lowering");
    if (source.ok()) {
        ValidatedInterpreterProgram lowered;
        require(ValidatedInterpreterProgram::create(source.program, lowered).ok(),
            "closed closure: source snapshot");
        if (lowered.valid()) {
            const auto* actual = lowered.independentScalarExpressions(
                &lowered.program().functions[lowered.program().funcIndex.at("test")]);
            require(actual && actual->hasClosureLoop,
                "closed closure: real lowered source uses the independent call interval");
            Vm vm;
            require(vm.bindProgram(lowered).ok(), "closed closure: source binding");
            core::pmr::Vector<Value> inputs{&core::defaultCpuMemoryResource()};
            inputs.push_back(Value{5});
            VmError error;
            requireInt(vm.runFunctionByName(lowered.program(), "test", inputs, &error),
                25,
                "closed closure: real source result");
            require(
                error.code == ScriptDiagnosticCode::None, "closed closure: real source executes");
        }
    }
    auto built = arp::scriptvm::test::buildInterpreter(
        "def leaf(value: int) -> int:\n    return value + 1\n"
        "def test(callback: int, value: int, n: int) -> int:\n    return value\n"
        "def seed(value: int, callback: int) -> int:\n    return value\n",
        "closure-loop.arp",
        "closure-loop");
    require(built.ok(), "closed closure: fixture source");
    if (!built.ok())
        return;
    const int target = built.program.funcIndex.at("leaf");
    auto& leaf = built.program.functions[target];
    leaf.captureCount = 1;
    leaf.locals = 16;
    leaf.code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadCapture, 0},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::ReturnInt}};
    auto& caller = built.program.functions[built.program.funcIndex.at("test")];
    caller.locals = 5;
    caller.maxStack = 3;
    caller.debugLocals.clear();
    caller.code = {Instruction{Opcode::LoadInt, 0, 0, 0},
        Instruction{Opcode::StoreLocal, 3},
        Instruction{Opcode::LoadInt, 0, 0, 0},
        Instruction{Opcode::StoreLocal, 4},
        Instruction{Opcode::LoadLocal, 4},
        Instruction{Opcode::LoadLocal, 2},
        Instruction{Opcode::IntCmpLt},
        Instruction{Opcode::JumpIfFalse, 18},
        Instruction{Opcode::LoadLocal, 3},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::CallValue, 0, 1},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::LoadInt, 0, 0, 103},
        Instruction{Opcode::IntMod},
        Instruction{Opcode::StoreLocal, 3},
        Instruction{Opcode::LocalAddIntConst, 4, 0, 1},
        Instruction{Opcode::Jump, 4},
        Instruction{Opcode::LoadLocal, 3},
        Instruction{Opcode::ReturnInt}};
    auto& seed = built.program.functions[built.program.funcIndex.at("seed")];
    seed.maxStack = 2;
    seed.code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::CallValue, 0, 1},
        Instruction{Opcode::ReturnInt}};
    for (auto& function : built.program.functions) {
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
        for (std::size_t pc = 0; pc < function.code.size(); ++pc)
            function.code[pc].pos =
                SourcePos(core::pmr::String("closure-loop.arp"), static_cast<int>(100 + pc), 1);
    }
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "closed closure: immutable snapshot");
    if (!artifact.valid())
        return;
    const auto& function = artifact.program().functions[artifact.program().funcIndex.at("test")];
    const auto* plan = artifact.independentScalarExpressions(&function);
    require(!artifact.hasVerifiedStackBound(&function) && plan && plan->hasClosureLoop
            && std::any_of(plan->entries.begin(),
                plan->entries.end(),
                [](const auto& entry) {
                    return entry.terminal == ScalarExpressionPlan::Terminal::ClosureLoop
                        && entry.count == 0;
                }),
        "closed closure: independent empty-stack call interval has no whole-caller proof");
    auto cell = core::makeShared<ScriptCell>(Value{3});
    ScriptClosure closure{target, {}};
    closure.captures.push_back(cell);
    Value callable{std::move(closure)};
    auto& resource = core::defaultCpuMemoryResource();
    core::pmr::Vector<Value> args{&resource};
    args.push_back(callable);
    args.push_back(Value{13});
    args.push_back(Value{3});
    Vm raw, owned;
    require(owned.bindProgram(artifact).ok(), "closed closure: bind");
    auto a = core::makeShared<ScriptExecutionSession>(),
         b = core::makeShared<ScriptExecutionSession>();
    const auto compare = [&] {
        VmError x, y;
        const auto expected = raw.runFunctionByName(built.program, "test", args, &x);
        const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &y);
        require(x.code == y.code && x.message == y.message && x.traceback == y.traceback,
            "closed closure: every original error PC and traceback replay");
        if (x.code == ScriptDiagnosticCode::None)
            requireInt(actual, *expected.ptr<int>(), "closed closure: checked live result");
        require(a->remainingInstructions == b->remainingInstructions && a->recursionDepth == 0
                && b->recursionDepth == 0,
            "closed closure: budget and logical frame scope");
    };
    int native = 13;
    const int cv = 13;
    for (Value input : {Value{13},
             Value{std::numeric_limits<int>::max()},
             Value{std::numeric_limits<int>::max() - 1},
             Value{std::int64_t(13)},
             Value{1.5},
             Value{cv},
             Value::native_reference(&native)}) {
        args[1] = input;
        for (Value capture : {Value{3},
                 Value{0},
                 Value{-17},
                 Value{std::int64_t(3)},
                 Value{1.5},
                 Value{cv},
                 Value::native_reference(&native)}) {
            cell->value = capture;
            for (bool session : {false, true}) {
                raw.setExecutionSession(session ? a : nullptr);
                owned.setExecutionSession(session ? b : nullptr);
                for (int budget = -1; budget <= 70; ++budget) {
                    raw.setInstructionBudget(budget);
                    owned.setInstructionBudget(budget);
                    a->begin(budget, 2);
                    b->begin(budget, 2);
                    compare();
                    require(args[1].storageIdentity() == input.storageIdentity()
                            && cell->value.storageIdentity() == capture.storageIdentity(),
                        "closed closure: actual argument/capture carriers and live rebinding");
                }
            }
        }
    }
    raw.setExecutionSession({});
    owned.setExecutionSession({});
    raw.setInstructionBudget(-1);
    owned.setInstructionBudget(-1);
    args[1] = Value{13};
    cell->value = Value{3};
    for (int limit : {0, 1, 2}) {
        raw.setRecursionLimit(limit);
        owned.setRecursionLimit(limit);
        compare();
    }
    raw.setRecursionLimit(-1);
    owned.setRecursionLimit(-1);
    for (std::size_t limit : {2u, 5u, 15u, 16u}) {
        raw.setMaxFrameValues(limit);
        owned.setMaxFrameValues(limit);
        compare();
    }
    raw.setMaxFrameValues(65536);
    owned.setMaxFrameValues(65536);
    for (std::size_t limit : {0u, 1u, 3u}) {
        raw.setMaxCallArguments(limit);
        owned.setMaxCallArguments(limit);
        compare();
    }
    raw.setMaxCallArguments(1024);
    owned.setMaxCallArguments(1024);
    for (int state = 0; state != 4; ++state) {
        auto* actual = callable.ptr<ScriptClosure>();
        actual->captures.assign(1, cell);
        actual->funcIndex = target;
        if (state == 0)
            actual->captures.clear();
        if (state == 1)
            actual->captures[0].reset();
        if (state == 2)
            actual->funcIndex = -1;
        if (state == 3)
            actual->vmOwner = core::makeShared<Vm>();
        compare();
        actual->vmOwner.reset();
    }
    callable.ptr<ScriptClosure>()->captures.assign(1, cell);
    callable.ptr<ScriptClosure>()->funcIndex = target;
    const ScriptClosure constClosure(*callable.ptr<ScriptClosure>());
    for (Value carrier :
        {Value{constClosure}, Value::native_reference(callable.ptr<ScriptClosure>())}) {
        args[0] = carrier;
        compare();
    }
    args[0] = callable;
    for (std::size_t quotaBytes : {1u, 1024u * 1024u}) {
        Vm first, second;
        require(second.bindProgram(artifact).ok(), "closed closure: quota bind");
        auto qa = core::makeShared<core::CpuMemoryQuota>(quotaBytes),
             qb = core::makeShared<core::CpuMemoryQuota>(quotaBytes);
        first.setMemoryQuota(qa);
        second.setMemoryQuota(qb);
        for (int attempt = 0; attempt != 2; ++attempt) {
            VmError x, y;
            const auto expected = first.runFunctionByName(built.program, "test", args, &x);
            const auto actual = second.runFunctionByName(artifact.program(), "test", args, &y);
            require(x.code == y.code && x.message == y.message && x.traceback == y.traceback
                    && qa->liveBytes() == qb->liveBytes(),
                "closed closure: original cold/warm quota allocation and failure order");
            if (x.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "closed closure: quota result");
        }
    }

    // Releasing a retained argument may change a capture before the new call.
    // Seed the original indirect argument buffer with an owned host object.
    struct ReleaseState {
        std::shared_ptr<ScriptCell> cell;
        bool armed = false;
        int count = 0;
    };

    struct ReleaseObserver {
        std::shared_ptr<ReleaseState> state;

        ~ReleaseObserver()
        {
            if (state && state->armed) {
                state->cell->value = Value{17};
                ++state->count;
            }
        }
    };

    for (bool validated : {false, true}) {
        Vm vm;
        if (validated)
            require(vm.bindProgram(artifact).ok(), "closed closure: retained argument bind");
        const auto& program = validated ? artifact.program() : built.program;
        auto state = core::makeShared<ReleaseState>();
        state->cell = cell;
        core::pmr::Vector<Value> seedArgs{&resource};
        seedArgs.push_back(Value{ReleaseObserver{state}});
        seedArgs.push_back(callable);
        VmError error;
        (void)vm.runFunctionByName(program, "seed", seedArgs, &error);
        require(error.code == ScriptDiagnosticCode::BadCast,
            "closed closure: original failed callee retains its host argument");
        seedArgs.clear();
        cell->value = Value{3};
        state->armed = true;
        VmError resumed;
        requireInt(vm.runFunctionByName(program, "test", args, &resumed),
            90,
            "closed closure: previous host argument releases before each actual capture read");
        require(resumed.code == ScriptDiagnosticCode::None && state->count == 1,
            "closed closure: no repeated or reordered host destructor");
        state->armed = false;
    }
    for (int replacement : {0, 4}) {
        auto rejected = built.program;
        rejected.functions[rejected.funcIndex.at("test")].code[15].a = replacement;
        rejected.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(rejected);
        ValidatedInterpreterProgram other;
        require(ValidatedInterpreterProgram::create(rejected, other).ok(),
            "closed closure: overwritten callable/step fixture");
        if (other.valid()) {
            const auto* candidate = other.independentScalarExpressions(
                &other.program().functions[other.program().funcIndex.at("test")]);
            require(!candidate || !candidate->hasClosureLoop,
                "closed closure: no callable replacement or nonindependent step admitted");
        }
    }
}

void testClosedMethodLeafLoop()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "class ClosedMethodBase:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "class ClosedMethodOther:\n    def step(self, value: int) -> int:\n        return value + 3\n"
        "class ClosedMethodNonleaf:\n    def step(self, value: int) -> int:\n"
        "        if value < 0:\n            return value\n        return value + 3\n"
        "def test(object: ClosedMethodBase, value: int, n: int) -> int:\n"
        "    var total: int = 0\n    var i: int = 0\n    while i < n:\n"
        "        total = (total + object.step(value)) % 1000000007\n"
        "        i = i + 1\n    return total\n",
        "closed-method.arp",
        "closed-method");
    require(built.ok(), "closed method: fixture compiles");
    if (!built.ok())
        return;
    for (auto& function : built.program.functions) {
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
        for (auto& name : function.paramClassNames)
            name.clear();
    }
    built.program.functions[built.program.funcIndex.at("ClosedMethodOther.step")].locals = 24;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "closed method: immutable snapshot");
    if (!artifact.valid())
        return;
    const auto& caller = artifact.program().functions[artifact.program().funcIndex.at("test")];
    const auto* plan = artifact.scalarExpressions(&caller);
    require(plan && plan->hasMethodLoop
            && std::any_of(plan->entries.begin(),
                plan->entries.end(),
                [](const auto& entry) {
                    return entry.terminal == ScalarExpressionPlan::Terminal::MethodLoop
                        && entry.count == 0;
                }),
        "closed method: original receiver/argument/call and independent tail self-edge proved");
    auto& resource = arp::core::defaultCpuMemoryResource();
    core::pmr::Vector<core::pmr::String> fields{&resource};
    const core::pmr::String classA("ClosedMethodBase", &resource),
        classB("ClosedMethodOther", &resource), classNonleaf("ClosedMethodNonleaf", &resource);
    auto created = ScriptSemanticRuntime::makeScriptInstance(classA, fields);
    require(created.ok(), "closed method: actual receiver created");
    if (!created.ok())
        return;
    auto* classInfo = arp::reflection::db::instance().find_class(classA);
    require(classInfo != nullptr, "closed method: actual class metadata exists");
    if (!classInfo)
        return;
    const arp::reflection::instance cv(*created.value.ptr<arp::reflection::instance>());
    Value native = Value::native_reference(created.value.ptr<arp::reflection::instance>());
    for (const auto& actualClass : {classA, classB, classNonleaf}) {
        classInfo->set_name(actualClass);
        for (Value receiver :
            {created.value, Value{cv}, native, Value{arp::reflection::instance(nullptr)}})
            for (Value input : {Value{100000},
                     Value{std::numeric_limits<int>::max()},
                     Value{std::int64_t(100000)},
                     Value{1.5}}) {
                core::pmr::Vector<Value> args{&resource};
                args.push_back(receiver);
                args.push_back(input);
                args.push_back(Value{3});
                Vm raw, owned;
                require(owned.bindProgram(artifact).ok(), "closed method: bind");
                const auto compare = [&] {
                    VmError a, b;
                    const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                    const auto actual =
                        owned.runFunctionByName(artifact.program(), "test", args, &b);
                    require(
                        a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                        "closed method: override/cv/native/null/wrong width/overflow/budget exact PC and traceback");
                    if (a.code == ScriptDiagnosticCode::None)
                        requireInt(actual,
                            *expected.ptr<int>(),
                            "closed method: same actual dispatch and live argument result");
                    require(args[0].storageIdentity() == receiver.storageIdentity()
                            && args[1].storageIdentity() == input.storageIdentity(),
                        "closed method: actual receiver and argument carriers retained");
                };
                for (int budget = -1; budget <= 100; ++budget) {
                    raw.setInstructionBudget(budget);
                    owned.setInstructionBudget(budget);
                    compare();
                }
                raw.setInstructionBudget(-1);
                owned.setInstructionBudget(-1);
                for (int limit : {0, 1, 2}) {
                    raw.setRecursionLimit(limit);
                    owned.setRecursionLimit(limit);
                    compare();
                }
                raw.setRecursionLimit(-1);
                owned.setRecursionLimit(-1);
                for (std::size_t limit : {1u, 2u, 3u}) {
                    raw.setMaxCallArguments(limit);
                    owned.setMaxCallArguments(limit);
                    compare();
                }
                raw.setMaxCallArguments(1024);
                owned.setMaxCallArguments(1024);
                for (std::size_t limit : {8u, 23u, 24u}) {
                    raw.setMaxFrameValues(limit);
                    owned.setMaxFrameValues(limit);
                    compare();
                }
                raw.setMaxFrameValues(65536);
                owned.setMaxFrameValues(65536);
                auto a = core::makeShared<ScriptExecutionSession>(),
                     b = core::makeShared<ScriptExecutionSession>();
                raw.setExecutionSession(a);
                owned.setExecutionSession(b);
                for (int depth : {1, 2})
                    for (int budget = -1; budget <= 100; ++budget) {
                        a->begin(budget, depth);
                        b->begin(budget, depth);
                        compare();
                        require(a->remainingInstructions == b->remainingInstructions
                                && a->recursionDepth == 0 && b->recursionDepth == 0,
                            "closed method: every original instruction and logical call charged to shared session");
                    }
                raw.setExecutionSession({});
                owned.setExecutionSession({});
                auto qa = core::makeShared<core::CpuMemoryQuota>(1),
                     qb = core::makeShared<core::CpuMemoryQuota>(1);
                raw.setMemoryQuota(qa);
                owned.setMemoryQuota(qb);
                compare();
                require(qa->liveBytes() == qb->liveBytes(),
                    "closed method: quota retains original allocation order");
            }
    }
    classInfo->set_name(classA);
}

void testMethodLoopReplayBoundaries()
{
    const char* sources[] = {
        "class MethodReplay:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "def test() -> int:\n    var object: MethodReplay = MethodReplay()\n"
        "    var other: MethodReplay = MethodReplay()\n    var total: int = 0\n    var i: int = 0\n"
        "    while i < 3:\n        total = total + object.step(i)\n"
        "        object = other\n        i = i + 1\n    return total\n",
        "class MethodReplay:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "def test() -> int:\n    var object: MethodReplay = MethodReplay()\n"
        "    var total: int = 0\n    var i: int = 0\n    var zero: int = 0\n"
        "    while i < 3:\n        total = (total / zero) + object.step(i)\n"
        "        i = i + 1\n    return total\n",
        "class MethodReplay:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "def test() -> int:\n    var object: MethodReplay = MethodReplay()\n"
        "    var total: int = 0\n    var i: int = 0\n"
        "    while i < 3:\n        total = total + object.step(i)\n"
        "        if total > 1:\n            break\n        i = i + 1\n    return total\n",
        "class MethodReplay:\n    def step(self, value: int) -> int:\n        return value + 1\n"
        "def test() -> int:\n    var object: MethodReplay = MethodReplay()\n"
        "    var total: int = 0\n    var i: int = 0\n"
        "    try:\n        while i < 3:\n            total = total + object.step(i)\n"
        "            i = i + 1\n    except:\n        return 100\n    return total\n",
        "class MethodReplay:\n    def step(self, a: int, b: int) -> int:\n        return a - b\n"
        "def test() -> int:\n    var object: MethodReplay = MethodReplay()\n"
        "    var total: int = 0\n    var i: int = 0\n"
        "    while i < 3:\n        total = total + object.step(i, 1)\n"
        "        i = i + 1\n    return total\n"};
    for (const auto* source : sources) {
        auto built =
            arp::scriptvm::test::buildInterpreter(source, "method-replay.arp", "method-replay");
        require(built.ok(), "method replay: boundary fixture compiles");
        if (!built.ok())
            continue;
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "method replay: snapshot");
        if (!artifact.valid())
            continue;
        const auto& caller = artifact.program().functions[artifact.program().funcIndex.at("test")];
        const auto* plan = artifact.scalarExpressions(&caller);
        require(!plan
                || (!plan->hasMethodLoop
                    && std::none_of(plan->entries.begin(),
                        plan->entries.end(),
                        [](const auto& entry) {
                            return entry.terminal == ScalarExpressionPlan::Terminal::MethodLoop;
                        })),
            "method replay: receiver writes, preceding arithmetic, branch, handler and multi-argument calls are not closed");
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "method replay: bind");
        for (int budget = -1; budget <= 100; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", {}, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", {}, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "method replay: boundary errors and budgets retain original PC and traceback");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(
                    actual, *expected.ptr<int>(), "method replay: original effects and result");
        }
    }
}

void testMethodArgumentTransfer()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "class Difference:\n    def step(self, a: int, b: int) -> int:\n        return a - b\n"
        "def test(a: int, b: int) -> int:\n    var receiver: Difference = Difference()\n"
        "    return receiver.step(a, b)\n",
        "method-arguments.arp",
        "method-arguments");
    require(built.ok(), "method transfer: multi-argument fixture");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "method transfer: snapshot");
    if (!artifact.valid())
        return;
    Value first{100000}, second{3};
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(first);
    args.push_back(second);
    Vm raw, owned;
    require(owned.bindProgram(artifact).ok(), "method transfer: bind");
    const auto compare = [&] {
        VmError a, b;
        const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
        const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
        require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
            "method transfer: budget/receiver-inclusive argument count/frame admission preserve exact diagnostics");
        if (a.code == ScriptDiagnosticCode::None) {
            requireInt(expected, 99997, "method transfer: canonical argument order");
            requireInt(actual, 99997, "method transfer: borrowed argument order");
        }
    };
    for (int budget = -1; budget <= 30; ++budget) {
        raw.setInstructionBudget(budget);
        owned.setInstructionBudget(budget);
        compare();
    }
    raw.setInstructionBudget(-1);
    owned.setInstructionBudget(-1);
    for (std::size_t limit : {0u, 1u, 2u, 3u}) {
        raw.setMaxCallArguments(limit);
        owned.setMaxCallArguments(limit);
        compare();
    }
    raw.setMaxCallArguments(1024u);
    owned.setMaxCallArguments(1024u);
    for (std::size_t limit : {0u, 1u, 2u, 8u}) {
        raw.setMaxFrameValues(limit);
        owned.setMaxFrameValues(limit);
        compare();
    }
    raw.setMaxFrameValues(65536u);
    owned.setMaxFrameValues(65536u);
    for (int depth : {0, 1, 2}) {
        auto a = arp::core::makeShared<ScriptExecutionSession>();
        auto b = arp::core::makeShared<ScriptExecutionSession>();
        a->begin(100, depth);
        b->begin(100, depth);
        raw.setExecutionSession(a);
        owned.setExecutionSession(b);
        compare();
        require(a->remainingInstructions == b->remainingInstructions,
            "method transfer: shared session consumes original instruction count");
    }
    require(first.storageIdentity() == args[0].storageIdentity() && *first.ptr<int>() == 100000
            && second.storageIdentity() == args[1].storageIdentity() && *second.ptr<int>() == 3,
        "method transfer: imported arguments retain original host aliases and payloads");
}

void testStaticCallStackProof()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def fib(n: int) -> int:\n    if n < 2:\n        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "def sum(a: int, b: int) -> int:\n    return a + b\n"
        "def middle(n: int) -> int:\n    return sum(fib(n), 3)\n"
        "def test(n: int) -> int:\n    return middle(n) + 2\n",
        "static-call-proof.arp",
        "static-call-proof");
    require(built.ok(), "static call: recursive and multiple-argument source");
    if (!built.ok())
        return;
    for (auto& function : built.program.functions)
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(
        ValidatedInterpreterProgram::create(built.program, artifact).ok(), "static call: snapshot");
    if (!artifact.valid())
        return;
    for (const auto& function : artifact.program().functions)
        require(artifact.hasVerifiedStackBound(&function),
            "static call: full recursive dependency and result CFG proved");
    for (bool sharedSession : {false, true})
        for (int limit : {1, 3, 16}) {
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "static call: bind");
            auto rawSession = arp::core::makeShared<ScriptExecutionSession>();
            auto ownedSession = arp::core::makeShared<ScriptExecutionSession>();
            if (sharedSession) {
                raw.setExecutionSession(rawSession);
                owned.setExecutionSession(ownedSession);
            }
            raw.setRecursionLimit(limit);
            owned.setRecursionLimit(limit);
            for (Value input : {Value{4}, Value{1.5}, makeString(std::string_view("bad"))}) {
                arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
                args.push_back(input);
                for (int budget = -1; budget <= 160; ++budget) {
                    raw.setInstructionBudget(budget);
                    owned.setInstructionBudget(budget);
                    rawSession->begin(budget, limit);
                    ownedSession->begin(budget, limit);
                    VmError a, b;
                    const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                    const auto actual =
                        owned.runFunctionByName(artifact.program(), "test", args, &b);
                    require(
                        a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                        "static call: recursive budget, actual type and recursion failures preserve full traceback");
                    if (a.code == ScriptDiagnosticCode::None)
                        requireInt(actual,
                            *expected.ptr<int>(),
                            "static call: ordinary multi-frame result");
                    require(rawSession->remainingInstructions == ownedSession->remainingInstructions
                            && rawSession->recursionDepth == 0 && ownedSession->recursionDepth == 0,
                        "static call: session budget and logical frame depth retained");
                }
            }
        }
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto modified = built.program;
        auto& callee = modified.functions[modified.funcIndex.at("fib")];
        if (mutation == 0) {
            for (auto& instruction : callee.code)
                if (instruction.op == Opcode::ReturnInt) {
                    instruction.op = Opcode::Return;
                    instruction.a = 0;
                    break;
                }
        } else if (mutation == 1)
            callee.maxStack = 0;
        else if (mutation == 2)
            callee.isGenerator = true;
        else if (mutation == 3)
            callee.results = 0;
        else if (mutation == 4)
            callee.code.back().op = Opcode::Throw;
        else
            for (auto& instruction : callee.code)
                if (instruction.op == Opcode::Call) {
                    instruction.b = 0;
                    break;
                }
        const auto proofs = InterpreterBytecodeLowerer::analyzeProgramStacks(modified);
        if (mutation == 2) {
            require(proofs[modified.funcIndex.at("test")].status == TypedIrScalarStackStatus::Valid
                    && proofs[modified.funcIndex.at("middle")].status
                        == TypedIrScalarStackStatus::Valid,
                "static call: generator construction has one result independently of producer body");
            require(!InterpreterBytecodeLowerer::isScalarDispatch(
                        modified.functions[modified.funcIndex.at("middle")], &modified),
                "static call: generator factory cannot enter ordinary-call compact driver");
        } else
            require(proofs[modified.funcIndex.at("test")].status != TypedIrScalarStackStatus::Valid
                    && proofs[modified.funcIndex.at("middle")].status
                        != TypedIrScalarStackStatus::Valid,
                "static call: zero/mismatched result, malformed CFG/arity and exception propagate rejection");
    }
}

void testStaticCallArgumentTransfer()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    const arp::core::pmr::String literal("borrowed call argument", &resource);
    Value alias{73};
    ExecutionSlotBuffer stack{&resource};
    stack.emplace_back(ScriptScalar(19));
    stack.emplace_back(ExecutionSlot::borrowLiteral(literal));
    stack.emplace_back(alias);
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    {
        const ExecutionArgumentsView arguments(stack, 2);
        auto text = arguments[0];
        auto reference = arguments[1];
        arguments.finishConsumption();
        std::string_view view;
        require(text.stringView(view) && view == literal,
            "call transfer: literal lifetime remains borrowed during inline handover");
        require(reference.ptr<int>() == alias.ptr<int>(),
            "call transfer: exact imported carrier retained");
        require(stack.size() == 1 && stack[0].scalarPointer<int>()
                && *stack[0].scalarPointer<int>() == 19,
            "call transfer: stack prefix remains intact");
        require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
            "call transfer: no scratch buffer or literal materialization allocation");
    }
    stack.emplace_back(alias);
    {
        const ExecutionArgumentsView rejected(stack, 1);
    }
    require(
        stack.size() == 1, "call transfer: admission rejection consumes arguments exactly once");

    auto built = arp::scriptvm::test::buildInterpreter(
        "def even(n: int) -> int:\n    if n < 1:\n        return 1\n    return odd(n - 1)\n"
        "def odd(n: int) -> int:\n    if n < 1:\n        return 0\n    return even(n - 1)\n"
        "def pair(a: int, b: int) -> int:\n    return a * 10 + b\n"
        "def test(n: int) -> int:\n    return 7 + pair(even(n), odd(n))\n",
        "call-transfer.arp",
        "call-transfer");
    require(built.ok(), "call transfer: mutual recursion and preserved prefix source");
    if (!built.ok())
        return;
    for (auto& function : built.program.functions)
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "call transfer: snapshot");
    if (!artifact.valid())
        return;
    for (const auto& function : artifact.program().functions)
        require(artifact.hasVerifiedStackBound(&function),
            "call transfer: mutual SCC proved independently");
    for (const char* name : {"even", "odd", "test"})
        require(artifact.hasScalarDispatch(
                    &artifact.program().functions[artifact.program().funcIndex.at(name)]),
            "scalar call: immutable proof admits acyclic, call-bearing functions");
    require(artifact.hasScalarDispatch(
                &artifact.program().functions[artifact.program().funcIndex.at("pair")]),
        "scalar call: arithmetic leaves share the small scalar dispatcher");
    for (bool session : {false, true})
        for (int limit : {1, 3, 16}) {
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "call transfer: bind");
            auto aSession = arp::core::makeShared<ScriptExecutionSession>();
            auto bSession = arp::core::makeShared<ScriptExecutionSession>();
            if (session) {
                raw.setExecutionSession(aSession);
                owned.setExecutionSession(bSession);
            }
            raw.setRecursionLimit(limit);
            owned.setRecursionLimit(limit);
            for (Value input : {Value{3}, Value{1.5}, makeString(std::string_view("wrong"))}) {
                arp::core::pmr::Vector<Value> args{&resource};
                args.push_back(input);
                for (int budget = -1; budget <= 90; ++budget) {
                    raw.setInstructionBudget(budget);
                    owned.setInstructionBudget(budget);
                    aSession->begin(budget, limit);
                    bSession->begin(budget, limit);
                    VmError a, b;
                    auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                    auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                    require(
                        a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                        "call transfer: mutual recursion, wrong payload, budget and recursion diagnostics match");
                    if (a.code == ScriptDiagnosticCode::None)
                        requireInt(actual,
                            *expected.ptr<int>(),
                            "call transfer: ordered arguments and stack prefix");
                    require(aSession->remainingInstructions == bSession->remainingInstructions
                            && aSession->recursionDepth == 0 && bSession->recursionDepth == 0,
                        "call transfer: session accounting matches");
                }
            }
        }
    auto rejected = built.program;
    rejected.functions[rejected.funcIndex.at("odd")].results = 0;
    const auto proofs = InterpreterBytecodeLowerer::analyzeProgramStacks(rejected);
    for (const char* name : {"even", "odd", "test"})
        require(proofs[rejected.funcIndex.at(name)].status != TypedIrScalarStackStatus::Valid,
            "call transfer: rejection propagates around mutual SCC and into caller");
}

void testStaticSlotReturnIdentity()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def leaf(x: any) -> any:\n    return x\n"
        "def forward(x: any, n: int) -> any:\n    if n < 1:\n        return leaf(x)\n"
        "    return forward(x, n - 1)\n"
        "def test(x: any) -> any:\n    return forward(x, 3)\n",
        "static-slot-return.arp",
        "static-slot-return");
    require(built.ok(), "slot return: actual recursive forwarding source");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(
        ValidatedInterpreterProgram::create(built.program, artifact).ok(), "slot return: snapshot");
    if (!artifact.valid())
        return;
    for (const char* name : {"leaf", "forward", "test"})
        require(artifact.hasScalarDispatch(
                    &artifact.program().functions[artifact.program().funcIndex.at(name)]),
            "slot return: scalar caller, recursive callee and actual Value leaf admitted");
    auto& resource = core::defaultCpuMemoryResource();
    core::pmr::String nativeText("native before", &resource);
    const core::pmr::String cvText("constant payload", &resource);
    const Value inputs[]{makeString(std::string_view("owned payload")),
        Value{cvText},
        Value::native_reference(&nativeText),
        makeNil(),
        Value{std::int64_t(2147483648LL)},
        Value{100000}};
    for (bool session : {false, true})
        for (const auto& input : inputs) {
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "slot return: bind");
            auto aSession = core::makeShared<ScriptExecutionSession>();
            auto bSession = core::makeShared<ScriptExecutionSession>();
            if (session) {
                raw.setExecutionSession(aSession);
                owned.setExecutionSession(bSession);
            }
            core::pmr::Vector<Value> args{&resource};
            args.push_back(input);
            for (int budget = -1; budget <= 80; ++budget) {
                nativeText = budget % 2 ? "live native one" : "live native two";
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                aSession->begin(budget, 16);
                bSession->begin(budget, 16);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "slot return: each instruction budget preserves precise diagnostics");
                if (a.code == ScriptDiagnosticCode::None) {
                    require(actual.storageIdentity() == input.storageIdentity()
                            && expected.storageIdentity() == input.storageIdentity(),
                        "slot return: string/cv/native/nil/wide/int exact carrier preserved through recursive slots");
                    if (input.is<core::pmr::String>()) {
                        const auto* text = checkedValuePayload<core::pmr::String>(actual);
                        const auto* original = checkedValuePayload<core::pmr::String>(input);
                        require(text && original && *text == *original,
                            "slot return: actual live string bytes");
                    }
                }
                require(aSession->remainingInstructions == bSession->remainingInstructions
                        && aSession->recursionDepth == 0 && bSession->recursionDepth == 0,
                    "slot return: shared session accounting and logical unwind");
            }
            raw.setInstructionBudget(-1);
            owned.setInstructionBudget(-1);
            raw.setExecutionSession(nullptr);
            owned.setExecutionSession(nullptr);
            auto quotaA = core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
            auto quotaB = core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
            raw.setMemoryQuota(quotaA);
            owned.setMemoryQuota(quotaB);
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.code == ScriptDiagnosticCode::None
                    && actual.storageIdentity() == expected.storageIdentity(),
                "slot return: actual quota path retains alias");
            if (input.storageIdentity() == inputs[0].storageIdentity()) {
                *args[0].ptr<core::pmr::String>() = "mutated after return";
                requireStr(actual,
                    "mutated after return",
                    "slot return: returned owned alias remains live");
            }
        }
}

void testGuardedArgumentReturns()
{
    auto& resource = core::defaultCpuMemoryResource();
    const char* comparisons[]{"<", "<=", ">", ">=", "==", "!="};
    for (const auto* comparison : comparisons) {
        core::pmr::String source("def select(x: int, y: int) -> int:\n    if x ", &resource);
        source += comparison;
        source += " y:\n        return y\n    return x + y\n"
                  "def test(x: int, y: int) -> int:\n    return 11 + select(x, y)\n"
                  "def forward(x: int, y: int) -> int:\n    return select(x, y)\n";
        auto built =
            arp::scriptvm::test::buildInterpreter(source, "guarded-return.arp", "guarded-return");
        require(built.ok(), "guarded return: all six parameter comparison sources");
        if (!built.ok())
            continue;
        for (auto& function : built.program.functions)
            for (auto& type : function.paramTypes)
                type = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "guarded return: snapshot");
        if (!artifact.valid())
            continue;
        const auto& function =
            artifact.program().functions[artifact.program().funcIndex.at("select")];
        const auto* plan = artifact.scalarExpressions(&function);
        require(plan && plan->guardedReturn.valid(),
            "guarded return: exact pure comparison and parameter return qualified");
        int native = 3;
        const int cv = 3;
        const Value inputs[][2]{{Value{2}, Value{3}},
            {Value{4}, Value{3}},
            {Value{3}, Value{3}},
            {Value{std::numeric_limits<int>::max()}, Value{3}},
            {Value{std::int64_t(3)}, Value{3}},
            {Value{1.5}, Value{3}},
            {Value{cv}, Value{3}},
            {Value::native_reference(&native), Value{3}}};
        for (bool session : {false, true})
            for (int recursionLimit : {1, 2, 8}) {
                Vm raw, owned;
                require(owned.bindProgram(artifact).ok(), "guarded return: bind");
                auto aSession = core::makeShared<ScriptExecutionSession>();
                auto bSession = core::makeShared<ScriptExecutionSession>();
                if (session) {
                    raw.setExecutionSession(aSession);
                    owned.setExecutionSession(bSession);
                }
                raw.setRecursionLimit(recursionLimit);
                owned.setRecursionLimit(recursionLimit);
                for (const auto& input : inputs) {
                    core::pmr::Vector<Value> args{&resource};
                    args.push_back(input[0]);
                    args.push_back(input[1]);
                    for (int budget = -1; budget <= 22; ++budget) {
                        raw.setInstructionBudget(budget);
                        owned.setInstructionBudget(budget);
                        aSession->begin(budget, recursionLimit);
                        bSession->begin(budget, recursionLimit);
                        VmError a, b;
                        auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                        auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                        require(a.code == b.code && a.message == b.message
                                && a.traceback == b.traceback,
                            "guarded return: every budget, both edges, overflow and recursion preserve original PCs");
                        if (a.code == ScriptDiagnosticCode::None)
                            requireInt(actual,
                                *expected.ptr<int>(),
                                "guarded return: original checked result and prefix");
                        require(aSession->remainingInstructions == bSession->remainingInstructions
                                && aSession->recursionDepth == 0 && bSession->recursionDepth == 0,
                            "guarded return: logical scope and full prefix cost retained");
                    }
                }
            }
        {
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "guarded return: alias bind");
            for (const auto& input : inputs) {
                core::pmr::Vector<Value> args{&resource};
                args.push_back(input[0]);
                args.push_back(input[1]);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "forward", args, &a);
                auto actual = owned.runFunctionByName(artifact.program(), "forward", args, &b);
                require(a.code == b.code, "guarded return: direct forwarding status");
                if (a.code == ScriptDiagnosticCode::None
                    && expected.storageIdentity() == args[1].storageIdentity())
                    require(actual.storageIdentity() == args[1].storageIdentity(),
                        "guarded return: actual selected parameter carrier survives early return");
            }
        }
        ExecutionSlotBuffer stack{&resource};
        Value alias{3};
        stack.emplace_back(ScriptScalar(17));
        stack.emplace_back(alias);
        stack.emplace_back(ScriptScalar(7));
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        {
            const ExecutionArgumentsView args(stack, 2);
            const int* first = args.ownedIntegerPointer(0);
            const int* second = args.ownedIntegerPointer(1);
            require(first == alias.ptr<int>() && second && *second == 7 && stack.size() == 3
                    && stack[1].boxedCarrier()
                    && stack[1].boxedCarrier()->storageIdentity() == alias.storageIdentity(),
                "guarded return: borrowed reads never move/consume actual argument slots");
            require(core::defaultCpuAllocator().stats().allocationCount == before,
                "guarded return: inspection allocates nothing");
        }
        require(stack.size() == 1 && *stack[0].scalarPointer<int>() == 17,
            "guarded return: original argument consumption leaves stack prefix");
    }
    auto built = arp::scriptvm::test::buildInterpreter(
        "def choose(n: int) -> int:\n    if n < 2:\n        return 37\n    return choose(n - 1)\n"
        "def test(n: int) -> int:\n    return choose(n)\n",
        "guarded-constant.arp",
        "guarded-constant");
    require(built.ok(), "guarded return: literal and recursive continuation source");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "guarded return: literal snapshot");
    if (!artifact.valid())
        return;
    const auto* plan = artifact.scalarExpressions(
        &artifact.program().functions[artifact.program().funcIndex.at("choose")]);
    require(
        plan && plan->guardedReturn.valid(), "guarded return: original constant result admitted");
    for (bool quota : {false, true}) {
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "guarded return: constant bind");
        if (quota) {
            raw.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(1024u * 1024u));
            owned.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(1024u * 1024u));
        }
        for (std::size_t frameLimit : {1u, 2u, 64u})
            for (std::size_t argumentLimit : {0u, 1u, 4u}) {
                raw.setMaxFrameValues(frameLimit);
                owned.setMaxFrameValues(frameLimit);
                raw.setMaxCallArguments(argumentLimit);
                owned.setMaxCallArguments(argumentLimit);
                core::pmr::Vector<Value> args{&resource};
                args.emplace_back(Value{4});
                VmError a, b;
                auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "guarded return: dynamic frame/argument limits and quota retain original diagnostics");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "guarded return: literal result");
            }
    }
}

void testScalarCallReplay()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def identity(n: int) -> int:\n    return n\n"
        "def divide(n: int) -> int:\n    return 2 / n\n"
        "def test(n: int) -> int:\n    return identity(n) + divide(n - 1)\n"
        "def count(n: int) -> int:\n    if n < 1:\n        return 0\n    return count(n - 1) + 1\n",
        "scalar-call-replay.arp",
        "scalar-call-replay");
    require(built.ok(), "scalar call: replay source");
    if (!built.ok())
        return;
    for (auto& function : built.program.functions)
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(
        ValidatedInterpreterProgram::create(built.program, artifact).ok(), "scalar call: snapshot");
    if (!artifact.valid())
        return;
    Vm raw, owned;
    require(owned.bindProgram(artifact).ok(), "scalar call: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    for (Value input : {Value{1},
             Value{2},
             Value{3},
             Value{1.5},
             Value{std::numeric_limits<int>::min()},
             makeString(std::string_view("wrong"))}) {
        args.assign(1, input);
        for (int budget = -1; budget <= 40; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            VmError a, b;
            auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "scalar call: callee division/overflow/type/short-budget replay preserves original PC and traceback");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "scalar call: shared checked result");
        }
    }
    owned.setInstructionBudget(-1);
    args.assign(1, Value{24});
    VmError error;
    requireInt(owned.runFunctionByName(artifact.program(), "count", args, &error),
        24,
        "scalar call: warm pooled recursive frames");
    std::uint64_t allocations[2]{};
    for (int index = 0; index < 2; ++index) {
        args[0] = Value{index == 0 ? 4 : 24};
        const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
        requireInt(owned.runFunctionByName(artifact.program(), "count", args, &error),
            index == 0 ? 4 : 24,
            "scalar call: recursion result");
        allocations[index] = arp::core::defaultCpuAllocator().stats().allocationCount - before;
    }
    require(allocations[0] == allocations[1],
        "scalar call: heap allocations do not grow with admitted call count");
    for (std::size_t frameLimit : {1u, 2u, 8u})
        for (std::size_t argumentLimit : {0u, 1u, 2u}) {
            raw.setInstructionBudget(-1);
            raw.setMaxFrameValues(frameLimit);
            owned.setMaxFrameValues(frameLimit);
            raw.setMaxCallArguments(argumentLimit);
            owned.setMaxCallArguments(argumentLimit);
            args.assign(1, Value{3});
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "scalar call: mutable frame and argument limits remain runtime gates");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "scalar call: admitted frame limits");
        }
    Vm limited;
    require(limited.bindProgram(artifact).ok(), "scalar call: quota bind");
    limited.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1u));
    (void)limited.runFunctionByName(artifact.program(), "count", args, &error);
    require(error.code == ScriptDiagnosticCode::ResourceLimitExceeded,
        "scalar call: pooled frame admission remains subject to allocator quota");
}

void testScalarFloatDispatch()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(x: float, end: int) -> float:\n    var i: int = 0\n    var hits: int = 0\n"
        "    while i < end:\n        x = x * 0.5 + 1.0\n        if x > 1.5:\n            hits = hits + 1\n        i = i + 1\n"
        "    if hits > 0:\n        return x\n    return x * 2.0\n",
        "scalar-float-dispatch.arp",
        "scalar-float-dispatch");
    require(built.ok(), "scalar float: source");
    if (!built.ok())
        return;
    for (auto& type : built.program.functions[built.program.funcIndex.at("test")].paramTypes)
        type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "scalar float: snapshot");
    if (!artifact.valid())
        return;
    const auto* function = &artifact.program().functions[artifact.program().funcIndex.at("test")];
    require(artifact.hasScalarDispatch(function),
        "scalar float: loop/branch qualification uses actual bytecode");
    const auto* floatPlan = artifact.scalarExpressions(function);
    require(floatPlan
            && std::any_of(floatPlan->entries.begin(),
                floatPlan->entries.end(),
                [](const auto& entry) {
                    return entry.conditionalBranch >= 0;
                }),
        "scalar float: pure update and virtual conditional span proved");
    for (bool sharedSession : {false, true}) {
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "scalar float: bind");
        auto aSession = arp::core::makeShared<ScriptExecutionSession>();
        auto bSession = arp::core::makeShared<ScriptExecutionSession>();
        if (sharedSession) {
            raw.setExecutionSession(aSession);
            owned.setExecutionSession(bSession);
        }
        for (Value input : {Value{-0.0},
                 Value{1.0},
                 Value{-32.0},
                 Value{std::numeric_limits<double>::infinity()},
                 Value{std::numeric_limits<double>::quiet_NaN()},
                 Value{1},
                 makeString(std::string_view("wrong"))})
            for (Value end : {Value{0}, Value{3}, Value{2.5}}) {
                arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
                args.push_back(input);
                args.push_back(end);
                for (int budget = -1; budget <= 90; ++budget) {
                    raw.setInstructionBudget(budget);
                    owned.setInstructionBudget(budget);
                    aSession->begin(budget, 64);
                    bSession->begin(budget, 64);
                    VmError a, b;
                    auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                    auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                    require(
                        a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                        "scalar float: each budget, wrong payload and mixed condition preserve PC/traceback");
                    if (a.code == ScriptDiagnosticCode::None) {
                        require(expected.is<double>() && actual.is<double>(),
                            "scalar float: exact result kind");
                        if (expected.is<double>() && actual.is<double>())
                            require(
                                std::memcmp(
                                    expected.ptr<double>(), actual.ptr<double>(), sizeof(double))
                                    == 0,
                                "scalar float: same IEEE bits including NaN/inf/signed zero and operation order");
                    }
                    require(aSession->remainingInstructions == bSession->remainingInstructions
                            && aSession->recursionDepth == 0 && bSession->recursionDepth == 0,
                        "scalar float: session accounting preserved");
                }
            }
    }
    auto unsupported = *function;
    unsupported.code.back().op = Opcode::CallHost;
    require(!InterpreterBytecodeLowerer::isScalarDispatch(unsupported),
        "scalar float: host boundary rejects compact dispatch");
    unsupported = *function;
    unsupported.code.back().op = Opcode::Yield;
    require(!InterpreterBytecodeLowerer::isScalarDispatch(unsupported),
        "scalar float: suspension rejects compact dispatch");
}

void testInheritedExecutionQuota()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def observe(token: any) -> any:\n    return token + token\n"
        "def nested(token: any, n: int) -> any:\n    if n < 1:\n        return observe(token)\n"
        "    return nested(token, n - 1)\n"
        "def test(token: any) -> any:\n    var ignored: any = observe(token)\n    return nested(token, 4)\n",
        "quota-inheritance.arp",
        "quota-inheritance");
    require(built.ok(), "quota inheritance: source");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "quota inheritance: snapshot");
    if (!artifact.valid())
        return;
    require(artifact.hasVerifiedStackBound(
                &artifact.program().functions[artifact.program().funcIndex.at("nested")]),
        "quota inheritance: actual recursive static edges qualify");

    struct State {
        Vm* vm = nullptr;
        Vm* other = nullptr;
        const Program* program = nullptr;
        std::shared_ptr<core::CpuMemoryQuota> replacement, otherQuota, hostQuota;
        core::pmr::Vector<const core::CpuMemoryQuota*> observed{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::shared_ptr<core::CpuMemoryQuota>> owners{
            &core::defaultCpuMemoryResource()};
        bool reenter = false;
        bool throwFailure = false;
    };

    struct Probe {
        std::shared_ptr<State> state;
    };

    arp::reflection::registration::class_<Probe>("QuotaInheritanceProbe")
        .method("__add__", [](const Probe& left, const Probe&) -> Value {
            auto& state = *left.state;
            const bool first = state.observed.empty();
            state.observed.push_back(core::currentCpuMemoryQuota().get());
            state.owners.push_back(core::currentCpuMemoryQuota());
            if (first && state.reenter) {
                state.vm->setMemoryQuota(state.replacement);
                core::CpuMemoryQuotaScope hostScope(state.hostQuota);
                core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
                args.push_back(Value{left});
                VmError a, b;
                requireInt(state.vm->runFunctionByName(*state.program, "observe", args, &a),
                    7,
                    "quota inheritance: same VM public reentry");
                require(a.code == ScriptDiagnosticCode::None
                        && core::currentCpuMemoryQuota() == state.hostQuota,
                    "quota inheritance: same VM reentry restores intervening host scope");
                requireInt(state.other->runFunctionByName(*state.program, "observe", args, &b),
                    7,
                    "quota inheritance: other VM public entry");
                require(b.code == ScriptDiagnosticCode::None
                        && core::currentCpuMemoryQuota() == state.hostQuota,
                    "quota inheritance: other VM restores intervening host scope");
            }
            if (state.throwFailure)
                throw std::bad_alloc();
            return makeInt(7);
        });
    (void)makeInt(7);
    for (bool validated : {false, true})
        for (bool sharedSession : {false, true})
            for (int change = 0; change != 4; ++change) {
                Vm vm, other;
                const auto& program = validated ? artifact.program() : built.program;
                if (validated) {
                    require(vm.bindProgram(artifact).ok() && other.bindProgram(artifact).ok(),
                        "quota inheritance: bind");
                }
                auto session = core::makeShared<ScriptExecutionSession>();
                session->begin(-1, 1024);
                if (sharedSession) {
                    vm.setExecutionSession(session);
                    other.setExecutionSession(session);
                }
                auto state = core::makeShared<State>();
                state->vm = &vm;
                state->other = &other;
                state->program = &program;
                auto initial = change == 0 ? std::shared_ptr<core::CpuMemoryQuota>()
                                           : core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
                state->replacement =
                    change == 1 ? nullptr : core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
                if (change == 3)
                    state->replacement = std::shared_ptr<core::CpuMemoryQuota>(
                        core::makeShared<int>(0), initial.get());
                state->hostQuota = core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
                state->otherQuota = core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
                state->observed.reserve(4);
                state->owners.reserve(4);
                state->reenter = true;
                vm.setMemoryQuota(initial);
                other.setMemoryQuota(state->otherQuota);
                core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
                args.push_back(Value{Probe{state}});
                auto external = core::makeShared<core::CpuMemoryQuota>(1024u * 1024u);
                {
                    core::CpuMemoryQuotaScope outer(external);
                    VmError error;
                    requireInt(vm.runFunctionByName(program, "test", args, &error),
                        7,
                        "quota inheritance: changing actual quota preserves recursive result");
                    require(error.code == ScriptDiagnosticCode::None
                            && core::currentCpuMemoryQuota() == external,
                        "quota inheritance: public entry restores original external scope");
                    require(state->observed.size() == 4 && state->observed[0] == initial.get()
                            && state->observed[1] == state->replacement.get()
                            && state->observed[2] == state->otherQuota.get()
                            && state->observed[3] == state->replacement.get(),
                        "quota inheritance: null/new/cleared quota, reentrant and cross-VM scopes never use stale binding");
                    const std::shared_ptr<core::CpuMemoryQuota> expected[]{
                        initial, state->replacement, state->otherQuota, state->replacement};
                    for (int index = 0; index < 4 && index < static_cast<int>(state->owners.size());
                        ++index)
                        require(!state->owners[index].owner_before(expected[index])
                                && !expected[index].owner_before(state->owners[index]),
                            "quota inheritance: aliased raw pointer still retains the exact original shared owner");
                    state->reenter = false;
                    state->throwFailure = true;
                    state->observed.clear();
                    state->owners.clear();
                    VmError failure;
                    (void)vm.runFunctionByName(program, "test", args, &failure);
                    require(failure.code == ScriptDiagnosticCode::ResourceLimitExceeded
                            && core::currentCpuMemoryQuota() == external,
                        "quota inheritance: bad_alloc diagnostics and scope restoration");
                    require(!vm.hasActiveFrames() && session->recursionDepth == 0,
                        "quota inheritance: every physical recursive frame unwinds after host bad_alloc");
                    for (int repeat = 0; repeat != 3; ++repeat) {
                        state->observed.clear();
                        state->owners.clear();
                        VmError repeatedFailure;
                        (void)vm.runFunctionByName(program, "test", args, &repeatedFailure);
                        require(repeatedFailure.code == ScriptDiagnosticCode::ResourceLimitExceeded
                                && !vm.hasActiveFrames() && session->recursionDepth == 0
                                && core::currentCpuMemoryQuota() == external,
                            "quota inheritance: repeated host allocation failures leave both stacks and binding clean");
                    }
                    state->throwFailure = false;
                    state->observed.clear();
                    state->owners.clear();
                    VmError resumed;
                    requireInt(vm.runFunctionByName(program, "observe", args, &resumed),
                        7,
                        "quota inheritance: entry after failure rebuilds its actual binding");
                    require(resumed.code == ScriptDiagnosticCode::None
                            && state->observed.size() == 1
                            && state->observed[0] == state->replacement.get()
                            && core::currentCpuMemoryQuota() == external,
                        "quota inheritance: no dangling inherited scope after failure");
                    require(!vm.hasActiveFrames(),
                        "quota inheritance: resumed call leaves no stale physical traceback frames");
                }
            }
}

void testResumableIndependentExpressions()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def produce(n: int, seed: int) -> int:\n    var i: int = 0\n    while i < n:\n"
        "        yield seed + i * 3\n        i = i + 1\n"
        "def create(n: int, seed: int) -> any:\n    return produce(n, seed)\n"
        "def wrapped(n: int, seed: int) -> any:\n    return create(n, seed)\n"
        "def test(n: int, seed: int) -> int:\n    var total: int = 0\n"
        "    for value in produce(n, seed):\n        total = (total + value) % 1000000007\n    return total\n",
        "resumable-expressions.arp",
        "resumable-expressions");
    require(built.ok(), "resumable expressions: source");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "resumable expressions: snapshot");
    if (!artifact.valid())
        return;
    for (const char* name : {"test", "create"}) {
        const auto& function = artifact.program().functions[artifact.program().funcIndex.at(name)];
        require(artifact.hasVerifiedStackBound(&function),
            "generator factory: caller has a full local stack proof without proving resumed body");
        require(!artifact.hasScalarDispatch(&function),
            "generator factory: actual creation uses original factory handler");
    }
    const auto& wrapped = artifact.program().functions[artifact.program().funcIndex.at("wrapped")];
    require(artifact.hasVerifiedStackBound(&wrapped) && artifact.hasScalarDispatch(&wrapped),
        "generator factory: transitive ordinary caller uses proved one-result compact call");
    const auto& consumer = artifact.program().functions[artifact.program().funcIndex.at("test")];
    const auto* consumerPlan = artifact.scalarExpressions(&consumer);
    require(consumerPlan && !consumerPlan->independentStackSpans
            && std::any_of(consumerPlan->entries.begin(),
                consumerPlan->entries.end(),
                [](const auto& entry) {
                    return entry.rangeBody >= 0;
                }),
        "generator consumer: original RangeNext joins only a proved pure scalar body");
    for (int mutation = 0; mutation < 3; ++mutation) {
        auto malformed = built.program;
        auto& factory = malformed.functions[malformed.funcIndex.at("create")];
        for (auto& instruction : factory.code)
            if (instruction.op == Opcode::Call) {
                if (mutation == 0)
                    instruction.b = 0;
                else if (mutation == 1)
                    instruction.a = -1;
                else
                    malformed.functions[instruction.a].isVariadic = true;
                break;
            }
        const auto proofs = InterpreterBytecodeLowerer::analyzeProgramStacks(malformed);
        require(proofs[malformed.funcIndex.at("create")].status != TypedIrScalarStackStatus::Valid,
            "generator factory: malformed target, arity and variadic admission stay checked");
    }
    for (const char* name : {"produce"}) {
        const auto& function = artifact.program().functions[artifact.program().funcIndex.at(name)];
        const auto* plan = artifact.independentScalarExpressions(&function);
        require(!artifact.hasVerifiedStackBound(&function) && plan && plan->independentStackSpans,
            "resumable expressions: producer still uses independent proofs only");
        if (!plan)
            continue;
        require(!plan->hasClosureLoop && !plan->hasMethodLoop,
            "resumable expressions: no closed call loop or whole-frame qualification");
        for (std::size_t pc = 0; pc < plan->entries.size(); ++pc) {
            const auto& entry = plan->entries[pc];
            if (entry.count == 0)
                continue;
            require(entry.evaluator && entry.leafCost == 0 && entry.leafFrameValues == 0
                    && entry.stepLocal < 0 && entry.loopCondition < 0,
                "resumable expressions: only one-shot specialized pure integer handlers");
            for (const auto& site : function.safepoints)
                require(site.pc <= static_cast<int>(pc) || site.pc > entry.lastPc,
                    "resumable expressions: no suspension boundary inside an expression");
            for (int at = static_cast<int>(pc); at <= entry.lastPc; ++at)
                require(
                    function.code[at].op != Opcode::Yield && function.code[at].op != Opcode::Call,
                    "resumable expressions: no yield or callee executes inside an expression");
        }
    }
    Vm raw, owned;
    require(owned.bindProgram(artifact).ok(), "resumable expressions: bind");
    core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
    args.emplace_back(Value{4});
    args.emplace_back(Value{7});
    int native = 11;
    const Value seeds[]{Value{7},
        Value{2147483647},
        Value{std::int64_t(2147483648LL)},
        Value{4.5},
        Value::native_reference(&native)};
    for (const auto& seed : seeds) {
        args[1] = seed;
        for (int budget = -1; budget <= 180; ++budget) {
            auto aSession = core::makeShared<ScriptExecutionSession>();
            auto bSession = core::makeShared<ScriptExecutionSession>();
            aSession->remainingInstructions = bSession->remainingInstructions = budget;
            raw.setExecutionSession(aSession);
            owned.setExecutionSession(bSession);
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                    && aSession->remainingInstructions == bSession->remainingInstructions
                    && aSession->recursionDepth == bSession->recursionDepth,
                "resumable expressions: each budget/session/type/overflow retains exact error PC and traceback");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "resumable expressions: consumer result");
        }
    }
    raw.setExecutionSession(nullptr);
    owned.setExecutionSession(nullptr);
    args[1] = Value{7};
    for (const std::size_t limit : {0u, 1u, 3u, 65536u}) {
        raw.setMaxFrameValues(limit);
        owned.setMaxFrameValues(limit);
        VmError a, b;
        const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
        const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
        require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
            "resumable expressions: low frame/temporary limits replay original PC");
        if (a.code == ScriptDiagnosticCode::None)
            requireInt(actual, *expected.ptr<int>(), "resumable expressions: frame limit result");
    }
    raw.setMaxFrameValues(65536u);
    owned.setMaxFrameValues(65536u);
    for (const std::uint64_t bytes : {1u, 4096u, 65536u}) {
        auto aQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto bQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        raw.setMemoryQuota(aQuota);
        owned.setMemoryQuota(bQuota);
        VmError a, b;
        const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
        const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
        require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                && aQuota->liveBytes() == bQuota->liveBytes(),
            "resumable expressions: quota retains original allocation/failure behavior");
        if (a.code == ScriptDiagnosticCode::None)
            requireInt(actual, *expected.ptr<int>(), "resumable expressions: quota result");
    }
    raw.setMemoryQuota(nullptr);
    owned.setMemoryQuota(nullptr);
    for (const char* factory : {"create", "wrapped"})
        for (int mode = 0; mode < 3; ++mode) {
            VmError a, b;
            auto expected = raw.runFunctionByName(built.program, factory, args, &a);
            auto actual = owned.runFunctionByName(artifact.program(), factory, args, &b);
            require(a.code == b.code && isScriptGenerator(expected) && isScriptGenerator(actual),
                "resumable expressions: real generator factory carriers");
            if (!isScriptGenerator(expected) || !isScriptGenerator(actual))
                continue;
            auto* first = asScriptGenerator(expected);
            auto* second = asScriptGenerator(actual);
            for (int index = 0; index < 5 && !first->done; ++index) {
                a = {};
                b = {};
                Value left, right;
                if (index == 1 && mode == 1) {
                    left = raw.resumeGeneratorValueSend(*first, Value{99}, a);
                    right = owned.resumeGeneratorValueSend(*second, Value{99}, b);
                } else if (index == 1 && mode == 2) {
                    const auto message = makeString(std::string_view("injected"));
                    left = raw.resumeGeneratorValueThrow(*first, message, a);
                    right = owned.resumeGeneratorValueThrow(*second, message, b);
                } else {
                    left = raw.resumeGeneratorValue(*first, a);
                    right = owned.resumeGeneratorValue(*second, b);
                }
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                        && first->done == second->done && first->frame.ip == second->frame.ip
                        && first->frame.lastIp == second->frame.lastIp
                        && first->frame.stack.size() == second->frame.stack.size(),
                    "resumable expressions: resume/send/throw retain actual continuation PC and stack");
                if (a.code == ScriptDiagnosticCode::None && !first->done)
                    requireInt(right,
                        *left.ptr<int>(),
                        "resumable expressions: each actual yielded value");
                if (index == 0) {
                    SuspendedScriptFrame aFrame, bFrame;
                    const auto sa =
                        raw.snapshotGeneratorFrame(*first, "resumable-expressions", 1, aFrame);
                    const auto sb =
                        owned.snapshotGeneratorFrame(*second, "resumable-expressions", 1, bFrame);
                    require(sa.ok() && sb.ok() && aFrame.bytecodePc == bFrame.bytecodePc
                            && aFrame.locals.size() == bFrame.locals.size()
                            && aFrame.operandStack.size() == bFrame.operandStack.size(),
                        "resumable expressions: canonical suspended frame remains available at the original yield");
                }
                if (a.code != ScriptDiagnosticCode::None)
                    break;
            }
            raw.closeGenerator(*first);
            owned.closeGenerator(*second);
            require(first->done && second->done, "resumable expressions: original close state");
        }
}

void testGeneratorFrameAllocation()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def count(n: int) -> int:\n    var i: int = 0\n    while i < n:\n        yield i\n        i = i + 1\n"
        "def test(n: int) -> int:\n    var total: int = 0\n    for value in count(n):\n        total = total + value\n    return total\n",
        "generator-allocation.arp",
        "generator-allocation");
    require(built.ok(), "generator allocation: source");
    if (!built.ok())
        return;
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "generator allocation: snapshot");
    Vm vm;
    require(vm.bindProgram(artifact).ok(), "generator allocation: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{0});
    std::uint64_t allocations[2]{};
    for (int warm = 0; warm < 2; ++warm)
        for (int size = 0; size < 2; ++size) {
            const int n = size == 0 ? 4 : 64;
            args[0] = makeInt(n);
            VmError error;
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            const auto result = vm.runFunctionByName(artifact.program(), "test", args, &error);
            allocations[size] = arp::core::defaultCpuAllocator().stats().allocationCount - before;
            require(error.code == ScriptDiagnosticCode::None, "generator allocation: execution");
            requireInt(result, n * (n - 1) / 2, "generator allocation: every yield executed");
        }
    require(allocations[0] == allocations[1],
        "generator allocation: ordinary resume does not allocate per yield");
}

void testIndexedStringComparison()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    for (const char* operation : {"==", "!=", "<", "<=", ">", ">="}) {
        arp::core::pmr::String source(
            "def test(values: [string], end: int) -> int:\n    var i: int = 0\n    var hits: int = 0\n"
            "    while i < end:\n        if values[i % 3] ",
            &resource);
        source += operation;
        source += " \"gamma\":\n            hits = hits + 1\n        i = i + 1\n    return hits\n";
        auto built = arp::scriptvm::test::buildInterpreter(
            source, "string-comparison.arp", "string-comparison");
        require(built.ok(), "string comparison: indexed source");
        if (!built.ok())
            continue;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "string comparison: snapshot");
        if (!artifact.valid())
            continue;
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        require(plan
                && std::any_of(plan->nodes.begin(),
                    plan->nodes.end(),
                    [](const auto& node) {
                        return node.operation == ScalarExpressionPlan::Operation::StringConstant;
                    }),
            "string comparison: exact load/index/literal/compare/branch span proved");
        require(std::any_of(plan->entries.begin(),
                    plan->entries.end(),
                    [](const auto& entry) {
                        return entry.conditionalBody >= 0;
                    }),
            "string comparison: conditional body/step/backedge proof");
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "string comparison: bind");
        const core::pmr::String cvText("gamma", &resource);
        core::pmr::String nativeText("gamma", &resource);
        for (int mutation = 0; mutation < 6; ++mutation) {
            auto values = arp::reflection::makeEmptyHList();
            values.push_back(makeString(std::string_view("alpha")));
            values.push_back(makeString(std::string_view("gamma")));
            if (mutation != 1)
                values.push_back(mutation == 2 ? Value::undefined()
                        : mutation == 4        ? Value{cvText}
                        : mutation == 5        ? Value::native_reference(&nativeText)
                                               : makeString(std::string_view("beta")));
            Value input{std::move(values)};
            const auto identity = input.storageIdentity();
            arp::core::pmr::Vector<Value> args{&resource};
            args.push_back(mutation == 3 ? Value{1.5} : input);
            args.push_back(Value{6});
            for (int budget = -1; budget <= 140; ++budget) {
                nativeText = budget % 2 == 0 ? "gamma" : "beta";
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const int expectedInteger = expected.is<int>() ? *expected.ptr<int>() : -999;
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "string comparison: short budget and post-success bounds/type guards replay original PC");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(
                        actual, expectedInteger, "string comparison: all six shared comparisons");
                require(input.storageIdentity() == identity
                        && input.ptr<arp::reflection::hlist>()->size() == (mutation == 1 ? 2 : 3),
                    "string comparison: imported container identity/aliases preserved");
            }
        }
        // Cross-type reflection ordering may depend on boxed-object identity.
        // Verify integer-element rejection directly, without comparing results
        // from independently allocated literal carriers in two separate runs.
        for (const auto& entry : plan->entries) {
            if (!entry.evaluator || entry.count != 3
                || plan->nodes[entry.offset + 1].operation
                    != ScalarExpressionPlan::Operation::StringConstant)
                continue;
            ExecutionSlotBuffer locals{&resource};
            locals.resize(static_cast<std::size_t>(function.locals));
            auto values = arp::reflection::makeEmptyHList();
            values.push_back(Value{42});
            const auto& index = plan->nodes[entry.offset];
            locals[static_cast<std::size_t>(index.operand)] = Value{std::move(values)};
            locals[static_cast<std::size_t>(index.index.operand)] = ScriptScalar(0);
            int result = 123;
            require(!plan->evaluateSlots(entry, locals, nullptr, result) && result == 123,
                "string comparison: wrong actual element replays without committing");
        }
    }
}

void testConditionalLoopGuards()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    for (const char* condition : {"i < end", "hits < end"}) {
        arp::core::pmr::String source(
            "def test(text: string, i: int, hits: int, end: int) -> int:\n    while ", &resource);
        source += condition;
        source +=
            ":\n        if text == \"gamma\":\n            hits = hits + 1\n        i = i + 2\n    return hits\n";
        auto built =
            arp::scriptvm::test::buildInterpreter(source, "conditional.arp", "conditional");
        require(built.ok(), "conditional guards: source");
        if (!built.ok())
            continue;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "conditional guards: snapshot");
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "conditional guards: bind");
        const Value inputs[][4]{
            {makeString(std::string_view("gamma")), Value{0}, Value{0}, Value{6}},
            {makeString(std::string_view("gamma")), Value{2147483646}, Value{0}, Value{2147483647}},
            {makeString(std::string_view("gamma")), Value{2147483642}, Value{0}, Value{2147483647}},
            {makeString(std::string_view("gamma")), Value{0}, Value{2147483645}, Value{8}},
            {makeString(std::string_view("gamma")), Value{0}, Value{2147483647}, Value{6}},
            {makeString(std::string_view("other")), Value{0}, Value{2147483647}, Value{6}},
            {makeString(std::string_view("gamma")), Value{0}, Value::undefined(), Value{6}},
            {makeString(std::string_view("other")), Value{0}, Value::undefined(), Value{6}},
            {makeString(std::string_view("gamma")), Value{1.5}, Value{0}, Value{6}},
            {Value::undefined(), Value{0}, Value{0}, Value{6}},
            {makeString(std::string_view("gamma")), Value{0}, Value{0}, Value{5.5}}};
        for (const auto& input : inputs) {
            arp::core::pmr::Vector<Value> args{&resource};
            args.assign(std::begin(input), std::end(input));
            // Non-taken counter-dependent loops can be intentionally infinite.
            // Finite budgets exercise every original instruction cutoff.
            for (int budget = 0; budget <= 140; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const int integer = expected.is<int>() ? *expected.ptr<int>() : -999;
                const bool expectedNil = isNil(expected);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "conditional guards: counter/step overflow, mixed actual types, virtual condition and budget PC");
                if (a.code == ScriptDiagnosticCode::None) {
                    if (expectedNil)
                        require(isNil(actual),
                            "conditional guards: skipped update preserves nil carrier");
                    else
                        requireInt(actual, integer, "conditional guards: result");
                }
            }
        }
    }
}

void testExplicitExpressionOperands()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(a: int, b: int) -> int:\n    return (a + b) % 5 + (a * b) % 7\n",
        "expression-operands.arp",
        "expression-operands");
    require(built.ok(), "explicit operands: source");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "explicit operands: snapshot");
    if (!artifact.valid())
        return;
    const auto* plan = artifact.scalarExpressions(
        &artifact.program().functions[artifact.program().funcIndex.at("test")]);
    require(plan && plan->entries[0].calculationCount == 5 && plan->scratchSize >= 5,
        "explicit operands: five arithmetic operations replace eleven stack instructions");
    const Value inputs[][2]{{Value{10000}, Value{7}},
        {Value{-13}, Value{2}},
        {Value{std::numeric_limits<int>::max()}, Value{1}},
        {Value{100000}, Value{100000}},
        {Value{1.5}, Value{2}}};
    for (const auto& input : inputs) {
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "explicit operands: bind");
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.assign(std::begin(input), std::end(input));
        for (int budget = -1; budget <= 20; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            VmError expectedError, actualError;
            const auto expected =
                raw.runFunctionByName(built.program, "test", args, &expectedError);
            const auto actual =
                owned.runFunctionByName(artifact.program(), "test", args, &actualError);
            require(expectedError.code == actualError.code
                    && expectedError.message == actualError.message
                    && expectedError.traceback == actualError.traceback,
                "explicit operands: intermediate guard/overflow/budget errors replay exact bytecodes");
            if (expectedError.code == ScriptDiagnosticCode::None)
                requireInt(actual,
                    *expected.ptr<int>(),
                    "explicit operands: dependent temporaries and fixed remainder");
        }
    }
}

void testInlineIndexAllocation()
{
    for (bool bytes : {false, true}) {
        auto built = arp::scriptvm::test::buildInterpreter(bytes
                ? "def test(data: bytes, end: int) -> int:\n    var i: int = 100000\n    var s: int = 0\n    while i < end:\n        s = s + data[i]\n        i = i + 1\n    return s\n"
                : "def test(data: [int], end: int) -> int:\n    var i: int = 100000\n    var s: int = 0\n    while i < end:\n        s = s + data[i]\n        i = i + 1\n    return s\n",
            "inline-index.arp",
            "inline-index");
        require(built.ok(), "inline index: compile");
        if (!built.ok())
            continue;
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "inline index: snapshot");
        if (!artifact.valid())
            continue;
        Value alias{73};
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        if (bytes) {
            ScriptBytes data;
            data.data.resize(102000, 255);
            args.emplace_back(Value{std::move(data)});
        } else {
            auto data = arp::reflection::makeEmptyHList();
            data.resize(102000, alias);
            args.emplace_back(Value{std::move(data)});
        }
        args.emplace_back(Value{102000});
        for (bool owned : {false, true}) {
            Vm vm;
            if (owned)
                require(vm.bindProgram(artifact).ok(), "inline index: bind");
            const auto& program = owned ? artifact.program() : built.program;
            VmError error;
            const int expected = bytes ? 510000 : 146000;
            requireInt(vm.runFunctionByName(program, "test", args, &error),
                expected,
                "inline index: warm");
            require(error.code == ScriptDiagnosticCode::None, "inline index: warm diagnostic");
            args[1] = Value{101000};
            const auto shortBefore = arp::core::defaultCpuAllocator().stats().allocationCount;
            requireInt(vm.runFunctionByName(program, "test", args, &error),
                expected / 2,
                "inline index: shorter loop");
            const auto shortAllocations =
                arp::core::defaultCpuAllocator().stats().allocationCount - shortBefore;
            args[1] = Value{102000};
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            requireInt(vm.runFunctionByName(program, "test", args, &error),
                expected,
                "inline index: repeated");
            const auto allocations =
                arp::core::defaultCpuAllocator().stats().allocationCount - before;
            // Raw programs pay entry validation costs; these must not grow with iterations.
            require(allocations == shortAllocations && (!owned || allocations <= 5),
                "inline index: allocation count independent of cache-external index loop");
            requireInt(alias, 73, "inline index: original element alias unchanged");
            vm.setInstructionBudget(20);
            (void)vm.runFunctionByName(program, "test", args, &error);
            require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
                "inline index: instruction budget retained");
        }
    }
}

void testFusedScalarAllocation()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(n: int) -> int:\n    return n\n", "fused.arp", "fused-inline");
    require(built.ok(), "fused scalar: source fixture");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.locals = 2;
    function.maxStack = 1;
    function.debugLocals.clear();
    function.code = {Instruction{Opcode::LoadInt, 0, 0, 0},
        Instruction{Opcode::StoreLocal, 1},
        Instruction{Opcode::JumpIfLocalIntLtConstFalse, 6, 1, 2000},
        Instruction{Opcode::LocalAddIntConst, 0, 0, 1},
        Instruction{Opcode::LocalAddIntConst, 1, 0, 1},
        Instruction{Opcode::Jump, 2},
        Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::ReturnInt}};
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "fused scalar: snapshot");
    if (!artifact.valid())
        return;
    Vm vm;
    require(vm.bindProgram(artifact).ok(), "fused scalar: bind");
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.emplace_back(Value{100000});
    VmError error;
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        102000,
        "fused scalar: warm result");
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    requireInt(vm.runFunctionByName(artifact.program(), "test", args, &error),
        102000,
        "fused scalar: repeated result");
    require(arp::core::defaultCpuAllocator().stats().allocationCount - before <= 5,
        "fused scalar: allocations independent of loop count above integer cache");
    requireInt(args[0], 100000, "fused scalar: imported alias remains unchanged");
}

void testDictionaryKeyProofBoundary()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(flag: bool) -> int:\n    return 1\n", "dictionary-join.arp", "dictionary-join");
    require(built.ok(), "dictionary join: fixture");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.locals = 2;
    function.maxStack = 2;
    function.debugLocals.clear();
    function.code = {Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::JumpIfFalse, 5},
        Instruction{Opcode::LoadInt, 0, 0, 0},
        Instruction{Opcode::LoadInt, 0, 0, 1},
        Instruction{Opcode::Jump, 7},
        Instruction{Opcode::LoadString},
        Instruction{Opcode::LoadInt, 0, 0, 2},
        Instruction{Opcode::MakeDict, 0, 1},
        Instruction{Opcode::StoreLocal, 1},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::LoadInt, 0, 0, 0},
        Instruction{Opcode::LoadIndex},
        Instruction{Opcode::LoadInt, 0, 0, 1},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::ReturnInt}};
    function.code[5].s = "bad";
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "dictionary join: snapshot");
    if (!artifact.valid())
        return;
    const auto* ownedFunction =
        &artifact.program().functions[artifact.program().funcIndex.at("test")];
    require(artifact.hasVerifiedStackBound(ownedFunction),
        "dictionary join: CFG stack proof independent of key types");
    const auto* plan = artifact.scalarExpressions(ownedFunction);
    if (plan)
        for (const auto& node : plan->nodes)
            if (node.operation == ScalarExpressionPlan::Operation::Index)
                require(!node.index.dictionary,
                    "dictionary join: interior incoming edge blocks literal-key assumption");
    for (bool flag : {false, true}) {
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.emplace_back(Value{flag});
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "dictionary join: bind");
        for (int budget = -1; budget <= 20; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "dictionary join: mixed keys and each budget follow original lookup");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "dictionary join: result");
        }
    }
}

void testExpressionStackPrefix()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(a: int, b: int) -> int:\n    var values: [int] = [a, b + 1]\n    return values[0]\n",
        "expression-prefix.arp",
        "expression-prefix");
    require(built.ok(), "expression prefix: fixture");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.paramTypes[1] = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "expression prefix: snapshot");
    if (!artifact.valid())
        return;
    const auto* plan = artifact.scalarExpressions(
        &artifact.program().functions[artifact.program().funcIndex.at("test")]);
    bool push = false;
    if (plan)
        for (const auto& entry : plan->entries)
            push |= entry.count != 0 && entry.terminal == ScalarExpressionPlan::Terminal::Push;
    require(
        push, "expression prefix: arithmetic push prepared independently of preceding operands");
    Value imported{100000};
    for (Value b : {Value{7}, Value{std::numeric_limits<int>::max()}, Value{0.5}}) {
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.push_back(imported);
        args.push_back(b);
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "expression prefix: bind");
        for (int budget = -1; budget <= 20; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            VmError a, bError;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &bError);
            require(a.code == bError.code && a.message == bError.message
                    && a.traceback == bError.traceback,
                "expression prefix: every budget and overflow/type fallback preserves errors");
            if (a.code == ScriptDiagnosticCode::None)
                require(actual.storageIdentity() == imported.storageIdentity()
                        && expected.storageIdentity() == imported.storageIdentity()
                        && *imported.ptr<int>() == 100000,
                    "expression prefix: preceding host carrier returns with exact identity");
        }
    }
}

void testCollectionExpressionOperands()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(values: [int], index: int) -> int:\n    return (values[index] + 1) % 5\n",
        "index-expression.arp",
        "index-expression");
    require(built.ok(), "index expression: fixture");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.paramTypes[0] = function.paramTypes[1] = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "index expression: snapshot");
    if (!artifact.valid())
        return;
    const auto* plan = artifact.scalarExpressions(
        &artifact.program().functions[artifact.program().funcIndex.at("test")]);
    bool hasIndex = false;
    if (plan)
        for (const auto& node : plan->nodes)
            hasIndex |= node.operation == ScalarExpressionPlan::Operation::Index;
    require(hasIndex, "index expression: explicit guarded index operand prepared");
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hlist values((arp::reflection::hlist::allocator_type(&resource)));
    Value element{100000};
    values.emplace_back(element);
    values.emplace_back(makeString(std::string_view("wrong")));
    values.emplace_back(Value{std::numeric_limits<int>::max()});
    Value list{std::move(values)};
    arp::reflection::hdict dictionary((arp::reflection::hdict::allocator_type(&resource)));
    for (int slot : {0, 1, 2})
        dictionary.emplace(Value{slot}, list.ptr<arp::reflection::hlist>()->at(slot));
    Value dict{std::move(dictionary)};
    arp::reflection::hdict mixed(*dict.ptr<arp::reflection::hdict>());
    mixed.emplace(makeString(std::string_view("other")), Value{9});
    Value mixedDict{std::move(mixed)};
    const Value indices[]{Value{0}, Value{1}, Value{2}, Value{-1}, Value{3}, Value{0.5}};
    for (const auto& container : {list, dict, mixedDict})
        for (const auto& index : indices) {
            arp::core::pmr::Vector<Value> args{&resource};
            args.push_back(container);
            args.push_back(index);
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "index expression: bind");
            for (int budget = -1; budget <= 15; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "index expression: imported list/dict/mixed keys, each budget, wrong payload, overflow and missing/bounds agree");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "index expression: scalar result");
            }
            require(list.ptr<arp::reflection::hlist>()->front().storageIdentity()
                        == element.storageIdentity()
                    && *element.ptr<int>() == 100000,
                "index expression: imported element alias remains intact");
        }
}

void testIndexedIntegerUpdateReplay()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(values: [int], index: int, prefix: int) -> int:\n    return prefix\n",
        "indexed-update.arp",
        "indexed-update");
    require(built.ok(), "indexed update: fixture");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    for (auto& type : function.paramTypes)
        type = RuntimeType::Any;
    function.debugLocals.clear();
    function.maxStack = 5;
    function.code = {Instruction{Opcode::LoadLocal, 2},
        Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::LoadLocal, 0},
        Instruction{Opcode::LoadLocal, 1},
        Instruction{Opcode::LoadIndex},
        Instruction{Opcode::LoadInt, 0, 0, 1},
        Instruction{Opcode::IntAdd},
        Instruction{Opcode::StoreIndex},
        Instruction{Opcode::ReturnInt}};
    auto& resource = arp::core::defaultCpuMemoryResource();
    Value prefix{100000};
    for (const auto opcode : {Opcode::IntAdd,
             Opcode::IntSub,
             Opcode::IntMul,
             Opcode::IntDiv,
             Opcode::IntMod,
             Opcode::Add,
             Opcode::Sub,
             Opcode::Mul,
             Opcode::Div,
             Opcode::Mod}) {
        function.code[7].op = opcode;
        function.code[6].i64 = opcode == Opcode::IntDiv || opcode == Opcode::Div
                || opcode == Opcode::IntMod || opcode == Opcode::Mod
            ? 0
            : 1;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "indexed update: snapshot");
        if (!artifact.valid())
            return;
        const auto* ownedFunction =
            &artifact.program().functions[artifact.program().funcIndex.at("test")];
        const auto* plan = artifact.scalarExpressions(ownedFunction);
        require(plan && plan->entries[1].terminal == ScalarExpressionPlan::Terminal::IndexedStore
                && plan->entries[1].destination == 0 && plan->entries[1].stepLocal == 1
                && plan->entries[1].cost == 8 && plan->entries[1].lastPc == 8,
            "indexed update: exact eight instructions with preserved stack prefix");
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "indexed update: bind");
        for (bool dictionary : {false, true})
            for (Value element : {Value{7},
                     Value{std::numeric_limits<int>::max()},
                     Value{std::numeric_limits<int>::min()},
                     Value{0.5},
                     makeString(std::string_view("wrong"))})
                for (Value index : {Value{0}, Value{1}, Value{-1}, Value{0.5}}) {
                    for (int budget = -1; budget <= 11; ++budget) {
                        const auto container = [&] {
                            if (dictionary) {
                                arp::reflection::hdict values(
                                    (arp::reflection::hdict::allocator_type(&resource)));
                                values.emplace(Value{0}, element);
                                values.emplace(Value{1}, Value{9});
                                return Value{std::move(values)};
                            }
                            arp::reflection::hlist values(
                                (arp::reflection::hlist::allocator_type(&resource)));
                            values.push_back(element);
                            values.emplace_back(Value{9});
                            return Value{std::move(values)};
                        };
                        Value aContainer = container(), bContainer = container();
                        const auto aIdentity = aContainer.storageIdentity(),
                                   bIdentity = bContainer.storageIdentity();
                        arp::core::pmr::Vector<Value> aArgs{{aContainer, index, prefix}, &resource};
                        arp::core::pmr::Vector<Value> bArgs{{bContainer, index, prefix}, &resource};
                        raw.setInstructionBudget(budget);
                        owned.setInstructionBudget(budget);
                        VmError a, b;
                        const auto expected =
                            raw.runFunctionByName(built.program, "test", aArgs, &a);
                        const auto actual =
                            owned.runFunctionByName(artifact.program(), "test", bArgs, &b);
                        require(a.code == b.code && a.message == b.message
                                && a.traceback == b.traceback,
                            "indexed update: each budget, overflow, zero divisor, actual type, bounds/missing replay");
                        if (a.code == ScriptDiagnosticCode::None)
                            require(expected.storageIdentity() == prefix.storageIdentity()
                                    && actual.storageIdentity() == prefix.storageIdentity(),
                                "indexed update: original prefix identity");
                        for (int slot : {0, 1}) {
                            auto x =
                                ScriptSemanticRuntime::loadIndex(aContainer, Value{slot}).value;
                            auto y =
                                ScriptSemanticRuntime::loadIndex(bContainer, Value{slot}).value;
                            require(x.type() == y.type(),
                                "indexed update: committed destination types agree");
                            if (x.is<int>())
                                require(*x.ptr<int>() == *y.ptr<int>(),
                                    "indexed update: destination values agree");
                            if (x.storageIdentity() == element.storageIdentity())
                                require(y.storageIdentity() == element.storageIdentity(),
                                    "indexed update: unmodified element identity");
                        }
                        require(aContainer.storageIdentity() == aIdentity
                                && bContainer.storageIdentity() == bIdentity,
                            "indexed update: aliases see original container storage");
                    }
                }
    }
    function.code[7].op = Opcode::IntAdd;
    function.code[6].i64 = 1;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "indexed update: limits snapshot");
    for (std::size_t limit : {3u, 4u, 5u, 65536u}) {
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "indexed update: limits bind");
        raw.setMaxFrameValues(limit);
        owned.setMaxFrameValues(limit);
        raw.setMaxContainerValues(0);
        owned.setMaxContainerValues(0);
        arp::reflection::hlist x((arp::reflection::hlist::allocator_type(&resource))),
            y(x.get_allocator());
        x.emplace_back(Value{7});
        y.emplace_back(Value{7});
        arp::core::pmr::Vector<Value> aArgs{{Value{std::move(x)}, Value{0}, prefix}, &resource};
        arp::core::pmr::Vector<Value> bArgs{{Value{std::move(y)}, Value{0}, prefix}, &resource};
        auto aSession = arp::core::makeShared<ScriptExecutionSession>();
        auto bSession = arp::core::makeShared<ScriptExecutionSession>();
        aSession->remainingInstructions = bSession->remainingInstructions = 30;
        raw.setExecutionSession(aSession);
        owned.setExecutionSession(bSession);
        VmError a, b;
        (void)raw.runFunctionByName(built.program, "test", aArgs, &a);
        (void)owned.runFunctionByName(artifact.program(), "test", bArgs, &b);
        require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                && aSession->remainingInstructions == bSession->remainingInstructions,
            "indexed update: dynamic stack/container limits and shared-session instruction count");
    }
    {
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "indexed update: quota bind");
        arp::reflection::hlist x((arp::reflection::hlist::allocator_type(&resource))),
            y(x.get_allocator());
        x.emplace_back(Value{7});
        y.emplace_back(Value{7});
        arp::core::pmr::Vector<Value> aArgs{{Value{std::move(x)}, Value{0}, prefix}, &resource};
        arp::core::pmr::Vector<Value> bArgs{{Value{std::move(y)}, Value{0}, prefix}, &resource};
        VmError a, b;
        (void)raw.runFunctionByName(built.program, "test", aArgs, &a);
        (void)owned.runFunctionByName(artifact.program(), "test", bArgs, &b);
        Value original{100000};
        aArgs[0].ptr<arp::reflection::hlist>()->at(0) = original;
        bArgs[0].ptr<arp::reflection::hlist>()->at(0) = original;
        raw.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1u));
        owned.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1u));
        (void)raw.runFunctionByName(built.program, "test", aArgs, &a);
        (void)owned.runFunctionByName(artifact.program(), "test", bArgs, &b);
        require(a.code == ScriptDiagnosticCode::ResourceLimitExceeded && a.code == b.code
                && a.message == b.message && a.traceback == b.traceback
                && aArgs[0].ptr<arp::reflection::hlist>()->at(0).storageIdentity()
                    == original.storageIdentity()
                && bArgs[0].ptr<arp::reflection::hlist>()->at(0).storageIdentity()
                    == original.storageIdentity(),
            "indexed update: active quota replays original allocation and failure PC without mutation");
    }
    // An interior jump target is independently stack-valid, but cannot enter
    // the combined update. Qualification must retain ordinary instruction PCs.
    function.code.insert(function.code.begin(),
        {Instruction{Opcode::LoadLocal, 2},
            Instruction{Opcode::LoadLocal, 0},
            Instruction{Opcode::LoadLocal, 1},
            Instruction{Opcode::LoadLocal, 0},
            Instruction{Opcode::Jump, 9}});
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
        "indexed update: incoming edge snapshot");
    const auto* ownedFunction =
        &artifact.program().functions[artifact.program().funcIndex.at("test")];
    require(artifact.hasVerifiedStackBound(ownedFunction),
        "indexed update: incoming edge fixture keeps independent stack proof");
    const auto* plan = artifact.scalarExpressions(ownedFunction);
    require(!plan || plan->entries[6].terminal != ScalarExpressionPlan::Terminal::IndexedStore,
        "indexed update: interior incoming edge rejects fusion");
}

void testIndexedLoopStages()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    for (bool dictionary : {false, true}) {
        arp::core::pmr::String source("def test(values: ", &resource);
        source += dictionary ? "{int: int}" : "[int]";
        source +=
            ", n: int, seed: int, divisor: int) -> int:\n"
            "    var i: int = 0\n    var total: int = seed\n    while i < n:\n"
            "        var j: int = i % divisor\n        values[j] = values[j] + 1\n"
            "        total = (total + values[j]) % 1000000007\n        i = i + 1\n    return total\n";
        auto built =
            arp::scriptvm::test::buildInterpreter(source, "indexed-loop.arp", "indexed-loop");
        require(built.ok(), "indexed loop: fixture");
        if (!built.ok())
            continue;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "indexed loop: snapshot");
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        int prefixPc = -1;
        if (plan)
            for (int pc = 0; pc != static_cast<int>(plan->entries.size()); ++pc)
                if (plan->entries[pc].terminal == ScalarExpressionPlan::Terminal::IndexedLoop)
                    prefixPc = pc;
        require(
            prefixPc >= 0, "indexed loop: independent index/update/tail relationship qualified");
        if (prefixPc >= 0) {
            const auto& prefix = plan->entries[prefixPc];
            const auto& update = plan->entries[prefix.rangeBody];
            arp::core::pmr::String text("wrong local", &resource);
            for (int badSlot : {update.destination, prefix.destination}) {
                ExecutionSlotBuffer locals{&resource};
                locals.resize(static_cast<std::size_t>(function.locals));
                for (auto& slot : locals)
                    slot = ScriptScalar(0);
                auto list = arp::reflection::makeEmptyHList();
                list.emplace_back(Value{1});
                locals[update.destination] = ExecutionSlot(Value{std::move(list)});
                locals[badSlot] = ExecutionSlot::borrowLiteral(text);
                const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
                std::int64_t remaining = 1000;
                int pc = prefixPc, lastPc = -1;
                require(!plan->repeatIndexed(prefix, locals, remaining, pc, lastPc)
                        && remaining == 1000 && pc == prefixPc && lastPc == -1
                        && locals[badSlot].uniqueBoxed() == nullptr,
                    "indexed loop: wrong literal base/destination leaves exact replay state and original slot kind");
                require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
                    "indexed loop: refusal cannot allocate an uncharged literal carrier");
            }
        }
        const auto container = [&](Value middle) {
            if (dictionary) {
                arp::reflection::hdict values{&resource};
                for (int i = 0; i != 7; ++i)
                    values.emplace(Value{i}, i == 2 ? middle : Value{1312});
                return Value{std::move(values)};
            }
            arp::reflection::hlist values{&resource};
            for (int i = 0; i != 7; ++i)
                values.emplace_back(i == 2 ? middle : Value{1312});
            return Value{std::move(values)};
        };
        const auto compare = [&](int seed,
                                 int divisor,
                                 Value middle,
                                 int budget,
                                 std::size_t stackLimit,
                                 bool quota) {
            Value aContainer = container(middle), bContainer = container(middle);
            const auto aIdentity = aContainer.storageIdentity(),
                       bIdentity = bContainer.storageIdentity();
            arp::core::pmr::Vector<Value> aArgs{
                {aContainer, Value{9}, Value{seed}, Value{divisor}}, &resource};
            arp::core::pmr::Vector<Value> bArgs{
                {bContainer, Value{9}, Value{seed}, Value{divisor}}, &resource};
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "indexed loop: bind");
            raw.setMaxFrameValues(stackLimit);
            owned.setMaxFrameValues(stackLimit);
            auto aSession = arp::core::makeShared<ScriptExecutionSession>();
            auto bSession = arp::core::makeShared<ScriptExecutionSession>();
            aSession->remainingInstructions = bSession->remainingInstructions = budget;
            raw.setExecutionSession(aSession);
            owned.setExecutionSession(bSession);
            if (quota) {
                raw.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u));
                owned.setMemoryQuota(
                    arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u));
            }
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", aArgs, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", bArgs, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                    && aSession->remainingInstructions == bSession->remainingInstructions,
                "indexed loop: staged budgets, fault PC/traceback, limits and actual session count");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "indexed loop: result");
            for (int i = 0; i != 7; ++i) {
                Value x = ScriptSemanticRuntime::loadIndex(aContainer, Value{i}).value;
                Value y = ScriptSemanticRuntime::loadIndex(bContainer, Value{i}).value;
                require(x.type() == y.type(), "indexed loop: staged mapped types");
                if (x.is<int>())
                    require(*x.ptr<int>() == *y.ptr<int>(),
                        "indexed loop: tail failure never replays an already committed node update");
                if (x.storageIdentity() == middle.storageIdentity())
                    require(y.storageIdentity() == middle.storageIdentity(),
                        "indexed loop: unmodified imported alias");
            }
            require(aContainer.storageIdentity() == aIdentity
                    && bContainer.storageIdentity() == bIdentity,
                "indexed loop: preserves actual container ownership and address");
        };
        for (int budget = -1; budget <= 330; ++budget) {
            compare(0, 7, Value{1312}, budget, 65536u, false);
            compare(2147483647, 7, Value{1312}, budget, 65536u, false);
        }
        for (int budget = -1; budget <= 105; ++budget) {
            compare(0, 7, Value{2147483647}, budget, 65536u, false);
            compare(0, 7, makeString(std::string_view("bad")), budget, 65536u, false);
            compare(0, 0, Value{1312}, budget, 65536u, false);
        }
        compare(0, 8, Value{1312}, -1, 65536u, false);
        for (std::size_t limit : {1u, 4u, 65536u})
            compare(0, 7, Value{1312}, 330, limit, true);
        Value cachedResult = makeInt(1313);
        const int oldResult = *cachedResult.ptr<int>();
        *cachedResult.ptr<int>() = -9;
        compare(0, 7, Value{1312}, -1, 65536u, false);
        *cachedResult.ptr<int>() = oldResult;
        Value cachedIndex = makeInt(0);
        const int oldIndex = *cachedIndex.ptr<int>();
        *cachedIndex.ptr<int>() = 1;
        compare(0, 7, Value{1312}, -1, 65536u, false);
        *cachedIndex.ptr<int>() = oldIndex;
        // Independent stack-valid incoming edge, plus metadata boundaries,
        // prohibit a borrow across the update/tail interior.
        if (prefixPc >= 0)
            for (int kind = 0; kind != 4; ++kind) {
                auto rejected = built.program;
                auto& other = rejected.functions[rejected.funcIndex.at("test")];
                if (kind == 0)
                    other.code.emplace_back(Opcode::Jump, plan->entries[prefixPc].rangeBody);
                else if (kind == 1) {
                    const int yieldPc = static_cast<int>(other.code.size()) + 1;
                    other.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
                    other.code.emplace_back(Opcode::Yield);
                    other.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
                    other.code.emplace_back(Opcode::ReturnInt);
                    BytecodeSafepoint point;
                    point.id = "borrow-negative";
                    point.resumeLabel = "resume";
                    point.pc = yieldPc;
                    point.source = SourcePos("indexed-loop.arp", 1, 1);
                    for (int slot = 0; slot != other.locals; ++slot)
                        point.canonicalSlots.push_back(slot);
                    other.safepoints.push_back(std::move(point));
                } else if (kind == 2) {
                    const int pushPc = static_cast<int>(other.code.size());
                    other.code.emplace_back(Opcode::PushHandler, pushPc + 3);
                    other.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
                    other.code.emplace_back(Opcode::PopHandler);
                    other.code.emplace_back(Opcode::ReturnInt);
                    BytecodeExceptionRegion region;
                    region.id = "borrow-negative";
                    region.handlerBlockId = "catch";
                    region.startPc = pushPc + 1;
                    region.endPc = pushPc + 2;
                    region.handlerPc = pushPc + 3;
                    region.source = SourcePos("indexed-loop.arp", 1, 1);
                    other.exceptionRegions.push_back(std::move(region));
                } else {
                    const auto& tail = plan->entries[plan->entries[prefixPc].conditionalTail];
                    other.code[tail.loopCondition].a = plan->entries[prefixPc].destination;
                }
                rejected.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(rejected);
                ValidatedInterpreterProgram blocked;
                const auto result = ValidatedInterpreterProgram::create(rejected, blocked);
                if (result.ok()) {
                    const auto* rejectedPlan = blocked.scalarExpressions(
                        &blocked.program().functions[blocked.program().funcIndex.at("test")]);
                    require(!rejectedPlan || rejectedPlan->entries[prefixPc].rangeBody < 0,
                        "indexed loop: interior entry/handler/safepoint boundary prohibits borrow");
                } else
                    require(false,
                        "indexed loop: all negative metadata retains independently valid bytecode");
            }
    }
}

std::size_t rangeProviderQueries = 0u;

bool countRangeProviderQuery(const Value&) noexcept
{
    ++rangeProviderQueries;
    return false;
}

void testRangeBodyDispatch()
{
    // A provider predicate remains observable even when it declines the
    // source. The repeated handler must execute it exactly as often as raw VM.
    static const GeneratorBridgeV2 observer{&countRangeProviderQuery};
    installGeneratorBridgeV2(&observer);
    for (const char* source :
        {"def test(values: [int], seed: int) -> int:\n    var total: int = seed\n"
         "    for item in values:\n        total = (total + item) % 1000000007\n    return total\n",
            "def test(values: [int], seed: int) -> int:\n    var total: int = seed\n"
            "    for item in range(9):\n        total = (total + item * 3 + 1) % 1000000007\n    return total\n"}) {
        auto built = arp::scriptvm::test::buildInterpreter(source, "range-body.arp", "range-body");
        require(built.ok(), "range body: fixture");
        if (!built.ok())
            continue;
        auto& function = built.program.functions[built.program.funcIndex.at("test")];
        for (auto& type : function.paramTypes)
            type = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "range body: snapshot");
        if (!artifact.valid())
            continue;
        const auto& ownedFunction =
            artifact.program().functions[artifact.program().funcIndex.at("test")];
        const auto* plan = artifact.scalarExpressions(&ownedFunction);
        int rangePc = -1;
        if (plan)
            for (int pc = 0; pc < static_cast<int>(plan->entries.size()); ++pc)
                if (plan->entries[pc].rangeBody >= 0)
                    rangePc = pc;
        require(rangePc >= 0, "range body: exact scalar body and original backedge admitted");
        Value aliased{100000};
        for (Value middle : {Value{7},
                 Value::undefined(),
                 makeString(std::string_view("bad")),
                 Value{2147483647}}) {
            auto list = arp::reflection::makeEmptyHList();
            list.emplace_back(aliased);
            list.emplace_back(middle);
            list.emplace_back(Value{11});
            for (bool empty : {false, true}) {
                Value values{empty ? arp::reflection::makeEmptyHList() : list};
                arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
                args.push_back(values);
                args.emplace_back(Value{0});
                Vm raw, owned;
                require(owned.bindProgram(artifact).ok(), "range body: bind");
                for (int budget = -1; budget <= 150; ++budget) {
                    raw.setInstructionBudget(budget);
                    owned.setInstructionBudget(budget);
                    VmError a, b;
                    rangeProviderQueries = 0;
                    const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                    const auto expectedQueries = rangeProviderQueries;
                    rangeProviderQueries = 0;
                    const auto actual =
                        owned.runFunctionByName(artifact.program(), "test", args, &b);
                    require(rangeProviderQueries == expectedQueries,
                        "range body: every original provider query remains observable");
                    if (a.code == ScriptDiagnosticCode::None)
                        require(expectedQueries > 0,
                            "range body: observer provider actually participates");
                    require(
                        a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                        "range body: every budget, empty/mixed/overflow path retains original PC and traceback");
                    if (a.code == ScriptDiagnosticCode::None)
                        requireInt(actual, *expected.ptr<int>(), "range body: scalar result");
                }
                require(aliased.storageIdentity() == list.front().storageIdentity()
                        && *aliased.ptr<int>() == 100000,
                    "range body: source elements preserve actual payload and shared identity");
            }
        }
        if (rangePc >= 0) {
            for (bool overlap : {false, true}) {
                auto rejected = built.program;
                auto& other = rejected.functions[rejected.funcIndex.at("test")];
                if (overlap)
                    other.code[rangePc].a = other.code[rangePc].b;
                else
                    other.code.emplace_back(Opcode::Jump, rangePc + 2);
                rejected.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(rejected);
                ValidatedInterpreterProgram blocked;
                require(ValidatedInterpreterProgram::create(rejected, blocked).ok(),
                    "range body: negative snapshot");
                if (blocked.valid()) {
                    const auto* otherPlan = blocked.scalarExpressions(
                        &blocked.program().functions[blocked.program().funcIndex.at("test")]);
                    require(!otherPlan || otherPlan->entries[rangePc].rangeBody < 0,
                        "range body: overlapping state or an interior incoming edge rejects splicing");
                }
            }
        }
    }
}

void testStringKeyIndexReplay()
{
    using Operation = ScalarExpressionPlan::Operation;
    auto& resource = arp::core::defaultCpuMemoryResource();
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(values: {string: int}, keys: [string], n: int, seed: int, begin: int) -> int:\n"
        "    var i: int = begin\n    var total: int = seed\n    while i < n:\n"
        "        total = (total + values[keys[i % 7]]) % 1000000007\n"
        "        i = i + 1\n    return total\n",
        "string-key.arp",
        "string-key");
    require(built.ok(), "string key: source");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    for (auto& type : function.paramTypes)
        type = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(
        ValidatedInterpreterProgram::create(built.program, artifact).ok(), "string key: snapshot");
    if (!artifact.valid())
        return;
    const auto* ownedFunction =
        &artifact.program().functions[artifact.program().funcIndex.at("test")];
    require(artifact.hasVerifiedStackBound(ownedFunction), "string key: independent stack proof");
    const auto* plan = artifact.scalarExpressions(ownedFunction);
    int bodyPc = -1, nodeOffset = -1;
    if (plan)
        for (int pc = 0; pc != static_cast<int>(plan->entries.size()); ++pc) {
            const auto& entry = plan->entries[pc];
            if (!entry.boundIntEvaluator)
                continue;
            for (int i = 0; i != entry.count; ++i)
                if (plan->nodes[entry.offset + i].operation == Operation::StringKeyIndex) {
                    bodyPc = pc;
                    nodeOffset = entry.offset + i;
                }
        }
    require(bodyPc >= 0, "string key: exact nested self-edge qualified");
    if (bodyPc < 0)
        return;
    arp::reflection::hlist keys{&resource};
    arp::reflection::hdict dictionary{&resource};
    for (const char* text : {"a", "b", "c", "d", "e", "f", "g"}) {
        Value key = makeString(std::string_view(text));
        keys.push_back(key);
        dictionary.emplace(key, Value{3});
    }
    Value keyCarrier{std::move(keys)}, dictionaryCarrier{std::move(dictionary)};
    auto* actualKeys = keyCarrier.ptr<arp::reflection::hlist>();
    auto* actualDictionary = dictionaryCarrier.ptr<arp::reflection::hdict>();
    Value element = actualDictionary->find(actualKeys->at(2))->second;
    const auto keyIdentity = actualKeys->at(2).storageIdentity();
    const auto elementIdentity = element.storageIdentity();
    Vm raw, owned;
    require(owned.bindProgram(artifact).ok(), "string key: bind");
    const auto compare =
        [&](Value values, Value names, Value n, Value seed, Value begin, int budget, bool quota) {
            arp::core::pmr::Vector<Value> args{{values, names, n, seed, begin}, &resource};
            auto aSession = arp::core::makeShared<ScriptExecutionSession>();
            auto bSession = arp::core::makeShared<ScriptExecutionSession>();
            aSession->remainingInstructions = bSession->remainingInstructions = budget;
            raw.setExecutionSession(aSession);
            owned.setExecutionSession(bSession);
            if (quota) {
                raw.setMemoryQuota(arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u));
                owned.setMemoryQuota(
                    arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u));
            } else {
                raw.setMemoryQuota(nullptr);
                owned.setMemoryQuota(nullptr);
            }
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                    && aSession->remainingInstructions == bSession->remainingInstructions,
                "string key: eviction, budget, session count, actual payload and fault PC replay");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "string key: result");
        };
    for (int state = 0; state != 7; ++state) {
        if (state == 1)
            *element.ptr<int>() = 2147483647;
        if (state == 2)
            actualDictionary->find(actualKeys->at(2))->second =
                makeString(std::string_view("wrong"));
        if (state == 3)
            actualKeys->at(2) = makeString(std::string_view("missing"));
        if (state == 4)
            actualKeys->at(2) = Value{100000};
        if (state == 5) {
            actualKeys->at(2) = makeString(std::string_view("c"));
            actualDictionary->find(actualKeys->at(2))->second = element;
            *element.ptr<int>() = 3;
        }
        for (int budget = -1; budget <= 210; ++budget)
            compare(dictionaryCarrier,
                keyCarrier,
                Value{19},
                Value{0},
                Value{state == 6 ? -1 : 0},
                budget,
                false);
    }
    require(actualKeys->at(2).storageIdentity() != keyIdentity
            && actualDictionary->find(actualKeys->at(2))->second.storageIdentity()
                == elementIdentity,
        "string key: rebinding observes replaced key carrier and retained mapped identity");
    *actualKeys->at(2).ptr<arp::core::pmr::String>() = "d";
    compare(dictionaryCarrier, keyCarrier, Value{19}, Value{0}, Value{0}, -1, false);
    require(*actualKeys->at(2).ptr<arp::core::pmr::String>() == "d",
        "string key: actual key payload changes between invocations remain visible");
    // Generic arithmetic and zero literal divisor reuse their actual checked
    // handlers. No detached key/result or synthesized diagnostic is allowed.
    for (bool zero : {false, true}) {
        auto alternate = built.program;
        auto& other = alternate.functions[alternate.funcIndex.at("test")];
        for (auto& instruction : other.code) {
            if (zero && instruction.op == Opcode::LoadInt && instruction.i64 == 7)
                instruction.i64 = 0;
            if (instruction.op >= Opcode::IntAdd && instruction.op <= Opcode::IntMod)
                instruction.op = static_cast<Opcode>(static_cast<int>(Opcode::Add)
                    + static_cast<int>(instruction.op) - static_cast<int>(Opcode::IntAdd));
        }
        alternate.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(alternate);
        ValidatedInterpreterProgram otherArtifact;
        require(ValidatedInterpreterProgram::create(alternate, otherArtifact).ok(),
            "string key: generic/zero snapshot");
        Vm aVm, bVm;
        require(bVm.bindProgram(otherArtifact).ok(), "string key: generic/zero bind");
        arp::core::pmr::Vector<Value> args{
            {dictionaryCarrier, keyCarrier, Value{19}, Value{0}, Value{0}}, &resource};
        for (int budget = -1; budget <= 130; ++budget) {
            aVm.setInstructionBudget(budget);
            bVm.setInstructionBudget(budget);
            VmError a, b;
            const auto expected = aVm.runFunctionByName(alternate, "test", args, &a);
            const auto actual = bVm.runFunctionByName(otherArtifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "string key: generic/zero divisor retains each budget and original failure PC");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "string key: generic result");
        }
    }
    actualKeys->resize(2);
    compare(dictionaryCarrier, keyCarrier, Value{19}, Value{0}, Value{0}, -1, false);
    for (std::size_t limit : {1u, 4u, 65536u}) {
        raw.setMaxFrameValues(limit);
        owned.setMaxFrameValues(limit);
        compare(dictionaryCarrier, keyCarrier, Value{19}, Value{0}, Value{0}, 200, true);
    }
    raw.setMaxFrameValues(65536u);
    owned.setMaxFrameValues(65536u);
    compare(Value::native_reference(actualDictionary),
        keyCarrier,
        Value{2},
        Value{0},
        Value{0},
        -1,
        false);
    compare(dictionaryCarrier,
        Value::native_reference(actualKeys),
        Value{2},
        Value{0},
        Value{0},
        -1,
        false);
    const arp::reflection::hdict cvDictionary{*actualDictionary};
    const arp::reflection::hlist cvKeys{*actualKeys};
    compare(Value{cvDictionary}, Value{cvKeys}, Value{2}, Value{0}, Value{0}, -1, false);
    const auto& entry = plan->entries[bodyPc];
    const auto& node = plan->nodes[nodeOffset];
    ExecutionSlotBuffer locals{&resource};
    locals.resize(static_cast<std::size_t>(function.locals));
    const auto prepare = [&] {
        for (auto& slot : locals)
            slot = ScriptScalar(0);
        locals[node.operand] = dictionaryCarrier;
        locals[node.stringKey.keys] = keyCarrier;
        locals[2] = ScriptScalar(2);
    };
    prepare();
    {
        std::int64_t budget = -1;
        int pc = bodyPc, last = -1;
        plan->repeatInteger(entry, locals, budget, pc, last);
    }
    prepare();
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    {
        std::int64_t budget = -1;
        int pc = bodyPc, last = -1;
        require(plan->repeatInteger(entry, locals, budget, pc, last),
            "string key: actual borrow executes");
    }
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "string key: warm borrow/cache/read/commit has no allocation");
    arp::core::pmr::String literal("invalid container", &resource);
    for (int slot : {node.operand, node.stringKey.keys}) {
        prepare();
        locals[slot] = ExecutionSlot::borrowLiteral(literal);
        const auto allocation = arp::core::defaultCpuAllocator().stats().allocationCount;
        std::int64_t budget = 1000;
        int pc = bodyPc, last = -1;
        require(!plan->repeatInteger(entry, locals, budget, pc, last) && budget == 1000
                && pc == bodyPc && last == -1 && locals[slot].uniqueBoxed() == nullptr,
            "string key: wrong Literal refuses without materializing or changing replay state");
        require(arp::core::defaultCpuAllocator().stats().allocationCount == allocation,
            "string key: refusal allocates nothing");
    }
    // Independent incoming edge and suspension metadata forbid borrowing.
    for (int kind = 0; kind != 5; ++kind) {
        auto rejected = built.program;
        auto& other = rejected.functions[rejected.funcIndex.at("test")];
        if (kind == 0) {
            other.code.emplace_back(Opcode::LoadLocal, entry.destination);
            other.code.emplace_back(Opcode::Jump, bodyPc + 1);
        } else if (kind == 1) {
            const int yieldPc = static_cast<int>(other.code.size()) + 1;
            other.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
            other.code.emplace_back(Opcode::Yield);
            other.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
            other.code.emplace_back(Opcode::ReturnInt);
            BytecodeSafepoint point;
            point.id = "nested-negative";
            point.resumeLabel = "resume";
            point.pc = yieldPc;
            point.source = SourcePos("string-key.arp", 1, 1);
            for (int slot = 0; slot != other.locals; ++slot)
                point.canonicalSlots.push_back(slot);
            other.safepoints.push_back(std::move(point));
        } else if (kind == 2) {
            const int pushPc = static_cast<int>(other.code.size());
            other.code.emplace_back(Opcode::PushHandler, pushPc + 3);
            other.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
            other.code.emplace_back(Opcode::PopHandler);
            other.code.emplace_back(Opcode::ReturnInt);
            BytecodeExceptionRegion region;
            region.id = "nested-negative";
            region.handlerBlockId = "catch";
            region.startPc = pushPc + 1;
            region.endPc = pushPc + 2;
            region.handlerPc = pushPc + 3;
            region.source = SourcePos("string-key.arp", 1, 1);
            other.exceptionRegions.push_back(std::move(region));
        } else {
            other.code[bodyPc + (kind == 3 ? 1 : 2)].a =
                kind == 3 ? entry.destination : entry.stepLocal;
        }
        rejected.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(rejected);
        ValidatedInterpreterProgram blocked;
        require(ValidatedInterpreterProgram::create(rejected, blocked).ok(),
            "string key: valid negative snapshot");
        if (blocked.valid()) {
            const auto* rejectedFunction =
                &blocked.program().functions[blocked.program().funcIndex.at("test")];
            if (kind != 1 && kind != 2)
                require(blocked.hasVerifiedStackBound(rejectedFunction),
                    "string key: incoming/overlap retains independent stack proof");
            const auto* blockedPlan = blocked.scalarExpressions(rejectedFunction);
            require(!blockedPlan || !blockedPlan->entries[bodyPc].boundIntEvaluator,
                "string key: incoming/handler/safepoint/overlap prohibits closed borrow");
        }
    }
    // Restoring the method still invalidates the stock gate. Keep this last:
    // subsequent string-key reads must use real reflected comparison.
    auto* metadata = const_cast<arp::reflection::class_info*>(
        arp::reflection::type_of<arp::core::pmr::String>()->class_());
    const Value original = metadata->get_field("__lt__");
    metadata->def("__lt__", original);
    prepare();
    std::int64_t budget = 1000;
    int pc = bodyPc, last = -1;
    require(!plan->repeatInteger(entry, locals, budget, pc, last) && pc == bodyPc && budget == 1000,
        "string key: public ordering mutation rejects previously qualified topology");
    compare(dictionaryCarrier, keyCarrier, Value{2}, Value{0}, Value{0}, -1, false);
}

void testReadonlyDictionaryReplay()
{
    for (const char* divisor : {"5", "7"}) {
        arp::core::pmr::String source(
            "def test(n: int, bad: int) -> int:\n    var values: {int: int} = {0: 3, 1: 5, 2: 7, 3: 11, 4: 13, 5: bad}\n"
            "    var i: int = 0\n    var total: int = 0\n    while i < n:\n        total = (total + values[i % ",
            &arp::core::defaultCpuMemoryResource());
        source += divisor;
        source += "]) % 1000000007\n        i = i + 1\n    return total\n";
        auto built =
            arp::scriptvm::test::buildInterpreter(source, "readonly-dict.arp", "readonly-dict");
        require(built.ok(), "readonly dictionary: source");
        if (!built.ok())
            continue;
        for (auto& type : built.program.functions[built.program.funcIndex.at("test")].paramTypes)
            type = RuntimeType::Any;
        built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
        ValidatedInterpreterProgram artifact;
        require(ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "readonly dictionary: snapshot");
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "readonly dictionary: bind");
        for (Value bad : {Value{17},
                 Value::undefined(),
                 makeString(std::string_view("wrong")),
                 Value{2147483647}}) {
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.push_back(Value{15});
            args.push_back(bad);
            for (int budget = -1; budget <= 200; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "readonly dictionary: eviction/missing/actual type/overflow replay original PC after successful lookups");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual,
                        *expected.ptr<int>(),
                        "readonly dictionary: cached and uncached values agree");
            }
        }
    }
}

void testRepeatedCollectionReplay()
{
    auto built = arp::scriptvm::test::buildInterpreter(
        "def test(values: [int], n: int, seed: int) -> int:\n"
        "    var i: int = 0\n    var total: int = seed\n"
        "    while i < n:\n        total = (total + values[i]) % 1000000007\n"
        "        i = i + 1\n    return total\n",
        "bound-index.arp",
        "bound-index");
    require(built.ok(), "bound index: fixture");
    if (!built.ok())
        return;
    auto& function = built.program.functions[built.program.funcIndex.at("test")];
    function.paramTypes[0] = function.paramTypes[1] = function.paramTypes[2] = RuntimeType::Any;
    built.program.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(built.program);
    ValidatedInterpreterProgram artifact;
    require(
        ValidatedInterpreterProgram::create(built.program, artifact).ok(), "bound index: snapshot");
    if (!artifact.valid())
        return;
    const auto* plan = artifact.scalarExpressions(
        &artifact.program().functions[artifact.program().funcIndex.at("test")]);
    bool boundIndex = false;
    if (plan)
        for (const auto& entry : plan->entries)
            if (entry.boundIntEvaluator)
                for (int i = 0; i < entry.count; ++i)
                    boundIndex |= plan->nodes[entry.offset + i].operation
                        == ScalarExpressionPlan::Operation::Index;
    require(boundIndex, "bound index: exact readonly self edge admitted");
    Value aliased{100000};
    for (Value middle : {Value{5}, makeString(std::string_view("bad")), Value{2147483647}}) {
        auto values = arp::reflection::makeEmptyHList();
        values.emplace_back(aliased);
        values.emplace_back(middle);
        values.emplace_back(Value{7});
        Value container{std::move(values)};
        for (Value limit : {Value{3}, Value{4}, Value{3.5}}) {
            arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
            args.push_back(container);
            args.push_back(limit);
            args.emplace_back(Value{0});
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "bound index: bind");
            for (int budget = -1; budget <= 120; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError expectedError, actualError;
                const auto expected =
                    raw.runFunctionByName(built.program, "test", args, &expectedError);
                const auto actual =
                    owned.runFunctionByName(artifact.program(), "test", args, &actualError);
                require(expectedError.code == actualError.code
                        && expectedError.message == actualError.message
                        && expectedError.traceback == actualError.traceback,
                    "bound index: each budget, missing element, wrong type and overflow replay after prior commits");
                if (expectedError.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "bound index: actual values agree");
                require(container.ptr<arp::reflection::hlist>()->front().storageIdentity()
                            == aliased.storageIdentity()
                        && *aliased.ptr<int>() == 100000,
                    "bound index: imported element identity retained");
            }
        }
    }
}

void testPrivateListReuse()
{
    const char* source =
        "def test(n: int) -> int:\n    var i: int = 0\n    var total: int = 0\n"
        "    while i < n:\n        var values: [int] = [i, i + 1, i + 2, i + 3]\n"
        "        total = total + values[0] + values[3]\n        i = i + 1\n    return total\n";
    const char* dictionarySource =
        "def test(n: int) -> int:\n    var i: int = 0\n    var total: int = 0\n"
        "    while i < n:\n        var values: {int: int} = {0: i, 1: i + 1, 2: i + 2, 3: i + 3}\n"
        "        total = total + values[0] + values[3]\n        i = i + 1\n    return total\n";
    for (const auto* fixture : {source, dictionarySource}) {
        auto built =
            arp::scriptvm::test::buildInterpreter(fixture, "private-list.arp", "private-list");
        ValidatedInterpreterProgram artifact;
        require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "private list: snapshot");
        if (!artifact.valid())
            return;
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        int sites = 0;
        if (plan)
            for (const auto& entry : plan->entries)
                sites += entry.listDestination >= 0 || entry.dictionaryDestination >= 0;
        require(sites == 1, "private list: exact local nonescape proof");
        int producerSites = 0;
        if (plan)
            for (const auto& entry : plan->entries)
                producerSites +=
                    entry.terminal == ScalarExpressionPlan::Terminal::ConstructionOperands
                    || entry.terminal == ScalarExpressionPlan::Terminal::ConstructionLoop;
        require(producerSites == 1, "private construction: integer operands lowered together");
        if (plan && producerSites == 1) {
            Program incoming = built.program;
            auto& function = incoming.functions[incoming.funcIndex.at("test")];
            const auto producer =
                std::find_if(plan->entries.begin(), plan->entries.end(), [](const auto& entry) {
                    return entry.terminal == ScalarExpressionPlan::Terminal::ConstructionOperands
                        || entry.terminal == ScalarExpressionPlan::Terminal::ConstructionLoop;
                });
            Instruction edge;
            edge.op = Opcode::Jump;
            edge.a = static_cast<int>(producer - plan->entries.begin()) + 1;
            function.code.push_back(edge);
            incoming.bytecodeFingerprint = computeInterpreterBytecodeFingerprint(incoming);
            ValidatedInterpreterProgram blocked;
            require(ValidatedInterpreterProgram::create(incoming, blocked).ok(),
                "construction operands: incoming-edge snapshot");
            if (blocked.valid()) {
                const auto* rejected = blocked.scalarExpressions(
                    &blocked.program().functions[blocked.program().funcIndex.at("test")]);
                if (rejected)
                    for (const auto& entry : rejected->entries)
                        require(
                            entry.terminal != ScalarExpressionPlan::Terminal::ConstructionOperands
                                && entry.terminal
                                    != ScalarExpressionPlan::Terminal::ConstructionLoop,
                            "construction operands: incoming interior edge rejects combined roots");
            }
        }
        if (fixture == dictionarySource && plan) {
            bool integerKeys = false;
            for (const auto& node : plan->nodes)
                if (node.operation == ScalarExpressionPlan::Operation::Index)
                    integerKeys |= node.index.dictionary;
            require(
                integerKeys, "private dictionary: literal-key proof uses canonical stack effects");
        }
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "private list: bind");
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.emplace_back(Value{7});
        const auto compare = [&] {
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                "private list: budget, quota, original PC and errors agree");
            if (a.code == ScriptDiagnosticCode::None)
                requireInt(actual,
                    *expected.ptr<int>(),
                    "private list: Value and cached payload semantics agree");
        };
        for (int budget = -1; budget <= 300; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            compare();
        }
        raw.setInstructionBudget(-1);
        owned.setInstructionBudget(-1);
        raw.setMaxContainerValues(3);
        owned.setMaxContainerValues(3);
        compare();
        raw.setMaxContainerValues(4);
        owned.setMaxContainerValues(4);
        Value cached = makeInt(3);
        const int original = *cached.ptr<int>();
        *cached.ptr<int>() = 300000;
        compare();
        *cached.ptr<int>() = original;
        const auto measure = [&](int n) {
            args[0] = Value{n};
            VmError error;
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            const auto value = owned.runFunctionByName(artifact.program(), "test", args, &error);
            const auto count = arp::core::defaultCpuAllocator().stats().allocationCount - before;
            require(error.code == ScriptDiagnosticCode::None, "private list: allocation run");
            requireInt(value, n * n + 2 * n, "private list: repeated replacement values");
            return count;
        };
        measure(2000);
        const auto small = measure(1000), large = measure(2000);
        require(large < small * 2,
            "private list: carrier and vector allocations do not grow per construction");
    }
    const char* negatives[]{
        "def test() -> int:\n    var values: [int] = [1]\n    var alias: [int] = values\n    values = [2]\n    return alias[0]\n",
        "def test() -> [int]:\n    var values: [int] = [1]\n    return values\n",
        "def test(values: [int]) -> int:\n    values = [2]\n    return values[0]\n",
        "def test() -> int:\n    var values: [int] = [1]\n    values[0] = 2\n    return values[0]\n"};
    for (const auto* negative : negatives) {
        auto other =
            arp::scriptvm::test::buildInterpreter(negative, "escaped-list.arp", "escaped-list");
        ValidatedInterpreterProgram blocked;
        require(other.ok() && ValidatedInterpreterProgram::create(other.program, blocked).ok(),
            "private list: escape fixture");
        if (!blocked.valid())
            continue;
        const auto* rejected = blocked.scalarExpressions(
            &blocked.program().functions[blocked.program().funcIndex.at("test")]);
        if (rejected)
            for (const auto& entry : rejected->entries)
                require(entry.listDestination < 0,
                    "private list: alias, return, parameter and mutation rejected");
    }
}

void testConstructionOperandReplay()
{
    using Plan = ScalarExpressionPlan;
    Plan plan;
    Plan::Entry entry;
    entry.terminal = Plan::Terminal::ConstructionOperands;
    entry.cost = 9;
    entry.lastPc = 18;
    entry.nextPc = 19;
    plan.nodes = {{Plan::Operation::Local, 0},
        {Plan::Operation::Local, 0},
        {Plan::Operation::Constant, 7},
        {Plan::Operation::Add},
        {Plan::Operation::Local, 0},
        {Plan::Operation::Local, 1},
        {Plan::Operation::Div}};
    entry.count = static_cast<int>(plan.nodes.size());
    core::pmr::Vector<Plan::Node> roots{&core::defaultCpuMemoryResource()};
    plan.lowerOperands(entry, roots);
    entry.offset = static_cast<int>(plan.nodes.size());
    entry.destination = static_cast<int>(roots.size());
    entry.count = 0;
    plan.nodes.insert(plan.nodes.end(), roots.begin(), roots.end());
    require(entry.destination == 3 && entry.calculationCount == 2,
        "construction operands: shared multi-root lowering");
    Value carrier{1312};
    core::pmr::String literal{"prefix", &core::defaultCpuMemoryResource()};
    ExecutionSlotBuffer locals, stack;
    locals.emplace_back(carrier);
    locals.emplace_back(ScriptScalar(2));
    std::int64_t coldBudget = 9;
    int coldPc = 10, coldLast = 9, coldScratch[2];
    require(!plan.executeConstructionOperands(
                entry, locals, stack, coldScratch, coldBudget, coldPc, coldLast)
            && stack.empty() && coldBudget == 9 && coldPc == 10,
        "construction operands: insufficient reserved stack replays without allocation");
    stack.reserve(4);
    stack.emplace_back(ExecutionSlot::borrowLiteral(literal));
    int scratch[2], pc = 10, last = 9;
    std::int64_t budget = 8;
    require(!plan.executeConstructionOperands(entry, locals, stack, scratch, budget, pc, last)
            && pc == 10 && last == 9 && budget == 8 && stack.size() == 1,
        "construction operands: short budget leaves all effects for original replay");
    budget = 9;
    locals[1] = ScriptScalar(0);
    require(!plan.executeConstructionOperands(entry, locals, stack, scratch, budget, pc, last)
            && stack.size() == 1 && budget == 9 && pc == 10 && last == 9,
        "construction operands: late zero divisor cannot partially push early roots");
    locals[1] = ScriptScalar(2);
    *carrier.ptr<int>() = std::numeric_limits<int>::max();
    require(!plan.executeConstructionOperands(entry, locals, stack, scratch, budget, pc, last)
            && stack.size() == 1 && budget == 9 && pc == 10,
        "construction operands: actual mutable payload overflow replays");
    *carrier.ptr<int>() = 1312;
    locals[1] = makeString(std::string_view("wrong"));
    require(!plan.executeConstructionOperands(entry, locals, stack, scratch, budget, pc, last)
            && stack.size() == 1 && budget == 9,
        "construction operands: wrong actual payload rejected");
    locals[1] = ScriptScalar(2);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    require(plan.executeConstructionOperands(entry, locals, stack, scratch, budget, pc, last)
            && pc == 19 && last == 18 && budget == 0 && stack.size() == 4,
        "construction operands: exact original cost and PC");
    require(stack[0].immutableLiteral() == &literal
            && stack[1].boxed().storageIdentity() == carrier.storageIdentity()
            && *stack[2].scalarPointer<int>() == 1319 && *stack[3].scalarPointer<int>() == 656,
        "construction operands: prefix, bare carrier identity and new computed values");
    require(core::defaultCpuAllocator().stats().allocationCount == allocations,
        "construction operands: qualified commit needs no owned allocation");
    for (const char* container :
        {"[seed, seed + 1, seed / divisor]", "{0: seed, 1: seed + 1, 2: seed / divisor}"}) {
        core::pmr::String source{"def test(seed: any, divisor: any) -> any:\n    var values: ",
            &core::defaultCpuMemoryResource()};
        source += container[0] == '[' ? "[any] = " : "{int: any} = ";
        source += container;
        source += "\n    return values[1]\n";
        auto built = arp::scriptvm::test::buildInterpreter(
            source, "construction-replay.arp", "construction-replay");
        ValidatedInterpreterProgram artifact;
        require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "construction operands: replay snapshot");
        if (!artifact.valid())
            continue;
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "construction operands: bind");
        for (const auto seed : {Value{1312},
                 Value{std::numeric_limits<int>::max()},
                 Value{1.5},
                 makeString(std::string_view("bad"))}) {
            for (const int divisor : {0, 2}) {
                core::pmr::Vector<Value> arguments{
                    {seed, Value{divisor}}, &core::defaultCpuMemoryResource()};
                for (int limit : {2, 65536})
                    for (bool quota : {false, true})
                        for (int budget = -1; budget <= 25; ++budget) {
                            raw.setMaxFrameValues(limit);
                            owned.setMaxFrameValues(limit);
                            raw.setMemoryQuota(quota
                                    ? core::makeShared<core::CpuMemoryQuota>(1024u * 1024u)
                                    : nullptr);
                            owned.setMemoryQuota(quota
                                    ? core::makeShared<core::CpuMemoryQuota>(1024u * 1024u)
                                    : nullptr);
                            auto aSession = core::makeShared<ScriptExecutionSession>(),
                                 bSession = core::makeShared<ScriptExecutionSession>();
                            aSession->remainingInstructions = bSession->remainingInstructions =
                                budget;
                            raw.setExecutionSession(aSession);
                            owned.setExecutionSession(bSession);
                            VmError a, b;
                            const auto expected =
                                raw.runFunctionByName(built.program, "test", arguments, &a);
                            const auto actual =
                                owned.runFunctionByName(artifact.program(), "test", arguments, &b);
                            require(a.code == b.code && a.message == b.message
                                    && a.traceback == b.traceback
                                    && aSession->remainingInstructions
                                        == bSession->remainingInstructions,
                                "construction operands: budget, session, late failures, quota and frame limit replay");
                            if (a.code == ScriptDiagnosticCode::None) {
                                if (expected.is<int>())
                                    requireInt(actual,
                                        *expected.ptr<int>(),
                                        "construction operands: integer result");
                                else
                                    require(actual.is<double>()
                                            && *actual.ptr<double>() == *expected.ptr<double>(),
                                        "construction operands: dynamic float fallback");
                            }
                        }
            }
        }
    }
}

void testClosedConstructionReplay()
{
    using Plan = ScalarExpressionPlan;
    for (bool dictionary : {false, true}) {
        core::pmr::String source{
            "def test(n: any, seed: any, index: int, divisor: any) -> any:\n"
            "    var i: int = 0\n    var total: int = 0\n    while i < n:\n        var values: ",
            &core::defaultCpuMemoryResource()};
        source += dictionary ? "{int: int} = {0: i + seed, 1: i + seed + 1, 2: i + seed + 2}\n"
                             : "[any] = [i + seed, i + seed + 1, i + seed + 2]\n";
        source +=
            "        total = (total + values[index]) % divisor\n        i = i + 1\n    return total\n";
        auto built = arp::scriptvm::test::buildInterpreter(
            source, "construction-loop.arp", "construction-loop");
        if (!built.ok())
            std::printf("construction loop fixture (%s): %s\n",
                dictionary ? "dictionary" : "list",
                built.error.message.c_str());
        if (built.ok()) {
            built.program.functions[built.program.funcIndex.at("test")].paramTypes[2] =
                RuntimeType::Any;
            built.program.bytecodeFingerprint =
                computeInterpreterBytecodeFingerprint(built.program);
        }
        ValidatedInterpreterProgram artifact;
        require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "construction loop: snapshot");
        if (!artifact.valid())
            continue;
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        bool qualified = false;
        if (plan)
            for (const auto& entry : plan->entries)
                qualified |= entry.terminal == Plan::Terminal::ConstructionLoop;
        require(qualified, "construction loop: exact multi-stage self edge qualifies");
        const auto compare = [&](Value n,
                                 Value seed,
                                 Value index,
                                 Value divisor,
                                 int budget,
                                 std::size_t limit,
                                 bool quota) {
            core::pmr::Vector<Value> args{
                {n, seed, index, divisor}, &core::defaultCpuMemoryResource()};
            Vm raw, owned;
            require(owned.bindProgram(artifact).ok(), "construction loop: bind");
            raw.setMaxFrameValues(limit);
            owned.setMaxFrameValues(limit);
            auto aSession = core::makeShared<ScriptExecutionSession>(),
                 bSession = core::makeShared<ScriptExecutionSession>();
            aSession->remainingInstructions = bSession->remainingInstructions = budget;
            raw.setExecutionSession(aSession);
            owned.setExecutionSession(bSession);
            if (quota) {
                raw.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(1024u * 1024u));
                owned.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(1024u * 1024u));
            }
            VmError a, b;
            const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
            const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
            require(a.code == b.code && a.message == b.message && a.traceback == b.traceback
                    && aSession->remainingInstructions == bSession->remainingInstructions,
                "construction loop: every budget, staged original PC/traceback/session and quota/frame admission");
            if (a.code == ScriptDiagnosticCode::None) {
                if (expected.is<int>())
                    requireInt(actual,
                        *expected.ptr<int>(),
                        "construction loop: actual post-commit integer result");
                else
                    require(actual.is<double>() && *actual.ptr<double>() == *expected.ptr<double>(),
                        "construction loop: mixed float replay");
            }
        };
        for (int budget = -1; budget <= 210; ++budget) {
            compare(Value{4}, Value{100000}, Value{1}, Value{1000000007}, budget, 65536u, false);
            compare(Value{4},
                Value{std::numeric_limits<int>::max() - 3},
                Value{1},
                Value{1000000007},
                budget,
                65536u,
                false);
            compare(Value{4}, Value{100000}, Value{1}, Value{0}, budget, 65536u, false);
        }
        int nativeSeed = 100000;
        for (Value seed :
            {Value{1.5}, makeString(std::string_view("bad")), Value{std::ref(nativeSeed)}}) {
            for (int budget = -1; budget <= 80; ++budget)
                compare(Value{4}, seed, Value{1}, Value{1000000007}, budget, 65536u, false);
        }
        for (Value index : {Value{-1},
                 Value{9},
                 Value{1.5},
                 makeString(std::string_view("bad")),
                 Value{std::ref(nativeSeed)}})
            compare(Value{4}, Value{100000}, index, Value{1000000007}, -1, 65536u, false);
        compare(Value{4}, Value{100000}, Value{1}, Value{1000000007}, -1, 2u, false);
        compare(Value{4}, Value{100000}, Value{1}, Value{1000000007}, -1, 65536u, true);
        compare(makeString(std::string_view("wrong")),
            Value{100000},
            Value{1},
            Value{1000000007},
            -1,
            65536u,
            false);
        Value cached = makeInt(1002);
        const int original = *cached.ptr<int>();
        *cached.ptr<int>() = -19;
        for (int budget = -1; budget <= 150; ++budget)
            compare(Value{4}, Value{1000}, Value{1}, Value{1000000007}, budget, 65536u, false);
        *cached.ptr<int>() = original;
        if (dictionary) {
            Value key = makeInt(1);
            const int old = *key.ptr<int>();
            *key.ptr<int>() = -7;
            for (int budget = -1; budget <= 150; ++budget)
                compare(
                    Value{4}, Value{100000}, Value{1}, Value{1000000007}, budget, 65536u, false);
            *key.ptr<int>() = old;
        }
    }
}

void testPrivateStringAppend()
{
    constexpr auto source = "def test(n: int) -> int:\n"
                            "    var text: string = \"\"\n"
                            "    var i: int = 0\n"
                            "    while i < n:\n"
                            "        text = text + \"x\"\n"
                            "        i = i + 1\n"
                            "    return len(text)\n";
    constexpr auto repeated = "def test(n: int) -> int:\n"
                              "    var text: string = \"\"\n"
                              "    var i: int = 0\n"
                              "    var total: int = 0\n"
                              "    while i < n:\n"
                              "        var part: string = text + \"x\"\n"
                              "        total = total + len(part)\n"
                              "        i = i + 1\n"
                              "    return total\n";
    for (const auto* fixture : {source, repeated}) {
        auto built =
            arp::scriptvm::test::buildInterpreter(fixture, "private-string.arp", "private-string");
        ValidatedInterpreterProgram artifact;
        require(built.ok() && ValidatedInterpreterProgram::create(built.program, artifact).ok(),
            "private string: validated source");
        if (!artifact.valid())
            return;
        const auto* plan = artifact.scalarExpressions(
            &artifact.program().functions[artifact.program().funcIndex.at("test")]);
        int appends = 0;
        if (plan)
            for (const auto& entry : plan->entries)
                if (entry.appendLocal >= 0) {
                    ++appends;
                    require(entry.cost >= 4
                            && entry.terminal == ScalarExpressionPlan::Terminal::AppendString,
                        "private string: original instruction cost and explicit terminal");
                }
        require(appends == 1, "private string: exact nonescaping append proved");
        if (fixture == source) {
            const auto iterator =
                std::find_if(plan->entries.begin(), plan->entries.end(), [](const auto& entry) {
                    return entry.boundString;
                });
            require(iterator != plan->entries.end(),
                "private string: uninterrupted self edge bound once");
            if (iterator != plan->entries.end()) {
                const auto& entry = *iterator;
                const int start = static_cast<int>(iterator - plan->entries.begin());
                const auto& function =
                    artifact.program().functions[artifact.program().funcIndex.at("test")];
                ExecutionSlotBuffer locals;
                locals.resize(function.locals);
                locals[0] = ScriptScalar(10);
                locals[entry.stepLocal] = ScriptScalar(0);
                arp::core::pmr::String text("owned", &arp::core::defaultCpuMemoryResource());
                text.reserve(64);
                text.resize(text.capacity() - 2, 'x');
                const auto capacity = text.capacity();
                locals[entry.appendLocal] = makeString(std::move(text));
                const auto identity = locals[entry.appendLocal].boxed().storageIdentity();
                auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
                std::int64_t remaining = 10 * entry.cost;
                int pc = start, lastPc = -1;
                bool rejected = false;
                try {
                    arp::core::CpuMemoryQuotaScope scope(quota);
                    plan->repeatString(entry, locals, "x", remaining, pc, lastPc);
                } catch (const std::bad_alloc&) {
                    rejected = true;
                }
                require(rejected && remaining == 8 * entry.cost && pc == start
                        && lastPc == entry.appendLiteralPc + 1
                        && *locals[entry.stepLocal].scalarPointer<int>() == 2
                        && locals[entry.appendLocal].boxed().storageIdentity() == identity
                        && locals[entry.appendLocal].boxed().ptr<arp::core::pmr::String>()->size()
                            == capacity
                        && quota->liveBytes() == 0,
                    "private string: repeated quota failure retains prior commits, exact fault PC and budget");
                locals[entry.stepLocal] = ScriptScalar(std::numeric_limits<int>::max());
                remaining = entry.cost;
                pc = start;
                lastPc = -1;
                require(!plan->repeatString(entry, locals, "x", remaining, pc, lastPc)
                        && remaining == entry.cost && pc == start && lastPc == -1
                        && locals[entry.appendLocal].boxed().ptr<arp::core::pmr::String>()->size()
                            == capacity,
                    "private string: checked step fails before any string mutation");
                locals[entry.stepLocal] = ScriptScalar(0);
                locals[0] = ScriptScalar(10.5);
                require(!plan->repeatString(entry, locals, "x", remaining, pc, lastPc)
                        && locals[entry.appendLocal].boxed().ptr<arp::core::pmr::String>()->size()
                            == capacity,
                    "private string: mixed condition fails before borrowing or mutation");
            }
        }
        Vm raw, owned;
        require(owned.bindProgram(artifact).ok(), "private string: owned bind");
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        args.emplace_back(Value{7});
        for (int budget = -1; budget <= 130; ++budget) {
            raw.setInstructionBudget(budget);
            owned.setInstructionBudget(budget);
            VmError expectedError, actualError;
            const auto expected =
                raw.runFunctionByName(built.program, "test", args, &expectedError);
            const auto actual =
                owned.runFunctionByName(artifact.program(), "test", args, &actualError);
            require(expectedError.code == actualError.code
                    && expectedError.message == actualError.message
                    && expectedError.traceback == actualError.traceback,
                "private string: each intermediate budget preserves diagnostic and PC");
            if (expectedError.code == ScriptDiagnosticCode::None)
                requireInt(actual, *expected.ptr<int>(), "private string: original result");
        }
        for (Value limit : {Value{7.5}, makeString(std::string_view("wrong"))}) {
            args[0] = limit;
            for (int budget = -1; budget <= 160; ++budget) {
                raw.setInstructionBudget(budget);
                owned.setInstructionBudget(budget);
                VmError a, b;
                const auto expected = raw.runFunctionByName(built.program, "test", args, &a);
                const auto actual = owned.runFunctionByName(artifact.program(), "test", args, &b);
                require(a.code == b.code && a.message == b.message && a.traceback == b.traceback,
                    "private string: mixed and wrong actual loop limit replays before mutation");
                if (a.code == ScriptDiagnosticCode::None)
                    requireInt(actual, *expected.ptr<int>(), "private string: mixed limit result");
            }
        }
        owned.setInstructionBudget(-1);
        const auto measure = [&](int n) {
            args[0] = Value{n};
            VmError error;
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            const auto result = owned.runFunctionByName(artifact.program(), "test", args, &error);
            const auto count = arp::core::defaultCpuAllocator().stats().allocationCount - before;
            require(error.code == ScriptDiagnosticCode::None,
                "private string: allocation run succeeds");
            requireInt(result, n, "private string: allocation run length");
            return count;
        };
        measure(1000);
        const auto small = measure(1000), large = measure(2000);
        require(
            large < small * 2, "private string: allocation growth is sublinear in append count");
    }
    const char* negatives[]{
        "def test(n: int) -> int:\n    var text: string = \"\"\n    var saved: string = text\n    text = text + \"x\"\n    return len(saved)\n",
        "def test(n: int) -> string:\n    var text: string = \"\"\n    text = text + \"x\"\n    return text\n",
        "def test(text: string) -> int:\n    text = text + \"x\"\n    return len(text)\n",
        "def observe(text: string) -> int:\n    return len(text)\ndef test(n: int) -> int:\n    var text: string = \"\"\n    text = text + \"x\"\n    return observe(text)\n"};
    for (const auto* negative : negatives) {
        auto other =
            arp::scriptvm::test::buildInterpreter(negative, "escaped-string.arp", "escaped-string");
        ValidatedInterpreterProgram blocked;
        require(other.ok() && ValidatedInterpreterProgram::create(other.program, blocked).ok(),
            "private string: escape fixture compiles");
        if (!blocked.valid())
            continue;
        const auto* rejected = blocked.scalarExpressions(
            &blocked.program().functions[blocked.program().funcIndex.at("test")]);
        if (rejected)
            for (const auto& entry : rejected->entries)
                require(entry.appendLocal < 0,
                    "private string: alias, return, parameter and call edges rejected");
    }
}

} // namespace

int main()
{
    testEmptyFrameCarrier();
    testIntegerCacheQuotaScope();
    testYieldIntegerCacheBorrow();
    testBorrowedLookupCache();
    testReadonlyDictionaryReplay();
    testRangeBodyDispatch();
    testGeneratorFrameAllocation();
    testResumableIndependentExpressions();
    testInheritedExecutionQuota();
    testRepeatedExpressionReplay();
    testRightExpressionHandlers();
    testDictionaryKeyProofBoundary();
    testExpressionStackPrefix();
    testCollectionExpressionOperands();
    testIndexedIntegerUpdateReplay();
    testIndexedLoopStages();
    testRepeatedCollectionReplay();
    testPrivateListReuse();
    testConstructionOperandReplay();
    testClosedConstructionReplay();
    testPrivateStringAppend();
    testExplicitExpressionOperands();
    testExtendedExpressionOperations();
    testScalarArgumentLeafCalls();
    testScalarCaptureLeafCalls();
    testStaticCallStackProof();
    testBoundMethodStackProof();
    testMethodArgumentTransfer();
    testClosedMethodLeafLoop();
    testClosedCalculationLoop();
    testClosedClosureLeafLoop();
    testMethodLoopReplayBoundaries();
    testSelectedArgumentLeafCalls();
    testStaticCallArgumentTransfer();
    testStaticSlotReturnIdentity();
    testGuardedArgumentReturns();
    testScalarCallReplay();
    testScalarFloatDispatch();
    testIndexedStringComparison();
    testConditionalLoopGuards();
    testFloatingExpressionExecution();
    testIntegerExpressionTerminals();
    testVarDecl();
    testArithmetic();
    testExtendedArithmetic();
    testIf();
    testElif();
    testWhile();
    testForRange();
    testBreakContinue();
    testFunctionCall();
    testClassInheritance();
    testFibonacci();
    testStringConcat();
    testComparison();
    testCompoundAssign();
    testListConstruction();
    testDictConstruction();
    testClosureCapture();
    testClosureSharedState();
    testClosureIndependentInstances();
    testUnary();
    testLogicalAnd();
    testLogicalOr();
    testNestedIf();
    testReturnVoid();
    testFibLarge();
    testBytesPipeline();
    testValidatedArtifact();
    testImmutableHandlerFilters();
    testUnobservedCatchLocal();
    testImmutableRaisePayload();
    testIndependentHandlerExpressions();
    testInlineDirectCalls();
    testInlineStrings();
    testInlineIndirectCalls();
    testActualMethodReceiverLookup();
    testVerifiedReleaseExecution();
    testOperationSpecializationBoundaries();
    testFusedScalarAllocation();
    testInlineIndexAllocation();
    testScalarExpressionPlans();
    testIntegerExpressionExecution();
    testStringKeyIndexReplay();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript compiler tests passed\n");
    return 0;
}
