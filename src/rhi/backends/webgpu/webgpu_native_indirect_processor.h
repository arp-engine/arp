#pragma once

#include "rhi/frontend/bindless_indirect_packet.h"

namespace arp::rhi_webgpu {

class WebGpuNativeZeroBaseIndirectProducer;
struct WebGpuNativeBufferPayload;
struct WebGpuNativeCommandPayload;

using WebGpuIndirectProcessorBudget = rhi::BindlessIndirectProcessorBudget;

// Closed backend payload: ordinary callers cannot obtain writable scratch
// buffers or replace the verified kernel, descriptors or packet destinations.
class WebGpuNativeIndirectProcessor final : public rhi::BindlessIndirectProcessor {
    class ConstructionKey {
        friend class WebGpuNativeIndirectProcessorPool;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    friend class WebGpuNativeIndirectProcessorPool;
    friend struct WebGpuNativeCommandPayload;

public:
    explicit WebGpuNativeIndirectProcessor(ConstructionKey) {}

    WebGpuNativeIndirectProcessor(const WebGpuNativeIndirectProcessor&) = delete;
    rhi::ValidationResult validate() const override;

    std::uint64_t identity() const noexcept override { return identity_; }

    const rhi::BindlessIndirectProcessorIntent& intent() const noexcept override
    {
        return *intent_;
    }

    std::uint64_t ownedBufferBytes() const noexcept { return ownedBufferBytes_; }

    std::uint64_t retainedBytes() const noexcept override { return retainedBytes_; }

private:
    core::UniquePtr<core::AtomicCapacityLease> countLease_;
    core::UniquePtr<core::AtomicCapacityLease> byteLease_;
    std::shared_ptr<const rhi::BindlessIndirectProcessorIntent> intent_;
    // Metadata, fault, physical arguments and scalar instance mapping.
    std::array<core::UniquePtr<rhi::GpuBuffer>, 4> buffers_;
    std::array<std::shared_ptr<WebGpuNativeBufferPayload>, 4> nativeBuffers_;
    std::array<std::shared_ptr<WebGpuNativeBufferPayload>, 2> destinations_;
    std::shared_ptr<const WebGpuNativeZeroBaseIndirectProducer> producer_;
    std::uint64_t ownedBufferBytes_ = 0;
    std::uint64_t retainedBytes_ = 0;
    const std::uint64_t identity_ = rhi::allocateGpuResourceId().value;
};

// Preparation only. Reservations survive the pool and remain charged while
// native completion storage retains the processor and its L2 recording lease.
class WebGpuNativeIndirectProcessorPool final {
public:
    WebGpuNativeIndirectProcessorPool(rhi::GpuDevice&, WebGpuIndirectProcessorBudget);
    WebGpuNativeIndirectProcessorPool(const WebGpuNativeIndirectProcessorPool&) = delete;
    std::shared_ptr<const WebGpuNativeIndirectProcessor> prepare(
        std::shared_ptr<const rhi::BindlessIndirectProcessorIntent>, core::pmr::String& diagnostic);

    std::uint64_t liveProcessors() const noexcept { return counts_->used(); }

    std::uint64_t chargedBytes() const noexcept { return bytes_->used(); }

private:
    rhi::GpuDevice& device_;
    WebGpuIndirectProcessorBudget budget_;
    std::shared_ptr<core::AtomicCapacityBudget> counts_;
    std::shared_ptr<core::AtomicCapacityBudget> bytes_;
};

class WebGpuRhiIndirectProcessorPool final : public rhi::BindlessIndirectProcessorPool {
public:
    WebGpuRhiIndirectProcessorPool(
        rhi::GpuDevice& device, const rhi::BindlessIndirectProcessorBudget& budget)
        : native_(device, budget)
    {}

    std::shared_ptr<const rhi::BindlessIndirectProcessor> prepare(
        std::shared_ptr<const rhi::BindlessIndirectProcessorIntent> intent,
        core::pmr::String& diagnostic) override
    {
        return native_.prepare(std::move(intent), diagnostic);
    }

    std::uint64_t liveProcessors() const noexcept override { return native_.liveProcessors(); }

    std::uint64_t chargedBytes() const noexcept override { return native_.chargedBytes(); }

private:
    WebGpuNativeIndirectProcessorPool native_;
};

} // namespace arp::rhi_webgpu
