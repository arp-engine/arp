#pragma once
#include "rhi/backends/vulkan/vulkan_bindless_root.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/command_stream_program.h"

namespace arp::rhi_vulkan {
// Authentic cold pass resources for the original /9 transaction. No native
// handles escape this backend; only the sealed original command may consume
// them. Ownership is all-or-nothing and cannot mutate a journal or issue work.
class VulkanOriginalBindlessGraphicsPass final : public core::QuotaBoundSharedObjectIdentity {
    friend class VulkanOriginalBindlessCommand;
    using Closure = VulkanBindlessRootNativeClosure;
    using Transaction = rhi::OriginalCommandStateTransaction;
    using Stream = rhi::OriginalCommandStreamProgram;
    using Contract = rhi::OriginalBindlessGraphicsPassContract;
    using Budget = Contract::Budget;

    class ConstructionKey {
        friend class VulkanOriginalBindlessGraphicsPass;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Attachment {
        std::shared_ptr<const rhi::OriginalCommandTextureViewSource> source;
        std::shared_ptr<VulkanRhiNativeResourcePayload> parent;
        std::uint64_t generation = 0u;
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

public:
    VulkanOriginalBindlessGraphicsPass(ConstructionKey,
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Transaction>,
        std::shared_ptr<const Stream>,
        Budget,
        std::shared_ptr<core::AtomicCapacityBudget>);
    ~VulkanOriginalBindlessGraphicsPass();
    static std::shared_ptr<const VulkanOriginalBindlessGraphicsPass> prepareOriginal(
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Transaction>,
        std::shared_ptr<const Stream>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::AtomicCapacityBudget>,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    static const char* validateOriginal(
        const std::shared_ptr<const VulkanOriginalBindlessGraphicsPass>&) noexcept;
    const char* validate() const noexcept;

    std::uint32_t nativeObjectCount() const noexcept { return attachmentCount_ + 2u; }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    std::size_t commandIndex() const noexcept { return commandIndex_; }

    const auto& originalClosure() const noexcept { return closure_; }

    const auto& originalTransaction() const noexcept { return transaction_; }

    const auto& originalStream() const noexcept { return stream_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const rhi::RenderPassDesc* descriptor() const noexcept;
    const char* captureAttachments() noexcept;
    const char* inspectOwners(std::uint32_t&) const noexcept;
    const char* validateNativeOwners(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& = {}) const noexcept;
    bool createNativeObjects() noexcept;
    const std::shared_ptr<const Closure> closure_;
    const std::shared_ptr<const Transaction> transaction_;
    const std::shared_ptr<const Stream> stream_;
    const Budget budget_;
    const std::shared_ptr<core::AtomicCapacityBudget> capacity_;
    core::UniquePtr<core::AtomicCapacityLease> lease_;
    std::array<Attachment, Contract::maxAttachments> attachments_;
    std::uint32_t attachmentCount_ = 0u, owners_ = 0u;
    std::size_t commandIndex_ = SIZE_MAX;
    rhi::Extent3D extent_;
    VulkanRhiNativeRenderPassResources native_;
};
} // namespace arp::rhi_vulkan
