#pragma once
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_ordinary_graphics_write.h"
#include "rhi/frontend/command_recorded_state.h"

namespace arp::rhi_metal {

struct MetalRhiOrdinaryWritePrototypeIntent {
    std::uint32_t contractVersion = 1u;
    bool prototypeOptIn = false;
    std::uint32_t maxProducerDraws = 0u;
    std::uint32_t maxConsumerDraws = 0u;

    const char* validate() const noexcept
    {
        if (contractVersion != 1u || !prototypeOptIn)
            return "RHI-METAL-ORDINARY-WRITE-PROTOTYPE-OPT-IN-REQUIRED";
        // Transfer has four fixed commands; shader has five (two pass pairs
        // and dependency). Both draw budgets are explicit and overflow-safe.
        const auto fixed = maxConsumerDraws ? 5u : 4u;
        return maxProducerDraws && maxProducerDraws <= UINT32_MAX - fixed
                && maxConsumerDraws <= UINT32_MAX - fixed - maxProducerDraws
            ? nullptr
            : "RHI-METAL-ORDINARY-WRITE-DRAW-BUDGET";
    }
};

class MetalRhiOrdinaryPreparedWriteReadback final {
    friend class MetalRhiNativeCommandPayload;

    class ConstructionKey {
        friend class MetalRhiOrdinaryPreparedWriteReadback;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class Status { Pending, Completed, Failed, Abandoned };
    MetalRhiOrdinaryPreparedWriteReadback(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>,
        std::shared_ptr<rhi::GpuBuffer>,
        MetalRhiNativeResourceObservation);
    MetalRhiOrdinaryPreparedWriteReadback(const MetalRhiOrdinaryPreparedWriteReadback&) = delete;
    MetalRhiOrdinaryPreparedWriteReadback& operator=(
        const MetalRhiOrdinaryPreparedWriteReadback&) = delete;
    static std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback> prepare(
        std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>,
        rhi::GpuDevice&,
        core::pmr::String&);
    rhi::ValidationResult validateObservation() const;
    rhi::ValidationResult read(std::uint32_t&) const;

    Status status() const noexcept { return status_.load(std::memory_order_acquire); }

    bool recorded() const noexcept { return recorded_.load(std::memory_order_acquire); }

    const auto& write() const noexcept { return write_; }

    const auto& resource() const noexcept { return *buffer_; }

    const auto& quota() const noexcept { return write_->quota(); }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    const char* validateOutput(bool writable) const;
    template <class Visitor> void projectOwners(Visitor&& visit) const;
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> write_;
    std::shared_ptr<rhi::GpuBuffer> buffer_;
    MetalRhiNativeResourceObservation observation_;
    core::SharedOwnerProjection owners_;
    std::atomic<Status> status_{Status::Pending};
    std::atomic<bool> recorded_{false};
};

// Owns the same shared journal as the private native recording payload. All
// construction is cold; wrapper destruction explicitly abandons native work.
class MetalRhiOrdinaryWriteCommand final : public rhi::RecordedStateCommandBuffer {
    class ConstructionKey {
        friend class MetalRhiOrdinaryWriteCommand;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiOrdinaryWriteCommand(ConstructionKey,
        std::shared_ptr<MetalRhiNativeCommandPayload> payload,
        std::shared_ptr<core::MutationJournal> states)
        : RecordedStateCommandBuffer(std::move(states)), payload_(std::move(payload))
    {}

    ~MetalRhiOrdinaryWriteCommand() override { payload_->abandonUnsubmitted(); }

    MetalRhiOrdinaryWriteCommand(const MetalRhiOrdinaryWriteCommand&) = delete;
    MetalRhiOrdinaryWriteCommand& operator=(const MetalRhiOrdinaryWriteCommand&) = delete;
    static core::UniquePtr<MetalRhiOrdinaryWriteCommand> prepare(
        const MetalRhiOrdinaryWritePrototypeIntent&,
        const core::pmr::String&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        const rhi::CommandRetentionBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);

    std::uint32_t commandCount() const override
    {
        return payload_->stats()->ordinaryWriteCommandCount;
    }

    const auto& payload() const noexcept { return payload_; }

    bool submit(MetalRhiNativeQueueStats& queue,
        const std::shared_ptr<MetalRhiNativeSharedEventPayload>& wait,
        std::uint64_t value,
        core::pmr::String& diagnostic)
    {
        if (!payload_->submit(
                queue, payload_->preparedCompletionState(), wait, value, {}, 0u, diagnostic))
            return false;
        return acceptRecordedOwnership();
    }

private:
    std::shared_ptr<MetalRhiNativeCommandPayload> payload_;
};

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiOrdinaryWriteNativeCommandPayload(
    const MetalRhiOrdinaryWritePrototypeIntent&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    std::shared_ptr<core::MutationJournal>,
    core::pmr::String&);
} // namespace arp::rhi_metal
