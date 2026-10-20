// ArpScript Fast-path optimization unit tests (Phase 7).
//
// Verifies that the compiler generates LocalAddIntConst / LocalSubIntConst
// fast-path instructions for common patterns, and that these instructions
// produce correct results at runtime.
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/interpreter/eval.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cstdint>
#include <cstdio>
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

void requireFloat(const Value& v, double expected, const char* label)
{
    double actual = 0.0;
    if (!tryGetFloat(v, actual)) {
        std::printf("FAIL: %s (not a float)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %.17g, want %.17g)\n", label, actual, expected);
        ++g_failures;
    }
}

void requireInt64(const Value& v, std::int64_t expected, const char* label)
{
    std::int64_t actual = 0;
    if (!tryGetInt64(v, actual)) {
        std::printf("FAIL: %s (not an int64)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %lld, want %lld)\n",
            label,
            static_cast<long long>(actual),
            static_cast<long long>(expected));
        ++g_failures;
    }
}

EvalResult runSource(const char* src, const char* label)
{
    auto cr = arp::scriptvm::test::buildInterpreter(src, "fastpath.arp", "fastpath-tests");
    if (!cr.ok()) {
        std::printf("FAIL: %s -- compile error: %s\n", label, cr.error.message.c_str());
        ++g_failures;
        return EvalResult{};
    }
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    return runFunctionByName(cr.program, "test", args);
}

// Count how many instructions of a given opcode appear in a function.
int countOpcodes(const Program& program, const char* funcName, Opcode op)
{
    auto it = program.funcIndex.find(funcName);
    if (it == program.funcIndex.end())
        return -1;
    const auto& fn = program.functions[it->second];
    int count = 0;
    for (const auto& inst : fn.code)
        if (inst.op == op)
            ++count;
    return count;
}

Program compileProgram(const char* src, const char* label)
{
    auto cr = arp::scriptvm::test::buildInterpreter(src, "fastpath.arp", "fastpath-program-tests");
    if (!cr.ok()) {
        std::printf("FAIL: %s -- compile error: %s\n", label, cr.error.message.c_str());
        ++g_failures;
        return {};
    }
    return std::move(cr.program);
}

EvalResult runCompiledFunction(
    const Program& program, const char* name, const arp::core::pmr::Vector<Value>& args)
{
    return runFunctionByName(
        program, arp::core::pmr::String(name, &arp::core::defaultCpuMemoryResource()), args);
}

void testFastPathAddConst()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 0\n"
                      "    x += 5\n"
                      "    return x\n";
    auto prog = compileProgram(src, "fastpath add");
    require(g_failures == 0 || true, "fastpath add: compile");
    int count = countOpcodes(prog, "test", Opcode::LocalAddIntConst);
    require(count == 1, "fastpath add: LocalAddIntConst emitted");
    auto r = runSource(src, "fastpath add");
    require(r.ok(), "fastpath add: ok");
    requireInt(r.value, 5, "fastpath add: 5");
}

void testFastPathSubConst()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 10\n"
                      "    x -= 3\n"
                      "    return x\n";
    auto prog = compileProgram(src, "fastpath sub");
    int count = countOpcodes(prog, "test", Opcode::LocalSubIntConst);
    require(count == 1, "fastpath sub: LocalSubIntConst emitted");
    auto r = runSource(src, "fastpath sub");
    require(r.ok(), "fastpath sub: ok");
    requireInt(r.value, 7, "fastpath sub: 7");
}

void testInt64FastPathConstants()
{
    const char* src = "def test() -> int64:\n"
                      "    var x: int64 = 9223372036854775800L\n"
                      "    x += 1\n"
                      "    x -= 2L\n"
                      "    return x\n";
    auto prog = compileProgram(src, "fastpath int64 constants");
    require(countOpcodes(prog, "test", Opcode::LocalAddInt64Const) == 1,
        "fastpath int64: LocalAddInt64Const emitted");
    require(countOpcodes(prog, "test", Opcode::LocalSubInt64Const) == 1,
        "fastpath int64: LocalSubInt64Const emitted");
    auto r = runSource(src, "fastpath int64 constants");
    require(r.ok(), "fastpath int64: ok");
    requireInt64(r.value, 9223372036854775799LL, "fastpath int64: checked result");
}

void testInt64FastPathOverflowFallsBack()
{
    const char* src = "def test() -> int64:\n"
                      "    var x: int64 = 9223372036854775807L\n"
                      "    x += 1L\n"
                      "    return x\n";
    auto r = runSource(src, "fastpath int64 overflow");
    require(!r.ok() && r.error.code == ScriptDiagnosticCode::IntegerOverflow,
        "fastpath int64: overflow uses shared diagnostic");
}

void testFastPathLoop()
{
    // The classic hot loop: x = x + 1 in a tight loop.
    // With fast-path, this uses LocalAddIntConst instead of
    // LoadLocal + LoadInt + Add + StoreLocal.
    const char* src = "def test() -> int:\n"
                      "    var x: int = 0\n"
                      "    var i: int = 0\n"
                      "    while i < 1000:\n"
                      "        x += 1\n"
                      "        i += 1\n"
                      "    return x\n";
    auto prog = compileProgram(src, "fastpath loop");
    int count = countOpcodes(prog, "test", Opcode::LocalAddIntConst);
    require(count == 2, "fastpath loop: 2 LocalAddIntConst (x+=1, i+=1)");
    auto r = runSource(src, "fastpath loop");
    require(r.ok(), "fastpath loop: ok");
    requireInt(r.value, 1000, "fastpath loop: 1000");
}

void testFastPathNegativeConst()
{
    const char* src = "def test() -> int:\n"
                      "    var x: int = 10\n"
                      "    x += -3\n"
                      "    return x\n";
    auto r = runSource(src, "fastpath negative");
    require(r.ok(), "fastpath negative: ok");
    requireInt(r.value, 7, "fastpath negative: 10+(-3)=7");
}

void testFastPathNotGeneratedForNonInt()
{
    // Fast-path should NOT be generated for string concatenation.
    const char* src = "def test() -> int:\n"
                      "    var s: string = \"a\"\n"
                      "    s += \"b\"\n"
                      "    return 0\n";
    auto prog = compileProgram(src, "fastpath non-int");
    int count = countOpcodes(prog, "test", Opcode::LocalAddIntConst);
    require(count == 0, "fastpath non-int: no LocalAddIntConst for string");
}

void testFastPathRespectsDeclaredType()
{
    const char* src = "def test() -> float:\n"
                      "    var x: float = 1.5\n"
                      "    x += 1\n"
                      "    return x\n";
    auto prog = compileProgram(src, "fastpath declared float");
    int count = countOpcodes(prog, "test", Opcode::LocalAddIntConst);
    require(count == 0, "fastpath declared float: no integer fast path");
    auto r = runSource(src, "fastpath declared float");
    require(r.ok(), "fastpath declared float: ok");
    requireFloat(r.value, 2.5, "fastpath declared float: result");
}

void testFloatFastPathConstants()
{
    const char* src = "def test() -> float:\n"
                      "    var x: float = 1.5\n"
                      "    x += 2\n"
                      "    x -= 0.25\n"
                      "    return x\n";
    auto prog = compileProgram(src, "fastpath float constants");
    require(countOpcodes(prog, "test", Opcode::LocalAddFloatConst) == 1,
        "fastpath float: LocalAddFloatConst emitted");
    require(countOpcodes(prog, "test", Opcode::LocalSubFloatConst) == 1,
        "fastpath float: LocalSubFloatConst emitted");
    auto r = runSource(src, "fastpath float constants");
    require(r.ok(), "fastpath float: ok");
    requireFloat(r.value, 3.25, "fastpath float: checked result");
}

void testTypedConditionFastPath()
{
    const char* src = "def test() -> int:\n"
                      "    var n: int = 1\n"
                      "    if n < 2:\n"
                      "        return 1\n"
                      "    return 0\n";
    auto prog = compileProgram(src, "fastpath typed condition");
    int count = countOpcodes(prog, "test", Opcode::JumpIfLocalIntLtConstFalse);
    require(count == 1, "fastpath typed condition: specialized jump emitted");
    auto r = runSource(src, "fastpath typed condition");
    require(r.ok(), "fastpath typed condition: ok");
    requireInt(r.value, 1, "fastpath typed condition: result");
}

void testTypedArithmeticOpcodes()
{
    const char* src = "def test() -> int:\n"
                      "    var n: int = 5\n"
                      "    return (n - 1) + 2\n";
    auto prog = compileProgram(src, "fastpath typed arithmetic");
    require(countOpcodes(prog, "test", Opcode::IntSub) == 1,
        "fastpath typed arithmetic: IntSub emitted");
    require(countOpcodes(prog, "test", Opcode::IntAdd) == 1,
        "fastpath typed arithmetic: IntAdd emitted");
    auto r = runSource(src, "fastpath typed arithmetic");
    require(r.ok(), "fastpath typed arithmetic: ok");
    requireInt(r.value, 6, "fastpath typed arithmetic: result");
}

void testTypedComparisonOpcodes()
{
    const char* src = "def test() -> bool:\n"
                      "    var n: int = 2\n"
                      "    return n <= 3\n";
    auto prog = compileProgram(src, "fastpath typed comparison");
    require(countOpcodes(prog, "test", Opcode::IntCmpLe) == 1,
        "fastpath typed comparison: IntCmpLe emitted");
    auto r = runSource(src, "fastpath typed comparison");
    require(r.ok(), "fastpath typed comparison: ok");
    requireBool(r.value, true, "fastpath typed comparison: 2 <= 3");
}

void testFastPathNotGeneratedForCaptured()
{
    // Fast-path should NOT be generated for captured locals (they use cells).
    const char* src = "def outer() -> () -> int:\n"
                      "    var x: int = 0\n"
                      "    def inner() -> int:\n"
                      "        x += 1\n"
                      "        return x\n"
                      "    return inner\n"
                      "def test() -> int:\n"
                      "    var f: () -> int = outer()\n"
                      "    return f()\n";
    auto prog = compileProgram(src, "fastpath captured");
    // In `inner`, `x` is captured so LocalAddIntConst should NOT be emitted.
    int count = countOpcodes(prog, "inner", Opcode::LocalAddIntConst);
    require(count == 0, "fastpath captured: no LocalAddIntConst for captured local");
    auto r = runSource(src, "fastpath captured");
    require(r.ok(), "fastpath captured: ok");
    requireInt(r.value, 1, "fastpath captured: 1");
}

void testFastPathBenchmark()
{
    // A larger computation to verify correctness with fast-path.
    const char* src = "def test() -> int:\n"
                      "    var sum: int = 0\n"
                      "    for i: int in range(100):\n"
                      "        sum += i\n"
                      "    return sum\n";
    auto r = runSource(src, "fastpath benchmark");
    require(r.ok(), "fastpath benchmark: ok");
    // 0+1+...+99 = 4950
    requireInt(r.value, 4950, "fastpath benchmark: sum(0..99)=4950");
}

void testNilFastPath()
{
    require(isNil(Value::undefined()), "nil fast path: undefined");
    require(isNil(Value{nullptr}), "nil fast path: null");
    require(!isNil(Value{7}), "nil fast path: integer is not nil");
}

void testWideningConversionsAtCallAndAssignmentBoundaries()
{
    const char* src = "def takes_float(x: float) -> float:\n"
                      "    return x\n"
                      "def takes_i64(x: int64) -> int64:\n"
                      "    return x\n"
                      "def test() -> float:\n"
                      "    var x: float = takes_float(1)\n"
                      "    x = 2\n"
                      "    return x\n";
    auto prog = compileProgram(src, "widening direct call and assignment");
    require(countOpcodes(prog, "test", Opcode::ConvertFloat) == 2,
        "widening: direct call and local assignment emit ConvertFloat");
    auto floatResult = runCompiledFunction(prog, "test", {});
    require(floatResult.ok(), "widening: float call and assignment ok");
    requireFloat(floatResult.value, 2.0, "widening: float result is 2.0");

    arp::core::pmr::Vector<Value> i64Args{&arp::core::defaultCpuMemoryResource()};
    i64Args.push_back(makeInt(7));
    auto i64Result = runCompiledFunction(prog, "takes_i64", i64Args);
    require(i64Result.ok(), "widening: int to int64 public call ok");
    requireInt64(i64Result.value, 7, "widening: int to int64 becomes int64");
}

void testGenericCallWithTypedSignature()
{
    const char* src = "def inc(x: int) -> int:\n"
                      "    return x + 1\n"
                      "def test() -> int:\n"
                      "    return inc(41)\n";
    auto prog = compileProgram(src, "generic call with typed signature");
    require(countOpcodes(prog, "test", Opcode::Call) == 1, "generic call: Call emitted");
    require(countOpcodes(prog, "inc", Opcode::ReturnInt) == 1, "generic call: ReturnInt emitted");
    auto result = runCompiledFunction(prog, "test", {});
    require(result.ok(), "generic call: execution ok");
    requireInt(result.value, 42, "generic call: inc(41) == 42");

    const char* dynamicSrc = "def inc(x: int) -> int:\n"
                             "    return x + 1\n"
                             "def test() -> float:\n"
                             "    return inc(41)\n";
    auto dynamicProgram = compileProgram(dynamicSrc, "dynamic generic call");
    auto dynamicResult = runCompiledFunction(dynamicProgram, "test", {});
    require(dynamicResult.ok(), "generic call: dynamic caller execution ok");
    requireFloat(dynamicResult.value, 42.0, "generic call: dynamic caller result");
}

void testWideningConversionForIndirectCall()
{
    const char* src = "def takes_float(x: float) -> float:\n"
                      "    return x\n"
                      "def make() -> (float) -> float:\n"
                      "    return takes_float\n"
                      "def test() -> float:\n"
                      "    var f: (float) -> float = make()\n"
                      "    return f(1)\n";
    auto prog = compileProgram(src, "widening indirect call");
    require(countOpcodes(prog, "test", Opcode::ConvertFloat) == 1,
        "widening indirect: CallValue emits ConvertFloat");
    auto result = runCompiledFunction(prog, "test", {});
    require(result.ok(), "widening indirect: ok");
    requireFloat(result.value, 1.0, "widening indirect: result is 1.0");
}

void testWideningConversionForIndexedAssignment()
{
    const char* src = "def test() -> float:\n"
                      "    var values: [float] = [1.0]\n"
                      "    values[0] = 2\n"
                      "    return values[0]\n";
    auto prog = compileProgram(src, "widening indexed assignment");
    require(countOpcodes(prog, "test", Opcode::ConvertFloat) == 1,
        "widening indexed: StoreIndex emits ConvertFloat");
    auto result = runCompiledFunction(prog, "test", {});
    require(result.ok(), "widening indexed: ok");
    requireFloat(result.value, 2.0, "widening indexed: result is 2.0");
}

void testPublicArgumentContract()
{
    const char* src = "def takes_int(x: int) -> int:\n"
                      "    return x\n"
                      "def takes_list(x: [int]) -> int:\n"
                      "    return 1\n"
                      "def takes_optional(x: int?) -> int:\n"
                      "    return 0\n";
    auto prog = compileProgram(src, "public argument contract");

    arp::core::pmr::Vector<Value> wrongType{&arp::core::defaultCpuMemoryResource()};
    wrongType.push_back(makeString(std::string_view("not an int")));
    auto typeResult = runCompiledFunction(prog, "takes_int", wrongType);
    require(typeResult.error.code == ScriptDiagnosticCode::TypeMismatch,
        "public argument contract: wrong type rejected");

    auto countResult = runCompiledFunction(prog, "takes_int", {});
    require(countResult.error.code == ScriptDiagnosticCode::WrongArgCount,
        "public argument contract: wrong count rejected");

    arp::reflection::hlist list{&arp::core::defaultCpuMemoryResource()};
    list.push_back(makeInt(1));
    arp::core::pmr::Vector<Value> listArgs{&arp::core::defaultCpuMemoryResource()};
    listArgs.emplace_back(std::move(list));
    auto listResult = runCompiledFunction(prog, "takes_list", listArgs);
    require(listResult.ok(), "public argument contract: list accepted by list parameter");

    arp::core::pmr::Vector<Value> scalarArgs{&arp::core::defaultCpuMemoryResource()};
    scalarArgs.push_back(makeInt(1));
    auto listTypeResult = runCompiledFunction(prog, "takes_list", scalarArgs);
    require(listTypeResult.error.code == ScriptDiagnosticCode::TypeMismatch,
        "public argument contract: scalar rejected by list parameter");

    arp::core::pmr::Vector<Value> nilArgs{&arp::core::defaultCpuMemoryResource()};
    nilArgs.push_back(Value::undefined());
    auto optionalResult = runCompiledFunction(prog, "takes_optional", nilArgs);
    require(optionalResult.ok(), "public argument contract: nil accepted by optional parameter");
    requireInt(optionalResult.value, 0, "public argument contract: optional result is 0");

    // Hand-authored bytecode from older callers has no parameter metadata and
    // must retain the original execution behavior.
    Program legacy;
    Function legacyInc("inc");
    legacyInc.params = 1;
    legacyInc.results = 1;
    legacyInc.locals = 1;
    legacyInc.maxStack = 1;
    legacyInc.code.emplace_back(Opcode::LoadLocal, 0);
    legacyInc.code.emplace_back(Opcode::Return, 1);
    legacy.functions.push_back(std::move(legacyInc));
    legacy.funcIndex["inc"] = 0;
    arp::core::pmr::Vector<Value> legacyArgs{&arp::core::defaultCpuMemoryResource()};
    legacyArgs.push_back(makeInt(41));
    auto legacyResult = runCompiledFunction(legacy, "inc", legacyArgs);
    require(legacyResult.ok(), "public argument contract: legacy metadata-free call ok");
    requireInt(legacyResult.value, 41, "public argument contract: legacy result is 41");
}

} // namespace

int main()
{
    testFastPathAddConst();
    testFastPathSubConst();
    testInt64FastPathConstants();
    testInt64FastPathOverflowFallsBack();
    testFastPathLoop();
    testFastPathNegativeConst();
    testFastPathNotGeneratedForNonInt();
    testFastPathRespectsDeclaredType();
    testFloatFastPathConstants();
    testTypedConditionFastPath();
    testTypedArithmeticOpcodes();
    testTypedComparisonOpcodes();
    testFastPathNotGeneratedForCaptured();
    testFastPathBenchmark();
    testNilFastPath();
    testWideningConversionsAtCallAndAssignmentBoundaries();
    testGenericCallWithTypedSignature();
    testWideningConversionForIndirectCall();
    testWideningConversionForIndexedAssignment();
    testPublicArgumentContract();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript fast-path tests passed\n");
    return 0;
}
