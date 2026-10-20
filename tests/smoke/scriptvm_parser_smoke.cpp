// ArpScript Parser smoke test (Phase 3).
//
// Parses a realistic ArpScript source file and verifies the AST is
// well-formed. Not named *_tests so it does not run under CTest.

#include "scriptvm/compile/parser.h"

#include <cstdio>

using namespace arp::scriptvm;

int main()
{
    const char* src = R"(
# ArpScript sample: class + function + closure + control flow
var x: int = 42
const pi: float = 3.14
var s: string = "hello world"
var arr: [int] = [1, 2, 3]
var m: {string: int} = {"a": 1, "b": 2}

class Vec3:
    x: float
    y: float
    z: float
    def __init__(self, x: float, y: float, z: float):
        self.x = x
        self.y = y
        self.z = z
    def length(self) -> float:
        return (self.x * self.x + self.y * self.y + self.z * self.z) ** 0.5

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

for i: int in range(10):
    print(fib(i))

while x > 0:
    x = x - 1
    if x == 5:
        continue
    if x == 3:
        break
)";

    arp::core::pmr::String source(src, &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("sample.arp", &arp::core::defaultCpuMemoryResource());
    auto result = parseSource(std::move(source), std::move(fname));

    if (!result.ok()) {
        std::printf("SMOKE FAIL: parse error: %s (line %d, col %d)\n",
            result.error.message.c_str(),
            result.error.line,
            result.error.column);
        return 1;
    }

    if (!result.module) {
        std::printf("SMOKE FAIL: no module produced\n");
        return 1;
    }

    auto& decls = result.module->decls;

    // Count statement types.
    int varDecls = 0, funcDecls = 0, classDecls = 0, forStmts = 0, whileStmts = 0;
    for (const auto& d : decls) {
        switch (d->kind) {
        case StmtKind::VarDecl:
            ++varDecls;
            break;
        case StmtKind::FuncDecl:
            ++funcDecls;
            break;
        case StmtKind::ClassDecl:
            ++classDecls;
            break;
        case StmtKind::For:
            ++forStmts;
            break;
        case StmtKind::While:
            ++whileStmts;
            break;
        default:
            break;
        }
    }

    bool ok = true;
    if (varDecls < 5) {
        std::printf("SMOKE FAIL: expected >=5 var decls, got %d\n", varDecls);
        ok = false;
    }
    if (funcDecls < 2) {
        std::printf("SMOKE FAIL: expected >=2 func decls, got %d\n", funcDecls);
        ok = false;
    }
    if (classDecls < 1) {
        std::printf("SMOKE FAIL: expected >=1 class decl, got %d\n", classDecls);
        ok = false;
    }
    if (forStmts < 1) {
        std::printf("SMOKE FAIL: expected >=1 for stmt, got %d\n", forStmts);
        ok = false;
    }
    if (whileStmts < 1) {
        std::printf("SMOKE FAIL: expected >=1 while stmt, got %d\n", whileStmts);
        ok = false;
    }

    // Check the class has fields and methods.
    for (const auto& d : decls) {
        if (d->kind == StmtKind::ClassDecl) {
            if (d->fields.size() < 3) {
                std::printf("SMOKE FAIL: class has <3 fields, got %zu\n", d->fields.size());
                ok = false;
            }
            if (d->methods.size() < 2) {
                std::printf("SMOKE FAIL: class has <2 methods, got %zu\n", d->methods.size());
                ok = false;
            }
        }
    }

    if (!ok) {
        std::printf("SMOKE FAIL: AST structure did not match expectations\n");
        return 1;
    }

    std::printf("SMOKE OK: parsed %zu top-level decls (var=%d func=%d class=%d for=%d while=%d)\n",
        decls.size(),
        varDecls,
        funcDecls,
        classDecls,
        forStmts,
        whileStmts);
    return 0;
}
