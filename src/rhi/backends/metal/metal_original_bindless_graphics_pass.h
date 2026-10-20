#pragma once
#include "rhi/backends/metal/metal_bindless_root.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/frontend/command_stream_program.h"

namespace arp::rhi_metal {
// Cold, immutable pass for the same original Content9 transaction. Its private
// native creation path cannot authorize an ordinary pass or issue GPU work.
class MetalOriginalBindlessGraphicsPass final : public core::QuotaBoundSharedObjectIdentity {
    friend class MetalOriginalBindlessCommand;
    using Closure = MetalRhiBindlessRootNativeClosure;
    using Transaction = rhi::OriginalCommandStateTransaction;
    using Stream = rhi::OriginalCommandStreamProgram;
    using Contract = rhi::OriginalBindlessGraphicsPassContract;
    using Budget = Contract::Budget;

    class ConstructionKey {
        friend class MetalOriginalBindlessGraphicsPass;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Attachment {
        std::shared_ptr<const rhi::OriginalCommandTextureViewSource> source;
        std::shared_ptr<MetalRhiNativeTexturePayload> parent;
        std::shared_ptr<MetalRhiNativeTextureViewPayload> view;
        MetalRhiNativeResourceObservation parentIdentity, viewIdentity;
        std::uint64_t generation = 0u;
    };

public:
    MetalOriginalBindlessGraphicsPass(ConstructionKey,
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Transaction>,
        std::shared_ptr<const Stream>,
        Budget,
        std::shared_ptr<core::AtomicCapacityBudget>,
        std::shared_ptr<MetalRhiResourceRegistry>);
    static std::shared_ptr<const MetalOriginalBindlessGraphicsPass> prepareOriginal(
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Transaction>,
        std::shared_ptr<const Stream>,
        rhi::GpuDevice&,
        const Budget&,
        std::shared_ptr<core::AtomicCapacityBudget>,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    static const char* validateOriginal(
        const std::shared_ptr<const MetalOriginalBindlessGraphicsPass>&) noexcept;
    const char* validate() const noexcept;

    // Metal retains existing views and creates one native pass descriptor.
    std::uint32_t nativeObjectCount() const noexcept { return 1u; }

    std::uint32_t retainedOwnerCount() const noexcept { return owners_; }

    std::size_t commandIndex() const noexcept { return commandIndex_; }

    const auto& originalClosure() const noexcept { return closure_; }

    const auto& originalTransaction() const noexcept { return transaction_; }

    const auto& originalStream() const noexcept { return stream_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const rhi::RenderPassDesc* descriptor() const noexcept;
    const char* captureAttachments() noexcept;
    const char* validateAttachments(bool pending = false) const noexcept;
    const char* validateNativeOwners(
        const std::shared_ptr<const rhi::OriginalCommandRecordingSeal>& = {}) const noexcept;
    const char* inspectOwners(std::uint32_t&) const noexcept;
    bool createNativeObjects(core::pmr::String&);
    const std::shared_ptr<const Closure> closure_;
    const std::shared_ptr<const Transaction> transaction_;
    const std::shared_ptr<const Stream> stream_;
    const Budget budget_;
    const std::shared_ptr<core::AtomicCapacityBudget> capacity_;
    const std::shared_ptr<MetalRhiResourceRegistry> registry_;
    core::UniquePtr<core::AtomicCapacityLease> lease_;
    std::array<Attachment, Contract::maxAttachments> attachments_;
    std::uint32_t attachmentCount_ = 0u, owners_ = 0u;
    std::size_t commandIndex_ = SIZE_MAX;
    MetalRhiNativeRenderPassPreparation native_;
};
} // namespace arp::rhi_metal
