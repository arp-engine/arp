#pragma once

#include "rhi/frontend/memory_upload_staging_budget.h"
#include "rhi/frontend/transfer_upload_recording_validation.h"
#include "rhi/gpu_device.h"

#include <thread>

namespace arp::rhi_vulkan {
class VulkanRhiNativeCommandPayload;
}

namespace arp::rhi {

class OriginalTextureUploadPublication final : public core::QuotaBoundSharedObjectIdentity {
    friend class rhi_vulkan::VulkanRhiNativeCommandPayload;

    class ConstructionKey {
        friend class OriginalTextureUploadPublication;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxPublications = 0u, maxRetainedOwners = 0u, maxCpuBytes = 0u;
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-OPT-IN";
        return b.maxPublications && b.maxRetainedOwners && b.maxCpuBytes
            ? nullptr
            : "RHI-TEXTURE-UPLOAD-PUBLICATION-BUDGET";
    }

    static std::uint64_t requiredOwners(
        const OriginalCommandStreamProgram& program, std::size_t count) noexcept
    {
        const std::uint64_t per = std::uint64_t(program.retainedOwnerCount())
            + OriginalCommandTextureUploadPackingSource::retainedOwnerCount + 2u;
        return count > (UINT64_MAX - 1u) / per ? UINT64_MAX : 1u + count * per;
    }

    OriginalTextureUploadPublication(ConstructionKey,
        std::shared_ptr<const OriginalCommandStreamProgram> program,
        OriginalOrdinaryTextureUploadContract::Binding binding)
        : program_(std::move(program)), binding_(std::move(binding)),
          owner_(std::this_thread::get_id())
    {}

    OriginalTextureUploadPublication(const OriginalTextureUploadPublication&) = delete;
    OriginalTextureUploadPublication& operator=(const OriginalTextureUploadPublication&) = delete;

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalTextureUploadPublication>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-THREAD";
        if (!completed_ || !state_)
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-NOT-COMPLETED";
        if (const auto* id =
                OriginalCommandTextureUploadPackingSource::validateOriginal(binding_.packing))
            return id;
        const auto& object =
            *binding_.packing->originalSource()->originalDestinationLease()->originalObject();
        if (const auto* id = object.validateOriginalContent(*completed_))
            return id;
        if (const auto* id = object.validateOriginalCommittedState(*state_))
            return id;
        return completed_->state() == common::OriginalTextureContentControl::State::Completed
                && completed_->contentGeneration() && state_->state() == ResourceState::ShaderRead
            ? nullptr
            : "RHI-TEXTURE-UPLOAD-PUBLICATION-CONTENT";
    }

    const auto& originalProgram() const noexcept { return program_; }

    const auto& originalPacking() const noexcept { return binding_.packing; }

    std::size_t commandIndex() const noexcept { return binding_.commandIndex; }

    const char* validateReadVisibility(PipelineStage stages) const noexcept
    {
        if (const auto* id = validate())
            return id;
        const auto& texture =
            *binding_.packing->originalSource()->originalDestinationLease()->originalObject();
        const ResourceDependencyArguments* release = nullptr;
        for (const auto& descriptor : program_->originalInputs()->descriptors()) {
            if (descriptor.commandIndex <= binding_.commandIndex)
                continue;
            const auto* dependency =
                std::get_if<ResourceDependencyArguments>(&descriptor.arguments);
            if (dependency && dependency->firstResource() == texture.id()
                && dependency->kind() == CommandUsageKind::TextureBarrier)
                release = dependency;
        }
        ResourceBarrierDesc barrier;
        if (!release || !release->write(CommandUsageKind::TextureBarrier, barrier))
            return "RHI-TEXTURE-UPLOAD-READ-RELEASE-MISSING";
        return OriginalOrdinaryTextureUploadContract::validateCompletedReadBarrier(
            texture.desc(), texture.id(), barrier, stages);
    }

    const char* validateView(
        const std::shared_ptr<const OriginalCommandTextureViewSource>& view) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(view))
            return id;
        const auto& destination = binding_.packing->originalSource()->originalDestinationLease();
        return view->originalParentLease().get() == destination.get()
                && core::SharedOwnerProjection::sameOwner(view->originalParentLease(), destination)
            ? nullptr
            : "RHI-TEXTURE-UPLOAD-PUBLICATION-VIEW-OWNER";
    }

private:
    static std::shared_ptr<OriginalTextureUploadPublication> issue(
        std::shared_ptr<const OriginalCommandStreamProgram> program,
        OriginalOrdinaryTextureUploadContract::Binding binding)
    {
        return shareOriginal(core::makeUnique<OriginalTextureUploadPublication>(
            ConstructionKey{}, std::move(program), std::move(binding)));
    }

    const char* publicationThreadDiagnostic() const noexcept
    {
        return owner_ == std::this_thread::get_id() ? nullptr
                                                    : "RHI-TEXTURE-UPLOAD-PUBLICATION-THREAD";
    }

    void publish() noexcept
    {
        const auto& object =
            *binding_.packing->originalSource()->originalDestinationLease()->originalObject();
        const auto content = object.observeOriginalContent();
        const auto state =
            object.observeOriginalCommittedState(defaultTextureSubresourceRange(object.desc()));
        if (content && state) {
            completed_.emplace(*content);
            state_.emplace(*state);
        }
    }

    const std::shared_ptr<const OriginalCommandStreamProgram> program_;
    const OriginalOrdinaryTextureUploadContract::Binding binding_;
    const std::thread::id owner_;
    std::optional<GpuTexture::ContentExpectation> completed_;
    std::optional<GpuTexture::CommittedStateObservation> state_;
};

struct OriginalTextureUploadPreparationRequest {
    OriginalOrdinaryTextureUploadContract::Budget uploads;
    OriginalTextureUploadPublication::Budget publications;
};

class OriginalTextureUploadPreparationContract final {
public:
    static const char* validate(const OriginalTextureUploadPreparationRequest& r) noexcept
    {
        if (const auto* id = OriginalOrdinaryTextureUploadContract::validate(r.uploads))
            return id;
        return OriginalTextureUploadPublication::validateBudget(r.publications);
    }

    static auto lowering(Backend backend, const OriginalTextureUploadPreparationRequest& r) noexcept
    {
        auto decision = OriginalOrdinaryTextureUploadContract::lowering(backend, r.uploads);
        if (const auto* id = validate(r))
            return OriginalOrdinaryTextureUploadContract::Decision{false, false, true, id};
        return decision;
    }

    static const char* inspect(const std::shared_ptr<const OriginalCommandStreamProgram>& program,
        const OriginalTextureUploadPreparationRequest& r,
        const OriginalOrdinaryTextureUploadContract::Binding* bindings,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = validate(r))
            return id;
        if (const auto* id =
                OriginalOrdinaryTextureUploadContract::inspect(program, r.uploads, bindings, count))
            return id;
        if (count > r.publications.maxPublications
            || OriginalTextureUploadPublication::requiredOwners(*program, count)
                > r.publications.maxRetainedOwners)
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-CAPACITY";
        if (!quota || quota->limitBytes() != r.publications.maxCpuBytes)
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-QUOTA";
        const auto& inputs = program->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        if (core::SharedOwnerProjection::sameControlBlock(quota, program->originalMetadataQuota())
            || core::SharedOwnerProjection::sameControlBlock(quota, inputs->originalMetadataQuota())
            || core::SharedOwnerProjection::sameControlBlock(
                quota, sources->originalMetadataQuota()))
            return "RHI-TEXTURE-UPLOAD-PUBLICATION-INDEPENDENT-QUOTA";
        for (std::size_t i = 0u; i < count; ++i)
            if (bindings[i].packing->overlapsCpuQuota(quota.get())
                || core::SharedOwnerProjection::sameControlBlock(quota,
                    bindings[i]
                        .packing->originalDestinationLease()
                        ->originalObject()
                        ->originalContentCpuQuota()))
                return "RHI-TEXTURE-UPLOAD-PUBLICATION-INDEPENDENT-QUOTA";
        return nullptr;
    }
};

struct OriginalTextureUploadRecording {
    core::UniquePtr<CommandEncoder> encoder;
    core::pmr::Vector<std::shared_ptr<const OriginalTextureUploadPublication>> publications;
};

class OriginalTextureUploadDeviceExtension {
public:
    static constexpr std::string_view id = "arp.rhi.original-texture-upload/1";
    virtual ~OriginalTextureUploadDeviceExtension() = default;
    virtual const GpuDevice& originalDevice() const noexcept = 0;

    static OriginalTextureUploadDeviceExtension* query(GpuDevice& device,
        const OriginalTextureUploadPreparationRequest& request,
        const char*& diagnostic) noexcept
    {
        const auto decision =
            OriginalTextureUploadPreparationContract::lowering(device.backend(), request);
        diagnostic = decision.diagnostic;
        if (diagnostic || !decision.privateRecordingAvailable)
            return nullptr;
        if ((diagnostic = device.originalDeviceLifetimeTicket().diagnostic()))
            return nullptr;
        auto* extension =
            static_cast<OriginalTextureUploadDeviceExtension*>(device.queryInterface(id));
        if (!extension || &extension->originalDevice() != &device) {
            diagnostic = "RHI-TEXTURE-UPLOAD-EXTENSION-UNAVAILABLE";
            return nullptr;
        }
        return extension;
    }

    virtual const char* prepareStorage(const semantics::UploadStagingColdStorageBudget&,
        const std::shared_ptr<core::CpuMemoryQuota>&) noexcept = 0;
    virtual OriginalTextureUploadRecording prepareUploads(
        std::shared_ptr<const OriginalCommandStreamProgram>,
        const OriginalTextureUploadPreparationRequest&,
        const OriginalOrdinaryTextureUploadContract::Binding*,
        std::size_t,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        core::pmr::String&) = 0;
};

} // namespace arp::rhi
