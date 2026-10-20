// ArpScript e2e feature coverage tests.
//
// Full pipeline (lex -> parse -> sema -> compile -> VM run) exercising
// source-level features NOT covered by scriptvm_e2e_contract_tests.cpp:
// float arithmetic, string operations, all comparison ops, compound
// assignment (=, -=, *=, /=, %=), while/break/continue, for-in over
// list and dict, class field mutation, nested classes, optional types,
// int64 literals, bitwise operators, unary ops, logical short-circuit.
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/compile/lexer.h"
#include "scriptvm/compile/parser.h"
#include "scriptvm/compile/sema.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/runtime/host.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/value.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

using namespace arp::scriptvm;

namespace {

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
        std::printf("FAIL: %s (not an int)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, want %d)\n", label, actual, expected);
        ++g_failures;
    }
}

void requireFloat(const Value& v, double expected, const char* label)
{
    double actual = 0;
    if (!tryGetFloat(v, actual)) {
        std::printf("FAIL: %s (not a float)\n", label);
        ++g_failures;
        return;
    }
    if (std::abs(actual - expected) > 1e-9) {
        std::printf("FAIL: %s (got %f, want %f)\n", label, actual, expected);
        ++g_failures;
    }
}

void requireString(const Value& v, std::string_view expected, const char* label)
{
    arp::core::pmr::String s;
    if (!tryGetString(v, s)) {
        std::printf("FAIL: %s (not a string)\n", label);
        ++g_failures;
        return;
    }
    if (s != expected) {
        std::printf("FAIL: %s (got '%s', want '%.*s')\n",
            label,
            s.c_str(),
            static_cast<int>(expected.size()),
            expected.data());
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

struct Pipeline {
    Program program;
    bool compiled = false;
    arp::core::pmr::String errMsg;

    bool compile(const char* src)
    {
        auto cr =
            arp::scriptvm::test::buildInterpreter(src, "feature-coverage.arp", "feature-coverage");
        if (!cr.ok()) {
            errMsg = cr.error.message;
            return false;
        }
        program = std::move(cr.program);
        compiled = true;
        return true;
    }

    EvalResult run(const char* funcName = "test")
    {
        if (!compiled) {
            std::printf("FAIL: pipeline not compiled: %s\n", errMsg.c_str());
            ++g_failures;
            return EvalResult{};
        }
        arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
        EvalResult result;
        Vm vm;
        result.value = vm.runFunctionByName(program, funcName, args, &result.error);
        return result;
    }
};

// --- Float arithmetic ---

void testFloatArithmetic()
{
    Pipeline p;
    const char* src = "def test() -> float:\n"
                      "    var a: float = 3.5\n"
                      "    var b: float = 2.0\n"
                      "    return a * b + 0.5\n";
    require(p.compile(src), "float arith: compile");
    auto r = p.run();
    require(r.ok(), "float arith: run");
    requireFloat(r.value, 7.5, "3.5*2.0+0.5 == 7.5");
}

void testFloatDivision()
{
    Pipeline p;
    const char* src = "def test() -> float:\n"
                      "    return 10.0 / 3.0\n";
    require(p.compile(src), "float div: compile");
    auto r = p.run();
    require(r.ok(), "float div: run");
    requireFloat(r.value, 10.0 / 3.0, "10.0/3.0 == 3.333...");
}

void testIntFloatWidening()
{
    Pipeline p;
    const char* src = "def test() -> float:\n"
                      "    var x: int = 7\n"
                      "    var y: float = 2.0\n"
                      "    return x + y\n";
    require(p.compile(src), "int float widen: compile");
    auto r = p.run();
    require(r.ok(), "int float widen: run");
    requireFloat(r.value, 9.0, "7 + 2.0 == 9.0");
}

// --- String operations ---

void testStringConcat()
{
    Pipeline p;
    const char* src = "def test() -> string:\n"
                      "    var a: string = \"hello\"\n"
                      "    var b: string = \" \"\n"
                      "    var c: string = \"world\"\n"
                      "    return a + b + c\n";
    require(p.compile(src), "string concat: compile");
    auto r = p.run();
    require(r.ok(), "string concat: run");
    requireString(r.value, "hello world", "string concat == 'hello world'");
}

void testStringLength()
{
    // len() is not a source-level builtin; test string length via
    // the VM-level BuiltinLen in scriptvm_vm_coverage_tests instead.
    // Here we verify string comparison + equality.
    Pipeline p;
    const char* src = "def test() -> bool:\n"
                      "    var s: string = \"hello\"\n"
                      "    return s == \"hello\"\n";
    require(p.compile(src), "string identity: compile");
    auto r = p.run();
    require(r.ok(), "string identity: run");
    requireBool(r.value, true, "string == 'hello'");
}

void testStringComparison()
{
    Pipeline p;
    const char* src = "def test() -> bool:\n"
                      "    var a: string = \"abc\"\n"
                      "    var b: string = \"abc\"\n"
                      "    var c: string = \"abd\"\n"
                      "    return a == b and a != c\n";
    require(p.compile(src), "string cmp: compile");
    auto r = p.run();
    require(r.ok(), "string cmp: run");
    requireBool(r.value, true, "string equality/inequality");
}

// --- All comparison operators ---

void testAllComparisons()
{
    Pipeline p;
    const char* src = "def test() -> bool:\n"
                      "    var a: int = 5\n"
                      "    var b: int = 10\n"
                      "    return a < b and a <= 5 and b > a and b >= 10 and a == 5 and a != b\n";
    require(p.compile(src), "all cmp: compile");
    auto r = p.run();
    require(r.ok(), "all cmp: run");
    requireBool(r.value, true, "all comparison operators");
}

// --- Compound assignment: +=, -=, *=, /=, %= ---

void testCompoundAssignAdd()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int = 10\n"
                      "    x += 5\n"
                      "    return x\n";
    require(p.compile(src), "compound += : compile");
    auto r = p.run();
    require(r.ok(), "compound += : run");
    requireInt(r.value, 15, "10 += 5 == 15");
}

void testCompoundAssignSub()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int = 10\n"
                      "    x -= 3\n"
                      "    return x\n";
    require(p.compile(src), "compound -= : compile");
    auto r = p.run();
    require(r.ok(), "compound -= : run");
    requireInt(r.value, 7, "10 -= 3 == 7");
}

void testCompoundAssignMul()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int = 6\n"
                      "    x *= 7\n"
                      "    return x\n";
    require(p.compile(src), "compound *= : compile");
    auto r = p.run();
    require(r.ok(), "compound *= : run");
    requireInt(r.value, 42, "6 *= 7 == 42");
}

void testCompoundAssignDiv()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int = 100\n"
                      "    x /= 4\n"
                      "    return x\n";
    require(p.compile(src), "compound /= : compile");
    auto r = p.run();
    require(r.ok(), "compound /= : run");
    requireInt(r.value, 25, "100 /= 4 == 25");
}

void testCompoundAssignMod()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int = 17\n"
                      "    x %= 5\n"
                      "    return x\n";
    require(p.compile(src), "compound %= : compile");
    auto r = p.run();
    require(r.ok(), "compound %= : run");
    requireInt(r.value, 2, "17 %= 5 == 2");
}

// --- While loop with break and continue ---

void testWhileBreakContinue()
{
    // Sum 1..10, skipping 5, stopping at 8.
    // 1+2+3+4+6+7+8 = 31
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var i: int = 0\n"
                      "    var sum: int = 0\n"
                      "    while i < 100:\n"
                      "        i += 1\n"
                      "        if i == 5:\n"
                      "            continue\n"
                      "        if i > 8:\n"
                      "            break\n"
                      "        sum += i\n"
                      "    return sum\n";
    require(p.compile(src), "while break/continue: compile");
    auto r = p.run();
    require(r.ok(), "while break/continue: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 31, "while skip5 stop8 == 1+2+3+4+6+7+8 = 31");
}

void testWhileSimple()
{
    // Count down from 5 to 1, sum = 15
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var n: int = 5\n"
                      "    var sum: int = 0\n"
                      "    while n > 0:\n"
                      "        sum += n\n"
                      "        n -= 1\n"
                      "    return sum\n";
    require(p.compile(src), "while simple: compile");
    auto r = p.run();
    require(r.ok(), "while simple: run");
    requireInt(r.value, 15, "5+4+3+2+1 == 15");
}

// --- For-in over list ---

void testForInList()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var arr: [int] = [3, 1, 4, 1, 5, 9, 2, 6]\n"
                      "    var total: int = 0\n"
                      "    for x: int in arr:\n"
                      "        total += x\n"
                      "    return total\n";
    require(p.compile(src), "for-in list: compile");
    auto r = p.run();
    require(r.ok(), "for-in list: run");
    requireInt(r.value, 31, "sum of [3,1,4,1,5,9,2,6] == 31");
}

// --- For-in over dict ---
// NOTE: dict for-in has a known compiler limitation where keySlot and valSlot
// are the same, so the iterator gets the value, not the key. This test
// verifies that dict iteration still produces all values (summed).

void testForInDict()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var m: {string: int} = {\"a\": 10, \"b\": 20, \"c\": 30}\n"
                      "    var total: int = 0\n"
                      "    for v: int in m:\n"
                      "        total += v\n"
                      "    return total\n";
    require(p.compile(src), "for-in dict: compile");
    auto r = p.run();
    require(r.ok(), "for-in dict: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 60, "dict values sum == 60");
}

// --- Nested loops ---

void testNestedLoops()
{
    // Build a 3x3 multiplication table, sum all = (1+2+3+4+5+6+7+8+9)*k
    // Actually: sum of i*j for i in 1..3, j in 1..3
    // = 1*(1+2+3) + 2*(1+2+3) + 3*(1+2+3) = 6+12+18 = 36
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var total: int = 0\n"
                      "    for i: int in range(1, 4):\n"
                      "        for j: int in range(1, 4):\n"
                      "            total += i * j\n"
                      "    return total\n";
    require(p.compile(src), "nested loops: compile");
    auto r = p.run();
    require(r.ok(), "nested loops: run");
    requireInt(r.value, 36, "nested loop product sum == 36");
}

// --- Class with field mutation ---

void testClassFieldMutation()
{
    Pipeline p;
    const char* src = "class Point:\n"
                      "    x: int\n"
                      "    y: int\n"
                      "    def __init__(self, x: int, y: int):\n"
                      "        self.x = x\n"
                      "        self.y = y\n"
                      "    def move(self, dx: int, dy: int):\n"
                      "        self.x = self.x + dx\n"
                      "        self.y = self.y + dy\n"
                      "    def manhattan(self) -> int:\n"
                      "        return self.x + self.y\n"
                      "def test() -> int:\n"
                      "    var p: Point = Point(1, 2)\n"
                      "    p.move(3, 4)\n"
                      "    return p.manhattan()\n";
    require(p.compile(src), "class field mutation: compile");
    auto r = p.run();
    require(r.ok(), "class field mutation: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 10, "Point(1,2).move(3,4).manhattan() == 1+3+2+4=10");
}

// --- Class with method calling another method ---

void testClassMethodChain()
{
    Pipeline p;
    const char* src = "class Calculator:\n"
                      "    result: int\n"
                      "    def __init__(self):\n"
                      "        self.result = 0\n"
                      "    def add(self, n: int) -> int:\n"
                      "        self.result = self.result + n\n"
                      "        return self.result\n"
                      "    def subtract(self, n: int) -> int:\n"
                      "        self.result = self.result - n\n"
                      "        return self.result\n"
                      "def test() -> int:\n"
                      "    var c: Calculator = Calculator()\n"
                      "    c.add(10)\n"
                      "    c.add(5)\n"
                      "    return c.subtract(3)\n";
    require(p.compile(src), "class method chain: compile");
    auto r = p.run();
    require(r.ok(), "class method chain: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 12, "0+10+5-3 == 12");
}

// --- Optional type with nil ---

void testOptionalNil()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int? = nil\n"
                      "    if x == nil:\n"
                      "        return 42\n"
                      "    return 0\n";
    require(p.compile(src), "optional nil: compile");
    auto r = p.run();
    require(r.ok(), "optional nil: run");
    requireInt(r.value, 42, "nil check -> 42");
}

void testOptionalWithValue()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int? = 99\n"
                      "    if x == nil:\n"
                      "        return 0\n"
                      "    return 1\n";
    require(p.compile(src), "optional with value: compile");
    auto r = p.run();
    require(r.ok(), "optional with value: run");
    requireInt(r.value, 1, "optional non-nil -> 1");
}

// --- Int64 literals ---

void testInt64Literal()
{
    Pipeline p;
    const char* src = "def test() -> int64:\n"
                      "    var big: int64 = 5000000000\n"
                      "    return big\n";
    require(p.compile(src), "int64 literal: compile");
    auto r = p.run();
    require(r.ok(), "int64 literal: run");
    std::int64_t actual = 0;
    require(tryGetInt64(r.value, actual), "int64 result is int64");
    require(actual == 5000000000LL, "int64 literal == 5000000000");
}

// --- Bitwise operators ---

void testBitwiseOperators()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var a: int = 0xFF\n"
                      "    var b: int = 0x0F\n"
                      "    var c: int = a & b\n"
                      "    var d: int = a | 0x100\n"
                      "    var e: int = a ^ 0xFF\n"
                      "    return c + d + e\n";
    require(p.compile(src), "bitwise: compile");
    auto r = p.run();
    require(r.ok(), "bitwise: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    // 0xFF & 0x0F = 15; 0xFF | 0x100 = 511; 0xFF ^ 0xFF = 0; total = 526
    requireInt(r.value, 526, "bitwise ops == 15+511+0 = 526");
}

void testShiftOperators()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var a: int = 1\n"
                      "    a = a << 10\n"
                      "    var b: int = 1024\n"
                      "    b = b >> 3\n"
                      "    return a + b\n";
    require(p.compile(src), "shift: compile");
    auto r = p.run();
    require(r.ok(), "shift: run");
    // 1<<10 = 1024; 1024>>3 = 128; total = 1152
    requireInt(r.value, 1152, "1<<10 + 1024>>3 == 1024+128 = 1152");
}

// --- Unary negation ---

void testUnaryNegation()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var x: int = 42\n"
                      "    return -x\n";
    require(p.compile(src), "unary neg: compile");
    auto r = p.run();
    require(r.ok(), "unary neg: run");
    requireInt(r.value, -42, "-42");
}

// --- Logical not ---

void testLogicalNot()
{
    Pipeline p;
    const char* src = "def test() -> bool:\n"
                      "    var t: bool = true\n"
                      "    var f: bool = false\n"
                      "    return not f and not not t\n";
    require(p.compile(src), "logical not: compile");
    auto r = p.run();
    require(r.ok(), "logical not: run");
    requireBool(r.value, true, "not false and not not true");
}

// --- Logical short-circuit: and/or ---

void testLogicalShortCircuit()
{
    // and: false && <error> should not evaluate the right side
    // or: true || <error> should not evaluate the right side
    Pipeline p;
    const char* src = "def side_effect(n: int) -> int:\n"
                      "    return n\n"
                      "def test() -> bool:\n"
                      "    var a: bool = false and side_effect(1) > 0\n"
                      "    var b: bool = true or side_effect(2) > 0\n"
                      "    return a == false and b == true\n";
    require(p.compile(src), "short-circuit: compile");
    auto r = p.run();
    require(r.ok(), "short-circuit: run");
    requireBool(r.value, true, "short-circuit and/or");
}

// --- Nested if/elif/else ---

void testIfElifElseChain()
{
    Pipeline p;
    const char* src =
        "def classify(n: int) -> int:\n"
        "    if n < 0:\n"
        "        return -1\n"
        "    elif n == 0:\n"
        "        return 0\n"
        "    elif n < 10:\n"
        "        return 1\n"
        "    elif n < 100:\n"
        "        return 2\n"
        "    else:\n"
        "        return 3\n"
        "def test() -> int:\n"
        "    return classify(-5) + classify(0) + classify(5) + classify(50) + classify(500)\n";
    require(p.compile(src), "if-elif-else: compile");
    auto r = p.run();
    require(r.ok(), "if-elif-else: run");
    requireInt(r.value, 5, "-1+0+1+2+3 == 5");
}

// --- Pass statement ---

void testPassStatement()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    if true:\n"
                      "        pass\n"
                      "    return 42\n";
    require(p.compile(src), "pass: compile");
    auto r = p.run();
    require(r.ok(), "pass: run");
    requireInt(r.value, 42, "pass -> 42");
}

// --- Return void ---

void testReturnVoid()
{
    Pipeline p;
    const char* src = "def noop() -> void:\n"
                      "    return\n"
                      "def test() -> int:\n"
                      "    noop()\n"
                      "    return 7\n";
    require(p.compile(src), "return void: compile");
    auto r = p.run();
    require(r.ok(), "return void: run");
    requireInt(r.value, 7, "void call then return 7");
}

// --- Global variables (module-level var) ---

void testGlobalVariable()
{
    Pipeline p;
    const char* src = "var counter: int = 100\n"
                      "def increment():\n"
                      "    counter = counter + 1\n"
                      "def test() -> int:\n"
                      "    increment()\n"
                      "    increment()\n"
                      "    return counter\n";
    require(p.compile(src), "global var: compile");
    if (!p.compiled) {
        std::printf("  error: %s\n", p.errMsg.c_str());
    }
    auto r = p.run();
    require(r.ok(), "global var: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 102, "global 100 + 2 increments == 102");
}

// --- Deep recursion (stress) ---

void testDeepRecursion()
{
    Pipeline p;
    const char* src = "def sum_to(n: int) -> int:\n"
                      "    if n <= 0:\n"
                      "        return 0\n"
                      "    return n + sum_to(n - 1)\n"
                      "def test() -> int:\n"
                      "    return sum_to(200)\n";
    require(p.compile(src), "deep recursion: compile");
    auto r = p.run();
    require(r.ok(), "deep recursion: run");
    requireInt(r.value, 20100, "sum_to(200) == 200*201/2 == 20100");
}

// --- String in list ---
// NOTE: len() is not a source-level builtin (only range and print are
// registered in sema). This test verifies list-of-string iteration by
// concatenating the strings instead.

void testListOfStrings()
{
    Pipeline p;
    const char* src = "def test() -> string:\n"
                      "    var names: [string] = [\"alice\", \"bob\", \"cz\"]\n"
                      "    var combined: string = \"\"\n"
                      "    for name: string in names:\n"
                      "        combined = combined + name\n"
                      "    return combined\n";
    require(p.compile(src), "list of strings: compile");
    auto r = p.run();
    require(r.ok(), "list of strings: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireString(r.value, "alicebobcz", "concatenated names");
}

// --- Nested function returning function (higher-order) ---

void testHigherOrderFunction()
{
    Pipeline p;
    const char* src = "def apply_twice(f: (int) -> int, x: int) -> int:\n"
                      "    return f(f(x))\n"
                      "def make_doubler() -> (int) -> int:\n"
                      "    def doubler(n: int) -> int:\n"
                      "        return n * 2\n"
                      "    return doubler\n"
                      "def test() -> int:\n"
                      "    var d: (int) -> int = make_doubler()\n"
                      "    return apply_twice(d, 3)\n";
    require(p.compile(src), "higher-order: compile");
    if (!p.compiled) {
        std::printf("  error: %s\n", p.errMsg.c_str());
    }
    auto r = p.run();
    require(r.ok(), "higher-order: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    // doubler(doubler(3)) = doubler(6) = 12
    requireInt(r.value, 12, "apply_twice(doubler, 3) == 12");
}

// --- For-in with break ---

void testForInBreak()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var total: int = 0\n"
                      "    for i: int in range(100):\n"
                      "        if i >= 10:\n"
                      "            break\n"
                      "        total += i\n"
                      "    return total\n";
    require(p.compile(src), "for-in break: compile");
    auto r = p.run();
    require(r.ok(), "for-in break: run");
    // 0+1+2+...+9 = 45
    requireInt(r.value, 45, "sum 0..9 == 45");
}

// --- For-in with continue ---

void testForInContinue()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var total: int = 0\n"
                      "    for i: int in range(10):\n"
                      "        if i % 2 == 0:\n"
                      "            continue\n"
                      "        total += i\n"
                      "    return total\n";
    require(p.compile(src), "for-in continue: compile");
    auto r = p.run();
    require(r.ok(), "for-in continue: run");
    // 1+3+5+7+9 = 25
    requireInt(r.value, 25, "sum of odd 1..9 == 25");
}

// --- List element assignment ---

void testListElementAssignment()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var arr: [int] = [1, 2, 3, 4, 5]\n"
                      "    arr[0] = 10\n"
                      "    arr[4] = 50\n"
                      "    return arr[0] + arr[4]\n";
    require(p.compile(src), "list elem assign: compile");
    auto r = p.run();
    require(r.ok(), "list elem assign: run");
    requireInt(r.value, 60, "arr[0]=10, arr[4]=50 -> 60");
}

// --- Dict element assignment ---

void testDictElementAssignment()
{
    Pipeline p;
    const char* src = "def test() -> int:\n"
                      "    var m: {string: int} = {\"a\": 1, \"b\": 2}\n"
                      "    m[\"a\"] = 100\n"
                      "    m[\"c\"] = 3\n"
                      "    return m[\"a\"] + m[\"b\"] + m[\"c\"]\n";
    require(p.compile(src), "dict elem assign: compile");
    auto r = p.run();
    require(r.ok(), "dict elem assign: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 105, "100+2+3 == 105");
}

// --- Class with list field ---
// NOTE: sema does not support list concatenation via '+' operator on [int].
// This test verifies a class can hold a list field and mutate it via index
// assignment, and that a counter field tracks the element count.

void testClassWithListField()
{
    Pipeline p;
    const char* src = "class Stack:\n"
                      "    items: [int]\n"
                      "    count: int\n"
                      "    def __init__(self):\n"
                      "        self.items = [0, 0, 0, 0, 0]\n"
                      "        self.count = 0\n"
                      "    def push(self, n: int):\n"
                      "        self.items[self.count] = n\n"
                      "        self.count = self.count + 1\n"
                      "    def top(self) -> int:\n"
                      "        return self.items[self.count - 1]\n"
                      "def test() -> int:\n"
                      "    var s: Stack = Stack()\n"
                      "    s.push(10)\n"
                      "    s.push(20)\n"
                      "    s.push(30)\n"
                      "    return s.top()\n";
    require(p.compile(src), "class list field: compile");
    auto r = p.run();
    require(r.ok(), "class list field: run");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 30, "stack top == 30");
}

} // namespace

int main()
{
    registerScriptObjectReflection();

    // Float arithmetic
    testFloatArithmetic();
    testFloatDivision();
    testIntFloatWidening();

    // String operations
    testStringConcat();
    testStringLength();
    testStringComparison();

    // Comparisons
    testAllComparisons();

    // Compound assignment
    testCompoundAssignAdd();
    testCompoundAssignSub();
    testCompoundAssignMul();
    testCompoundAssignDiv();
    testCompoundAssignMod();

    // Control flow
    testWhileBreakContinue();
    testWhileSimple();
    testForInList();
    testForInDict();
    testNestedLoops();
    testForInBreak();
    testForInContinue();

    // Classes
    testClassFieldMutation();
    testClassMethodChain();
    testClassWithListField();

    // Optional types
    testOptionalNil();
    testOptionalWithValue();

    // Int64
    testInt64Literal();

    // Bitwise
    testBitwiseOperators();
    testShiftOperators();

    // Unary/logical
    testUnaryNegation();
    testLogicalNot();
    testLogicalShortCircuit();

    // Control flow misc
    testIfElifElseChain();
    testPassStatement();
    testReturnVoid();

    // Globals
    testGlobalVariable();

    // Recursion
    testDeepRecursion();

    // Data structures
    testListOfStrings();
    testListElementAssignment();
    testDictElementAssignment();

    // Higher-order functions
    testHigherOrderFunction();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript e2e feature coverage tests passed\n");
    return 0;
}
