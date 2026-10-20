#include "webgpu_public_transition_admission_fixture.h"
#pragma once

#include "borrowed_texture_readback_retirement_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "webgpu_generic_recording_admission_fixture.h"

#include <cassert>
#include <cstdio>

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
namespace arp::test {

class WebGpuGenericQueueAdmissionFixture final {
    // Deliberately unprepared input proves only negative public admission.
    class UnpreparedCommand final : public rhi::CommandBuffer {
    public:
        std::uint32_t commandCount() const override { return 1u; }
    };

public:
    static void verifyActual(rhi::GpuDevice& device)
    {
        using namespace rhi_webgpu;
        auto encoder = device.createCommandEncoder({"unprepared generic queue admission"});
        assert(encoder);
        UnpreparedCommand commands;
        const auto limits = webGpuNativeDeviceLimits();
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        auto nativeStats = core::makeShared<WebGpuNativeCommandStats>();
        auto native = createWebGpuNativeCommandPayload("raw recording admission",
            limits.uniformBufferOffsetAlignment,
            limits.maxUniformBufferBindingSize,
            limits.uniformBufferOffsetAlignment,
            1u,
            nativeStats,
            false,
            diagnostic);
        auto fence = device.createFence();
        assert(native && fence && !fence->isSignaled());
        WebGpuPublicTransitionAdmissionFixture publicTransitions(device);
        BorrowedTextureReadbackRetirementFixture borrowedReadback(device);
        const auto before = inspectWebGpuRhiNativeCommandStats(device);
        const auto submits = device.queue().submitCount();
        EM_ASM({
            // clang-format off
                const queue = Module.preinitializedWebGPUDevice.queue;
                Module.arpGenericQueueOriginals = ([]);
                Module.arpGenericQueueCalls = ([]);
                const targets = ([
                    [ queue, [ 'submit', 'writeBuffer', 'writeTexture', 'onSubmittedWorkDone' ] ],
                    [
                        GPUDevice.prototype,
                        [ 'createBindGroup', 'createBuffer', 'createCommandEncoder', 'createQuerySet' ]
                    ],
                    [
                        GPUCommandEncoder.prototype,
                        [
                            'beginRenderPass', 'beginComputePass', 'copyBufferToBuffer', 'copyBufferToTexture',
                            'copyTextureToBuffer', 'copyTextureToTexture', 'clearBuffer', 'resolveQuerySet',
                            'finish'
                        ]
                    ],
                    [
                        GPURenderPassEncoder.prototype,
                        [
                            'setPipeline', 'setBindGroup', 'setVertexBuffer', 'setIndexBuffer', 'setViewport',
                            'setScissorRect', 'draw', 'drawIndexed', 'drawIndexedIndirect', 'executeBundles',
                            'end'
                        ]
                    ],
                    [
                        GPUComputePassEncoder.prototype,
                        [
                            'setPipeline', 'setBindGroup', 'dispatchWorkgroups', 'dispatchWorkgroupsIndirect',
                            'end'
                        ]
                    ],
                    [ GPUTexture.prototype, [ 'createView' ] ]
                ]);
                for (const entry of targets)
                    for (const name of entry[1]) {
                        const target = entry[0];
                        const original = target[name];
                        if (typeof original !== 'function')
                            throw new Error('missing admission instrumentation API: ' + name);
                        const slot = Module.arpGenericQueueCalls.length;
                        Module.arpGenericQueueCalls.push(0);
                        Module.arpGenericQueueOriginals.push([ target, name, original ]);
                        target[name] = function(...args) {
                            Module.arpGenericQueueCalls[slot]++;
                            return original.apply(this, args);
                        };
                    }
            // clang-format on
        });
        const auto assertPublicRecordingRejected = [&] {
            const auto result = encoder->lastValidation();
            assert(!result.success
                && result.status == rhi::CommandValidationStatus::DiagnosticFailure);
            assert(result.diagnostics == webGpuGenericRecordingUsageDiagnostic
                && !result.recordedCommandCount);
        };
        encoder->pushDebugGroup("unprepared public debug group");
        assertPublicRecordingRejected();
        encoder->popDebugGroup();
        assertPublicRecordingRejected();
        encoder->insertDebugMarker("unprepared public debug marker");
        assertPublicRecordingRejected();
        assert(!encoder->finish());
        assert(encoder->lastValidation().diagnostics == webGpuGenericRecordingUsageDiagnostic);
        publicTransitions.verify();
        borrowedReadback.verify();
        WebGpuGenericRecordingAdmissionFixture::verifyEntries(*native);
        const auto assertRejected = [&](const rhi::QueueSubmitResult& result) {
            assert(!result.success && !result.hasAcceptedWork());
            assert(result.diagnostics == webGpuGenericCompletionResourceGraphDiagnostic);
            assert(result.submittedCommandCount == commands.commandCount());
        };
        assertRejected(device.queue().submit(commands, fence.get()));
        assert(!fence->isSignaled());
        fence->signal();
        assert(fence->isSignaled());
        assertRejected(device.queue().submit(commands, fence.get()));
        assert(fence->isSignaled());
        assertRejected(device.queue().submit(commands));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!native->submit(diagnostic));
        assert(diagnostic == webGpuGenericCompletionResourceGraphDiagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        assert(!native->submit(diagnostic));
        const auto noCalls = EM_ASM_INT({
            // clang-format off
                const unchanged = Module.arpGenericQueueCalls.every(value => value === 0);
                for (const entry of Module.arpGenericQueueOriginals)
                    entry[0][entry[1]] = entry[2];
                delete Module.arpGenericQueueOriginals;
                delete Module.arpGenericQueueCalls;
                return unchanged;
            // clang-format on
        });
        assert(noCalls);
        assert(device.queue().submitCount() == submits);
        const auto after = inspectWebGpuRhiNativeCommandStats(device);
        assert(after.nativeQueueSubmitCount == before.nativeQueueSubmitCount);
        assert(after.nativeCommandRecordCount == before.nativeCommandRecordCount);
        assert(!native->finished());
        assert(
            !nativeStats->nativeCommandRecordCount && !nativeStats->nativeCommandBufferCreateCount);
        auto* extension = static_cast<rhi::BindlessCrossExtension*>(
            device.queryInterface(rhi::kExtensionBindlessCross));
        assert(extension && !extension->capabilities().executablePayload);
        assert(extension->capabilities().execution == rhi::BindlessExecution::DiagnosticFailure);
        std::puts("BINDLESS_RHI_GENERIC_QUEUE_ADMISSION_OK 0 0 0 0");
        std::puts("BINDLESS_RHI_GENERIC_RECORDING_ADMISSION_OK 47 0 0");
    }
};
} // namespace arp::test
#endif
