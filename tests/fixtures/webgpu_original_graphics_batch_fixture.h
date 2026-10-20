#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "webgpu_async_wait_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalGraphicsBatchFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOriginalGraphicsRecordingBudget budget;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(budget))
            == "RHI-WEBGPU-ORIGINAL-GRAPHICS-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(budget))
            == "RHI-WEBGPU-ORIGINAL-GRAPHICS-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(budget))
            == "RHI-WEBGPU-ORIGINAL-GRAPHICS-COMMAND-CAPACITY");
        budget.maxOriginalCommands = 8u;
        budget.native = {
            1u, true, 1u, 1u, 1u, 1u, WebGpuOrdinaryRecording::callbackCount, 1u, 65536u};
        assert(!WebGpuOrdinaryRecording::validateBudget(budget));
        assert(std::string_view(WebGpuOrdinaryRecording::recordOriginalGraphics({}))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        assert(
            !WebGpuOrdinaryRecording::prepareOriginalGraphics({}, {}, {}, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-GRAPHICS-NATIVE-UNAVAILABLE");
        assert(!quota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyRejected(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates>& states,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange>& range,
        const char* expected)
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const WebGpuOriginalGraphicsRecordingBudget budget{1u,
            true,
            9u,
            {1u,
                true,
                1u,
                1u,
                1u,
                1u,
                WebGpuOrdinaryRecording::callbackCount,
                1u,
                quota->limitBytes()}};
        EM_ASM({
            // clang-format off
                Module.arpGraphicsRejectedEncoders = 0;
                Module.arpGraphicsRejectedCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                    ++Module.arpGraphicsRejectedEncoders;
                    return Module.arpGraphicsRejectedCreate.call(this, desc);
                };
            // clang-format on
        });
        assert(!WebGpuOrdinaryRecording::prepareOriginalGraphics(
            states, range, {}, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == expected && !quota->liveBytes());
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpGraphicsRejectedEncoders;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpGraphicsRejectedCreate;
                delete Module.arpGraphicsRejectedCreate;
            // clang-format on
        });
        std::printf(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_REJECT_OK %s encoder0 reservation0\n", expected);
    }

    static void verify(const std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs>& inputs,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange>& range)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        using States = WebGpuOriginalResourceStates;
        using Submission = WebGpuOrdinarySubmission;
        const char* diagnostic = nullptr;
        const auto& target = inputs->scalars()->usage()->projection()->resources()->resources()[0];
        auto texture = target.backing().texture;
        WebGpuOrdinaryPhysicalGroups::OriginalBufferBinding binding;
        assert(!WebGpuOrdinaryPhysicalGroups::originalBufferBinding(range->originalGroups(),
            range->facts().target.group,
            range->facts().target.binding,
            binding));
        auto buffer = binding.resource->backing().buffer;
        const auto initialTexture = texture->recordedState().state(),
                   initialBuffer = buffer->recordedState().state();
        assert(!texture->recordState(ResourceState::ColorAttachment)
            && !buffer->recordState(ResourceState::ShaderRead));
        EM_ASM(
            {
                // clang-format off
                    Module.preinitializedWebGPUDevice.queue.writeBuffer(WebGPU.getJsObject($0), 0,
                                                                        new Uint32Array([ $1 ]));
                // clang-format on
            },
            buffer->buffer,
            range->facts().fragment.replacement);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto recordQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto submitQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto signalQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        std::shared_ptr<WebGpuNativeCompletionState> signal;
        const auto snapshot = [&] {
            return States::prepareOriginal(
                inputs, {1u, true, 2u, 2u, stateQuota->limitBytes()}, stateQuota, diagnostic);
        };
        auto states = snapshot();
        assert(states);
        auto owner = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 1u, ownerQuota->limitBytes()}, ownerQuota, diagnostic);
        assert(owner);
        const WebGpuOriginalGraphicsRecordingBudget budget{1u,
            true,
            8u,
            {1u, true, 1u, 1u, 1u, 1u, Recording::callbackCount, 1u, recordQuota->limitBytes()}};
        const auto prepare = [&] {
            return Recording::prepareOriginalGraphics(
                states, range, owner, budget, recordQuota, diagnostic, signal);
        };
        EM_ASM({
            // clang-format off
                Module.arpOriginalGraphicsEvents = [];
                Module.arpOriginalGraphicsCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                    Module.arpOriginalGraphicsEvents.push("encoder");
                    var encoder = Module.arpOriginalGraphicsCreate.call(this, desc);
                    for (var method of ["beginRenderPass", "finish"]) {
                        (function(name, fn) {
                            encoder[name] = function() {
                                Module.arpOriginalGraphicsEvents.push(name);
                                var result = fn.apply(this, arguments);
                                if (name === "beginRenderPass") {
                                    Module.arpOriginalGraphicsColor = arguments[0].colorAttachments[0];
                                    for (var passMethod of ["setPipeline", "setViewport", "setScissorRect",
                                                            "setBindGroup", "draw", "end"]) {
                                        (function(passName, passFn) {
                                            result[passName] = function() {
                                                Module.arpOriginalGraphicsEvents.push(passName);
                                                if (passName === "setViewport" ||
                                                    passName === "setScissorRect" || passName === "draw")
                                                    Module["arpOriginalGraphics" + passName] =
                                                        Array.from(arguments);
                                                return passFn.apply(this, arguments);
                                            };
                                        })(passMethod, result[passMethod]);
                                    }
                                }
                                return result;
                            };
                        })(method, encoder[method]);
                    }
                    return encoder;
                };
            // clang-format on
        });
        auto shortBudget = budget;
        --shortBudget.maxOriginalCommands;
        assert(!Recording::prepareOriginalGraphics(
                   states, range, owner, shortBudget, recordQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-GRAPHICS-COMMAND-CAPACITY");
        core::MutationJournal pending;
        assert(!texture->recordState(ResourceState::ColorAttachment, &pending));
        assert(!prepare() && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        pending.rollback();
        assert(!prepare() && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        states.reset();
        states = snapshot();
        assert(states);
        CpuQuotaFactoryFixture pressure(recordQuota);
        const auto peak = pressure.measure(prepare);
        pressure.verify(peak, prepare);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalGraphicsEvents.length;
            // clang-format on
        }) == 0);
        signal = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, signalQuota->limitBytes()}, signalQuota, diagnostic);
        assert(signal);
        auto recording = prepare();
        assert(recording && recording->status() == Recording::Status::Prepared);
        auto forged = std::shared_ptr<Recording>(recordQuota, recording.get());
        assert(std::string_view(Recording::recordOriginalGraphics(forged))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
        forged.reset();
        assert(std::string_view(Recording::record(recording, 3u, 1u, 0u, 0u))
            == "RHI-WEBGPU-ORIGINAL-GRAPHICS-MODE");
        assert(std::string_view(Recording::recordOriginalCompute(recording))
            == "RHI-WEBGPU-ORIGINAL-COMPUTE-MODE");
        assert(!texture->recordState(ResourceState::ColorAttachment));
        assert(std::string_view(Recording::recordOriginalGraphics(recording))
            == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalGraphicsEvents.length;
            // clang-format on
        }) == 0);
        recording.reset();
        states.reset();
        states = snapshot();
        assert(states);
        recording = prepare();
        assert(recording);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto groupsBefore = EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        });
        {
            core::CpuMemoryQuotaScope scope(recordQuota);
            core::CpuMemoryQuotaLease exhaust(
                recordQuota, recordQuota->limitBytes() - recordQuota->liveBytes());
            assert(exhaust.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            assert(!Recording::recordOriginalGraphics(recording)
                && recording->status() == Recording::Status::RecordingPending);
            assert(
                !recording->hasRecordedCommand() && Recording::recordOriginalGraphics(recording));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalGraphicsEvents.join(",") ===
                       "encoder,beginRenderPass,setPipeline,setViewport,setScissorRect,setBindGroup,draw,end,finish";
            // clang-format on
        }) == 1);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalGraphicssetViewport.join(",") === "0,0,2,2,0,1" &&
                       Module.arpOriginalGraphicssetScissorRect.join(",") === "0,0,2,2" &&
                       Module.arpOriginalGraphicsdraw.join(",") === "3,1,0,0";
            // clang-format on
        }) == 1);
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           return Module.arpOriginalGraphicsColor.view === WebGPU.getJsObject($0) &&
                                  Module.arpOriginalGraphicsColor.loadOp === "clear" &&
                                  Module.arpOriginalGraphicsColor.storeOp === "store" &&
                                  Module.arpOriginalGraphicsColor.clearValue.r === 0.125 &&
                                  Module.arpOriginalGraphicsColor.clearValue.g === 0.375 &&
                                  Module.arpOriginalGraphicsColor.clearValue.b === 0.875;
                       // clang-format on
                   },
                   target.view()->view)
            == 1);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        }) == groupsBefore);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpOriginalGraphicsCreate;
                delete Module.arpOriginalGraphicsCreate;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand() && !recording->diagnostic());
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, submitQuota->limitBytes()},
            submitQuota,
            diagnostic);
        assert(submission);
        assert(!Submission::submit(submission) && !signal->isComplete());
        std::weak_ptr<Recording> weak = recording;
        recording.reset();
        assert(!weak.expired());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed
            && submission->queueCompletionObserved() && signal->isComplete());
        submission.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        assert(!recordQuota->liveBytes() && !submitQuota->liveBytes());
        signal.reset();
        assert(!signalQuota->liveBytes());
        // RGBA8 fixture is 2x2; WebGPU texture-copy row alignment is 256 bytes.
        EM_ASM(
            {
                // clang-format off
                    var device = Module.preinitializedWebGPUDevice;
                    var texture = WebGPU.getJsObject($0);
                    var readback = device.createBuffer(
                        {size : 512, usage : GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
                    var encoder = device.createCommandEncoder();
                    encoder.copyTextureToBuffer({texture : texture},
                                                {buffer : readback, bytesPerRow : 256, rowsPerImage : 2},
                                                {width : 2, height : 2, depthOrArrayLayers : 1});
                    device.queue.submit([ encoder.finish() ]);
                    Module.arpOriginalGraphicsReadbackDone = false;
                    readback.mapAsync(GPUMapMode.READ).then(function() {
                        Module.arpOriginalGraphicsPixels =
                            Array.from(new Uint8Array(readback.getMappedRange().slice(0)));
                        readback.unmap();
                        readback.destroy();
                        Module.arpOriginalGraphicsReadbackDone = true;
                    });
                // clang-format on
            },
            texture->texture);
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpOriginalGraphicsReadbackDone;
                // clang-format on
            }) == 1;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return ([ 0, 4, 256, 260 ]).every(function(offset) {
                    return Module.arpOriginalGraphicsPixels.slice(offset, offset + 4).join(",") ===
                           "0,255,0,255";
                });
            // clang-format on
        }) == 1);
        for (bool finishEncoder : {true, false}) {
            auto invalid = prepare();
            assert(invalid);
            EM_ASM(
                {
                    // clang-format off
                        Module.arpGraphicsInvalidCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                            var encoder = Module.arpGraphicsInvalidCreate.call(this, desc);
                            if ($0)
                                encoder.finish();
                            else {
                                var begin = encoder.beginRenderPass;
                                encoder.beginRenderPass = function(passDesc) {
                                    var pass = begin.call(this, passDesc);
                                    pass.end();
                                    return pass;
                                };
                            }
                            return encoder;
                        };
                    // clang-format on
                },
                finishEncoder);
            assert(!Recording::recordOriginalGraphics(invalid));
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpGraphicsInvalidCreate;
                    delete Module.arpGraphicsInvalidCreate;
                // clang-format on
            });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return invalid->status() != Recording::Status::RecordingPending;
            }));
            assert(
                invalid->status() == Recording::Status::Rejected && !invalid->hasRecordedCommand());
            assert(std::string_view(invalid->diagnostic())
                == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
            assert(!Submission::prepare(invalid,
                       {1u, true, 1u, Submission::callbackCount, submitQuota->limitBytes()},
                       submitQuota,
                       diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
            invalid.reset();
            assert(!recordQuota->liveBytes() && !submitQuota->liveBytes()
                && !owner->retained.pending());
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_BATCH_NATIVE_ERRORS_OK 2 no-recorded-proof no-submit");
        recording = prepare();
        assert(recording && !Recording::recordOriginalGraphics(recording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand());
        assert(!retained_);
        retained_ = recording;
        retainedStateQuota_ = stateQuota;
        retainedRecordQuota_ = recordQuota;
        retainedOwner_ = owner;
        retainedOwnerQuota_ = ownerQuota;
        auto staleSubmission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, submitQuota->limitBytes()},
            submitQuota,
            diagnostic);
        assert(staleSubmission);
        assert(!texture->recordState(initialTexture) && !buffer->recordState(initialBuffer));
        assert(std::string_view(Submission::submit(staleSubmission))
                == "RHI-WEBGPU-ORIGINAL-STATES-STALE"
            && staleSubmission->status() == Submission::Status::Ready
            && !staleSubmission->queueCompletionObserved());
        staleSubmission.reset();
        assert(!submitQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_BATCH_OK %llu 8 1 synchronous frozen scopes pixels4 submission drop allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyActualDeviceLoss()
    {
        assert(retained_ && rhi_webgpu::webGpuNativeDeviceLost()
            && retained_->status() == rhi_webgpu::WebGpuOrdinaryRecording::Status::DeviceLost);
        retained_.reset();
        retainedOwner_.reset();
        assert(!retainedStateQuota_->liveBytes() && !retainedRecordQuota_->liveBytes()
            && !retainedOwnerQuota_->liveBytes());
        retainedStateQuota_.reset();
        retainedRecordQuota_.reset();
        retainedOwnerQuota_.reset();
        std::puts("BINDLESS_RHI_ORIGINAL_GRAPHICS_BATCH_LOSS_OK source-reject reservation-retire");
    }

private:
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinaryRecording> retained_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> retainedOwner_;
    inline static std::shared_ptr<core::CpuMemoryQuota> retainedStateQuota_, retainedRecordQuota_,
        retainedOwnerQuota_;
#endif
};
} // namespace arp::test
