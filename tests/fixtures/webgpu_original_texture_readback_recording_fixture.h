#pragma once
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_original_readback_state_transaction_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackRecordingFixture final {
public:
    using Recording = rhi_webgpu::WebGpuOrdinaryRecording;
    using Budget = rhi_webgpu::WebGpuOriginalTextureReadbackRecordingBudget;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;
    using Scopes = WebGpuErrorScopeHandoffFixture<11u>;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static Budget budget()
    {
        Budget b{1u,
            true,
            {1u, true, 5u, 3u, 1u, 2u, 1u, Recording::callbackCount, cpuBytes},
            WebGpuOriginalReadbackStateTransactionFixture::budget(),
            WebGpuOriginalTextureReadbackCopiesFixture::budget(),
            2u};
        b.copies.maxNativeCompletionOwners = 1u;
        return b;
    }

    static void verifyContract()
    {
        Budget b;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-READBACK-VERSION");
        b.contractVersion = 1u;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-READBACK-OPT-IN");
        b = budget();
        assert(!Recording::validateBudget(b));
        b.copies.maxNativeCompletionOwners = 0u;
        assert(std::string_view(Recording::validateBudget(b))
            == "RHI-WEBGPU-ORIGINAL-READBACK-BUDGET");
        assert(std::string_view(Recording::recordOriginalTextureReadback({}))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
        using Dependency = rhi::semantics::TransferDependencyLowering;
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = {1u};
        barrier.before = rhi::ResourceState::CopyDst;
        barrier.after = rhi::ResourceState::CopySrc;
        barrier.sourceStages = barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        barrier.textureSubresources = {0u, 2u, 0u, 2u, rhi::TextureAspect::Color};
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Dependency::transition(
                backend, 1u, true, barrier, barrier.before, true, barrier.textureSubresources);
            assert((backend == rhi::Backend::WebGPU)
                == (!decision.diagnostic
                    && decision.operation == Dependency::Operation::ImplicitOrderedUsage));
        }
        auto invalid = barrier;
        invalid.sourceStages = rhi::PipelineStage::VertexShader;
        assert(std::string_view(Dependency::transition(rhi::Backend::WebGPU,
                   1u,
                   true,
                   invalid,
                   invalid.before,
                   true,
                   invalid.textureSubresources)
                       .diagnostic)
            == "RHI-ACCESS-STAGE-MISMATCH");
        invalid = barrier;
        --invalid.textureSubresources.arrayLayerCount;
        assert(std::string_view(Dependency::transition(rhi::Backend::WebGPU,
                   1u,
                   true,
                   invalid,
                   invalid.before,
                   true,
                   barrier.textureSubresources)
                       .diagnostic)
            == "RHI-TRANSFER-DEPENDENCY-RANGE-UNPROVEN");
        std::puts(
            "BINDLESS_RHI_READBACK_NATIVE_RECORDING_CONTRACT_OK backends4 explicit-dependency frozen-original-mips completion-closed");
        std::puts(
            "BINDLESS_RHI_READBACK_RECORDING_COLD_CONTRACT_OK common-proof-issuers2 exact-original-owners unrecorded-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(const std::shared_ptr<const States>& states,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner)
    {
        auto b = budget();
        const char* diagnostic = nullptr;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const auto ownerBaseline = owner->preparationQuota->liveBytes();
        assert(!owner->retained.pending());
        const auto factory = [&] {
            return Recording::prepareOriginalTextureReadback(
                states, owner, b, quotas[0], quotas[1], quotas[2], diagnostic);
        };
        std::array<std::uint64_t, 4> peaks;
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            const auto quota = i < quotas.size() ? quotas[i] : owner->preparationQuota;
            CpuQuotaFactoryFixture pressure(quota);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            {
                core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - quota->liveBytes());
                assert(full.charged() && !factory());
            }
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            assert(owner->preparationQuota->liveBytes() == ownerBaseline
                && !owner->retained.pending());
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !states->validate());
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        };
        --b.maxMipCopies;
        reject("RHI-WEBGPU-ORIGINAL-READBACK-COPY-CAPACITY");
        b = budget();
        --b.native.maxCommands;
        reject("RHI-WEBGPU-ORIGINAL-READBACK-COPY-CAPACITY");
        b = budget();
        --b.native.maxOriginalCommands;
        reject("RHI-WEBGPU-ORIGINAL-READBACK-BUDGET");
        b = budget();
        --b.native.maxDebugScopes;
        reject("RHI-WEBGPU-ORIGINAL-READBACK-BUDGET");
        b = budget();
        --b.native.maxCallbacks;
        reject("RHI-WEBGPU-ORIGINAL-TRANSFER-BUDGET");
        b = budget();
        --b.native.maxCpuMetadataBytes;
        reject("RHI-WEBGPU-ORDINARY-RECORDING-QUOTA");
        b = budget();
        --b.transaction.states.maxStateControls;
        reject("RHI-WEBGPU-STATE-TRANSACTION-CONTROL-CAPACITY");
        b = budget();
        --b.copies.maxCopyDecisions;
        reject("RHI-WEBGPU-READBACK-COPIES-CAPACITY");
        b = budget();
        auto recording = factory();
        assert(recording && recording->status() == Recording::Status::Prepared
            && !recording->hasRecordedCommand());
        assert(!factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-CAPACITY"
            && owner->retained.pending() == 1u);
        auto submissionQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto forged = std::shared_ptr<Recording>(quotas[0], recording.get());
        const auto& pair =
            states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(Recording::recordOriginalTextureReadback(forged))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
        assert(std::string_view(recording->diagnostic())
            == "RHI-WEBGPU-ORDINARY-RECORDING-UNRECORDED");
        assert(std::string_view(Recording::recordOriginalTransfer(recording))
            == "RHI-WEBGPU-ORIGINAL-TRANSFER-MODE");
        assert(std::string_view(Recording::recordOriginalTransferDependency(recording))
            == "RHI-WEBGPU-ORIGINAL-DEPENDENCY-MODE");
        assert(std::string_view(Recording::recordOriginalBufferUpload(recording))
            == "RHI-WEBGPU-ORIGINAL-UPLOAD-MODE");
        assert(std::string_view(Recording::recordOriginalTextureUpload(recording))
            == "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-MODE");
        assert(!rhi_webgpu::WebGpuOrdinarySubmission::prepare(recording,
                   {1u, true, 1u, rhi_webgpu::WebGpuOrdinarySubmission::callbackCount, cpuBytes},
                   submissionQuota,
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-RECORDING-UNRECORDED"
            && !submissionQuota->liveBytes());
        assert(owner->retained.containsMatching([&](const auto& graph) noexcept {
            return graph.matches(owner->nativeDevice()) && !graph.command()->finished()
                && graph.command()->nativeQueue()
                && core::SharedOwnerProjection::sameOwner(
                    graph.command()->originalReadbackCopies(), recording->originalReadbackCopies())
                && core::SharedOwnerProjection::sameOwner(
                    graph.command()->originalStateTransaction(), recording->originalTransaction());
        }));
        auto& buffer = *pair->destination()->nativeResource();
        const auto bytes = buffer.sizeBytes;
        ++buffer.sizeBytes;
        assert(recording->diagnostic());
        assert(!owner->retained.containsMatching([&](const auto& graph) noexcept {
            return graph.matches(owner->nativeDevice());
        }));
        buffer.sizeBytes = bytes;
        assert(
            std::string_view(recording->diagnostic()) == "RHI-WEBGPU-ORDINARY-RECORDING-UNRECORDED"
            && !pair->destination()->resultPublished());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        forged.reset();
        recording.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        assert(!owner->retained.pending() && owner->preparationQuota->liveBytes() == ownerBaseline);
        std::printf(
            "BINDLESS_RHI_READBACK_RECORDING_COLD_NATIVE_OK %llu %llu %llu %llu common-original-command-queue copies-state-closure native-mutation allocation0 unrecorded-submit-closed\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]),
            static_cast<unsigned long long>(peaks[3]));
    }

    static void verifyRecordingActual(const std::shared_ptr<const States>& states,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        unsigned mode)
    {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        auto submissionQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const auto baseline = owner->preparationQuota->liveBytes();
        {
            auto recording = Recording::prepareOriginalTextureReadback(
                states, owner, budget(), quotas[0], quotas[1], quotas[2], diagnostic);
            assert(recording);
            rhi::semantics::TransferDependencyLowering::Decision dependency;
            assert(!WebGpuOriginalTextureReadbackCopiesFixture::Copies::initialDependencyMapping(
                       recording->originalTransaction(), dependency)
                && dependency.operation
                    == rhi::semantics::TransferDependencyLowering::Operation::ImplicitOrderedUsage);
            const auto& pair =
                states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
            auto& buffer = *pair->destination()->nativeResource();
            const auto bytes = buffer.sizeBytes;
            ++buffer.sizeBytes;
            assert(Recording::recordOriginalTextureReadback(recording)
                && recording->status() == Recording::Status::Prepared);
            buffer.sizeBytes = bytes;
            EM_ASM(
                {
                    // clang-format off
                        Module.arpReadbackRecordEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.arpReadbackRecordCopies = [];
                        Module.arpReadbackRecordLabels = [];
                        Module.arpReadbackRecordEncoders = 0;
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(d) {
                            ++Module.arpReadbackRecordEncoders;
                            const encoder = Module.arpReadbackRecordEncoder.call(this, d);
                            const push = encoder.pushDebugGroup;
                            encoder.pushDebugGroup = function(label) {
                                Module.arpReadbackRecordLabels.push(label);
                                return push.call(this, label);
                            };
                            const copy = encoder.copyTextureToBuffer;
                            encoder.copyTextureToBuffer = function(s, t, e) {
                                Module.arpReadbackRecordCopies.push({
                                    source : {...s},
                                    destination : {
                                        ...t,
                                        layout : {
                                            offset : t.offset,
                                            bytesPerRow : t.bytesPerRow,
                                            rowsPerImage : t.rowsPerImage
                                        }
                                    },
                                    extent : {...e}
                                });
                                return copy.call(this, s, $0 === 1 ? {...t, offset : 1} : t, e);
                            };
                            if ($0 === 2)
                                encoder.finish();
                            return encoder;
                        };
                    // clang-format on
                },
                mode);
            if (mode == 0u || mode == 3u)
                Scopes::holdScopes();
            const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Recording::recordOriginalTextureReadback(recording));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            std::pmr::set_default_resource(previous);
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpReadbackRecordEncoder;
                    delete Module.arpReadbackRecordEncoder;
                // clang-format on
            });
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpReadbackRecordEncoders === 1 && Module.arpReadbackRecordCopies.length === 2 &&
                           Module.arpReadbackRecordLabels.length === 2;
                // clang-format on
            }) == 1);
            if (mode == 0u || mode == 3u) {
                Scopes::restoreScopes();
                Scopes::waitHeldScopes();
                assert(recording->status() == Recording::Status::RecordingPending
                    && !recording->hasRecordedCommand());
            }
            if (mode == 0u) {
                const auto passLabel = states->inputs()->scalars()->arguments()->text(1u),
                           copyLabel = states->inputs()->scalars()->arguments()->text(2u);
                assert(EM_ASM_INT(
                           {
                               // clang-format off
                                   return Module.arpReadbackRecordLabels[0] === UTF8ToString($0, $1) &&
                                          Module.arpReadbackRecordLabels[1] === UTF8ToString($2, $3);
                               // clang-format on
                           },
                           passLabel.data(),
                           passLabel.size(),
                           copyLabel.data(),
                           copyLabel.size())
                    == 1);
                for (std::size_t i = 0u;
                    i < recording->originalReadbackCopies()->decisions().size();
                    ++i) {
                    const auto& m = recording->originalReadbackCopies()->decisions()[i].original;
                    assert(EM_ASM_INT(
                               {
                                   // clang-format off
                                       const c = Module.arpReadbackRecordCopies[$0];
                                       const s = c.source;
                                       const d = c.destination;
                                       const e = c.extent;
                                       return s.texture === WebGPU.getJsObject(Number($1)) &&
                                              d.buffer === WebGPU.getJsObject(Number($2)) && s.mipLevel === $3 && s.aspect === "all" &&
                                              s.origin.x === $4 && s.origin.y === $5 && s.origin.z === $6 && e.width === $7 &&
                                              e.height === $8 && e.depthOrArrayLayers === $9 && d.offset === Number($10) &&
                                              d.bytesPerRow === $11 && d.rowsPerImage === $12;
                                   // clang-format on
                               },
                               i,
                               pair->destination()->source()->resource().texture->texture,
                               buffer.buffer,
                               m.range.baseMipLevel,
                               m.physicalOrigin.width,
                               m.physicalOrigin.height,
                               m.physicalOrigin.depth,
                               m.physicalExtent.width,
                               m.physicalExtent.height,
                               m.physicalExtent.depth,
                               m.bufferOffsetBytes,
                               m.bytesPerRow,
                               m.rowsPerImage)
                        == 1);
                }
                Scopes::releaseScopes();
            }
            if (mode == 3u) {
                std::weak_ptr<Recording> weak = recording;
                recording.reset();
                assert(!weak.expired());
                Scopes::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return weak.expired();
                }));
                weak.reset();
            } else {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return recording->status() != Recording::Status::RecordingPending;
                }));
                if (mode == 0u) {
                    assert(recording->hasRecordedCommand() && !recording->diagnostic());
                    assert(recording->originalTransaction()->status()
                        == rhi_webgpu::WebGpuOriginalStateTransaction::Status::Pending);
                    assert(
                        std::string_view(recording->originalReadbackCopies()->executionDiagnostic())
                        == "RHI-WEBGPU-READBACK-COPIES-RECORDING-UNPROVEN");
                    auto ready = rhi_webgpu::WebGpuOrdinarySubmission::prepare(recording,
                        {1u,
                            true,
                            1u,
                            rhi_webgpu::WebGpuOrdinarySubmission::callbackCount,
                            cpuBytes},
                        submissionQuota,
                        diagnostic);
                    assert(ready);
                    assert(WebGpuOriginalTextureReadbackCopiesFixture::Copies::
                            validateNativeCompletionOriginal(recording->originalReadbackCopies()));
                    ready.reset();
                    assert(!submissionQuota->liveBytes());
                } else {
                    assert(recording->status() == Recording::Status::Rejected
                        && !recording->hasRecordedCommand() && recording->diagnostic());
                    assert(recording->originalTransaction()->status()
                        == rhi_webgpu::WebGpuOriginalStateTransaction::Status::Abandoned);
                    assert(!rhi_webgpu::WebGpuOrdinarySubmission::prepare(recording,
                        {1u,
                            true,
                            1u,
                            rhi_webgpu::WebGpuOrdinarySubmission::callbackCount,
                            cpuBytes},
                        submissionQuota,
                        diagnostic));
                }
                recording.reset();
            }
            assert(std::string_view(states->validate()) == "RHI-READBACK-SOURCE-STATE-STALE"
                && !pair->validateReadyDeclaration() && !pair->destination()->resultPublished());
            for (const auto& state : states->states())
                assert(state.source.control.active() && !state.source.control.pending()
                    && state.source.control.state() == state.observation.value);
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            assert(!owner->retained.pending() && owner->preparationQuota->liveBytes() == baseline);
        }
        std::printf(
            "BINDLESS_RHI_READBACK_NATIVE_RECORDING_OK %u original-fields2 labels2 dependency-consumed scopes3 pending-close errors2 callback-drop allocation0 exact-retire native-completion-map-result-closed\n",
            mode);
    }

    static void prepareRecordingLoss(const std::shared_ptr<Recording>& recording)
    {
        Scopes::holdScopes();
        assert(!Recording::recordOriginalTextureReadback(recording));
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(recording->status() == Recording::Status::RecordingPending
            && !recording->hasRecordedCommand());
    }

    static void verifyRecordingLoss(std::shared_ptr<Recording>& recording)
    {
        assert(recording->status() == Recording::Status::DeviceLost
            && !recording->hasRecordedCommand() && recording->diagnostic());
        assert(recording->originalTransaction()->status()
            == rhi_webgpu::WebGpuOriginalStateTransaction::Status::Invalidated);
        for (const auto& state : recording->originalTransaction()->states()->states())
            assert(!state.source.control.active() && !state.source.control.pending());
        std::weak_ptr<Recording> weak = recording;
        recording.reset();
        assert(!weak.expired());
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        std::puts(
            "BINDLESS_RHI_READBACK_NATIVE_RECORDING_LOSS_OK actual-pending-device-loss scopes3 controls3 callback-drop exact-retire no-proof");
    }

    static void prepareActualLoss(const std::shared_ptr<const States>& states,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner)
    {
        for (auto& q : lossQuotas_)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        lossRecording_ = Recording::prepareOriginalTextureReadback(
            states, owner, budget(), lossQuotas_[0], lossQuotas_[1], lossQuotas_[2], diagnostic);
        assert(lossRecording_);
        assert(lossRecording_->status() == Recording::Status::Prepared
            && !lossRecording_->hasRecordedCommand());
    }

    static void verifyActualLoss()
    {
        assert(lossRecording_->status() == Recording::Status::DeviceLost
            && !lossRecording_->hasRecordedCommand());
        assert(std::string_view(lossRecording_->diagnostic())
            == "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST");
        assert(std::string_view(Recording::recordOriginalTextureReadback(lossRecording_))
            == "RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST");
        const auto& states = lossRecording_->originalReadbackCopies()->transaction()->states();
        // Another deliberately Pending transaction owns two journals. Terminal
        // loss deactivates them now; that exact owner's rollback clears pending.
        assert(lossRecording_->originalReadbackCopies()->transaction()->status()
            == rhi_webgpu::WebGpuOriginalStateTransaction::Status::Invalidated);
        for (const auto& state : states->states())
            assert(!state.source.control.active());
        lossRecording_.reset();
        for (const auto& q : lossQuotas_)
            assert(!q->liveBytes());
        lossQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_READBACK_RECORDING_COLD_LOSS_OK actual-device-loss controls3 exact-retire unrecorded-unpublished");
    }

private:
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> lossQuotas_;
    static inline std::shared_ptr<Recording> lossRecording_;
#endif
};
} // namespace arp::test
