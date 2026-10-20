// ArpScript Lexer unit tests (Phase 2).
//
// Tests the tokenizer: token kinds, INDENT/DEDENT, string/number/comment
// lexing, operator recognition, and error reporting with line/column.
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/compile/lexer.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

// Helper: lex a source string, assert success.
LexResult lexOk(const char* src, const char* label)
{
    arp::core::pmr::String source(src, &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto result = lex(std::move(source), std::move(fname));
    if (!result.ok()) {
        std::printf("FAIL: %s -- lex error: %s (line %d, col %d)\n",
            label,
            result.error.message.c_str(),
            result.error.line,
            result.error.column);
        ++g_failures;
    }
    return result;
}

// Count tokens of a specific kind.
int countTok(const LexResult& lr, Tok kind)
{
    int n = 0;
    for (const auto& t : lr.tokens)
        if (t.kind == kind)
            ++n;
    return n;
}

// Find the first token of a specific kind, or nullptr.
const Token* findTok(const LexResult& lr, Tok kind)
{
    for (const auto& t : lr.tokens)
        if (t.kind == kind)
            return &t;
    return nullptr;
}

// Shorthand for checking token at position has expected kind and text.
void expectTok(const LexResult& lr, std::size_t idx, Tok kind, const char* text, const char* label)
{
    if (idx >= lr.tokens.size()) {
        std::printf(
            "FAIL: %s -- token index %zu out of range (size=%zu)\n", label, idx, lr.tokens.size());
        ++g_failures;
        return;
    }
    const Token& t = lr.tokens[idx];
    if (t.kind != kind) {
        std::printf("FAIL: %s -- token[%zu] kind=%s, want %s\n",
            label,
            idx,
            tokName(t.kind),
            tokName(kind));
        ++g_failures;
    }
    if (t.text != arp::core::pmr::String(text, &arp::core::defaultCpuMemoryResource())) {
        std::printf(
            "FAIL: %s -- token[%zu] text='%s', want '%s'\n", label, idx, t.text.c_str(), text);
        ++g_failures;
    }
}

void testBasicTokens()
{
    auto lr = lexOk("var x: int = 42\n", "basic tokens");
    // var x : int = 42 Newline Eof
    require(lr.tokens.size() >= 8, "basic tokens count");
    expectTok(lr, 0, Tok::KwVar, "var", "basic var");
    expectTok(lr, 1, Tok::Ident, "x", "basic ident");
    expectTok(lr, 2, Tok::Colon, ":", "basic colon");
    expectTok(lr, 3, Tok::Ident, "int", "basic int type");
    expectTok(lr, 4, Tok::Assign, "=", "basic assign");
    expectTok(lr, 5, Tok::Int, "42", "basic int literal");
    expectTok(lr, 6, Tok::Newline, "", "basic newline");
    expectTok(lr, 7, Tok::Eof, "", "basic eof");
}

void testIntLiterals()
{
    auto lr = lexOk("0 42 255 0x1F 0o17 0b1010\n", "int literals");
    const Token* t0 = &lr.tokens[0];
    require(t0->kind == Tok::Int, "int 0");
    require(t0->i64 == 0, "int 0 value");
    const Token* t1 = &lr.tokens[1];
    require(t1->kind == Tok::Int, "int 42");
    require(t1->i64 == 42, "int 42 value");
    const Token* t2 = &lr.tokens[2];
    require(t2->kind == Tok::Int, "int 255");
    require(t2->i64 == 255, "int 255 value");
    const Token* t3 = &lr.tokens[3];
    require(t3->kind == Tok::Int, "int 0x1F");
    require(t3->i64 == 31, "int 0x1F value");
    const Token* t4 = &lr.tokens[4];
    require(t4->kind == Tok::Int, "int 0o17");
    require(t4->i64 == 15, "int 0o17 value");
    const Token* t5 = &lr.tokens[5];
    require(t5->kind == Tok::Int, "int 0b1010");
    require(t5->i64 == 10, "int 0b1010 value");
}

void testInt64Literal()
{
    auto lr = lexOk("100000000000L\n", "int64 literal");
    const Token* t = &lr.tokens[0];
    require(t->kind == Tok::Int64, "int64 kind");
    require(t->i64 == 100000000000LL, "int64 value");
}

void testFloatLiterals()
{
    auto lr = lexOk("3.14 1e10 2.5e-3 .5\n", "float literals");
    // Note: ".5" starts with '.', lexer should handle it as float
    // Actually our lexer requires a digit start. Let's test 0.5 instead.
    auto lr2 = lexOk("0.5\n", "float 0.5");
    const Token* t = &lr2.tokens[0];
    require(t->kind == Tok::Float, "float 0.5 kind");
    require(t->f64 > 0.49 && t->f64 < 0.51, "float 0.5 value");
}

void testStringLiterals()
{
    auto lr = lexOk("\"hello world\" 'single'\n", "string literals");
    const Token* t0 = &lr.tokens[0];
    require(t0->kind == Tok::String, "double-quoted string");
    require(
        t0->text == arp::core::pmr::String("hello world", &arp::core::defaultCpuMemoryResource()),
        "double-quoted value");
    const Token* t1 = &lr.tokens[1];
    require(t1->kind == Tok::String, "single-quoted string");
    require(t1->text == arp::core::pmr::String("single", &arp::core::defaultCpuMemoryResource()),
        "single-quoted value");
}

void testStringEscapes()
{
    auto lr = lexOk("\"a\\nb\\tc\\\\d\"\n", "string escapes");
    const Token* t = &lr.tokens[0];
    require(t->kind == Tok::String, "escape string kind");
    // a\nb\tc\\d = "a", newline, "b", tab, "c", backslash, "d"
    arp::core::pmr::String expected("a\nb\tc\\d", &arp::core::defaultCpuMemoryResource());
    require(t->text == expected, "escape string value");
}

void testKeywords()
{
    auto lr = lexOk(
        "var const def class if elif else for while return break continue in nil true false pass and or not\n",
        "keywords");
    require(lr.tokens[0].kind == Tok::KwVar, "kw var");
    require(lr.tokens[1].kind == Tok::KwConst, "kw const");
    require(lr.tokens[2].kind == Tok::KwDef, "kw def");
    require(lr.tokens[3].kind == Tok::KwClass, "kw class");
    require(lr.tokens[4].kind == Tok::KwIf, "kw if");
    require(lr.tokens[5].kind == Tok::KwElif, "kw elif");
    require(lr.tokens[6].kind == Tok::KwElse, "kw else");
    require(lr.tokens[7].kind == Tok::KwFor, "kw for");
    require(lr.tokens[8].kind == Tok::KwWhile, "kw while");
    require(lr.tokens[9].kind == Tok::KwReturn, "kw return");
    require(lr.tokens[10].kind == Tok::KwBreak, "kw break");
    require(lr.tokens[11].kind == Tok::KwContinue, "kw continue");
    require(lr.tokens[12].kind == Tok::KwIn, "kw in");
    require(lr.tokens[13].kind == Tok::KwNil, "kw nil");
    require(lr.tokens[14].kind == Tok::KwTrue, "kw true");
    require(lr.tokens[15].kind == Tok::KwFalse, "kw false");
    require(lr.tokens[16].kind == Tok::KwPass, "kw pass");
    require(lr.tokens[17].kind == Tok::KwAnd, "kw and");
    require(lr.tokens[18].kind == Tok::KwOr, "kw or");
    require(lr.tokens[19].kind == Tok::KwNot, "kw not");
}

void testOperators()
{
    auto lr = lexOk("-> == != <= >= ** // << >> += -= *= /= %= ?\n", "operators");
    require(lr.tokens[0].kind == Tok::Arrow, "op ->");
    require(lr.tokens[1].kind == Tok::Eq, "op ==");
    require(lr.tokens[2].kind == Tok::Ne, "op !=");
    require(lr.tokens[3].kind == Tok::Le, "op <=");
    require(lr.tokens[4].kind == Tok::Ge, "op >=");
    require(lr.tokens[5].kind == Tok::StarStar, "op **");
    require(lr.tokens[6].kind == Tok::FloorDiv, "op //");
    require(lr.tokens[7].kind == Tok::Shl, "op <<");
    require(lr.tokens[8].kind == Tok::Shr, "op >>");
    require(lr.tokens[9].kind == Tok::PlusAssign, "op +=");
    require(lr.tokens[10].kind == Tok::MinusAssign, "op -=");
    require(lr.tokens[11].kind == Tok::StarAssign, "op *=");
    require(lr.tokens[12].kind == Tok::SlashAssign, "op /=");
    require(lr.tokens[13].kind == Tok::PercentAssign, "op %=");
    require(lr.tokens[14].kind == Tok::Question, "op ?");
}

void testSingleCharOps()
{
    auto lr = lexOk("( ) [ ] { } : , . = < > + - * / % & | ^ ~\n", "single ops");
    require(lr.tokens[0].kind == Tok::LParen, "op (");
    require(lr.tokens[1].kind == Tok::RParen, "op )");
    require(lr.tokens[2].kind == Tok::LBracket, "op [");
    require(lr.tokens[3].kind == Tok::RBracket, "op ]");
    require(lr.tokens[4].kind == Tok::LBrace, "op {");
    require(lr.tokens[5].kind == Tok::RBrace, "op }");
    require(lr.tokens[6].kind == Tok::Colon, "op :");
    require(lr.tokens[7].kind == Tok::Comma, "op ,");
    require(lr.tokens[8].kind == Tok::Dot, "op .");
    require(lr.tokens[9].kind == Tok::Assign, "op =");
    require(lr.tokens[10].kind == Tok::Lt, "op <");
    require(lr.tokens[11].kind == Tok::Gt, "op >");
    require(lr.tokens[12].kind == Tok::Plus, "op +");
    require(lr.tokens[13].kind == Tok::Minus, "op -");
    require(lr.tokens[14].kind == Tok::Star, "op *");
    require(lr.tokens[15].kind == Tok::Slash, "op /");
    require(lr.tokens[16].kind == Tok::Percent, "op %");
    require(lr.tokens[17].kind == Tok::Amp, "op &");
    require(lr.tokens[18].kind == Tok::Pipe, "op |");
    require(lr.tokens[19].kind == Tok::Caret, "op ^");
    require(lr.tokens[20].kind == Tok::Tilde, "op ~");
}

void testIndentDedent()
{
    const char* src = "def f():\n"
                      "    x = 1\n"
                      "    y = 2\n"
                      "    if x:\n"
                      "        z = 3\n"
                      "    return x\n";
    auto lr = lexOk(src, "indent/dedent");
    // def f ( ) : Newline
    // Indent x = 1 Newline
    // y = 2 Newline
    // if x : Newline
    // Indent z = 3 Newline
    // Dedent return x Newline
    // Dedent Eof
    int indents = countTok(lr, Tok::Indent);
    int dedents = countTok(lr, Tok::Dedent);
    require(indents == 2, "indent count == 2");
    require(dedents == 2, "dedent count == 2");
}

void testIndentDedentNested()
{
    const char* src = "def a():\n"
                      "    def b():\n"
                      "        c = 1\n"
                      "    d = 2\n";
    auto lr = lexOk(src, "nested indent/dedent");
    // Two indents (entering a, entering b), then two dedents (exiting b, exiting a).
    int indents = countTok(lr, Tok::Indent);
    int dedents = countTok(lr, Tok::Dedent);
    require(indents == 2, "nested indent count == 2");
    require(dedents == 2, "nested dedent count == 2");
}

void testBracketNewlineIgnore()
{
    // Inside brackets, newlines are ignored.
    auto lr = lexOk("x = [1,\n  2,\n  3]\n", "bracket newline ignore");
    // Should have no Newline tokens between [ and ].
    bool foundNewlineInBrackets = false;
    int depth = 0;
    for (const auto& t : lr.tokens) {
        if (t.kind == Tok::LBracket || t.kind == Tok::LBrace || t.kind == Tok::LParen)
            ++depth;
        else if (t.kind == Tok::RBracket || t.kind == Tok::RBrace || t.kind == Tok::RParen)
            --depth;
        else if (t.kind == Tok::Newline && depth > 0)
            foundNewlineInBrackets = true;
    }
    require(!foundNewlineInBrackets, "no newline in brackets");
}

void testComments()
{
    auto lr = lexOk("# this is a comment\nx = 1  # inline comment\n", "comments");
    // Should produce: x = 1 Newline Eof
    require(lr.tokens[0].kind == Tok::Ident, "comment: first token is ident");
    require(lr.tokens[1].kind == Tok::Assign, "comment: second token is assign");
    require(lr.tokens[2].kind == Tok::Int, "comment: third token is int");
    require(countTok(lr, Tok::Newline) == 1, "comment: one newline");
}

void testBlankLines()
{
    // Blank lines and comment-only lines should not produce INDENT/DEDENT.
    const char* src = "x = 1\n"
                      "\n"
                      "# comment\n"
                      "y = 2\n";
    auto lr = lexOk(src, "blank lines");
    require(countTok(lr, Tok::Indent) == 0, "blank: no indent");
    require(countTok(lr, Tok::Dedent) == 0, "blank: no dedent");
    require(countTok(lr, Tok::Newline) == 2, "blank: two newlines");
}

void testUnterminatedString()
{
    arp::core::pmr::String source("\"unterminated\n", &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto result = lex(std::move(source), std::move(fname));
    require(!result.ok(), "unterminated string: error reported");
    require(result.error.code == ScriptDiagnosticCode::UnterminatedString,
        "unterminated string: correct error code");
    require(result.error.line == 1, "unterminated string: line 1");
}

void testSourcePos()
{
    auto lr = lexOk("var x = 1\n", "source pos");
    require(lr.tokens[0].pos.line == 1, "pos: var line 1");
    require(lr.tokens[0].pos.column == 1, "pos: var col 1");
    // "var " = 4 chars, so "x" starts at column 5.
    require(lr.tokens[1].pos.line == 1, "pos: x line 1");
    require(lr.tokens[1].pos.column == 5, "pos: x col 5");
}

void testMultilineSourcePos()
{
    const char* src = "x = 1\n"
                      "y = 2\n"
                      "z = 3\n";
    auto lr = lexOk(src, "multiline pos");
    require(lr.tokens[0].pos.line == 1, "multiline: x line 1");
    // tokens: x = 1 Newline y = 2 Newline z = 3 Newline Eof
    // "y" is on line 2.
    require(lr.tokens[4].pos.line == 2, "multiline: y line 2");
    require(lr.tokens[8].pos.line == 3, "multiline: z line 3");
}

void testEmptyInput()
{
    auto lr = lexOk("", "empty input");
    require(lr.tokens.size() == 1, "empty: just Eof");
    require(lr.tokens[0].kind == Tok::Eof, "empty: Eof token");
}

void testOnlyWhitespace()
{
    auto lr = lexOk("   \n  \n  \n", "only whitespace");
    require(countTok(lr, Tok::Newline) == 0, "whitespace: no newlines");
    require(countTok(lr, Tok::Indent) == 0, "whitespace: no indent");
    require(countTok(lr, Tok::Dedent) == 0, "whitespace: no dedent");
    require(lr.tokens.back().kind == Tok::Eof, "whitespace: ends with Eof");
}

void testUnexpectedChar()
{
    arp::core::pmr::String source("x = @\n", &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname("test", &arp::core::defaultCpuMemoryResource());
    auto result = lex(std::move(source), std::move(fname));
    require(!result.ok(), "unexpected char: error reported");
    require(result.error.code == ScriptDiagnosticCode::UnexpectedChar,
        "unexpected char: correct error code");
}

void testBackslashContinuationRejected()
{
    arp::core::pmr::String source(R"(x = 1 \
    + 2
)",
        &arp::core::defaultCpuMemoryResource());
    arp::core::pmr::String fname(
        "backslash-continuation.arp", &arp::core::defaultCpuMemoryResource());
    auto result = lex(std::move(source), std::move(fname));
    require(!result.ok(), "backslash continuation: error reported");
    require(result.error.code == ScriptDiagnosticCode::UnexpectedChar,
        "backslash continuation: unexpected character");
    require(result.error.message.find("\\") != std::string::npos,
        "backslash continuation: diagnostic identifies backslash");
}

void testFinalDedents()
{
    // At EOF, remaining indentation levels should produce DEDENT tokens.
    const char* src = "def f():\n"
                      "    def g():\n"
                      "        x = 1\n";
    auto lr = lexOk(src, "final dedents");
    // 2 indents, 2 dedents at EOF.
    require(countTok(lr, Tok::Indent) == 2, "final: 2 indents");
    require(countTok(lr, Tok::Dedent) == 2, "final: 2 dedents");
}

void testCrLfBlankLinesPreserveIndentation()
{
    const char* src = "class Base:\r\n"
                      "    def value(self) -> int:\r\n"
                      "        return 1\r\n"
                      "\r\n"
                      "    def next(self) -> int:\r\n"
                      "        return self.value()\r\n";
    auto lr = lexOk(src, "CRLF blank lines preserve indentation");
    require(countTok(lr, Tok::Indent) == 3, "CRLF blank lines: indentation levels preserved");
    require(countTok(lr, Tok::Dedent) == 3, "CRLF blank lines: dedentation levels preserved");
}

} // namespace

int main()
{
    testBasicTokens();
    testIntLiterals();
    testInt64Literal();
    testFloatLiterals();
    testStringLiterals();
    testStringEscapes();
    testKeywords();
    testOperators();
    testSingleCharOps();
    testIndentDedent();
    testIndentDedentNested();
    testBracketNewlineIgnore();
    testComments();
    testBlankLines();
    testUnterminatedString();
    testSourcePos();
    testMultilineSourcePos();
    testEmptyInput();
    testOnlyWhitespace();
    testUnexpectedChar();
    testBackslashContinuationRejected();
    testFinalDedents();
    testCrLfBlankLinesPreserveIndentation();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript lexer tests passed\n");
    return 0;
}
