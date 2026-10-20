#pragma once

#include "original_completed_sampled_input_fixture.h"
#include "original_general_output_readback_inputs_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/finite_graphics_content_validation.h"

namespace arp::test {
// Real initialization and exclusive attachment custody on the caller's
// registered Graphics device. This grants no mixed root, draw or output plan.
class OriginalGeneralFiniteGraphicsContentSourcesFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Graphics content Sources: %s\n", id);
        assert(!id);
    }

public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Sources = rhi::OriginalCommandResourceSources;
    using Initializer = OriginalCompletedSampledInputFixture;
    using Prepared = decltype(Initializer::prepare(
        std::declval<Initializer::Context&>(), std::declval<Initializer::Quotas&>()));

    OriginalGeneralFiniteGraphicsContentSourcesFixture(rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& source,
        const std::shared_ptr<core::CpuMemoryQuota>& resource)
        : attachmentQuota_(core::makeShared<core::CpuMemoryQuota>(bytes))
    {
        upload_.emplace(device, source, resource);
        auto parent = upload_->binding->originalDestinationLease();
        const auto initial = parent->originalObject()->observeOriginalContent();
        assert(initial);
        initial_.emplace(*initial);
        prepared_.emplace(Initializer::prepare(*upload_, quotas_));
        auto& completed = std::get<5>(*prepared_);
        assert(completed && initial_->diagnostic());
        require(parent->originalObject()->validateOriginalContent(*completed));
        assert(parent->originalObject()->desc().initialState == rhi::ResourceState::CopyDst
            && completed->state() == rhi::common::OriginalTextureContentControl::State::Completed);
        const std::array domains{attachmentQuota_,
            parent->originalObject()->originalContentCpuQuota(),
            source,
            resource};
        assert(core::SharedOwnerProjection::hasIndependentOwners(
            domains.size(), [&](std::size_t i) noexcept {
                return domains[i];
            }));
        const char* id = nullptr;
        core::UniquePtr<rhi::GpuTextureView> sampledView;
        {
            core::CpuMemoryQuotaScope scope(resource);
            rhi::TextureViewDesc view;
            view.format = parent->originalObject()->desc().format;
            view.usage = rhi::TextureUsage::Sampled;
            view.mipLevelCount = 2u;
            sampledView = device.createTextureView(*parent->originalObject(), view);
            assert(sampledView);
        }
        auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            sampledView, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(viewLease);
        auto sampledSource = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            viewLease, parent, {1u, true, 5u, bytes}, source, id);
        require(id);
        assert(sampledSource);
        sampled_ = {parent, sampledSource};
        rhi::TextureDesc desc;
        desc.extent = {1u, 1u, 1u};
        desc.format = rhi::TextureFormat::Rgba16Float;
        desc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        desc.initialState = rhi::ResourceState::Undefined;
        const rhi::OriginalTextureContentCreationBudget creation{
            2u, true, 1u, 1u, 8u, 1u, bytes, 1u};
        const auto create = [&](const auto& budget, const auto& quota) {
            core::CpuMemoryQuotaScope scope(resource);
            return device.createOriginalContentTexture(desc, budget, quota, id);
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto less = creation;
        --less.maxTexelBytes;
        assert(!create(less, attachmentQuota_) && id);
        assert(!create(creation, resource) && id);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations
            && !attachmentQuota_->liveBytes());
        {
            core::CpuMemoryQuotaLease full(attachmentQuota_, bytes);
            assert(full.charged() && !create(creation, attachmentQuota_) && id);
        }
        assert(!attachmentQuota_->liveBytes());
        auto attachment = create(creation, attachmentQuota_);
        require(id);
        assert(attachment);
        core::UniquePtr<rhi::GpuTextureView> attachmentView;
        {
            core::CpuMemoryQuotaScope scope(resource);
            rhi::TextureViewDesc view;
            view.format = desc.format;
            view.usage = rhi::TextureUsage::RenderAttachment;
            attachmentView = device.createTextureView(*attachment, view);
            assert(attachmentView);
        }
        std::uint64_t footprint = 0u;
        require(rhi::OriginalFiniteGraphicsContentPlanContract::inspectAttachment(desc,
            attachmentView->desc(),
            creation,
            rhi::LoadOp::Clear,
            rhi::StoreOp::Store,
            false,
            false,
            footprint));
        assert(footprint == 8u);
        const auto attachmentInitial =
            attachment->observeOriginalContent(rhi::defaultTextureSubresourceRange(desc));
        assert(attachmentInitial);
        attachmentInitial_.emplace(*attachmentInitial);
        assert(attachmentInitial_ && !attachmentInitial_->diagnostic()
            && attachmentInitial_->state()
                == rhi::common::OriginalTextureContentControl::State::Uninitialized
            && !attachmentInitial_->contentGeneration());
        assert(attachment->originalContentExclusive() && !attachment->observeOriginalContent());
        auto attachmentParent = rhi::OriginalCommandTextureLease::prepareOriginal(
            attachment, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(attachmentParent);
        auto attachmentLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            attachmentView, {1u, true, 2u, bytes}, source, id);
        require(id);
        assert(attachmentLease);
        auto attachmentSource = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            attachmentLease, attachmentParent, {1u, true, 5u, bytes}, source, id);
        require(id);
        assert(attachmentSource);
        attachment_ = {attachmentParent, attachmentSource};
        require(rhi::OriginalFiniteGraphicsContentPlanContract::validateIndependentResources(
            parent->resource(),
            completed->resourceGeneration(),
            attachmentParent->resource(),
            attachmentInitial_->resourceGeneration()));
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCES_CREATED_OK sampled-upload20-mips2-completed attachment-ContentCreation2-RGBA16F8-cell1-uninitialized original-independent-quotas exact-less-exhausted source-owners no-mixed-root");
    }

    const auto& sampledSources() const noexcept { return sampled_; }

    const auto& attachmentSources() const noexcept { return attachment_; }

    auto completedShaderInput() const
    {
        return rhi_webgpu::WebGpuCanonicalNativeClosure::ShaderTextureContentInput{4u,
            std::get<3>(*prepared_),
            std::get<4>(*prepared_),
            std::get<1>(*prepared_),
            std::get<5>(*prepared_),
            rhi::ResourceState::ShaderRead,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderRead};
    }

    const char* validateAssociation(
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const rhi::GpuDevice& device) const noexcept
    {
        if (const auto* id =
                rhi::OriginalCommandDescriptorSetSource::validateOriginal(descriptor, device))
            return id;
        const auto& elements = descriptor->frozenElements();
        if (descriptor->sourceContractVersion() != 5u
            || descriptor->frozenPipelineSourceIndex() != 8u || elements.size() != 4u)
            return "RHI-FINITE-GRAPHICS-CONTENT-SOURCE-PROFILE";
        const auto* frozen =
            std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                &elements[2].resourceOwner);
        const auto& original =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(sampled_[1]);
        if (!frozen || !core::SharedOwnerProjection::sameOwner(*frozen, original))
            return "RHI-FINITE-GRAPHICS-CONTENT-SOURCE-OWNER";
        const auto& declaration = elements[2].declaration;
        if (declaration.sourceIndex != 4u || declaration.write.resource != original->resource())
            return "RHI-FINITE-GRAPHICS-CONTENT-SOURCE-POSITION";
        if (const auto* id = rhi::OriginalFiniteGraphicsContentPlanContract::validateSampledUse(
                original->originalParentLease()->originalObject()->desc(),
                original->originalObject()->desc(),
                declaration.write.resourceState,
                declaration.stages,
                declaration.access,
                rhi::OriginalFiniteGraphicsContentPlanContract::SampledOrigin::RenderBinding))
            return id;
        const auto& completed = std::get<5>(*prepared_);
        if (!completed || initial_->diagnostic() == nullptr || attachmentInitial_->diagnostic())
            return "RHI-FINITE-GRAPHICS-CONTENT-SOURCE-STAMP";
        if (const auto* id =
                original->originalParentLease()->originalObject()->validateOriginalContent(
                    *completed))
            return id;
        using Issuer = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
        return Issuer::validateOriginalCompletedShaderTextureInput(std::get<4>(*prepared_),
            std::get<1>(*prepared_),
            *completed,
            {1u, true, 2u, 2u, 20u, 5u},
            std::get<3>(*prepared_));
    }

    void verifyAssociation(
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const rhi::GpuDevice& device)
    {
        require(validateAssociation(descriptor, device));
        auto forged = std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>(
            attachmentQuota_, descriptor.get());
        assert(validateAssociation(forged, device));
        forged.reset();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{attachmentQuota_, descriptor->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    return validateAssociation(descriptor, device);
                }));
        });
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCE5_ASSOCIATION_OK original-RenderPipeline8-set9 Fragment-sampled4 completed-upload-root-custody stale-initial-refused alias-owner-refused attachment-uninitialized hot0-query0 native-draw-closed");
    }

    void verifyCapturedContent() const
    {
        const auto& sampled =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(sampled_[0])
                ->originalObject();
        const auto& attachment =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(attachment_[0])
                ->originalObject();
        const auto sampledState = sampled->observeOriginalCommittedState(
            rhi::defaultTextureSubresourceRange(sampled->desc()));
        const auto attachmentState = attachment->observeOriginalCommittedState(
            rhi::defaultTextureSubresourceRange(attachment->desc()));
        assert(sampledState && sampledState->state() == rhi::ResourceState::CopyDst
            && attachmentState && attachmentState->state() == rhi::ResourceState::Undefined);
        const auto& completed = std::get<5>(*prepared_);
        require(sampled->validateOriginalContent(*completed));
        require(attachmentInitial_->diagnostic());
        assert(attachmentInitial_->state()
                == rhi::common::OriginalTextureContentControl::State::Uninitialized
            && !attachmentInitial_->contentGeneration());
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_INITIALIZED_CAPTURE_CONTENT_OK sampled-completed-unchanged state-CopyDst attachment-uninitialized-generation0 state-Undefined no-root-clear-draw-output");
    }

    void verifyPendingContent() const
    {
        const auto& sampled =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(sampled_[0])
                ->originalObject();
        const auto& attachment =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(attachment_[0])
                ->originalObject();
        require(sampled->validateOriginalContent(*std::get<5>(*prepared_)));
        assert(!attachment->observeOriginalContent(
            rhi::defaultTextureSubresourceRange(attachment->desc())));
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_PENDING_OK sampled-completed-stamp-current attachment-private-proposal original-journal no-native-completion");
    }

    void verifyRollbackContent()
    {
        const auto& attachment =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(attachment_[0])
                ->originalObject();
        auto restored = attachment->observeOriginalContent(
            rhi::defaultTextureSubresourceRange(attachment->desc()));
        assert(restored && attachmentInitial_->diagnostic()
            && restored->revision() > attachmentInitial_->revision()
            && restored->state() == rhi::common::OriginalTextureContentControl::State::Uninitialized
            && !restored->contentGeneration());
        // Rollback restores values while advancing revision. Recapture only an
        // observation, preserving stale-stamp refusal and no completion authority.
        attachmentInitial_.emplace(*restored);
        verifyCapturedContent();
    }

    void verifyCommittedContent() const
    {
        using Readback = OriginalGeneralOutputReadbackInputsFixture;
        const auto terminal = Readback::selectedMapTerminal();
        if (terminal != Readback::MapTerminal::None
            && (Readback::mapUsesPhysicalLoss(terminal) || !Readback::mapCompletes(terminal))) {
            const auto& sampled =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(sampled_[0])
                    ->originalObject();
            const auto& attachment =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(attachment_[0])
                    ->originalObject();
            const bool lost = Readback::mapUsesPhysicalLoss(terminal);
            const auto* sampledDiagnostic =
                sampled->validateOriginalContent(*std::get<5>(*prepared_));
            if (lost)
                assert(sampledDiagnostic);
            else
                require(sampledDiagnostic);
            const auto range = rhi::defaultTextureSubresourceRange(attachment->desc());
            assert(attachmentInitial_->diagnostic()
                && !attachment->observeOriginalCommittedState(range));
            const auto content = attachment->observeOriginalContent(range);
            if (lost)
                assert(!content);
            else {
                // The downstream copy reads completed content. Cancellation
                // invalidates its state claim, without writing that content.
                assert(content && !content->diagnostic()
                    && content->state()
                        == rhi::common::OriginalTextureContentControl::State::Completed
                    && content->contentGeneration() == 1u
                    && content->revision() > attachmentInitial_->revision());
            }
            std::printf(
                "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_READBACK_TERMINAL_OK %s %s %s %s readback3\n",
                Readback::mapTerminalName(terminal),
                lost ? "sampled-device-loss-refused" : "sampled-stamp-current",
                lost ? "attachment-content-state-invalidated"
                     : "attachment-state-invalidated-content-retained",
                Readback::mapCompletes(terminal) ? "cpu-published-before-loss"
                                                 : "unpublished-no-retry");
            return;
        }
        const bool readback = EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-output-readback");
            // clang-format on
        });
        const auto& sampled =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(sampled_[0])
                ->originalObject();
        const auto& attachment =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(attachment_[0])
                ->originalObject();
        require(sampled->validateOriginalContent(*std::get<5>(*prepared_)));
        const auto range = rhi::defaultTextureSubresourceRange(attachment->desc());
        const auto state = attachment->observeOriginalCommittedState(range);
        const auto content = attachment->observeOriginalContent(range);
        assert(state
            && state->state()
                == (readback ? rhi::ResourceState::CopySrc : rhi::ResourceState::ColorAttachment)
            && content && !content->diagnostic() && attachmentInitial_->diagnostic()
            && content->state() == rhi::common::OriginalTextureContentControl::State::Completed
            && content->contentGeneration() == 1u
            && content->revision() > attachmentInitial_->revision());
        std::puts(readback
                ? "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_COMMITTED_OK sampled-stamp-current attachment-CopySrc Completed generation1 stale-initial-refused readback3"
                : "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_COMMITTED_OK sampled-stamp-current attachment-ColorAttachment Completed generation1 stale-initial-refused no-readback");
    }

    static bool failedSubmission()
    {
        return EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                const terminal = p.get("general-graphics-submission-terminal");
                const recording = p.get("general-graphics-recording-terminal");
                return p.has("general-graphics-physical-loss") || recording === "retired" ||
                       recording === "shutdown" ||
                       (p.has("general-graphics-native-submission") && terminal !== null &&
                        terminal !== "reverse-scopes");
            // clang-format on
        });
    }

    void verifyFailedContent(bool lifetimeTerminated = false) const
    {
        const auto& sampled =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(sampled_[0])
                ->originalObject();
        const auto& attachment =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(attachment_[0])
                ->originalObject();
        const auto* sampledId = sampled->validateOriginalContent(*std::get<5>(*prepared_));
        if (lifetimeTerminated)
            assert(sampledId);
        else
            require(sampledId);
        const auto range = rhi::defaultTextureSubresourceRange(attachment->desc());
        assert(attachmentInitial_->diagnostic() && !attachment->observeOriginalContent(range)
            && !attachment->observeOriginalCommittedState(range));
        std::puts(lifetimeTerminated
                ? "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_FAILED_OK sampled-lifetime-refused attachment-content-state-invalidated no-completion no-readback"
                : "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_FAILED_OK sampled-stamp-current attachment-content-state-invalidated no-completion no-readback");
    }

    void verifyTerminal(
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const rhi::GpuDevice& device)
    {
        verifyTerminal(descriptor, &device, false);
    }

    void verifyTerminal(
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const rhi::GpuDevice* device,
        bool lifetimeTerminated)
    {
        assert(rhi::OriginalCommandDescriptorSetSource::validateOriginal(descriptor));
        if (device)
            assert(validateAssociation(descriptor, *device));
        else
            assert(lifetimeTerminated);
        if (failedSubmission()) {
            verifyFailedContent(lifetimeTerminated);
            std::puts(lifetimeTerminated
                    ? "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCE5_SUBMISSION_FAILED_TERMINAL_OK descriptor-lifetime-refused sampled-lifetime-refused attachment-invalidated"
                    : "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCE5_SUBMISSION_FAILED_TERMINAL_OK descriptor-reset-refused sampled-completed-unchanged attachment-invalidated");
            return;
        }
        if (EM_ASM_INT({
                // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-submission");
                // clang-format on
            })) {
            verifyCommittedContent();
            if (EM_ASM_INT({
                    // clang-format off
                    return new URLSearchParams(location.search).has("general-graphics-output-terminal");
                    // clang-format on
                }))
                std::puts(
                    "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCE5_READBACK_TERMINAL_OK descriptor-reset-refused original-terminal-content-revalidated");
            else
                std::puts(
                    "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCE5_SUBMISSION_TERMINAL_OK descriptor-reset-refused sampled-completed-unchanged attachment-completed");
            return;
        }
        const auto& completed = std::get<5>(*prepared_);
        require(completed->diagnostic());
        require(attachmentInitial_->diagnostic());
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCE5_TERMINAL_OK descriptor-reset-refused sampled-completed-unchanged attachment-uninitialized no-publication");
    }

    void retire()
    {
        attachmentInitial_.reset();
        initial_.reset();
        sampled_ = {};
        attachment_ = {};
        prepared_.reset();
        upload_->release();
        upload_.reset();
        for (std::size_t i = 0u; i < quotas_.size(); ++i)
            if (i != 5u && i != 11u)
                assert(!quotas_[i]->liveBytes());
        assert(!attachmentQuota_->liveBytes());
        if (failedSubmission())
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_FAILED_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-invalidated no-readback");
        else if (EM_ASM_INT({
                     // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-output-terminal");
                     // clang-format on
                 }))
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_READBACK_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero terminal-revalidated readback3");
        else if (EM_ASM_INT({
                     // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-submission");
                     // clang-format on
                 }))
            std::puts(EM_ASM_INT({
                // clang-format off
                    return new URLSearchParams(location.search).has("general-graphics-output-readback");
                // clang-format on
            })
                    ? "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-completed readback3"
                    : "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-completed no-readback");
        else
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-never-completed no-root-draw-output");
    }

private:
    Initializer::Quotas quotas_;
    std::shared_ptr<core::CpuMemoryQuota> attachmentQuota_;
    std::optional<Initializer::Context> upload_;
    std::optional<Prepared> prepared_;
    std::optional<rhi::GpuTexture::ContentExpectation> initial_;
    std::optional<rhi::GpuTexture::ContentRangeExpectation> attachmentInitial_;
    std::array<Sources::Source, 2u> sampled_, attachment_;
#endif
};
} // namespace arp::test
