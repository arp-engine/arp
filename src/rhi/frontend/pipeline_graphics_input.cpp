#include "rhi/frontend/pipeline_graphics_input.h"

namespace arp::rhi {
std::shared_ptr<const OriginalGraphicsPipelineInput>
OriginalGraphicsPipelineInput::prepareTableExpansion(
    const std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact>& artifact,
    ExpansionBudget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    return OriginalDepthTableExpansionInputContract::prepare<OriginalGraphicsPipelineInput>(
        artifact,
        budget,
        quota,
        diagnostic,
        shader::OriginalDepthTableExpansionArtifact::Profile::Graphics,
        ConstructionKey{});
}

std::shared_ptr<const OriginalGraphicsPipelineInput> OriginalGraphicsPipelineInput::prepare(
    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& module,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (budget.contractVersion != 1u && budget.contractVersion != 2u) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-INPUT-VERSION";
        return {};
    }
    if ((budget.contractVersion == 1u
            && (budget.availableTargetContractVersion || budget.prototypeOptIn))
        || (budget.contractVersion == 2u && budget.availableTargetContractVersion != 1u)) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-INPUT-ROLE-POLICY-VERSION";
        return {};
    }
    if (budget.contractVersion == 2u && !budget.prototypeOptIn) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-INPUT-ROLE-POLICY-OPT-IN";
        return {};
    }
    if (const auto* id = shader::GraphicsShaderModuleArtifact::validateOriginal(module)) {
        diagnostic = id;
        return {};
    }
    if (budget.contractVersion == 1u
        && module->targetDiagnostic(shader::ShaderArtifactRole::Metal)
            == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED") {
        diagnostic = module->targetDiagnostic(shader::ShaderArtifactRole::Metal);
        return {};
    }
    if (budget.contractVersion == 2u)
        if (const auto* id =
                shader::GraphicsShaderModuleArtifact::runtimeLengthAvailableTargetDiagnostic(
                    module)) {
            diagnostic = id;
            return {};
        }
    if (!quota || !budget.maxCpuMetadataBytes
        || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA";
        return {};
    }
    if (quota.get() == module->quota().get()
        || core::SharedOwnerProjection::sameControlBlock(quota, module->quota())) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-INPUT-QUOTA-DOMAIN";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto input =
            core::makeUnique<OriginalGraphicsPipelineInput>(ConstructionKey{}, budget, module);
        const auto valid = budget.contractVersion == 2u
            ? populateRuntimeLengthShaderInterface(module, input->package_)
            : populateGraphicsModuleShaderInterface(*module, input->package_);
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return shareOriginal(std::move(input));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-GRAPHICS-INPUT-ALLOCATION";
        return {};
    }
}
} // namespace arp::rhi
