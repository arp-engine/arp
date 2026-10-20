// ArpScript Sema smoke test (Phase 4).
//
// Runs semantic analysis on a realistic ArpScript source and verifies
// no type errors are found. Not named *_tests so it does not run under CTest.

#include "scriptvm/compile/parser.h"
#include "scriptvm/compile/sema.h"

#include <cstdio>

using namespace arp::scriptvm;

int main()
{
    const char* src = R"(
class Vec3:
    x: float
    y: float
    z: float
    def __init__(self, x: float, y: float, z: float):
        self.x = x
        self.y = y
        self.z = z
    def length(self) -> float:
        return 1.0

def make_counter(start: int) -> () -> int:
    var count: int = start
    def inc() -> int:
        count = count + 1
        return count
    return inc

def fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

var result: int = fib(10)
var pi: float = 3.14
var s: string = "hello"
var arr: [int] = [1, 2, 3]
var m: {string: int} = {"a": 1, "b": 2}

for i: int in range(10):
    result = result + i

while result > 0:
    result = result - 1
    if result == 50:
        break
    if result == 40:
        continue
)";

    arp::core::pmr::String source(src, &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("sample.arp", &arp::core::defaultCpuMemoryResource());
    auto pr = parseSource(std::move(source), std::move(fname));
    if (!pr.ok()) {
        std::printf("SMOKE FAIL: parse error: %s (line %d, col %d)\n",
            pr.error.message.c_str(),
            pr.error.line,
            pr.error.column);
        return 1;
    }
    auto sr = analyze(pr.module);
    if (!sr.ok()) {
        std::printf("SMOKE FAIL: sema error: %s (line %d, col %d)\n",
            sr.error.message.c_str(),
            sr.error.line,
            sr.error.column);
        return 1;
    }

    std::printf(
        "SMOKE OK: semantic analysis passed on %zu top-level decls\n", pr.module->decls.size());
    return 0;
}
