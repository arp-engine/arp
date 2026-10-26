#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/query_creation.h"
#include "rhi/frontend/query_slot_declaration_control.h"
#include "rhi/gpu_device.h"

#include <mutex>
#include <variant>

namespace arp::rhi_vulkan {

class VulkanRhiQueryPoolPayload final {
public:
    using SlotControl = rhi::common::QuerySlotDeclarationControl;

    // Declaration observation only; callers retain this exact pool lifetime.
    // No native handle, completion authority or second journal is exposed.
    class SlotObservation final {
        friend class VulkanRhiQueryPoolPayload;
        using Value = std::variant<SlotControl::Observation, SlotControl::PendingObservation>;

        SlotObservation(const VulkanRhiQueryPoolPayload* owner, std::uint32_t index, Value value)
            : owner_(owner), index_(index), value_(std::move(value))
        {}

        const VulkanRhiQueryPoolPayload* owner_;
        std::uint32_t index_;
        Value value_;

    public:
        std::uint32_t index() const noexcept { return index_; }

        SlotControl::Phase phase() const noexcept;
        std::uint64_t revision() const noexcept;
    };

    VulkanRhiQueryPoolPayload(std::shared_ptr<VulkanRhiRuntime> runtime,
        VkQueryPool queryPool,
        core::pmr::Vector<std::uint64_t> scopeIds,
        std::uint32_t count);
    ~VulkanRhiQueryPoolPayload();
    VkQueryPool nativeHandle() const;
    bool belongsTo(const VulkanRhiRuntime& runtime) const noexcept;
    const core::pmr::Vector<std::uint64_t>& scopeIds() const;
    rhi::GpuTimingSample completedTimingSample(
        const VulkanRhiRuntime& runtime, std::uint64_t frameSerial) const;

    std::optional<SlotObservation> observeSlot(
        std::uint32_t queryIndex, const core::MutationJournal* journal = nullptr) const noexcept;
    bool matchesSlot(
        const SlotObservation&, const core::MutationJournal* journal = nullptr) const noexcept;
    const char* preflightRecord(
        const SlotObservation&, core::MutationJournal* journal = nullptr) const noexcept;
    bool markRecorded(std::uint32_t queryIndex,
        std::pmr::string& diagnostics,
        core::MutationJournal* journal = nullptr);
    bool markRecorded(const SlotObservation& original,
        core::MutationJournal& journal,
        std::pmr::string& diagnostics);
    const char* declareRecorded(const SlotObservation&, core::MutationJournal&) noexcept;
    bool markSubmitted(std::uint32_t queryIndex,
        std::pmr::string& diagnostics,
        core::MutationJournal* journal = nullptr);
    bool rollbackSubmitted(std::uint32_t queryIndex,
        std::pmr::string& diagnostics,
        core::MutationJournal* journal = nullptr);
    bool markSubmissionUnproven(
        std::uint32_t queryIndex, core::MutationJournal* journal = nullptr) noexcept;
    void markCompleted(const VulkanRhiRuntime& runtime, std::uint32_t queryIndex);
    bool rollbackRecorded(std::uint32_t queryIndex);
    void markDiagnosticFailure(std::uint32_t queryIndex, const char* diagnostic);
    rhi::CommandValidationResult reset();
    rhi::QueryExecutionStatus executionStatus() const;

private:
    friend class VulkanRhiQueryPool;
    friend core::UniquePtr<rhi::QueryPool> createVulkanRhiQueryPool(
        const std::shared_ptr<VulkanRhiRuntime>&, const rhi::QueryPoolDesc&);

    const char* createNativePool(std::uint32_t count) noexcept;
    static rhi::QueryExecutionState slotState(const SlotControl&) noexcept;
    static void failCompletedSlot(SlotControl&) noexcept;
    bool matchesSlotLocked(const SlotObservation&, const core::MutationJournal*) const noexcept;
    const char* preflightRecordLocked(
        const SlotObservation&, core::MutationJournal*) const noexcept;

    std::shared_ptr<VulkanRhiRuntime> runtime_;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    core::pmr::Vector<std::uint64_t> scopeIds_;
    core::pmr::Vector<core::UniquePtr<SlotControl>> slots_{
        &core::defaultCpuMemoryResource(),
    };
    core::pmr::Vector<std::uint64_t> completedTimestamps_;
    std::shared_ptr<const rhi::QueryPoolInputSnapshot> originalInput_;
    mutable std::mutex mutex_;
};

using VulkanOriginalQueryPoolBudget = rhi::OriginalQueryPoolCreationBudget;

class VulkanRhiQueryPool final : public rhi::QueryPool,
                                 public core::QuotaBoundSharedObjectIdentity {
public:
    VulkanRhiQueryPool(std::shared_ptr<VulkanRhiQueryPoolPayload> payload, rhi::QueryPoolDesc desc);
    VulkanRhiQueryPool(const VulkanRhiQueryPool&) = delete;
    VulkanRhiQueryPool& operator=(const VulkanRhiQueryPool&) = delete;
    std::uint32_t count() const override;
    rhi::QueryType type() const override;
    rhi::CommandValidationResult reset() override;
    rhi::QueryExecutionStatus executionStatus() const override;
    VkQueryPool nativeHandle() const;
    const std::shared_ptr<VulkanRhiQueryPoolPayload>& payload() const;
    static const char* validateOriginalBudget(const VulkanOriginalQueryPoolBudget&) noexcept;
    static std::shared_ptr<VulkanRhiQueryPool> prepareOriginal(
        const std::shared_ptr<VulkanRhiRuntime>& runtime,
        std::shared_ptr<const rhi::QueryPoolInputSnapshot> input,
        const VulkanOriginalQueryPoolBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic);
    static const char* validateOriginal(const std::shared_ptr<const VulkanRhiQueryPool>& self,
        const VulkanRhiRuntime& runtime) noexcept;
    std::shared_ptr<const rhi::QueryPoolInputSnapshot> originalInput() const noexcept override;
    const char* validateOriginalInput(const rhi::QueryPoolOriginalInputArguments&) const noexcept;

private:
    struct OriginalCreation {
        rhi::semantics::OriginalQueryCreationLowering::Decision decision;
        VulkanOriginalQueryPoolBudget budget;
    };

    std::optional<OriginalCreation> originalCreation_;
    std::shared_ptr<VulkanRhiQueryPoolPayload> payload_;
    rhi::QueryPoolDesc desc_;
};

arp::core::UniquePtr<rhi::QueryPool> createVulkanRhiQueryPool(
    const std::shared_ptr<VulkanRhiRuntime>& runtime, const rhi::QueryPoolDesc& desc);

const VulkanRhiQueryPool* asVulkanRhiQueryPool(const rhi::QueryPool& pool);

} // namespace arp::rhi_vulkan
