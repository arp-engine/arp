#pragma once

#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/ir/typed_ir.h"

namespace arp::scriptvm {

struct DirectNativeCodegenOptionsV5 {
    core::pmr::String packageId{&core::defaultCpuMemoryResource()};
    std::uint32_t packageVersion = 1u;
    core::pmr::String packageIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceHash{&core::defaultCpuMemoryResource()};
    core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::String hostContractHash{&core::defaultCpuMemoryResource()};
    core::pmr::String generatedHeader = "script_aot_v5.gen.h";
    core::pmr::String registrationSymbol = "arp_register_script_aot_package_v5";
    core::pmr::String moduleDescriptorSymbol{&core::defaultCpuMemoryResource()};
    AotPlatform platforms = AotPlatform::None;
    AotExecutionBudgetV5 budget;
    core::pmr::Vector<AotPluginDependencyManifestV5> pluginDependencies{
        &core::defaultCpuMemoryResource()};
};

struct DirectNativeGeneratedModuleV5 {
    AotPackageManifestV5 manifest;
    core::pmr::String manifestYaml{&core::defaultCpuMemoryResource()};
    core::pmr::String header{&core::defaultCpuMemoryResource()};
    core::pmr::String source{&core::defaultCpuMemoryResource()};
    core::pmr::String report{&core::defaultCpuMemoryResource()};
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

ARP_API ScriptResult verifyDirectNativeControlFlowV5(const TypedIrModule& module);
ARP_API DirectNativeGeneratedModuleV5 generateDirectNativeModuleV5(
    const TypedIrModule& module, const DirectNativeCodegenOptionsV5& options);

} // namespace arp::scriptvm
