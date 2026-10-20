#pragma once
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/command_geometry_content.h"

namespace arp::rhi {
class OriginalCommandRecordingSeal;
}

namespace arp::rhi_webgpu {
// Closed genuine mapped-write/flush actor. Its exact original lease joins the
// canonical Root inventory. No command issuer, callback or completion receipt.
class WebGpuOriginalGeometryContent final : public rhi::OriginalInitializedGeometrySource {
    class ConstructionKey {
        friend class WebGpuOriginalGeometryContent;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::OriginalHostGeometryContentContract;
    using Budget = Contract::Budget;
    using Lease = rhi::OriginalCommandBufferLease;

    using Quotas = Contract::Quotas;

    WebGpuOriginalGeometryContent(ConstructionKey,
        std::shared_ptr<const Lease>,
        std::shared_ptr<WebGpuNativeBufferPayload>,
        WebGpuTransferResourceObservation,
        core::pmr::Vector<std::uint8_t>,
        Budget,
        Quotas,
        rhi::GpuBuffer::CommittedStateObservation);
    static std::shared_ptr<const WebGpuOriginalGeometryContent> prepareOriginal(rhi::GpuDevice&,
        const rhi::BufferDesc&,
        const void*,
        std::size_t,
        const Budget&,
        const Quotas&,
        const char*&);
    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalGeometryContent>&) noexcept;
    static const char* validateOriginalPending(
        const std::shared_ptr<const WebGpuOriginalGeometryContent>&,
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>&,
        const rhi::GpuBuffer::CommittedStateObservation&) noexcept;
    const char* validate() const noexcept;

    const std::shared_ptr<const Lease>& originalLease() const noexcept override { return lease_; }

    std::uint64_t initializedBytes() const noexcept override { return bytes_.size(); }

    std::uint32_t retainedOwnerCount() const noexcept { return Contract::retainedOwnerBound; }

    bool nativeExecutionProven() const noexcept { return false; }

    // ImmutableShader/2 reuses this exact sealed mapped initializer and lease.
    // The callback borrows authentic backing bytes; it cannot issue GPU work.
    template <class Visitor>
    static const char* visitInitializedShader(
        const std::shared_ptr<const WebGpuOriginalGeometryContent>& self,
        const std::shared_ptr<const Lease>& lease,
        rhi::DescriptorType type,
        Visitor&& visitor) noexcept
    {
        if (const auto* id = validateLease(self, lease))
            return id;
        const auto* shader =
            std::get_if<Contract::Staging::ShaderContentIntent>(&self->budget_.definition);
        if (!shader || self->budget_.contractVersion != 2u || shader->type != type)
            return "RHI-HOST-IMMUTABLE-SHADER-CONTENT-PROFILE";
        return visitor(self->bytes_.data(), self->bytes_.size());
    }

    // The lease/native payload/runtime are already in the original closure.
    // Only these independent CPU quota owners are additional retained roots.
    static constexpr std::size_t exclusiveCpuQuotaSlots = 3u;

    const std::shared_ptr<core::CpuMemoryQuota>& exclusiveCpuQuota(std::size_t slot) const noexcept
    {
        return slot == 0u ? quotas_.metadata : slot == 1u ? quotas_.payload : quotas_.resource;
    }

    static const char* analyzeInitializedIndices(
        const std::shared_ptr<const WebGpuOriginalGeometryContent>&,
        const std::shared_ptr<const Lease>&,
        rhi::IndexFormat,
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        std::int32_t,
        rhi::IndexInputConsumptionContract::DrawRange&) noexcept;
    static const char* validateInitializedVertices(
        const std::shared_ptr<const WebGpuOriginalGeometryContent>&,
        const std::shared_ptr<const Lease>&,
        const rhi::VertexInputConsumptionContract::Stream&,
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        std::uint64_t) noexcept;

private:
    const char* validateIdentity() const noexcept;
    static const char* validateLease(const std::shared_ptr<const WebGpuOriginalGeometryContent>&,
        const std::shared_ptr<const Lease>&) noexcept;
    const std::shared_ptr<const Lease> lease_;
    const std::shared_ptr<WebGpuNativeBufferPayload> payload_;
    const core::pmr::Vector<std::uint8_t> bytes_;
    const Budget budget_;
    const Quotas quotas_;
    const rhi::GpuBuffer::CommittedStateObservation state_;
    const WebGpuTransferResourceObservation nativeIdentity_;
};
} // namespace arp::rhi_webgpu
