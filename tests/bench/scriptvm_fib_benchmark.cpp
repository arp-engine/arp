// ArpScript vs Python fibonacci performance benchmark.
//
// Measures: ArpScript recursive fib(N) vs the same algorithm in Python.
// The fib function is called N times in a loop to measure
// both per-call overhead and raw arithmetic/recursion throughput.
//
// Usage: arp_scriptvm_fib_benchmark [fib_n] [iterations]
//   fib_n       -- the fibonacci index (default 25, fib(25)=75025)
//   iterations  -- how many times to call fib(fib_n) (default 100000)

#include "scriptvm/compile/lexer.h"
#include "scriptvm/compile/parser.h"
#include "scriptvm/compile/sema.h"
#include "scriptvm/interpreter/eval.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace arp::scriptvm;

int main(int argc, char** argv)
{
    std::fprintf(stderr, "START\n");
    std::fflush(stderr);

    int fibN = 25;
    int iterations = 100000;
    if (argc > 1) {
        fibN = std::atoi(argv[1]);
    }
    if (argc > 2) {
        iterations = std::atoi(argv[2]);
    }

    if (fibN < 0 || iterations < 0) {
        std::fprintf(stderr, "fib_n and iterations must be non-negative\n");
        return 1;
    }

    std::fprintf(stderr, "fibN=%d iterations=%d\n", fibN, iterations);
    std::fflush(stderr);

    std::printf("=== ArpScript vs Python Fibonacci Benchmark ===\n");
    std::printf("fib(%d) called %d times\n", fibN, iterations);
    std::printf("\n");
    std::fflush(stdout);

    // Build source string with fibN substituted
    char src[512];
    std::snprintf(src,
        sizeof(src),
        "def fib(n: int) -> int:\n"
        "    if n < 2:\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "def bench(n: int) -> int:\n"
        "    var i: int = 0\n"
        "    var total: int = 0\n"
        "    while i < n:\n"
        "        total = total + fib(%d)\n"
        "        i = i + 1\n"
        "    return total\n",
        fibN);

    auto& res = arp::core::defaultCpuMemoryResource();
    auto cr = arp::scriptvm::test::buildInterpreter(src, "fib_bench.arp", "fib-benchmark");
    if (!cr.ok()) {
        std::printf("compile error: %s\n", cr.error.message.c_str());
        return 1;
    }
    Program program = std::move(cr.program);

    // Warm up (1 call)
    {
        arp::core::pmr::Vector<Value> args((arp::core::pmr::Vector<Value>::allocator_type(&res)));
        args.push_back(makeInt(1));
        Vm vm;
        VmError err;
        Value warm = vm.runFunctionByName(program, "bench", args, &err);
        if (err.code != ScriptDiagnosticCode::None) {
            std::printf(
                "ArpScript warmup error: %s\n%s", err.message.c_str(), err.traceback.c_str());
            return 1;
        }
        int warmResult = 0;
        if (!tryGetInt(warm, warmResult)) {
            std::printf("ArpScript warmup error: result is not an int\n");
            return 1;
        }
        std::printf("ArpScript warmup: bench(1) = %d\n", warmResult);
    }

    // Benchmark: call bench(iterations) which loops internally
    arp::core::pmr::Vector<Value> args((arp::core::pmr::Vector<Value>::allocator_type(&res)));
    args.push_back(makeInt(iterations));
    Vm vm;
    VmError err;

    auto start = std::chrono::high_resolution_clock::now();
    Value result = vm.runFunctionByName(program, "bench", args, &err);
    auto end = std::chrono::high_resolution_clock::now();
    auto arpscriptMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (err.code != ScriptDiagnosticCode::None) {
        std::printf("ArpScript error: %s\n%s", err.message.c_str(), err.traceback.c_str());
        return 1;
    }
    int totalInt = 0;
    if (!tryGetInt(result, totalInt)) {
        std::printf("ArpScript error: result is not an integer\n");
        return 1;
    }
    std::printf("ArpScript: %d iterations, total=%lld, time=%lldms\n",
        iterations,
        static_cast<long long>(totalInt),
        (long long)arpscriptMs);

    // --- Print Python comparison instructions ---
    std::printf("\n--- Python comparison ---\n");
    std::printf("Run this Python script for comparison:\n\n");
    std::printf("import time\n");
    std::printf("def fib(n):\n");
    std::printf("    if n < 2:\n");
    std::printf("        return n\n");
    std::printf("    return fib(n - 1) + fib(n - 2)\n");
    std::printf("def bench(n):\n");
    std::printf("    total = 0\n");
    std::printf("    for i in range(n):\n");
    std::printf("        total += fib(%d)\n", fibN);
    std::printf("    return total\n");
    std::printf("bench(1)  # warmup\n");
    std::printf("start = time.time()\n");
    std::printf("result = bench(%d)\n", iterations);
    std::printf("elapsed = (time.time() - start) * 1000\n");
    std::printf(
        "print(f'Python: %d iterations, total={result}, time={{elapsed:.0f}}ms')\n", iterations);
    std::printf("\n--- Summary ---\n");
    std::printf("ArpScript: %lldms for %d x fib(%d)\n", (long long)arpscriptMs, iterations, fibN);
    std::printf("Run the Python script above and compare.\n");

    return 0;
}
