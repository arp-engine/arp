// ArpScript Compiler smoke test (Phase 5).
//
// Runs the full pipeline (lex -> parse -> sema -> compile -> VM run) on a
// realistic ArpScript source and verifies execution produces correct results.
// Not named *_tests so it does not run under CTest.

#include "scriptvm/interpreter/eval.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cstdio>

using namespace arp::scriptvm;

int main()
{
    const char* src = R"(
def fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

def sum_range(start: int, end: int) -> int:
    var total: int = 0
    var i: int = start
    while i < end:
        total = total + i
        i = i + 1
    return total

def count_evens(n: int) -> int:
    var count: int = 0
    for i: int in range(n):
        if i % 2 == 0:
            count = count + 1
    return count

def test() -> int:
    var f: int = fib(12)
    var s: int = sum_range(1, 11)
    var c: int = count_evens(20)
    return f + s + c
)";

    auto cr =
        arp::scriptvm::test::buildInterpreter(src, "sample.arp", "interpreter-pipeline-smoke");
    if (!cr.ok()) {
        std::printf("SMOKE FAIL: compile error: %s (line %d, col %d)\n",
            cr.error.message.c_str(),
            cr.error.line,
            cr.error.column);
        return 1;
    }
    std::printf(
        "SMOKE: compiled %zu functions, entry=%d\n", cr.program.functions.size(), cr.program.entry);

    // Print the compiled bytecode for the main test function.
    auto it = cr.program.funcIndex.find("test");
    if (it != cr.program.funcIndex.end()) {
        const auto& fn = cr.program.functions[it->second];
        std::printf("SMOKE: test() function: params=%d locals=%d maxStack=%d code=%zu\n",
            fn.params,
            fn.locals,
            fn.maxStack,
            fn.code.size());
    }

    // Run the test function.
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    auto result = runFunctionByName(cr.program, "test", args);
    if (!result.ok()) {
        std::printf("SMOKE FAIL: VM error: %s\n", result.error.message.c_str());
        return 1;
    }

    int val = 0;
    if (!tryGetInt(result.value, val)) {
        std::printf("SMOKE FAIL: result is not an int\n");
        return 1;
    }

    // fib(12) = 144, sum_range(1,11) = 1+2+...+10 = 55, count_evens(20) = 10
    // Expected: 144 + 55 + 10 = 209
    int expected = 144 + 55 + 10;
    if (val != expected) {
        std::printf("SMOKE FAIL: result = %d, expected %d\n", val, expected);
        return 1;
    }

    std::printf("SMOKE OK: result = %d (fib(12)=144 + sum(1..10)=55 + evens(20)=10)\n", val);
    return 0;
}
