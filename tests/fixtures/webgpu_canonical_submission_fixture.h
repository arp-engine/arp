#pragma once
#include "original_command_queue_submission_contract_fixture.h"
#include "webgpu_canonical_scoped_recording_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
class WebGpuCanonicalSubmissionFixture final {
    using Issuers = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
    using Root = rhi::OriginalCommandStateTransaction;
    using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Prepared final {
    public:
        template <class Context, class Graph> Prepared(Context& context, const Graph& graph)
        {
            const char* diagnostic = nullptr;
            root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                *context.device,
                context.quotas[5],
                diagnostic,
                {1u, true, 10u, 0u, 16u, 32u});
            assert(root);
            admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                context, root, 67u, diagnostic);
            assert(admission);
            command = rhi_webgpu::WebGpuCanonicalColdCommand::prepareOriginal(
                admission, {1u, true, 1u, 67u, 65536u}, diagnostic);
            assert(command);
            custody = Custody::prepareOriginal(
                command, {1u, true, 1u, 8u, 67u, 65536u, 65536u}, diagnostic);
            assert(custody);
            lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering::prepareOriginal(
                custody, {1u, true, 7u, 1u, 67u, 65536u}, diagnostic);
            assert(lowering);
            issuers = Issuers::prepareOriginal(
                lowering, WebGpuCanonicalIssuerPreparationFixture::budget(), diagnostic);
            assert(issuers);
            rhi::OriginalCommandScopedRecordingIntent wrongRecording{1u, true};
            wrongRecording.generalOutput = {1u, true};
            rhi::OriginalCommandQueueSubmissionIntent wrongSubmission{1u, true};
            wrongSubmission.generalOutput = {1u, true};
            is(Issuers::recordOriginal(issuers, wrongRecording),
                "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
            is(Issuers::submitOriginal(issuers, wrongSubmission),
                "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
            assert(!issuers->validate() && root->status() == Root::Status::Prepared
                && root->journalEmpty());
            queue = wgpuDeviceGetQueue(
                static_cast<WGPUDevice>(admission->originalClosure()->nativeDevice()));
            assert(queue);
            quotas = {context.quotas[0],
                context.quotas[1],
                context.quotas[2],
                context.quotas[3],
                context.quotas[4],
                context.quotas[5],
                context.nativeLossQuota,
                admission->originalSubmissionQuota(),
                admission->originalCallbackQuota()};
        }

        ~Prepared()
        {
            if (queue)
                wgpuQueueRelease(queue);
        }

        WGPUQueue queue = nullptr;

        void record()
        {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    assert(!Issuers::recordOriginal(issuers, {1u, true}));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return issuers->hasOriginalRecordedCommand();
                    }));
                    rhi::OriginalCommandQueueSubmissionIntent wrong{1u, true};
                    wrong.generalOutput = {1u, true};
                    is(Issuers::submitOriginal(issuers, wrong),
                        "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
                    assert(root->status() == Root::Status::Pending && !root->journalEmpty());
                    return nullptr;
                }));
        }

        void drop() noexcept
        {
            issuers.reset();
            lowering.reset();
            custody.reset();
            command.reset();
            admission.reset();
            root.reset();
        }

        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        std::shared_ptr<Root> root;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalColdAdmission> admission;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalColdCommand> command;
        std::shared_ptr<const Custody> custody;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalCopyPassLowering> lowering;
        std::shared_ptr<const Issuers> issuers;
        core::UniquePtr<rhi::GpuDevice> device;
    };

    static inline std::array<core::UniquePtr<Prepared>, 4> loss_;
    static inline bool alternativeScopeProbe_ = false;
    using Probe = WebGpuNativeSubmissionProbeFixture::Probe;

    static void observeMaySubmit(void* data) noexcept
    {
        const auto& p = *static_cast<Prepared*>(data);
        assert(p.root->status() == Root::Status::Pending && !p.root->journalEmpty());
        assert(!p.issuers->hasOriginalCompletedSubmission());
        is(Issuers::submitOriginal(p.issuers, {1u, true}),
            "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT");
    }

    static void instrument(WGPUDevice device, WGPUQueue queue, Probe probe, Prepared& prepared)
    {
        WebGpuNativeSubmissionProbeFixture::instrument(
            device, queue, probe, &prepared, &observeMaySubmit);
    }

    static void restore() { WebGpuNativeSubmissionProbeFixture::restore(); }

    static void submitQueueRegistrationLossProbe()
    {
        auto& p = *loss_[1];
        WebGpuNativeSubmissionProbeFixture::instrumentLoss(
            p.admission->originalClosure()->nativeDevice(), p.queue, true);
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(p.quotas, [&]() noexcept -> const char* {
                is(Issuers::submitOriginal(p.issuers, {1u, true}),
                    "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                return nullptr;
            }));
        WebGpuNativeSubmissionProbeFixture::restoreLoss();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.issuers->submissionSlotsDrainedForInspection();
        }));
        assert(p.custody->pendingOriginalRecords() == 1u
            && p.root->status() == Root::Status::Invalidated);
        std::puts(
            "BINDLESS_RHI_CANONICAL_SUBMISSION_QUEUE_REGISTRATION_RETAINED native-listeners-suppressed genuine-scopes-drained queue-lifetime-unproven requires-physical-loss");
    }
#endif
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static bool originalSubmissionSlotsDrained(const Issuers& issuers) noexcept
    {
        return issuers.submissionSlotsDrainedForInspection();
    }

    static std::uint32_t originalSubmissionSlotsAtLoss(const Issuers& issuers) noexcept
    {
        return issuers.submissionSlotsAtLossForInspection();
    }
#endif
    template <class Factory, class QueueWait, class MapRead>
    static void verify(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
        OriginalCommandQueueSubmissionContractFixture::verifyContract();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (const auto probe : {Probe::Success,
                 Probe::ThrowBefore,
                 Probe::ThrowAfter,
                 Probe::NativeError,
                 Probe::Reverse,
                 Probe::PartialScope,
                 Probe::Cancel,
                 Probe::Drop,
                 Probe::Shutdown,
                 Probe::ChangedNative}) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> finalQuotas;
            OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
                WebGpuCanonicalColdAdmissionFixture::configure,
                [&](auto& context, auto& graph, const char*) {
                    Prepared p(context, graph);
                    finalQuotas = p.quotas;
                    auto source = rhi_webgpu::webGpuNativePayloadFor(
                        *std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            context.leases[0])
                            ->originalObject());
                    auto destination = rhi_webgpu::webGpuNativePayloadFor(
                        *std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            context.leases[1])
                            ->originalObject());
                    auto texture = rhi_webgpu::webGpuNativePayloadFor(
                        *std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                            context.leases[2])
                            ->originalObject());
                    auto queue = wgpuDeviceGetQueue(source->ownerDevice);
                    std::array<std::uint8_t, 2048> pixels;
                    pixels.fill(0x5au);
                    wgpuQueueWriteBuffer(queue, source->buffer, 0u, pixels.data(), pixels.size());
                    auto write = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                    write.texture = texture->texture;
                    auto layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
                    layout.bytesPerRow = context.read.bytesPerRow;
                    layout.rowsPerImage = context.read.rowsPerImage;
                    for (const auto& mip : context.read.mipLayouts) {
                        write.mipLevel = mip.mipLevel;
                        layout.offset = mip.bufferOffsetBytes;
                        layout.bytesPerRow = mip.bytesPerRow;
                        layout.rowsPerImage = mip.rowsPerImage;
                        const WGPUExtent3D extent{
                            mip.extent.width, mip.extent.height, mip.extent.depth};
                        wgpuQueueWriteTexture(
                            queue, &write, pixels.data(), pixels.size(), &layout, &extent);
                    }
                    is(Issuers::submitOriginal(p.issuers, {}),
                        "RHI-ORIGINAL-QUEUE-SUBMISSION-VERSION");
                    is(Issuers::submitOriginal(p.issuers, {1u, false}),
                        "RHI-ORIGINAL-QUEUE-SUBMISSION-OPT-IN");
                    auto forged = std::shared_ptr<const Issuers>(p.quotas[5], p.issuers.get());
                    is(Issuers::submitOriginal(forged, {1u, true}),
                        "RHI-WEBGPU-CANONICAL-ISSUER-OWNER");
                    forged.reset();
                    is(Issuers::submitOriginal(p.issuers, {1u, true}),
                        "RHI-WEBGPU-CANONICAL-SUBMISSION-UNRECORDED");
                    p.record();
                    instrument(source->ownerDevice, queue, probe, p);
                    const auto size = source->sizeBytes;
                    std::weak_ptr<Root> retained = p.root;
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        p.quotas, [&]() noexcept -> const char* {
                            const auto* id = Issuers::submitOriginal(p.issuers, {1u, true});
                            if (probe == Probe::ThrowBefore || probe == Probe::ThrowAfter)
                                is(id, "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
                            else if (probe == Probe::PartialScope)
                                is(id, "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                            else
                                assert(!id);
                            restore();
                            if (probe == Probe::Cancel)
                                is(p.custody->cancelOriginal(),
                                    "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
                            if (probe == Probe::ChangedNative)
                                --source->sizeBytes;
                            if (probe == Probe::Shutdown)
                                context.device.reset();
                            if (probe == Probe::Drop) {
                                p.drop();
                                assert(!retained.expired());
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return retained.expired();
                                }));
                            } else {
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return !p.custody->pendingOriginalRecords();
                                }));
                                const bool success =
                                    probe == Probe::Success || probe == Probe::Reverse;
                                assert(p.issuers->hasOriginalCompletedSubmission() == success);
                                assert(p.root->status()
                                    == (success ? Root::Status::Committed
                                                : Root::Status::Invalidated));
                                assert(p.root->journalEmpty()
                                    && p.issuers->submissionSlotsDrainedForInspection());
                                if (success)
                                    assert(!p.issuers->submissionDiagnostic());
                                else
                                    assert(p.issuers->submissionDiagnostic());
                                if (probe == Probe::Success || probe == Probe::Reverse)
                                    is(Issuers::submitOriginal(p.issuers, {1u, true}),
                                        "RHI-WEBGPU-CANONICAL-SUBMISSION-ONE-SHOT");
                            }
                            source->sizeBytes = size;
                            retained.reset();
                            return nullptr;
                        }));
                    if (probe == Probe::Success || probe == Probe::Reverse) {
                        // Original read copies overwrite the destination's pixel rows;
                        // the first buffer copy retains its seeded bytes between rows.
                        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
                        desc.size = 2048u;
                        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
                        const auto readback = wgpuDeviceCreateBuffer(source->ownerDevice, &desc);
                        assert(readback);
                        const auto encoder =
                            wgpuDeviceCreateCommandEncoder(source->ownerDevice, nullptr);
                        assert(encoder);
                        wgpuCommandEncoderCopyBufferToBuffer(
                            encoder, destination->buffer, 0u, readback, 0u, desc.size);
                        const auto buffer = wgpuCommandEncoderFinish(encoder, nullptr);
                        assert(buffer);
                        wgpuQueueSubmit(queue, 1u, &buffer);
                        assert(wait(queue) && map(readback, desc.size));
                        const auto* data = static_cast<const std::uint8_t*>(
                            wgpuBufferGetConstMappedRange(readback, 0u, desc.size));
                        assert(data);
                        for (const auto& mip : context.read.mipLayouts)
                            for (std::uint32_t row = 0u; row < mip.extent.height; ++row)
                                assert(!std::memcmp(
                                    data + mip.bufferOffsetBytes + row * mip.bytesPerRow,
                                    pixels.data(),
                                    mip.extent.width * 4u));
                        assert(!std::memcmp(data + 16u, pixels.data(), 48u));
                        wgpuBufferUnmap(readback);
                        wgpuBufferRelease(readback);
                        wgpuCommandBufferRelease(buffer);
                        wgpuCommandEncoderRelease(encoder);
                        std::puts(
                            "BINDLESS_RHI_CANONICAL_SUBMISSION_OUTPUT_OK original-destination-buffer mip0-mip1 seeded-pixel-rows buffer-copy-gap actual-native-readback");
                    }
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        p.quotas, [&]() noexcept -> const char* {
                            p.drop();
                            return nullptr;
                        }));
                    wgpuQueueRelease(queue);
                    context.nativeLossObservation.reset();
                    context.device.reset();
                    graph.reset();
                    std::printf(
                        "BINDLESS_RHI_CANONICAL_SUBMISSION_OK probe%u MaySubmit-before-native same-root commit-after-queue-scopes4-drain errors-invalidated hot-allocation0\n",
                        static_cast<unsigned>(probe));
                });
            for (const auto& quota : finalQuotas)
                assert(!quota->liveBytes());
        }
        for (std::size_t index = 0u; index < loss_.size(); ++index)
            OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
                WebGpuCanonicalColdAdmissionFixture::configure,
                [&](auto& context, auto& graph, const char*) {
                    loss_[index] = core::makeUnique<Prepared>(context, graph);
                    if (index != 3u)
                        loss_[index]->record();
                    loss_[index]->device = std::move(context.device);
                });
        submitQueueRegistrationLossProbe();
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

    static void beginActualLossProbe(void (*alternativeScopeProbe)() = nullptr)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (const auto index : {0u}) {
            auto& p = *loss_[index];
            WebGpuNativeSubmissionProbeFixture::instrumentLoss(
                p.admission->originalClosure()->nativeDevice(), p.queue);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                p.quotas, [&]() noexcept -> const char* {
                    const auto* id = Issuers::submitOriginal(p.issuers, {1u, true});
                    assert(!id);
                    return nullptr;
                }));
            WebGpuNativeSubmissionProbeFixture::restoreLoss();
        }
        // The third exact root exercises a pop that throws after the genuine
        // native pop. No guessed stack recovery or replacement callback follows.
        auto& uncertain = *loss_[2];
        alternativeScopeProbe_ = alternativeScopeProbe != nullptr;
        if (alternativeScopeProbe)
            alternativeScopeProbe();
        else {
            WebGpuNativeSubmissionProbeFixture::instrumentLoss(
                uncertain.admission->originalClosure()->nativeDevice(),
                uncertain.queue,
                false,
                true);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                uncertain.quotas, [&]() noexcept -> const char* {
                    is(Issuers::submitOriginal(uncertain.issuers, {1u, true}),
                        "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                    WebGpuNativeSubmissionProbeFixture::restoreLoss();
                    return nullptr;
                }));
        }
        auto& refused = *loss_[3];
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            refused.quotas, [&]() noexcept -> const char* {
                is(Issuers::recordOriginal(refused.issuers, {1u, true}),
                    "RHI-WEBGPU-CANONICAL-SCOPE-STACK-UNPROVEN");
                assert(refused.root->status() == Root::Status::Prepared
                    && refused.root->journalEmpty());
                return nullptr;
            }));
        std::puts(
            "BINDLESS_RHI_CANONICAL_SUBMISSION_SCOPE_STACK_REFUSAL_OK genuine-pop-then-throw userdata-suppressed subsequent-canonical-recording-refused physical-replacement-required");
        std::puts(
            "BINDLESS_RHI_CANONICAL_SUBMISSION_LOSS_ARMED queue-scopes4 genuinely-issued-before-physical-destroy");
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (std::size_t index = 0u; index < loss_.size(); ++index) {
            auto& p = *loss_[index];
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !p.custody->pendingOriginalRecords();
            }));
            assert(!p.issuers->hasOriginalCompletedSubmission()
                && p.root->status() == Root::Status::Invalidated);
            const auto outstanding = p.issuers->submissionSlotsAtLossForInspection();
            if (index == 0u)
                assert(outstanding == 4u);
            if (index == 2u)
                assert(outstanding == (alternativeScopeProbe_ ? 0u : 3u));
            const auto quotas = p.quotas;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    p.drop();
                    p.device.reset();
                    return nullptr;
                }));
            loss_[index].reset();
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            std::printf(
                "BINDLESS_RHI_CANONICAL_SUBMISSION_LOSS_OK probe%zu outstanding-at-observer%u actual-loss root-invalidated callbacks-drained all-nine-quotas-zero hot-allocation0\n",
                index,
                outstanding);
        }
#endif
    }
};
} // namespace arp::test
