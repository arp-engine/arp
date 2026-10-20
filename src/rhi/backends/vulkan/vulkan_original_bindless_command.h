#pragma once
#include "rhi/backends/vulkan/vulkan_bindless_root.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.h"
#include "rhi/backends/vulkan/vulkan_original_command_custody.h"
#include "rhi/frontend/bindless_graphics_pass_sequence.h"
#include "rhi/frontend/bindless_produced_geometry.h"
#include "rhi/frontend/bindless_root_native_sequence.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/command_buffer_readback.h"

namespace arp::rhi_vulkan {
class VulkanRhiNativeCommandPayload;

// RootFaultFence/6, CompletedRootTextureFence/8 and GraphicsRootFence/9.
// Closed cold factory; /9 retains the exact original Graphics stream and cold
// pass through authentic completion, with independent completion profile /6.
// Command owns custody, custody never owns Command. All events come from the
// actual native command/queue/fence or exact-runtime device idle, never from a
// caller completion flag.
class VulkanOriginalBindlessCommand final : public core::QuotaBoundSharedObjectIdentity,
                                            public VulkanOriginalCommandCustody {
    friend class VulkanRhiNativeCommandPayload;
    friend class VulkanOriginalBindlessGraphicsExecutionView;
    friend class VulkanOriginalBindlessComputeExecutionView;
    friend class VulkanBindlessCrossExtension;
    using Closure = VulkanBindlessRootNativeClosure;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using Binding = rhi::OriginalCommandBufferReadbackBinding;
    using Life = rhi::OriginalCommandNativeLifecycleContract;
    using PassInput = rhi::OriginalBindlessGraphicsPassSequenceContract::Input<
        VulkanOriginalBindlessGraphicsPass>;

    class ConstructionKey {
        friend class VulkanOriginalBindlessCommand;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    VulkanOriginalBindlessCommand(ConstructionKey,
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Binding>,
        rhi::BindlessFaultSubmissionLease,
        std::shared_ptr<const VulkanOriginalBindlessGraphicsPass>,
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Closure>,
        const rhi::OriginalBindlessRootReadbackSequence::Input& = {},
        const rhi::OriginalBindlessRootNativeSequence<Closure>::Input& = {},
        const PassInput& = {});
    ~VulkanOriginalBindlessCommand() override;

    std::uint32_t lifecycleVersion() const noexcept override
    {
        return reservation_->originalSeal()->originalBudget().contractVersion;
    }

    static std::shared_ptr<VulkanRhiNativeCommandPayload> prepareOriginal(
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Binding>,
        rhi::GpuDevice&,
        core::pmr::String&,
        std::shared_ptr<const VulkanOriginalBindlessGraphicsPass> = {},
        const rhi::OriginalIndexedDrawFetchContract::Budget& = {},
        bool deferRecording = false,
        std::shared_ptr<const Closure> = {},
        std::shared_ptr<core::AtomicCapacityBudget> = {},
        std::shared_ptr<const Closure> = {},
        const rhi::OriginalBindlessRootReadbackSequence::Input& = {},
        const rhi::OriginalBindlessRootNativeSequence<Closure>::Input& = {},
        const PassInput& = {},
        const rhi::OriginalBindlessProducedGeometryInputs& = {});

private:
    static const char* validateGeometryFetch(const std::shared_ptr<const Closure>&,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>&,
        const rhi::OriginalIndexedDrawFetchContract::Budget&) noexcept;
    const char* advance(Life::Event) noexcept;
    const char* validatePending() const noexcept;
    const char* validateStoragePasses(bool nativeObjects = true) const noexcept;
    bool prepareStoragePasses(
        const std::shared_ptr<core::AtomicCapacityBudget>&, core::pmr::String&);
    bool beforeSubmit(const VulkanRhiNativeCommandPayload&, core::pmr::String&) noexcept override;

    const char* maySubmit() noexcept override { return advance(Life::Event::MaySubmit); }

    void submissionUnproven(const VulkanRhiNativeCommandPayload&) noexcept override;
    void fenceCompleted(const VulkanRhiNativeCommandPayload&) noexcept override;
    bool record(VulkanRhiNativeCommandPayload&, core::pmr::String&);
    const rhi::OriginalBindlessRootNativeSequence<Closure> roots_;
    const std::shared_ptr<const Closure>& closure_;
    const std::shared_ptr<const Closure>& nextClosure_;
    const std::shared_ptr<const Closure>& finalClosure_;
    const std::shared_ptr<const Reservation> reservation_;
    const rhi::OriginalBindlessRootReadbackSequence readbacks_;
    const std::shared_ptr<const Binding>& binding_;
    const std::shared_ptr<const VulkanOriginalBindlessGraphicsPass> graphics_;
    core::pmr::Vector<std::shared_ptr<const VulkanOriginalBindlessGraphicsPass>> producedPasses_{
        &core::defaultCpuMemoryResource()};

    struct ClearPass {
        std::size_t commandIndex = SIZE_MAX;
        std::shared_ptr<const rhi::OriginalCommandTextureViewSource> source;
        Closure::Observation parent, view;
        rhi::Extent3D extent;
        VulkanRhiNativeRenderPassResources native;
    };

    core::pmr::Vector<ClearPass> clearPasses_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<core::AtomicCapacityLease> attachmentLease_;
    rhi::BindlessFaultSubmissionLease fault_;
    rhi::OriginalBindlessRootFaultCustody additionalFaults_;
    Life::Phase phase_ = Life::Phase::Claimed;
    const VulkanRhiNativeCommandPayload* command_ = nullptr;
};
} // namespace arp::rhi_vulkan
