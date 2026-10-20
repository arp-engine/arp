// ArpScript end-to-end smoke test (Phase 6).
//
// Runs the full pipeline on a realistic ArpScript source combining
// closures, loops, conditionals, function calls, and containers.
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

def make_counter(start: int) -> () -> int:
    var count: int = start
    def inc() -> int:
        count = count + 1
        return count
    return inc

def sum_range(start: int, end: int) -> int:
    var total: int = 0
    var i: int = start
    while i < end:
        total = total + i
        i = i + 1
    return total

def count_primes(limit: int) -> int:
    var count: int = 0
    for n: int in range(2, limit):
        var is_prime: bool = true
        var d: int = 2
        while d * d <= n:
            if n % d == 0:
                is_prime = false
            d = d + 1
        if is_prime:
            count = count + 1
    return count

def sum_all(base: int, *args: int) -> int:
    var total: int = base
    for value in args:
        total = total + value
    return total

def handled_error() -> int:
    var handled: int = 0
    try:
        var impossible: int = 10 / 0
    except:
        handled = 4
    return handled

def numbers() -> int:
    yield 1
    yield 2

def generator_total() -> int:
    var total: int = 0
    for value in numbers():
        total = total + value
    return total

def test() -> int:
    var f: int = fib(15)
    var converted: int = "7" as int
    var twice: (int) -> int = lambda(x: int) -> int: x * 2
    var lambdaValue: int = twice(3)
    var variadicValue: int = sum_all(1, 2, 3)
    var handledValue: int = handled_error()
    var generatorValue: int = generator_total()
    var pairSum: int = 0
    for index, value in [10, 20]:
        pairSum = pairSum + index + value
    var c: () -> int = make_counter(100)
    c()
    c()
    var cval: int = c()
    var s: int = sum_range(1, 101)
    var p: int = count_primes(50)
    return f + converted + lambdaValue + variadicValue + handledValue + generatorValue + pairSum + cval + s + p
)";

    auto cr = arp::scriptvm::test::buildInterpreter(src, "e2e.arp", "e2e-smoke");
    if (!cr.ok()) {
        std::printf("SMOKE FAIL: compile error: %s (line %d, col %d)\n",
            cr.error.message.c_str(),
            cr.error.line,
            cr.error.column);
        return 1;
    }
    std::printf(
        "SMOKE: compiled %zu functions, entry=%d\n", cr.program.functions.size(), cr.program.entry);

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

    // fib(15) = 610
    // counter(100): 3 calls -> 103
    // sum_range(1,101) = 1+2+...+100 = 5050
    // count_primes(50) = 15 (primes < 50: 2,3,5,7,11,13,17,19,23,29,31,37,41,43,47)
    // Expected: 610 + 7 + 6 + 6 + 4 + 3 + 31 + 103 + 5050 + 15 = 5835
    int expected = 610 + 7 + 6 + 6 + 4 + 3 + 31 + 103 + 5050 + 15;
    if (val != expected) {
        std::printf("SMOKE FAIL: result = %d, expected %d\n", val, expected);
        return 1;
    }

    std::printf("SMOKE OK: result = %d\n", val);
    return 0;
}
