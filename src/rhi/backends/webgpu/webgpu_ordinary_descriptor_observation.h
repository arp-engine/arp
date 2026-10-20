#pragma once

#include "core/quota_bound_shared_identity.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/frontend/descriptor_set_validation.h"

namespace arp::rhi_webgpu {

struct WebGpuOrdinaryDescriptorBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxBindings = 0u;
    std::uint32_t maxWrites = 0u;
    std::uint32_t maxDynamicOffsets = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class WebGpuOrdinaryDescriptorObservation final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOrdinaryDescriptorObservation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOrdinaryDescriptorObservation(ConstructionKey,
        const rhi::DescriptorSet& descriptor,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> payload)
        : descriptor_(&descriptor, descriptor.lifetimeTicket()),
          revision_(descriptor.contentRevision()), registry_(std::move(registry)),
          payload_(std::move(payload)), packageHash_(payload_->shaderPackageHash),
          loweredInput_(payload_->containsLoweredInputAttachment)
    {}

    static const char* validateBudget(const WebGpuOrdinaryDescriptorBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-OPT-IN";
        if (!budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORDINARY-DESCRIPTOR-BUDGET";
        return nullptr;
    }

    static core::UniquePtr<WebGpuOrdinaryDescriptorObservation> prepare(const rhi::DescriptorSet&,
        std::shared_ptr<WebGpuRhiResourceRegistry>,
        const std::uint32_t* offsets,
        std::uint32_t offsetCount,
        const WebGpuOrdinaryDescriptorBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const WebGpuOrdinaryResourceBudget&,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        const char*& diagnostic);
    const char* validate() const noexcept;

    const auto& payload() const noexcept { return payload_; }

    const auto& bindings() const noexcept { return bindings_; }

    const auto& offsets() const noexcept { return offsets_; }

    const auto& resources() const noexcept { return *resources_; }

private:
    core::LifetimeObserver<const rhi::DescriptorSet> descriptor_;
    const std::uint64_t revision_;
    const std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload> payload_;
    const std::uint64_t packageHash_;
    const bool loweredInput_;
    core::pmr::Vector<rhi::DescriptorBindingDesc> layout_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<WebGpuNativeDescriptorBindingPayload> bindings_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> offsetBindings_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> offsets_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<WebGpuOrdinaryResourceProjection> resources_;
};

} // namespace arp::rhi_webgpu
