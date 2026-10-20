// ArpScript VM smoke test (Phase 1).
//
// Runs a heavier workload (fib(25)) to sanity-check VM stability and
// performance under deeper recursion. Intentionally NOT named *_tests so
// it does not run under CTest (per repo convention for smoke targets).

#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/runtime/value.h"

#include <cstdio>
#include <cstdlib>

using namespace arp::scriptvm;

static Program buildFibProgram()
{
    Program program;
    Function& fib = program.functions.emplace_back("fib");
    fib.params = 1;
    fib.results = 1;
    fib.locals = 1;
    fib.maxStack = 4;
    fib.code.emplace_back(Opcode::LoadLocal, 0);
    fib.code.emplace_back(Opcode::LoadInt, 0, 0, 2);
    fib.code.emplace_back(Opcode::CmpLt);
    // JumpIfFalse to pc=6 (the else branch: LoadLocal 0 for fib(n-1))
    fib.code.emplace_back(Opcode::JumpIfFalse, 6);
    // pc=4: return n
    fib.code.emplace_back(Opcode::LoadLocal, 0);
    fib.code.emplace_back(Opcode::Return, 1);
    // pc=6: else: fib(n-1) + fib(n-2)
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

    Function& mainFn = program.functions.emplace_back("main");
    mainFn.params = 0;
    mainFn.results = 1;
    mainFn.locals = 0;
    mainFn.maxStack = 2;
    mainFn.code.emplace_back(Opcode::LoadInt, 0, 0, 25);
    mainFn.code.emplace_back(Opcode::Call, 0, 1);
    mainFn.code.emplace_back(Opcode::Return, 1);

    program.entry = 1;
    program.funcIndex["fib"] = 0;
    program.funcIndex["main"] = 1;
    return program;
}

int main()
{
    auto program = buildFibProgram();
    auto result = runProgram(program);
    if (!result.ok()) {
        std::printf("SMOKE FAIL: %s\n", result.error.message.c_str());
        std::printf("traceback:\n%s\n", result.error.traceback.c_str());
        return 1;
    }
    int v = 0;
    if (!tryGetInt(result.value, v) || v != 75025) {
        std::printf("SMOKE FAIL: fib(25) = %d (expected 75025)\n", v);
        return 1;
    }
    std::printf("SMOKE OK: fib(25) = 75025\n");
    return 0;
}
