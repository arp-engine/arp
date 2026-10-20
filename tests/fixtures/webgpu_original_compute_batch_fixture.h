#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "webgpu_async_wait_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalComputeBatchFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOriginalComputeRecordingBudget budget;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(budget))
            == "RHI-WEBGPU-ORIGINAL-COMPUTE-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(budget))
            == "RHI-WEBGPU-ORIGINAL-COMPUTE-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(budget))
            == "RHI-WEBGPU-ORIGINAL-COMPUTE-COMMAND-CAPACITY");
        budget.maxOriginalCommands = 6u;
        assert(std::string_view(WebGpuOrdinaryRecording::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RECORDING-VERSION");
        budget.native = {
            1u, true, 1u, 1u, 1u, 1u, WebGpuOrdinaryRecording::callbackCount, 1u, 65536u};
        assert(!WebGpuOrdinaryRecording::validateBudget(budget));
        assert(std::string_view(WebGpuOrdinaryRecording::recordOriginalCompute({}))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        assert(
            !WebGpuOrdinaryRecording::prepareOriginalCompute({}, {}, {}, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-COMPUTE-NATIVE-UNAVAILABLE");
        assert(!quota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyRejected(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs>& inputs,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryScalarComputeRange>& range,
        const char* expected)
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        Groups::OriginalBufferBinding binding;
        assert(!Groups::originalBufferBinding(
            range->originalGroups(), range->physicalGroup(), range->physicalBinding(), binding));
        auto native = binding.resource->backing().buffer;
        const auto initial = native->recordedState().state();
        assert(!native->recordState(rhi::ResourceState::ShaderWrite));
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto states = WebGpuOriginalResourceStates::prepareOriginal(
            inputs, {1u, true, 1u, 1u, quota->limitBytes()}, quota, diagnostic);
        assert(states);
        const WebGpuOriginalComputeRecordingBudget budget{1u,
            true,
            7u,
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
                Module.arpOriginalRejectedEncoders = 0;
                Module.arpOriginalRejectedCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                    ++Module.arpOriginalRejectedEncoders;
                    return Module.arpOriginalRejectedCreate.call(this, desc);
                };
            // clang-format on
        });
        const auto live = quota->liveBytes();
        assert(!WebGpuOrdinaryRecording::prepareOriginalCompute(
            states, range, {}, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == expected && quota->liveBytes() == live);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalRejectedEncoders;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpOriginalRejectedCreate;
                delete Module.arpOriginalRejectedCreate;
            // clang-format on
        });
        states.reset();
        assert(!quota->liveBytes());
        assert(!native->recordState(initial));
        std::printf("BINDLESS_RHI_ORIGINAL_COMPUTE_REJECT_OK %s encoder0 reservation0\n", expected);
    }

    static void verify(const std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs>& inputs,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryScalarComputeRange>& range)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        using Submission = WebGpuOrdinarySubmission;
        const char* diagnostic = nullptr;
        Groups::OriginalBufferBinding binding;
        assert(!Groups::originalBufferBinding(
            range->originalGroups(), range->physicalGroup(), range->physicalBinding(), binding));
        auto native = binding.resource->backing().buffer;
        const auto initial = native->recordedState().state();
        assert(!native->recordState(ResourceState::ShaderWrite));
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto recordQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto submitQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto signalQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto states = WebGpuOriginalResourceStates::prepareOriginal(
            inputs, {1u, true, 1u, 1u, stateQuota->limitBytes()}, stateQuota, diagnostic);
        assert(states);
        auto owner = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 2u, ownerQuota->limitBytes()}, ownerQuota, diagnostic);
        assert(owner);
        const WebGpuOriginalComputeRecordingBudget budget{1u,
            true,
            6u,
            {1u, true, 1u, 1u, 1u, 1u, Recording::callbackCount, 1u, recordQuota->limitBytes()}};
        auto make = [&](std::shared_ptr<WebGpuNativeCompletionState> signal = {}) {
            return Recording::prepareOriginalCompute(
                states, range, owner, budget, recordQuota, diagnostic, std::move(signal));
        };
        EM_ASM({
            // clang-format off
                Module.arpOriginalBatchEvents = [];
                Module.arpOriginalBatchOriginalCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                    Module.arpOriginalBatchEvents.push("encoder");
                    var encoder = Module.arpOriginalBatchOriginalCreate.call(this, desc);
                    for (var method of ["beginComputePass", "finish"]) {
                        (function(name, fn) {
                            encoder[name] = function() {
                                Module.arpOriginalBatchEvents.push(name);
                                var result = fn.apply(this, arguments);
                                if (name === "beginComputePass") {
                                    Module.arpOriginalBatchLabel = arguments[0].label;
                                    for (var passMethod
                                             of ["setPipeline", "setBindGroup", "dispatchWorkgroups", "end"]) {
                                        (function(passName, passFn) {
                                            result[passName] = function() {
                                                Module.arpOriginalBatchEvents.push(passName);
                                                if (passName === "dispatchWorkgroups")
                                                    Module.arpOriginalBatchDispatch = Array.from(arguments);
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
        assert(!Recording::prepareOriginalCompute(
                   states, range, owner, shortBudget, recordQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-COMPUTE-COMMAND-CAPACITY");
        core::MutationJournal pendingState;
        assert(!native->recordState(ResourceState::ShaderWrite, &pendingState));
        assert(!make() && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        pendingState.rollback();
        assert(!make() && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        states.reset();
        states = WebGpuOriginalResourceStates::prepareOriginal(
            inputs, {1u, true, 1u, 1u, stateQuota->limitBytes()}, stateQuota, diagnostic);
        assert(states);
        CpuQuotaFactoryFixture pressure(recordQuota);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalBatchEvents.length;
            // clang-format on
        }) == 0);
        auto signal = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, signalQuota->limitBytes()}, signalQuota, diagnostic);
        assert(signal);
        auto recording = make(signal);
        assert(recording && recording->status() == Recording::Status::Prepared
            && owner->retained.pending() == 1u);
        auto forged = std::shared_ptr<Recording>(recordQuota, recording.get());
        assert(std::string_view(Recording::recordOriginalCompute(forged))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
        forged.reset();
        assert(std::string_view(Recording::record(recording, 1u, 1u, 1u))
            == "RHI-WEBGPU-ORIGINAL-COMPUTE-MODE");
        assert(!native->recordState(ResourceState::ShaderWrite));
        assert(std::string_view(Recording::recordOriginalCompute(recording))
            == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalBatchEvents.length;
            // clang-format on
        }) == 0);
        recording.reset();
        states.reset();
        states = WebGpuOriginalResourceStates::prepareOriginal(
            inputs, {1u, true, 1u, 1u, stateQuota->limitBytes()}, stateQuota, diagnostic);
        assert(states);
        recording = make(signal);
        assert(recording);
        const auto groupsBefore = EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        });
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(recordQuota);
            core::CpuMemoryQuotaLease exhaust(
                recordQuota, recordQuota->limitBytes() - recordQuota->liveBytes());
            assert(exhaust.charged());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            assert(!Recording::recordOriginalCompute(recording)
                && recording->status() == Recording::Status::RecordingPending);
            assert(!recording->hasRecordedCommand() && Recording::recordOriginalCompute(recording));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalBatchEvents.join(",") ===
                       "encoder,beginComputePass,setPipeline,setBindGroup,dispatchWorkgroups,end,finish";
            // clang-format on
        }) == 1);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOriginalBatchDispatch.join(",") === "1,1,1";
            // clang-format on
        }) == 1);
        const auto label = inputs->scalars()->arguments()->text(0u);
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           return Module.arpOriginalBatchLabel === UTF8ToString($0, $1);
                       // clang-format on
                   },
                   label.data(),
                   label.size())
            == 1);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        }) == groupsBefore);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpOriginalBatchOriginalCreate;
                delete Module.arpOriginalBatchOriginalCreate;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->status() == Recording::Status::Recorded && !recording->diagnostic());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, submitQuota->limitBytes()},
            submitQuota,
            diagnostic);
        assert(submission);
        assert(!Submission::submit(submission) && !signal->isComplete());
        std::weak_ptr<Recording> weakRecording = recording;
        recording.reset();
        assert(!weakRecording.expired() && owner->retained.pending() == 1u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed && signal->isComplete()
            && submission->queueCompletionObserved());
        submission.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weakRecording.expired();
        }));
        weakRecording.reset(); // Weak control block is independently quota-charged.
        assert(!recordQuota->liveBytes() && !submitQuota->liveBytes());
        signal.reset();
        assert(!signalQuota->liveBytes());
        // Real native command result, no mock completion or metadata-only success.
        EM_ASM(
            {
                // clang-format off
                    var device = Module.preinitializedWebGPUDevice;
                    var buffer = WebGPU.getJsObject($0);
                    var readback =
                        device.createBuffer({size : 20, usage : GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
                    var encoder = device.createCommandEncoder();
                    encoder.copyBufferToBuffer(buffer, 0, readback, 0, 20);
                    device.queue.submit([ encoder.finish() ]);
                    Module.arpOriginalBatchReadbackDone = false;
                    readback.mapAsync(GPUMapMode.READ).then(function() {
                        Module.arpOriginalBatchWords =
                            Array.from(new Uint32Array(readback.getMappedRange().slice(0)));
                        readback.unmap();
                        readback.destroy();
                        Module.arpOriginalBatchReadbackDone = true;
                    });
                // clang-format on
            },
            native->buffer);
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpOriginalBatchReadbackDone;
                // clang-format on
            }) == 1;
        }));
        for (std::size_t i = 0u; i < range->facts().fixedWords.size(); ++i)
            assert(static_cast<std::uint32_t>(EM_ASM_INT(
                       {
                           // clang-format off
                               return Module.arpOriginalBatchWords[$0];
                           // clang-format on
                       },
                       i))
                == range->facts().fixedWords[i]);
        auto ready = make();
        assert(ready && !Recording::recordOriginalCompute(ready));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return ready->status() != Recording::Status::RecordingPending;
        }));
        assert(ready->hasRecordedCommand() && !ready->diagnostic());
        assert(!retained_);
        retained_ = ready;
        retainedStateQuota_ = stateQuota;
        retainedRecordQuota_ = recordQuota;
        retainedOwner_ = owner;
        retainedOwnerQuota_ = ownerQuota;
        assert(!native->recordState(
            initial)); // Restored caller value invalidates the retained ready batch.
        assert(std::string_view(ready->diagnostic()) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        std::printf(
            "BINDLESS_RHI_ORIGINAL_COMPUTE_BATCH_OK %llu 6 1 synchronous frozen scopes words5 submission drop allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyActualDeviceLoss()
    {
        using namespace rhi_webgpu;
        assert(retained_ && webGpuNativeDeviceLost());
        assert(retained_->status() == WebGpuOrdinaryRecording::Status::DeviceLost);
        retained_.reset();
        retainedOwner_.reset();
        assert(!retainedStateQuota_->liveBytes() && !retainedRecordQuota_->liveBytes()
            && !retainedOwnerQuota_->liveBytes());
        retainedStateQuota_.reset();
        retainedRecordQuota_.reset();
        retainedOwnerQuota_.reset();
        std::puts("BINDLESS_RHI_ORIGINAL_COMPUTE_BATCH_LOSS_OK source-reject reservation-retire");
    }

private:
    using Groups = rhi_webgpu::WebGpuOrdinaryPhysicalGroups;
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinaryRecording> retained_;
    inline static std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> retainedOwner_;
    inline static std::shared_ptr<core::CpuMemoryQuota> retainedStateQuota_, retainedRecordQuota_,
        retainedOwnerQuota_;
#endif
};
} // namespace arp::test
