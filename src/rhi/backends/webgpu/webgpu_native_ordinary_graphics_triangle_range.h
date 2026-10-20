#pragma once
#include "rhi/backends/webgpu/webgpu_graphics_triangle_correlation.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_scalar_compute_range.h"
#include "shader/graphics_shader_module_artifact.h"

namespace arp::rhi_webgpu {
using WebGpuOrdinaryGraphicsTriangleRangeBudget = WebGpuOrdinaryScalarComputeRangeBudget;

// Bounded cold proof ownership; no recording or execution authority.
class WebGpuOrdinaryGraphicsTriangleRange final : public core::QuotaBoundSharedObjectIdentity {
public:
    class ConstructionKey {
        friend class WebGpuOrdinaryGraphicsTriangleRange;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    WebGpuOrdinaryGraphicsTriangleRange(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation> validation,
        shader::GraphicsTriangleStorageCrossTargetProof::Facts facts,
        std::array<std::uint8_t, core::Sha256::digestByteCount> identity)
        : validation_(std::move(validation)), facts_(facts), identity_(identity)
    {}

    static const char* validateBudget(
        const WebGpuOrdinaryGraphicsTriangleRangeBudget& budget) noexcept
    {
        return WebGpuOrdinaryScalarComputeRange::validateBudget(budget);
    }

    static core::UniquePtr<WebGpuOrdinaryGraphicsTriangleRange> prepare(
        std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation>,
        const shader::GraphicsShaderModuleArtifact&,
        const WebGpuOrdinaryGraphicsTriangleRangeBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);

    const char* validate() const noexcept
    {
        if (!validation_->hasQuotaBoundSharedOwner(validation_))
            return "RHI-WEBGPU-ORDINARY-RANGE-VALIDATION-OWNER";
        return validation_->diagnostic();
    }

    const char* validateDraw(std::uint32_t vertices,
        std::uint32_t instances,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance,
        bool indexed = false) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return shader::GraphicsTriangleStorageCrossTargetProof::validateDraw(
            vertices, instances, firstVertex, firstInstance, indexed);
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED";
    }

    const auto& facts() const noexcept { return facts_; }

    const auto& publicationIdentity() const noexcept { return identity_; }

    const auto& originalGroups() const noexcept { return validation_->originalGroups(); }

private:
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinaryGraphicsAttachments;
    const std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation> validation_;
    const shader::GraphicsTriangleStorageCrossTargetProof::Facts facts_;
    const std::array<std::uint8_t, core::Sha256::digestByteCount> identity_;
};
} // namespace arp::rhi_webgpu
