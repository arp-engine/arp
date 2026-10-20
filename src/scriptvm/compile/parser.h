#pragma once

// ArpScript: Recursive-descent parser.
//
// Consumes a token stream from the lexer and produces a Module AST.
// Handles Python-style indentation blocks (INDENT/DEDENT) for:
//   - function bodies
//   - class bodies
//   - if/elif/else bodies
//   - for/while bodies
//
// Expression parsing uses Pratt-style precedence climbing for binary operators.
//
// On error, fills ParseResult::error with a diagnostic (code, message, line, col).
// The parser does minimal error recovery -- it reports the first error and stops.

#include "core/cpu_allocator.h"
#include "scriptvm/compile/ast.h"
#include "scriptvm/compile/lexer.h"
#include "scriptvm/runtime/diagnostics.h"

#include <cstdint>

namespace arp::scriptvm {

struct ParseResult {
    std::shared_ptr<Module> module;
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

// Parse a token stream into a Module AST.
// On error, ParseResult::error is filled and module may be partially populated.
ARP_API ParseResult parse(const core::pmr::Vector<Token>& tokens);

// Convenience: lex + parse in one step.
ARP_API ParseResult parseSource(core::pmr::String source, core::pmr::String filename);

} // namespace arp::scriptvm
