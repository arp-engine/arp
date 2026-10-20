#pragma once

#include "scriptvm/compile/frontend.h"
#include "scriptvm/interpreter/bytecode_lowerer.h"

namespace arp::scriptvm {

struct ScriptInterpreterBuildRequest {
    core::pmr::String source{&core::defaultCpuMemoryResource()};
    core::pmr::String filename{&core::defaultCpuMemoryResource()};
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 1u;
    core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::String entryFunctionId{&core::defaultCpuMemoryResource()};
    ModuleRegistry* moduleRegistry = nullptr;
};

// Internal orchestration hook for deterministic tooling that must augment a
// frontend-owned AST (for example, source-module class composition) before IR
// lowering. The source still enters through ScriptFrontend and no Program is
// accepted here.
struct ScriptInterpreterPreparedBuildRequest {
    core::pmr::String source{&core::defaultCpuMemoryResource()};
    ScriptTypedModule module;
    std::uint32_t moduleVersion = 1u;
    core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::String entryFunctionId{&core::defaultCpuMemoryResource()};
};

struct ScriptInterpreterBuildResult {
    TypedIrModule typedIr;
    Program program;
    BytecodeLoweringReceipt receipt;
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

class ARP_API ScriptInterpreterPipeline final {
public:
    static ScriptInterpreterBuildResult build(const ScriptInterpreterBuildRequest& request);
    static ScriptInterpreterBuildResult buildPrepared(
        const ScriptInterpreterPreparedBuildRequest& request);
};

} // namespace arp::scriptvm
