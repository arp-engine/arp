#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_compute_recording.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_ordinary_submission_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
struct WebGpuOrdinaryComputeRecordingTestDomain {
    using Inputs = rhi_webgpu::WebGpuOrdinaryScalarComputeRange;
    using Budget = rhi_webgpu::WebGpuOrdinaryComputeRecordingBudget;
    static constexpr bool graphics = false;
    static constexpr const char* name = "COMPUTE";
    static constexpr const char* ownerDiagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-RANGE-OWNER";
};

struct WebGpuOrdinaryGraphicsRecordingTestDomain {
    using Inputs = rhi_webgpu::WebGpuOrdinaryGraphicsAttachments;
    using Budget = rhi_webgpu::WebGpuOrdinaryGraphicsRecordingBudget;
    static constexpr bool graphics = true;
    static constexpr const char* name = "GRAPHICS";
    static constexpr const char* ownerDiagnostic = "RHI-WEBGPU-ORDINARY-RECORDING-ATTACHMENT-OWNER";
};
// Common hostile ownership/quota/native-scope/loss checks for both languages.
// Domain-specific attachments and draw semantics are tested by their fixture.
template <class Domain> class WebGpuOrdinaryRecordingFixture final {
public:
    static void verifyContract()
    {
        WebGpuOrdinarySubmissionFixture<Domain>::verifyContract();
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        typename Domain::Budget budget;
        assert(std::string_view(Recording::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RECORDING-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(Recording::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RECORDING-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(Recording::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-RECORDING-BUDGET");
        budget = {1u, true, 1u, 1u, 1u, 1u, Recording::callbackCount, 1u, metadataBytes};
        assert(!Recording::validateBudget(budget));
        auto* passCount = [&] {
            if constexpr (Domain::graphics)
                return &budget.maxRenderPasses;
            else
                return &budget.maxComputePasses;
        }();
        for (auto* field : {&budget.maxCommands,
                 &budget.maxEncoders,
                 passCount,
                 &budget.maxCommandBuffers,
                 &budget.maxGroups}) {
            --*field;
            assert(Recording::validateBudget(budget));
            ++*field;
        }
        --budget.maxCallbacks;
        assert(Recording::validateBudget(budget));
        ++budget.maxCallbacks;
        const char* id = nullptr;
        assert(!Recording::prepare({}, {}, budget, {}, id)
            && std::string_view(id) == "RHI-WEBGPU-ORDINARY-RECORDING-QUOTA");
        assert(std::string_view(record({})) == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        assert(!Recording::prepare({}, {}, budget, quota, id)
            && std::string_view(id) == "RHI-WEBGPU-ORDINARY-RECORDING-NATIVE-UNAVAILABLE");
        assert(!quota->liveBytes());
#endif
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(std::shared_ptr<const typename Domain::Inputs> range)
    {
        using namespace rhi_webgpu;
        using Recording = WebGpuOrdinaryRecording;
        const char* diagnostic = nullptr;
        auto completionQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        WebGpuOrdinaryCompletionBudget completionBudget{1u, true, 2u, metadataBytes};
        auto owner =
            prepareWebGpuOrdinaryCompletionOwner(completionBudget, completionQuota, diagnostic);
        assert(owner && owner->sharedOwnerMatches(owner));
        typename Domain::Budget budget{
            1u, true, 1u, 1u, 1u, 1u, Recording::callbackCount, 1u, metadataBytes};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto factory = [&] {
            auto value = Recording::prepare(range, owner, budget, quota, diagnostic);
            if (value) {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return value->status() != Recording::Status::PreparationPending;
                }));
                if (value->status() != Recording::Status::Prepared)
                    std::printf("COMPUTE_RECORDING_PREPARE_FAILURE %s\n", value->diagnostic());
                assert(value->status() == Recording::Status::Prepared);
                assert(value.use_count() == 1u);
            }
            return value;
        };
        const auto expect = [&](const char* id) {
            const auto live = quota->liveBytes();
            const auto records = owner->retained.pending();
            auto value = factory();
            if (value || !diagnostic || std::string_view(diagnostic) != id)
                std::printf("COMPUTE_RECORDING_EXPECTED %s GOT %s\n",
                    id,
                    diagnostic ? diagnostic : "Success");
            assert(!value && std::string_view(diagnostic) == id && quota->liveBytes() == live
                && owner->retained.pending() == records);
        };
        auto originalRange = range;
        range = std::shared_ptr<const typename Domain::Inputs>(quota, originalRange.get());
        expect(Domain::ownerDiagnostic);
        range = originalRange;
        originalRange.reset();
        auto originalOwner = owner;
        owner = std::shared_ptr<WebGpuOrdinaryCompletionOwner>(quota, originalOwner.get());
        expect("RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED");
        owner = originalOwner;
        originalOwner.reset();
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-ORDINARY-RECORDING-QUOTA");
        ++budget.maxCpuMetadataBytes;
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        assert(!quota->liveBytes());
        admission.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            expect("RHI-WEBGPU-ORDINARY-RECORDING-ALLOCATION");
        }
        // Pending callbacks retain a dropped original only until actual native
        // creation validation completes; no queue submission has occurred.
        auto dropped = Recording::prepare(range, owner, budget, quota, diagnostic);
        assert(dropped);
        std::weak_ptr<Recording> weak = dropped;
        dropped.reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        assert(!quota->liveBytes() && !owner->retained.pending());

        // Real native invalid encoder/pass handles must fail the asynchronous
        // scopes; these injections alter native operations, never their status.
        EM_ASM({
            // clang-format off
                Module.arpRecordingCreateEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(desc) {
                    var encoder = Module.arpRecordingCreateEncoder.call(this, desc);
                    encoder.finish();
                    return encoder;
                };
            // clang-format on
        });
        auto invalidPreparation = Recording::prepare(range, owner, budget, quota, diagnostic);
        assert(invalidPreparation);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpRecordingCreateEncoder;
                delete Module.arpRecordingCreateEncoder;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return invalidPreparation->status() != Recording::Status::PreparationPending;
        }));
        assert(invalidPreparation->status() == Recording::Status::Rejected);
        assert(std::string_view(invalidPreparation->diagnostic())
            == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
        assert(record(invalidPreparation));
        invalidPreparation.reset();
        assert(!quota->liveBytes() && !owner->retained.pending());
        EM_ASM(
            {
                // clang-format off
                    Module.arpRecordingPassName = $0 ? "beginRenderPass" : "beginComputePass";
                    Module.arpRecordingBeginPass = GPUCommandEncoder.prototype[Module.arpRecordingPassName];
                    GPUCommandEncoder.prototype[Module.arpRecordingPassName] = function(desc) {
                        var pass = Module.arpRecordingBeginPass.call(this, desc);
                        pass.end();
                        return pass;
                    };
                // clang-format on
            },
            Domain::graphics);
        auto invalidRecording = Recording::prepare(range, owner, budget, quota, diagnostic);
        assert(invalidRecording);
        EM_ASM({
            // clang-format off
                GPUCommandEncoder.prototype[Module.arpRecordingPassName] = Module.arpRecordingBeginPass;
                delete Module.arpRecordingBeginPass;
                delete Module.arpRecordingPassName;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return invalidRecording->status() != Recording::Status::PreparationPending;
        }));
        assert(
            invalidRecording->status() == Recording::Status::Prepared && !record(invalidRecording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return invalidRecording->status() != Recording::Status::RecordingPending;
        }));
        assert(invalidRecording->status() == Recording::Status::Rejected);
        assert(std::string_view(invalidRecording->executionDiagnostic())
            == "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR");
        invalidRecording.reset();
        assert(!quota->liveBytes() && !owner->retained.pending());
        std::printf("BINDLESS_RHI_ORDINARY_%s_RECORDING_NATIVE_ERRORS_OK 2\n", Domain::name);

        auto value = Recording::prepare(range, owner, budget, quota, diagnostic);
        assert(value);
        assert(value->status() == Recording::Status::PreparationPending);
        WebGpuRecordedCommandProofFixture::verifyUnrecorded(
            value, "RHI-WEBGPU-ORDINARY-RECORDING-PREPARATION-PENDING");
        assert(
            std::string_view(record(value)) == "RHI-WEBGPU-ORDINARY-RECORDING-PREPARATION-PENDING");
        const auto beforeCallbacks = core::defaultCpuAllocator().stats().allocationCount;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return value->status() != Recording::Status::PreparationPending;
        }));
        assert(core::defaultCpuAllocator().stats().allocationCount == beforeCallbacks
            && value->status() == Recording::Status::Prepared);
        auto alias = std::shared_ptr<Recording>(quota, value.get());
        assert(std::string_view(record(alias)) == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER");
        alias.reset();
        assert(std::string_view(value->diagnostic()) == "RHI-WEBGPU-ORDINARY-RECORDING-UNRECORDED");
        WebGpuRecordedCommandProofFixture::verifyUnrecorded(
            value, "RHI-WEBGPU-ORDINARY-RECORDING-UNRECORDED");
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto groupsBefore = EM_ASM_INT({
            // clang-format off
                return Module.webGpuNativeGroupCreateCount;
            // clang-format on
        });
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            assert(pressure.charged());
            for (const auto invalid : {0u, 2u, UINT32_MAX}) {
                if constexpr (Domain::graphics) {
                    assert(Recording::record(value, invalid, 1u, 0u, 0u));
                    assert(Recording::record(value, 3u, invalid, 0u, 0u));
                } else {
                    assert(Recording::record(value, invalid, 1u, 1u));
                    assert(Recording::record(value, 1u, invalid, 1u));
                    assert(Recording::record(value, 1u, 1u, invalid));
                }
            }
            if constexpr (Domain::graphics) {
                assert(Recording::record(value, 3u, 1u, 1u, 0u));
                assert(Recording::record(value, 3u, 1u, 0u, 1u));
                assert(std::string_view(Recording::record(value, 1u, 1u, 1u))
                    == "RHI-WEBGPU-ORDINARY-RECORDING-LANGUAGE");
            } else
                assert(std::string_view(Recording::record(value, 3u, 1u, 0u, 0u))
                    == "RHI-WEBGPU-ORDINARY-RECORDING-LANGUAGE");
            assert(!record(value));
            assert(value->status() == Recording::Status::RecordingPending);
            assert(std::string_view(value->executionDiagnostic())
                == "RHI-WEBGPU-ORDINARY-RECORDING-FINISH-PENDING");
            assert(record(value));
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.webGpuNativeGroupCreateCount;
                // clang-format on
            }) == groupsBefore);
            std::pmr::set_default_resource(previous);
        }
        const auto finishingCallbacks = core::defaultCpuAllocator().stats().allocationCount;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return value->status() != Recording::Status::RecordingPending;
        }));
        if (value->diagnostic())
            std::printf("COMPUTE_RECORDING_FINISH_FAILURE %s\n", value->diagnostic());
        assert(value->status() == Recording::Status::Recorded && !value->diagnostic());
        assert(core::defaultCpuAllocator().stats().allocationCount == finishingCallbacks);
        assert(std::string_view(value->executionDiagnostic())
            == "RHI-WEBGPU-ORDINARY-COMPLETION-UNPREPARED");
        assert(record(value));
        // The original completion graph exposes the command to backend-only
        // consumers. Even that original cannot bypass the closed language.
        core::pmr::String message(&core::defaultCpuMemoryResource());
        message.reserve(256u);
        assert(owner->retained.containsMatching([&](const auto& graph) noexcept {
            assert(!graph.command()->submit(message)
                && message == "RHI-WEBGPU-ORDINARY-COMPLETION-UNPREPARED");
            assert(!graph.command()->recordDraw(3u, 1u, 0u, 0u, message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            assert(!graph.command()->recordDispatch(1u, 1u, 1u, message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            assert(!graph.command()->finish(message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            WebGpuNativeBufferPayload buffer;
            WebGpuNativeTexturePayload texture;
            assert(!graph.command()->recordBufferCopy(buffer, buffer, {}, message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            assert(!graph.command()->recordTextureUpload(texture, {}, message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            assert(!graph.command()->recordBufferToTextureCopy(buffer, texture, {}, message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            assert(!graph.command()->recordTextureToBufferCopy(texture, buffer, {}, message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            assert(!graph.command()->recordTextureToTextureCopy(texture, texture, {}, message)
                && message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            for (const auto rejected :
                {graph.command()->recordRegisteredTransfer(rhi::BufferCopyDesc{}, message),
                    graph.command()->recordRegisteredTransfer(
                        rhi::BufferToTextureCopyDesc{}, message),
                    graph.command()->recordRegisteredTransfer(
                        rhi::TextureToBufferCopyDesc{}, message),
                    graph.command()->recordRegisteredTransfer(
                        rhi::TextureToTextureCopyDesc{}, message)})
                assert(!rejected);
            assert(message == "RHI-WEBGPU-ORDINARY-RECORDING-OWNER-REQUIRED");
            return true;
        }));

        // Retain real native popErrorScope promises for a second original
        // preparation across exact-device loss, never synthesize a status.
        EM_ASM({
            // clang-format off
                Module.arpRecordingScopes = Module.arpRecordingScopes || {};
                Module.arpRecordingScopes[$0] = [];
                Module.arpRecordingOriginalPop = Module.preinitializedWebGPUDevice.popErrorScope;
                Module.preinitializedWebGPUDevice.popErrorScope = function() {
                    return Module.arpRecordingOriginalPop.call(this).then(function(value) {
                        return new Promise(function(resolve) {
                            Module.arpRecordingScopes[$0].push({
                                release : function() {
                                    resolve(value);
                                }
                            });
                        });
                    });
                };

    // clang-format on
        },Domain::graphics);
        auto pending = Recording::prepare(range, owner, budget, quota, diagnostic);
        assert(pending);
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT(
                       {
                           // clang-format off
                               return Module.arpRecordingScopes[$0].length;
                           // clang-format on
                       },
                       Domain::graphics)
                == Recording::scopeCount;
        }));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.popErrorScope = Module.arpRecordingOriginalPop;
                delete Module.arpRecordingOriginalPop;
            // clang-format on
        });
        assert(pending->status() == Recording::Status::PreparationPending
            && owner->retained.pending() == 2u);
        expect("RHI-WEBGPU-ORDINARY-COMPLETION-CAPACITY");
        assert(!retained_ && !pending_);
        retained_ = std::move(value);
        pending_ = std::move(pending);
        retainedQuota_ = quota;
        completionOwner_ = owner;
        completionQuota_ = completionQuota;
        std::printf("BINDLESS_RHI_ORDINARY_%s_RECORDING_OK %llu\n",
            Domain::name,
            static_cast<unsigned long long>(peak));
        }

        static void verifySubmissionActual(std::shared_ptr<const typename Domain::Inputs> range)
        {
            using namespace rhi_webgpu;
            using Recording = WebGpuOrdinaryRecording;
            const char* diagnostic = nullptr;
            auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            typename Domain::Budget budget{
                1u, true, 1u, 1u, 1u, 1u, Recording::callbackCount, 1u, metadataBytes};
            auto makeRecorded = [&](std::shared_ptr<WebGpuOrdinaryCompletionOwner> submissionOwner,
                                    std::shared_ptr<WebGpuNativeCompletionState> signal = {}) {
                auto recording = Recording::prepare(range,
                    std::move(submissionOwner),
                    budget,
                    quota,
                    diagnostic,
                    std::move(signal));
                assert(recording);
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return recording->status() != Recording::Status::PreparationPending;
                }));
                assert(recording->status() == Recording::Status::Prepared && !record(recording));
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return recording->status() != Recording::Status::RecordingPending;
                }));
                assert(recording->status() == Recording::Status::Recorded);
                return recording;
            };
            WebGpuOrdinarySubmissionFixture<Domain>::verifyActual(makeRecorded, quota);
        }

        static void verifyRetainedDiagnostic(const char* id)
        {
            if (WebGpuOrdinarySubmissionFixture<Domain>::hasRetainedReady())
                WebGpuOrdinarySubmissionFixture<Domain>::verifyRetainedDiagnostic(id);
            assert(retained_ && std::string_view(retained_->diagnostic()) == id);
            assert(std::string_view(record(retained_)) == id);
        }

        static void verifyActualDeviceLoss()
        {
            using Recording = rhi_webgpu::WebGpuOrdinaryComputeRecording;
            WebGpuOrdinarySubmissionFixture<Domain>::verifyActualDeviceLoss();
            assert(retained_ && pending_ && retained_->status() == Recording::Status::DeviceLost
                && pending_->status() == Recording::Status::DeviceLost);
            verifyRetainedDiagnostic("RHI-WEBGPU-ORDINARY-VALIDATION-DEVICE-LOST");
            assert(!completionOwner_->retained.pending() && completionOwner_->retained.closed());
            EM_ASM(
                {
                    // clang-format off
                        for (var item of Module.arpRecordingScopes[$0])
                            item.release();
                        delete Module.arpRecordingScopes[$0];
                    // clang-format on
                },
                Domain::graphics);
            std::weak_ptr<Recording> weak = pending_;
            pending_.reset();
            retained_.reset();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return weak.expired();
            }));
            weak.reset();
            completionOwner_.reset();
            assert(!retainedQuota_->liveBytes() && !completionQuota_->liveBytes());
            retainedQuota_.reset();
            completionQuota_.reset();
            std::printf("BINDLESS_RHI_ORDINARY_%s_RECORDING_LOSS_OK\n", Domain::name);
        }
#endif
    private:
        using Recording = rhi_webgpu::WebGpuOrdinaryRecording;

        static const char* record(const std::shared_ptr<Recording>& value) noexcept
        {
            if constexpr (Domain::graphics)
                return Recording::record(value, 3u, 1u, 0u, 0u);
            else
                return Recording::record(value, 1u, 1u, 1u);
        }

        static constexpr std::uint64_t metadataBytes = 65536u;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        static inline std::shared_ptr<Recording> retained_, pending_;
        static inline std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> completionOwner_;
        static inline std::shared_ptr<core::CpuMemoryQuota> retainedQuota_, completionQuota_;
#endif
        }
        ;
        using WebGpuOrdinaryComputeRecordingFixture =
            WebGpuOrdinaryRecordingFixture<WebGpuOrdinaryComputeRecordingTestDomain>;
        using WebGpuOrdinaryGraphicsRecordingCommonFixture =
            WebGpuOrdinaryRecordingFixture<WebGpuOrdinaryGraphicsRecordingTestDomain>;
        } // namespace arp::test
