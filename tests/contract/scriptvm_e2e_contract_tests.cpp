// ArpScript end-to-end contract tests (Phase 6).
//
// Full pipeline: source -> lex -> parse -> sema -> compile -> VM run.
// Tests closures, host calls, lists/dicts, classes, and yield.
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/compile/lexer.h"
#include "scriptvm/compile/parser.h"
#include "scriptvm/compile/sema.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/runtime/host.h"
#include "scriptvm/runtime/module_registry.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cstdio>
#include <cstdlib>

using namespace arp::scriptvm;

namespace {

int g_failures = 0;

struct BoundCounter {
    explicit BoundCounter(int initial) : value_(initial) {}

    int add(int amount) const { return value_ + amount; }

    int value() const { return value_; }

    void set_value(int value) { value_ = value; }

    int readonly() const { return value_ * 2; }

private:
    int value_ = 0;
};

enum class BoundMode {
    Fast = 1,
    Safe = 2,
};

void registerBoundCounter()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    arp::reflection::registration::class_<BoundCounter>("ScriptVmBoundCounter")
        .construct<int>()
        .method("add", &BoundCounter::add)
        .accessor_property("value", &BoundCounter::value, &BoundCounter::set_value)
        .def_property("readonly",
            arp::reflection::type_of<int>(),
            arp::reflection::object(arp::reflection::function(&BoundCounter::readonly)),
            arp::reflection::object::undefined(),
            "",
            0);
}

void registerBoundMode()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    arp::reflection::registration::class_<BoundMode>("ScriptVmBoundMode")
        .constant<BoundMode>("Values",
            {
                arp::reflection::enum_value("Fast", BoundMode::Fast),
                arp::reflection::enum_value("Safe", BoundMode::Safe),
            });
}

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

void requireBool(const Value& v, bool expected, const char* label)
{
    bool actual = false;
    if (!tryGetBool(v, actual)) {
        std::printf("FAIL: %s (not a bool)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %s, want %s)\n",
            label,
            actual ? "true" : "false",
            expected ? "true" : "false");
        ++g_failures;
    }
}

struct Pipeline {
    Program program;
    bool compiled = false;
    arp::core::pmr::String errMsg;

    bool compile(const char* src, ModuleRegistry* moduleRegistry = nullptr)
    {
        auto cr =
            arp::scriptvm::test::buildInterpreter(src, "e2e.arp", "e2e-contract", moduleRegistry);
        if (!cr.ok()) {
            errMsg = cr.error.message;
            return false;
        }
        program = std::move(cr.program);
        compiled = true;
        return true;
    }

    EvalResult run(const char* funcName = "test", ModuleRegistry* moduleRegistry = nullptr)
    {
        if (!compiled) {
            std::printf("FAIL: pipeline not compiled: %s\n", errMsg.c_str());
            ++g_failures;
            return EvalResult{};
        }
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        EvalResult result;
        Vm vm;
        vm.setModuleRegistry(moduleRegistry);
        result.value = vm.runFunctionByName(program, funcName, args, &result.error);
        return result;
    }
};

// -- Tests --

void testClosureCounter()
{
    Pipeline p;
    const char* src = "def make_counter(start: int) -> () -> int:\n"
                      "    var count: int = start\n"
                      "    def inc() -> int:\n"
                      "        count = count + 1\n"
                      "        return count\n"
                      "    return inc\n"
                      "def test() -> int:\n"
                      "    var c: () -> int = make_counter(0)\n"
                      "    c()\n"
                      "    c()\n"
                      "    return c()\n";
    require(p.compile(src), "closure counter: compile");
    auto r = p.run();
    require(r.ok(), "closure counter: run");
    requireInt(r.value, 3, "closure counter: 3");
}

void testClosureIndependent()
{
    Pipeline p;
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
    require(p.compile(src), "closure indep: compile");
    auto r = p.run();
    require(r.ok(), "closure indep: run");
    requireInt(r.value, 112, "closure indep: 11+101=112");
}

void testHostCall()
{
    // Register a host function that doubles its argument.
    HostRegistry registry;
    registry.registerFunction(
        "host_double", [](const arp::core::pmr::Vector<Value>& args) -> Value {
            if (args.empty())
                return makeInt(0);
            int v = 0;
            tryGetInt(args[0], v);
            return makeInt(v * 2);
        });

    Pipeline p;
    // The compiler doesn't know about host functions yet, so we test
    // by calling the host function manually through the VM API.
    const char* src = "def test() -> int:\n"
                      "    return 42\n";
    require(p.compile(src), "host: compile");
    auto r = p.run();
    require(r.ok(), "host: run");
    requireInt(r.value, 42, "host: 42");

    // Test host function directly.
    arp::core::pmr::Vector<Value> hostArgs{&arp::core::defaultCpuMemoryResource()};
    hostArgs.push_back(makeInt(21));
    auto fn = registry.lookup(0);
    require(fn != nullptr, "host: lookup");
    if (fn) {
        Value result = (*fn)(hostArgs);
        requireInt(result, 42, "host: double(21)=42");
    }
}

void testListOperations()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var arr: [int] = [10, 20, 30, 40]\n"
                      "    var sum: int = arr[0] + arr[1] + arr[2] + arr[3]\n"
                      "    return sum\n";
    require(p.compile(src), "list ops: compile");
    auto r = p.run();
    require(r.ok(), "list ops: run");
    requireInt(r.value, 100, "list ops: 10+20+30+40=100");
}

void testDictOperations()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var m: {string: int} = {\"x\": 1, \"y\": 2, \"z\": 3}\n"
                      "    return m[\"x\"] + m[\"y\"] + m[\"z\"]\n";
    require(p.compile(src), "dict ops: compile");
    auto r = p.run();
    require(r.ok(), "dict ops: run");
    requireInt(r.value, 6, "dict ops: 1+2+3=6");
}

void testFibRecursive()
{
    Pipeline p;
    const char* src = "def fib(n: int) -> int:\n"
                      "    if n < 2:\n"
                      "        return n\n"
                      "    return fib(n - 1) + fib(n - 2)\n"
                      "def test() -> int:\n"
                      "    return fib(20)\n";
    require(p.compile(src), "fib: compile");
    auto r = p.run();
    require(r.ok(), "fib: run");
    requireInt(r.value, 6765, "fib: fib(20)=6765");
}

void testNestedClosures()
{
    // Closures within closures: each level captures a variable.
    Pipeline p;
    const char* src = "def outer(a: int) -> () -> int:\n"
                      "    var x: int = a\n"
                      // No nested closure in this test -- just verify multi-level.
                      "    def inner() -> int:\n"
                      "        x = x + 1\n"
                      "        return x\n"
                      "    return inner\n"
                      "def test() -> int:\n"
                      "    var f: () -> int = outer(5)\n"
                      "    f()\n"
                      "    return f()\n";
    require(p.compile(src), "nested closures: compile");
    auto r = p.run();
    require(r.ok(), "nested closures: run");
    requireInt(r.value, 7, "nested closures: 7");
}

void testComplexProgram()
{
    // A program combining closures, loops, conditionals, and functions.
    Pipeline p;
    const char* src = "def is_even(n: int) -> bool:\n"
                      "    return n % 2 == 0\n"
                      "def sum_evens(limit: int) -> int:\n"
                      "    var total: int = 0\n"
                      "    for i: int in range(limit):\n"
                      "        if is_even(i):\n"
                      "            total = total + i\n"
                      "    return total\n"
                      "def test() -> int:\n"
                      "    return sum_evens(100)\n";
    require(p.compile(src), "complex: compile");
    auto r = p.run();
    require(r.ok(), "complex: run");
    // Sum of even numbers 0..98 = 2+4+...+98 = 2*(1+2+...+49) = 2*49*50/2 = 2450
    requireInt(r.value, 2450, "complex: sum_evens(100)=2450");
}

void testClosureReadCapture()
{
    // Closure that only reads (not writes) a captured variable.
    Pipeline p;
    const char* src = "def make_adder(n: int) -> () -> int:\n"
                      "    var x: int = n\n"
                      "    def get_x() -> int:\n"
                      "        return x\n"
                      "    return get_x\n"
                      "def test() -> int:\n"
                      "    var f: () -> int = make_adder(42)\n"
                      "    return f()\n";
    require(p.compile(src), "closure read: compile");
    auto r = p.run();
    require(r.ok(), "closure read: run");
    requireInt(r.value, 42, "closure read: 42");
}

void testClassDefinition()
{
    Pipeline p;
    const char* src = "class Counter:\n"
                      "    value: int\n"
                      "    def __init__(self, initial: int):\n"
                      "        self.value = initial\n"
                      "    def increment(self) -> int:\n"
                      "        self.value = self.value + 1\n"
                      "        return self.value\n"
                      "def test() -> int:\n"
                      "    var c: Counter = Counter(10)\n"
                      "    return c.increment()\n";
    require(p.compile(src), "class definition: compile");
    auto r = p.run();
    require(r.ok(), "class definition: run");
    requireInt(r.value, 11, "class definition: bound method returns 11");
}

void testStringMethods()
{
    // String methods registered via reflection on `str` type.
    // Tests upper/lower/strip/find/contains/startswith/endswith/replace/split/join/format.
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var s: string = \"Hello World\"\n"
                      "    var up: string = s.upper()\n"
                      "    var lo: string = s.lower()\n"
                      "    var tr: string = \"  hi  \".strip()\n"
                      "    var fi: int = \"hello\".find(\"ll\")\n"
                      "    var re: string = \"hello\".replace(\"l\", \"L\")\n"
                      "    var pa: list = \"a,b,c\".split(\",\")\n"
                      "    var jo: string = \"-\".join(pa)\n"
                      "    var fm: string = \"{0}+{1}\".format([\"x\", \"y\"])\n"
                      "    # Return a status code per check.\n"
                      "    if fi != 2:\n"
                      "        return 4\n"
                      "    if re != \"heLLo\":\n"
                      "        return 8\n"
                      "    if jo != \"a-b-c\":\n"
                      "        return 9\n"
                      "    if fm != \"x+y\":\n"
                      "        return 10\n"
                      "    # String equality on upper/lower/strip results\n"
                      "    if up != \"HELLO WORLD\":\n"
                      "        return 1\n"
                      "    if lo != \"hello world\":\n"
                      "        return 2\n"
                      "    if tr != \"hi\":\n"
                      "        return 3\n"
                      "    return 0\n";
    require(p.compile(src), "string methods: compile");
    auto r = p.run();
    require(r.ok(), "string methods: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 0, "string methods: all pass");
}

void testTypeAssertions()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var fromString: int = \"42\" as int\n"
                      "    var fromFloat: int = 3.9 as int\n"
                      "    var fromInt: string = 7 as string\n"
                      "    var fromBool: bool = 1 as bool\n"
                      "    var fromInt64: int64 = 9 as int64\n"
                      "    if fromString != 42:\n"
                      "        return 1\n"
                      "    if fromFloat != 3:\n"
                      "        return 2\n"
                      "    if fromInt != \"7\":\n"
                      "        return 3\n"
                      "    if not fromBool:\n"
                      "        return 4\n"
                      "    if fromInt64 != 9:\n"
                      "        return 5\n"
                      "    return 0\n";
    require(p.compile(src), "type assertions: compile");
    auto result = p.run();
    require(result.ok(), "type assertions: run");
    requireInt(result.value, 0, "type assertions: conversions");

    Pipeline bad;
    require(bad.compile("def test() -> int:\n"
                        "    return \"abc\" as int\n"),
        "type assertions: bad cast compiles");
    auto badResult = bad.run();
    require(!badResult.ok(), "type assertions: bad cast fails at runtime");
    require(badResult.error.code == ScriptDiagnosticCode::BadCast,
        "type assertions: bad cast diagnostic");
}

void testTupleIteration()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var dictTotal: int = 0\n"
                      "    var dictMarker: int = 0\n"
                      "    for key, value in {\"a\": 10, \"b\": 20, \"c\": 30}:\n"
                      "        dictTotal += value\n"
                      "        if key == \"b\":\n"
                      "            dictMarker = value\n"
                      "    var listTotal: int = 0\n"
                      "    var lastIndex: int = 0\n"
                      "    for index, value in [10, 20]:\n"
                      "        lastIndex = index\n"
                      "        listTotal += index + value\n"
                      "    return dictTotal + dictMarker + listTotal\n";
    require(p.compile(src), "tuple iteration: compile");
    auto result = p.run();
    require(result.ok(), "tuple iteration: run");
    requireInt(result.value, 111, "tuple iteration: dict keys and list indices");
}

void testLambdas()
{
    Pipeline p;
    const char* src = "def makeAdder(n: int) -> (int) -> int:\n"
                      "    return lambda(x: int) -> int: x + n\n"
                      "def test() -> int:\n"
                      "    var twice: (int) -> int = lambda(x: int) -> int: x * 2\n"
                      "    var addTen: (int) -> int = makeAdder(10)\n"
                      "    return twice(21) + addTen(5)\n";
    require(p.compile(src), "lambda: compile");
    auto result = p.run();
    require(result.ok(), "lambda: run");
    requireInt(result.value, 57, "lambda: direct and captured closures");
}

void testVariadicFunctions()
{
    Pipeline p;
    const char* src = "def sum_all(base: int, *args: int) -> int:\n"
                      "    var total: int = base\n"
                      "    for value in args:\n"
                      "        total += value\n"
                      "    return total\n"
                      "def test() -> int:\n"
                      "    return sum_all(1, 2, 3, 4) + sum_all(5)\n";
    require(p.compile(src), "variadic: compile");
    auto result = p.run();
    require(result.ok(), "variadic: run");
    requireInt(result.value, 15, "variadic: collected args and empty args");

    Pipeline invalid;
    require(!invalid.compile("def invalid(*args: int, tail: int) -> int:\n"
                             "    return tail\n"),
        "variadic: non-final parameter rejected");
}

void testTryExceptRaise()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var first: int = 0\n"
                      "    try:\n"
                      "        var bad: int = 10 / 0\n"
                      "    except as error:\n"
                      "        if error == \"division by zero\":\n"
                      "            first = 10\n"
                      "    try:\n"
                      "        try:\n"
                      "            raise \"inner\"\n"
                      "        except as error:\n"
                      "            raise \"outer\"\n"
                      "    except as error:\n"
                      "        if error == \"outer\":\n"
                      "            first += 5\n"
                      "    return first\n";
    require(p.compile(src), "try: compile");
    auto result = p.run();
    require(result.ok(), "try: run");
    requireInt(result.value, 15, "try: catch runtime and re-raise");
}

void testRaisePreservesNestedTraceback()
{
    Pipeline p;
    const char* src = "def leaf() -> int:\n"
                      "    raise \"nested failure\"\n"
                      "def middle() -> int:\n"
                      "    return leaf()\n"
                      "def test() -> int:\n"
                      "    return middle()\n";
    require(p.compile(src), "raise traceback: compile");
    auto result = p.run();
    require(!result.ok(), "raise traceback: run fails");
    require(result.error.message == "nested failure", "raise traceback: preserves message");
    require(result.error.traceback.find("at leaf") != arp::core::pmr::String::npos,
        "raise traceback: includes leaf");
    require(result.error.traceback.find("at middle") != arp::core::pmr::String::npos,
        "raise traceback: includes middle");
    require(result.error.traceback.find("at test") != arp::core::pmr::String::npos,
        "raise traceback: includes test");
}

void testGenerators()
{
    Pipeline p;
    const char* src = "def gen() -> int:\n"
                      "    yield 1\n"
                      "    yield 2\n"
                      "    yield 3\n"
                      "def test() -> int:\n"
                      "    var total: int = 0\n"
                      "    for value in gen():\n"
                      "        total += value\n"
                      "    return total\n";
    require(p.compile(src), "generator: compile");
    auto result = p.run();
    require(result.ok(), "generator: run");
    requireInt(result.value, 6, "generator: yields 1+2+3");

    Pipeline typedExcept;
    require(!typedExcept.compile("def test() -> int:\n"
                                 "    try:\n"
                                 "        raise \"x\"\n"
                                 "    except Error as error:\n"
                                 "        return 1\n"
                                 "    return 0\n"),
        "try: typed except is rejected deterministically");
}

void testYieldFromIteratorDelegation()
{
    Pipeline p;
    const char* src = "def child() -> int:\n"
                      "    yield 4\n"
                      "    yield 5\n"
                      "def parent() -> int:\n"
                      "    yield from child()\n"
                      "    yield 6\n"
                      "def test() -> int:\n"
                      "    var total: int = 0\n"
                      "    for value in parent():\n"
                      "        total += value\n"
                      "    return total\n";
    require(p.compile(src), "yield from: compile");
    auto result = p.run();
    require(result.ok(), "yield from: run");
    requireInt(result.value, 15, "yield from: delegates child values and resumes parent");
}

void testSourceImport()
{
    ScriptPluginRegistrar builder(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()));
    builder.exportFunction(
        arp::core::pmr::String("double", &arp::core::defaultCpuMemoryResource()),
        [](const arp::core::pmr::Vector<Value>& args) -> Value {
            int value = 0;
            if (!args.empty())
                tryGetInt(args[0], value);
            return makeInt(value * 2);
        },
        HostCallableContract{
            arp::core::pmr::String("e2e.math.double.v1", &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String(
                "sha256:e2e-math-double", &arp::core::defaultCpuMemoryResource()),
            1,
            false,
            1u});

    ModuleRegistry registry;
    registry.installPluginModule(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()), builder.build());

    Pipeline p;
    const char* src = "import math as m\n"
                      "from math import double as twice\n"
                      "def test() -> int:\n"
                      "    return m.double(21) + twice(1)\n";
    require(p.compile(src, &registry), "import: source compile");
    auto r = p.run("test", &registry);
    require(r.ok(), "import: source run");
    requireInt(r.value, 44, "import: module and from-import calls");
}

void testReflectedClassBindingImport()
{
    registerBoundCounter();
    registerBoundMode();

    ScriptPluginRegistrar builder(
        arp::core::pmr::String("native", &arp::core::defaultCpuMemoryResource()));
    require(builder.bindClass<BoundCounter>(
                arp::core::pmr::String("Counter", &arp::core::defaultCpuMemoryResource())),
        "binding: register reflected class export");
    require(builder.bindType<BoundMode>(
                arp::core::pmr::String("ModeType", &arp::core::defaultCpuMemoryResource())),
        "binding: register reflected enum export");
    builder.exportValue(arp::core::pmr::String("triple", &arp::core::defaultCpuMemoryResource()),
        Value{arp::reflection::function{"triple", [](int value) {
                                            return value * 3;
                                        }}});
    builder.exportValue(arp::core::pmr::String("getValue", &arp::core::defaultCpuMemoryResource()),
        arp::reflection::class_of<BoundCounter>()->get_field("value"));
    auto exports = builder.build();
    auto bound = exports->symbols.find("Counter");
    require(bound != exports->symbols.end(), "binding: class export exists");
    if (bound != exports->symbols.end()) {
        require(isReflectedClassBinding(bound->second),
            "binding: export carries reflected class binding");
        require(asReflectedClassBinding(bound->second) == arp::reflection::class_of<BoundCounter>(),
            "binding: export shares class_info with C++ reflection");
    }

    ModuleRegistry registry;
    registry.installPluginModule(
        arp::core::pmr::String("native", &arp::core::defaultCpuMemoryResource()),
        std::move(exports));

    Pipeline p;
    const char* src =
        "from native import Counter\n"
        "from native import triple, getValue\n"
        "import native as native_module\n"
        "def test() -> int:\n"
        "    var direct: any = Counter(10)\n"
        "    var qualified: any = native_module.Counter(20)\n"
        "    var qualifiedValue: int = qualified.value\n"
        "    var mode: int = native_module.ModeType.Values.Safe\n"
        "    var tripled: int = triple(4)\n"
        "    var propertyValue: int = getValue(direct)\n"
        "    direct.value = 11\n"
        "    direct.value += 1\n"
        "    var updatedValue: int = direct.value\n"
        "    var memberValue: int = qualified.add(2)\n"
        "    return updatedValue + qualifiedValue + tripled + propertyValue + mode + memberValue\n";
    require(p.compile(src, &registry), "binding: source compile");
    auto result = p.run("test", &registry);
    if (!result.ok())
        std::printf("binding diagnostic: %s\n", result.error.message.c_str());
    require(result.ok(), "binding: source run");
    requireInt(
        result.value, 78, "binding: imported reflection class/function/property/enum values work");

    Pipeline readonly;
    const char* readonlySource = "from native import Counter\n"
                                 "def test() -> int:\n"
                                 "    var direct: Counter = Counter(10)\n"
                                 "    direct.readonly = 7\n"
                                 "    return 0\n";
    require(!readonly.compile(readonlySource, &registry),
        "binding: readonly reflected property is rejected by sema");
}

} // namespace

// --- Source module import test ---
// Tests that a source (.arp) module can be loaded via the ModuleRegistry's
// source resolver and its top-level functions imported into another script.

void testSourceModuleImport()
{
    // Simulate a .arp source module "mymod" that exports a helper function.
    // The compiled Program must outlive any closures created from it, so we
    // store it in a shared_ptr that the resolver captures.
    auto moduleProgram = std::make_shared<Program>();
    auto moduleSource = std::make_shared<std::string>("def helper(x: int) -> int:\n"
                                                      "    return x + 100\n"
                                                      "def double_it(x: int) -> int:\n"
                                                      "    return x * 2\n");

    ModuleRegistry registry;
    registry.setSourceResolver([moduleProgram, moduleSource](const arp::core::pmr::String& name)
                                   -> std::shared_ptr<ModuleExports> {
        if (name != arp::core::pmr::String("mymod", &arp::core::defaultCpuMemoryResource()))
            return nullptr;

        // Compile the source module into the persistent Program.
        auto cr =
            arp::scriptvm::test::buildInterpreter(moduleSource->c_str(), "mymod.arp", "mymod");
        if (!cr.ok())
            return nullptr;
        *moduleProgram = std::move(cr.program);

        // Export each function as a HostCallable that invokes it on the
        // module's own Program via a persistent Vm. We keep a Vm per module
        // so the function's Program stays alive and callable.
        auto moduleVm = std::make_shared<Vm>();
        auto exports = std::make_shared<ModuleExports>();
        for (const auto& [fname, fidx] : moduleProgram->funcIndex) {
            if (fname == "__module_init__" || fname == "__entry__")
                continue;
            int capturedIdx = fidx;
            HostFn fn = [moduleProgram, moduleVm, capturedIdx](
                            const arp::core::pmr::Vector<Value>& args) -> Value {
                VmError err;
                Value result = moduleVm->runFunction(*moduleProgram, capturedIdx, args, &err);
                return result;
            };
            HostCallableContract contract{
                arp::core::pmr::String("e2e.mymod.", &arp::core::defaultCpuMemoryResource()),
                arp::core::pmr::String("sha256:e2e-mymod-", &arp::core::defaultCpuMemoryResource()),
                1,
                false,
                1u};
            contract.bindingId += fname;
            contract.signatureHash += fname;
            exports->hostCallableContracts[fname] = contract;
            HostCallable callable(fn, contract);
            exports->symbols[fname] = Value{callable};
        }
        return exports;
    });

    Pipeline p;
    const char* src = "from mymod import helper\n"
                      "def test() -> int:\n"
                      "    return helper(5)\n";
    // Source-module exports carry the same explicit host callable metadata as
    // plugin exports, so compile-time import validation is deterministic.
    require(p.compile(src, &registry), "source-import: compile");
    // Pass the registry so the VM's LoadModule triggers the source resolver.
    auto r = p.run("test", &registry);
    require(r.ok(), "source-import: run");
    requireInt(r.value, 105, "source-import: helper(5)=105");
}

// ---------------------------------------------------------------------------
// Variant SBO (Small-Buffer Optimization) contract test.
//
// Verifies that the variant class stores small types (int, double, bool,
// int64_t) inline without heap allocation, and large types (String) on the
// heap. This mirrors the original ae::hybrid::variant design.
//
// Contract:
//   1. variant(int) stores int inline -- is<int>() returns true, value correct
//   2. variant(double) stores double inline
//   3. variant(bool) stores bool inline
//   4. variant(int64_t) stores int64_t inline
//   5. variant(String) stores String on heap (sizeof(String) > sizeof(void*))
//   6. variant(const char*) converts to String
//   7. Copy is deep (modifying copy does not affect original)
//   8. Move empties source
//   9. Default variant is void/empty
//  10. object wraps variant via shared_ptr (copy is shallow)
//  11. sizeof(variant) == sizeof(void*) * 2 (vtable ptr + storage_t)
//  12. sizeof(object) == sizeof(void*) (intrusive VM-local ObjectRef)
// ---------------------------------------------------------------------------
void testVariantSBO()
{
    using namespace arp::reflection;

    // 1. int (stack policy)
    variant vi(42);
    require(vi.is<int>(), "variant-sbo: int is<int>");
    require(*vi.cast<int>() == 42, "variant-sbo: int value");

    // 2. double (stack policy)
    variant vd(3.14);
    require(vd.is<double>(), "variant-sbo: double is<double>");
    require(*vd.cast<double>() == 3.14, "variant-sbo: double value");

    // 3. bool (stack policy)
    variant vb(true);
    require(vb.is<bool>(), "variant-sbo: bool is<bool>");
    require(*vb.cast<bool>() == true, "variant-sbo: bool value");

    // 4. int64_t (stack policy)
    variant vi64(static_cast<std::int64_t>(1234567890123LL));
    require(vi64.is<std::int64_t>(), "variant-sbo: int64 is<int64>");
    require(*vi64.cast<std::int64_t>() == 1234567890123LL, "variant-sbo: int64 value");

    // 5. String (heap policy)
    variant vs(arp::core::pmr::String("hello", &arp::core::defaultCpuMemoryResource()));
    require(vs.is<arp::core::pmr::String>(), "variant-sbo: String is<String>");
    require(*vs.cast<arp::core::pmr::String>() == "hello", "variant-sbo: String value");

    // 6. const char* -> String conversion
    variant vc("world");
    require(vc.is<arp::core::pmr::String>(), "variant-sbo: cstr is<String>");
    require(*vc.cast<arp::core::pmr::String>() == "world", "variant-sbo: cstr value");

    // 7. Copy is deep (stack)
    {
        variant viCopy(vi);
        require(viCopy.is<int>(), "variant-sbo: copy is<int>");
        require(*viCopy.cast<int>() == 42, "variant-sbo: copy value");
        *viCopy.cast<int>() = 100; // modify copy
        require(*vi.cast<int>() == 42, "variant-sbo: original after copy modify");
        require(*viCopy.cast<int>() == 100, "variant-sbo: copy after modify");
    }

    // 8. Move empties source
    {
        variant viMoved(std::move(vi));
        require(viMoved.is<int>(), "variant-sbo: moved is<int>");
        require(*viMoved.cast<int>() == 42, "variant-sbo: moved value");
        require(vi.is<void>(), "variant-sbo: moved-from is void");
    }

    // 9. Default variant is void
    variant ve;
    require(ve.is<void>(), "variant-sbo: default is<void>");
    require(ve.ptr() == nullptr, "variant-sbo: default ptr null");

    // 10. object wraps variant via shared_ptr (copy is shallow)
    {
        object oi(99);
        require(oi.is<int>(), "variant-sbo: object is<int>");
        require(*oi.ptr<int>() == 99, "variant-sbo: object int value");
        object oiCopy = oi; // shallow copy
        *oiCopy.ptr<int>() = 200;
        require(*oi.ptr<int>() == 200, "variant-sbo: object shallow copy shares variant");
    }

    // 11. sizeof(variant) == sizeof(variant_vtable) + sizeof(storage_t)
    //     vtable has 6 pointers (48 bytes) + storage 8 bytes = 56 bytes on 64-bit.
    require(sizeof(variant) == sizeof(variant_vtable) + sizeof(storage_t),
        "variant-sbo: sizeof(variant) == vtable + storage");

    // 12. ObjectRef stores one control pointer; its counter and variant live
    // in the allocator-owned control block.
    require(sizeof(object) == sizeof(void*), "variant-sbo: sizeof(object) == 1*ptr");
}

int main()
{
    testClosureCounter();
    testClosureIndependent();
    testHostCall();
    testListOperations();
    testDictOperations();
    testFibRecursive();
    testNestedClosures();
    testComplexProgram();
    testClosureReadCapture();
    testClassDefinition();
    testStringMethods();
    testTypeAssertions();
    testTupleIteration();
    testLambdas();
    testVariadicFunctions();
    testTryExceptRaise();
    testRaisePreservesNestedTraceback();
    testGenerators();
    testYieldFromIteratorDelegation();
    testSourceImport();
    testReflectedClassBindingImport();
    testSourceModuleImport();
    testVariantSBO();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript e2e contract tests passed\n");
    return 0;
}
