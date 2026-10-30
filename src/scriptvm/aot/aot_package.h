#pragma once

#include "scriptvm/aot/aot_codegen_v5.h"
#include "scriptvm/aot/aot_manifest_runtime.h"
#include "scriptvm/plugin/script_source_snapshot.h"

#include <filesystem>

namespace arp::scriptvm {

struct ScriptAotPackageModuleInput {
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 1u;
    std::filesystem::path source;
    core::pmr::Vector<core::pmr::String> entries{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> dependencies{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AotPluginDependencyManifestV5> pluginDependencies{
        &core::defaultCpuMemoryResource()};
};

struct ScriptAotCallableContractInput {
    core::pmr::String bindingId{&core::defaultCpuMemoryResource()};
    core::pmr::String targetId{&core::defaultCpuMemoryResource()};
    core::pmr::String signatureHash{&core::defaultCpuMemoryResource()};
    TypedIrCallableOrigin origin = TypedIrCallableOrigin::HostCapability;
    int fixedArity = -1;
    bool maySuspend = false;
    std::uint32_t lifetimeVersion = 0u;
};

struct ScriptAotPackageDefinition {
    core::pmr::String type = kAotPackageManifestType;
    core::pmr::String packageId{&core::defaultCpuMemoryResource()};
    std::uint32_t packageVersion = 1u;
    AotPlatform platforms = AotPlatform::None;
    AotExecutionBudgetV5 budget;
    core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::String hostContractHash{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ScriptAotCallableContractInput> callableContracts{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<ScriptAotPackageModuleInput> modules{&core::defaultCpuMemoryResource()};
    std::filesystem::path manifestPath;
};

struct ScriptAotPackageLoadResult {
    ScriptAotPackageDefinition package;
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

struct ScriptAotPackageBuildResult {
    core::pmr::String packageIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::filesystem::path> outputs{&core::defaultCpuMemoryResource()};
    core::pmr::String report{&core::defaultCpuMemoryResource()};
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

class ARP_API ScriptAotCompiler final {
public:
    DirectNativeGeneratedModuleV5 compileModule(const ScriptAotPackageDefinition& package,
        const core::pmr::String& moduleId,
        const ScriptSourceSnapshot& sources,
        const core::pmr::String& packageIdentity,
        const core::pmr::String& generatedHeader) const;
    // Source compatibility only: no implicit file IO or unbounded source input.
    DirectNativeGeneratedModuleV5 compileModule(const ScriptAotPackageDefinition& package,
        const ScriptAotPackageModuleInput& module,
        const core::pmr::String& packageIdentity,
        const core::pmr::String& generatedHeader) const;
};

class ARP_API ScriptAotPackageBuilder final {
public:
    static ScriptAotPackageLoadResult load(const core::io::FileReadRequest& binding,
        core::io::FileReadResult source,
        std::uint64_t maxTextBytes);
    static ScriptAotPackageLoadResult load(
        const std::filesystem::path& manifestPath, std::uint64_t maxManifestBytes);
    static ScriptAotPackageLoadResult load(const std::filesystem::path& manifestPath);
    static ScriptResult validate(const ScriptAotPackageDefinition& package);
    static ScriptResult captureHostSources(
        const ScriptAotPackageDefinition& package, ScriptSourceSnapshot& sources);
    ScriptAotPackageBuildResult build(const ScriptAotPackageDefinition& package,
        const ScriptSourceSnapshot& sources,
        const std::filesystem::path& outputDirectory) const;
    // Source compatibility only; use an explicitly prepared snapshot above.
    ScriptAotPackageBuildResult build(const ScriptAotPackageDefinition& package,
        const std::filesystem::path& outputDirectory) const;
};

} // namespace arp::scriptvm
