#pragma once
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_original_command_custody.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_texture_upload_staging.h"

namespace arp::rhi_vulkan {
class VulkanOriginalTextureUploadCommand;

// Actual cold-written/flushed immutable original staging, not an initialization
// flag. The exact packing and registered buffer join the canonical inventory.
class VulkanOriginalTextureUploadStaging final : public core::QuotaBoundSharedObjectIdentity {
    friend class VulkanOriginalTextureUploadCommand;

    class ConstructionKey {
        friend class VulkanOriginalTextureUploadStaging;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalTextureUploadStagingBudget;

    struct Quotas {
        std::shared_ptr<core::CpuMemoryQuota> metadata, resource, lease;
    };

    VulkanOriginalTextureUploadStaging(ConstructionKey,
        std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource>,
        std::shared_ptr<const rhi::OriginalCommandBufferLease>,
        std::shared_ptr<VulkanRhiNativeResourcePayload>,
        std::shared_ptr<VulkanRhiNativeResourcePayload>,
        std::shared_ptr<VulkanRhiRuntime>,
        const Budget&);
    static std::shared_ptr<const VulkanOriginalTextureUploadStaging> prepareOriginal(
        std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource>,
        rhi::GpuDevice&,
        const Budget&,
        const Quotas&,
        const char*&);
    static const char* validateOriginal(
        const std::shared_ptr<const VulkanOriginalTextureUploadStaging>&) noexcept;
    // Shared borrowed validation for immutable staging and the existing cold
    // pool. No new registration, ownership receipt or execution grant.
    static const char* inspectDestination(
        const std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource>&,
        rhi::GpuDevice&,
        const VulkanRhiRuntime&) noexcept;
    const char* validate() const noexcept;

    const auto& originalPacking() const noexcept { return packing_; }

    const auto& originalStagingLease() const noexcept { return staging_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return rhi::OriginalTextureUploadStagingContract::retainedOwnerBound;
    }

    std::uint64_t nativeStagingBytes() const noexcept
    {
        return staging_->originalObject()->allocation().estimatedSizeBytes;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const char* validateIdentity() const noexcept;
    const std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource> packing_;
    const std::shared_ptr<const rhi::OriginalCommandBufferLease> staging_;
    const std::shared_ptr<VulkanRhiNativeResourcePayload> buffer_, image_;
    const std::shared_ptr<VulkanRhiRuntime> runtime_;
    const Budget budget_;
    const rhi::GpuBuffer::CommittedStateObservation bufferState_;
    const rhi::GpuTexture::CommittedStateObservation textureState_;
    const rhi::GpuTexture::ContentExpectation content_;
};

// WholeOriginalUploadFence /7. Reuses the same real command queue/fence/idle
// retirement. No fault/readback buffer, second journal or caller receipt exists.
class VulkanOriginalTextureUploadCommand final : public core::QuotaBoundSharedObjectIdentity,
                                                 public VulkanOriginalCommandCustody {
    friend class VulkanRhiNativeCommandPayload;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using Program = rhi::OriginalCommandStreamProgram;
    using Staging = VulkanOriginalTextureUploadStaging;
    using Life = rhi::OriginalCommandNativeLifecycleContract;

    class ConstructionKey {
        friend class VulkanOriginalTextureUploadCommand;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    VulkanOriginalTextureUploadCommand(ConstructionKey,
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Program>,
        std::shared_ptr<const Staging>,
        std::size_t);
    ~VulkanOriginalTextureUploadCommand() override;

    std::uint32_t lifecycleVersion() const noexcept override { return 7u; }

    static std::shared_ptr<VulkanRhiNativeCommandPayload> prepareOriginal(
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Program>,
        std::shared_ptr<const Staging>,
        rhi::GpuDevice&,
        core::pmr::String&);

private:
    const char* advance(Life::Event) noexcept;
    const char* validatePending() const noexcept;
    bool beforeSubmit(const VulkanRhiNativeCommandPayload&, core::pmr::String&) noexcept override;

    const char* maySubmit() noexcept override { return advance(Life::Event::MaySubmit); }

    void submissionUnproven(const VulkanRhiNativeCommandPayload&) noexcept override;
    void fenceCompleted(const VulkanRhiNativeCommandPayload&) noexcept override;
    bool record(VulkanRhiNativeCommandPayload&, core::pmr::String&);
    const std::shared_ptr<const Reservation> reservation_;
    const std::shared_ptr<const Program> program_;
    const std::shared_ptr<const Staging> staging_;
    const std::size_t uploadCommand_;
    Life::Phase phase_ = Life::Phase::Claimed;
    const VulkanRhiNativeCommandPayload* command_ = nullptr;
};
} // namespace arp::rhi_vulkan
