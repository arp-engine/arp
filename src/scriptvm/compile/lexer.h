#pragma once

// ArpScript: Lexer / Tokenizer.
//
// Produces a flat token stream from ArpScript source text. Implements
// Python-style INDENT/DEDENT tokens via an indent-stack (same algorithm as
// CPython's tokenizer): at the start of each logical line, the leading
// whitespace is compared against the indent stack; deeper => emit INDENT,
// shallower => emit DEDENT(s) until matching, same => nothing.
//
// Blank lines and comment-only lines do not produce INDENT/DEDENT.
// Inside (), [], {} brackets, newlines are ignored (no logical line break),
// and indentation is not significant (matches Python).
//
// Token kinds:
//   - Keywords: var, const, def, class, if, elif, else, for, while, return,
//               break, continue, in, nil, true, false, pass, and, or, not
//   - Punctuation: ( ) [ ] { } : , . -> = == != < <= > >= + - * / % ** &
//                  | ^ ~ << >> // += -= *= /= %=
//   - Literals: Int, Int64, Float, String
//   - Identifiers
//   - Newline (logical line end), Indent, Dedent, Eof

#include "core/memory/cpu_allocator.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/source_pos.h"

#include <cstdint>

namespace arp::scriptvm {

enum class Tok : std::uint8_t {
    // Special
    Eof = 0,
    Newline,
    Indent,
    Dedent,

    // Literals
    Int,
    Int64,
    Float,
    String,

    // Identifier / keyword
    Ident,

    // Keywords
    KwVar,
    KwConst,
    KwDef,
    KwClass,
    KwIf,
    KwElif,
    KwElse,
    KwFor,
    KwWhile,
    KwReturn,
    KwBreak,
    KwContinue,
    KwIn,
    KwNil,
    KwTrue,
    KwFalse,
    KwPass,
    KwAnd,
    KwOr,
    KwNot,
    KwImport,
    KwFrom,
    KwAs,
    KwAwait,
    KwLambda,
    KwTry,
    KwExcept,
    KwRaise,
    KwYield,

    // Punctuation
    LParen,        // (
    RParen,        // )
    LBracket,      // [
    RBracket,      // ]
    LBrace,        // {
    RBrace,        // }
    Colon,         // :
    Comma,         // ,
    Dot,           // .
    Arrow,         // ->
    Assign,        // =
    Eq,            // ==
    Ne,            // !=
    Lt,            // <
    Le,            // <=
    Gt,            // >
    Ge,            // >=
    Plus,          // +
    Minus,         // -
    Star,          // *
    Slash,         // /
    Percent,       // %
    StarStar,      // **
    Amp,           // &
    Pipe,          // |
    Caret,         // ^
    Tilde,         // ~
    Shl,           // <<
    Shr,           // >>
    FloorDiv,      // //
    PlusAssign,    // +=
    MinusAssign,   // -=
    StarAssign,    // *=
    SlashAssign,   // /=
    PercentAssign, // %=
    Question,      // ? (for optional type ?T)

    TokCount,
};

const char* tokName(Tok t);

struct Token {
    Tok kind = Tok::Eof;
    core::pmr::String text{&core::defaultCpuMemoryResource()};
    SourcePos pos;

    // For numeric literals: the parsed value.
    // For Int:   i64 holds the int value.
    // For Int64: i64 holds the int64 value (suffix L).
    // For Float: f64 holds the double value.
    std::int64_t i64 = 0;
    double f64 = 0.0;

    Token() = default;

    Token(Tok k, core::pmr::String t, SourcePos p) : kind(k), text(std::move(t)), pos(std::move(p))
    {}
};

struct LexResult {
    core::pmr::Vector<Token> tokens{&core::defaultCpuMemoryResource()};
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

// Tokenize ArpScript source text. Returns a token stream ending with Tok::Eof.
// On error, LexResult::error is filled and tokens may be partially populated.
LexResult lex(core::pmr::String source, core::pmr::String filename);

} // namespace arp::scriptvm
