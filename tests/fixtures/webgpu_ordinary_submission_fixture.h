#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_recorded_command_proof_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
template <class Domain> class WebGpuOrdinarySubmissionFixture final {
public:
    static void verifyContract()
    {
        WebGpuRecordedCommandProofFixture::verifyContract();
        using namespace rhi_webgpu;
        using Submission = WebGpuOrdinarySubmission;
        WebGpuOrdinarySubmissionBudget budget;
        assert(std::string_view(Submission::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-SUBMISSION-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Submission::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-SUBMISSION-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Submission::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-SUBMISSION-BUDGET");
        budget = {1u, true, 1u, Submission::callbackCount, metadataBytes};
        assert(!Submission::validateBudget(budget));
        --budget.maxCallbacks;
        assert(Submission::validateBudget(budget));
        ++budget.maxCallbacks;
        --budget.maxSubmissions;
        assert(Submission::validateBudget(budget));
        ++budget.maxSubmissions;
        const char* id = nullptr;
        assert(!Submission::prepare({}, budget, {}, id)
            && std::string_view(id) == "RHI-WEBGPU-ORDINARY-SUBMISSION-QUOTA");
        assert(std::string_view(Submission::submit({})) == "RHI-WEBGPU-ORDINARY-SUBMISSION-OWNER");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        assert(!Submission::prepare({}, budget, quota, id)
            && std::string_view(id) == "RHI-WEBGPU-ORDINARY-SUBMISSION-NATIVE-UNAVAILABLE");
        assert(!quota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class MakeRecorded>
    static void verifyActual(
        MakeRecorded&& makeRecorded, std::shared_ptr<core::CpuMemoryQuota> recordingQuota)
    {
        using namespace rhi_webgpu;
        using Submission = WebGpuOrdinarySubmission;
        const char* id = nullptr;
        auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        WebGpuOrdinaryCompletionBudget ownerBudget{1u, true, 1u, metadataBytes};
        auto owner = prepareWebGpuOrdinaryCompletionOwner(ownerBudget, ownerQuota, id);
        assert(owner);
        const auto recordingBaseline = recordingQuota->liveBytes();
        auto signalQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto makeSignal = [&] {
            auto state = WebGpuNativeCompletionState::prepareOrdinarySignal(
                {1u, true, 1u, signalQuota->limitBytes()}, signalQuota, id);
            assert(state);
            return state;
        };
        auto signal = makeSignal();
        const auto signalBytes = signalQuota->liveBytes();
        std::shared_ptr<const WebGpuRecordedCommandProof> recorded = makeRecorded(owner, signal);
        assert(recorded && recorded->hasRecordedCommand() && !recorded->diagnostic());
        assert(recorded->hasQuotaBoundSharedOwner(recorded));
        {
            const auto derived = std::dynamic_pointer_cast<const WebGpuOrdinaryRecording>(recorded);
            assert(derived && derived->hasQuotaBoundSharedOwner(derived));
            assert(!derived.owner_before(recorded) && !recorded.owner_before(derived));
        }
        WebGpuOrdinarySubmissionBudget budget{
            1u, true, 1u, Submission::callbackCount, metadataBytes};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto factory = [&] {
            return Submission::prepare(recorded, budget, quota, id);
        };
        auto expect = [&](const char* diagnostic) {
            const auto live = quota->liveBytes();
            auto result = factory();
            assert(!result && std::string_view(id) == diagnostic && quota->liveBytes() == live);
        };
        auto original = recorded;
        recorded = std::shared_ptr<const WebGpuRecordedCommandProof>(quota, recorded.get());
        expect("RHI-WEBGPU-ORDINARY-SUBMISSION-RECORDING-OWNER");
        recorded = original;
        original.reset();
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-ORDINARY-SUBMISSION-QUOTA");
        ++budget.maxCpuMetadataBytes;
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            expect("RHI-WEBGPU-ORDINARY-SUBMISSION-ALLOCATION");
        }
        auto result = factory();
        assert(result && !result->queueCompletionObserved());
        expect("RHI-WEBGPU-ORDINARY-SUBMISSION-DUPLICATE");
        auto alias = std::shared_ptr<Submission>(quota, result.get());
        assert(
            std::string_view(Submission::submit(alias)) == "RHI-WEBGPU-ORDINARY-SUBMISSION-OWNER");
        alias.reset();
        holdActualPromises();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            assert(pressure.charged() && !Submission::submit(result));
            assert(result->status() == Submission::Status::Pending
                && !result->queueCompletionObserved() && !signal->isComplete());
            assert(
                std::string_view(result->diagnostic()) == "RHI-WEBGPU-ORDINARY-SUBMISSION-PENDING");
            assert(std::string_view(Submission::submit(result))
                == "RHI-WEBGPU-ORDINARY-SUBMISSION-STATE");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
        }
        restorePromiseMethods();
        waitHeldPromises();
        recorded.reset();
        assert(owner->retained.pending() == 1u);
        releaseQueue();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return result->queueCompletionObserved();
        }));
        assert(result->status() == Submission::Status::Pending && owner->retained.pending() == 1u
            && !signal->isComplete());
        releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return result->status() == Submission::Status::Completed;
        }));
        assert(!result->diagnostic() && !owner->retained.pending() && signal->isComplete());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        result.reset();
        assert(!quota->liveBytes() && recordingQuota->liveBytes() == recordingBaseline);

        // Drop every external recorder/submission owner while real native
        // promises remain held. Callback ownership alone retains the graph.
        signal->reset();
        recorded = makeRecorded(owner, signal);
        result = factory();
        assert(result);
        holdActualPromises();
        assert(!Submission::submit(result));
        restorePromiseMethods();
        waitHeldPromises();
        std::weak_ptr<WebGpuNativeCompletionState> droppedSignal = signal;
        signal.reset();
        std::weak_ptr<Submission> dropped = result;
        result.reset();
        recorded.reset();
        assert(!droppedSignal.expired());
        assert(!dropped.expired() && owner->retained.pending() == 1u);
        releaseQueue();
        releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return dropped.expired();
        }));
        dropped.reset();
        assert(!owner->retained.pending() && !quota->liveBytes()
            && recordingQuota->liveBytes() == recordingBaseline);
        assert(droppedSignal.expired());
        droppedSignal.reset();
        assert(!signalQuota->liveBytes());

        // Reset before acceptance rejects the original graph without native work.
        signal = makeSignal();
        recorded = makeRecorded(owner, signal);
        result = factory();
        assert(result);
        signal->reset();
        EM_ASM({
            // clang-format off
                Module.arpSignalOriginalSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                Module.arpSignalSubmitCalls = 0;
                Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                    ++Module.arpSignalSubmitCalls;
                    return Module.arpSignalOriginalSubmit.call(this, buffers);
                };
            // clang-format on
        });
        const auto mutationAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(Submission::submit(result))
            == "RHI-WEBGPU-ORDINARY-RECORDING-ORIGINAL-COMMAND");
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpSignalSubmitCalls;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.queue.submit = Module.arpSignalOriginalSubmit;
                delete Module.arpSignalOriginalSubmit;
                delete Module.arpSignalSubmitCalls;
            // clang-format on
        });
        assert(result->status() == Submission::Status::Ready && !signal->isComplete()
            && !signal->canReserveSignal());
        assert(core::defaultCpuAllocator().stats().allocationCount == mutationAllocations);
        result.reset();
        recorded.reset();
        assert(signal->canReserveSignal());

        // Reset while real queue/scope promises are held cannot publish into the
        // newer signal epoch. The original submitted graph remains quarantined.
        recorded = makeRecorded(owner, signal);
        result = factory();
        assert(result);
        holdActualPromises();
        assert(!Submission::submit(result));
        restorePromiseMethods();
        waitHeldPromises();
        signal->reset();
        releaseQueue();
        releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return result->status() == Submission::Status::Unproven;
        }));
        assert(!signal->isComplete() && !signal->canReserveSignal() && owner->retained.failed());
        mutatedSignal_ = signal;
        signal.reset();
        result.reset();
        recorded.reset();
        mutatedOwner_ = owner;
        mutatedOwnerQuota_ = ownerQuota;
        owner.reset();
        ownerQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        owner = prepareWebGpuOrdinaryCompletionOwner(ownerBudget, ownerQuota, id);
        assert(owner);

        // Actual duplicate native command-buffer submission produces a real
        // validation-scope Error. WorkDone Success alone must not publish.
        signal = makeSignal();
        recorded = makeRecorded(owner, signal);
        result = factory();
        assert(result);
        EM_ASM({
            // clang-format off
                Module.arpSubmissionNativeSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                    Module.arpSubmissionNativeSubmit.call(this, [ buffers[0], buffers[0] ]);
                };
            // clang-format on
        });
        assert(!Submission::submit(result));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.queue.submit = Module.arpSubmissionNativeSubmit;
                delete Module.arpSubmissionNativeSubmit;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return result->status() == Submission::Status::Unproven;
        }));
        assert(std::string_view(result->diagnostic()) == "RHI-WEBGPU-ORDINARY-COMPLETION-UNPROVEN");
        assert(owner->retained.pending() == 1u && owner->retained.failed()
            && !owner->retained.accepting());
        assert(!signal->isComplete() && !signal->canReserveSignal());
        errorSignal_ = signal;
        signal.reset();
        expect("RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED");
        assert(
            std::string_view(Submission::submit(result)) == "RHI-WEBGPU-ORDINARY-SUBMISSION-STATE");
        std::weak_ptr<Submission> errorWeak = result;
        result.reset();
        recorded.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return errorWeak.expired();
        }));
        errorWeak.reset();
        assert(!quota->liveBytes() && recordingQuota->liveBytes() > recordingBaseline);
        errorOwner_ = owner;
        errorOwnerQuota_ = ownerQuota;
        owner.reset();

        ownerQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        ownerBudget.maxPendingRecords = 2u;
        owner = prepareWebGpuOrdinaryCompletionOwner(ownerBudget, ownerQuota, id);
        assert(owner);
        readySignal_ = makeSignal();
        recorded = makeRecorded(owner, readySignal_);
        ready_ = factory();
        assert(ready_);
        recorded.reset();
        pendingSignal_ = makeSignal();
        recorded = makeRecorded(owner, pendingSignal_);
        result = factory();
        assert(result);
        holdActualPromises();
        assert(!Submission::submit(result));
        restorePromiseMethods();
        waitHeldPromises();
        recorded.reset();
        assert(result->status() == Submission::Status::Pending);
        pending_ = std::move(result);
        pendingQuota_ = quota;
        pendingOwner_ = owner;
        pendingOwnerQuota_ = ownerQuota;
        recordingQuota_ = recordingQuota;
        recordingBaseline_ = recordingBaseline;
        signalQuota_ = signalQuota;
        std::printf("BINDLESS_RHI_ORDINARY_%s_SUBMISSION_OK %llu 4\n",
            Domain::name,
            static_cast<unsigned long long>(peak));
        std::printf("BINDLESS_RHI_ORDINARY_%s_SUBMISSION_NATIVE_ERROR_OK\n", Domain::name);
        std::printf("BINDLESS_RHI_ORDINARY_%s_SIGNAL_OK %llu\n",
            Domain::name,
            static_cast<unsigned long long>(signalBytes));
        std::printf("BINDLESS_RHI_RECORDED_PROOF_%s_OK\n", Domain::name);
    }

    static bool hasRetainedReady() noexcept { return bool(ready_); }

    static void verifyRetainedDiagnostic(const char* id)
    {
        using Submission = rhi_webgpu::WebGpuOrdinarySubmission;
        assert(ready_ && std::string_view(ready_->diagnostic()) == id);
        assert(std::string_view(Submission::submit(ready_)) == id);
        assert(ready_->status() == Submission::Status::Ready);
    }

    static void verifyActualDeviceLoss()
    {
        using Submission = rhi_webgpu::WebGpuOrdinarySubmission;
        assert(pending_ && pending_->status() == Submission::Status::DeviceLost);
        assert(ready_ && ready_->status() == Submission::Status::DeviceLost);
        ready_.reset();
        assert(errorOwner_->retained.closed() && !errorOwner_->retained.pending());
        assert(mutatedOwner_->retained.closed() && !mutatedOwner_->retained.pending());
        assert(!readySignal_->isComplete() && !pendingSignal_->isComplete()
            && !errorSignal_->isComplete() && !mutatedSignal_->isComplete());
        assert(pendingOwner_->retained.closed() && !pendingOwner_->retained.pending());
        releaseQueue();
        releaseScopes();
        std::weak_ptr<Submission> weak = pending_;
        pending_.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        assert(!pendingSignal_->isComplete() && !errorSignal_->isComplete()
            && !mutatedSignal_->isComplete());
        errorOwner_.reset();
        pendingOwner_.reset();
        mutatedOwner_.reset();
        readySignal_.reset();
        pendingSignal_.reset();
        errorSignal_.reset();
        mutatedSignal_.reset();
        assert(!signalQuota_->liveBytes() && !mutatedOwnerQuota_->liveBytes());
        signalQuota_.reset();
        mutatedOwnerQuota_.reset();
        assert(!errorOwnerQuota_->liveBytes() && !pendingOwnerQuota_->liveBytes()
            && !pendingQuota_->liveBytes());
        errorOwnerQuota_.reset();
        pendingOwnerQuota_.reset();
        pendingQuota_.reset();
        // The enclosing recording fixture still owns its original two records;
        // it verifies the aggregate recording quota after releasing those.
        assert(recordingQuota_->liveBytes() <= recordingBaseline_);
        recordingQuota_.reset();
        std::printf("BINDLESS_RHI_ORDINARY_%s_SUBMISSION_LOSS_OK\n", Domain::name);
        std::printf("BINDLESS_RHI_ORDINARY_%s_SIGNAL_LOSS_OK\n", Domain::name);
    }
#endif
private:
    friend class WebGpuOriginalTransferDependencyBatchFixture;
    friend class WebGpuOriginalBufferUploadSourceFixture;
    friend class WebGpuOriginalTextureUploadSourceFixture;
    friend class WebGpuOriginalTextureReadbackCompletionFixture;
    friend class WebGpuTextureReadbackSnapshotFixture;
    friend class WebGpuOwnedReadbackAdvancementFixture;
    static constexpr std::uint64_t metadataBytes = 65536u;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void holdActualPromises()
    {
        EM_ASM(
            {
                // clang-format off
                    Module.arpSubmissionHeld = Module.arpSubmissionHeld || {};
                    Module.arpSubmissionHeld[$0] = ({queue : [], scopes : []});
                    Module.arpSubmissionOriginalQueue = Module.preinitializedWebGPUDevice.queue.onSubmittedWorkDone;
                    Module.arpSubmissionOriginalScope = Module.preinitializedWebGPUDevice.popErrorScope;
                    Module.preinitializedWebGPUDevice.queue.onSubmittedWorkDone = function() {
                        return Module.arpSubmissionOriginalQueue.call(this).then(function(value) {
                            return new Promise(function(resolve) {
                                Module.arpSubmissionHeld[$0].queue.push(function() {
                                    resolve(value);
                                });
                            });
                        });
                    };
                    Module.preinitializedWebGPUDevice.popErrorScope = function() {
                        return Module.arpSubmissionOriginalScope.call(this).then(function(value) {
                            return new Promise(function(resolve) {
                                Module.arpSubmissionHeld[$0].scopes.push(function() {
                                    resolve(value);
                                });
                            });
                        });
                    };
                // clang-format on
            },
            Domain::graphics);
    }

    static void restorePromiseMethods()
    {
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.queue.onSubmittedWorkDone = Module.arpSubmissionOriginalQueue;
                Module.preinitializedWebGPUDevice.popErrorScope = Module.arpSubmissionOriginalScope;
                delete Module.arpSubmissionOriginalQueue;
                delete Module.arpSubmissionOriginalScope;
            // clang-format on
        });
    }

    static void waitHeldPromises()
    {
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT(
                       {
                           // clang-format off
                               return Module.arpSubmissionHeld[$0].queue.length;
                           // clang-format on
                       },
                       Domain::graphics)
                == 1
                && EM_ASM_INT(
                       {
                           // clang-format off
                               return Module.arpSubmissionHeld[$0].scopes.length;
                           // clang-format on
                       },
                       Domain::graphics)
                == 3;
        }));
    }

    static void releaseQueue()
    {
        EM_ASM(
            {
                // clang-format off
                    for (var release of Module.arpSubmissionHeld[$0].queue)
                        release();
                    Module.arpSubmissionHeld[$0].queue = [];
                // clang-format on
            },
            Domain::graphics);
    }

    static void releaseScopes()
    {
        EM_ASM(
            {
                // clang-format off
                    for (var release of Module.arpSubmissionHeld[$0].scopes)
                        release();
                    Module.arpSubmissionHeld[$0].scopes = [];
                // clang-format on
            },
            Domain::graphics);
    }

    static inline std::shared_ptr<rhi_webgpu::WebGpuOrdinarySubmission> pending_, ready_;
    static inline std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> errorOwner_,
        pendingOwner_, mutatedOwner_;
    static inline std::shared_ptr<rhi_webgpu::WebGpuNativeCompletionState> readySignal_,
        pendingSignal_, errorSignal_, mutatedSignal_;
    static inline std::shared_ptr<core::CpuMemoryQuota> pendingQuota_, errorOwnerQuota_,
        pendingOwnerQuota_, recordingQuota_, signalQuota_, mutatedOwnerQuota_;
    static inline std::uint64_t recordingBaseline_ = 0u;
#endif
};
} // namespace arp::test
