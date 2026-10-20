#pragma once

#include "normalized_texture_state_creation_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_copy_pass_lowering.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"
#include "transfer_copy_projection_fixture.h"
#include "webgpu_canonical_callback_custody_fixture.h"

namespace arp::test {
class WebGpuCanonicalCopyPassLoweringFixture final {
    using Lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering;
    using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;
    using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
    using Contract = rhi::OriginalCommandCopyPassLoweringContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Live final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        std::weak_ptr<rhi::OriginalCommandStateTransaction> root;
        std::shared_ptr<const Lowering> lowering;
        std::shared_ptr<rhi_webgpu::WebGpuNativeTexturePayload> texture;
    };

    static inline core::UniquePtr<Live> live_;
#endif
public:
    static bool inspectCopyProjection(const std::shared_ptr<const Lowering>& source,
        std::size_t index,
        const rhi::TransferCopyArguments& fields) noexcept
    {
        return source && source->matchesProjectedCopyForInspection(index, fields);
    }

    static bool inspectUploadProjection(const std::shared_ptr<const Lowering>& source,
        std::size_t index,
        std::size_t command,
        std::size_t join,
        const rhi::TransferCopyArguments& fields) noexcept
    {
        return source && source->matchesProjectedUploadForInspection(index, command, join, fields);
    }

    static bool inspectReadbackProjection(const std::shared_ptr<const Lowering>& source,
        std::size_t index,
        std::size_t command,
        std::size_t join,
        const rhi::TransferCopyArguments& fields) noexcept
    {
        return source
            && source->matchesProjectedReadbackForInspection(index, command, join, fields);
    }

    static Lowering::Budget budget(std::uint32_t copies = 4u)
    {
        return {1u, true, copies, 1u, 64u, 65536u};
    }

    static void verifyContract()
    {
        using Upload = rhi::OriginalCommandInitializedBufferUploadExecutionContract;
        assert(!Upload::validateWork(2u, 0u));
        is(Upload::validateWork(0u, 0u), "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-MISSING");
        is(Upload::validateWork(2u, 1u), "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-TEXTURE-PROFILE");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = arp::rhi::semantics::
                OriginalCommandInitializedBufferUploadExecutionLowering::lowering(backend);
            assert(d.diagnostic && d.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && d.executionAvailable == d.recordingAvailable);
        }
        using TextureUpload = rhi::OriginalCommandInitializedTextureUploadExecutionContract;
        assert(!TextureUpload::validateWork(2u));
        is(TextureUpload::validateWork(0u), "RHI-ORIGINAL-INITIALIZED-TEXTURE-UPLOAD-MISSING");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = arp::rhi::semantics::
                OriginalCommandInitializedTextureUploadExecutionLowering::lowering(backend);
            assert(d.diagnostic && d.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && d.executionAvailable == d.recordingAvailable);
        }
        NormalizedTextureStateCreationFixture::verify();
        TransferCopyProjectionFixture::verify();
        auto b = budget();
        assert(!Contract::validateIntent(b));
        b.contractVersion = 0u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-COPY-PASS-LOWERING-VERSION");
        b = budget();
        b.prototypeOptIn = false;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-COPY-PASS-LOWERING-OPT-IN");
        b = budget();
        b.maxCopyOperations = 0u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-COPY-PASS-LOWERING-BUDGET");
        b = {3u, true, 0u, 0u, 64u, 65536u, 0u};
        b.maxUploadCopyOperations = 2u;
        b.maxBufferUploadCommands = 2u;
        b.maxBufferUploadDeclaredBytes = 32u;
        b.uploadStaging = {1u, true, 32u, 0u, 32u};
        b.uploadWork = {1u, true, 0u, 2u, 0u};
        assert(!Contract::validateIntent(b));
        for (const auto version : {1u, 2u}) {
            auto forbidden = b;
            forbidden.contractVersion = version;
            is(Contract::validateIntent(forbidden),
                "RHI-ORIGINAL-COPY-PASS-LOWERING-UPLOAD-PROFILE");
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandCopyPassLowering::lowering(backend, 3u);
            assert(d.diagnostic && !d.recordingAvailable && !d.executionAvailable
                && d.nativeProjectionAvailable == (backend == rhi::Backend::WebGPU));
        }
        rhi::RenderPassDesc pass;
        b = budget();
        is(Contract::validatePassShape(pass, b), "RHI-ORIGINAL-COPY-PASS-LOWERING-EMPTY-PASS");
        pass.colorAttachments.push_back({});
        assert(!Contract::validatePassShape(pass, b));
        pass.layeredRendering = true;
        is(Contract::validatePassShape(pass, b),
            "RHI-ORIGINAL-COPY-PASS-LOWERING-SUBPASS-UNPROVEN");
        pass.layeredRendering = false;
        pass.subpasses.push_back({});
        is(Contract::validatePassShape(pass, b),
            "RHI-ORIGINAL-COPY-PASS-LOWERING-SUBPASS-UNPROVEN");
        pass.subpasses.clear();
        pass.subpassDependencies.push_back({});
        is(Contract::validatePassShape(pass, b),
            "RHI-ORIGINAL-COPY-PASS-LOWERING-SUBPASS-UNPROVEN");
        pass.subpassDependencies.clear();
        pass.colorAttachments.push_back({});
        is(Contract::validatePassShape(pass, b), "RHI-ORIGINAL-COPY-PASS-LOWERING-COLOR-CAPACITY");
        rhi::TextureSubresourceRange range{0u, 2u, 0u, 1u, rhi::TextureAspect::Color};
        rhi::ResourceBarrierDesc barrier{rhi::allocateGpuResourceId(),
            range,
            rhi::ResourceState::CopyDst,
            rhi::ResourceState::ColorAttachment};
        barrier.sourceStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
        barrier.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
        barrier.destinationAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        assert(!Contract::attachmentTransition(barrier, range));
        auto bad = barrier;
        bad.textureSubresources.mipLevelCount = 1u;
        is(Contract::attachmentTransition(bad, range),
            "RHI-ORIGINAL-COPY-PASS-LOWERING-BARRIER-RANGE");
        bad = barrier;
        bad.sourceAccess = rhi::ResourceAccess::TransferRead;
        assert(Contract::attachmentTransition(bad, range));
        bad = barrier;
        bad.destinationStages = rhi::PipelineStage::Transfer;
        assert(Contract::attachmentTransition(bad, range));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = arp::rhi::semantics::OriginalCommandCopyPassLowering::lowering(backend);
            assert(d.diagnostic && !d.recordingAvailable && !d.executionAvailable);
            assert(d.nativeProjectionAvailable == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_CANONICAL_COPY_PASS_CONTRACT_OK complete-mips explicit-attachment-usage-transition owner-add2 original-recording-quota four-record-submit-gates-closed");
    }

    template <class Factory> static void verifyNullGraph(Factory&& create)
    {
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(
            create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& graph, const char* diagnostic) {
                assert(graph && !diagnostic);
                auto root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                    *context.device,
                    context.quotas[5],
                    diagnostic,
                    {1u, true, 10u, 0u, 16u, 32u});
                assert(root && !diagnostic && !root->validate() && root->journalEmpty());
                assert(root->plannedFinalCellCount() == 6u && root->plannedTransitionCount() == 1u
                    && root->plannedAccessStepCount() == 15u);
                assert(!root->nativeExecutionProven()
                    && !arp::rhi::semantics::OriginalCommandCopyPassLowering::lowering(
                        rhi::Backend::Null)
                        .nativeProjectionAvailable);
            },
            false);
        std::puts(
            "BINDLESS_RHI_CANONICAL_COPY_PASS_NULL_MULTI_MIP_OK genuine-normalized-original-graph full-plan-cells6-steps15 null-native-gate-closed");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto test = [&](auto& context,
                              auto& root,
                              std::uint32_t copies,
                              const rhi::OriginalCommandStateSequenceBudget& planBudget) {
            const char* diagnostic = nullptr;
            const auto b = budget(copies);
            const auto graph = root->originalGraph();
            const auto source =
                graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
            std::shared_ptr<const Command> command;
            std::shared_ptr<const Custody> custody;
            const auto prepare = [&] {
                auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                    context, root, 64u, diagnostic);
                assert(admission);
                command =
                    Command::prepareOriginal(admission, {1u, true, 1u, 64u, 65536u}, diagnostic);
                assert(command);
                custody = Custody::prepareOriginal(
                    command, {1u, true, 1u, 8u, 64u, 65536u, 65536u}, diagnostic);
                assert(custody);
            };
            const auto rebuild = [&] {
                custody.reset();
                command.reset();
                root.reset();
                assert(!source->originalStateTransactionCount());
                root = OriginalCommandAccessFixture::prepareOriginalPlan(
                    graph, *context.device, context.quotas[5], diagnostic, planBudget);
                assert(root);
                prepare();
            };
            prepare();
            const auto make = [&] {
                return Lowering::prepareOriginal(custody, b, diagnostic);
            };
            auto recording = context.quotas[5];
            std::uint64_t peak = 0u;
            {
                core::CpuMemoryQuotaLease marker(
                    recording, recording->peakBytes() - recording->liveBytes() + 1u);
                assert(marker.charged());
                const auto baseline = recording->liveBytes();
                auto lowered = make();
                assert(lowered);
                peak = recording->peakBytes() - baseline;
                assert(peak);
                assert(!custody->cancelOriginal());
                lowered.reset();
                assert(recording->liveBytes() == baseline);
            }
            for (const auto available : std::array<std::uint64_t, 3>{peak, peak - 1u, 0u}) {
                rebuild();
                const auto baseline = recording->liveBytes();
                {
                    core::CpuMemoryQuotaLease pressure(
                        recording, recording->limitBytes() - baseline - available);
                    assert(pressure.charged());
                    auto lowered = make();
                    assert(bool(lowered) == (available == peak));
                    if (lowered)
                        assert(!custody->cancelOriginal());
                    else
                        is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-ALLOCATION");
                }
                assert(recording->liveBytes() == baseline);
                if (available != peak) {
                    auto retry = make();
                    assert(retry);
                    assert(!custody->cancelOriginal());
                }
            }
            rebuild();
            auto invalid = b;
            --invalid.maxCopyOperations;
            assert(!Lowering::prepareOriginal(custody, invalid, diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-COPY-CAPACITY");
            invalid = b;
            --invalid.maxRetainedOwners;
            assert(!Lowering::prepareOriginal(custody, invalid, diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-OWNER-CAPACITY");
            invalid = b;
            --invalid.maxCpuMetadataBytes;
            assert(!Lowering::prepareOriginal(custody, invalid, diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-QUOTA");
            auto lowered = make();
            assert(lowered && !lowered->validate() && lowered->projectedCopyCount() == copies
                && lowered->retainedOwnerCount() == 64u);
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-DUPLICATE");
            auto forged = std::shared_ptr<const Lowering>(context.quotas[5], lowered.get());
            is(Lowering::validateOriginal(forged), "RHI-WEBGPU-CANONICAL-LOWERING-OWNER");
            forged.reset();
            auto forgedCustody = std::shared_ptr<const Custody>(context.quotas[5], custody.get());
            assert(!Lowering::prepareOriginal(forgedCustody, b, diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-OWNER");
            forgedCustody.reset();
            std::array quotas{context.quotas[0],
                context.quotas[1],
                context.quotas[2],
                context.quotas[3],
                context.quotas[4],
                context.quotas[5],
                context.nativeLossQuota,
                command->originalAdmission()->originalSubmissionQuota(),
                command->originalAdmission()->originalCallbackQuota()};
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    assert(core::SharedOwnerProjection::sameOwner(
                        lowered->originalCustody(), custody));
                    std::size_t index = 0u;
                    const auto& fields =
                        std::get<rhi::TransferCopyArguments>(context.descriptors[0].arguments);
                    assert(lowered->matchesProjectedCopyForInspection(index++, fields));
                    for (const auto& mip : context.upload.mipLayouts) {
                        rhi::BufferToTextureCopyDesc copy;
                        copy.sourceBuffer = context.upload.sourceBuffer;
                        copy.destinationTexture = context.upload.destinationTexture;
                        copy.textureSubresources = context.upload.textureSubresources;
                        copy.textureSubresources.baseMipLevel = mip.mipLevel;
                        copy.textureSubresources.mipLevelCount = 1u;
                        copy.extent = mip.extent;
                        copy.bytesPerRow = mip.bytesPerRow;
                        copy.rowsPerImage = mip.rowsPerImage;
                        copy.bufferOffsetBytes = mip.bufferOffsetBytes;
                        assert(lowered->matchesProjectedCopyForInspection(
                            index++, *rhi::TransferCopyArguments::capture(copy)));
                    }
                    for (const auto& mip : context.read.mipLayouts) {
                        rhi::TextureToBufferCopyDesc copy;
                        copy.sourceTexture = context.read.sourceTexture;
                        copy.destinationBuffer = context.read.destinationBuffer;
                        copy.textureSubresources = context.read.textureSubresources;
                        copy.textureSubresources.baseMipLevel = mip.mipLevel;
                        copy.textureSubresources.mipLevelCount = 1u;
                        copy.extent = mip.extent;
                        copy.bytesPerRow = mip.bytesPerRow;
                        copy.rowsPerImage = mip.rowsPerImage;
                        copy.bufferOffsetBytes = mip.bufferOffsetBytes;
                        assert(lowered->matchesProjectedCopyForInspection(
                            index++, *rhi::TransferCopyArguments::capture(copy)));
                    }
                    for (const auto& mip : context.copy.mipLayouts) {
                        rhi::TextureToTextureCopyDesc copy;
                        copy.sourceTexture = context.copy.sourceTexture;
                        copy.destinationTexture = context.copy.destinationTexture;
                        copy.sourceSubresources = context.copy.sourceSubresources;
                        copy.destinationSubresources = context.copy.destinationSubresources;
                        copy.sourceSubresources.baseMipLevel =
                            copy.destinationSubresources.baseMipLevel = mip.mipLevel;
                        copy.sourceSubresources.mipLevelCount =
                            copy.destinationSubresources.mipLevelCount = 1u;
                        copy.extent = mip.extent;
                        assert(lowered->matchesProjectedCopyForInspection(
                            index++, *rhi::TransferCopyArguments::capture(copy)));
                    }
                    assert(index == copies);
                    return Lowering::validateOriginal(lowered);
                }));
            lowered.reset();
            assert(!make());
            is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-DUPLICATE");
            rebuild();
            lowered = make();
            assert(lowered);
            quotas[7] = command->originalAdmission()->originalSubmissionQuota();
            quotas[8] = command->originalAdmission()->originalCallbackQuota();
            std::weak_ptr<rhi::OriginalCommandStateTransaction> retained = root;
            root.reset();
            command.reset();
            custody.reset();
            context.nativeLossObservation.reset();
            context.device.reset();
            assert(!retained.expired());
            is(lowered->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    lowered.reset();
                    assert(retained.expired());
                    retained.reset();
                    return nullptr;
                }));
            assert(!source->originalStateTransactionCount()
                && !source->originalReservedSubmissionCount()
                && !source->originalReservedCallbackTicketCount()
                && !source->originalReservedCallbackRegistrationCount());
            assert(!context.quotas[5]->liveBytes() && !quotas[7]->liveBytes()
                && !quotas[8]->liveBytes() && !context.nativeLossQuota->liveBytes());
            std::printf(
                "BINDLESS_RHI_CANONICAL_COPY_PASS_OK copies%u owner64 cold%llu actual-original-native-bindings full-mip-pitch-offset native-color-depth-resolve-contract quota-one-shot exact-less-exhausted forged-budget-refuse caller-drop-shutdown hot-allocation0 no-encoder-no-record-proof\n",
                copies,
                static_cast<unsigned long long>(peak));
        };
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(
            create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& root) {
                test(context, root, 4u, {1u, true, 10u, 0u, 8u, 10u});
            },
            5u,
            true);
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& graph, const char* diagnostic) {
                assert(graph && !diagnostic);
                const rhi::OriginalCommandStateSequenceBudget plan{1u, true, 10u, 0u, 16u, 32u};
                auto root = OriginalCommandAccessFixture::prepareOriginalPlan(
                    graph, *context.device, context.quotas[5], diagnostic, plan);
                assert(root && root->plannedFinalCellCount() == 6u
                    && root->plannedAccessStepCount() == 15u);
                test(context, root, 7u, plan);
                std::puts(
                    "BINDLESS_RHI_CANONICAL_COPY_PASS_MULTI_MIP_OK actual-original-native-resources normalized-creation-bound-control partial-view exact-mips2 cells6 steps15 no-replacement-control no-encoder");
            });
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& graph, const char* diagnostic) {
                assert(graph && !diagnostic);
                auto root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                    *context.device,
                    context.quotas[5],
                    diagnostic,
                    {1u, true, 10u, 0u, 16u, 32u});
                assert(root);
                auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                    context, root, 64u, diagnostic);
                assert(admission);
                auto command =
                    Command::prepareOriginal(admission, {1u, true, 1u, 64u, 65536u}, diagnostic);
                assert(command);
                auto custody = Custody::prepareOriginal(
                    command, {1u, true, 1u, 8u, 64u, 65536u, 65536u}, diagnostic);
                assert(custody);
                auto lowered = Lowering::prepareOriginal(custody, budget(7u), diagnostic);
                assert(lowered);
                live_ = core::makeUnique<Live>();
                live_->quotas = {context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    admission->originalSubmissionQuota(),
                    admission->originalCallbackQuota()};
                live_->root = root;
                live_->lowering = std::move(lowered);
                const auto& textureLease =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                        context.leases[3]);
                live_->texture =
                    rhi_webgpu::webGpuNativePayloadFor(*textureLease->originalObject());
                assert(live_->texture
                    && live_->texture->normalizedRecordedState().geometry().cellCount() == 2u);
                context.device.reset();
            });
#else
        (void)create;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(live_ && WebGpuAsyncWaitFixture::until([] {
            const auto* id = live_->lowering->validate();
            return id && std::string_view(id) == "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST";
        }));
        auto& live = *live_;
        assert(live.root.expired());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            live.quotas, [&]() noexcept -> const char* {
                is(live.lowering->validate(), "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST");
                assert(!live.lowering->nativeExecutionProven()
                    && !live.texture->normalizedRecordedState().active());
                live.lowering.reset();
                live.root.reset();
                live.texture.reset();
                return nullptr;
            }));
        for (const auto& quota : live.quotas)
            assert(!quota->liveBytes());
        live_.reset();
        std::puts(
            "BINDLESS_RHI_CANONICAL_COPY_PASS_LOSS_OK actual-browser-loss borrowed-native-projection-released-before-original-root wrapper-terminal-diagnostic all-nine-quotas-zero hot-allocation0 no-completion-proof");
#endif
    }
};
} // namespace arp::test
