#include "core/diagnostics/diagnostic_error.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_prototype_batch.h"

#include <cassert>
#include <iostream>
#include <string_view>

#if defined(__EMSCRIPTEN__)
#include "tests/fixtures/webgpu_original_device_fixture.h"

#include <emscripten.h>
#endif

using namespace arp;

int main()
{
    using Contract = rhi::PrototypeCommandBatchContract;
    rhi::PrototypeCommandBatchBudget budget{
        Contract::version, true, 4, 1024 * 1024, 1024, 1000000000};
    assert(!Contract::validate(budget));
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU})
        assert(!Contract::lowering(backend, budget, {}).executable);
    auto bad = budget;
    bad.contractVersion = 1;
    assert(Contract::validate(bad));
    bad = budget;
    bad.optIn = false;
    assert(Contract::validate(bad));
    for (int i = 0; i != 5; ++i) {
        bad = budget;
        if (i == 0)
            bad.maxCommands = 0;
        if (i == 1)
            bad.maxCpuBytes = 0;
        if (i == 2)
            bad.maxRetainedGpuBytes = 0;
        if (i == 3)
            bad.timeoutNanoseconds = 0;
        if (i == 4)
            bad.timeoutNanoseconds = UINT64_MAX;
        assert(Contract::validate(bad));
    }
    auto device = rhi::createNullDevice();
    auto desc = rhi::CommandEncoderDesc{"PrototypeProfileNegative"};
    desc.prototypeBatch = budget;
    assert(!device->createCommandEncoder(desc));
    desc.prototypeBatch.reset();
    auto encoder = device->createCommandEncoder(desc);
    assert(encoder);
    auto commands = encoder->finish();
    assert(commands && !commands->ownedSubmissionFence());

    using Batch = rhi_webgpu::WebGpuPrototypeBatch;
    const auto rejects = [](auto&& operation, std::string_view diagnostic) {
        bool threw = false;
        try {
            operation();
        } catch (const core::DiagnosticError& error) {
            threw = true;
            assert(std::string_view(error.what()) == diagnostic);
        }
        assert(threw);
    };
    {
        auto state = core::makeShared<Batch::Completion>();
        Batch::Fence fence(state);
        rejects(
            [&] {
                fence.wait();
            },
            "RHI-PROTOTYPE-BATCH-NOT-SUBMITTED");
        rejects(
            [&] {
                fence.signal();
            },
            "RHI-PROTOTYPE-BATCH-HOST-SIGNAL");
        rejects(
            [&] {
                fence.reset();
            },
            "RHI-PROTOTYPE-BATCH-ONE-SHOT-FENCE");
        assert(!fence.isSignaled());
        state->accepted = true;
#if defined(__EMSCRIPTEN__)
        // Unit fixture for blocking behavior only; this is not a GPU receipt.
        emscripten_async_call(
            [](void* userdata) {
                auto& fixture = *static_cast<Batch::Completion*>(userdata);
                fixture.nativeSuccess = fixture.terminal = true;
            },
            state.get(),
            1);
        fence.wait();
        assert(state->terminal && fence.isSignaled());
#else
        rejects(
            [&] {
                fence.wait();
            },
            "RHI-PROTOTYPE-BATCH-NATIVE-UNAVAILABLE");
#endif
        state->nativeSuccess = false;
        state->accepted = state->terminal = true;
        state->diagnostic = "RHI-PROTOTYPE-BATCH-DEADLINE";
        rejects(
            [&] {
                fence.wait();
            },
            state->diagnostic);
        assert(!fence.isSignaled());
        // Value-only fence behavior, not evidence of native queue execution.
        state->nativeSuccess = true;
        fence.wait();
        rejects(
            [&] {
                fence.reset();
            },
            "RHI-PROTOTYPE-BATCH-ONE-SHOT-FENCE");
        assert(fence.isSignaled());
    }
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuBytes);
    auto registry = core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
    core::pmr::String diagnostic;
    {
        core::CpuMemoryQuotaScope scope(quota);
        Batch batch(budget, quota, registry);
        assert(!batch.fence()->isSignaled());
        assert(batch.capture(
            Batch::Op::ComputeBegin,
            [](auto&) {
                return true;
            },
            diagnostic));
        assert(!batch.capture(
            Batch::Op::Draw,
            [](auto&) {
                return true;
            },
            diagnostic));
        assert(batch.diagnostic());
        assert(!batch.capture(
            Batch::Op::ComputeEnd,
            [](auto&) {
                return true;
            },
            diagnostic));
        assert(!batch.fence()->isSignaled());
    }
    {
        core::CpuMemoryQuotaScope scope(quota);
        Batch batch(budget, quota, registry);
        for (int i = 0; i != 4; ++i)
            assert(batch.capture(
                Batch::Op::Marker,
                [](auto&) {
                    return true;
                },
                diagnostic));
        assert(!batch.capture(
            Batch::Op::Marker,
            [](auto&) {
                return true;
            },
            diagnostic));
    }
    {
        core::CpuMemoryQuotaScope scope(quota);
        Batch batch(budget, quota, registry);
        assert(!batch.capture(
            Batch::Op::PopDebug,
            [](auto&) {
                return true;
            },
            diagnostic));
    }
    {
        core::CpuMemoryQuotaScope scope(quota);
        Batch batch(budget, quota, registry);
        assert(batch.capture(
            Batch::Op::ComputeBegin,
            [](auto&) {
                return true;
            },
            diagnostic));
        assert(!batch.capture(
            Batch::Op::RenderBegin,
            [](auto&) {
                return true;
            },
            diagnostic));
    }
    {
        core::CpuMemoryQuotaScope scope(quota);
        Batch batch(budget, quota, registry);
        assert(!batch.capture(
            Batch::Op::TextureBarrier,
            [&](auto& record) {
                return batch.resource(record, {42}, nullptr, true, diagnostic);
            },
            diagnostic));
    }
    {
        core::CpuMemoryQuotaScope scope(quota);
        Batch batch(budget, quota, registry);
        assert(batch.capture(
            Batch::Op::ComputeBegin,
            [](auto&) {
                return true;
            },
            diagnostic));
        assert(!batch.capture(
            Batch::Op::ComputePipeline,
            [](auto&) {
                return true;
            },
            diagnostic));
        assert(diagnostic == "RHI-PROTOTYPE-BATCH-PIPELINE-OWNER-OR-ORDER");
    }
    {
        core::CpuMemoryQuotaScope scope(quota);
        Batch batch(budget, quota, registry);
        assert(!batch.capture(
            Batch::Op::Marker,
            [&](auto& r) {
                r.label.resize(budget.maxCpuBytes + 1);
                return true;
            },
            diagnostic));
        assert(diagnostic == "RHI-PROTOTYPE-BATCH-CPU-BUDGET");
        assert(!batch.capture(
            Batch::Op::Marker,
            [](auto&) {
                return true;
            },
            diagnostic));
    }
#if defined(__EMSCRIPTEN__)
    auto native = rhi_webgpu::createWebGpuRhiDevice(test::WebGpuOriginalDeviceFixture::describe(
        "PrototypeBatchNativeLifecycle", 16 * 1024 * 1024, 4096, 1024 * 1024));
    assert(native);
    const auto nativeCaps = native->capabilities();
    assert(nativeCaps.prototypeCommandBatch.contractVersion == Contract::version);
    assert(nativeCaps.prototypeCommandBatch.mode == rhi::PrototypeCommandBatchMode::Prototype);
    assert(!nativeCaps.features.nativeSync && !nativeCaps.commandRetention.boundedRecordingPayload);
    assert(Contract::lowering(rhi::Backend::WebGPU, budget, nativeCaps.prototypeCommandBatch)
            .executable);
    rhi::CommandEncoderDesc profile{"PrototypeBatchNativeLifecycle"};
    profile.prototypeBatch = budget;
    // Each command uses a genuine native encoder and real scope/queue receipts.
    // Dropping a finished, unsubmitted buffer must drain scopes without work.
    {
        auto e = native->createCommandEncoder(profile);
        assert(e);
        e->insertDebugMarker("discard original");
        auto c = e->finish();
        assert(c && !c->ownedSubmissionFence()->isSignaled());
        c.reset();
        e.reset();
        emscripten_sleep(0);
    }
    {
        auto e = native->createCommandEncoder(profile);
        assert(e);
        for (int i = 0; i != 5; ++i)
            e->insertDebugMarker("bounded");
        assert(!e->finish());
    }
    {
        auto e = native->createCommandEncoder(profile);
        assert(e);
        auto p = rhi_webgpu::webGpuNativePayloadFor(*e);
        assert(p);
        assert(p->preparedRecordingDiagnostic()); // unsupported raw recording poisons the batch
        e->insertDebugMarker("must stay rejected");
        assert(!e->finish());
    }
    {
        auto e = native->createCommandEncoder(profile);
        assert(e);
        e->insertDebugMarker("native one shot");
        auto c = e->finish();
        assert(c);
        auto completion = c->ownedSubmissionFence();
        assert(completion && !completion->isSignaled());
        rejects(
            [&] {
                completion->wait();
            },
            "RHI-PROTOTYPE-BATCH-NOT-SUBMITTED");
        auto rejected = native->queue().submit(*c); // exact owned fence required, no native submit
        assert(!rejected.success && !rejected.workAccepted && !completion->isSignaled());
        auto submitted = native->queue().submit(*c, completion.get());
        assert(submitted.success && submitted.workAccepted && completion->isSignaled());
        completion->wait();
        rejects(
            [&] {
                completion->reset();
            },
            "RHI-PROTOTYPE-BATCH-ONE-SHOT-FENCE");
        auto duplicate = native->queue().submit(*c, completion.get());
        assert(!duplicate.success && !duplicate.workAccepted);
    }
    {
        auto tiny = profile;
        tiny.prototypeBatch->maxRetainedGpuBytes = 1;
        auto buffer = native->createBuffer({"PrototypeGpuQuota",
            16,
            rhi::ResourceUsage::StorageBuffer,
            rhi::MemoryUsage::GpuOnly});
        assert(buffer);
        auto e = native->createCommandEncoder(tiny);
        assert(e);
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = buffer->id();
        barrier.before = rhi::ResourceState::Undefined;
        barrier.after = rhi::ResourceState::ShaderRead;
        barrier.sourceStages = rhi::PipelineStage::TopOfPipe;
        barrier.destinationStages = rhi::PipelineStage::ComputeShader;
        barrier.destinationAccess = rhi::ResourceAccess::ShaderRead;
        const auto denied = e->transition(*buffer, barrier);
        assert(!denied.success && denied.diagnostics == "RHI-PROTOTYPE-BATCH-GPU-BUDGET");
        assert(!e->finish());
    }
    {
        auto timed = profile;
        timed.prototypeBatch->timeoutNanoseconds = 1;
        // Delay delivery of the genuine native queue promise. A 1 ns authored
        // deadline alone is not deterministic with the browser clock precision.
        EM_ASM({
            // clang-format off
            const q = Module.preinitializedWebGPUDevice.queue;
            Module.arpOriginalQueueDone = q.onSubmittedWorkDone;
            q.onSubmittedWorkDone = function() {
                return Module.arpOriginalQueueDone.call(this)
                    .then(() => new Promise(resolve => setTimeout(resolve, 50)));
            };
            // clang-format on
        });
        auto e = native->createCommandEncoder(timed);
        assert(e);
        e->insertDebugMarker("accepted native work with authored immediate deadline");
        auto c = e->finish();
        assert(c);
        auto completion = c->ownedSubmissionFence();
        const auto submitted = native->queue().submit(*c, completion.get());
        assert(!submitted.success && submitted.workAccepted
            && submitted.diagnostics == "RHI-PROTOTYPE-BATCH-DEADLINE");
        EM_ASM({
            // clang-format off
            Module.preinitializedWebGPUDevice.queue.onSubmittedWorkDone = Module.arpOriginalQueueDone;
            delete Module.arpOriginalQueueDone;
            // clang-format on
        });
        rejects(
            [&] {
                completion->wait();
            },
            "RHI-PROTOTYPE-BATCH-DEADLINE");
        c.reset();
        e.reset();
        emscripten_sleep(100);
        assert(!completion->isSignaled()); // callbacks cannot convert terminal failure to success
    }
    {
        auto swapchain =
            native->createSwapchain({"#canvas", {64, 64}, rhi::TextureFormat::Bgra8Unorm, 2});
        assert(swapchain);
        auto canvasProfile = profile;
        canvasProfile.prototypeBatch->maxRetainedGpuBytes = 1024 * 1024;
        // Actual acquired images, including expiry at capture, finish, and submit.
        for (int mode = 0; mode != 5; ++mode) {
            emscripten_sleep(0);
            const auto frame = swapchain->acquireNextFrame();
            assert(frame.success);
            auto image = swapchain->acquiredImage(frame);
            assert(image.success && image.texture);
            auto e = native->createCommandEncoder(canvasProfile);
            assert(e);
            rhi::ResourceBarrierDesc barrier;
            barrier.resource = image.texture->id();
            assert(image.texture->state() == rhi::ResourceState::Undefined);
            barrier.before = image.texture->state();
            barrier.after = rhi::ResourceState::ColorAttachment;
            barrier.sourceStages = rhi::PipelineStage::TopOfPipe;
            barrier.sourceAccess = rhi::ResourceAccess::None;
            barrier.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
            barrier.destinationAccess = rhi::ResourceAccess::ColorAttachmentWrite;
            if (mode == 0)
                emscripten_sleep(0);
            const auto recorded = e->transition(*image.texture, barrier);
            if (mode == 0) {
                assert(!recorded.success
                    && recorded.diagnostics == "RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED");
                assert(!e->finish());
            } else {
                assert(recorded.success);
                if (mode == 1)
                    emscripten_sleep(0);
                auto c = e->finish();
                if (mode == 1) {
                    assert(!c);
                } else {
                    assert(c);
                    if (mode == 2)
                        emscripten_sleep(0);
                    if (mode >= 3) {
                        // Resize/surface changes already require explicit frame discard at L2.
                        assert(!swapchain->resize({32, 32}).success);
                        assert(swapchain->discardFrame(frame).success);
                        if (mode == 3)
                            assert(swapchain->resize({64, 64}).success);
                        if (mode == 4)
                            assert(swapchain->setSurfaceAvailable(false).success);
                    }
                    const auto before = native->queue().submitCount();
                    auto completion = c->ownedSubmissionFence();
                    const auto denied = native->queue().submit(*c, completion.get());
                    assert(!denied.success && !denied.workAccepted
                        && denied.diagnostics == "RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED");
                    assert(native->queue().submitCount() == before && !completion->isSignaled());
                    rejects(
                        [&] {
                            completion->wait();
                        },
                        "RHI-PROTOTYPE-BATCH-ACQUIRED-IMAGE-EXPIRED");
                }
            }
            if (mode < 3)
                assert(swapchain->discardFrame(frame).success);
            if (mode == 4)
                assert(swapchain->setSurfaceAvailable(true).success);
        }
    }
    // Genuine device loss rejects new execution and retires original callbacks.
    {
        auto e = native->createCommandEncoder(profile);
        assert(e);
        e->insertDebugMarker("device loss");
        auto c = e->finish();
        assert(c);
        auto completion = c->ownedSubmissionFence();
        EM_ASM({
            // clang-format off
            Module.preinitializedWebGPUDevice.destroy();
            // clang-format on
        });
        // Observe the genuine device-lost promise before testing loss admission.
        for (int i = 0; i != 1000 && !rhi_webgpu::webGpuNativeDeviceLost(); ++i)
            emscripten_sleep(1);
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        auto rejected = native->queue().submit(*c, completion.get());
        assert(!rejected.success && !rejected.workAccepted && !completion->isSignaled());
        assert(native->capabilities().prototypeCommandBatch.mode
            == rhi::PrototypeCommandBatchMode::DiagnosticFailure);
        rejects(
            [&] {
                completion->wait();
            },
            "RHI-PROTOTYPE-BATCH-DEVICE-LOST");
    }
    std::cout
        << "PrototypeCommandBatch/2 native capability discard capacity unsupported fence deadline lease oneShot deviceLoss passed\n";
#endif
    std::cout << "PrototypeCommandBatch/2 contract and default-path negatives passed\n";
}
