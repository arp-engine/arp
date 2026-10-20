#pragma once

// ArpScript: frontend and typed-module production boundary.
//
// The frontend owns the sema-validated module that is shared by all lowering
// backends. ScriptLowerer is the single entry into the versioned Typed IR
// contract. Every backend consumes Typed IR emitted directly from the
// sema-validated AST; no bytecode compatibility adapter exists here.

#include "core/cpu_allocator.h"
#include "scriptvm/compile/ast.h"
#include "scriptvm/ir/typed_ir.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/module_registry.h"

#include <memory>

namespace arp::scriptvm {

struct ScriptTypedModule {
    std::shared_ptr<Module> syntax;
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TypedIrCallableBinding> callableBindings{&core::defaultCpuMemoryResource()};

    bool valid() const { return syntax != nullptr && !moduleId.empty(); }
};

struct ScriptFrontendResult {
    ScriptTypedModule module;
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

class ARP_API ScriptFrontend final {
public:
    static ScriptFrontendResult compile(core::pmr::String source,
        core::pmr::String filename,
        core::pmr::String moduleId,
        ModuleRegistry* moduleRegistry = nullptr);

    // Deterministic tooling may compose a parsed module before semantic
    // analysis. Adoption keeps that exceptional path behind the same typed
    // frontend boundary; callers must pass a successfully analyzed module.
    static ScriptFrontendResult adoptAnalyzedSyntax(std::shared_ptr<Module> syntax,
        core::pmr::String moduleId,
        ModuleRegistry* moduleRegistry = nullptr);
};

class ARP_API ScriptLowerer final {
public:
    static TypedIrBuildResult lower(const ScriptTypedModule& module);
};

} // namespace arp::scriptvm
