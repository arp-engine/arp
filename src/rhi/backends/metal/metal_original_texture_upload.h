#pragma once
#include "core/external_object_slot.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_original_command_custody.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_texture_upload_staging.h"

namespace arp::rhi_metal {
class MetalRhiResourceRegistry;
class MetalOriginalTextureUploadCommand;

// ImmutableOriginalTextureUploadStaging/1: actual cold write/flush followed by
// sealing and adoption into the same original canonical inventory.
class MetalOriginalTextureUploadStaging final : public core::QuotaBoundSharedObjectIdentity {
    friend class MetalOriginalTextureUploadCommand;

    class ConstructionKey {
        friend class MetalOriginalTextureUploadStaging;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalTextureUploadStagingBudget;
    static constexpr std::uint32_t retainedOwnerBound =
        rhi::OriginalTextureUploadStagingContract::retainedOwnerBound + 5u;

    struct Quotas {
        std::shared_ptr<core::CpuMemoryQuota> metadata, resource, lease;
    };

    MetalOriginalTextureUploadStaging(ConstructionKey,
        std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource>,
        std::shared_ptr<const rhi::OriginalCommandBufferLease>,
        std::shared_ptr<MetalRhiNativeBufferPayload>,
        std::shared_ptr<MetalRhiNativeTexturePayload>,
        std::shared_ptr<MetalRhiResourceRegistry>,
        const Budget&);
    static std::shared_ptr<const MetalOriginalTextureUploadStaging> prepareOriginal(
        std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource>,
        rhi::GpuDevice&,
        const Budget&,
        const Quotas&,
        const char*&);
    static const char* validateOriginal(
        const std::shared_ptr<const MetalOriginalTextureUploadStaging>&) noexcept;
    const char* validate() const noexcept;

    const auto& originalPacking() const noexcept { return packing_; }

    const auto& originalStagingLease() const noexcept { return staging_; }

    std::uint32_t retainedOwnerCount() const noexcept { return retainedOwnerBound; }

    std::uint64_t nativeStagingBytes() const noexcept
    {
        return staging_->originalObject()->allocation().estimatedSizeBytes;
    }

private:
    const char* validateIdentity() const noexcept;
    const std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource> packing_;
    const std::shared_ptr<const rhi::OriginalCommandBufferLease> staging_;
    const std::shared_ptr<MetalRhiNativeBufferPayload> buffer_;
    const std::shared_ptr<MetalRhiNativeTexturePayload> image_;
    const std::shared_ptr<MetalRhiResourceRegistry> registry_;
    // Frozen native owners detect mutation of the publicly inspectable payload.
    const std::shared_ptr<void> bufferObject_, imageObject_, device_, bufferAllocation_,
        imageAllocation_;
    const Budget budget_;
    const rhi::GpuBuffer::CommittedStateObservation bufferState_;
    const rhi::GpuTexture::CommittedStateObservation textureState_;
    const rhi::GpuTexture::ContentExpectation content_;
};

class MetalOriginalTextureUploadCommand final : public core::QuotaBoundSharedObjectIdentity,
                                                public MetalOriginalCommandCustody {
    friend class MetalRhiNativeCommandPayload;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using Program = rhi::OriginalCommandStreamProgram;
    using Staging = MetalOriginalTextureUploadStaging;
    using Life = rhi::OriginalCommandNativeLifecycleContract;

    class ConstructionKey {
        friend class MetalOriginalTextureUploadCommand;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalTextureUploadCommand(ConstructionKey,
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Program>,
        std::shared_ptr<const Staging>);
    ~MetalOriginalTextureUploadCommand() override;
    static std::shared_ptr<MetalRhiNativeCommandPayload> prepareOriginal(
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Program>,
        std::shared_ptr<const Staging>,
        rhi::GpuDevice&,
        core::pmr::String&);

private:
    const char* advance(Life::Event) noexcept;
    const char* validatePending() const noexcept;
    bool record(MetalRhiNativeCommandPayload&, core::pmr::String&);
    bool beforeSubmit(const MetalRhiNativeCommandPayload&, core::pmr::String&) noexcept override;

    const char* beforeNativeCommit() noexcept override { return advance(Life::Event::MaySubmit); }

    bool nativeCompleted(bool) noexcept override;
    void abandonUnsubmitted() noexcept override;
    const std::shared_ptr<const Reservation> reservation_;
    const std::shared_ptr<const Program> program_;
    const std::shared_ptr<const Staging> staging_;
    std::shared_ptr<core::ExternalObjectSlot> encoderSlot_;
    Life::Phase phase_ = Life::Phase::Claimed;
    const MetalRhiNativeCommandPayload* command_ = nullptr;
};
} // namespace arp::rhi_metal
