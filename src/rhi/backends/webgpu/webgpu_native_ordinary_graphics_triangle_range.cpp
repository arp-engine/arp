#include "rhi/backends/webgpu/webgpu_native_ordinary_graphics_triangle_range.h"

#include "core/math/unsigned_range.h"

#include <algorithm>

namespace arp::rhi_webgpu {
core::UniquePtr<WebGpuOrdinaryGraphicsTriangleRange> WebGpuOrdinaryGraphicsTriangleRange::prepare(
    std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation> validation,
    const shader::GraphicsShaderModuleArtifact& artifact,
    const WebGpuOrdinaryGraphicsTriangleRangeBudget& budget,
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
    if (!issued.graphics().graphics
        || issued.graphics().topology != rhi::PrimitiveTopology::TriangleList
        || issued.graphics().vertexBufferCount || issued.stages().size() != 2u
        || issued.facts().size() != 1u || issued.semantics().usesPush || !issued.tables().empty()) {
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
        diagnostic = WebGpuGraphicsTriangleCorrelation::validateSource(issued.stages(),
            artifact.vertex()->entry(),
            artifact.fragment()->entry(),
            source.size(),
            sourceDigest);
        if (diagnostic)
            return {};
        const auto& vertex = artifact.vertex()->bytes(shader::ShaderArtifactRole::Spirv);
        const auto& fragment = artifact.fragment()->bytes(shader::ShaderArtifactRole::Spirv);
        shader::GraphicsTriangleStorageCrossTargetProof::Facts proof;
        diagnostic = shader::GraphicsTriangleStorageCrossTargetProof::verify(vertex.data(),
            vertex.size(),
            fragment.data(),
            fragment.size(),
            source,
            artifact.vertex()->entry(),
            artifact.fragment()->entry(),
            proof);
        if (diagnostic)
            return {};
        diagnostic = WebGpuGraphicsTriangleCorrelation::validateBinding(issued.facts()[0],
            issued.semantics().groupCount,
            artifact.vertex()->module().reflection,
            artifact.fragment()->module().reflection,
            proof);
        if (diagnostic)
            return {};
        if (proof.fragment.byteCount > budget.maxAccessBytes) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-ACCESS-CAPACITY";
            return {};
        }
        if (!core::containsUnsignedRange(std::uint64_t{0},
                groups.bindingRangeBytes(proof.target.group, proof.target.binding),
                proof.fragment.byteOffset,
                proof.fragment.byteCount)) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-BUFFER-TOO-SMALL";
            return {};
        }
        if (artifact.identity().size() != core::Sha256::digestByteCount) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-PUBLICATION-IDENTITY";
            return {};
        }
        std::array<std::uint8_t, core::Sha256::digestByteCount> identity;
        std::copy(artifact.identity().begin(), artifact.identity().end(), identity.begin());
        return core::makeUnique<WebGpuOrdinaryGraphicsTriangleRange>(
            ConstructionKey{}, std::move(validation), proof, identity);
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-ORDINARY-RANGE-ALLOCATION";
        return {};
    }
}
} // namespace arp::rhi_webgpu
