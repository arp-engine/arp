#pragma once
#include "webgpu_canonical_ordered_copy_attachment_metadata_fixture.h"
#include "webgpu_canonical_submission_fixture.h"

namespace arp::test {
class WebGpuCanonicalOrderedCopyAttachmentFixture final
    : public WebGpuCanonicalOrderedCopyAttachmentMetadataFixture {
    using Issuers = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Prepared final {
    public:
        template <class Context, class Graph>
        Prepared(Context& context, const Graph& graph, bool copyOnly = false, bool complete = true)
        {
            const char* diagnostic = nullptr;
            root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                *context.device,
                context.quotas[5],
                diagnostic,
                {1u, true, 18u, 0u, 32u, 128u},
                {1u, true, 1u, 5u, 10u, 8u, 30u, bytes});
            assert(root);
            auto closure = rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalLossBound(root,
                *context.device,
                WebGpuCanonicalNativeClosureFixture::resourceBudget(context, 43u),
                *context.nativeLossObservation,
                diagnostic);
            assert(closure);
            auto submission = core::makeShared<core::CpuMemoryQuota>(bytes),
                 callback = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto seal = rhi::OriginalCommandRecordingSeal::prepareOriginal(root,
                *context.device,
                {1u, true, 1u, 9u, 9u, 67u, 67u, 67u, bytes, bytes, bytes},
                submission,
                callback,
                diagnostic);
            assert(seal);
            auto b = budget();
            if (copyOnly) {
                b.maxRenderPasses = b.maxAttachmentReferences = 0u;
                b.maxCopyElements = 7u;
            }
            Admission::Requirements required;
            assert(!Admission::deriveRequirements(root, b, required));
            auto reservation = rhi::OriginalCommandLifecycleReservation::prepareOriginal(
                seal, required.capacity, diagnostic);
            assert(reservation);
            admission = Admission::prepareOriginal(closure, reservation, b, diagnostic);
            assert(admission);
            command = rhi_webgpu::WebGpuCanonicalColdCommand::prepareOriginal(
                admission, {1u, true, 1u, 67u, bytes}, diagnostic);
            assert(command);
            custody = rhi_webgpu::WebGpuCanonicalCallbackCustody::prepareOriginal(
                command, {1u, true, 1u, 8u, 67u, bytes, bytes}, diagnostic);
            assert(custody);
            lowerBudget = {
                2u, true, copyOnly ? 7u : 3u, copyOnly ? 0u : 2u, 67u, bytes, copyOnly ? 0u : 2u};
            quotas = {context.quotas[0],
                context.quotas[1],
                context.quotas[2],
                context.quotas[3],
                context.quotas[4],
                context.quotas[5],
                context.nativeLossQuota,
                submission,
                callback};
            queue = wgpuDeviceGetQueue(static_cast<WGPUDevice>(closure->nativeDevice()));
            assert(queue);
            if (!complete)
                return;
            auto make = [&] {
                return rhi_webgpu::WebGpuCanonicalCopyPassLowering::prepareOriginal(
                    custody, lowerBudget, diagnostic);
            };
            // Allocation failure cannot publish/claim the lowering. Reuse the
            // same genuine custody after the pressure is released.
            const auto live = context.quotas[5]->liveBytes();
            {
                core::CpuMemoryQuotaLease full(context.quotas[5], bytes - live);
                assert(full.charged());
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-ALLOCATION");
            }
            auto bad = lowerBudget;
            bad.contractVersion = 1u;
            bad.maxRenderPasses = 0u;
            assert(!rhi_webgpu::WebGpuCanonicalCopyPassLowering::prepareOriginal(
                custody, bad, diagnostic));
            assert(diagnostic);
            if (!copyOnly) {
                bad = lowerBudget;
                --bad.maxRenderPasses;
                assert(!rhi_webgpu::WebGpuCanonicalCopyPassLowering::prepareOriginal(
                    custody, bad, diagnostic));
                bad = lowerBudget;
                --bad.maxColorAttachments;
                assert(!rhi_webgpu::WebGpuCanonicalCopyPassLowering::prepareOriginal(
                    custody, bad, diagnostic));
            }
            lowering = make();
            if (!lowering)
                std::fprintf(stderr, "ordered lowering diagnostic %s\n", diagnostic);
            assert(lowering);
            issuers = Issuers::prepareOriginal(
                lowering, WebGpuCanonicalIssuerPreparationFixture::budget(), diagnostic);
            assert(issuers);
        }

        ~Prepared()
        {
            if (queue)
                wgpuQueueRelease(queue);
        }

        void record()
        {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    assert(!Issuers::recordOriginal(issuers, {1u, true}));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return issuers->hasOriginalRecordedCommand();
                    }));
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

        std::shared_ptr<Root> root;
        std::shared_ptr<const Admission> admission;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalColdCommand> command;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalCallbackCustody> custody;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalCopyPassLowering> lowering;
        std::shared_ptr<const Issuers> issuers;
        rhi::OriginalCommandCopyPassLoweringBudget lowerBudget;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        WGPUQueue queue = nullptr;
        core::UniquePtr<rhi::GpuDevice> device;
    };

    static inline core::UniquePtr<Prepared> loss_;
#endif
public:
    template <class Factory, class QueueWait, class MapRead>
    static void verifyNative(Factory&& create, QueueWait&& wait, MapRead&& map)
    {
        verifyMetadata(create);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(
            create, configure, [&](auto& context, auto& initial, const char*) {
                initial.reset();
                const char* diagnostic = nullptr;
                auto graph = makeGraph(context, diagnostic);
                assert(graph);
                core::UniquePtr<Prepared> p;
                const auto prepare = [&] {
                    p.reset();
                    p = core::makeUnique<Prepared>(context, graph, false, false);
                };
                const auto make = [&] {
                    auto result = rhi_webgpu::WebGpuCanonicalCopyPassLowering::prepareOriginal(
                        p->custody, p->lowerBudget, diagnostic);
                    if (result)
                        assert(!p->custody->cancelOriginal());
                    else
                        is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-ALLOCATION");
                    return result;
                };
                prepare();
                CpuQuotaFactoryFixture pressure(context.quotas[5]);
                const auto peak = pressure.measure(make);
                pressure.verifySingleUse(peak, prepare, make);
                // Failed cold candidates remain retryable with genuine custody.
                auto retry = make();
                assert(retry);
                retry.reset();
                const auto quotas = p->quotas;
                p.reset();
                graph.reset();
                context.leases = {};
                for (auto& d : context.descriptors)
                    d.arguments = rhi::TransferCopyArguments::finish();
                context.nativeLossObservation.reset();
                context.device.reset();
                for (const auto& q : quotas)
                    assert(!q->liveBytes());
                std::printf(
                    "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_COLD_OK peak%llu exact one-less exhausted original-custody-rebuilt retry all-nine-quotas-zero\n",
                    static_cast<unsigned long long>(peak));
            });
        // Actual variable ordered profiles: two clear/copy interleavings, and a
        // transfer-only seven-command/seven-expanded-copy stream with no pass.
        for (unsigned probe = 0u; probe < 5u; ++probe) {
            OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(
                create, configure, [&](auto& context, auto& initial, const char*) {
                    initial.reset();
                    const char* diagnostic = nullptr;
                    auto graph =
                        probe == 2u ? context.makeCopies() : makeGraph(context, diagnostic);
                    assert(graph);
                    Prepared p(context, graph, probe == 2u);
                    if (probe == 1u) {
                        // A genuine native attachment-usage error on the second pass
                        // must drain scopes and rollback without queue submission.
                        auto td = WGPU_TEXTURE_DESCRIPTOR_INIT;
                        td.dimension = WGPUTextureDimension_2D;
                        td.size = {4u, 4u, 1u};
                        td.format = WGPUTextureFormat_RGBA8Unorm;
                        td.mipLevelCount = td.sampleCount = 1u;
                        td.usage = WGPUTextureUsage_CopySrc;
                        const auto texture = wgpuDeviceCreateTexture(
                            static_cast<WGPUDevice>(p.admission->originalClosure()->nativeDevice()),
                            &td);
                        assert(texture);
                        const auto view = wgpuTextureCreateView(texture, nullptr);
                        assert(view);
                        EM_ASM(
                            {
                                // clang-format off
                                    const device = WebGPU.getJsObject($0);
                                    const view = WebGPU.getJsObject($1);
                                    const original = device.createCommandEncoder;
                                    Module.orderedOriginalEncoder = original;
                                    device.createCommandEncoder = function(desc) {
                                        const encoder = original.call(this, desc);
                                        const begin = encoder.beginRenderPass;
                                        let count = 0;
                                        encoder.beginRenderPass = function(pass) {
                                            if (++count === 2)
                                                pass.colorAttachments[0].view = view;
                                            return begin.call(this, pass);
                                        };
                                        return encoder;
                                    };
                                // clang-format on
                            },
                            p.admission->originalClosure()->nativeDevice(),
                            view);
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            p.quotas, [&]() noexcept -> const char* {
                                assert(!Issuers::recordOriginal(p.issuers, {1u, true}));
                                EM_ASM(
                                    {
                                        // clang-format off
                                            const device = WebGPU.getJsObject($0);
                                            device.createCommandEncoder = Module.orderedOriginalEncoder;
                                            delete Module.orderedOriginalEncoder;
                                        // clang-format on
                                    },
                                    p.admission->originalClosure()->nativeDevice());
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return !p.custody->pendingOriginalRecords();
                                }));
                                assert(!p.issuers->hasOriginalRecordedCommand());
                                is(p.issuers->recordingDiagnostic(),
                                    "RHI-WEBGPU-CANONICAL-RECORDING-NATIVE-ERROR");
                                assert(p.root->journalEmpty()
                                    && !p.issuers->hasOriginalCompletedSubmission());
                                return nullptr;
                            }));
                        wgpuTextureViewRelease(view);
                        wgpuTextureRelease(texture);
                        std::puts(
                            "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_ERROR_OK genuine-second-pass-usage-error scope-drain rollback no-submit no-completion");
                    } else {
                        p.record();
                        std::weak_ptr<Root> retained = p.root;
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            p.quotas, [&]() noexcept -> const char* {
                                assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
                                if (probe == 3u)
                                    is(p.custody->cancelOriginal(),
                                        "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
                                if (probe == 4u) {
                                    p.drop();
                                    assert(!retained.expired());
                                    assert(WebGpuAsyncWaitFixture::until([&] {
                                        return retained.expired();
                                    }));
                                } else {
                                    assert(WebGpuAsyncWaitFixture::until([&] {
                                        return !p.custody->pendingOriginalRecords();
                                    }));
                                    assert(p.issuers->hasOriginalCompletedSubmission()
                                        == (probe != 3u));
                                    assert(p.root->status()
                                            == (probe == 3u ? Root::Status::Invalidated
                                                            : Root::Status::Committed)
                                        && p.root->journalEmpty());
                                }
                                return nullptr;
                            }));
                        if (probe == 0u) {
                            auto destination = rhi_webgpu::webGpuNativePayloadFor(
                                *std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                                    context.leases[1])
                                    ->originalObject());
                            auto bd = WGPU_BUFFER_DESCRIPTOR_INIT;
                            bd.size = 2048u;
                            bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
                            const auto buffer =
                                wgpuDeviceCreateBuffer(destination->ownerDevice, &bd);
                            assert(buffer);
                            const auto encoder =
                                wgpuDeviceCreateCommandEncoder(destination->ownerDevice, nullptr);
                            assert(encoder);
                            wgpuCommandEncoderCopyBufferToBuffer(
                                encoder, destination->buffer, 0u, buffer, 0u, bd.size);
                            const auto commands = wgpuCommandEncoderFinish(encoder, nullptr);
                            assert(commands);
                            wgpuQueueSubmit(p.queue, 1u, &commands);
                            assert(wait(p.queue) && map(buffer, bd.size));
                            const auto* data = static_cast<const std::uint8_t*>(
                                wgpuBufferGetConstMappedRange(buffer, 0u, bd.size));
                            assert(data);
                            for (std::size_t row = 0u; row < 4u; ++row)
                                for (std::size_t x = 0u; x < 4u; ++x) {
                                    const auto first = row * 256u + x * 4u, second = 1024u + first;
                                    assert(data[first] == 255u && data[first + 1u] == 0u
                                        && data[first + 2u] == 0u && data[first + 3u] == 255u);
                                    assert(data[second] == 0u && data[second + 1u] == 255u
                                        && data[second + 2u] == 0u && data[second + 3u] == 255u);
                                }
                            wgpuBufferUnmap(buffer);
                            wgpuBufferRelease(buffer);
                            wgpuCommandBufferRelease(commands);
                            wgpuCommandEncoderRelease(encoder);
                            std::puts(
                                "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_OUTPUT_OK commands18 copies3 passes2 clear-red copy-red clear-green copy-green original-native-buffer exact-pixels");
                        } else if (probe == 2u)
                            std::puts(
                                "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_TRANSFER_ONLY_OK commands7 expanded-copy7 passes0 genuine-queue-scopes same-root-commit");
                    }
                    if (probe >= 3u)
                        std::printf(
                            "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_LIFECYCLE_OK probe%u commands18 genuine-queue quarantine-or-caller-drop drained no-early-release hot-allocation0\n",
                            probe);
                    const auto quotas = p.quotas;
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        quotas, [&]() noexcept -> const char* {
                            p.drop();
                            return nullptr;
                        }));
                    graph.reset();
                    context.leases = {};
                    for (auto& d : context.descriptors)
                        d.arguments = rhi::TransferCopyArguments::finish();
                    context.nativeLossObservation.reset();
                    context.device.reset();
                    for (const auto& q : quotas)
                        assert(!q->liveBytes());
                });
        }
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(
            create, configure, [&](auto& context, auto& initial, const char*) {
                initial.reset();
                const char* diagnostic = nullptr;
                auto graph = makeGraph(context, diagnostic);
                assert(graph);
                loss_ = core::makeUnique<Prepared>(context, graph);
                loss_->record();
                loss_->device = std::move(context.device);
            });
#else
        (void)wait;
        (void)map;
#endif
    }

    static void beginActualLossProbe()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto& p = *loss_;
        EM_ASM(
            {
                // clang-format off
                    const device = WebGPU.getJsObject($0);
                    const queue = WebGPU.getJsObject($1);
                    const state = ({device, queue, pop : device.popErrorScope, done : queue.onSubmittedWorkDone});
                    Module.orderedLossDelay = state;
                    const delay = function(promise) {
                        return promise.then(function(value) {
                            return device.lost.then(function() {
                                return value;
                            });
                        });
                    };
                    device.popErrorScope = function() {
                        return delay(state.pop.call(this));
                    };
                    queue.onSubmittedWorkDone = function() {
                        return delay(state.done.call(this));
                    };
                // clang-format on
            },
            p.admission->originalClosure()->nativeDevice(),
            p.queue);
        assert(!Issuers::submitOriginal(p.issuers, {1u, true}));
        EM_ASM({
            // clang-format off
                const p = Module.orderedLossDelay;
                p.device.popErrorScope = p.pop;
                p.queue.onSubmittedWorkDone = p.done;
                delete Module.orderedLossDelay;
            // clang-format on
        });
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto& p = *loss_;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return !p.custody->pendingOriginalRecords();
        }));
        assert(p.root->status() == Root::Status::Invalidated && p.root->journalEmpty()
            && !p.issuers->hasOriginalCompletedSubmission());
        assert(WebGpuCanonicalSubmissionFixture::originalSubmissionSlotsAtLoss(*p.issuers) == 4u);
        const auto quotas = p.quotas;
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                p.drop();
                p.device.reset();
                return nullptr;
            }));
        loss_.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_LOSS_OK actual-loss four-outstanding-receipts root-invalidated callbacks-drained all-nine-quotas-zero hot-allocation0");
#endif
    }
};
} // namespace arp::test
