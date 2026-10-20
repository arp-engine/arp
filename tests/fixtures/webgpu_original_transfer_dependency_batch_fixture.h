#pragma once
#include "webgpu_ordinary_submission_fixture.h"
#include "webgpu_original_dependency_inputs_fixture.h"
#include "webgpu_original_transfer_batch_fixture.h"

namespace arp::test {
class WebGpuOriginalTransferDependencyBatchFixture final {
    using Recording = rhi_webgpu::WebGpuOrdinaryRecording;
    using Submission = rhi_webgpu::WebGpuOrdinarySubmission;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;
    using Transaction = rhi_webgpu::WebGpuOriginalStateTransaction;
    using Native = WebGpuOriginalTransferBatchFixture;

    struct PromiseDomain {
        static constexpr unsigned graphics = 2u;
    };

    using Promises = WebGpuOrdinarySubmissionFixture<PromiseDomain>;

public:
    static void verifyContract()
    {
        rhi_webgpu::WebGpuOriginalTransferDependencyRecordingBudget b;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-DEPENDENCY-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-DEPENDENCY-OPT-IN");
        b.prototypeOptIn = true;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-DEPENDENCY-COMMAND-CAPACITY");
        b = budget(5u);
        assert(!Recording::validateBudget(b));
        assert(std::string_view(Recording::recordOriginalTransferDependency({}))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto transactionQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        assert(!Recording::prepareOriginalTransferDependency(
                   {}, {}, b, quota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-DEPENDENCY-NATIVE-UNAVAILABLE");
        assert(!quota->liveBytes() && !transactionQuota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Snapshot> static void verify(Snapshot&& snapshot)
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        auto recordQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto transactionQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto ownerQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto submitQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        // Two independent tickets are required by the queued successor probe.
        auto owner = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 2u, metadataBytes}, ownerQuota, diagnostic);
        assert(owner);
        auto initial = snapshot();
        assert(initial);
        const auto& root = *initial->inputs()->scalars()->usage()->projection()->transfer();
        const auto copy =
            std::get<rhi::TransferCopyArguments>(initial->inputs()->descriptors()[2].arguments);
        const auto b = budget(static_cast<std::uint32_t>(initial->states().size()));
        const auto prepare = [&] {
            return Recording::prepareOriginalTransferDependency(
                snapshot(), owner, b, recordQuota, transactionQuota, diagnostic);
        };
        Native::installNativeObserver();
        CpuQuotaFactoryFixture pressure(recordQuota);
        const auto peak = pressure.measure(prepare);
        pressure.verify(peak, prepare);
        CpuQuotaFactoryFixture transactionPressure(transactionQuota);
        const auto transactionPeak = transactionPressure.measure(prepare);
        transactionPressure.verify(transactionPeak, prepare);
        assert(!transactionQuota->liveBytes() && !owner->retained.pending() && EM_ASM_INT({
            // clang-format off
                    return Module.arpOriginalTransferEvents.length;
            // clang-format on
        }) == 0);
        auto shortBudget = b;
        --shortBudget.native.maxOriginalCommands;
        assert(!Recording::prepareOriginalTransferDependency(
                   initial, owner, shortBudget, recordQuota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-DEPENDENCY-COMMAND-CAPACITY");
        shortBudget = b;
        --shortBudget.transaction.maxStateControls;
        assert(!Recording::prepareOriginalTransferDependency(
                   initial, owner, shortBudget, recordQuota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-CONTROL-CAPACITY");
        shortBudget = b;
        shortBudget.transaction.maxTransitions = 0u;
        assert(!Recording::prepareOriginalTransferDependency(
                   initial, owner, shortBudget, recordQuota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-TRANSITION-CAPACITY");
        shortBudget = b;
        --shortBudget.transaction.maxCpuMetadataBytes;
        assert(!Recording::prepareOriginalTransferDependency(
                   initial, owner, shortBudget, recordQuota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-STATE-TRANSACTION-QUOTA");
        assert(!Recording::prepareOriginalTransferDependency(
                   initial, {}, b, recordQuota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED");
        assert(!transactionQuota->liveBytes() && !recordQuota->liveBytes()
            && !owner->retained.pending()
            && EM_ASM_INT({
                   // clang-format off
                    return Module.arpOriginalTransferEvents.length;
                   // clang-format on
               })
                == 0);
        auto forgedState = std::shared_ptr<const States>(recordQuota, initial.get());
        assert(!Recording::prepareOriginalTransferDependency(
                   forgedState, owner, b, recordQuota, transactionQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-STATE-OWNER");
        forgedState.reset();
        initial.reset();
        auto recording = prepare();
        assert(recording
            && recording->originalTransaction()->status() == Transaction::Status::Prepared);
        auto forged = std::shared_ptr<Recording>(recordQuota, recording.get());
        assert(std::string_view(Recording::recordOriginalTransferDependency(forged))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
        forged.reset();
        assert(std::string_view(Recording::recordOriginalTransfer(recording))
            == "RHI-WEBGPU-ORIGINAL-TRANSFER-MODE");
        Native::seedNativeSource(root, copy);
        recordHot(recording, recordQuota);
        Native::verifyNativeArguments(root, copy);
        const auto label =
            recording->originalTransaction()->states()->inputs()->scalars()->arguments()->text(2u);
        assert(EM_ASM_INT(
                   {
                       // clang-format off
                           return Module.arpOriginalTransferEvents.length === 5 &&
                                  Module.arpOriginalTransferEvents[1] === "pushDebugGroup" &&
                                  Module.arpOriginalTransferEvents[3] === "popDebugGroup" &&
                                  Module.arpOriginalTransferLabel === UTF8ToString($0, $1);
                       // clang-format on
                   },
                   label.data(),
                   label.size())
            == 1);
        Native::restoreNativeObserver();
        waitRecorded(recording);
        auto transaction = recording->originalTransaction();
        assert(transaction->status() == Transaction::Status::Pending && !transaction->validate()
            && transaction->states()->validate());
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, metadataBytes},
            submitQuota,
            diagnostic);
        assert(submission);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Submission::submit(submission)
            && transaction->status() == Transaction::Status::Committed
            && transaction->journalEmpty());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        recording.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed
            && submission->queueCompletionObserved() && !transaction->validate());
        submission.reset();
        transaction.reset();
        assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes()
            && !submitQuota->liveBytes());
        Native::verifyNativeReadback(root, copy);
        verifySuccessor(snapshot, owner, b, recordQuota, transactionQuota, submitQuota);
        verifyFailures(snapshot, owner, b, recordQuota, transactionQuota, submitQuota);
        // The established loss observer keeps this bounded empty owner until loss.
        assert(ownerCount_ < owners_.size());
        owners_[ownerCount_++] = {owner, ownerQuota};
        std::printf(
            "BINDLESS_RHI_ORIGINAL_DEPENDENCY_BATCH_OK %u %llu 6 2 1 frozen commit readback errors drop untouched allocation0\n",
            static_cast<unsigned>(copy.kind()),
            static_cast<unsigned long long>(peak));
    }
    enum class IsolatedMode { NativeError, SignalMutation, PendingLoss, SubmittedLoss };

    static void verifyIsolated(
        std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryTransferRetention> root,
        const rhi::TransferCopyArguments& copy,
        IsolatedMode mode)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        assert(isolatedCount_ < isolated_.size());
        auto& probe = isolated_[isolatedCount_++];
        for (auto& quota : probe.quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        const auto& source = *root->find(copy.source());
        const auto& destination = *root->find(copy.destination());
        const auto sourceBarrier = makeInferredResourceBarrier(
            source.id, {}, source.buffer->recordedState().state(), ResourceState::CopySrc);
        const auto destinationBarrier = makeInferredResourceBarrier(destination.id,
            {},
            destination.buffer->recordedState().state(),
            ResourceState::CopyDst);
        WebGpuOriginalDependencyInputsFixture inputs(root,
            copy,
            CommandUsageKind::BufferBarrier,
            CommandUsageKind::BufferBarrier,
            probe.quotas[0],
            "依赖✓");
        probe.initial = inputs.prepare(
            *ResourceDependencyArguments::capture(CommandUsageKind::BufferBarrier, sourceBarrier),
            *ResourceDependencyArguments::capture(
                CommandUsageKind::BufferBarrier, destinationBarrier),
            probe.quotas[1]);
        assert(probe.initial);
        probe.owner = prepareWebGpuOrdinaryCompletionOwner(
            {1u, true, 1u, metadataBytes}, probe.quotas[4], diagnostic);
        assert(probe.owner);
        auto signal = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, metadataBytes}, probe.quotas[6], diagnostic);
        assert(signal);
        auto recording = Recording::prepareOriginalTransferDependency(probe.initial,
            probe.owner,
            budget(static_cast<std::uint32_t>(root->resources().size())),
            probe.quotas[2],
            probe.quotas[3],
            diagnostic,
            signal);
        assert(recording);
        Native::seedNativeSource(*root, copy);
        recordHot(recording, probe.quotas[2]);
        waitRecorded(recording);
        probe.transaction = recording->originalTransaction();
        probe.mode = mode;
        if (mode == IsolatedMode::PendingLoss) {
            probe.recording = recording;
            return;
        }
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, metadataBytes},
            probe.quotas[5],
            diagnostic);
        assert(submission);
        if (mode == IsolatedMode::NativeError) {
            EM_ASM({
                // clang-format off
                    Module.arpDependencySubmit = Module.preinitializedWebGPUDevice.queue.submit;
                    Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                        Module.arpDependencySubmit.call(this, [ buffers[0], buffers[0] ]);
                    };
                // clang-format on
            });
            assert(!Submission::submit(submission));
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.queue.submit = Module.arpDependencySubmit;
                    delete Module.arpDependencySubmit;
                // clang-format on
            });
        } else {
            Promises::holdActualPromises();
            assert(!Submission::submit(submission));
            Promises::restorePromiseMethods();
            Promises::waitHeldPromises();
            if (mode == IsolatedMode::SubmittedLoss) {
                probe.submission = submission;
                return;
            }
            signal->reset();
            Promises::releaseQueue();
            Promises::releaseScopes();
        }
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() == Submission::Status::Unproven;
        }));
        assert(probe.transaction->status() == Transaction::Status::Invalidated
            && !signal->isComplete());
        for (const auto& state : probe.initial->states()) {
            assert(!state.source.control.active() && !state.source.control.pending());
            const auto* resource = root->find(state.source.resource);
            assert(resource);
            assert(std::string_view(resource->buffer
                           ? resource->buffer->recordState(state.source.control.state())
                           : resource->texture->recordState(state.source.control.state()))
                == "RHI-RESOURCE-STATE-REVISION-EXHAUSTED");
        }
        assert(probe.owner->retained.pending() == 1u && probe.owner->retained.failed());
        assert(std::string_view(submission->diagnostic())
            == "RHI-WEBGPU-ORDINARY-COMPLETION-UNPROVEN");
        submission.reset();
        recording.reset();
        assert(probe.quotas[2]->liveBytes() && !probe.quotas[5]->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_DEPENDENCY_QUARANTINE_OK all5 permanent no-signal retained original-owner");
    }

    static void verifyActualDeviceLoss()
    {
        using namespace rhi_webgpu;
        assert(webGpuNativeDeviceLost() && isolatedCount_ == isolated_.size());
        for (auto& probe : isolated_) {
            if (probe.mode == IsolatedMode::PendingLoss) {
                assert(probe.recording->status() == Recording::Status::DeviceLost
                    && probe.transaction->status() == Transaction::Status::Abandoned);
                for (const auto& state : probe.initial->states())
                    assert(!state.source.control.pending()
                        && state.source.control.state() == state.observation.value);
            } else {
                assert(probe.transaction->status() == Transaction::Status::Invalidated);
                for (const auto& state : probe.initial->states())
                    assert(!state.source.control.active() && !state.source.control.pending());
                if (probe.submission) {
                    assert(probe.submission->status() == Submission::Status::DeviceLost);
                    Promises::releaseQueue();
                    Promises::releaseScopes();
                }
            }
            assert(!probe.owner->retained.pending() && probe.owner->retained.closed());
            probe.recording.reset();
            probe.submission.reset();
            probe.transaction.reset();
            probe.initial.reset();
            probe.owner.reset();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !probe.quotas[2]->liveBytes() && !probe.quotas[5]->liveBytes();
            }));
            for (const auto& quota : probe.quotas)
                assert(!quota->liveBytes());
            probe = {};
        }
        isolatedCount_ = 0u;
        for (std::size_t i = 0u; i < ownerCount_; ++i) {
            owners_[i].owner.reset();
            assert(!owners_[i].quota->liveBytes());
            owners_[i] = {};
        }
        ownerCount_ = 0u;
        std::puts(
            "BINDLESS_RHI_ORIGINAL_DEPENDENCY_BATCH_LOSS_OK pending-rollback submitted-quarantine isolated4 quotas-retire");
    }

private:
    template <class Snapshot>
    static void verifySuccessor(Snapshot&& snapshot,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        const rhi_webgpu::WebGpuOriginalTransferDependencyRecordingBudget& b,
        const std::shared_ptr<core::CpuMemoryQuota>& recordQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& transactionQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& submitQuota)
    {
        const char* diagnostic = nullptr;
        const auto prepare = [&] {
            return Recording::prepareOriginalTransferDependency(
                snapshot(), owner, b, recordQuota, transactionQuota, diagnostic);
        };
        auto first = prepare();
        assert(first);
        recordHot(first, recordQuota);
        waitRecorded(first);
        auto transaction = first->originalTransaction();
        auto submission = Submission::prepare(first,
            {1u, true, 1u, Submission::callbackCount, metadataBytes},
            submitQuota,
            diagnostic);
        assert(submission);
        Promises::holdActualPromises();
        assert(!Submission::submit(submission));
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        assert(transaction->status() == Transaction::Status::Committed);
        first.reset();
        auto successor = prepare();
        assert(successor);
        recordHot(successor, recordQuota);
        waitRecorded(successor);
        assert(successor->originalTransaction()->status() == Transaction::Status::Pending
            && transaction->validate());
        Promises::releaseQueue();
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed
            && submission->queueCompletionObserved()
            && successor->originalTransaction()->status() == Transaction::Status::Pending);
        submission.reset();
        transaction.reset();
        successor.reset();
        assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes()
            && !submitQuota->liveBytes() && !owner->retained.pending());
    }

    static void recordHot(const std::shared_ptr<Recording>& recording,
        const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto groups = EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        });
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        assert(pressure.charged());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!Recording::recordOriginalTransferDependency(recording)
            && recording->status() == Recording::Status::RecordingPending);
        assert(!recording->hasRecordedCommand()
            && Recording::recordOriginalTransferDependency(recording));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations && EM_ASM_INT({
            // clang-format off
                    return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        }) == groups);
        std::pmr::set_default_resource(previous);
    }

    static void waitRecorded(const std::shared_ptr<Recording>& recording)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand() && !recording->diagnostic());
    }

    template <class Snapshot>
    static void verifyFailures(Snapshot&& snapshot,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        const rhi_webgpu::WebGpuOriginalTransferDependencyRecordingBudget& b,
        const std::shared_ptr<core::CpuMemoryQuota>& recordQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& transactionQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& submitQuota)
    {
        const char* diagnostic = nullptr;
        const auto prepare = [&] {
            return Recording::prepareOriginalTransferDependency(
                snapshot(), owner, b, recordQuota, transactionQuota, diagnostic);
        };
        for (bool finishEncoder : {true, false}) {
            auto invalid = prepare();
            assert(invalid);
            auto initial = invalid->originalTransaction()->states();
            EM_ASM(
                {
                    // clang-format off
                        Module.arpDependencyInvalidCreate = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                            var encoder = Module.arpDependencyInvalidCreate.call(this, desc);
                            if (Number($0))
                                encoder.finish();
                            else {
                                var pop = encoder.popDebugGroup;
                                encoder.popDebugGroup = function() {
                                    pop.call(this);
                                    pop.call(this);
                                };
                            }
                            return encoder;
                        };
                    // clang-format on
                },
                finishEncoder);
            recordHot(invalid, recordQuota);
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpDependencyInvalidCreate;
                    delete Module.arpDependencyInvalidCreate;
                // clang-format on
            });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return invalid->status() != Recording::Status::RecordingPending;
            }));
            assert(invalid->status() == Recording::Status::Rejected
                && invalid->originalTransaction()->status() == Transaction::Status::Abandoned);
            assert(std::string_view(invalid->diagnostic())
                == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
            for (const auto& state : initial->states())
                assert(!state.source.control.pending()
                    && state.source.control.state() == state.observation.value);
            assert(!Submission::prepare(invalid,
                       {1u, true, 1u, Submission::callbackCount, metadataBytes},
                       submitQuota,
                       diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
            invalid.reset();
            assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes()
                && !owner->retained.pending());
        }
        auto recording = prepare();
        assert(recording);
        recordHot(recording, recordQuota);
        waitRecorded(recording);
        auto initial = recording->originalTransaction()->states();
        auto transaction = recording->originalTransaction();
        recording.reset();
        assert(transaction->status() == Transaction::Status::Abandoned && !recordQuota->liveBytes()
            && !owner->retained.pending());
        for (const auto& state : initial->states())
            assert(!state.source.control.pending()
                && state.source.control.state() == state.observation.value);
        transaction.reset();
        initial.reset();
        assert(!transactionQuota->liveBytes());
        recording = prepare();
        assert(recording);
        recordHot(recording, recordQuota);
        waitRecorded(recording);
        transaction = recording->originalTransaction();
        initial = transaction->states();
        auto submission = Submission::prepare(recording,
            {1u, true, 1u, Submission::callbackCount, metadataBytes},
            submitQuota,
            diagnostic);
        assert(submission);
        const States::State* untouched = nullptr;
        for (const auto& state : initial->states())
            if (!state.source.control.pending()) {
                untouched = &state;
                break;
            }
        assert(untouched);
        core::MutationJournal foreign;
        const auto* resource =
            initial->inputs()->scalars()->usage()->projection()->transfer()->find(
                untouched->source.resource);
        assert(resource);
        assert(!(resource->buffer
                ? resource->buffer->recordState(untouched->observation.value, &foreign)
                : resource->texture->recordState(untouched->observation.value, &foreign)));
        assert(
            std::string_view(Submission::submit(submission)) == "RHI-WEBGPU-ORIGINAL-STATES-PENDING"
            && transaction->status() == Transaction::Status::Pending);
        foreign.rollback();
        assert(
            std::string_view(Submission::submit(submission)) == "RHI-WEBGPU-ORIGINAL-STATES-STALE"
            && transaction->status() == Transaction::Status::Pending);
        assert(submission->status() == Submission::Status::Ready
            && !submission->queueCompletionObserved());
        submission.reset();
        recording.reset();
        transaction.reset();
        initial.reset();
        assert(!recordQuota->liveBytes() && !transactionQuota->liveBytes()
            && !submitQuota->liveBytes() && !owner->retained.pending());
    }

    struct OwnerProbe {
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> owner;
        std::shared_ptr<core::CpuMemoryQuota> quota;
    };

    struct IsolatedProbe {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 7> quotas;
        std::shared_ptr<const States> initial;
        std::shared_ptr<Recording> recording;
        std::shared_ptr<Submission> submission;
        std::shared_ptr<Transaction> transaction;
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> owner;
        IsolatedMode mode;

        IsolatedProbe() noexcept : mode(IsolatedMode::NativeError) {}
    };

    inline static std::array<OwnerProbe, 6> owners_;
    inline static std::array<IsolatedProbe, 4> isolated_;
    inline static std::size_t ownerCount_ = 0u, isolatedCount_ = 0u;
#endif
    static constexpr std::uint64_t metadataBytes = 65536u;

    static rhi_webgpu::WebGpuOriginalTransferDependencyRecordingBudget budget(
        std::uint32_t controls)
    {
        return {1u,
            true,
            {1u, true, 6u, 1u, 1u, 1u, 1u, Recording::callbackCount, metadataBytes},
            {1u, true, controls, 2u, 0u, metadataBytes}};
    }
};
} // namespace arp::test
