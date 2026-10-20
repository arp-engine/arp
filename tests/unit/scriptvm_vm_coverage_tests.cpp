// ArpScript VM coverage tests.
//
// Hand-authored bytecode Programs exercising opcodes and paths NOT covered
// by scriptvm_vm_tests.cpp: missing arithmetic/bitwise ops, container
// mutation builtins, conversion opcodes, globals, pointer/address ops,
// MakeFuncValue/MakeMethodValue/MakeRangeFromStack, JumpIfTrue, literal
// loads (nil/bool/float/int64), fast-path variants, and runtime error paths.
//
// Test style follows scriptvm_vm_tests.cpp: self-contained main() + require()
// helpers, no third-party framework.

#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/runtime/value.h"

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
        std::printf("FAIL: %s (not an int: type=%d)\n", label, static_cast<int>(v.type()));
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, want %d)\n", label, actual, expected);
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

void requireNil(const Value& v, const char* label)
{
    if (!isNil(v)) {
        std::printf("FAIL: %s (expected nil, type=%d)\n", label, static_cast<int>(v.type()));
        ++g_failures;
    }
}

void requireError(const EvalResult& r, ScriptDiagnosticCode expectedCode, const char* label)
{
    if (r.ok()) {
        std::printf(
            "FAIL: %s (expected error %d, got success)\n", label, static_cast<int>(expectedCode));
        ++g_failures;
        return;
    }
    if (r.error.code != expectedCode) {
        std::printf("FAIL: %s (got code %d, want %d): %s\n",
            label,
            static_cast<int>(r.error.code),
            static_cast<int>(expectedCode),
            r.error.message.c_str());
        ++g_failures;
    }
}

// Helper to build a one-function program returning a single result.
struct SingleFnProgram {
    Program program;
    Function& fn;

    SingleFnProgram(const char* name, int locals, int maxStack)
        : fn(program.functions.emplace_back(name))
    {
        fn.params = 0;
        fn.results = 1;
        fn.locals = locals;
        fn.maxStack = maxStack;
        program.entry = 0;
        program.funcIndex[arp::core::pmr::String(name, &arp::core::defaultCpuMemoryResource())] = 0;
    }

    EvalResult run()
    {
        fn.code.emplace_back(Opcode::Return, 1);
        return runProgram(program);
    }
};

// --- Literal load opcodes ---

void testLoadNil()
{
    SingleFnProgram p("main", 0, 1);
    p.fn.code.emplace_back(Opcode::LoadNil);
    auto r = p.run();
    require(r.ok(), "LoadNil ok");
    requireNil(r.value, "LoadNil pushes nil");
}

void testLoadBool()
{
    // return true
    SingleFnProgram p("main", 0, 1);
    p.fn.code.emplace_back(Opcode::LoadBool, 1);
    auto r = p.run();
    require(r.ok(), "LoadBool ok");
    requireBool(r.value, true, "LoadBool true");

    // return false
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadBool, 0);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r2 = runProgram(program);
    require(r2.ok(), "LoadBool false ok");
    requireBool(r2.value, false, "LoadBool false");
}

void testLoadFloat()
{
    // LoadFloat uses bit-pattern: I64 = bits(double).
    // We want to load 2.5.
    std::int64_t bits = 0;
    double val = 2.5;
    std::memcpy(&bits, &val, sizeof(bits));

    SingleFnProgram p("main", 0, 1);
    p.fn.code.emplace_back(Opcode::LoadFloat, 0, 0, bits);
    auto r = p.run();
    require(r.ok(), "LoadFloat ok");
    requireFloat(r.value, 2.5, "LoadFloat 2.5");
}

void testLoadInt64()
{
    // Load a value exceeding int32 range.
    SingleFnProgram p("main", 0, 1);
    p.fn.code.emplace_back(Opcode::LoadInt64, 0, 0, 5000000000LL);
    auto r = p.run();
    require(r.ok(), "LoadInt64 ok");
    requireInt64(r.value, 5000000000LL, "LoadInt64 5B");
}

// --- Arithmetic: Div, Mod, Neg, Not ---

void testDivAndMod()
{
    // 17 / 5 = 3; 17 % 5 = 2
    SingleFnProgram p("main", 0, 2);
    p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 17);
    p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
    p.fn.code.emplace_back(Opcode::Div);
    auto r = p.run();
    require(r.ok(), "Div ok");
    requireInt(r.value, 3, "17/5 == 3");

    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 17);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
    fn.code.emplace_back(Opcode::Mod);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r2 = runProgram(program);
    require(r2.ok(), "Mod ok");
    requireInt(r2.value, 2, "17%5 == 2");
}

void testNeg()
{
    // -42
    SingleFnProgram p("main", 0, 1);
    p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 42);
    p.fn.code.emplace_back(Opcode::Neg);
    auto r = p.run();
    require(r.ok(), "Neg ok");
    requireInt(r.value, -42, "Neg 42 -> -42");
}

void testNot()
{
    // not true -> false; not false -> true
    SingleFnProgram p("main", 0, 1);
    p.fn.code.emplace_back(Opcode::LoadBool, 1);
    p.fn.code.emplace_back(Opcode::Not);
    auto r = p.run();
    require(r.ok(), "Not ok");
    requireBool(r.value, false, "not true -> false");

    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadBool, 0);
    fn.code.emplace_back(Opcode::Not);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r2 = runProgram(program);
    require(r2.ok(), "Not false ok");
    requireBool(r2.value, true, "not false -> true");
}

// --- Bitwise: BitAnd, BitOr, BitXor, BitClear, ShiftRight ---

void testBitwiseAll()
{
    // 0xF0 & 0x0F = 0; 0xF0 | 0x0F = 0xFF=255; 0xAA ^ 0xFF = 0x55=85; 256>>3=32
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0xF0);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0x0F);
        p.fn.code.emplace_back(Opcode::BitAnd);
        auto r = p.run();
        require(r.ok(), "BitAnd ok");
        requireInt(r.value, 0, "0xF0 & 0x0F == 0");
    }
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0xF0);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0x0F);
        p.fn.code.emplace_back(Opcode::BitOr);
        auto r = p.run();
        require(r.ok(), "BitOr ok");
        requireInt(r.value, 255, "0xF0 | 0x0F == 255");
    }
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0xAA);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0xFF);
        p.fn.code.emplace_back(Opcode::BitXor);
        auto r = p.run();
        require(r.ok(), "BitXor ok");
        requireInt(r.value, 85, "0xAA ^ 0xFF == 85");
    }
    {
        // ShiftRight: 256 >> 3 == 32
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 256);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
        p.fn.code.emplace_back(Opcode::ShiftRight);
        auto r = p.run();
        require(r.ok(), "ShiftRight ok");
        requireInt(r.value, 32, "256 >> 3 == 32");
    }
}

// --- Comparison: CmpLe, CmpNe (CmpEq/CmpLt/CmpGt/CmpGe already tested) ---

void testCmpLeAndNe()
{
    // 3 <= 3 -> true; 3 != 4 -> true
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
        p.fn.code.emplace_back(Opcode::CmpLe);
        auto r = p.run();
        require(r.ok(), "CmpLe ok");
        requireBool(r.value, true, "3 <= 3 -> true");
    }
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 4);
        p.fn.code.emplace_back(Opcode::CmpNe);
        auto r = p.run();
        require(r.ok(), "CmpNe ok");
        requireBool(r.value, true, "3 != 4 -> true");
    }
}

// --- JumpIfTrue ---

void testJumpIfTrue()
{
    // if true: return 1; else: return 0
    // JumpIfTrue target=5 (skip the else branch)
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadBool, 1);      // pc 0: push true
    fn.code.emplace_back(Opcode::JumpIfTrue, 5);    // pc 1: if true, jump to pc 5
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0); // pc 2: else: push 0
    fn.code.emplace_back(Opcode::Return, 1);        // pc 3
    // unreachable pad pc 4
    fn.code.emplace_back(Opcode::LoadNil);          // pc 4
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1); // pc 5: push 1
    fn.code.emplace_back(Opcode::Return, 1);        // pc 6
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "JumpIfTrue ok");
    requireInt(r.value, 1, "JumpIfTrue skips else -> 1");
}

// --- Globals: LoadGlobal / StoreGlobal ---

void testGlobals()
{
    // StoreGlobal slot 0 = 99, then LoadGlobal slot 0, return it.
    Program program;
    program.globalCount = 1;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    fn.code.emplace_back(Opcode::StoreGlobal, 0);
    fn.code.emplace_back(Opcode::LoadGlobal, 0);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "globals ok");
    requireInt(r.value, 99, "global store/load == 99");
}

void testGlobalsCrossFunction()
{
    // main stores 42 into global 0, calls helper which loads global 0 and returns it.
    Program program;
    program.globalCount = 1;

    // func 0: helper: LoadGlobal 0; Return 1
    Function& helper = program.functions.emplace_back("helper");
    helper.params = 0;
    helper.results = 1;
    helper.locals = 0;
    helper.maxStack = 1;
    helper.code.emplace_back(Opcode::LoadGlobal, 0);
    helper.code.emplace_back(Opcode::Return, 1);

    // func 1: main: LoadInt 42; StoreGlobal 0; Call 0,0; Return 1
    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 1;
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 42);
    mainFn.code.emplace_back(Opcode::StoreGlobal, 0);
    mainFn.code.emplace_back(Opcode::Call, 0, 0);
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 1;
    program.funcIndex["helper"] = 0;
    program.funcIndex["main"] = 1;

    auto r = runProgram(program);
    require(r.ok(), "globals cross-fn ok");
    requireInt(r.value, 42, "global shared across functions == 42");
}

// --- Container mutation: StoreIndex, BuiltinAppend, BuiltinDelete, BuiltinClear ---

void testStoreIndexList()
{
    // Build [10, 20, 30], set [1] = 99, return [1] -> 99
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 4;
    // Build list
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 30);
    fn.code.emplace_back(Opcode::MakeList, 0, 3);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // list[1] = 99  (push base, push index, push value, StoreIndex)
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    fn.code.emplace_back(Opcode::StoreIndex);
    // return list[1]
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::LoadIndex);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "StoreIndex list ok");
    requireInt(r.value, 99, "list[1]=99 then read == 99");
}

void testStoreIndexDict()
{
    // Build {"a": 1}, set ["b"] = 2, return dict["b"] -> 2
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 4;
    // Build dict {"a": 1}
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("a", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::MakeDict, 0, 1);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // dict["b"] = 2
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("b", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 2);
    fn.code.emplace_back(Opcode::StoreIndex);
    // return dict["b"]
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

    auto r = runProgram(program);
    require(r.ok(), "StoreIndex dict ok");
    requireInt(r.value, 2, "dict['b']=2 then read == 2");
}

void testBuiltinAppend()
{
    // Build [], append(10), append(20), return len -> 2
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 3;
    // Build empty list
    fn.code.emplace_back(Opcode::MakeList, 0, 0);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // append(10): push list, push 10, BuiltinAppend, store back
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::BuiltinAppend);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // append(20)
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
    fn.code.emplace_back(Opcode::BuiltinAppend);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // return len(list)
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::BuiltinLen);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "BuiltinAppend ok");
    requireInt(r.value, 2, "append twice -> len 2");

    // Also verify element access: return list[1] == 20
    Program program2;
    Function& fn2 = program2.functions.emplace_back("main");
    fn2.params = 0;
    fn2.results = 1;
    fn2.locals = 1;
    fn2.maxStack = 3;
    fn2.code.emplace_back(Opcode::MakeList, 0, 0);
    fn2.code.emplace_back(Opcode::StoreLocal, 0);
    fn2.code.emplace_back(Opcode::LoadLocal, 0);
    fn2.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn2.code.emplace_back(Opcode::BuiltinAppend);
    fn2.code.emplace_back(Opcode::StoreLocal, 0);
    fn2.code.emplace_back(Opcode::LoadLocal, 0);
    fn2.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
    fn2.code.emplace_back(Opcode::BuiltinAppend);
    fn2.code.emplace_back(Opcode::StoreLocal, 0);
    fn2.code.emplace_back(Opcode::LoadLocal, 0);
    fn2.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn2.code.emplace_back(Opcode::LoadIndex);
    fn2.code.emplace_back(Opcode::Return, 1);
    program2.entry = 0;
    program2.funcIndex["main"] = 0;
    auto r2 = runProgram(program2);
    require(r2.ok(), "BuiltinAppend access ok");
    requireInt(r2.value, 20, "appended[1] == 20");
}

void testBuiltinDelete()
{
    // Build {"a": 1, "b": 2}, delete "a", return len -> 1
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 4;
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
    // delete "a": push dict, push key, BuiltinDelete
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("a", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::BuiltinDelete);
    // return len(dict)
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::BuiltinLen);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "BuiltinDelete ok");
    requireInt(r.value, 1, "delete reduces len to 1");
}

void testBuiltinClear()
{
    // Build [1,2,3], clear, return len -> 0
    {
        Program program;
        Function& fn = program.functions.emplace_back("main");
        fn.params = 0;
        fn.results = 1;
        fn.locals = 1;
        fn.maxStack = 4;
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 2);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
        fn.code.emplace_back(Opcode::MakeList, 0, 3);
        fn.code.emplace_back(Opcode::StoreLocal, 0);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::BuiltinClear);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::BuiltinLen);
        fn.code.emplace_back(Opcode::Return, 1);
        program.entry = 0;
        program.funcIndex["main"] = 0;
        auto r = runProgram(program);
        require(r.ok(), "BuiltinClear list ok");
        requireInt(r.value, 0, "clear list -> len 0");
    }
    // dict clear
    {
        Program program;
        Function& fn = program.functions.emplace_back("main");
        fn.params = 0;
        fn.results = 1;
        fn.locals = 1;
        fn.maxStack = 4;
        fn.code.emplace_back(Opcode::LoadString,
            0,
            0,
            0,
            arp::core::pmr::String("a", &arp::core::defaultCpuMemoryResource()));
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
        fn.code.emplace_back(Opcode::MakeDict, 0, 1);
        fn.code.emplace_back(Opcode::StoreLocal, 0);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::BuiltinClear);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::BuiltinLen);
        fn.code.emplace_back(Opcode::Return, 1);
        program.entry = 0;
        program.funcIndex["main"] = 0;
        auto r = runProgram(program);
        require(r.ok(), "BuiltinClear dict ok");
        requireInt(r.value, 0, "clear dict -> len 0");
    }
}

void testBuiltinLenString()
{
    // len("hello") == 5
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("hello", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::BuiltinLen);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    require(r.ok(), "BuiltinLen string ok");
    requireInt(r.value, 5, "len('hello') == 5");
}

// --- Conversions: ConvertFloat, ConvertString, ConvertBool, ConvertInt64 ---

void testConversions()
{
    // int -> float: ConvertFloat(3) -> 3.0
    {
        SingleFnProgram p("main", 0, 1);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
        p.fn.code.emplace_back(Opcode::ConvertFloat);
        auto r = p.run();
        require(r.ok(), "ConvertFloat ok");
        requireFloat(r.value, 3.0, "ConvertFloat(3) == 3.0");
    }
    // int -> string: ConvertString(42) -> "42"
    {
        SingleFnProgram p("main", 0, 1);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 42);
        p.fn.code.emplace_back(Opcode::ConvertString);
        auto r = p.run();
        require(r.ok(), "ConvertString ok");
        requireString(r.value, "42", "ConvertString(42) == '42'");
    }
    // int -> bool: ConvertBool(1) -> true; ConvertBool(0) -> false
    {
        SingleFnProgram p("main", 0, 1);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
        p.fn.code.emplace_back(Opcode::ConvertBool);
        auto r = p.run();
        require(r.ok(), "ConvertBool true ok");
        requireBool(r.value, true, "ConvertBool(1) == true");
    }
    {
        SingleFnProgram p("main", 0, 1);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
        p.fn.code.emplace_back(Opcode::ConvertBool);
        auto r = p.run();
        require(r.ok(), "ConvertBool false ok");
        requireBool(r.value, false, "ConvertBool(0) == false");
    }
    // nil -> bool: ConvertBool(nil) -> false
    {
        SingleFnProgram p("main", 0, 1);
        p.fn.code.emplace_back(Opcode::LoadNil);
        p.fn.code.emplace_back(Opcode::ConvertBool);
        auto r = p.run();
        require(r.ok(), "ConvertBool nil ok");
        requireBool(r.value, false, "ConvertBool(nil) == false");
    }
    // int -> int64: ConvertInt64(100) -> int64 100
    {
        SingleFnProgram p("main", 0, 1);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 100);
        p.fn.code.emplace_back(Opcode::ConvertInt64);
        auto r = p.run();
        require(r.ok(), "ConvertInt64 ok");
        requireInt64(r.value, 100, "ConvertInt64(100) == 100");
    }
}

// --- Pointer ops: AddrLocal + Deref + StoreIndirect ---

void testPointerDerefStoreIndirect()
{
    // var x = 10; ptr = &x; *ptr = 99; return x -> 99
    // After AddrLocal, the slot holds a ScriptPointer. We need Deref to read it.
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 2;
    fn.maxStack = 2;
    // slot 0 = x: x = 10
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // slot 1 = ptr: ptr = &x  (AddrLocal promotes slot 0 into a cell, slot 0 becomes Pointer)
    fn.code.emplace_back(Opcode::AddrLocal, 0);
    fn.code.emplace_back(Opcode::StoreLocal, 1);
    // *ptr = 99: push ptr, push 99, StoreIndirect
    fn.code.emplace_back(Opcode::LoadLocal, 1);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    fn.code.emplace_back(Opcode::StoreIndirect);
    // return *ptr (or x via Deref): LoadLocal 1, Deref, Return
    fn.code.emplace_back(Opcode::LoadLocal, 1);
    fn.code.emplace_back(Opcode::Deref);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "pointer Deref/StoreIndirect ok");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 99, "indirect store/read == 99");
}

// --- AddrCapture: take pointer to a captured cell ---

void testAddrCaptureDeref()
{
    // Closure that captures a cell, then reads it via AddrCapture + Deref.
    // func inner(captures[0]): AddrCapture 0; Deref; Return 1
    // func outer: var x = 7; AddrLocal 0; MakeClosure 0,1; StoreLocal 1;
    //             LoadLocal 1; CallValue 0,0; Return 1
    Program program;

    Function& inner = program.functions.emplace_back("inner");
    inner.params = 0;
    inner.results = 1;
    inner.locals = 0;
    inner.maxStack = 1;
    inner.code.emplace_back(Opcode::AddrCapture, 0);
    inner.code.emplace_back(Opcode::Deref);
    inner.code.emplace_back(Opcode::Return, 1);

    Function& outer = program.functions.emplace_back("outer");
    outer.params = 0;
    outer.results = 1;
    outer.locals = 2;
    outer.maxStack = 2;
    outer.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    outer.code.emplace_back(Opcode::StoreLocal, 0);
    outer.code.emplace_back(Opcode::AddrLocal, 0);
    outer.code.emplace_back(Opcode::MakeClosure, 0, 1);
    outer.code.emplace_back(Opcode::StoreLocal, 1);
    outer.code.emplace_back(Opcode::LoadLocal, 1);
    outer.code.emplace_back(Opcode::CallValue, 0, 0);
    outer.code.emplace_back(Opcode::Return, 1);

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

    auto r = runProgram(program);
    require(r.ok(), "AddrCapture Deref ok");
    requireInt(r.value, 7, "AddrCapture+Deref reads captured cell == 7");
}

// --- MakeFuncValue: create a function value without captures ---

void testMakeFuncValue()
{
    // main: LoadInt 41 (arg); MakeFuncValue 0 (func index 0 = inc);
    //       CallValue 0,1(arg=41); Return 1
    // Note: CallValue pops callee from TOP, then args below it.
    // So args must be pushed BEFORE the callee.
    // func 0: inc(x): LoadLocal 0; LoadInt 1; Add; Return 1
    Program program;

    Function& inc = program.functions.emplace_back("inc");
    inc.params = 1;
    inc.results = 1;
    inc.locals = 1;
    inc.maxStack = 2;
    inc.code.emplace_back(Opcode::LoadLocal, 0);
    inc.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    inc.code.emplace_back(Opcode::Add);
    inc.code.emplace_back(Opcode::Return, 1);

    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 2;
    // Push arg first, then callee on top
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 41);
    mainFn.code.emplace_back(Opcode::MakeFuncValue, 0);
    mainFn.code.emplace_back(Opcode::CallValue, 0, 1);
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 1;
    program.funcIndex["inc"] = 0;
    program.funcIndex["main"] = 1;

    auto r = runProgram(program);
    require(r.ok(), "MakeFuncValue ok");
    if (!r.ok()) {
        std::printf(
            "  error: %s (code %d)\n", r.error.message.c_str(), static_cast<int>(r.error.code));
    }
    requireInt(r.value, 42, "MakeFuncValue + call == 42");
}

// --- MakeRangeFromStack: 1-arg and 2-arg forms ---

void testMakeRangeFromStack()
{
    // range(5) -> [0,1,2,3,4], len == 5
    {
        Program program;
        Function& fn = program.functions.emplace_back("main");
        fn.params = 0;
        fn.results = 1;
        fn.locals = 1;
        fn.maxStack = 2;
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
        fn.code.emplace_back(Opcode::MakeRangeFromStack, 0, 1);
        fn.code.emplace_back(Opcode::StoreLocal, 0);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::BuiltinLen);
        fn.code.emplace_back(Opcode::Return, 1);
        program.entry = 0;
        program.funcIndex["main"] = 0;
        auto r = runProgram(program);
        require(r.ok(), "MakeRangeFromStack 1-arg ok");
        requireInt(r.value, 5, "range(5) len == 5");
    }
    // range(2, 6) -> [2,3,4,5], len == 4
    {
        Program program;
        Function& fn = program.functions.emplace_back("main");
        fn.params = 0;
        fn.results = 1;
        fn.locals = 1;
        fn.maxStack = 3;
        // push start=2, push end=6 (end popped first, then start)
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 2);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 6);
        fn.code.emplace_back(Opcode::MakeRangeFromStack, 0, 2);
        fn.code.emplace_back(Opcode::StoreLocal, 0);
        // return list[0] + list[3] -> 2 + 5 = 7
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
        fn.code.emplace_back(Opcode::LoadIndex);
        fn.code.emplace_back(Opcode::LoadLocal, 0);
        fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
        fn.code.emplace_back(Opcode::LoadIndex);
        fn.code.emplace_back(Opcode::Add);
        fn.code.emplace_back(Opcode::Return, 1);
        program.entry = 0;
        program.funcIndex["main"] = 0;
        auto r = runProgram(program);
        require(r.ok(), "MakeRangeFromStack 2-arg ok");
        requireInt(r.value, 7, "range(2,6)[0]+[3] == 7");
    }
}

// --- MakeMethodValue: bind a method to a script instance ---

void testMakeMethodValue()
{
    registerScriptObjectReflection();

    constexpr const char* className = "ScriptVmCoverageMethodValue";
    constexpr const char* fieldName = "value";
    auto* cls = arp::reflection::db::instance().find_class(className);
    if (cls == nullptr) {
        cls = arp::reflection::db::instance().register_class(
            className, arp::reflection::instance_t, 0, 0, nullptr);
    }
    require(cls != nullptr, "method value class registration");

    Program program;
    program.classSchemas[arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource())]
        .push_back(arp::core::pmr::String(fieldName, &arp::core::defaultCpuMemoryResource()));

    // func 0: getValue(self) -> int: LoadLocal 0; LoadField "value"; Return 1
    Function& getValue = program.functions.emplace_back("getValue");
    getValue.params = 1;
    getValue.results = 1;
    getValue.locals = 1;
    getValue.maxStack = 1;
    getValue.code.emplace_back(Opcode::LoadLocal, 0);
    getValue.code.emplace_back(Opcode::LoadField,
        0,
        0,
        0,
        arp::core::pmr::String("value", &arp::core::defaultCpuMemoryResource()));
    getValue.code.emplace_back(Opcode::Return, 1);

    // func 1: main:
    //   MakeInstance className, 0  -> instance
    //   StoreLocal 0               -> slot 0 = instance
    //   LoadField "value"          -> would bind ScriptMethod; but we use MakeMethodValue directly:
    //   LoadLocal 0; LoadInt 99; StoreField "value"  -> set value=99
    //   LoadLocal 0; MakeMethodValue 0               -> bound method
    //   CallValue 0,0                                -> call it -> 99
    //   Return 1
    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 1;
    mainFn.maxStack = 2;
    mainFn.code.emplace_back(Opcode::MakeInstance,
        0,
        0,
        0,
        arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource()));
    mainFn.code.emplace_back(Opcode::StoreLocal, 0);
    // Set value = 99
    mainFn.code.emplace_back(Opcode::LoadLocal, 0);
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    mainFn.code.emplace_back(Opcode::StoreField,
        0,
        0,
        0,
        arp::core::pmr::String("value", &arp::core::defaultCpuMemoryResource()));
    // MakeMethodValue: pop receiver, push ScriptMethod
    mainFn.code.emplace_back(Opcode::LoadLocal, 0);
    mainFn.code.emplace_back(Opcode::MakeMethodValue, 0);
    mainFn.code.emplace_back(Opcode::CallValue, 0, 0);
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 1;
    program.funcIndex["getValue"] = 0;
    program.funcIndex["main"] = 1;
    program.methodSets[arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource())]
                      [arp::core::pmr::String("getValue", &arp::core::defaultCpuMemoryResource())] =
        0;

    auto r = runProgram(program);
    require(r.ok(), "MakeMethodValue ok");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 99, "MakeMethodValue bound call == 99");
}

// --- StoreField on script instance ---

void testStoreFieldScriptInstance()
{
    registerScriptObjectReflection();

    constexpr const char* className = "ScriptVmCoverageStoreField";
    auto* cls = arp::reflection::db::instance().find_class(className);
    if (cls == nullptr) {
        cls = arp::reflection::db::instance().register_class(
            className, arp::reflection::instance_t, 0, 0, nullptr);
    }
    require(cls != nullptr, "store field class registration");

    Program program;
    program.classSchemas[arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource())]
        .push_back(arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()));

    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 1;
    mainFn.maxStack = 2;
    // instance = MakeInstance
    mainFn.code.emplace_back(Opcode::MakeInstance,
        0,
        0,
        0,
        arp::core::pmr::String(className, &arp::core::defaultCpuMemoryResource()));
    mainFn.code.emplace_back(Opcode::StoreLocal, 0);
    // instance.x = 123
    mainFn.code.emplace_back(Opcode::LoadLocal, 0);
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 123);
    mainFn.code.emplace_back(Opcode::StoreField,
        0,
        0,
        0,
        arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()));
    // return instance.x
    mainFn.code.emplace_back(Opcode::LoadLocal, 0);
    mainFn.code.emplace_back(Opcode::LoadField,
        0,
        0,
        0,
        arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()));
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "StoreField script instance ok");
    requireInt(r.value, 123, "instance.x store/load == 123");
}

// --- Fast-path: LocalSubIntConst, LocalAddLocal, JumpIfLocalIntLtConstFalse ---

void testFastPathSubIntConst()
{
    // var x = 10; x -= 3 (LocalSubIntConst); return x -> 7
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    fn.code.emplace_back(Opcode::LocalSubIntConst, 0, 0, 3);
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    require(r.ok(), "LocalSubIntConst ok");
    requireInt(r.value, 7, "10-3(fast) == 7");
}

void testFastPathLocalAddLocal()
{
    // var a = 5; var b = 3; a += b (LocalAddLocal); return a -> 8
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 2;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::StoreLocal, 1);
    fn.code.emplace_back(Opcode::LocalAddLocal, 0, 1);
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    require(r.ok(), "LocalAddLocal ok");
    requireInt(r.value, 8, "5+3(local) == 8");
}

void testFastPathJumpIfLocalIntLtConstFalse()
{
    // var i = 0; loop: i += 1; if NOT(i < 5): exit; goto loop; return i -> 5
    // JumpIfLocalIntLtConstFalse jumps to target when NOT(i < 5), i.e. i >= 5.
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 1;
    fn.maxStack = 1;
    // i = 0
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0); // pc 0
    fn.code.emplace_back(Opcode::StoreLocal, 0);    // pc 1
    // loop start (pc 2): i += 1
    fn.code.emplace_back(Opcode::LocalAddIntConst, 0, 0, 1); // pc 2
    // if NOT(i < 5): jump to exit (pc 5)
    fn.code.emplace_back(
        Opcode::JumpIfLocalIntLtConstFalse, 5, 0, 5); // pc 3: target=5, slot=0, const=5
    // jump back to loop (pc 2)
    fn.code.emplace_back(Opcode::Jump, 2); // pc 4
    // exit (pc 5): return i
    fn.code.emplace_back(Opcode::LoadLocal, 0); // pc 5
    fn.code.emplace_back(Opcode::Return, 1);    // pc 6
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "JumpIfLocalIntLtConstFalse ok");
    requireInt(r.value, 5, "fast-path loop exits at i==5");
}

// --- Dict iteration via RangeNext ---

void testDictIteration()
{
    // Build {"a": 1, "b": 2, "c": 3}, iterate, sum values -> 6
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 4;
    fn.maxStack = 4;
    // slots: 0=dict, 1=index, 2=sum, 3=value(temp)
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
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("c", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    fn.code.emplace_back(Opcode::MakeDict, 0, 3);
    fn.code.emplace_back(Opcode::StoreLocal, 0);
    // sum = 0
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    fn.code.emplace_back(Opcode::StoreLocal, 2);
    // RangeInit source=0, index=1
    fn.code.emplace_back(Opcode::LoadLocal, 0);
    fn.code.emplace_back(Opcode::RangeInit, 0, 1);
    // loop (pc): RangeNext source=0 index=1, packed(keySlot=-1, valueSlot=3)
    // keySlot = -1 means no key slot; use a negative value that unpacks to -1
    fn.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, 3));
    // JumpIfFalse to end
    int loopNextPc = static_cast<int>(fn.code.size()) - 1;
    int jumpIfFalsePc = static_cast<int>(fn.code.size());
    fn.code.emplace_back(Opcode::JumpIfFalse, 0); // target patched below
    // sum = sum + value (slot 3)
    fn.code.emplace_back(Opcode::LoadLocal, 2);
    fn.code.emplace_back(Opcode::LoadLocal, 3);
    fn.code.emplace_back(Opcode::Add);
    fn.code.emplace_back(Opcode::StoreLocal, 2);
    // jump back to RangeNext
    int jumpBackPc = static_cast<int>(fn.code.size());
    fn.code.emplace_back(Opcode::Jump, loopNextPc);
    // end: return sum
    int endPc = static_cast<int>(fn.code.size());
    fn.code[jumpIfFalsePc] = Instruction(Opcode::JumpIfFalse, endPc);
    fn.code.emplace_back(Opcode::LoadLocal, 2);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;

    auto r = runProgram(program);
    require(r.ok(), "dict iteration ok");
    if (!r.ok()) {
        std::printf("  error: %s\n", r.error.message.c_str());
    }
    requireInt(r.value, 6, "dict iteration sum == 6");
}

// --- Error paths ---

void testDivisionByZero()
{
    // 10 / 0 -> DivisionByZero
    SingleFnProgram p("main", 0, 2);
    p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    p.fn.code.emplace_back(Opcode::Div);
    // Don't use SingleFnProgram::run() since it appends Return; build manually.
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    fn.code.emplace_back(Opcode::Div);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    requireError(r, ScriptDiagnosticCode::DivisionByZero, "Div by zero");
}

void testModuloByZero()
{
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    fn.code.emplace_back(Opcode::Mod);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    requireError(r, ScriptDiagnosticCode::DivisionByZero, "Mod by zero");
}

void testIndexOutOfBounds()
{
    // [10, 20][5] -> IndexError
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 3;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
    fn.code.emplace_back(Opcode::MakeList, 0, 2);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
    fn.code.emplace_back(Opcode::LoadIndex);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    requireError(r, ScriptDiagnosticCode::IndexError, "list index out of bounds");
}

void testNegativeIndex()
{
    // [10, 20][-1] -> IndexError
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 3;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 10);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 20);
    fn.code.emplace_back(Opcode::MakeList, 0, 2);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, -1);
    fn.code.emplace_back(Opcode::LoadIndex);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    requireError(r, ScriptDiagnosticCode::IndexError, "negative index");
}

void testDictKeyMissingReturnsNil()
{
    // {"a": 1}["b"] -> nil (not an error; dict returns nil for missing keys)
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 3;
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("a", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    fn.code.emplace_back(Opcode::MakeDict, 0, 1);
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("b", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::LoadIndex);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    require(r.ok(), "dict missing key ok");
    requireNil(r.value, "dict missing key -> nil");
}

void testNotIndexable()
{
    // 42[0] -> NotIndexable
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 3;
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 42);
    fn.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    fn.code.emplace_back(Opcode::LoadIndex);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    requireError(r, ScriptDiagnosticCode::NotIndexable, "int not indexable");
}

void testBadCastConvert()
{
    // ConvertInt("hello") -> BadCast
    Program program;
    Function& fn = program.functions.emplace_back("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 1;
    fn.code.emplace_back(Opcode::LoadString,
        0,
        0,
        0,
        arp::core::pmr::String("hello", &arp::core::defaultCpuMemoryResource()));
    fn.code.emplace_back(Opcode::ConvertInt);
    fn.code.emplace_back(Opcode::Return, 1);
    program.entry = 0;
    program.funcIndex["main"] = 0;
    auto r = runProgram(program);
    requireError(r, ScriptDiagnosticCode::BadCast, "ConvertInt(string) -> BadCast");
}

void testNilComparison()
{
    // nil == nil -> true; nil != nil -> false; nil == 5 -> false
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadNil);
        p.fn.code.emplace_back(Opcode::LoadNil);
        p.fn.code.emplace_back(Opcode::CmpEq);
        auto r = p.run();
        require(r.ok(), "nil==nil ok");
        requireBool(r.value, true, "nil == nil -> true");
    }
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadNil);
        p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 5);
        p.fn.code.emplace_back(Opcode::CmpEq);
        auto r = p.run();
        require(r.ok(), "nil==5 ok");
        requireBool(r.value, false, "nil == 5 -> false");
    }
}

void testFloatArithmetic()
{
    // 3.5 + 2.5 = 6.0; 3.5 * 2.0 = 7.0; 10.0 / 4.0 = 2.5
    std::int64_t bits35 = 0, bits25 = 0, bits20 = 0, bits100 = 0, bits40 = 0;
    double v35 = 3.5, v25 = 2.5, v20 = 2.0, v100 = 10.0, v40 = 4.0;
    std::memcpy(&bits35, &v35, 8);
    std::memcpy(&bits25, &v25, 8);
    std::memcpy(&bits20, &v20, 8);
    std::memcpy(&bits100, &v100, 8);
    std::memcpy(&bits40, &v40, 8);

    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadFloat, 0, 0, bits35);
        p.fn.code.emplace_back(Opcode::LoadFloat, 0, 0, bits25);
        p.fn.code.emplace_back(Opcode::Add);
        auto r = p.run();
        require(r.ok(), "float add ok");
        requireFloat(r.value, 6.0, "3.5+2.5 == 6.0");
    }
    {
        SingleFnProgram p("main", 0, 2);
        p.fn.code.emplace_back(Opcode::LoadFloat, 0, 0, bits100);
        p.fn.code.emplace_back(Opcode::LoadFloat, 0, 0, bits40);
        p.fn.code.emplace_back(Opcode::Div);
        auto r = p.run();
        require(r.ok(), "float div ok");
        requireFloat(r.value, 2.5, "10.0/4.0 == 2.5");
    }
}

void testInt64Arithmetic()
{
    // 5000000000 + 1000000000 = 6000000000
    SingleFnProgram p("main", 0, 2);
    p.fn.code.emplace_back(Opcode::LoadInt64, 0, 0, 5000000000LL);
    p.fn.code.emplace_back(Opcode::LoadInt64, 0, 0, 1000000000LL);
    p.fn.code.emplace_back(Opcode::Add);
    auto r = p.run();
    require(r.ok(), "int64 add ok");
    requireInt64(r.value, 6000000000LL, "5B+1B == 6B");
}

void testMixedNumericWidening()
{
    // int 3 + float 2.5 = 5.5 (widening)
    std::int64_t bits25 = 0;
    double v25 = 2.5;
    std::memcpy(&bits25, &v25, 8);

    SingleFnProgram p("main", 0, 2);
    p.fn.code.emplace_back(Opcode::LoadInt, 0, 0, 3);
    p.fn.code.emplace_back(Opcode::LoadFloat, 0, 0, bits25);
    p.fn.code.emplace_back(Opcode::Add);
    auto r = p.run();
    require(r.ok(), "mixed numeric ok");
    requireFloat(r.value, 5.5, "3 + 2.5 == 5.5");
}

} // namespace

int main()
{
    registerScriptObjectReflection();

    // Literal loads
    testLoadNil();
    testLoadBool();
    testLoadFloat();
    testLoadInt64();

    // Arithmetic
    testDivAndMod();
    testNeg();
    testNot();

    // Bitwise
    testBitwiseAll();

    // Comparison
    testCmpLeAndNe();

    // Jumps
    testJumpIfTrue();

    // Globals
    testGlobals();
    testGlobalsCrossFunction();

    // Container mutation
    testStoreIndexList();
    testStoreIndexDict();
    testBuiltinAppend();
    testBuiltinDelete();
    testBuiltinClear();
    testBuiltinLenString();

    // Conversions
    testConversions();

    // Pointer ops
    testPointerDerefStoreIndirect();
    testAddrCaptureDeref();

    // Function values
    testMakeFuncValue();
    testMakeRangeFromStack();
    testMakeMethodValue();

    // StoreField on script instance
    testStoreFieldScriptInstance();

    // Fast-path
    testFastPathSubIntConst();
    testFastPathLocalAddLocal();
    testFastPathJumpIfLocalIntLtConstFalse();

    // Dict iteration
    testDictIteration();

    // Error paths
    testDivisionByZero();
    testModuloByZero();
    testIndexOutOfBounds();
    testNegativeIndex();
    testDictKeyMissingReturnsNil();
    testNotIndexable();
    testBadCastConvert();

    // Nil semantics
    testNilComparison();

    // Numeric type coverage
    testFloatArithmetic();
    testInt64Arithmetic();
    testMixedNumericWidening();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript VM coverage tests passed\n");
    return 0;
}
