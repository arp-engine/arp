#pragma once
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_context.h"
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_preparation.h"
#include "texture_readback_cold_budget_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_map_handoff_fixture.h"
#include "webgpu_ordinary_submission_fixture.h"
#include "webgpu_original_texture_readback_statistics_fixture.h"

namespace arp::test {
class WebGpuOwnedReadbackAdvancementFixture final {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using Preparation = rhi_webgpu::WebGpuOwnedTextureReadbackPreparation;
    using Cold = rhi_webgpu::WebGpuOwnedTextureReadbackRecordingPreparation;
    using Extension = rhi_webgpu::WebGpuOwnedTextureReadbackExtension;
    using Submission = rhi_webgpu::WebGpuOrdinarySubmission;
    using Publication = rhi_webgpu::WebGpuOriginalTextureReadbackPublication;
    using Mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping;
    using Phase = rhi::TextureReadbackOwnedPreparationContract::Phase;

    struct QueueDomain {
        static constexpr unsigned graphics = 86u;
    };

    using Queue = WebGpuOrdinarySubmissionFixture<QueueDomain>;
    using Scopes = WebGpuErrorScopeHandoffFixture<86u>;
    using Map = WebGpuMapPromiseHandoffFixture<86u>;

    struct LossQueueDomain {
        static constexpr unsigned graphics = 87u;
    };

    using LossQueue = WebGpuOrdinarySubmissionFixture<LossQueueDomain>;
    using LossRecordingScopes = WebGpuErrorScopeHandoffFixture<87u>;
    using LossMappingScopes = WebGpuErrorScopeHandoffFixture<88u>;
    using LossMap = WebGpuMapPromiseHandoffFixture<88u>;
    using PublicLossCreationScopes = WebGpuErrorScopeHandoffFixture<89u>;
    using PublicLossMapScopes = WebGpuErrorScopeHandoffFixture<90u>;
    using PublicLossMap = WebGpuMapPromiseHandoffFixture<90u>;
    using DestructorCreationScopes = WebGpuErrorScopeHandoffFixture<91u>;
    using DestructorRecordingScopes = WebGpuErrorScopeHandoffFixture<92u>;

    struct DestructorQueueDomain {
        static constexpr unsigned graphics = 93u;
    };

    using DestructorQueue = WebGpuOrdinarySubmissionFixture<DestructorQueueDomain>;
    using DestructorMappingScopes = WebGpuErrorScopeHandoffFixture<94u>;
    using DestructorMap = WebGpuMapPromiseHandoffFixture<94u>;
    static constexpr std::array<std::uint8_t, 16> pixels{
        {0, 16, 32, 255, 64, 80, 96, 255, 128, 144, 160, 255, 192, 208, 224, 255}};

    struct Candidate {
        core::UniquePtr<rhi::GpuTexture> source;
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request;
        std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statistics;
        rhi::TextureReadbackOwnedPreparationIntent intent;
    };

    static Candidate authorRequest(rhi::GpuDevice& device,
        const TextureReadbackColdBudgetFixture::Recipe& recipe,
        rhi::TextureReadbackIdentity identity = {})
    {
        Candidate result;
        if (!identity.requestId)
            identity = {rhi::allocateTextureReadbackRequestId(), 37u, 43u};
        rhi::TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.format = rhi::TextureFormat::Rgba8Unorm;
        texture.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
        texture.initialState = rhi::ResourceState::CopySrc;
        result.source = device.createTexture(texture);
        assert(result.source);
        rhi::ReadbackTextureDesc d;
        d.label = "owned 原始";
        d.sourceResource = result.source->id();
        d.destinationReadbackBuffer = rhi::allocateGpuResourceId();
        d.sizeBytes = 512u;
        d.extent = texture.extent;
        d.sourceSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        d.bytesPerRow = 256u;
        d.rowsPerImage = 2u;
        const char* id = nullptr;
        auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(d,
            recipe.input,
            recipe.originalPlan->quotas()[1],
            recipe.originalPlan->quotas()[1],
            id);
        assert(input);
        auto layout = rhi::TextureReadbackLayout::prepareOriginal(
            input, texture, recipe.layout, recipe.originalPlan->quotas()[1], id);
        assert(layout);
        result.request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
            layout, identity, recipe.request, recipe.originalPlan->quotas()[1], id);
        assert(result.request);
        result.statistics = rhi::TextureReadbackStatisticsPlan::prepareOriginal(
            layout, recipe.statistics, recipe.originalPlan->quotas()[1], id);
        assert(result.statistics);
        auto& intent = result.intent;
        intent.contractVersion = 1u;
        intent.prototypeOptIn = true;
        intent.content = {1u, true, rhi::TextureReadbackContentIntent::Mode::QueueOrderedSnapshot};
        intent.passLabel = "原始读回";
        intent.sourceBarrier.resource = result.source->id();
        intent.sourceBarrier.before = intent.sourceBarrier.after = rhi::ResourceState::CopySrc;
        intent.sourceBarrier.textureSubresources = rhi::defaultTextureSubresourceRange(texture);
        intent.sourceBarrier.sourceStages = intent.sourceBarrier.destinationStages =
            rhi::PipelineStage::Transfer;
        intent.sourceBarrier.sourceAccess = intent.sourceBarrier.destinationAccess =
            rhi::ResourceAccess::TransferRead;
        return result;
    }

    static std::shared_ptr<Preparation> admit(
        rhi::TextureReadbackOwnedPreparationExtension& extension,
        Candidate& candidate,
        const char*& id)
    {
        return std::dynamic_pointer_cast<Preparation>(extension.prepareOwnedOriginal(
            candidate.source, candidate.request, candidate.statistics, candidate.intent, id));
    }

    static void writePreparation(
        const std::shared_ptr<Preparation>& preparation, const std::array<std::uint8_t, 16>& bytes)
    {
        const auto& source = preparation->destination_->source();
        // Real native fixture producer, never production upload evidence.
        EM_ASM(
            {
                // clang-format off
                    const texture = WebGPU.getJsObject($0);
                    Module.preinitializedWebGPUDevice.queue.writeTexture(
                        {texture : texture, mipLevel : 0, origin : {x : 0, y : 0, z : 0}, aspect : 'all'},
                        HEAPU8.slice($1, $1 + $2), {offset : 0, bytesPerRow : 8, rowsPerImage : 2},
                        {width : 2, height : 2, depthOrArrayLayers : 1});
                // clang-format on
            },
            source->resource().texture->texture,
            bytes.data(),
            bytes.size());
    }

    class Context final {
    public:
        core::UniquePtr<rhi::GpuDevice> device;
        TextureReadbackColdBudgetFixture::Recipe recipe;
        rhi::TextureReadbackColdBudgetPlan::Quotas quotas;
        Extension* extension = nullptr;
        std::shared_ptr<Preparation> preparation;
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request;
        std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statistics;
        Publication::Delivery delivery;

        explicit Context(const rhi_webgpu::WebGpuRhiDeviceDesc& desc,
            const TextureReadbackColdBudgetFixture::Budget& budget =
                TextureReadbackColdBudgetFixture::budget(),
            rhi::TextureReadbackIdentity identity = {})
        {
            device = rhi_webgpu::createWebGpuRhiDevice(desc);
            assert(device);
            extension = dynamic_cast<Extension*>(
                static_cast<rhi::TextureReadbackOwnedPreparationExtension*>(
                    device->queryInterface(rhi::kExtensionTextureReadbackOwnedPreparation)));
            assert(extension && !extension->executionAvailable());
            recipe = TextureReadbackColdBudgetFixture::prepare(budget);
            quotas = recipe.originalPlan->quotas();
            assert(!extension->configureOriginalPlan(recipe.originalPlan));
            auto& context = *extension->context_;
            auto candidate = authorRequest(*device, recipe, identity);
            request = candidate.request;
            statistics = candidate.statistics;
            core::pmr::String label("原始读回", &core::defaultCpuMemoryResource());
            candidate.intent.passLabel = label;
            const char* id = nullptr;
            assert(extension->executionAvailable());
            preparation = admit(*extension, candidate, id);
            assert(preparation && !candidate.source
                && preparation->phase() == Phase::CreationPrepared);
            label.assign("mutated");
            candidate.intent.sourceBarrier.after = rhi::ResourceState::CopyDst;
            assert(preparation->passLabel_ == "原始读回"
                && preparation->barrier_.after == rhi::ResourceState::CopySrc);
            const std::array<std::uint8_t, 16> initial{};
            write(initial);
        }

        auto& budgetContext() { return *extension->context_; }

        auto cold(const char*& diagnostic)
        {
            auto& p = *preparation;
            return Cold::prepareOriginal(p.plan_,
                p.destination_,
                p.completion_,
                p.statistics_,
                p.barrier_,
                p.passLabel_,
                diagnostic);
        }

        void write(const std::array<std::uint8_t, 16>& bytes)
        {
            writePreparation(preparation, bytes);
        }

        void createReady()
        {
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!preparation->initializeCreation());
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return preparation->phase() == Phase::CreationReady;
            }));
        }

        void recordingReady()
        {
            createReady();
            assert(!preparation->prepareNextCold());
            assert(!preparation->record());
            assert(WebGpuAsyncWaitFixture::until([&] {
                return preparation->phase() == Phase::RecordingReady;
            }));
        }

        void submissionCompleted(const std::array<std::uint8_t, 16>& content = pixels)
        {
            recordingReady();
            write(content);
            assert(!preparation->prepareNextCold() && !preparation->submit());
            assert(WebGpuAsyncWaitFixture::until([&] {
                return preparation->phase() == Phase::SubmissionCompleted;
            }));
        }

        bool poll()
        {
            const char* id = nullptr;
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            const auto delivered =
                Publication::pollOriginal(budgetContext().publication, delivery, id);
            assert(!id && a == core::defaultCpuAllocator().stats().allocationCount);
            return delivered;
        }

        void retire()
        {
            preparation.reset();
            delivery = {};
            request.reset();
            statistics.reset();
            if (extension->context_) {
                assert(!budgetContext().publication->pending()
                    && !budgetContext().staging->usedBytes()
                    && !budgetContext().completion->retained.pending());
                assert(!extension->shutdownOriginal());
            }
            device.reset();
            recipe = {};
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

    static void assertWrongActions(Context& c)
    {
        const auto phase = c.preparation->phase();
        for (auto action :
            {rhi::TextureReadbackOwnedPreparationContract::Action::InitializeCreation,
                rhi::TextureReadbackOwnedPreparationContract::Action::Record,
                rhi::TextureReadbackOwnedPreparationContract::Action::Submit,
                rhi::TextureReadbackOwnedPreparationContract::Action::StartMapping}) {
            if (!rhi::TextureReadbackOwnedPreparationContract::validateAction(phase, action))
                continue;
            const auto* id =
                CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    switch (action) {
                    case rhi::TextureReadbackOwnedPreparationContract::Action::InitializeCreation:
                        return c.preparation->initializeCreation();
                    case rhi::TextureReadbackOwnedPreparationContract::Action::Record:
                        return c.preparation->record();
                    case rhi::TextureReadbackOwnedPreparationContract::Action::Submit:
                        return c.preparation->submit();
                    default:
                        return c.preparation->startMapping();
                    }
                });
            assert(std::string_view(id) == "RHI-READBACK-OWNED-ACTION-PHASE"
                && c.preparation->phase() == phase);
        }
    }

    static void verifyComplete(Context& c)
    {
        assert(
            c.preparation->phase() == Phase::Published && !c.preparation->diagnostic() && c.poll());
        assert(c.delivery.outcome == Publication::Outcome::Published
            && core::SharedOwnerProjection::sameOwner(c.delivery.originalRequest, c.request));
        const auto* bytes = c.delivery.result->publishedBytes();
        assert(bytes && c.delivery.result->byteCapacity() == 512u);
        assert(!std::memcmp(bytes, pixels.data(), 8u)
            && !std::memcmp(bytes + 256u, pixels.data() + 8u, 8u));
        const auto* analyses = c.delivery.numericalResult->publishedValues();
        assert(analyses && analyses->size() == 1u);
        const auto& a = (*analyses)[0];
        assert(a.scalar.valid && a.scalar.sampleCount == 4u && a.channels.valid
            && a.channels.channelCount == 4u);
        assert(std::abs(a.channels.channels[0].meanValue - 96.0 / 255.0)
                <= 4u * std::numeric_limits<double>::epsilon()
            && a.channels.channels[0].minValue == 0.0
            && a.channels.channels[0].maxValue == 192.0 / 255.0
            && a.channels.channels[3].meanValue == 1.0);
    }

    static void verifySuccessAndBudgets(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        Context c(desc);
        c.createReady();
        const char* id = nullptr;
        EM_ASM({
            // clang-format off
                Module.arpOwnedAdvanceEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                Module.arpOwnedAdvanceEncoderCalls = 0;
                Module.preinitializedWebGPUDevice.createCommandEncoder = function(d) {
                    ++Module.arpOwnedAdvanceEncoderCalls;
                    return Module.arpOwnedAdvanceEncoder.call(this, d);
                };
            // clang-format on
        });
        const auto factory = [&] {
            return c.cold(id);
        };
        CpuQuotaFactoryFixture pressure(c.quotas[1]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto baseline = c.quotas[1]->liveBytes();
        for (std::uint64_t available = 0u; available < peak; ++available) {
            core::CpuMemoryQuotaLease quota(
                c.quotas[1], c.quotas[1]->limitBytes() - baseline - available);
            assert(quota.charged() && c.preparation->prepareNextCold()
                && c.preparation->phase() == Phase::CreationReady
                && c.quotas[1]->liveBytes() == baseline + quota.bytes());
            assert(c.budgetContext().publication->pending() == 1u
                && !c.budgetContext().completion->retained.pending());
        }
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOwnedAdvanceEncoderCalls;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpOwnedAdvanceEncoder;
                delete Module.arpOwnedAdvanceEncoder;
                delete Module.arpOwnedAdvanceEncoderCalls;
            // clang-format on
        });
        assertWrongActions(c);
        assert(!c.preparation->prepareNextCold()
            && c.preparation->phase() == Phase::RecordingPrepared && !c.preparation->diagnostic());
        assert(c.preparation->prepareNextCold());
        assertWrongActions(c);
        Scopes::holdScopes();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return c.preparation->record();
        }));
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(c.preparation->phase() == Phase::RecordingPending);
        assertWrongActions(c);
        assert(c.preparation->prepareNextCold());
        Scopes::releaseScope(0u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->coldRecording_->recording()->readbackRecordingCallbacksRemaining()
                == 2u;
        }));
        assert(c.preparation->phase() == Phase::RecordingPending && !c.poll());
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::RecordingReady;
        }));
        // Submission measurement needs a fresh unsaturated Request history.
        Context s(desc);
        s.recordingReady();
        const auto subFactory = [&] {
            return Submission::prepare(
                s.preparation->coldRecording_->recording(), s.recipe.submission, s.quotas[1], id);
        };
        CpuQuotaFactoryFixture subPressure(s.quotas[1]);
        const auto subPeak = subPressure.measure(subFactory);
        subPressure.verify(subPeak, subFactory);
        const auto before = s.quotas[1]->liveBytes();
        {
            core::CpuMemoryQuotaLease quota(
                s.quotas[1], s.quotas[1]->limitBytes() - before - subPeak);
            assert(quota.charged());
            assert(std::string_view(s.preparation->prepareNextCold())
                    == "RHI-READBACK-PUBLICATION-EXECUTION-ALLOCATION"
                && s.preparation->phase() == Phase::RecordingReady
                && s.quotas[1]->liveBytes() == before + quota.bytes());
        }
        assert(!s.preparation->prepareNextCold());
        const auto completeSubmissionBytes = s.quotas[1]->liveBytes() - before;
        assert(completeSubmissionBytes > subPeak);
        assert(s.preparation->cancel());
        s.retire();
        const auto subBaseline = c.quotas[1]->liveBytes();
        for (std::uint64_t available = 0u; available < completeSubmissionBytes; ++available) {
            core::CpuMemoryQuotaLease quota(
                c.quotas[1], c.quotas[1]->limitBytes() - subBaseline - available);
            assert(quota.charged());
            assert(c.preparation->prepareNextCold()
                && c.preparation->phase() == Phase::RecordingReady
                && c.quotas[1]->liveBytes() == subBaseline + quota.bytes());
        }
        {
            core::CpuMemoryQuotaLease quota(
                c.quotas[1], c.quotas[1]->limitBytes() - subBaseline - completeSubmissionBytes);
            assert(quota.charged() && !c.preparation->prepareNextCold());
        }
        assert(c.preparation->phase() == Phase::SubmissionPrepared && !c.preparation->diagnostic());
        assertWrongActions(c);
        c.write(pixels);
        Queue::holdActualPromises();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return c.preparation->submit();
        }));
        Queue::restorePromiseMethods();
        Queue::waitHeldPromises();
        assert(c.preparation->phase() == Phase::SubmissionPending);
        assertWrongActions(c);
        Queue::releaseQueue();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->submission_->queueCompletionObserved();
        }));
        assert(c.preparation->phase() == Phase::SubmissionPending && !c.poll());
        Queue::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::SubmissionCompleted;
        }));
        assertWrongActions(c);
        Map::install(c.preparation->destination_->nativeResource()->buffer, 0u, true);
        Scopes::holdScopes();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return c.preparation->startMapping();
        }));
        Scopes::restoreScopes();
        Map::restoreMap();
        Scopes::waitHeldScopes();
        Map::waitHeld();
        assert(c.preparation->phase() == Phase::MappingPending);
        assertWrongActions(c);
        Map::release();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->coldRecording_->mapping()->mapCompletionObserved();
        }));
        assert(c.preparation->phase() == Phase::MappingPending && !c.poll());
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::Published;
        }));
        Map::restoreAll();
        verifyComplete(c);
        assertWrongActions(c);
        c.retire();
        std::printf(
            "BINDLESS_RHI_OWNED_ADVANCEMENT_NATIVE_OK %llu %llu copied-label-barrier Request-exact-less-exhausted cold-encoder0 retries phases original-bytes-statistics hot-quotas4 allocation0 base-capability-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(completeSubmissionBytes));
    }

    static void verifyCancelDrain(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            Context c(desc);
            if (mode == 0u) {
                c.createReady();
                assert(!c.preparation->prepareNextCold());
                Scopes::holdScopes();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->record();
                }));
                Scopes::restoreScopes();
                Scopes::waitHeldScopes();
            } else if (mode == 1u) {
                c.recordingReady();
                assert(!c.preparation->prepareNextCold());
                Queue::holdActualPromises();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->submit();
                }));
                Queue::restorePromiseMethods();
                Queue::waitHeldPromises();
            } else {
                c.submissionCompleted();
                Map::install(c.preparation->destination_->nativeResource()->buffer, 0u, true);
                Scopes::holdScopes();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->startMapping();
                }));
                Scopes::restoreScopes();
                Map::restoreMap();
                Scopes::waitHeldScopes();
                Map::waitHeld();
            }
            std::weak_ptr<Preparation> facade = c.preparation;
            std::weak_ptr<rhi_webgpu::WebGpuOrdinaryRecording> recording =
                c.preparation->coldRecording_->recording();
            std::weak_ptr<Submission> submission = c.preparation->submission_;
            std::weak_ptr<Mapping> mapping = c.preparation->coldRecording_->mapping();
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(c.preparation->cancel() && c.preparation->phase() == Phase::CanceledDraining);
            assertWrongActions(c);
            assert(c.preparation->prepareNextCold());
            c.preparation.reset();
            assert(facade.expired());
            if (mode == 0u) {
                Scopes::releaseScope(0u);
                assert(WebGpuAsyncWaitFixture::until([&] {
                    auto native = recording.lock();
                    return native && native->readbackRecordingCallbacksRemaining() == 2u;
                }));
            } else if (mode == 1u) {
                Queue::releaseQueue();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    auto native = submission.lock();
                    return native && native->queueCompletionObserved();
                }));
            } else {
                Map::release();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    auto native = mapping.lock();
                    return native && native->mapCompletionObserved();
                }));
            }
            assert(!c.poll() && c.budgetContext().publication->pending() == 1u
                && c.budgetContext().staging->usedBytes() == 512u);
            if (mode == 1u)
                Queue::releaseScopes();
            else
                Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                (void)c.poll();
                return !c.budgetContext().publication->pending();
            }));
            if (mode == 2u)
                Map::restoreAll();
            assert(recording.expired() && submission.expired() && mapping.expired()
                && c.delivery.outcome == Publication::Outcome::Empty);
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            facade.reset();
            recording.reset();
            submission.reset();
            mapping.reset();
            c.retire();
            std::printf(
                "BINDLESS_RHI_OWNED_ADVANCEMENT_CANCEL_OK %u opaque-drop actual-partial-drain native-graph-retained identity-suppressed quotas4-zero allocation0\n",
                mode);
        }
    }

    static void verifyErrors(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        for (unsigned mode = 0u; mode < 3u; ++mode) {
            Context c(desc);
            if (mode == 0u) {
                c.createReady();
                assert(!c.preparation->prepareNextCold());
                EM_ASM({
                    // clang-format off
                        Module.arpOwnedBadEncoder = Module.preinitializedWebGPUDevice.createCommandEncoder;
                        Module.preinitializedWebGPUDevice.createCommandEncoder = function(d) {
                            const encoder = Module.arpOwnedBadEncoder.call(this, d);
                            const copy = encoder.copyTextureToBuffer;
                            encoder.copyTextureToBuffer = function(s, t, e) {
                                return copy.call(this, s, Object.assign({}, t, {offset : 1}), e);
                            };
                            return encoder;
                        };
                    // clang-format on
                });
                Scopes::holdScopes();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->record();
                }));
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.createCommandEncoder = Module.arpOwnedBadEncoder;
                        delete Module.arpOwnedBadEncoder;
                    // clang-format on
                });
                Scopes::restoreScopes();
                Scopes::waitHeldScopes();
                assert(!c.poll());
                Scopes::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return c.preparation->coldRecording_->recording()
                        ->readbackRecordingCallbacksDrained();
                }));
            } else {
                c.submissionCompleted();
                if (mode == 1u) {
                    Map::install(c.preparation->destination_->nativeResource()->buffer, 2u, true);
                    Scopes::holdScopes();
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                        return c.preparation->startMapping();
                    }));
                    Scopes::restoreScopes();
                    Map::restoreMap();
                    Scopes::waitHeldScopes();
                    Map::waitHeld();
                    Map::release();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return c.preparation->coldRecording_->mapping()->mapCompletionObserved();
                    }));
                    assert(!c.poll());
                    Scopes::releaseScopes();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return c.preparation->coldRecording_->mapping()->callbacksDrained();
                    }));
                    Map::restoreAll();
                } else {
                    auto& native = *c.preparation->destination_->nativeResource();
                    const auto bytes = native.sizeBytes;
                    ++native.sizeBytes;
                    assert(CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                        return c.preparation->startMapping();
                    }));
                    native.sizeBytes = bytes;
                    assert(!c.preparation->coldRecording_->mapping()->callbacksIssued());
                }
            }
            assert(c.preparation->phase() == Phase::Rejected && c.preparation->diagnostic());
            assertWrongActions(c);
            rhi::TextureReadbackOwnedDelivery output;
            bool delivered = false;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                const char* id = nullptr;
                delivered = c.extension->pollOriginal(output, id);
                return id;
            }) && delivered);
            assert(output.outcome == rhi::TextureReadbackOwnedDelivery::Outcome::Rejected
                && core::SharedOwnerProjection::sameOwner(output.originalRequest, c.request)
                && output.diagnostic && !output.snapshot);
            output = {};
            c.retire();
            std::printf(
                "BINDLESS_RHI_OWNED_ADVANCEMENT_ERROR_OK %u actual-native-or-contract-rejection callback-drained original-identity no-bytes-numerics allocation0\n",
                mode);
        }
    }

    static void verifyDetachedSnapshot(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        Context c(desc);
        c.submissionCompleted();
        assert(!c.preparation->startMapping());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::Published;
        }));
        verifyComplete(c);
        auto snapshot = c.delivery.ownedSnapshot;
        assert(snapshot
            && !rhi::TextureReadbackOwnedSnapshotContract::validatePublishedOriginal(snapshot));
        assert(
            snapshot->contentMode() == rhi::TextureReadbackContentIntent::Mode::QueueOrderedSnapshot
            && snapshot->analysisCount() == 1u);
        assert(core::SharedOwnerProjection::sameOwner(snapshot->originalRequest(), c.request)
            && core::SharedOwnerProjection::sameOwner(
                snapshot->originalStatisticsPlan(), c.statistics));
        assert(!std::memcmp(snapshot->publishedBytes(), pixels.data(), 8u)
            && !std::memcmp(snapshot->publishedBytes() + 256u, pixels.data() + 8u, 8u));
        assert(snapshot->publishedAnalyses()[0].channels.channels[0].meanValue
            == c.delivery.numericalResult->publishedValues()
                ->front()
                .channels.channels[0]
                .meanValue);
        std::weak_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackDestination> native =
            c.preparation->destination_;
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(c.budgetContext().publication->pending() == 0u);
        c.preparation.reset();
        c.delivery = {};
        assert(native.expired() && !c.budgetContext().staging->usedBytes());
        assert(!c.extension->shutdownOriginal());
        c.device.reset();
        c.recipe = {};
        c.request.reset();
        c.statistics.reset();
        assert(!c.quotas[0]->liveBytes() && !c.quotas[2]->liveBytes() && c.quotas[1]->liveBytes()
            && c.quotas[3]->liveBytes() == 512u);
        assert(!rhi::TextureReadbackOwnedSnapshotContract::validatePublishedOriginal(snapshot)
            && !std::memcmp(snapshot->publishedBytes(), pixels.data(), 8u)
            && snapshot->publishedAnalyses()[0].channels.channels[3].meanValue == 1.0);
        snapshot.reset();
        native.reset();
        for (const auto& quota : c.quotas)
            assert(!quota->liveBytes());
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_OWNED_SNAPSHOT_NATIVE_OK exact-original bytes-statistics CPU-detached native-context-retired retained-quota immutable allocation0 base-capability-closed");
    }

    static void verifyPublicDelivery(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        Context c(desc);
        std::shared_ptr<rhi::TextureReadbackOwnedPreparation> owned = c.preparation;
        assert(!owned->initializeCreation());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owned->phase() == Phase::CreationReady;
        }));
        assert(!owned->prepareNextCold() && !owned->record());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owned->phase() == Phase::RecordingReady;
        }));
        c.write(pixels);
        assert(!owned->prepareNextCold() && !owned->submit());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owned->phase() == Phase::SubmissionCompleted;
        }));
        rhi::TextureReadbackOwnedDelivery output;
        bool delivered = false;
        const auto poll = [&]() noexcept {
            const char* id = nullptr;
            delivered = c.extension->pollOriginal(output, id);
            return id;
        };
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, poll) && !delivered
            && !output.snapshot);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return owned->startMapping();
        }));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owned->phase() == Phase::Published;
        }));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, poll) && delivered);
        assert(output.outcome == rhi::TextureReadbackOwnedDelivery::Outcome::Published
            && !output.diagnostic
            && core::SharedOwnerProjection::sameOwner(output.originalRequest, c.request)
            && !rhi::TextureReadbackOwnedSnapshotContract::validatePublishedOriginal(
                output.snapshot));
        assert(!std::memcmp(output.snapshot->publishedBytes(), pixels.data(), 8u)
            && !std::memcmp(output.snapshot->publishedBytes() + 256u, pixels.data() + 8u, 8u)
            && output.snapshot->publishedAnalyses()[0].channels.channels[0].meanValue
                == 96.0 / 255.0);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            WebGpuOriginalTextureReadbackStatisticsFixture::verifyLegacySnapshot(output.snapshot);
            return static_cast<const char*>(nullptr);
        }));
        std::puts(
            "BINDLESS_RHI_OWNED_LEGACY_NUMERICAL_EQUIVALENCE_OK actual-original-snapshot scalar-channels-extrema-hashes-counts padding-excluded quotas4-exhausted allocation0 legacy-GPU-not-promoted");
        auto snapshot = output.snapshot;
        auto original = output.originalRequest;
        assert(std::string_view(CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, poll))
                == "RHI-READBACK-OWNED-OUTPUT-BUSY"
            && !delivered && core::SharedOwnerProjection::sameOwner(snapshot, output.snapshot)
            && core::SharedOwnerProjection::sameOwner(original, output.originalRequest));
        owned.reset();
        c.preparation.reset();
        assert(!c.extension->shutdownOriginal() && !c.extension->executionAvailable());
        core::UniquePtr<rhi::GpuTexture> missing;
        const char* id = nullptr;
        assert(!c.extension->prepareOwnedOriginal(missing, c.request, c.statistics, {}, id)
            && std::string_view(id) == "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED");
        output = {};
        snapshot.reset();
        original.reset();
        c.retire();
        std::puts(
            "BINDLESS_RHI_OWNED_PUBLIC_DELIVERY_OK actual-GpuDevice Prototype-opt-in public-cold-hot-poll original-bytes-statistics busy-preserved quotas4-exhausted allocation0 base-capability-closed");
    }

    static void verifyPublicShutdown(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        for (unsigned mode = 0u; mode < 5u; ++mode) {
            Context c(desc);
            if (mode == 1u) {
                Scopes::holdScopes();
                assert(!c.preparation->initializeCreation());
                Scopes::restoreScopes();
                Scopes::waitHeldScopes();
            } else if (mode == 2u) {
                c.createReady();
                assert(!c.preparation->prepareNextCold());
                Scopes::holdScopes();
                assert(!c.preparation->record());
                Scopes::restoreScopes();
                Scopes::waitHeldScopes();
            } else if (mode == 3u) {
                c.recordingReady();
                assert(!c.preparation->prepareNextCold());
                Queue::holdActualPromises();
                assert(!c.preparation->submit());
                Queue::restorePromiseMethods();
                Queue::waitHeldPromises();
            } else if (mode == 4u) {
                c.submissionCompleted();
                Map::install(c.preparation->destination_->nativeResource()->buffer, 0u, true);
                Scopes::holdScopes();
                assert(!c.preparation->startMapping());
                Scopes::restoreScopes();
                Map::restoreMap();
                Scopes::waitHeldScopes();
                Map::waitHeld();
            }
            std::weak_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackDestination> native =
                c.preparation->destination_;
            std::weak_ptr<rhi_webgpu::WebGpuOrdinaryRecording> recording;
            std::weak_ptr<Submission> submission;
            std::weak_ptr<Mapping> mapping;
            if (c.preparation->coldRecording_) {
                recording = c.preparation->coldRecording_->recording();
                mapping = c.preparation->coldRecording_->mapping();
            }
            submission = c.preparation->submission_;
            const auto* id =
                CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.extension->shutdownOriginal();
                });
            assert(std::string_view(id)
                == (mode ? "RHI-READBACK-OWNED-CONTEXT-CALLBACKS-DRAINING"
                         : "RHI-READBACK-OWNED-CONTEXT-RETAINED-STAGING"));
            assert(!c.extension->executionAvailable()
                && c.preparation->phase() == (mode ? Phase::CanceledDraining : Phase::Retired));
            assertWrongActions(c);
            core::UniquePtr<rhi::GpuTexture> missing;
            const char* rejection = nullptr;
            assert(
                !c.extension->prepareOwnedOriginal(missing, c.request, c.statistics, {}, rejection)
                && std::string_view(rejection) == "RHI-READBACK-OWNED-CONTEXT-CLOSED");
            c.preparation.reset();
            rhi::TextureReadbackOwnedDelivery output;
            const auto poll = [&]() noexcept {
                const char* diagnostic = nullptr;
                assert(!c.extension->pollOriginal(output, diagnostic) && !output.snapshot);
                return diagnostic;
            };
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            if (mode) {
                if (mode == 3u)
                    Queue::releaseQueue();
                else if (mode == 4u)
                    Map::release();
                else
                    Scopes::releaseScope(0u);
                assert(WebGpuAsyncWaitFixture::until([&] {
                    if (mode == 1u) {
                        auto owner = native.lock();
                        return owner && owner->creationScopeObserved(0u);
                    }
                    if (mode == 2u) {
                        auto owner = recording.lock();
                        return owner && owner->readbackRecordingCallbacksRemaining() == 2u;
                    }
                    if (mode == 3u) {
                        auto owner = submission.lock();
                        return owner && owner->queueCompletionObserved();
                    }
                    auto owner = mapping.lock();
                    return owner && owner->mapCompletionObserved();
                }));
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, poll)
                    && !native.expired());
                assert(std::string_view(c.extension->shutdownOriginal())
                    == "RHI-READBACK-OWNED-CONTEXT-CALLBACKS-DRAINING");
                if (mode == 3u)
                    Queue::releaseScopes();
                else
                    Scopes::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    assert(!poll());
                    return native.expired() && !c.budgetContext().publication->pending();
                }));
                if (mode == 4u)
                    Map::restoreAll();
            } else
                assert(native.expired() && !c.budgetContext().publication->pending());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                return c.extension->shutdownOriginal();
            }));
            assert(!c.extension->executionAvailable()
                && a == core::defaultCpuAllocator().stats().allocationCount);
            native.reset();
            recording.reset();
            submission.reset();
            mapping.reset();
            c.retire();
            std::printf(
                "BINDLESS_RHI_OWNED_PUBLIC_SHUTDOWN_OK %u close-cancel actual-partial-drain no-output opaque-drop nonblocking quotas4-zero allocation0\n",
                mode);
        }
    }

    static void verifyPublicConcurrent(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        Context c(desc, TextureReadbackColdBudgetFixture::publicPairBudget());
        const char* id = nullptr;
        auto second = authorRequest(*c.device, c.recipe),
             third = authorRequest(*c.device, c.recipe);
        auto duplicate = authorRequest(*c.device, c.recipe, c.request->identity());
        auto& extension = *c.extension;
        auto& context = c.budgetContext();
        auto other = admit(extension, second, id);
        assert(other && !second.source);
        assert(context.publication->pending() == 2u && context.staging->usedBytes() == 1024u
            && second.request->label() == c.request->label()
            && second.request->identity().requestId != c.request->identity().requestId);
        const auto reject = [&](Candidate& candidate, const char* expected) {
            const auto* source = candidate.source.get();
            std::array<std::uint64_t, 4> baseline{};
            for (std::size_t i = 0u; i < baseline.size(); ++i)
                baseline[i] = c.quotas[i]->liveBytes();
            assert(!admit(extension, candidate, id) && id && std::string_view(id) == expected
                && candidate.source.get() == source);
            for (std::size_t i = 0u; i < baseline.size(); ++i)
                assert(baseline[i] == c.quotas[i]->liveBytes());
        };
        EM_ASM({
            // clang-format off
                Module.arpOwnedPairCreate = Module.preinitializedWebGPUDevice.createBuffer;
                Module.arpOwnedPairCreateCalls = 0;
                Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                    ++Module.arpOwnedPairCreateCalls;
                    return Module.arpOwnedPairCreate.call(this, d);
                };
            // clang-format on
        });
        reject(duplicate, "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE");
        reject(third, "RHI-READBACK-PUBLICATION-CAPACITY");
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOwnedPairCreateCalls;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBuffer = Module.arpOwnedPairCreate;
                delete Module.arpOwnedPairCreate;
                delete Module.arpOwnedPairCreateCalls;
            // clang-format on
        });
        assert(!c.preparation->initializeCreation() && !other->initializeCreation());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::CreationReady
                && other->phase() == Phase::CreationReady;
        }));
        assert(!c.preparation->prepareNextCold() && !other->prepareNextCold());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            if (const auto* diagnostic = c.preparation->record())
                return diagnostic;
            return other->record();
        }));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::RecordingReady
                && other->phase() == Phase::RecordingReady;
        }));
        constexpr std::array<std::uint8_t, 16> different{
            {224, 208, 192, 255, 160, 144, 128, 255, 96, 80, 64, 255, 32, 16, 0, 255}};
        c.write(pixels);
        writePreparation(other, different);
        assert(!c.preparation->prepareNextCold() && !other->prepareNextCold());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            if (const auto* diagnostic = c.preparation->submit())
                return diagnostic;
            return other->submit();
        }));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::SubmissionCompleted
                && other->phase() == Phase::SubmissionCompleted;
        }));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return other->startMapping();
        }));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return other->phase() == Phase::Published;
        }));
        rhi::TextureReadbackOwnedDelivery secondOutput, firstOutput;
        const auto poll = [&](rhi::TextureReadbackOwnedDelivery& output, bool expected) {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                const char* diagnostic = nullptr;
                assert(extension.pollOriginal(output, diagnostic) == expected);
                return diagnostic;
            }));
        };
        poll(secondOutput, true);
        assert(core::SharedOwnerProjection::sameOwner(secondOutput.originalRequest, second.request)
            && context.publication->pending() == 1u
            && !std::memcmp(secondOutput.snapshot->publishedBytes(), different.data(), 8u)
            && !std::memcmp(
                secondOutput.snapshot->publishedBytes() + 256u, different.data() + 8u, 8u));
        poll(firstOutput, false);
        assert(!firstOutput.snapshot);
        // Delivered CPU output frees a pending slot, but the original native
        // preparation still owns its staging allocation until its final release.
        reject(third, "RHI-READBACK-DEVICE-STAGING-CAPACITY");
        assert(context.publication->pending() == 1u);
        other.reset();
        assert(context.staging->usedBytes() == 512u);
        auto retry = admit(extension, third, id);
        assert(retry && !third.source && context.publication->pending() == 2u);
        assert(retry->cancel());
        retry.reset();
        assert(context.publication->pending() == 1u && context.staging->usedBytes() == 512u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return c.preparation->startMapping();
        }));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.preparation->phase() == Phase::Published;
        }));
        poll(firstOutput, true);
        assert(core::SharedOwnerProjection::sameOwner(firstOutput.originalRequest, c.request)
            && !context.publication->pending()
            && !std::memcmp(firstOutput.snapshot->publishedBytes(), pixels.data(), 8u)
            && !std::memcmp(firstOutput.snapshot->publishedBytes() + 256u, pixels.data() + 8u, 8u));
        assert(firstOutput.snapshot->publishedAnalyses()[0].channels.channels[0].meanValue
                == 96.0 / 255.0
            && secondOutput.snapshot->publishedAnalyses()[0].channels.channels[0].meanValue
                == 128.0 / 255.0
            && firstOutput.snapshot->publishedAnalyses()[0].scalar.sampleHash
                != secondOutput.snapshot->publishedAnalyses()[0].scalar.sampleHash);
        c.preparation.reset();
        assert(!context.staging->usedBytes());
        firstOutput = {};
        secondOutput = {};
        second = {};
        third = {};
        duplicate = {};
        c.retire();
        std::puts(
            "BINDLESS_RHI_OWNED_PUBLIC_CONCURRENT_OK actual-context2 same-label distinct-identities reverse-map-output duplicate-pending-aggregate-refusal retained-staging retry-cancel quotas4-zero hot-allocation0");
    }

    static void verifyPublicConcurrentCancel(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        Context c(desc, TextureReadbackColdBudgetFixture::publicPairBudget());
        const char* id = nullptr;
        auto second = authorRequest(*c.device, c.recipe);
        auto other = admit(*c.extension, second, id);
        assert(other);
        auto& context = c.budgetContext();
        Scopes::holdScopes();
        assert(!c.preparation->initializeCreation());
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(c.preparation->cancel() && c.preparation->phase() == Phase::CanceledDraining
            && context.publication->pending() == 2u);
        std::weak_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackDestination> canceled =
            c.preparation->destination_;
        c.preparation.reset();
        assert(!canceled.expired());
        assert(!other->initializeCreation());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return other->phase() == Phase::CreationReady;
        }));
        assert(!other->prepareNextCold() && !other->record());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return other->phase() == Phase::RecordingReady;
        }));
        writePreparation(other, pixels);
        assert(!other->prepareNextCold() && !other->submit());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return other->phase() == Phase::SubmissionCompleted;
        }));
        assert(!other->startMapping());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return other->phase() == Phase::Published;
        }));
        rhi::TextureReadbackOwnedDelivery output;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            const char* diagnostic = nullptr;
            assert(c.extension->pollOriginal(output, diagnostic));
            return diagnostic;
        }));
        assert(core::SharedOwnerProjection::sameOwner(output.originalRequest, second.request)
            && !canceled.expired() && context.publication->pending() == 1u
            && context.staging->usedBytes() == 1024u);
        other.reset();
        assert(context.staging->usedBytes() == 512u);
        Scopes::releaseScope(0u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            auto owner = canceled.lock();
            return owner && owner->creationScopeObserved(0u);
        }));
        rhi::TextureReadbackOwnedDelivery empty;
        const auto drain = [&]() noexcept {
            const char* diagnostic = nullptr;
            assert(!c.extension->pollOriginal(empty, diagnostic) && !empty.snapshot);
            return diagnostic;
        };
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, drain)
            && !canceled.expired() && context.publication->pending() == 1u);
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            assert(!drain());
            return canceled.expired() && !context.publication->pending();
        }));
        assert(!context.staging->usedBytes()
            && !std::memcmp(output.snapshot->publishedBytes(), pixels.data(), 8u)
            && !std::memcmp(output.snapshot->publishedBytes() + 256u, pixels.data() + 8u, 8u));
        output = {};
        second = {};
        canceled.reset();
        c.retire();
        std::puts(
            "BINDLESS_RHI_OWNED_PUBLIC_CONCURRENT_CANCEL_OK actual-context2 canceled-creation-partial retained-native-ticket other-success-isolated full-drain no-canceled-output quotas4-zero hot-allocation0");
    }

    static void verifyPublicDeviceIsolation(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        Context first(desc, TextureReadbackColdBudgetFixture::publicPairBudget());
        Context peer(
            desc, TextureReadbackColdBudgetFixture::publicPairBudget(), first.request->identity());
        assert(first.request->identity().requestId == peer.request->identity().requestId
            && first.request->label() == peer.request->label()
            && first.request->source() != peer.request->source()
            && first.budgetContext().completion->nativeDevice()
                == peer.budgetContext().completion->nativeDevice()
            && !core::SharedOwnerProjection::sameOwner(
                first.budgetContext().registry, peer.budgetContext().registry));
        // Correct receiver Request quota and exact source object/layout: the
        // logical registry must still reject the other device's actual texture.
        auto foreign = authorRequest(*first.device, peer.recipe);
        const auto* source = foreign.source.get();
        const auto reject = [&](Extension& receiver, const char* expected) {
            std::array<std::uint64_t, 8> baseline{};
            for (std::size_t i = 0u; i < 4u; ++i) {
                baseline[i] = first.quotas[i]->liveBytes();
                baseline[4u + i] = peer.quotas[i]->liveBytes();
            }
            const char* id = nullptr;
            assert(!admit(receiver, foreign, id) && std::string_view(id) == expected
                && foreign.source.get() == source);
            for (std::size_t i = 0u; i < 4u; ++i)
                assert(baseline[i] == first.quotas[i]->liveBytes()
                    && baseline[4u + i] == peer.quotas[i]->liveBytes());
            assert(first.budgetContext().publication->pending() == 1u
                && peer.budgetContext().publication->pending() == 1u
                && first.budgetContext().staging->usedBytes() == 512u
                && peer.budgetContext().staging->usedBytes() == 512u);
        };
        EM_ASM({
            // clang-format off
                Module.arpOwnedIsolationCreate = Module.preinitializedWebGPUDevice.createBuffer;
                Module.arpOwnedIsolationCreateCalls = 0;
                Module.preinitializedWebGPUDevice.createBuffer = function(d) {
                    ++Module.arpOwnedIsolationCreateCalls;
                    return Module.arpOwnedIsolationCreate.call(this, d);
                };
            // clang-format on
        });
        reject(*peer.extension, "RHI-WEBGPU-TRANSFER-RESOURCE-MISSING");
        // Correct source registry but wrong Request domain is independently closed.
        reject(*first.extension, "RHI-READBACK-OWNED-REQUEST-QUOTA");
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpOwnedIsolationCreateCalls;
            // clang-format on
        }) == 0);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBuffer = Module.arpOwnedIsolationCreate;
                delete Module.arpOwnedIsolationCreate;
                delete Module.arpOwnedIsolationCreateCalls;
            // clang-format on
        });
        foreign = {};
        constexpr std::array<std::uint8_t, 16> peerPixels{
            {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255}};
        first.submissionCompleted();
        peer.submissionCompleted(peerPixels);
        assert(!first.preparation->startMapping() && !peer.preparation->startMapping());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return first.preparation->phase() == Phase::Published
                && peer.preparation->phase() == Phase::Published;
        }));
        rhi::TextureReadbackOwnedDelivery firstOutput, peerOutput;
        const auto poll = [&](Context& c,
                              rhi::TextureReadbackOwnedDelivery& output,
                              const std::array<std::uint8_t, 16>& expected) {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                const char* id = nullptr;
                assert(c.extension->pollOriginal(output, id));
                return id;
            }));
            assert(core::SharedOwnerProjection::sameOwner(output.originalRequest, c.request)
                && core::SharedOwnerProjection::sameOwner(
                    output.snapshot->originalRequest(), c.request)
                && !std::memcmp(output.snapshot->publishedBytes(), expected.data(), 8u)
                && !std::memcmp(
                    output.snapshot->publishedBytes() + 256u, expected.data() + 8u, 8u));
        };
        // Equal identity values cannot consume the other logical context's ticket.
        poll(peer, peerOutput, peerPixels);
        assert(first.budgetContext().publication->pending() == 1u);
        poll(first, firstOutput, pixels);
        assert(firstOutput.originalRequest->identity().requestId
                == peerOutput.originalRequest->identity().requestId
            && firstOutput.snapshot->publishedAnalyses()[0].scalar.sampleHash
                != peerOutput.snapshot->publishedAnalyses()[0].scalar.sampleHash);
        firstOutput = {};
        peerOutput = {};
        first.retire();
        assert(peer.extension->executionAvailable());
        peer.retire();
        std::puts(
            "BINDLESS_RHI_OWNED_PUBLIC_DEVICE_ISOLATION_OK actual-contexts2 same-native-device distinct-registries equal-request-label-id foreign-source-quota-reject native-create0 original-output-isolated independent-quota-retirement");
    }

    struct DestructorState {
        TextureReadbackColdBudgetFixture::Recipe recipe;
        rhi::TextureReadbackColdBudgetPlan::Quotas quotas;
        std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> completion;
    };

    static inline std::array<core::UniquePtr<DestructorState>, 4> destructorStates_;

    static void verifyPendingDeviceDestruction(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            Context c(desc);
            auto& saved = destructorStates_[mode];
            saved = core::makeUnique<DestructorState>();
            saved->quotas = c.quotas;
            saved->completion = c.budgetContext().completion;
            if (mode == 0u) {
                DestructorCreationScopes::holdScopes();
                assert(!c.preparation->initializeCreation());
                DestructorCreationScopes::restoreScopes();
                DestructorCreationScopes::waitHeldScopes();
            } else if (mode == 1u) {
                c.createReady();
                assert(!c.preparation->prepareNextCold());
                DestructorRecordingScopes::holdScopes();
                assert(!c.preparation->record());
                DestructorRecordingScopes::restoreScopes();
                DestructorRecordingScopes::waitHeldScopes();
            } else if (mode == 2u) {
                c.recordingReady();
                assert(!c.preparation->prepareNextCold());
                DestructorQueue::holdActualPromises();
                assert(!c.preparation->submit());
                DestructorQueue::restorePromiseMethods();
                DestructorQueue::waitHeldPromises();
            } else {
                c.submissionCompleted();
                DestructorMap::install(
                    c.preparation->destination_->nativeResource()->buffer, 0u, true);
                DestructorMappingScopes::holdScopes();
                assert(!c.preparation->startMapping());
                DestructorMappingScopes::restoreScopes();
                DestructorMap::restoreMap();
                DestructorMappingScopes::waitHeldScopes();
                DestructorMap::waitHeld();
            }
            std::weak_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackDestination> native =
                c.preparation->destination_;
            std::weak_ptr<rhi_webgpu::WebGpuOrdinaryRecording> recording;
            std::weak_ptr<Submission> submission;
            std::weak_ptr<Mapping> mapping;
            if (c.preparation->coldRecording_) {
                recording = c.preparation->coldRecording_->recording();
                mapping = c.preparation->coldRecording_->mapping();
            }
            submission = c.preparation->submission_;
            saved->recipe = std::move(c.recipe);
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                c.device.reset();
                return static_cast<const char*>(nullptr);
            }));
            c.extension = nullptr;
            assert(c.preparation->phase() == Phase::CanceledDraining);
            assertWrongActions(c);
            c.preparation.reset();
            c.request.reset();
            c.statistics.reset();
            c.delivery = {};
            assert(!native.expired());
            if (mode == 0u)
                DestructorCreationScopes::releaseScope(0u);
            else if (mode == 1u)
                DestructorRecordingScopes::releaseScope(0u);
            else if (mode == 2u)
                DestructorQueue::releaseQueue();
            else
                DestructorMap::release();
            assert(WebGpuAsyncWaitFixture::until([&] {
                if (mode == 0u) {
                    auto owner = native.lock();
                    return owner && owner->creationScopeObserved(0u);
                }
                if (mode == 1u) {
                    auto owner = recording.lock();
                    return owner && owner->readbackRecordingCallbacksRemaining() == 2u;
                }
                if (mode == 2u) {
                    auto owner = submission.lock();
                    return owner && owner->queueCompletionObserved();
                }
                auto owner = mapping.lock();
                return owner && owner->mapCompletionObserved();
            }));
            assert(!native.expired() && saved->completion->originalLossArmed());
            if (mode == 0u)
                DestructorCreationScopes::releaseScopes();
            else if (mode == 1u)
                DestructorRecordingScopes::releaseScopes();
            else if (mode == 2u)
                DestructorQueue::releaseScopes();
            else
                DestructorMappingScopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return native.expired();
            }));
            if (mode == 3u)
                DestructorMap::restoreAll();
            native.reset();
            recording.reset();
            submission.reset();
            mapping.reset();
            assert(!c.quotas[1]->liveBytes() && !c.quotas[2]->liveBytes()
                && !c.quotas[3]->liveBytes() && c.quotas[0]->liveBytes()
                && saved->completion->originalLossArmed()
                && !saved->completion->retained.pending());
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            std::printf(
                "BINDLESS_RHI_OWNED_PUBLIC_DESTRUCTOR_PENDING_OK %u actual-GpuDevice-drop original-callbacks partial-full-drain native-graph-retired Device-retained-until-loss allocation0\n",
                mode);
        }
    }

    static void verifyDestructorLoss()
    {
        for (unsigned mode = 0u; mode < 4u; ++mode) {
            auto& state = destructorStates_[mode];
            const auto q = state->quotas;
            assert(WebGpuAsyncWaitFixture::until([&] {
                return state->completion->originalLossDelivered();
            }));
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!state->completion->originalLossArmed());
            state->completion.reset();
            state->recipe = {};
            state.reset();
            for (const auto& quota : q)
                assert(!quota->liveBytes());
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            std::printf(
                "BINDLESS_RHI_OWNED_PUBLIC_DESTRUCTOR_LOSS_OK %u actual-loss original-registration-retired quotas4-zero allocation0\n",
                mode);
        }
    }

    static void prepareLoss(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        for (unsigned mode = 0u; mode < 6u; ++mode) {
            auto& retained = lossContexts_[mode];
            retained = core::makeUnique<Context>(desc);
            auto& c = *retained;
            if (mode == 4u) {
                PublicLossCreationScopes::holdScopes();
                assert(!c.preparation->initializeCreation());
                PublicLossCreationScopes::restoreScopes();
                PublicLossCreationScopes::waitHeldScopes();
                continue;
            }
            if (mode == 5u) {
                c.submissionCompleted();
                PublicLossMap::install(
                    c.preparation->destination_->nativeResource()->buffer, 0u, true);
                PublicLossMapScopes::holdScopes();
                assert(!c.preparation->startMapping());
                PublicLossMapScopes::restoreScopes();
                PublicLossMap::restoreMap();
                PublicLossMapScopes::waitHeldScopes();
                PublicLossMap::waitHeld();
                continue;
            }
            if (mode == 0u) {
                c.createReady();
                assert(!c.preparation->prepareNextCold());
                LossRecordingScopes::holdScopes();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->record();
                }));
                LossRecordingScopes::restoreScopes();
                LossRecordingScopes::waitHeldScopes();
            } else if (mode == 1u) {
                c.recordingReady();
                assert(!c.preparation->prepareNextCold());
                LossQueue::holdActualPromises();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->submit();
                }));
                LossQueue::restorePromiseMethods();
                LossQueue::waitHeldPromises();
            } else if (mode == 2u) {
                c.submissionCompleted();
                LossMap::install(c.preparation->destination_->nativeResource()->buffer, 0u, true);
                LossMappingScopes::holdScopes();
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->startMapping();
                }));
                LossMappingScopes::restoreScopes();
                LossMap::restoreMap();
                LossMappingScopes::waitHeldScopes();
                LossMap::waitHeld();
            } else {
                c.recordingReady();
                assert(!c.preparation->prepareNextCold());
                EM_ASM({
                    // clang-format off
                        Module.arpOwnedDuplicateSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                        Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                            return Module.arpOwnedDuplicateSubmit.call(this, [ buffers[0], buffers[0] ]);
                        };
                    // clang-format on
                });
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
                    return c.preparation->submit();
                }));
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.queue.submit = Module.arpOwnedDuplicateSubmit;
                        delete Module.arpOwnedDuplicateSubmit;
                    // clang-format on
                });
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return c.preparation->submission_->callbacksDrained();
                }));
                assert(c.preparation->phase() == Phase::Rejected
                    && c.budgetContext().completion->retained.failed()
                    && c.budgetContext().completion->retained.pending() == 1u);
            }
            assert(c.preparation->cancel() && c.preparation->phase() == Phase::CanceledDraining);
            assertWrongActions(c);
            // The completion-owner detach contract observes submitted graphs.
            // Creation/record/map publication debt is a context shutdown fact.
            if (mode == 1u || mode == 3u)
                assert(rhi_webgpu::WebGpuOrdinaryCompletionOwner::detachOriginalDeviceLoss(
                    c.budgetContext().completion));
            else
                assert(std::string_view(c.extension->shutdownOriginal())
                    == "RHI-READBACK-OWNED-CONTEXT-CALLBACKS-DRAINING");
            if (mode == 3u) {
                quarantineDestination_ = c.preparation->destination_;
                c.preparation.reset();
                c.request.reset();
                c.statistics.reset();
                assert(!quarantineDestination_.expired()
                    && c.budgetContext().staging->usedBytes() == 512u
                    && !c.budgetContext().publication->pending());
                assert(std::string_view(c.extension->shutdownOriginal())
                    == "RHI-READBACK-OWNED-CONTEXT-QUARANTINE");
            }
        }
        std::puts(
            "BINDLESS_RHI_OWNED_ADVANCEMENT_LOSS_PREPARED original-record-submit-map callback-debt native-duplicate-quarantine detach-rejected facade-drop-retained");
    }

    static inline std::array<core::UniquePtr<Context>, 6> lossContexts_;
    static inline std::weak_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackDestination>
        quarantineDestination_;
#endif
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual(const rhi_webgpu::WebGpuRhiDeviceDesc& desc)
    {
        verifySuccessAndBudgets(desc);
        verifyCancelDrain(desc);
        verifyErrors(desc);
        verifyDetachedSnapshot(desc);
        verifyPublicDelivery(desc);
        verifyPublicShutdown(desc);
        verifyPublicConcurrent(desc);
        verifyPublicConcurrentCancel(desc);
        verifyPublicDeviceIsolation(desc);
        verifyPendingDeviceDestruction(desc);
        prepareLoss(desc);
    }

    static void verifyLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        verifyDestructorLoss();
        for (unsigned mode = 0u; mode < 6u; ++mode) {
            auto& c = *lossContexts_[mode];
            assert(WebGpuAsyncWaitFixture::until([&] {
                return c.budgetContext().completion->originalLossDelivered();
            }));
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            if (mode >= 4u) {
                assert(!c.extension->executionAvailable()
                    && c.preparation->phase() == Phase::DeviceLost);
                rhi::TextureReadbackOwnedDelivery output;
                bool delivered = false;
                const auto poll = [&]() noexcept {
                    const char* id = nullptr;
                    delivered = c.extension->pollOriginal(output, id);
                    return id;
                };
                assert(
                    !CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, poll) && !delivered);
                if (mode == 4u) {
                    PublicLossCreationScopes::releaseScope(0u);
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return c.preparation->destination_->creationScopeObserved(0u);
                    }));
                } else {
                    PublicLossMap::release();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return c.preparation->coldRecording_->mapping()->mapCompletionObserved();
                    }));
                }
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, poll) && !delivered
                    && !output.snapshot);
                if (mode == 4u)
                    PublicLossCreationScopes::releaseScopes();
                else
                    PublicLossMapScopes::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    assert(!poll());
                    return delivered;
                }));
                assert(output.outcome == rhi::TextureReadbackOwnedDelivery::Outcome::DeviceLost
                    && output.diagnostic && !output.snapshot
                    && core::SharedOwnerProjection::sameOwner(output.originalRequest, c.request));
                output = {};
                if (mode == 5u)
                    PublicLossMap::restoreAll();
                c.retire();
                lossContexts_[mode].reset();
                assert(a == core::defaultCpuAllocator().stats().allocationCount);
                std::printf(
                    "BINDLESS_RHI_OWNED_PUBLIC_LOSS_OK %u actual-loss admission-closed original-identity partial-callbacks no-snapshot quotas4-zero allocation0\n",
                    mode - 4u);
                continue;
            }
            if (mode < 3u) {
                assert(c.preparation->phase() == Phase::CanceledDraining
                    && c.preparation->callbacksOutstanding() && !c.poll());
                if (mode == 0u) {
                    LossRecordingScopes::releaseScope(0u);
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return c.preparation->coldRecording_->recording()
                                   ->readbackRecordingCallbacksRemaining()
                            == 2u;
                    }));
                    assert(c.preparation->phase() == Phase::CanceledDraining);
                    LossRecordingScopes::releaseScopes();
                } else if (mode == 1u) {
                    LossQueue::releaseQueue();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return c.preparation->submission_->queueCompletionObserved();
                    }));
                    assert(c.preparation->phase() == Phase::CanceledDraining);
                    LossQueue::releaseScopes();
                } else {
                    LossMap::release();
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return c.preparation->coldRecording_->mapping()->mapCompletionObserved();
                    }));
                    assert(c.preparation->phase() == Phase::CanceledDraining);
                    LossMappingScopes::releaseScopes();
                }
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return c.preparation->phase() == Phase::Retired;
                }));
                if (mode == 2u)
                    LossMap::restoreAll();
                (void)c.poll();
                assertWrongActions(c);
            } else {
                assert(quarantineDestination_.expired() && !c.budgetContext().staging->usedBytes()
                    && !c.budgetContext().completion->retained.pending());
                quarantineDestination_.reset();
            }
            c.retire();
            lossContexts_[mode].reset();
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
            std::printf(
                "BINDLESS_RHI_OWNED_ADVANCEMENT_LOSS_OK %u actual-loss original-callbacks-full-drain quarantine-retired quotas4-zero allocation0\n",
                mode);
        }
    }
#endif
};
} // namespace arp::test
