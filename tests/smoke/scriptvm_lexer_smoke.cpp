// ArpScript Lexer smoke test (Phase 2).
//
// Tokenizes a realistic ArpScript source file (covering class, def, control
// flow, closures, expressions) and verifies the token stream is well-formed.
// Not named *_tests so it does not run under CTest.

#include "scriptvm/compile/lexer.h"

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

for i in range(10):
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
    auto result = lex(std::move(source), std::move(fname));

    if (!result.ok()) {
        std::printf("SMOKE FAIL: lex error: %s (line %d, col %d)\n",
            result.error.message.c_str(),
            result.error.line,
            result.error.column);
        return 1;
    }

    // Count token types for a sanity check.
    int counts[static_cast<int>(Tok::TokCount)] = {};
    for (const auto& t : result.tokens) {
        counts[static_cast<int>(t.kind)]++;
    }

    int totalTokens = static_cast<int>(result.tokens.size());

    // Verify we got a reasonable token stream.
    // The sample has:
    //   - Multiple INDENT/DEDENT pairs (class body, def bodies, for body, while body, if bodies)
    //   - String literals (at least 2: "hello world", "a", "b")
    //   - Int and Float literals
    //   - All major keyword types
    //   - Balanced INDENT/DEDENT (equal counts)

    int indents = counts[static_cast<int>(Tok::Indent)];
    int dedents = counts[static_cast<int>(Tok::Dedent)];
    int strings = counts[static_cast<int>(Tok::String)];
    int ints = counts[static_cast<int>(Tok::Int)];
    int floats = counts[static_cast<int>(Tok::Float)];
    int idents = counts[static_cast<int>(Tok::Ident)];
    int newlines = counts[static_cast<int>(Tok::Newline)];

    bool ok = true;

    if (indents != dedents) {
        std::printf("SMOKE FAIL: INDENT(%d) != DEDENT(%d) count\n", indents, dedents);
        ok = false;
    }
    if (indents < 5) {
        std::printf("SMOKE FAIL: expected at least 5 INDENT tokens, got %d\n", indents);
        ok = false;
    }
    if (strings < 3) {
        std::printf("SMOKE FAIL: expected at least 3 String tokens, got %d\n", strings);
        ok = false;
    }
    if (ints < 5) {
        std::printf("SMOKE FAIL: expected at least 5 Int tokens, got %d\n", ints);
        ok = false;
    }
    if (floats < 1) {
        std::printf("SMOKE FAIL: expected at least 1 Float token, got %d\n", floats);
        ok = false;
    }
    if (idents < 20) {
        std::printf("SMOKE FAIL: expected at least 20 Ident tokens, got %d\n", idents);
        ok = false;
    }
    if (newlines < 20) {
        std::printf("SMOKE FAIL: expected at least 20 Newline tokens, got %d\n", newlines);
        ok = false;
    }

    // Verify the last token is Eof.
    if (result.tokens.empty() || result.tokens.back().kind != Tok::Eof) {
        std::printf("SMOKE FAIL: last token is not Eof\n");
        ok = false;
    }

    if (!ok) {
        std::printf("SMOKE FAIL: token counts did not match expectations\n");
        return 1;
    }

    std::printf(
        "SMOKE OK: lexed %d tokens (INDENT=%d DEDENT=%d String=%d Int=%d Float=%d Ident=%d Newline=%d)\n",
        totalTokens,
        indents,
        dedents,
        strings,
        ints,
        floats,
        idents,
        newlines);
    return 0;
}
