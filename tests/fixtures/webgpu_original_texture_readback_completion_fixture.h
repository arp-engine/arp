#pragma once
#include "webgpu_completed_texture_readback_destination_fixture.h"
#include "webgpu_ordinary_submission_fixture.h"
#include "webgpu_original_texture_readback_native_mapping_fixture.h"
#include "webgpu_original_texture_readback_recording_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackCompletionFixture final {
public:
    using Recording = rhi_webgpu::WebGpuOrdinaryRecording;
    using Submission = rhi_webgpu::WebGpuOrdinarySubmission;
    using Copies = rhi_webgpu::WebGpuOriginalTextureReadbackCopies;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;

    struct Domain {
        static constexpr unsigned graphics = 12u;
    };

    using Promises = WebGpuOrdinarySubmissionFixture<Domain>;

    struct MutationDomain {
        static constexpr unsigned graphics = 13u;
    };

    using MutationPromises = WebGpuOrdinarySubmissionFixture<MutationDomain>;
    static constexpr auto cpuBytes = WebGpuOriginalTextureReadbackRecordingFixture::cpuBytes;

    static auto budget()
    {
        return rhi_webgpu::WebGpuOrdinarySubmissionBudget{
            1u, true, 1u, Submission::callbackCount, cpuBytes};
    }

    static void verifyContract()
    {
        assert(std::string_view(Copies::validateNativeCompletedDeclarationOriginal({}))
            == "RHI-WEBGPU-READBACK-COPIES-OWNER");
        assert(std::string_view(Copies::validateNativeCompletionOriginal({}))
            == "RHI-WEBGPU-READBACK-COPIES-OWNER");
        std::puts(
            "BINDLESS_RHI_READBACK_NATIVE_COMPLETION_CONTRACT_OK issuers2 independent-cpu native-ticket queue-scopes4 map-result-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void record(const std::shared_ptr<Recording>& recording)
    {
        assert(!Recording::recordOriginalTextureReadback(recording));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return recording->status() != Recording::Status::RecordingPending;
        }));
        assert(recording->hasRecordedCommand() && !recording->diagnostic()
            && recording->originalReadbackCopies()->nativeStatus()
                == Copies::NativeStatus::Recorded);
    }

    static void verifyActual(const std::shared_ptr<const States>& states,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing,
        unsigned order)
    {
        const char* diagnostic = nullptr;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto recording = Recording::prepareOriginalTextureReadback(states,
            owner,
            WebGpuOriginalTextureReadbackRecordingFixture::budget(),
            quotas[0],
            quotas[1],
            quotas[2],
            diagnostic);
        assert(recording);
        auto copies = recording->originalReadbackCopies();
        assert(copies->nativeStatus() == Copies::NativeStatus::Unrecorded
            && Copies::validateNativeCompletionOriginal(copies));
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> mapQuotas;
        for (auto& q : mapQuotas)
            q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const rhi_webgpu::WebGpuOriginalTextureReadbackMappingBudget mapBudget{1u,
            true,
            1u,
            1u,
            1u,
            2u,
            4432u,
            1696u,
            1536u,
            cpuBytes,
            cpuBytes,
            rhi_webgpu::WebGpuOriginalTextureReadbackMapping::scopeCallbackCount};
        auto mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping::prepareOriginal(
            copies, mapBudget, mapQuotas[0], mapQuotas[1], diagnostic);
        assert(mapping);
        auto canceledDraft = mapping->resultDeclaration();
        mapping.reset();
        assert(!mapQuotas[0]->liveBytes() && mapQuotas[1]->liveBytes()
            && !canceledDraft->publishedBytes());
        mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping::prepareOriginal(
            copies, mapBudget, mapQuotas[0], mapQuotas[1], diagnostic);
        assert(mapping);
        WebGpuOriginalTextureReadbackPublicationFixture::Context publication(
            mapping, true, packing);
        using Numerical = WebGpuOriginalTextureReadbackStatisticsFixture::Numerical;
        auto foreignNumerical = Numerical::prepareOriginal(publication.statistics->plan,
            canceledDraft,
            {1u, true, 4u, 20u, 1536u, WebGpuOriginalTextureReadbackStatisticsFixture::cpuBytes},
            publication.statistics->numericalQuota,
            diagnostic);
        assert(foreignNumerical
            && std::string_view(Numerical::attachOriginal(foreignNumerical, mapping))
                == "RHI-READBACK-NUMERICAL-SOURCE-MISMATCH");
        foreignNumerical.reset();
        record(recording);
        const auto factory = [&] {
            return Submission::prepare(recording, budget(), quotas[3], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quotas[3]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(quotas[3], cpuBytes);
            assert(full.charged() && !factory());
        }
        auto submission = factory();
        assert(submission && !factory()
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-SUBMISSION-DUPLICATE");
        const auto& pair =
            states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
        auto& buffer = *pair->destination()->nativeResource();
        const auto bytes = buffer.sizeBytes;
        ++buffer.sizeBytes;
        assert(Submission::submit(submission));
        buffer.sizeBytes = bytes;
        assert(submission->status() == Submission::Status::Ready
            && copies->nativeStatus() == Copies::NativeStatus::Recorded);
        Promises::holdActualPromises();
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Submission::submit(submission));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        Promises::restorePromiseMethods();
        Promises::waitHeldPromises();
        assert(copies->transaction()->status()
                == rhi_webgpu::WebGpuOriginalStateTransaction::Status::Committed
            && copies->nativeStatus() == Copies::NativeStatus::Submitted);
        assert(Copies::validateNativeCompletedDeclarationOriginal(copies)
            && Copies::validateNativeCompletionOriginal(copies)
            && !pair->destination()->resultPublished());
        if (order == 0u) {
            Promises::releaseQueue();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return submission->queueCompletionObserved();
            }));
        } else {
            Promises::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return submission->scopeCompletionsObserved();
            }));
        }
        assert(submission->status() == Submission::Status::Pending
            && copies->nativeStatus() == Copies::NativeStatus::Submitted
            && Copies::validateNativeCompletionOriginal(copies));
        std::weak_ptr<Recording> weakRecording = recording;
        std::weak_ptr<Submission> weakSubmission = submission;
        recording.reset();
        submission.reset();
        assert(!weakRecording.expired() && !weakSubmission.expired());
        if (order == 0u)
            Promises::releaseScopes();
        else
            Promises::releaseQueue();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weakSubmission.expired();
        }));
        weakSubmission.reset();
        assert(weakRecording.expired());
        weakRecording.reset();
        assert(!owner->retained.pending()
            && copies->nativeStatus() == Copies::NativeStatus::Completed
            && !Copies::validateNativeCompletionOriginal(copies));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto forged = std::shared_ptr<const Copies>(quotas[2], copies.get());
        assert(std::string_view(Copies::validateNativeCompletionOriginal(forged))
            == "RHI-WEBGPU-READBACK-COPIES-OWNER");
        assert(Copies::validateNativeCompletedDeclarationOriginal(forged));
        forged.reset();
        ++buffer.sizeBytes;
        assert(Copies::validateNativeCompletedDeclarationOriginal(copies)
            && Copies::validateNativeCompletionOriginal(copies));
        buffer.sizeBytes = bytes;
        assert(!Copies::validateNativeCompletionOriginal(copies));
        assert(!mapping->resultDeclaration()->publishedBytes());
        assert(!rhi_webgpu::WebGpuOriginalTextureReadbackMapping::prepareOriginal(
                   copies, mapBudget, mapQuotas[0], mapQuotas[1], diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-READBACK-MAP-COLD-STATUS");
        WebGpuMapHandoffFixture::verifyMapStateSeparation(
            buffer.buffer, pair->destination()->unpublishedResultCapacityBytes(), [&] {
                const auto count = core::defaultCpuAllocator().stats().allocationCount;
                assert(!Copies::validateNativeCompletedDeclarationOriginal(copies));
                assert(copies->validate() && copies->transaction()->validate()
                    && Copies::validateNativeCompletionOriginal(copies));
                assert(core::defaultCpuAllocator().stats().allocationCount == count);
            });
        std::printf(
            "BINDLESS_RHI_READBACK_COMMITTED_DECLARATION_NATIVE_OK %u actual-completed-ticket Pending-Mapped-declaration strict-access forged-mutation allocation0\n",
            order);
        verifyBytes(copies, packing);
        assert(!Copies::validateNativeCompletionOriginal(copies)
            && !pair->destination()->resultPublished());
        WebGpuOriginalTextureReadbackNativeMappingFixture::verifyCompleted(
            mapping, packing, order, &publication);
        assert(!canceledDraft->publishedBytes()
            && rhi_webgpu::WebGpuOriginalTextureReadbackResult::validatePublishedOriginal(
                canceledDraft));
        canceledDraft.reset();
        std::puts(
            "BINDLESS_RHI_READBACK_RESULT_CANCELED_DRAFT_OK exact-generation later-publication-no-impersonation");
        publication.retire();
        mapping.reset();
        for (const auto& q : mapQuotas)
            assert(!q->liveBytes());
        copies.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_NATIVE_COMPLETION_OK %u %llu original-command-ticket queue-scopes4 cpu-commit-unproven native-bytes2 callback-drop allocation0 map-result-owned-separately\n",
            order,
            static_cast<unsigned long long>(peak));
    }

    static void verifyLowered(const std::shared_ptr<const States>& states,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing)
    {
        using Mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping;
        using Sink = rhi_webgpu::WebGpuOriginalTextureReadbackPublication;
        using Request = rhi::TextureReadbackRequestDeclaration;
        auto recipe = TextureReadbackColdBudgetFixture::prepare();
        auto domains = recipe.originalPlan->quotas();
        const char* diagnostic = nullptr;
        // Caller-prepared original resource/input graph retains its existing charges.
        // Every new cold command/map/statistical/publication owner uses the exact
        // sealed public Request or Device domain, without unrelated local quotas.
        auto recording = Recording::prepareOriginalTextureReadback(
            states, owner, recipe.recording, domains[1], domains[1], domains[1], diagnostic);
        if (!recording)
            std::printf("COLD_BUDGET_RECORDING_DIAGNOSTIC %s\n", diagnostic);
        assert(recording);
        auto mapping = Mapping::prepareOriginal(recording->originalReadbackCopies(),
            recipe.mapping,
            domains[1],
            domains[1],
            diagnostic);
        assert(mapping);
        const auto& layout = mapping->resultDeclaration()->layout();
        auto request = Request::prepareOriginal(layout,
            {rhi::allocateTextureReadbackRequestId(), 37u, 43u},
            recipe.request,
            domains[1],
            diagnostic);
        assert(request);
        std::optional<WebGpuOriginalTextureReadbackStatisticsFixture::Context> statistics;
        statistics.emplace(mapping->resultDeclaration(), packing, false, &recipe);
        statistics->attachNumerical(mapping, false);
        assert(!rhi::TextureReadbackColdBudgetPlan::validateRequestOriginal(
            recipe.originalPlan, request, statistics->plan));
        const auto& pair =
            states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
        auto sink = Sink::prepareForOriginalRegistry(pair->destination()->source()->registry(),
            recipe.publication,
            domains[0],
            domains[1],
            diagnostic);
        assert(sink);
        auto reservation =
            Sink::reserveTrackedOriginal(sink, mapping, request, statistics->numerical, diagnostic);
        assert(reservation);
        record(recording);
        auto submission = Submission::prepare(recording, recipe.submission, domains[1], diagnostic);
        assert(submission);
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Submission::submit(submission));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return submission->status() != Submission::Status::Pending;
        }));
        assert(submission->status() == Submission::Status::Completed && !reservation->start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return mapping->status() == Mapping::Status::Published;
        }));
        Sink::Delivery delivery;
        assert(Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic
            && delivery.outcome == Sink::Outcome::Published);
        assert(core::SharedOwnerProjection::sameOwner(delivery.originalRequest, request)
            && core::SharedOwnerProjection::sameOwner(
                delivery.numericalResult, statistics->numerical));
        statistics->verifyPublished(delivery.result);
        WebGpuOriginalTextureReadbackNativeMappingFixture::verifyRows(
            delivery.result->publishedBytes(), delivery.result->layout(), packing);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        submission.reset();
        recording.reset();
        mapping.reset();
        reservation.reset();
        sink.reset();
        request.reset();
        statistics.reset();
        assert(domains[1]->liveBytes() && delivery.result->publishedBytes());
        delivery = {};
        assert(!domains[1]->liveBytes() && !domains[2]->liveBytes() && !domains[3]->liveBytes());
        recipe = {};
        assert(!domains[0]->liveBytes());
        std::puts(
            "BINDLESS_RHI_READBACK_COLD_BUDGET_EXECUTION_OK recording-submission-map-statistics-publication exact-Device-Request quotas retained-delivery allocation0 private-only");
    }

    class EarlyProbe final {
    public:
        using DestinationFixture = WebGpuCompletedTextureReadbackDestinationFixture;
        using Mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping;
        using Sink = rhi_webgpu::WebGpuOriginalTextureReadbackPublication;
        using Pair = rhi_webgpu::WebGpuOriginalTextureReadbackPairProjection;
        DestinationFixture::Context caller;
        TextureReadbackColdBudgetFixture::Recipe recipe;
        rhi::TextureReadbackColdBudgetPlan::Quotas quotas;
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request;
        std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statisticsPlan;
        std::shared_ptr<Sink> sink;
        std::optional<Sink::Reservation> reservation;
        std::shared_ptr<rhi_webgpu::WebGpuOriginalTextureReadbackDestination> destination;
        std::shared_ptr<const Pair> pair;
        std::shared_ptr<const States> states;
        std::shared_ptr<Recording> recording;
        std::shared_ptr<Submission> submission;
        std::shared_ptr<Mapping> mapping;
        std::optional<WebGpuOriginalTextureReadbackStatisticsFixture::Context> statistics;
        Sink::Delivery delivery;

        explicit EarlyProbe(const std::shared_ptr<const DestinationFixture::View>& view)
            : caller(view)
        {
            recipe = TextureReadbackColdBudgetFixture::prepare();
            quotas = recipe.originalPlan->quotas();
            const char* diagnostic = nullptr;
            const auto& layout = caller.source->layout();
            request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(layout,
                {rhi::allocateTextureReadbackRequestId(), 37u, 43u},
                recipe.request,
                quotas[1],
                diagnostic);
            assert(request);
            statisticsPlan = rhi::TextureReadbackStatisticsPlan::prepareOriginal(
                layout, recipe.statistics, quotas[1], diagnostic);
            assert(statisticsPlan);
            sink = Sink::prepareForDevicePlan(
                recipe.originalPlan, caller.source->registry(), recipe.publication, diagnostic);
            assert(sink);
            reservation = Sink::reserveRequestOriginal(sink, request, statisticsPlan, diagnostic);
            assert(reservation && sink->pending() == 1u);
        }

        void prepare(const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
            const std::shared_ptr<const rhi::TextureUploadPacking>& packing)
        {
            const char* diagnostic = nullptr;
            auto duplicate =
                rhi::TextureReadbackRequestDeclaration::prepareOriginal(request->originalLayout(),
                    request->identity(),
                    recipe.request,
                    quotas[1],
                    diagnostic);
            assert(duplicate);
            auto other =
                rhi::TextureReadbackRequestDeclaration::prepareOriginal(request->originalLayout(),
                    {rhi::allocateTextureReadbackRequestId(), 37u, 43u},
                    recipe.request,
                    quotas[1],
                    diagnostic);
            assert(other);
            EM_ASM({
                // clang-format off
                    Module.arpEarlyReadbackCreates = 0;
                    Module.arpEarlyReadbackCreate = Module.preinitializedWebGPUDevice.createBuffer;
                    Module.preinitializedWebGPUDevice.createBuffer = function() {
                        Module.arpEarlyReadbackCreates++;
                        return Module.arpEarlyReadbackCreate.apply(this, arguments);
                    };
                // clang-format on
            });
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Sink::reserveRequestOriginal(sink, duplicate, statisticsPlan, diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE");
            assert(!Sink::reserveRequestOriginal(sink, other, statisticsPlan, diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-CAPACITY");
            assert(before == core::defaultCpuAllocator().stats().allocationCount && EM_ASM_INT({
                // clang-format off
                        return Module.arpEarlyReadbackCreates;
                // clang-format on
            }) == 0);
            const rhi_webgpu::WebGpuCompletedTextureReadbackDestinationBudget b{1u,
                true,
                1u,
                recipe.originalPlan->budget().maxContentBytesPerRequest,
                recipe.destination};
            destination =
                rhi_webgpu::WebGpuOriginalTextureReadbackDestination::prepareCompletedOriginal(
                    caller.source, b, quotas[1], quotas[2], quotas[3], diagnostic);
            assert(destination);
            assert(EM_ASM_INT({
                // clang-format off
                    return Module.arpEarlyReadbackCreates;
                // clang-format on
            }) == 0);
            assert(!rhi_webgpu::WebGpuOriginalTextureReadbackDestination::initialize(destination));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return destination->status()
                    != rhi_webgpu::WebGpuOriginalTextureReadbackDestination::Status::Pending;
            }));
            assert(!destination->validate() && EM_ASM_INT({
                // clang-format off
                        return Module.arpEarlyReadbackCreates;
                // clang-format on
            }) == 1);
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createBuffer = Module.arpEarlyReadbackCreate;
                    delete Module.arpEarlyReadbackCreate;
                // clang-format on
            });
            auto pb = WebGpuOriginalTextureReadbackPairFixture::budget();
            pb.maxCpuMetadataBytes = quotas[1]->limitBytes();
            pair = Pair::prepareOriginal(destination, pb, quotas[1], diagnostic);
            assert(pair);
            WebGpuOriginalTextureReadbackStateIntentFixture::Quotas stateQuotas;
            stateQuotas.fill(quotas[1]);
            states = WebGpuOriginalTextureReadbackStateIntentFixture::prepare(pair,
                WebGpuOriginalTextureReadbackStateIntentFixture::barrier(pair),
                stateQuotas,
                diagnostic,
                quotas[1]->limitBytes());
            assert(states);
            recording = Recording::prepareOriginalTextureReadback(
                states, owner, recipe.recording, quotas[1], quotas[1], quotas[1], diagnostic);
            assert(recording);
            mapping = Mapping::prepareOriginal(recording->originalReadbackCopies(),
                recipe.mapping,
                quotas[1],
                quotas[1],
                diagnostic);
            assert(mapping);
            statistics.emplace(
                mapping->resultDeclaration(), packing, false, &recipe, statisticsPlan);
            statistics->attachNumerical(mapping, false);
            auto alienPlan = rhi::TextureReadbackStatisticsPlan::prepareOriginal(
                request->originalLayout(), recipe.statistics, quotas[1], diagnostic);
            assert(alienPlan);
            auto alienSink = Sink::prepareForDevicePlan(
                recipe.originalPlan, caller.source->registry(), recipe.publication, diagnostic);
            assert(alienSink);
            auto alien = Sink::reserveRequestOriginal(alienSink, request, alienPlan, diagnostic);
            assert(alien);
            const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            auto forgedMap = std::shared_ptr<Mapping>(quotas[0], mapping.get());
            assert(reservation->attachOriginal(forgedMap, statistics->numerical));
            forgedMap.reset();
            auto forgedNumerical =
                std::shared_ptr<WebGpuOriginalTextureReadbackStatisticsFixture::Numerical>(
                    quotas[0], statistics->numerical.get());
            assert(reservation->attachOriginal(mapping, forgedNumerical));
            forgedNumerical.reset();
            assert(std::string_view(alien->attachOriginal(mapping, statistics->numerical))
                    == "RHI-READBACK-PUBLICATION-STATISTICS-MISMATCH"
                && alienSink->pending() == 1u);
            assert(!reservation->attachOriginal(mapping, statistics->numerical)
                && reservation->attachOriginal(mapping, statistics->numerical));
            assert(
                !reservation->rejectBeforeAttachment(Sink::PreparationFailure::MappingPreparation));
            assert(reservation->start() && mapping->status() == Mapping::Status::Prepared
                && sink->pending() == 1u);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
            std::pmr::set_default_resource(previous);
            alien->cancel();
            alien.reset();
            alienSink.reset();
            alienPlan.reset();
        }

        const char* start() { return reservation->start(); }

        void verifyUnavailable()
        {
            const char* diagnostic = nullptr;
            assert(
                !Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic && !delivery.result);
            statistics->verifyUnavailable();
        }

        void verifyPublished()
        {
            const char* diagnostic = nullptr;
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic
                && delivery.outcome == Sink::Outcome::Published && delivery.result
                && !sink->pending());
            assert(core::SharedOwnerProjection::sameOwner(delivery.originalRequest, request)
                && core::SharedOwnerProjection::sameOwner(
                    delivery.numericalResult, statistics->numerical));
            assert(core::SharedOwnerProjection::sameOwner(
                       delivery.numericalResult->originalPlan(), statisticsPlan)
                && a == core::defaultCpuAllocator().stats().allocationCount);
            statistics->verifyPublished(delivery.result);
        }

        void retire()
        {
            delivery = {};
            reservation.reset();
            sink.reset();
            statistics.reset();
            mapping.reset();
            submission.reset();
            recording.reset();
            states.reset();
            pair.reset();
            destination.reset();
            request.reset();
            statisticsPlan.reset();
            recipe = {};
            caller.retire();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

    static void verifyEarly(
        const std::shared_ptr<const WebGpuCompletedTextureReadbackDestinationFixture::View>& view,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        unsigned order)
    {
        EarlyProbe p(view);
        auto packing = view->completedPacking();
        p.prepare(owner, packing);
        record(p.recording);
        const char* diagnostic = nullptr;
        p.submission =
            Submission::prepare(p.recording, p.recipe.submission, p.quotas[1], diagnostic);
        assert(p.submission && !Submission::submit(p.submission));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.submission->status() != Submission::Status::Pending;
        }));
        assert(p.submission->status() == Submission::Status::Completed);
        WebGpuOriginalTextureReadbackNativeMappingFixture::verifyCompleted(
            p.mapping, packing, order, &p);
        p.retire();
        std::printf(
            "BINDLESS_RHI_READBACK_EARLY_ADMISSION_NATIVE_OK %u pending-before-native duplicate-capacity no-create original-map-statistics attach allocation0 actual-delivery quotas4\n",
            order);
    }

    static void verifyEarlyFailure(
        const std::shared_ptr<const WebGpuCompletedTextureReadbackDestinationFixture::View>& view,
        const std::shared_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner>& owner,
        unsigned mode)
    {
        using Native = WebGpuOriginalTextureReadbackNativeMappingFixture;
        using Mapping = EarlyProbe::Mapping;
        using Sink = EarlyProbe::Sink;
        EarlyProbe p(view);
        auto packing = view->completedPacking();
        p.prepare(owner, packing);
        record(p.recording);
        const char* diagnostic = nullptr;
        p.submission =
            Submission::prepare(p.recording, p.recipe.submission, p.quotas[1], diagnostic);
        assert(p.submission && !Submission::submit(p.submission));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.submission->status() != Submission::Status::Pending;
        }));
        assert(p.submission->status() == Submission::Status::Completed);
        const auto buffer = p.destination->nativeResource()->buffer;
        // mode0 drops every readback caller while a real successful map/scopes
        // pair owns the graph; mode2 cancels a native map failure; mode6 delivers
        // a real scope rejection. None can manufacture a terminal native ticket.
        Native::MapPromises::install(buffer, mode, true);
        Native::Scopes::holdScopes();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!p.start());
        Native::MapPromises::restoreMap();
        Native::Scopes::restoreScopes();
        Native::MapPromises::waitHeld();
        Native::Scopes::waitHeldScopes();
        if (mode == 0u) {
            auto draft = p.mapping->resultDeclaration();
            std::weak_ptr<Mapping> weak = p.mapping;
            std::weak_ptr<WebGpuOriginalTextureReadbackStatisticsFixture::Numerical> weakNumeric =
                p.statistics->numerical;
            p.reservation.reset();
            p.sink.reset();
            p.statistics.reset();
            p.mapping.reset();
            p.submission.reset();
            p.recording.reset();
            p.states.reset();
            p.pair.reset();
            p.destination.reset();
            p.request.reset();
            p.statisticsPlan.reset();
            p.recipe = {};
            p.caller.source.reset();
            p.caller.authoring.layout.reset();
            assert(!weak.expired() && !weakNumeric.expired());
            Native::MapPromises::release();
            assert(WebGpuAsyncWaitFixture::until([&] {
                auto original = weak.lock();
                return original && original->mapCompletionObserved();
            }));
            assert(!weak.expired() && !weakNumeric.expired() && !draft->publishedBytes());
            Native::Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return weak.expired();
            }));
            weak.reset();
            assert(weakNumeric.expired());
            weakNumeric.reset();
            assert(draft->publishedBytes());
            Native::verifyRows(draft->publishedBytes(), draft->layout(), packing);
            draft.reset();
            Native::MapPromises::restoreAll();
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
            p.retire();
            std::puts(
                "BINDLESS_RHI_READBACK_EARLY_ADMISSION_DROP_OK actual-map-scopes all-readback-callers-dropped retain-then-retire original-snapshot allocation0");
            return;
        }
        if (mode == 2u) {
            assert(p.reservation->cancel());
            p.reservation.reset();
            assert(p.sink->pending() == 1u);
        }
        Native::MapPromises::release();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.mapping->mapCompletionObserved();
        }));
        p.verifyUnavailable();
        Native::Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return p.mapping->status() == Mapping::Status::Rejected
                && p.mapping->scopeCompletionsObserved();
        }));
        assert(!p.mapping->resultDeclaration()->publishedBytes()
            && p.mapping->scopeCompletionsObserved());
        p.statistics->verifyFailure();
        const auto polled = Sink::pollOriginal(p.sink, p.delivery, diagnostic);
        assert(!diagnostic && !p.sink->pending());
        if (mode == 2u)
            assert(!polled && p.delivery.outcome == Sink::Outcome::Empty);
        else {
            assert(polled && p.delivery.outcome == Sink::Outcome::Rejected && p.delivery.diagnostic
                && !p.delivery.result);
            assert(core::SharedOwnerProjection::sameOwner(p.delivery.originalRequest, p.request)
                && core::SharedOwnerProjection::sameOwner(
                    p.delivery.numericalResult, p.statistics->numerical));
        }
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        Native::MapPromises::restoreAll();
        p.retire();
        std::printf(
            "BINDLESS_RHI_READBACK_EARLY_ADMISSION_FAILURE_OK %u actual-native-reject original-identity canceled-drain-or-delivery allocation0 exact-retire\n",
            mode);
    }

    static void prepareEarlyLoss(
        const std::shared_ptr<const WebGpuCompletedTextureReadbackDestinationFixture::View>& view)
    {
        earlyLoss_ = core::makeUnique<EarlyProbe>(view);
    }

    static void verifyEarlyLoss()
    {
        auto& p = *earlyLoss_;
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        const char* diagnostic = nullptr;
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        using Sink = EarlyProbe::Sink;
        assert(!Sink::reserveRequestOriginal(p.sink, p.request, p.statisticsPlan, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-PREPARATION-DEVICE-LOST");
        assert(Sink::pollOriginal(p.sink, p.delivery, diagnostic) && !diagnostic
            && p.delivery.outcome == Sink::Outcome::DeviceLost && !p.delivery.result
            && !p.delivery.numericalResult);
        assert(core::SharedOwnerProjection::sameOwner(p.delivery.originalRequest, p.request)
            && !p.sink->pending() && a == core::defaultCpuAllocator().stats().allocationCount);
        p.retire();
        earlyLoss_.reset();
        std::puts(
            "BINDLESS_RHI_READBACK_EARLY_ADMISSION_LOSS_OK actual-device-loss unattached-original-request no-result allocation0 exact-retire");
    }

    static void prepareLoss(const std::shared_ptr<Recording>& recording, unsigned mode)
    {
        record(recording);
        lossQuotas_[mode] = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* diagnostic = nullptr;
        lossSubmissions_[mode] =
            Submission::prepare(recording, budget(), lossQuotas_[mode], diagnostic);
        assert(lossSubmissions_[mode]);
        lossCopies_[mode] = recording->originalReadbackCopies();
        if (mode == 0u) {
            Promises::holdActualPromises();
            assert(!Submission::submit(lossSubmissions_[mode]));
            Promises::restorePromiseMethods();
            Promises::waitHeldPromises();
        } else {
            if (mode == 1u)
                EM_ASM({
                    // clang-format off
                        Module.arpReadbackBadSubmit = Module.preinitializedWebGPUDevice.queue.submit;
                        Module.preinitializedWebGPUDevice.queue.submit = function(buffers) {
                            return Module.arpReadbackBadSubmit.call(this, [ buffers[0], buffers[0] ]);
                        };
                    // clang-format on
                });
            if (mode == 3u)
                MutationPromises::holdActualPromises();
            assert(!Submission::submit(lossSubmissions_[mode]));
            if (mode == 3u) {
                MutationPromises::restorePromiseMethods();
                MutationPromises::waitHeldPromises();
                MutationPromises::releaseQueue();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return lossSubmissions_[mode]->queueCompletionObserved();
                }));
                const auto& pair = lossCopies_[mode]
                                       ->transaction()
                                       ->states()
                                       ->inputs()
                                       ->scalars()
                                       ->usage()
                                       ->projection()
                                       ->originalReadbacks()[0];
                auto& buffer = *pair->destination()->nativeResource();
                const auto bytes = buffer.sizeBytes;
                ++buffer.sizeBytes;
                MutationPromises::releaseScopes();
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return lossSubmissions_[mode]->status() == Submission::Status::Unproven;
                }));
                buffer.sizeBytes = bytes;
            }
            if (mode == 1u)
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.queue.submit = Module.arpReadbackBadSubmit;
                        delete Module.arpReadbackBadSubmit;
                    // clang-format on
                });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return lossSubmissions_[mode]->status() != Submission::Status::Pending;
            }));
            assert(mode == 1u || mode == 3u
                    ? lossSubmissions_[mode]->status() == Submission::Status::Unproven
                    : lossSubmissions_[mode]->status() == Submission::Status::Completed);
            assert(mode == 1u || mode == 3u
                    ? lossCopies_[mode]->nativeStatus() == Copies::NativeStatus::Unproven
                    : !Copies::validateNativeCompletionOriginal(lossCopies_[mode]));
        }
    }

    static void verifyLoss(unsigned mode)
    {
        assert(lossCopies_[mode]->nativeStatus() == Copies::NativeStatus::DeviceLost
            && Copies::validateNativeCompletionOriginal(lossCopies_[mode]));
        if (mode != 2u)
            assert(lossSubmissions_[mode]->status() == Submission::Status::DeviceLost);
        if (mode == 0u) {
            Promises::releaseQueue();
            Promises::releaseScopes();
        }
        std::weak_ptr<Submission> weak = lossSubmissions_[mode];
        lossSubmissions_[mode].reset();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        lossCopies_[mode].reset();
        assert(!lossQuotas_[mode]->liveBytes());
        lossQuotas_[mode].reset();
        std::printf(
            "BINDLESS_RHI_READBACK_NATIVE_COMPLETION_LOSS_OK %u actual-device-loss native-proof-revoked late-callback exact-retire\n",
            mode);
    }

private:
    static void verifyBytes(const std::shared_ptr<const Copies>& copies,
        const std::shared_ptr<const rhi::TextureUploadPacking>& packing)
    {
        assert(!Copies::validateNativeCompletionOriginal(copies));
        const auto& pair = copies->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        WebGpuMapHandoffFixture::inspectMappedBuffer(pair->destination()->nativeResource()->buffer,
            pair->destination()->unpublishedResultCapacityBytes(),
            [&] {
                assert(Copies::validateNativeCompletionOriginal(copies)
                    && !Copies::validateNativeCompletedDeclarationOriginal(copies));
                std::array<std::uint8_t, 1536> bytes{};
                assert(wgpuBufferReadMappedRange(pair->destination()->nativeResource()->buffer,
                           0u,
                           bytes.data(),
                           bytes.size())
                    == WGPUStatus_Success);
                WebGpuOriginalTextureReadbackNativeMappingFixture::verifyRows(
                    bytes.data(), pair->layout(), packing);
            });
    }

    static inline core::UniquePtr<EarlyProbe> earlyLoss_;
    static inline std::array<std::shared_ptr<Submission>, 4> lossSubmissions_;
    static inline std::array<std::shared_ptr<const Copies>, 4> lossCopies_;
    static inline std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> lossQuotas_;
#endif
};
} // namespace arp::test
