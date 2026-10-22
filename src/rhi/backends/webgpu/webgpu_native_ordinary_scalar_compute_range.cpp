#include "rhi/backends/webgpu/webgpu_native_ordinary_scalar_compute_range.h"

#include "core/math/unsigned_range.h"

#include <algorithm>

namespace arp::rhi_webgpu {
core::UniquePtr<WebGpuOrdinaryScalarComputeRange> WebGpuOrdinaryScalarComputeRange::prepare(
    std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation> validation,
    const shader::SingleEntryShaderArtifact& artifact,
    const WebGpuOrdinaryScalarComputeRangeBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-QUOTA";
        return {};
    }
    if (!validation || !validation->hasQuotaBoundSharedOwner(validation)) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-VALIDATION-OWNER";
        return {};
    }
    if ((diagnostic = validation->diagnostic()))
        return {};
    const auto& groups = *validation->groups_;
    const auto& issued = groups.pipeline_->contract();
    if (issued.graphics().graphics || issued.stages().size() != 1u || issued.facts().size() != 1u
        || issued.semantics().usesPush || !issued.tables().empty()
        || artifact.stage() != shader::ShaderStage::Compute) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-LANGUAGE-UNSUPPORTED";
        return {};
    }
    if (!artifact.quota() || artifact.quota()->limitBytes() != budget.maxArtifactCpuMetadataBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-ARTIFACT-QUOTA";
        return {};
    }
    const auto& wgsl = artifact.bytes(shader::ShaderArtifactRole::Wgsl);
    if (artifact.inputBytes() > budget.maxArtifactInputBytes || wgsl.size() > budget.maxWgslBytes) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-INPUT-CAPACITY";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        const core::StringView source(reinterpret_cast<const char*>(wgsl.data()), wgsl.size());
        const auto digest = core::Sha256::digest(source);
        std::array<std::uint8_t, core::Sha256::digestByteCount> sourceDigest;
        std::copy(digest.begin(), digest.end(), sourceDigest.begin());
        diagnostic = WebGpuScalarComputeCorrelation::validateSource(
            issued.stages()[0], artifact.entry(), source.size(), sourceDigest);
        if (diagnostic)
            return {};
        const auto& spirv = artifact.bytes(shader::ShaderArtifactRole::Spirv);
        shader::ScalarComputeCrossTargetProof::Facts proof;
        diagnostic = shader::ScalarComputeCrossTargetProof::verify(
            spirv.data(), spirv.size(), source, artifact.entry(), proof);
        if (diagnostic)
            return {};
        const auto& binary = proof.binary;
        const auto& target = proof.target;
        diagnostic = WebGpuScalarComputeCorrelation::validateBinding(
            issued.facts()[0], issued.semantics().groupCount, artifact.module().reflection, proof);
        if (diagnostic)
            return {};
        if (binary.byteCount > budget.maxAccessBytes) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-ACCESS-CAPACITY";
            return {};
        }
        if (!core::containsUnsignedRange(std::uint64_t{0u},
                groups.bindingRangeBytes(target.group, target.binding),
                binary.byteOffset,
                binary.byteCount)) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-BUFFER-TOO-SMALL";
            return {};
        }
        if (artifact.identity().size() != core::Sha256::digestByteCount) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-PUBLICATION-IDENTITY";
            return {};
        }
        std::array<std::uint8_t, core::Sha256::digestByteCount> identity;
        std::copy(artifact.identity().begin(), artifact.identity().end(), identity.begin());
        return core::makeUnique<WebGpuOrdinaryScalarComputeRange>(ConstructionKey{},
            std::move(validation),
            binary,
            target.group,
            target.binding,
            identity);
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-ALLOCATION";
        return {};
    }
}
} // namespace arp::rhi_webgpu
