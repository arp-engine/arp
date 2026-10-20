#pragma once
#include "rhi/backends/webgpu/webgpu_readback_results.h"
#include "rhi/frontend/transfer_texture_readback_batch.h"
#include "texture_readback_cold_budget_fixture.h"

namespace arp::test {
class WebGpuOriginalTextureReadbackEarlyAdmissionFixture final {
public:
    using Sink = rhi_webgpu::WebGpuOriginalTextureReadbackPublication;
    using Request = rhi::TextureReadbackRequestDeclaration;
    using Statistics = rhi::TextureReadbackStatisticsPlan;
    using Recipe = TextureReadbackColdBudgetFixture::Recipe;
    using Failure = Sink::PreparationFailure;

    class Context final {
    public:
        TextureReadbackLayoutFixture::Context caller;
        Recipe recipe;
        rhi::TextureReadbackColdBudgetPlan::Quotas quotas;
        std::shared_ptr<rhi_webgpu::WebGpuRhiResourceRegistry> registry =
            core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
        std::shared_ptr<const rhi::TextureReadbackLayout> layout;
        std::shared_ptr<const Request> request;
        std::shared_ptr<const Statistics> statistics;
        std::shared_ptr<Sink> sink;

        Context()
        {
            const char* diagnostic = nullptr;
            layout = caller.prepare(TextureReadbackLayoutFixture::descriptor(),
                TextureReadbackLayoutFixture::texture(),
                diagnostic);
            assert(layout);
            auto b = TextureReadbackColdBudgetFixture::budget();
            b.maxResultBytesPerRequest = b.maxStagingBytesPerRequest =
                b.maxDeviceRetainedResultBytes = b.maxDeviceStagingBytes = 5120u;
            b.maxAnalyzedPixelsPerRequest = 40u;
            recipe = TextureReadbackColdBudgetFixture::prepare(b);
            quotas = recipe.originalPlan->quotas();
            request = makeRequest({rhi::allocateTextureReadbackRequestId(), 37u, 43u});
            statistics =
                Statistics::prepareOriginal(layout, recipe.statistics, quotas[1], diagnostic);
            assert(statistics);
            sink = Sink::prepareForDevicePlan(
                recipe.originalPlan, registry, recipe.publication, diagnostic);
            assert(sink);
        }

        std::shared_ptr<const Request> makeRequest(rhi::TextureReadbackIdentity id)
        {
            const char* diagnostic = nullptr;
            auto request =
                Request::prepareOriginal(layout, id, recipe.request, quotas[1], diagnostic);
            assert(request);
            return request;
        }

        void retire()
        {
            sink.reset();
            statistics.reset();
            request.reset();
            layout.reset();
            recipe = {};
            caller.retired();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

    static void verifyContract()
    {
        Context c;
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Sink::reserveRequestOriginal(c.sink, c.request, c.statistics, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(c.quotas[1]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(
                c.quotas[1], c.quotas[1]->limitBytes() - c.quotas[1]->liveBytes());
            assert(full.charged() && !factory());
        }
        assert(!c.sink->pending());
        assert(!Sink::reserveOriginal(c.sink, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-EARLY-ADMISSION-REQUIRED");
        auto b = c.recipe.publication;
        --b.maxMapCallbacks;
        assert(!Sink::prepareForDevicePlan(c.recipe.originalPlan, c.registry, b, diagnostic));
        auto forgedRequest = std::shared_ptr<const Request>(c.quotas[0], c.request.get());
        assert(!Sink::reserveRequestOriginal(c.sink, forgedRequest, c.statistics, diagnostic));
        forgedRequest.reset();
        auto forgedPlan = std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>(
            c.quotas[1], c.recipe.originalPlan.get());
        assert(
            !Sink::prepareForDevicePlan(forgedPlan, c.registry, c.recipe.publication, diagnostic));
        forgedPlan.reset();
        auto external = core::makeShared<core::CpuMemoryQuota>(c.quotas[1]->limitBytes());
        auto foreignStats =
            Statistics::prepareOriginal(c.layout, c.recipe.statistics, external, diagnostic);
        assert(foreignStats
            && !Sink::reserveRequestOriginal(c.sink, c.request, foreignStats, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REQUEST-QUOTA");
        foreignStats.reset();
        assert(!external->liveBytes());
        auto duplicate = c.makeRequest(c.request->identity());
        auto other = c.makeRequest({rhi::allocateTextureReadbackRequestId(), 37u, 43u});
        auto reservation = factory();
        assert(reservation && c.sink->pending() == 1u);
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Sink::reserveRequestOriginal(c.sink, duplicate, c.statistics, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE");
        assert(!Sink::reserveRequestOriginal(c.sink, other, c.statistics, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-CAPACITY");
        assert(reservation->bindRecordingOriginal({}) && reservation->bindSubmissionOriginal({})
            && reservation->recordOriginal() && reservation->submitOriginal()
            && c.sink->pending() == 1u);
        assert(std::string_view(reservation->initializeDestinationOriginal({}))
                == "RHI-READBACK-PUBLICATION-PREPARATION-DESTINATION-OWNER"
            && c.sink->pending() == 1u);
        assert(std::string_view(reservation->start()) == "RHI-READBACK-PUBLICATION-NOT-ATTACHED");
        assert(std::string_view(reservation->attachOriginal({}, {}))
                == "RHI-READBACK-PUBLICATION-MAP-OWNER"
            && c.sink->pending() == 1u);
        Sink::Delivery delivery;
        assert(!Sink::pollOriginal(c.sink, delivery, diagnostic) && !diagnostic);
        auto moved = std::move(*reservation);
        assert(!*reservation && reservation->attachOriginal({}, {})
            && reservation->initializeDestinationOriginal({}) && moved);
        reservation.reset();
        assert(moved.cancel() && !c.sink->pending() && !moved && !moved.cancel());
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        for (const auto failure : {Failure::SourceContract,
                 Failure::DestinationCreation,
                 Failure::RecordingPreparation,
                 Failure::MappingPreparation}) {
            reservation = factory();
            assert(reservation && !reservation->rejectBeforeAttachment(Failure::None)
                && !reservation->rejectBeforeAttachment(static_cast<Failure>(99)));
            assert(reservation->rejectBeforeAttachment(failure)
                && !reservation->rejectBeforeAttachment(failure)
                && reservation->attachOriginal({}, {}) && reservation->start());
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            assert(Sink::pollOriginal(c.sink, delivery, diagnostic) && !diagnostic
                && delivery.outcome == Sink::Outcome::Rejected && delivery.diagnostic
                && !delivery.result && !delivery.numericalResult);
            assert(core::SharedOwnerProjection::sameOwner(delivery.originalRequest, c.request)
                && core::SharedOwnerProjection::sameOwner(
                    delivery.originalInput, c.layout->inputOwner()));
            assert(before == core::defaultCpuAllocator().stats().allocationCount
                && !c.sink->pending() && !*reservation);
            auto stale = std::move(*reservation);
            reservation.reset();
            delivery = {};
            reservation = factory();
            assert(reservation && c.sink->pending() == 1u);
            assert(stale.attachOriginal({}, {}) && stale.start() && !stale.cancel()
                && c.sink->pending() == 1u);
            assert(reservation->cancel());
            reservation.reset();
        }
        reservation = factory();
        std::weak_ptr<Sink> weak = c.sink;
        c.sink.reset();
        assert(!weak.expired());
        assert(reservation->cancel());
        reservation.reset();
        assert(weak.expired());
        weak.reset();
        duplicate.reset();
        other.reset();
        c.retire();
        std::printf(
            "BINDLESS_RHI_READBACK_EARLY_ADMISSION_CONTRACT_OK %llu exact-less-exhausted Request-domain plan-quotas duplicate-capacity no-map preserve-failure reject4 moved-drop allocation0 no-native-proof\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
