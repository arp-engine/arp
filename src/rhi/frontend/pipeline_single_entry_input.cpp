#include "rhi/frontend/pipeline_single_entry_input.h"

namespace arp::rhi {
std::shared_ptr<const OriginalSingleEntryPipelineInput>
OriginalSingleEntryPipelineInput::prepareTableExpansion(
    const std::shared_ptr<const shader::OriginalDepthTableExpansionArtifact>& artifact,
    ExpansionBudget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    return OriginalDepthTableExpansionInputContract::prepare<OriginalSingleEntryPipelineInput>(
        artifact,
        budget,
        quota,
        diagnostic,
        shader::OriginalDepthTableExpansionArtifact::Profile::Compute,
        ConstructionKey{});
}

std::shared_ptr<const OriginalSingleEntryPipelineInput> OriginalSingleEntryPipelineInput::prepare(
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact,
    Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (budget.contractVersion != 1u && budget.contractVersion != 2u) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-INPUT-VERSION";
        return {};
    }
    if ((budget.contractVersion == 1u
            && (budget.availableTargetContractVersion || budget.prototypeOptIn))
        || (budget.contractVersion == 2u && budget.availableTargetContractVersion != 1u)) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-INPUT-ROLE-POLICY-VERSION";
        return {};
    }
    if (budget.contractVersion == 2u && !budget.prototypeOptIn) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-INPUT-ROLE-POLICY-OPT-IN";
        return {};
    }
    if (const auto* id = shader::SingleEntryShaderArtifact::validateOriginal(artifact)) {
        diagnostic = id;
        return {};
    }
    if (budget.contractVersion == 1u
        && artifact->targetDiagnostic(shader::ShaderArtifactRole::Metal)
            == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED") {
        diagnostic = artifact->targetDiagnostic(shader::ShaderArtifactRole::Metal);
        return {};
    }
    if (budget.contractVersion == 2u)
        if (const auto* id =
                shader::SingleEntryShaderArtifact::runtimeLengthAvailableTargetDiagnostic(
                    artifact)) {
            diagnostic = id;
            return {};
        }
    if (!quota || !budget.maxCpuMetadataBytes
        || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA";
        return {};
    }
    if (quota.get() == artifact->quota().get()
        || core::SharedOwnerProjection::sameControlBlock(quota, artifact->quota())) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-INPUT-QUOTA-DOMAIN";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result =
            core::makeUnique<OriginalSingleEntryPipelineInput>(ConstructionKey{}, budget, artifact);
        const auto valid = budget.contractVersion == 2u
            ? populateRuntimeLengthShaderInterface(artifact, result->package_)
            : populateSingleEntryShaderInterface(*artifact, result->package_);
        if (!valid.success) {
            diagnostic = valid.diagnostics;
            return {};
        }
        return shareOriginal(std::move(result));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-ARTIFACT-INPUT-ALLOCATION";
        return {};
    }
}
} // namespace arp::rhi
