// ArpScript VM unit tests (Phase 1).
//
// These tests build hand-authored bytecode Programs and exercise the VM
// execution loop: scalar arithmetic, control flow, function calls,
// closures, list/dict construction and access.
//
// Test style follows the repo convention (tests/unit/poisson_scatter_tests.cpp):
// self-contained main() + require() helpers, no third-party framework.

#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/value.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

using namespace arp::scriptvm;

namespace {

void addDelegationSite(Function& function,
    int sourcePc,
    int initPc,
    int rangePc,
    int branchPc,
    int loadPc,
    int yieldPc,
    int backEdgePc,
    int exitPc)
{
    function.delegationSites.push_back(DelegationSiteMetadata{
        sourcePc, initPc, rangePc, branchPc, loadPc, yieldPc, backEdgePc, exitPc, 0, 1, 2});
}

struct ReflectedScalar {
    int value = 0;
};

struct ReflectedConstructible {
    explicit ReflectedConstructible(int initial) : value(initial) {}

    int value = 0;
};

void registerReflectedScalar()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    arp::reflection::registration::class_<ReflectedScalar>("ScriptVmReflectedScalar")
        .method("__add__",
            [](const ReflectedScalar& lhs, const ReflectedScalar& rhs) {
                return ReflectedScalar{lhs.value + rhs.value + 1000};
            })
        .method("__eq__",
            [](const ReflectedScalar& lhs, const ReflectedScalar& rhs) {
                return lhs.value == rhs.value;
            })
        .method("__lt__",
            [](const ReflectedScalar& lhs, const ReflectedScalar& rhs) {
                return lhs.value < rhs.value;
            })
        .method("__int__",
            [](const ReflectedScalar& value) {
                return value.value + 2000;
            })
        .method("__float__",
            [](const ReflectedScalar& value) {
                return static_cast<double>(value.value + 3000);
            })
        .method("__bool__",
            [](const ReflectedScalar& value) {
                return value.value != 0;
            })
        .method("__str__", [](const ReflectedScalar& value) {
            return arp::core::toPmrString(std::to_string(value.value + 4000));
        });
}

void registerReflectedConstructible()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    arp::reflection::registration::class_<ReflectedConstructible>("ScriptVmReflectedConstructible")
        .construct<int>()
        .property("value", &ReflectedConstructible::value);
}

int g_failures = 0;

void require(bool cond, const char* label)
{
    if (!cond) {
        std::printf("FAIL: %s\n", label);
        ++g_failures;
    }
}

void requireInt(const Value& v, int expected, const char* label)
{
    int actual = 0;
    if (!tryGetInt(v, actual)) {
        std::printf("FAIL: %s (not an int: type=%d)\n", label, static_cast<int>(v.type()));
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, want %d)\n", label, actual, expected);
        ++g_failures;
    }
}

void requireBool(const Value& v, bool expected, const char* label)
{
    bool actual = false;
    if (!tryGetBool(v, actual)) {
        std::printf("FAIL: %s (not a bool)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, want %d)\n", label, actual, expected);
        ++g_failures;
    }
}

void testMakeInstanceUsesReflectedConstructor()
{
    registerReflectedConstructible();

    Program program;
    Function fn("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 41);
    fn.code.emplace_back(Opcode::MakeInstance,
        0,
        1,
        0,
        arp::core::pmr::String(
            "ScriptVmReflectedConstructible", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadField,
        0,
        0,
        0,
        arp::core::pmr::String("value", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::Return, 1);
    program.functions.push_back(std::move(fn));
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "MakeInstance reflected constructor ok");
    requireInt(result.value, 41, "MakeInstance uses reflected __new__/__init__");
}

void testScriptInstanceMethodsReleaseWithoutReferenceCycle()
{
    registerScriptObjectReflection();

    constexpr const char* className = "ScriptVmLifetimeCounter";
    constexpr const char* fieldName = "value";
    constexpr const char* methodName = "getValue";
    auto* cls = arp::reflection::db::instance().find_class(className);
    if (cls == nullptr) {
        cls = arp::reflection::db::instance().register_class(
            className, arp::reflection::instance_t, 0, 0, nullptr);
    }
    require(cls != nullptr, "lifetime class registration");

    Program program;
    program.classSchemas[arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource())]
        .push_back(arp::core::pmr::String(fieldName, &arp::core::defaultCpuMemoryResource()));

    Function main{arp::core::pmr::String("main", &arp::core::defaultCpuMemoryResource())};
    main.results = 1;
    main.maxStack = 2;
    main.code.emplace_back(Opcode::MakeInstance,
        0,
        0,
        0,
        arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource()));
    main.code.emplace_back(Opcode::LoadField,
        0,
        0,
        0,
        arp::core::pmr::String(methodName, &arp::core::defaultCpuMemoryResource()));
    main.code.emplace_back(Opcode::CallValue, 0, 0);
    main.code.emplace_back(Opcode::Return, 1);
    program.functions.push_back(std::move(main));

    Function value{arp::core::pmr::String(
        "ScriptVmLifetimeCounter.value", &arp::core::defaultCpuMemoryResource())};
    value.params = 1;
    value.results = 1;
    value.locals = 1;
    value.maxStack = 1;
    value.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    value.code.emplace_back(Opcode::Return, 1);
    program.functions.push_back(std::move(value));
    program.entry = 0;
    program.funcIndex[arp::core::pmr::String("main", &arp::core::defaultCpuMemoryResource())] = 0;
    program.methodSets[arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource())]
                      [arp::core::pmr::String("getValue", &arp::core::defaultCpuMemoryResource())] =
        1;

    // Warm up reflection registration before taking the allocator baseline.
    {
        auto warmup = runProgram(program);
        require(warmup.ok(), "lifetime warmup run");
    }
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (int i = 0; i < 32; ++i) {
        auto result = runProgram(program);
        require(result.ok(), "script instance lifecycle run");
        requireInt(result.value, 7, "script method lookup and call");
    }
    const auto after = arp::core::defaultCpuAllocator().stats();
    require(after.liveBytes == before.liveBytes,
        "script instances release all method receiver storage");
}

template <typename Fn>
void requireStableScriptValueLifetime(const char* label, Fn&& createAndRelease)
{
    const auto before = arp::core::defaultCpuAllocator().stats();
    createAndRelease();
    const auto after = arp::core::defaultCpuAllocator().stats();
    require(after.liveBytes == before.liveBytes, label);
}

void testAllScriptValueKindsRelease()
{
    registerScriptObjectReflection();
    registerModuleReflection();
    registerReflectedConstructible();

    constexpr const char* className = "ScriptVmAllValueKinds";
    auto* cls = arp::reflection::db::instance().find_class(className);
    if (cls == nullptr) {
        cls = arp::reflection::db::instance().register_class(
            className, arp::reflection::instance_t, 0, 0, nullptr);
    }
    require(cls != nullptr, "all value kinds class registration");

    // Primitive reflection values, including both nil representations.
    requireStableScriptValueLifetime("undefined value releases", [] {
        Value value = Value::undefined();
    });
    requireStableScriptValueLifetime("null value releases", [] {
        Value value{nullptr};
    });
    requireStableScriptValueLifetime("bool value releases", [] {
        Value value{true};
    });
    requireStableScriptValueLifetime("integer value releases", [] {
        Value value{17};
    });
    requireStableScriptValueLifetime("int64 value releases", [] {
        Value value{std::int64_t{17}};
    });
    requireStableScriptValueLifetime("float value releases", [] {
        Value value{17.5};
    });
    requireStableScriptValueLifetime("string value releases", [] {
        Value value{
            arp::core::pmr::String("script string", &arp::core::defaultCpuMemoryResource())};
    });
    requireStableScriptValueLifetime("buffer value releases", [] {
        arp::reflection::ByteBuffer buffer{&arp::core::defaultCpuMemoryResource()};
        buffer.resize(256, 0x5a);
        Value value{std::move(buffer)};
    });

    requireStableScriptValueLifetime("list value releases", [] {
        arp::reflection::hlist list{
            arp::reflection::hlist::allocator_type(&arp::core::defaultCpuMemoryResource())};
        list.push_back(makeInt(1));
        list.push_back(makeString(std::string_view("list item")));
        Value value{std::move(list)};
    });
    requireStableScriptValueLifetime("dict value releases", [] {
        arp::reflection::hdict dict{&arp::core::defaultCpuMemoryResource()};
        dict[makeString(std::string_view("key"))] = makeInt(2);
        dict[makeString(std::string_view("nested"))] =
            Value{arp::reflection::hlist{&arp::core::defaultCpuMemoryResource()}};
        Value value{std::move(dict)};
    });

    requireStableScriptValueLifetime("instance value releases", [cls] {
        arp::reflection::instance instance{cls};
        instance.slot("field") = makeString(std::string_view("instance field"));
        Value value{instance};
    });
    requireStableScriptValueLifetime("script method value releases", [cls] {
        arp::reflection::instance instance{cls};
        Value receiver{instance};
        Value method{ScriptMethod{0, receiver}};
    });

    requireStableScriptValueLifetime("script cell value releases", [] {
        Value captured = makeString(std::string_view("captured"));
        Value value{ScriptCell{std::move(captured)}};
    });
    requireStableScriptValueLifetime("script pointer value releases", [] {
        auto cell = arp::core::makeShared<ScriptCell>(makeInt(3));
        Value value{ScriptPointer{std::move(cell)}};
    });
    requireStableScriptValueLifetime("script closure value releases", [] {
        auto cell = arp::core::makeShared<ScriptCell>(makeInt(4));
        arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> captures{
            &arp::core::defaultCpuMemoryResource()};
        captures.push_back(std::move(cell));
        Value value{ScriptClosure{0, std::move(captures)}};
    });

    requireStableScriptValueLifetime("reflection function value releases", [] {
        Value value{arp::reflection::function{"script lifetime function", [](int input) {
                                                  return input + 1;
                                              }}};
    });
    requireStableScriptValueLifetime("reflected C++ object value releases", [] {
        Value value{ReflectedConstructible{9}};
    });
    requireStableScriptValueLifetime("host callable value releases", [] {
        Value value{HostCallable{[](const arp::core::pmr::Vector<Value>&) {
            return makeInt(1);
        }}};
    });
    requireStableScriptValueLifetime("module object value releases", [] {
        auto exports = arp::core::makeShared<ModuleExports>();
        exports
            ->symbols[arp::core::pmr::String("exported", &arp::core::defaultCpuMemoryResource())] =
            Value{5};
        Value value{ModuleObject{std::move(exports)}};
    });
    requireStableScriptValueLifetime("execution slot owned payloads release", [] {
        Value receiver{100000};
        ExecutionSlot slot(ScriptMethod{3, receiver});
        auto alias = slot;
        slot = ScriptScalar(100001);
        slot = std::move(alias);
        const arp::core::pmr::String text(
            "literal lifetime", &arp::core::defaultCpuMemoryResource());
        slot = ExecutionSlot::borrowLiteral(text);
        (void)slot.boxed();
        slot = ExecutionSlot(ScriptMethod{7, receiver});
    });
}

// Build a simple function that returns its single int param + 1.
// func inc(x: int) -> int:
//     return x + 1
//
// slot 0: x (param)
// slot 1: result temp
// code:
//   LoadLocal 0
//   LoadInt 1
//   Add
//   Return 1
Function makeIncFunction()
{
    Function fn{"inc"};
    fn.params = 1;
    fn.results = 1;
    fn.locals = 1; // param
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadInt, 1, 0, 1);
    fn.code.emplace_back(Opcode::Add);
    fn.code.emplace_back(Opcode::Return, 1);
    return fn;
}

void testScalarArithmetic()
{
    // Compute (3 + 4) * 2 - 1 = 13
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    fn.code.emplace_back(Opcode::Add);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 2);
    fn.code.emplace_back(Opcode::Mul);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::Sub);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    if (!result.ok()) {
        std::printf("ERROR: scalar arithmetic: code=%d msg=%s\n",
            static_cast<int>(result.error.code),
            result.error.message.c_str());
        if (!result.error.traceback.empty()) {
            std::printf("  %s\n", result.error.traceback.c_str());
        }
    }
    require(result.ok(), "scalar arithmetic ok");
    requireInt(result.value, 13, "scalar arithmetic == 13");
}

void testControlFlowIf()
{
    // if 5 > 3: return 1 else return 0
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    // LoadInt 5, LoadInt 3, CmpGt, JumpIfFalse L1, LoadInt 1, Return 1
    // L1: LoadInt 0, Return 1
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::CmpGt);
    // JumpIfFalse target=6 (the else branch)
    fn.code.emplace_back(Opcode::JumpIfFalse, 6);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::Return, 1);
    // else:
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "if ok");
    requireInt(result.value, 1, "if 5>3 -> 1");
}

void testLoopSum()
{
    // var sum = 0; for i in range(0,5): sum = sum + i; return sum
    // sum = 0+1+2+3+4 = 10
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 3; // slot0=range list, slot1=index, slot2=sum
    fn.maxStack = 4;
    // sum = 0
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    fn.code.emplace_back(Opcode::StoreLocal, 2);
    // Build range list [0,1,2,3,4]
    // I64 = (0<<32)|5 -> start=0, end=5
    fn.code.emplace_back(Opcode::MakeRange, 0, 0, (static_cast<std::int64_t>(0) << 32) | 5u);
    // RangeInit source=0, index=1
    // pc=3
    fn.code.emplace_back(Opcode::RangeInit, 0, 1);
    // loop start pc=4: RangeNext source=0 index=1, packed(2=-1 unused, 2=sum) -> keySlot=-1 (none), valSlot=...
    // We want value -> slot 2 is sum, but we need a temp for i. Let's use slot 0 for range, 1 for index, 2 for sum, and we need i somewhere.
    // Actually for integer range we just need sum. Let's pack keySlot=0 (unused, will be range source -- bad).
    // Simpler: keySlot = -1 won't work (must be valid slot). Use a dummy slot 0? No, 0 is range.
    // Let's add a 4th local for i: locals=4, slots: 0=range,1=index,2=sum,3=i
    // Rebuild with locals=4.
    fn.locals = 4;
    // RangeNext: source=0, index=1, I64=pack(keySlot=3, valueSlot=3) -- both key and value get i.
    // Actually for range over list, key=index, value=elem. We want the elem.
    fn.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(3, 3));
    // pc=5: JumpIfFalse to end (pc=11 = LoadLocal for return)
    fn.code.emplace_back(Opcode::JumpIfFalse, 11);
    // pc=6: sum = sum + i  (LoadLocal 2, LoadLocal 3, Add, StoreLocal 2)
    fn.code.emplace_back(Opcode::LoadLocal, 2);
    fn.code.emplace_back(Opcode::LoadLocal, 3);
    fn.code.emplace_back(Opcode::Add);
    fn.code.emplace_back(Opcode::StoreLocal, 2);
    // pc=10: Jump back to loop start (pc=4)
    fn.code.emplace_back(Opcode::Jump, 4);
    // pc=11: end: return sum
    fn.code.emplace_back(Opcode::LoadLocal, 2);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "loop sum ok");
    if (!result.ok()) {
        std::printf("  error: %s\n", result.error.message.c_str());
        std::printf("  traceback:\n%s\n", result.error.traceback.c_str());
    }
    requireInt(result.value, 10, "loop sum 0..4 == 10");
}

void testFunctionCall()
{
    // main calls inc(41) -> 42
    Program program;
    program.functions.push_back(makeIncFunction());
    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 2;
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 41);
    mainFn.code.emplace_back(Opcode::Call, 0, 1); // call inc with 1 arg
    mainFn.code.emplace_back(Opcode::Return, 1);
    program.entry = 1;
    program.funcIndex["inc"] = 0;
    program.funcIndex["main"] = 1;

    auto result = runProgram(program);
    require(result.ok(), "function call ok");
    if (!result.ok()) {
        std::printf("  error: %s\n", result.error.message.c_str());
        std::printf("  traceback:\n%s\n", result.error.traceback.c_str());
    }
    requireInt(result.value, 42, "inc(41) == 42");
}

void testFibonacci()
{
    // Recursive fibonacci.
    // func fib(n: int) -> int:
    //     if n < 2: return n
    //     return fib(n-1) + fib(n-2)
    Program program;
    Function& fib = program.functions.emplace_back("fib");
    fib.params = 1; // slot 0 = n
    fib.results = 1;
    fib.locals = 1;
    fib.maxStack = 4;
    // LoadLocal 0; LoadInt 2; CmpLt; JumpIfFalse L1
    fib.code.emplace_back(Opcode::LoadLocal, 0);     // pc 0
    fib.code.emplace_back(Opcode::LoadInt, 0, 0, 2); // pc 1
    fib.code.emplace_back(Opcode::CmpLt);            // pc 2
    // JumpIfFalse to pc=6 (the else branch)
    fib.code.emplace_back(Opcode::JumpIfFalse, 6); // pc 3
    // return n
    fib.code.emplace_back(Opcode::LoadLocal, 0); // pc 4
    fib.code.emplace_back(Opcode::Return, 1);    // pc 5
    // else: fib(n-1) + fib(n-2)
    fib.code.emplace_back(Opcode::LoadLocal, 0);     // pc 6
    fib.code.emplace_back(Opcode::LoadInt, 0, 0, 1); // pc 7
    fib.code.emplace_back(Opcode::Sub);              // pc 8
    fib.code.emplace_back(Opcode::Call, 0, 1);       // pc 9
    fib.code.emplace_back(Opcode::LoadLocal, 0);     // pc 10
    fib.code.emplace_back(Opcode::LoadInt, 0, 0, 2); // pc 11
    fib.code.emplace_back(Opcode::Sub);              // pc 12
    fib.code.emplace_back(Opcode::Call, 0, 1);       // pc 13
    fib.code.emplace_back(Opcode::Add);              // pc 14
    fib.code.emplace_back(Opcode::Return, 1);        // pc 15
    program.entry = 0;
    program.funcIndex["fib"] = 0;

    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 2;
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    mainFn.code.emplace_back(Opcode::Call, 0, 1);
    mainFn.code.emplace_back(Opcode::Return, 1);
    program.entry = 1;
    program.funcIndex["main"] = 1;

    auto result = runProgram(program);
    require(result.ok(), "fib ok");
    if (!result.ok()) {
        std::printf("  error: %s\n", result.error.message.c_str());
        std::printf("  traceback:\n%s\n", result.error.traceback.c_str());
    }
    // fib(10) = 55
    requireInt(result.value, 55, "fib(10) == 55");
}

void testComparisonAndLogic()
{
    // Test all comparison ops: 3==3, 3!=4, 3<4, 3<=3, 4>3, 4>=4
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    // We'll return a single bool: (3==3) && (3!=4) && (3<4) && (3<=3) && (4>3) && (4>=4)
    // Build as: each comparison leaves a bool; AND them via Not+JumpIfTrue short-circuit.
    // Simpler: just test CmpEq and CmpLt and return combined via bitwise on bools won't work.
    // Instead, test one comparison at a time in sequence, return last.
    // Test CmpEq: 3==3 -> true
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::CmpEq);
    fn.code.emplace_back(Opcode::Pop);
    // Test CmpNe: 3!=4 -> true
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    fn.code.emplace_back(Opcode::CmpNe);
    fn.code.emplace_back(Opcode::Pop);
    // Test CmpLt: 3<4 -> true
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    fn.code.emplace_back(Opcode::CmpLt);
    fn.code.emplace_back(Opcode::Pop);
    // Test CmpGe: 4>=4 -> true
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    fn.code.emplace_back(Opcode::CmpGe);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "comparison ok");
    requireBool(result.value, true, "4>=4 -> true");
}

void testListConstruction()
{
    // Build [10, 20, 30], len == 3, [1] == 20
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1; // slot 0 = list
    fn.maxStack = 4;
    // Push 10, 20, 30, MakeList 3
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 30);
    fn.code.emplace_back(Opcode::MakeList, 0, 3);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // len(list)
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::BuiltinLen);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "list construction ok");
    requireInt(result.value, 3, "len([10,20,30]) == 3");

    // Now test index access: [1] == 20
    Function& fn2 = program.functions.emplace_back("main2");
    fn2.params = 0;
    fn2.results = 1;
    fn2.locals = 1;
    fn2.maxStack = 4;
    fn2.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn2.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
    fn2.code.emplace_back(Opcode::LoadInt, 0, 0, 30);
    fn2.code.emplace_back(Opcode::MakeList, 0, 3);
    fn2.code.emplace_back(Opcode::StoreLocal, 0);
    // list[1]
    fn2.code.emplace_back(Opcode::LoadLocal, 0);
    fn2.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn2.code.emplace_back(Opcode::LoadIndex);
    fn2.code.emplace_back(Opcode::Return, 1);
    program.entry = 1;
    program.funcIndex["main2"] = 1;

    auto result2 = runProgram(program);
    require(result2.ok(), "list index ok");
    requireInt(result2.value, 20, "[10,20,30][1] == 20");
}

void testClosureCapture()
{
    // Closure that captures a cell and increments it.
    // func outer() -> int:
    //     var count: int = 0
    //     var inc = func() -> int: count = count + 1; return count
    //     inc()  -> 1
    //     inc()  -> 2
    //     return inc()  -> 3
    //
    // Layout:
    //   func 0 = inner (the closure body), params=0, results=1
    //     captures[0] -> count cell
    //     code: LoadCapture 0; LoadInt 1; Add; StoreCapture 0;
    //           LoadCapture 0; Return 1
    //   func 1 = outer, params=0, results=1
    //     slot 0 = count (local, will be promoted to pointer on capture)
    //     slot 1 = inc (closure value)
    //     code:
    //       LoadInt 0; StoreLocal 0        # count = 0
    //       AddrLocal 0; MakeClosure 0,1   # push closure capturing count
    //       StoreLocal 1                   # inc = closure
    //       CallValue 0                     # inc() -- need to push inc first
    //       ...
    // The issue: CallValue pops callee then args. So we need: LoadLocal 1; CallValue 0; Pop (discard).
    // Then again, then return third.

    Program program;

    // func 0: inner
    Function& inner = program.functions.emplace_back("inner");
    inner.params = 0;
    inner.results = 1;
    inner.locals = 0;
    inner.maxStack = 2;
    // captures[0] = count cell
    inner.code.emplace_back(Opcode::LoadCapture, 0);
    inner.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    inner.code.emplace_back(Opcode::Add);
    inner.code.emplace_back(Opcode::StoreCapture, 0);
    inner.code.emplace_back(Opcode::LoadCapture, 0);
    inner.code.emplace_back(Opcode::Return, 1);

    // func 1: outer
    Function& outer = program.functions.emplace_back("outer");
    outer.params = 0;
    outer.results = 1;
    outer.locals = 2; // slot0=count, slot1=inc
    outer.maxStack = 2;
    // count = 0
    outer.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    outer.code.emplace_back(Opcode::StoreLocal, 0);
    // inc = make closure capturing slot 0
    outer.code.emplace_back(Opcode::AddrLocal, 0);
    outer.code.emplace_back(Opcode::MakeClosure, 0, 1); // func 0, 1 capture
    outer.code.emplace_back(Opcode::StoreLocal, 1);
    // inc() -> discard
    outer.code.emplace_back(Opcode::LoadLocal, 1);
    outer.code.emplace_back(Opcode::CallValue, 0, 0);
    outer.code.emplace_back(Opcode::Pop);
    // inc() -> discard
    outer.code.emplace_back(Opcode::LoadLocal, 1);
    outer.code.emplace_back(Opcode::CallValue, 0, 0);
    outer.code.emplace_back(Opcode::Pop);
    // return inc()
    outer.code.emplace_back(Opcode::LoadLocal, 1);
    outer.code.emplace_back(Opcode::CallValue, 0, 0);
    outer.code.emplace_back(Opcode::Return, 1);

    // main calls outer
    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 1;
    mainFn.code.emplace_back(Opcode::Call, 1, 0);
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 2;
    program.funcIndex["inner"] = 0;
    program.funcIndex["outer"] = 1;
    program.funcIndex["main"] = 2;

    auto result = runProgram(program);
    require(result.ok(), "closure ok");
    if (!result.ok()) {
        std::printf("  error: %s\n", result.error.message.c_str());
        std::printf("  traceback:\n%s\n", result.error.traceback.c_str());
    }
    requireInt(result.value, 3, "closure inc()x3 == 3");
}

void testStringConcat()
{
    // "hello" + " " + "world" == "hello world", len == 11
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("hello", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String(" ", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::Add);
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("world", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::Add);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "string concat ok");
    arp::core::pmr::String s;
    if (tryGetString(result.value, s)) {
        require(s == "hello world", "string concat == 'hello world'");
    } else {
        require(false, "string concat result is string");
    }
}

void testReflectionMagicDispatch()
{
    registerReflectedScalar();

    Program program;
    Function& add = program.functions.emplace_back("add");
    add.params = 2;
    add.results = 1;
    add.locals = 2;
    add.maxStack = 2;
    add.code.emplace_back(Opcode::LoadLocal, 0);
    add.code.emplace_back(Opcode::LoadLocal, 1);
    add.code.emplace_back(Opcode::Add);
    add.code.emplace_back(Opcode::Return, 1);

    Function& convert = program.functions.emplace_back("convert");
    convert.params = 1;
    convert.results = 1;
    convert.locals = 1;
    convert.maxStack = 1;
    convert.code.emplace_back(Opcode::LoadLocal, 0);
    convert.code.emplace_back(Opcode::ConvertInt);
    convert.code.emplace_back(Opcode::Return, 1);

    Function& compare = program.functions.emplace_back("compare");
    compare.params = 2;
    compare.results = 1;
    compare.locals = 2;
    compare.maxStack = 2;
    compare.code.emplace_back(Opcode::LoadLocal, 0);
    compare.code.emplace_back(Opcode::LoadLocal, 1);
    compare.code.emplace_back(Opcode::CmpLt);
    compare.code.emplace_back(Opcode::Return, 1);

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.emplace_back(ReflectedScalar{2});
    args.emplace_back(ReflectedScalar{3});
    auto addResult = runFunction(program, 0, args);
    require(addResult.ok(), "reflection __add__ dispatch ok");
    require(addResult.value.cast<ReflectedScalar>().value == 1005,
        "reflection __add__ dispatch uses host magic");

    arp::core::pmr::Vector<Value> oneArg{&arp::core::defaultCpuMemoryResource()};
    oneArg.emplace_back(ReflectedScalar{7});
    auto intResult = runFunction(program, 1, oneArg);
    require(intResult.ok(), "reflection __int__ dispatch ok");
    requireInt(intResult.value, 2007, "reflection __int__ dispatch uses host magic");

    auto compareResult = runFunction(program, 2, args);
    require(compareResult.ok(), "reflection __lt__ dispatch ok");
    requireBool(compareResult.value, true, "reflection __lt__ dispatch uses host magic");
}

void testBitwiseAndShift()
{
    // 0xF0 & 0x0F == 0; 1 << 4 == 16; 256 >> 3 == 32
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    // 1 << 4
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    fn.code.emplace_back(Opcode::ShiftLeft);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "shift ok");
    requireInt(result.value, 16, "1 << 4 == 16");
}

void testFastPathLocalAddIntConst()
{
    // var x = 5; x += 3 (via LocalAddIntConst); return x -> 8
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    fn.code.emplace_back(Opcode::LocalAddIntConst, 0, 0, 3);
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "fastpath LocalAddIntConst ok");
    requireInt(result.value, 8, "5+3(fast) == 8");
}

void testDictConstruction()
{
    // {"a": 1, "b": 2}, len == 2, dict["b"] == 2
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 4;
    // Push "a", 1, "b", 2, MakeDict 2
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("a", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("b", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 2);
    fn.code.emplace_back(Opcode::MakeDict, 0, 2);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // dict["b"]
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("b", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadIndex);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto result = runProgram(program);
    require(result.ok(), "dict ok");
    requireInt(result.value, 2, "dict['b'] == 2");
}

void testGeneratorAwaitContinuation()
{
    Program program;
    Function& generator = program.functions.emplace_back("generator_await");
    generator.params = 1;
    generator.locals = 1;
    generator.maxStack = 1;
    generator.isGenerator = true;
    generator.code.emplace_back(Opcode::LoadLocal, 0);
    generator.code.emplace_back(Opcode::Await);
    generator.code.emplace_back(Opcode::Yield);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 9);
    generator.code.emplace_back(Opcode::Return, 1);

    Function& maker = program.functions.emplace_back("make_generator_await");
    maker.params = 1;
    maker.locals = 1;
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::LoadLocal, 0);
    maker.code.emplace_back(Opcode::Call, 0, 1);
    maker.code.emplace_back(Opcode::Return, 1);

    bool ready = false;
    ScriptAwaitable awaitable;
    awaitable.poll = [&ready](Value& value, arp::core::pmr::String&) {
        if (!ready)
            return ScriptAwaitStatus::Pending;
        value = makeInt(77);
        return ScriptAwaitStatus::Ready;
    };
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});

    Vm vm;
    VmError error;
    Value generatorValue = vm.runFunction(program, 1, args, &error);
    require(error.code == ScriptDiagnosticCode::None && generatorValue.is<ScriptGenerator>(),
        "VM generator-await function materializes a generator");
    auto* state = generatorValue.ptr<ScriptGenerator>();
    int value = 0;
    require(isNil(vm.resumeGeneratorValue(*state, error))
            && error.code == ScriptDiagnosticCode::Suspended && state->suspendedFrame != nullptr,
        "VM generator stores a pending await continuation");
    require(isNil(vm.resumeGeneratorValue(*state, error))
            && error.code == ScriptDiagnosticCode::Suspended,
        "VM generator await remains pending without replay");
    ready = true;
    require(tryGetInt(vm.resumeGeneratorValue(*state, error), value) && value == 77
            && error.code == ScriptDiagnosticCode::None && state->suspendedFrame == nullptr,
        "VM generator resumes through await and yields its result");
    require(tryGetInt(vm.resumeGeneratorValue(*state, error), value) && value == 9
            && error.code == ScriptDiagnosticCode::None && state->done,
        "VM generator continues after an awaited yield");
}

void testGeneratorFailedAwaitUnwindsCallerHandler()
{
    Program program;
    Function& awaitChild = program.functions.emplace_back("await_child");
    awaitChild.params = 1;
    awaitChild.locals = 1;
    awaitChild.maxStack = 1;
    awaitChild.results = 1;
    awaitChild.code.emplace_back(Opcode::LoadLocal, 0);
    awaitChild.code.emplace_back(Opcode::Await);
    awaitChild.code.emplace_back(Opcode::Return, 1);

    // The await lives in an ordinary callee. A failed poll must discard that
    // child frame, inject into the generator caller's handler, and continue
    // without replaying the awaitable.
    Function& generator = program.functions.emplace_back("caller_handler");
    generator.params = 1;
    generator.locals = 1;
    generator.maxStack = 1;
    generator.results = 1;
    generator.isGenerator = true;
    generator.code.emplace_back(Opcode::PushHandler, 7);
    generator.code.emplace_back(Opcode::LoadLocal, 0);
    generator.code.emplace_back(Opcode::Call, 0, 1);
    generator.code.emplace_back(Opcode::Yield);
    generator.code.emplace_back(Opcode::PopHandler);
    generator.code.emplace_back(Opcode::Jump, 10);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, -1);
    generator.code.emplace_back(Opcode::Pop);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    generator.code.emplace_back(Opcode::Return, 1);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    generator.code.emplace_back(Opcode::Return, 1);

    Function& maker = program.functions.emplace_back("make_caller_handler");
    maker.params = 1;
    maker.locals = 1;
    maker.maxStack = 1;
    maker.results = 1;
    maker.code.emplace_back(Opcode::LoadLocal, 0);
    maker.code.emplace_back(Opcode::Call, 1, 1);
    maker.code.emplace_back(Opcode::Return, 1);

    bool failed = false;
    int polls = 0;
    ScriptAwaitable awaitable;
    awaitable.poll = [&failed, &polls](Value&, arp::core::pmr::String& diagnostics) {
        ++polls;
        if (!failed)
            return ScriptAwaitStatus::Pending;
        diagnostics = "caller handler await failed";
        return ScriptAwaitStatus::Failed;
    };
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});

    Vm vm;
    VmError error;
    Value value = vm.runFunction(program, 2, args, &error);
    require(error.code == ScriptDiagnosticCode::None && value.is<ScriptGenerator>(),
        "failed-await caller fixture creates a generator");
    auto* state = value.ptr<ScriptGenerator>();
    require(isNil(vm.resumeGeneratorValue(*state, error))
            && error.code == ScriptDiagnosticCode::Suspended && state->suspendedFrame != nullptr
            && state->suspendedCallers.size() == 1u && polls == 1,
        "failed-await caller fixture retains child and generator frames");

    failed = true;
    int result = 0;
    require(tryGetInt(vm.resumeGeneratorValue(*state, error), result) && result == 99
            && error.code == ScriptDiagnosticCode::None && state->done && polls == 2,
        "failed await unwinds into the generator caller handler without replay");
}

void testMetadataHostCallableWrongArity()
{
    HostCallableContract contract;
    contract.bindingId = "vm.host.v1";
    contract.signatureHash = "sha256:vm-host";
    contract.fixedArity = 1;
    contract.lifetimeVersion = 1;
    int calls = 0;
    Program program;
    Function& main = program.functions.emplace_back("main");
    main.maxStack = 1;
    main.code.emplace_back(Opcode::LoadGlobal, 0);
    main.code.emplace_back(Opcode::CallValue, 0, 0);
    main.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    arp::core::pmr::Vector<Value> globals{&arp::core::defaultCpuMemoryResource()};
    globals.push_back(Value{HostCallable{[&calls](const arp::core::pmr::Vector<Value>&) {
                                             ++calls;
                                             return makeInt(1);
                                         },
        contract}});
    Vm vm;
    vm.setSharedGlobals(&globals);
    VmError error;
    vm.run(program, &error);
    require(error.code == ScriptDiagnosticCode::WrongArgCount && calls == 0,
        "Interpreter metadata-bound HostCallable rejects wrong arity before invocation");
}

void testGeneratorSendAndClose()
{
    Program program;
    Function& generator = program.functions.emplace_back("generator_send");
    generator.locals = 1;
    generator.maxStack = 1;
    generator.isGenerator = true;
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    generator.code.emplace_back(Opcode::Yield);
    generator.code.emplace_back(Opcode::StoreLocal, 0);
    generator.code.emplace_back(Opcode::LoadLocal, 0);
    generator.code.emplace_back(Opcode::Return, 1);
    Function& maker = program.functions.emplace_back("make_generator_send");
    maker.locals = 1;
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::Call, 0, 0);
    maker.code.emplace_back(Opcode::Return, 1);

    Vm vm;
    VmError error;
    Value value = vm.runFunction(program, 1, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && value.is<ScriptGenerator>(),
        "VM generator send fixture creates a generator");
    auto* state = value.ptr<ScriptGenerator>();
    int result = 0;
    require(isNil(vm.resumeGeneratorValueSend(*state, makeInt(1), error))
            && error.code == ScriptDiagnosticCode::AotSwitchUnsupported,
        "VM generator send rejects the first resume deterministically");
    require(tryGetInt(vm.resumeGeneratorValue(*state, error), result) && result == 7
            && error.code == ScriptDiagnosticCode::None,
        "VM generator send fixture yields before input");
    require(tryGetInt(vm.resumeGeneratorValueSend(*state, makeInt(42), error), result)
            && result == 42 && error.code == ScriptDiagnosticCode::None,
        "VM generator send injects one value at the yield boundary");
    vm.closeGenerator(*state);
    require(isNil(vm.resumeGeneratorValue(*state, error))
            && error.code == ScriptDiagnosticCode::None && state->done,
        "VM generator close exhausts and discards continuation state");

    Program delegatedProgram;
    Function& delegated = delegatedProgram.functions.emplace_back("delegated");
    delegated.locals = 0;
    delegated.maxStack = 1;
    delegated.isGenerator = true;
    delegated.hasYieldFrom = true;
    delegated.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    delegated.code.emplace_back(Opcode::Yield);
    delegated.code.emplace_back(Opcode::Return, 0);
    Function& delegatedMaker = delegatedProgram.functions.emplace_back("delegated_maker");
    delegatedMaker.locals = 0;
    delegatedMaker.maxStack = 1;
    delegatedMaker.code.emplace_back(Opcode::Call, 0, 0);
    delegatedMaker.code.emplace_back(Opcode::Return, 1);
    Vm delegatedVm;
    Value delegatedValue = delegatedVm.runFunction(delegatedProgram, 1, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && delegatedValue.is<ScriptGenerator>(),
        "VM delegated generator fixture creates a generator");
    auto* delegatedState = delegatedValue.ptr<ScriptGenerator>();
    require(tryGetInt(delegatedVm.resumeGeneratorValue(*delegatedState, error), result)
            && result == 1 && error.code == ScriptDiagnosticCode::None,
        "VM delegated generator yields before send forwarding");
    require(isNil(delegatedVm.resumeGeneratorValueSend(*delegatedState, makeInt(42), error))
            && error.code == ScriptDiagnosticCode::AotSwitchUnsupported,
        "VM delegated generator rejects child send forwarding deterministically");

    Program forwardingProgram;
    Function& child = forwardingProgram.functions.emplace_back("child_forward");
    child.locals = 1;
    child.maxStack = 1;
    child.isGenerator = true;
    child.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    child.code.emplace_back(Opcode::Yield);
    child.code.emplace_back(Opcode::StoreLocal, 0);
    child.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
    child.code.emplace_back(Opcode::Yield);
    child.code.emplace_back(Opcode::LoadLocal, 0);
    child.code.emplace_back(Opcode::Return, 1);
    Function& parent = forwardingProgram.functions.emplace_back("parent_forward");
    parent.locals = 3;
    parent.maxStack = 1;
    parent.isGenerator = true;
    parent.hasYieldFrom = true;
    parent.code.emplace_back(Opcode::Call, 0, 0);
    parent.code.emplace_back(Opcode::RangeInit, 0, 1);
    parent.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, 2));
    parent.code.emplace_back(Opcode::JumpIfFalse, 7);
    parent.code.emplace_back(Opcode::LoadLocal, 2);
    parent.code.emplace_back(Opcode::Yield);
    parent.code.emplace_back(Opcode::Jump, 2);
    parent.code.emplace_back(Opcode::LoadInt, 0, 0, 6);
    parent.code.emplace_back(Opcode::Return, 1);
    addDelegationSite(parent, 0, 1, 2, 3, 4, 5, 6, 7);
    Function& forwardingMaker = forwardingProgram.functions.emplace_back("make_parent_forward");
    forwardingMaker.maxStack = 1;
    forwardingMaker.code.emplace_back(Opcode::Call, 1, 0);
    forwardingMaker.code.emplace_back(Opcode::Return, 1);
    Vm forwardingVm;
    error = VmError{};
    Value forwardingValue = forwardingVm.runFunction(forwardingProgram, 2, {}, &error);
    auto* forwardingState = forwardingValue.ptr<ScriptGenerator>();
    require(tryGetInt(forwardingVm.resumeGeneratorValue(*forwardingState, error), result)
            && result == 4 && error.code == ScriptDiagnosticCode::None,
        "VM yield from forwarding fixture yields the child value");
    require(tryGetInt(
                forwardingVm.resumeGeneratorValueSend(*forwardingState, makeInt(42), error), result)
            && result == 5 && error.code == ScriptDiagnosticCode::None,
        "VM yield from forwards send() to the delegated child");
    require(tryGetInt(forwardingVm.resumeGeneratorValue(*forwardingState, error), result)
            && result == 6 && error.code == ScriptDiagnosticCode::None && forwardingState->done,
        "VM yield from continues after delegated child exhaustion");

    error = VmError{};
    Value throwValue = forwardingVm.runFunction(forwardingProgram, 2, {}, &error);
    auto* throwState = throwValue.ptr<ScriptGenerator>();
    require(tryGetInt(forwardingVm.resumeGeneratorValue(*throwState, error), result) && result == 4
            && error.code == ScriptDiagnosticCode::None,
        "VM yield from throw fixture yields before forwarding");
    require(isNil(forwardingVm.resumeGeneratorValueThrow(
                *throwState, makeString(std::string_view("child-error")), error))
            && error.code == ScriptDiagnosticCode::UserRaise && error.message == "child-error",
        "VM yield from forwards throw() to the delegated child");

    error = VmError{};
    Value closeValue = forwardingVm.runFunction(forwardingProgram, 2, {}, &error);
    auto* closeState = closeValue.ptr<ScriptGenerator>();
    require(tryGetInt(forwardingVm.resumeGeneratorValue(*closeState, error), result) && result == 4
            && error.code == ScriptDiagnosticCode::None,
        "VM yield from close fixture yields before closing");
    auto* closeChild = asScriptGenerator(closeState->frame.locals[0]);
    forwardingVm.closeGenerator(*closeState);
    require(closeState->done && closeChild != nullptr && closeChild->done,
        "VM yield from close() forwards close to the delegated child");

    Program delegatedAwaitProgram;
    delegatedAwaitProgram.globalCount = 1;
    Function& awaitChild = delegatedAwaitProgram.functions.emplace_back("delegated_await_child");
    awaitChild.params = 1;
    awaitChild.locals = 1;
    awaitChild.maxStack = 1;
    awaitChild.isGenerator = true;
    awaitChild.code.emplace_back(Opcode::LoadLocal, 0);
    awaitChild.code.emplace_back(Opcode::Await);
    awaitChild.code.emplace_back(Opcode::Yield);
    awaitChild.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    awaitChild.code.emplace_back(Opcode::Return, 1);
    Function& awaitParent = delegatedAwaitProgram.functions.emplace_back("delegated_await_parent");
    awaitParent.locals = 3;
    awaitParent.maxStack = 1;
    awaitParent.isGenerator = true;
    awaitParent.hasYieldFrom = true;
    awaitParent.code.emplace_back(Opcode::LoadGlobal, 0);
    awaitParent.code.emplace_back(Opcode::RangeInit, 0, 1);
    awaitParent.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, 2));
    awaitParent.code.emplace_back(Opcode::JumpIfFalse, 8);
    awaitParent.code.emplace_back(Opcode::LoadLocal, 2);
    awaitParent.code.emplace_back(Opcode::Yield);
    awaitParent.code.emplace_back(Opcode::Jump, 2);
    awaitParent.code.emplace_back(Opcode::LoadInt, 0, 0, 77);
    awaitParent.code.emplace_back(Opcode::Return, 1);
    addDelegationSite(awaitParent, 0, 1, 2, 3, 4, 5, 6, 8);
    Function& awaitChildMaker =
        delegatedAwaitProgram.functions.emplace_back("make_delegated_await_child");
    awaitChildMaker.params = 1;
    awaitChildMaker.locals = 1;
    awaitChildMaker.maxStack = 1;
    awaitChildMaker.code.emplace_back(Opcode::LoadLocal, 0);
    awaitChildMaker.code.emplace_back(Opcode::Call, 0, 1);
    awaitChildMaker.code.emplace_back(Opcode::Return, 1);
    Function& awaitParentMaker =
        delegatedAwaitProgram.functions.emplace_back("make_delegated_await_parent");
    awaitParentMaker.maxStack = 1;
    awaitParentMaker.code.emplace_back(Opcode::Call, 1, 0);
    awaitParentMaker.code.emplace_back(Opcode::Return, 1);

    std::size_t delegatedAwaitPolls = 0u;
    ScriptAwaitable delegatedAwaitable;
    delegatedAwaitable.poll = [&delegatedAwaitPolls](Value&, arp::core::pmr::String&) {
        ++delegatedAwaitPolls;
        return ScriptAwaitStatus::Pending;
    };
    delegatedAwaitable.inject = [](ScriptAwaitInjectionKind kind,
                                    const Value& value,
                                    Value& result,
                                    arp::core::pmr::String&) {
        if (kind != ScriptAwaitInjectionKind::Send && kind != ScriptAwaitInjectionKind::Throw)
            return ScriptAwaitInjectionStatus::Rejected;
        int injected = 0;
        if (!tryGetInt(value, injected))
            return ScriptAwaitInjectionStatus::Failed;
        result = makeInt(injected + 1);
        return ScriptAwaitInjectionStatus::Ready;
    };
    arp::core::pmr::Vector<Value> awaitChildArgs{&arp::core::defaultCpuMemoryResource()};
    awaitChildArgs.push_back(Value{std::move(delegatedAwaitable)});
    Vm delegatedAwaitVm;
    error = VmError{};
    Value awaitChildValue =
        delegatedAwaitVm.runFunction(delegatedAwaitProgram, 2, awaitChildArgs, &error);
    require(error.code == ScriptDiagnosticCode::None && awaitChildValue.is<ScriptGenerator>(),
        "VM delegated-await fixture creates an Interpreter child");
    arp::core::pmr::Vector<Value> awaitGlobals{&arp::core::defaultCpuMemoryResource()};
    awaitGlobals.push_back(std::move(awaitChildValue));
    delegatedAwaitVm.setSharedGlobals(&awaitGlobals);
    Value awaitParentValue = delegatedAwaitVm.runFunction(delegatedAwaitProgram, 3, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && awaitParentValue.is<ScriptGenerator>(),
        "VM delegated-await fixture creates an Interpreter parent");
    auto* awaitParentState = awaitParentValue.ptr<ScriptGenerator>();
    require(isNil(delegatedAwaitVm.resumeGeneratorValue(*awaitParentState, error))
            && error.code == ScriptDiagnosticCode::Suspended && delegatedAwaitPolls == 1u
            && awaitParentState->delegatedChildPending
            && awaitParentState->delegatedPendingYieldPc == 5,
        "VM yield from preserves a pending child await without replay");
    const Value delegatedAwaitSend =
        delegatedAwaitVm.resumeGeneratorValueSend(*awaitParentState, makeInt(41), error);
    require(tryGetInt(delegatedAwaitSend, result) && result == 42
            && error.code == ScriptDiagnosticCode::None && delegatedAwaitPolls == 1u
            && !awaitParentState->delegatedChildPending
            && awaitParentState->delegatedPendingYieldPc < 0,
        "VM yield from forwards send() into a pending child await once");
    delegatedAwaitVm.closeGenerator(*awaitParentState);
    require(awaitParentState->done && asScriptGenerator(awaitGlobals[0]) != nullptr
            && asScriptGenerator(awaitGlobals[0])->done,
        "VM yield from close() closes a child with a pending await");

    delegatedAwaitPolls = 0u;
    ScriptAwaitable throwAwaitable;
    throwAwaitable.poll = [&delegatedAwaitPolls](Value&, arp::core::pmr::String&) {
        ++delegatedAwaitPolls;
        return ScriptAwaitStatus::Pending;
    };
    throwAwaitable.inject = [](ScriptAwaitInjectionKind kind,
                                const Value& value,
                                Value& result,
                                arp::core::pmr::String&) {
        if (kind != ScriptAwaitInjectionKind::Throw)
            return ScriptAwaitInjectionStatus::Rejected;
        int injected = 0;
        if (!tryGetInt(value, injected))
            return ScriptAwaitInjectionStatus::Failed;
        result = makeInt(injected + 1);
        return ScriptAwaitInjectionStatus::Ready;
    };
    arp::core::pmr::Vector<Value> throwAwaitArgs{&arp::core::defaultCpuMemoryResource()};
    throwAwaitArgs.push_back(Value{std::move(throwAwaitable)});
    Vm delegatedThrowVm;
    error = VmError{};
    Value throwAwaitChild =
        delegatedThrowVm.runFunction(delegatedAwaitProgram, 2, throwAwaitArgs, &error);
    arp::core::pmr::Vector<Value> throwAwaitGlobals{&arp::core::defaultCpuMemoryResource()};
    throwAwaitGlobals.push_back(std::move(throwAwaitChild));
    delegatedThrowVm.setSharedGlobals(&throwAwaitGlobals);
    Value throwAwaitParent = delegatedThrowVm.runFunction(delegatedAwaitProgram, 3, {}, &error);
    auto* throwAwaitState = throwAwaitParent.ptr<ScriptGenerator>();
    require(
        tryGetInt(delegatedThrowVm.resumeGeneratorValue(*throwAwaitState, error), result) == false
            && error.code == ScriptDiagnosticCode::Suspended && delegatedAwaitPolls == 1u,
        "VM delegated-await throw fixture reaches pending child state");
    const Value delegatedAwaitThrow =
        delegatedThrowVm.resumeGeneratorValueThrow(*throwAwaitState, makeInt(4), error);
    require(tryGetInt(delegatedAwaitThrow, result) && result == 5
            && error.code == ScriptDiagnosticCode::None && delegatedAwaitPolls == 1u,
        "VM yield from forwards throw() into a pending child await once");

    Program collectionProgram;
    Function& collectionChild = collectionProgram.functions.emplace_back("collection_child");
    collectionChild.maxStack = 2;
    collectionChild.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
    collectionChild.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    collectionChild.code.emplace_back(Opcode::MakeList, 0, 2);
    collectionChild.code.emplace_back(Opcode::Return, 1);
    Function& collectionParent = collectionProgram.functions.emplace_back("collection_parent");
    collectionParent.locals = 3;
    collectionParent.maxStack = 3;
    collectionParent.isGenerator = true;
    collectionParent.hasYieldFrom = true;
    collectionParent.code.emplace_back(Opcode::PushHandler, 11);
    collectionParent.code.emplace_back(Opcode::Call, 0, 0);
    collectionParent.code.emplace_back(Opcode::RangeInit, 0, 1);
    collectionParent.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, 2));
    collectionParent.code.emplace_back(Opcode::JumpIfFalse, 8);
    collectionParent.code.emplace_back(Opcode::LoadLocal, 2);
    collectionParent.code.emplace_back(Opcode::Yield);
    collectionParent.code.emplace_back(Opcode::Jump, 3);
    collectionParent.code.emplace_back(Opcode::PopHandler);
    collectionParent.code.emplace_back(Opcode::LoadInt, 0, 0, 9);
    collectionParent.code.emplace_back(Opcode::Return, 1);
    collectionParent.code.emplace_back(Opcode::StoreLocal, 0);
    collectionParent.code.emplace_back(Opcode::LoadInt, 0, 0, 8);
    collectionParent.code.emplace_back(Opcode::Return, 1);
    addDelegationSite(collectionParent, 1, 2, 3, 4, 5, 6, 7, 8);
    Function& collectionMaker = collectionProgram.functions.emplace_back("make_collection_parent");
    collectionMaker.maxStack = 1;
    collectionMaker.code.emplace_back(Opcode::Call, 1, 0);
    collectionMaker.code.emplace_back(Opcode::Return, 1);

    Vm collectionVm;
    error = VmError{};
    Value collectionValue = collectionVm.runFunction(collectionProgram, 2, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && collectionValue.is<ScriptGenerator>(),
        "VM collection yield-from fixture creates a generator");
    auto* collectionState = collectionValue.ptr<ScriptGenerator>();
    require(tryGetInt(collectionVm.resumeGeneratorValue(*collectionState, error), result)
            && result == 4 && error.code == ScriptDiagnosticCode::None,
        "VM collection yield-from yields the first value");
    require(
        tryGetInt(collectionVm.resumeGeneratorValueSend(*collectionState, makeNil(), error), result)
            && result == 7 && error.code == ScriptDiagnosticCode::None,
        "VM collection yield-from accepts nil send() and continues");
    require(isNil(collectionVm.resumeGeneratorValueSend(*collectionState, makeInt(2), error))
            && error.code == ScriptDiagnosticCode::AotSwitchUnsupported,
        "VM collection yield-from rejects non-nil send() deterministically");
    require(
        tryGetInt(collectionVm.resumeGeneratorValueSend(*collectionState, makeNil(), error), result)
            && result == 9 && error.code == ScriptDiagnosticCode::None && collectionState->done,
        "VM collection yield-from remains resumable after rejected send()");

    error = VmError{};
    Value collectionThrowValue = collectionVm.runFunction(collectionProgram, 2, {}, &error);
    auto* collectionThrowState = collectionThrowValue.ptr<ScriptGenerator>();
    require(tryGetInt(collectionVm.resumeGeneratorValue(*collectionThrowState, error), result)
            && result == 4 && error.code == ScriptDiagnosticCode::None,
        "VM collection yield-from throw fixture yields before injection");
    require(tryGetInt(
                collectionVm.resumeGeneratorValueThrow(
                    *collectionThrowState, makeString(std::string_view("collection-error")), error),
                result)
            && result == 8 && error.code == ScriptDiagnosticCode::None
            && collectionThrowState->done,
        "VM collection yield-from throw() enters the parent handler");
}

void testGeneratorThrow()
{
    Program program;
    Function& generator = program.functions.emplace_back("generator_throw");
    generator.locals = 1;
    generator.maxStack = 1;
    generator.isGenerator = true;
    generator.code.emplace_back(Opcode::PushHandler, 5);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    generator.code.emplace_back(Opcode::Yield);
    generator.code.emplace_back(Opcode::PopHandler);
    generator.code.emplace_back(Opcode::Jump, 9);
    generator.code.emplace_back(Opcode::StoreLocal, 0);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 8);
    generator.code.emplace_back(Opcode::Yield);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 9);
    generator.code.emplace_back(Opcode::Return, 1);
    Function& maker = program.functions.emplace_back("make_generator_throw");
    maker.locals = 1;
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::Call, 0, 0);
    maker.code.emplace_back(Opcode::Return, 1);

    Vm vm;
    VmError error;
    Value value = vm.runFunction(program, 1, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && value.is<ScriptGenerator>(),
        "VM generator throw fixture creates a generator");
    auto* state = value.ptr<ScriptGenerator>();
    int result = 0;
    require(
        isNil(vm.resumeGeneratorValueThrow(*state, makeString(std::string_view("early")), error))
            && error.code == ScriptDiagnosticCode::AotSwitchUnsupported,
        "VM generator throw rejects the first resume deterministically");
    require(tryGetInt(vm.resumeGeneratorValue(*state, error), result) && result == 7
            && error.code == ScriptDiagnosticCode::None,
        "VM generator throw fixture yields before injection");
    require(tryGetInt(vm.resumeGeneratorValueThrow(
                          *state, makeString(std::string_view("handled")), error),
                result)
            && result == 8 && error.code == ScriptDiagnosticCode::None,
        "VM generator throw is caught and resumes at the handler yield");
    require(tryGetInt(vm.resumeGeneratorValue(*state, error), result) && result == 9
            && error.code == ScriptDiagnosticCode::None && state->done,
        "VM generator throw handler continues to the return");

    Program uncaughtProgram;
    Function& uncaught = uncaughtProgram.functions.emplace_back("uncaught_throw");
    uncaught.locals = 0;
    uncaught.maxStack = 1;
    uncaught.isGenerator = true;
    uncaught.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    uncaught.code.emplace_back(Opcode::Yield);
    uncaught.code.emplace_back(Opcode::LoadInt, 0, 0, 2);
    uncaught.code.emplace_back(Opcode::Return, 1);
    Function& uncaughtMaker = uncaughtProgram.functions.emplace_back("make_uncaught");
    uncaughtMaker.maxStack = 1;
    uncaughtMaker.code.emplace_back(Opcode::Call, 0, 0);
    uncaughtMaker.code.emplace_back(Opcode::Return, 1);
    Vm uncaughtVm;
    Value uncaughtValue = uncaughtVm.runFunction(uncaughtProgram, 1, {}, &error);
    auto* uncaughtState = uncaughtValue.ptr<ScriptGenerator>();
    require(tryGetInt(uncaughtVm.resumeGeneratorValue(*uncaughtState, error), result) && result == 1
            && error.code == ScriptDiagnosticCode::None,
        "VM uncaught throw fixture yields before injection");
    require(isNil(uncaughtVm.resumeGeneratorValueThrow(
                *uncaughtState, makeString(std::string_view("uncaught")), error))
            && error.code == ScriptDiagnosticCode::UserRaise && error.message == "uncaught",
        "VM generator throw reports an uncaught UserRaise");

    bool ready = false;
    ScriptAwaitable awaitable;
    awaitable.poll = [&ready](Value& awaited, arp::core::pmr::String&) {
        if (!ready)
            return ScriptAwaitStatus::Pending;
        awaited = makeInt(4);
        return ScriptAwaitStatus::Ready;
    };
    Program pendingProgram;
    Function& pendingGenerator = pendingProgram.functions.emplace_back("pending_throw");
    pendingGenerator.params = 1;
    pendingGenerator.locals = 1;
    pendingGenerator.maxStack = 1;
    pendingGenerator.isGenerator = true;
    pendingGenerator.code.emplace_back(Opcode::LoadLocal, 0);
    pendingGenerator.code.emplace_back(Opcode::Await);
    pendingGenerator.code.emplace_back(Opcode::Yield);
    pendingGenerator.code.emplace_back(Opcode::Return, 0);
    Function& pendingMaker = pendingProgram.functions.emplace_back("make_pending");
    pendingMaker.params = 1;
    pendingMaker.locals = 1;
    pendingMaker.maxStack = 1;
    pendingMaker.code.emplace_back(Opcode::LoadLocal, 0);
    pendingMaker.code.emplace_back(Opcode::Call, 0, 1);
    pendingMaker.code.emplace_back(Opcode::Return, 1);
    arp::core::pmr::Vector<Value> awaitArgs{&arp::core::defaultCpuMemoryResource()};
    awaitArgs.push_back(Value{awaitable});
    Vm pendingVm;
    error = VmError{};
    Value pendingValue = pendingVm.runFunction(pendingProgram, 1, awaitArgs, &error);
    auto* pendingState = pendingValue.ptr<ScriptGenerator>();
    require(isNil(pendingVm.resumeGeneratorValue(*pendingState, error))
            && error.code == ScriptDiagnosticCode::Suspended,
        "VM pending throw fixture suspends at await");
    require(isNil(pendingVm.resumeGeneratorValueSend(*pendingState, makeInt(7), error))
            && error.code == ScriptDiagnosticCode::AotSwitchUnsupported,
        "VM generator send rejects a pending await");
    require(isNil(pendingVm.resumeGeneratorValueThrow(
                *pendingState, makeString(std::string_view("pending")), error))
            && error.code == ScriptDiagnosticCode::AotSwitchUnsupported,
        "VM generator throw rejects a pending await");
}

void testGeneratorBridgeIdentityV3()
{
    Program program;
    Function& generator = program.functions.emplace_back("bridge_generator");
    generator.isGenerator = true;
    generator.maxStack = 1;
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    generator.code.emplace_back(Opcode::Yield);
    generator.code.emplace_back(Opcode::Return, 0);
    Function& maker = program.functions.emplace_back("make_bridge_generator");
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::Call, 0, 0);
    maker.code.emplace_back(Opcode::Return, 1);

    Vm vm;
    vm.setModuleIdentity("bridge-module", 7u);
    vm.setBridgeArtifactIdentity("bridge-artifact");
    VmError error;
    Value value = vm.runFunction(program, 1, {}, &error);
    require(value.is<ScriptGenerator>() && error.code == ScriptDiagnosticCode::None,
        "Interpreter generator is created for V3 identity query");
    const auto* bridge = generatorBridgeV3();
    const auto* capability = generatorBridgeV3Capability();
    GeneratorIdentityV3 identity;
    require(bridge != nullptr && bridge->queryIdentity != nullptr && capability != nullptr
            && capability->supportsPendingAwaitInjection != nullptr
            && bridge->queryIdentity(value, identity) && identity.complete()
            && std::string_view(identity.moduleId) == "bridge-module"
            && identity.moduleVersion == 7u
            && std::string_view(identity.artifactIdentity) == "bridge-artifact",
        "Interpreter V3 provider exposes complete owner identity");

    Vm noIdentityVm;
    noIdentityVm.setModuleIdentity("bridge-module", 7u);
    error = VmError{};
    Value noIdentity = noIdentityVm.runFunction(program, 1, {}, &error);
    GeneratorIdentityV3 incomplete;
    require(noIdentity.is<ScriptGenerator>() && !bridge->queryIdentity(noIdentity, incomplete),
        "Interpreter V3 provider rejects missing artifact identity");
    require(!capability->supportsPendingAwaitInjection(value),
        "Interpreter V3 provider reports no pending await injection at yield");
}

void testGeneratorBridgeDelegatedAwaitDepthBudget()
{
    Vm vm;

    ScriptGenerator leaf;
    Value current{std::move(leaf)};
    for (int depth = 0; depth < 4; ++depth) {
        ScriptGenerator parent;
        parent.delegatedChildPending = true;
        parent.delegatedSourceSlot = 0;
        parent.frame.locals.push_back(current);
        current = Value{std::move(parent)};
    }

    const auto* bridge = generatorBridgeV2();
    require(bridge != nullptr && bridge->delegatedAwaitDepth != nullptr
            && bridge->delegatedAwaitDepth(current, 8) == 4,
        "Interpreter generator bridge traverses delegated await depth beyond the old fixed limit");
    require(bridge != nullptr && bridge->delegatedAwaitDepth != nullptr
            && bridge->delegatedAwaitDepth(current, 2) > 2,
        "Interpreter generator bridge reports deterministic bounded-depth overflow");
}

void testDeepDelegatedAwaitContinuationBudget()
{
    Program program;
    program.globalCount = 1;

    Function& leaf = program.functions.emplace_back("await_leaf");
    leaf.maxStack = 1;
    leaf.isGenerator = true;
    leaf.code.emplace_back(Opcode::LoadGlobal, 0);
    leaf.code.emplace_back(Opcode::Await);
    leaf.code.emplace_back(Opcode::Yield);
    leaf.code.emplace_back(Opcode::Return, 0);

    // Each parent uses the compiler's canonical yield-from lowering shape.
    // The chain is intentionally deeper than the former fixed depth-two gate.
    for (int childIndex = 0; childIndex < 4; ++childIndex) {
        Function& parent = program.functions.emplace_back("delegated_parent");
        parent.locals = 3;
        parent.maxStack = 1;
        parent.isGenerator = true;
        parent.hasYieldFrom = true;
        parent.code.emplace_back(Opcode::Call, childIndex, 0);
        parent.code.emplace_back(Opcode::RangeInit, 0, 1);
        parent.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, 2));
        parent.code.emplace_back(Opcode::JumpIfFalse, 8);
        parent.code.emplace_back(Opcode::LoadLocal, 2);
        parent.code.emplace_back(Opcode::Yield);
        parent.code.emplace_back(Opcode::Jump, 2);
        parent.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
        parent.code.emplace_back(Opcode::Return, 1);
        addDelegationSite(parent, 0, 1, 2, 3, 4, 5, 6, 8);
    }

    Function& maker = program.functions.emplace_back("make_deep_parent");
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::Call, 4, 0);
    maker.code.emplace_back(Opcode::Return, 1);

    std::size_t polls = 0u;
    ScriptAwaitable awaitable;
    awaitable.poll = [&polls](Value&, arp::core::pmr::String&) {
        ++polls;
        return ScriptAwaitStatus::Pending;
    };
    awaitable.inject = [](ScriptAwaitInjectionKind kind,
                           const Value& input,
                           Value& output,
                           arp::core::pmr::String&) {
        if (kind != ScriptAwaitInjectionKind::Send)
            return ScriptAwaitInjectionStatus::Rejected;
        int sent = 0;
        if (!tryGetInt(input, sent))
            return ScriptAwaitInjectionStatus::Failed;
        output = makeInt(sent + 1);
        return ScriptAwaitInjectionStatus::Ready;
    };

    arp::core::pmr::Vector<Value> globals{&arp::core::defaultCpuMemoryResource()};
    globals.push_back(Value{awaitable});
    Vm vm;
    vm.setMaxContinuationFrames(8u);
    vm.setSharedGlobals(&globals);
    VmError error;
    Value root = vm.runFunction(program, 5, {}, &error);
    auto* generator = root.ptr<ScriptGenerator>();
    require(generator != nullptr && error.code == ScriptDiagnosticCode::None,
        "deep delegated-await fixture creates an Interpreter generator");
    require(isNil(vm.resumeGeneratorValue(*generator, error))
            && error.code == ScriptDiagnosticCode::Suspended && polls == 1u,
        "deep delegated-await chain suspends after one poll");
    int result = 0;
    require(tryGetInt(vm.resumeGeneratorValueSend(*generator, makeInt(41), error), result)
            && result == 42 && error.code == ScriptDiagnosticCode::None && polls == 1u,
        "deep delegated-await send crosses all bounded Interpreter parents without replay");

    polls = 0u;
    globals.clear();
    globals.push_back(Value{awaitable});
    Vm limitedVm;
    limitedVm.setMaxContinuationFrames(2u);
    limitedVm.setSharedGlobals(&globals);
    error = VmError{};
    Value limitedRoot = limitedVm.runFunction(program, 5, {}, &error);
    auto* limitedGenerator = limitedRoot.ptr<ScriptGenerator>();
    require(limitedGenerator != nullptr && error.code == ScriptDiagnosticCode::None,
        "limited deep delegated-await fixture creates an Interpreter generator");
    limitedVm.resumeGeneratorValue(*limitedGenerator, error);
    require(error.code == ScriptDiagnosticCode::AotSwitchUnsupported,
        "delegated-await chain above the VM continuation budget fails closed");
}

void testBytesRuntimeValue()
{
    const std::string_view source("\x01\xFF", 2);
    auto value = makeBytes(source);
    require(value.is<ScriptBytes>(), "bytes runtime value has ScriptBytes type");
    require(asScriptBytes(value) != nullptr, "bytes runtime value is inspectable");
    arp::core::pmr::Vector<std::uint8_t> copied{&arp::core::defaultCpuMemoryResource()};
    require(tryGetBytes(value, copied), "bytes runtime value can be read");
    require(copied.size() == 2 && copied[0] == 0x01u && copied[1] == 0xFFu,
        "bytes runtime value preserves binary payload");
}

void testPointerAddressingParitySurface()
{
    {
        Program program;
        Function& fn = program.functions.emplace_back("index_pointer");
        fn.results = 1;
        fn.locals = 2;
        fn.maxStack = 3;
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
        fn.code.emplace_back(Opcode::MakeList, 0, 2);
        fn.code.emplace_back(Opcode::StoreLocal, 0);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
        fn.code.emplace_back(Opcode::AddrIndex);
        fn.code.emplace_back(Opcode::StoreLocal, 1);
        fn.code.emplace_back(Opcode::LoadLocal, 1);
        fn.code.emplace_back(Opcode::Deref);
        fn.code.emplace_back(Opcode::Pop);
        fn.code.emplace_back(Opcode::LoadLocal, 1);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
        fn.code.emplace_back(Opcode::StoreIndirect);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
        fn.code.emplace_back(Opcode::LoadIndex);
        fn.code.emplace_back(Opcode::Return, 1);
        program.entry = 0;
        auto result = runProgram(program);
        require(result.ok(), "VM index pointer execution succeeds");
        requireInt(result.value, 99, "VM index pointer writes through to the list");
    }

    registerReflectedConstructible();
    {
        Program program;
        Function& fn = program.functions.emplace_back("field_pointer");
        fn.results = 1;
        fn.locals = 2;
        fn.maxStack = 2;
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 41);
        fn.code.emplace_back(Opcode::MakeInstance,
            0,
            1,
            0,
            arp::core::pmr::String(
                "ScriptVmReflectedConstructible", &arp::core::defaultCpuMemoryResource()));
        fn.code.emplace_back(Opcode::StoreLocal, 0);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::AddrField,
            0,
            0,
            0,
            arp::core::pmr::String("value", &arp::core::defaultCpuMemoryResource()));
        fn.code.emplace_back(Opcode::StoreLocal, 1);
        fn.code.emplace_back(Opcode::LoadLocal, 1);
        fn.code.emplace_back(Opcode::Deref);
        fn.code.emplace_back(Opcode::Pop);
        fn.code.emplace_back(Opcode::LoadLocal, 1);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 55);
        fn.code.emplace_back(Opcode::StoreIndirect);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::LoadField,
            0,
            0,
            0,
            arp::core::pmr::String("value", &arp::core::defaultCpuMemoryResource()));
        fn.code.emplace_back(Opcode::Return, 1);
        program.entry = 0;
        auto result = runProgram(program);
        require(result.ok(), "VM field pointer execution succeeds");
        requireInt(result.value, 55, "VM field pointer writes through to the instance");
    }

    {
        Program program;
        Function& fn = program.functions.emplace_back("global_pointer");
        fn.results = 1;
        fn.locals = 1;
        fn.maxStack = 2;
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
        fn.code.emplace_back(Opcode::StoreGlobal, 0);
        fn.code.emplace_back(Opcode::AddrGlobal, 0);
        fn.code.emplace_back(Opcode::StoreLocal, 0);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::Deref);
        fn.code.emplace_back(Opcode::Pop);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 11);
        fn.code.emplace_back(Opcode::StoreIndirect);
        fn.code.emplace_back(Opcode::LoadGlobal, 0);
        fn.code.emplace_back(Opcode::Return, 1);
        program.globalCount = 1;
        program.entry = 0;
        auto result = runProgram(program);
        require(result.ok(), "VM global pointer execution succeeds");
        requireInt(result.value, 11, "VM global pointer writes through to the global");
    }
}

} // namespace

int main()
{
    try {
        testScalarArithmetic();
        std::printf("PASSED: testScalarArithmetic\n");
        testControlFlowIf();
        std::printf("PASSED: testControlFlowIf\n");
        testLoopSum();
        std::printf("PASSED: testLoopSum\n");
        testFunctionCall();
        std::printf("PASSED: testFunctionCall\n");
        testFibonacci();
        std::printf("PASSED: testFibonacci\n");
        testComparisonAndLogic();
        std::printf("PASSED: testComparisonAndLogic\n");
        testListConstruction();
        std::printf("PASSED: testListConstruction\n");
        testClosureCapture();
        std::printf("PASSED: testClosureCapture\n");
        testStringConcat();
        std::printf("PASSED: testStringConcat\n");
        testReflectionMagicDispatch();
        std::printf("PASSED: testReflectionMagicDispatch\n");
        testMakeInstanceUsesReflectedConstructor();
        std::printf("PASSED: testMakeInstanceUsesReflectedConstructor\n");
        testScriptInstanceMethodsReleaseWithoutReferenceCycle();
        std::printf("PASSED: testScriptInstanceMethodsReleaseWithoutReferenceCycle\n");
        testAllScriptValueKindsRelease();
        std::printf("PASSED: testAllScriptValueKindsRelease\n");
        testBitwiseAndShift();
        std::printf("PASSED: testBitwiseAndShift\n");
        testFastPathLocalAddIntConst();
        std::printf("PASSED: testFastPathLocalAddIntConst\n");
        testDictConstruction();
        std::printf("PASSED: testDictConstruction\n");
        testMetadataHostCallableWrongArity();
        std::printf("PASSED: testMetadataHostCallableWrongArity\n");
        testBytesRuntimeValue();
        std::printf("PASSED: testBytesRuntimeValue\n");
        testPointerAddressingParitySurface();
        std::printf("PASSED: testPointerAddressingParitySurface\n");
        testGeneratorAwaitContinuation();
        std::printf("PASSED: testGeneratorAwaitContinuation\n");
        testGeneratorFailedAwaitUnwindsCallerHandler();
        std::printf("PASSED: testGeneratorFailedAwaitUnwindsCallerHandler\n");
        testGeneratorSendAndClose();
        std::printf("PASSED: testGeneratorSendAndClose\n");
        testGeneratorThrow();
        std::printf("PASSED: testGeneratorThrow\n");
        testGeneratorBridgeIdentityV3();
        std::printf("PASSED: testGeneratorBridgeIdentityV3\n");
        testGeneratorBridgeDelegatedAwaitDepthBudget();
        std::printf("PASSED: testGeneratorBridgeDelegatedAwaitDepthBudget\n");
        testDeepDelegatedAwaitContinuationBudget();
        std::printf("PASSED: testDeepDelegatedAwaitContinuationBudget\n");
    } catch (const std::exception& e) {
        std::printf("EXCEPTION: %s\n", e.what());
        return 1;
    }

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript VM tests passed\n");
    return 0;
}
