#pragma once

// ArpScript: Semantic analysis (Sema).
//
// Walks the AST from parse() and performs:
//   1. Scope/Symbol resolution: every identifier must be declared before use.
//   2. Type checking: mandatory type hints on every var/const/param/field/return;
//      assignment type compatibility; binary/unary operand type compatibility;
//      call argument count + type matching; return value type matching.
//   3. Closure capture analysis: mark local variables that are captured by
//      nested functions (so the compiler knows to box them in cells).
//   4. Class/method validation: method self parameter, field access on class.
//
// On error, fills SemaResult::error with a diagnostic.
// The sema annotates the AST in-place where needed (e.g., marking captured
// variables, resolving overloaded methods). For Phase 4 we keep annotations
// minimal -- the compiler re-walks scopes for slot allocation.

#include "core/memory/cpu_allocator.h"
#include "scriptvm/compile/ast.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/module_registry.h"

#include <cstdint>
#include <memory>

namespace arp::scriptvm {

struct SemaResult {
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

// Run semantic analysis on a parsed Module.
// moduleRegistry is optional; if provided, sema validates that imported
// symbols exist in the target module. If null, import validation is
// deferred to runtime.
// Returns SemaResult; on success, the module is annotated and ready for
// the compiler.
ARP_API SemaResult analyze(
    std::shared_ptr<Module> module, ModuleRegistry* moduleRegistry = nullptr);

// Convenience: parse + sema in one step.
// Returns the module on success, or nullptr + fills error on failure.
ARP_API std::shared_ptr<Module> compileToTypedAst(core::pmr::String source,
    core::pmr::String filename,
    ScriptDiagnostic* error,
    ModuleRegistry* moduleRegistry = nullptr);

} // namespace arp::scriptvm
