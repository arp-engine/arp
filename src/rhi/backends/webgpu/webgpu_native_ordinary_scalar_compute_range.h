#pragma once
#include "rhi/backends/webgpu/webgpu_native_ordinary_shader_validation.h"
#include "rhi/backends/webgpu/webgpu_scalar_compute_correlation.h"
#include "shader/scalar_compute_cross_target_proof.h"
#include "shader/single_entry_shader_artifact.h"

namespace arp::rhi_webgpu {
struct WebGpuOrdinaryScalarComputeRangeBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxProofs = 0u;
    std::uint64_t maxArtifactInputBytes = 0u, maxWgslBytes = 0u, maxAccessBytes = 0u;
    std::uint64_t maxArtifactCpuMetadataBytes = 0u, maxCpuMetadataBytes = 0u;
};

class WebGpuOrdinaryScalarComputeRange final : public core::QuotaBoundSharedObjectIdentity {
public:
    class ConstructionKey {
        friend class WebGpuOrdinaryScalarComputeRange;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    WebGpuOrdinaryScalarComputeRange(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation> validation,
        shader::ComputeIndirectProducerProof::Facts facts,
        std::uint32_t group,
        std::uint32_t binding,
        std::array<std::uint8_t, core::Sha256::digestByteCount> publicationIdentity)
        : validation_(std::move(validation)), facts_(facts), group_(group), binding_(binding),
          publicationIdentity_(publicationIdentity)
    {}

    static const char* validateBudget(const WebGpuOrdinaryScalarComputeRangeBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-RANGE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-RANGE-OPT-IN";
        if (!budget.maxProofs || !budget.maxArtifactInputBytes || !budget.maxWgslBytes
            || !budget.maxAccessBytes || !budget.maxArtifactCpuMetadataBytes
            || !budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORDINARY-RANGE-BUDGET";
        return nullptr;
    }

    static core::UniquePtr<WebGpuOrdinaryScalarComputeRange> prepare(
        std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation>,
        const shader::SingleEntryShaderArtifact&,
        const WebGpuOrdinaryScalarComputeRangeBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);

    const char* validate() const noexcept
    {
        if (!validation_->hasQuotaBoundSharedOwner(validation_))
            return "RHI-WEBGPU-ORDINARY-RANGE-VALIDATION-OWNER";
        return validation_->diagnostic();
    }

    const char* validateDispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return shader::ComputeIndirectProducerProof::validateDispatch(x, y, z);
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED";
    }

    const auto& facts() const noexcept { return facts_; }

    std::uint32_t physicalGroup() const noexcept { return group_; }

    std::uint32_t physicalBinding() const noexcept { return binding_; }

    const auto& publicationIdentity() const noexcept { return publicationIdentity_; }

    const auto& originalGroups() const noexcept { return validation_->originalGroups(); }

private:
    friend class WebGpuOrdinaryRecording;
    const std::shared_ptr<const WebGpuOrdinaryShaderSizeValidation> validation_;
    const shader::ComputeIndirectProducerProof::Facts facts_;
    const std::uint32_t group_, binding_;
    const std::array<std::uint8_t, core::Sha256::digestByteCount> publicationIdentity_;
};
} // namespace arp::rhi_webgpu
