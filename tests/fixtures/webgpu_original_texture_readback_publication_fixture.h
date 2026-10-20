#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_readback_results.h"
#include "rhi/frontend/transfer_texture_readback_batch.h"
#include "texture_readback_cold_budget_fixture.h"
#include "texture_readback_request_declaration_fixture.h"
#include "webgpu_original_texture_readback_early_admission_fixture.h"
#include "webgpu_original_texture_readback_statistics_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class WebGpuOriginalTextureReadbackPublicationFixture final {
public:
    using Sink = rhi_webgpu::WebGpuOriginalTextureReadbackPublication;
    using Budget = rhi_webgpu::WebGpuOriginalTextureReadbackPublicationBudget;
    using Mapping = rhi_webgpu::WebGpuOriginalTextureReadbackMapping;
    static constexpr std::uint64_t cpuBytes = 262144u;

    static void verifyContract()
    {
        WebGpuOriginalTextureReadbackStatisticsFixture::verifyContract();
        TextureReadbackRequestDeclarationFixture::verify();
        TextureReadbackColdBudgetFixture::verify();
        WebGpuOriginalTextureReadbackEarlyAdmissionFixture::verifyContract();
        static_assert(!std::is_copy_constructible_v<Sink>
            && !std::is_copy_constructible_v<Sink::Reservation>);
        Sink::Reservation empty;
        assert(!empty && empty.start() && !empty.cancel());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(std::string_view(empty.bindDestinationOriginalCold({}))
            == "RHI-READBACK-PUBLICATION-RESERVATION");
        assert(std::string_view(empty.initializeBoundDestinationOriginal())
            == "RHI-READBACK-PUBLICATION-RESERVATION");
        assert(std::string_view(empty.bindRecordingAndMappingOriginalCold({}, {}, {}))
            == "RHI-READBACK-PUBLICATION-RESERVATION");
        assert(std::string_view(rhi_webgpu::WebGpuNativeBufferCreation::initialize({}))
            == "RHI-READBACK-BUFFER-CREATION-OWNER");
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_READBACK_CREATION_COLD_HOT_CONTRACT_OK backends4 original-destination Request-drain Resource-envelope hot-no-allocation no-native-proof");
        Budget b;
        assert(Sink::validateBudget(b));
        b.contractVersion = 1u;
        assert(Sink::validateBudget(b));
        b.prototypeOptIn = true;
        assert(Sink::validateBudget(b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Sink::lowering(backend);
            assert(l.validatorOnly == (backend == rhi::Backend::Null)
                && l.sinkImplemented == (backend == rhi::Backend::WebGPU) && l.diagnostic);
        }
        assert(!Sink::lowering(static_cast<rhi::Backend>(99u)).sinkImplemented);
        const char* diagnostic = nullptr;
        Sink::Delivery delivery;
        assert(!Sink::pollOriginal({}, delivery, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-SINK-OWNER");
        assert(!Sink::reserveOriginal({}, {}, diagnostic));
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        b = {1u, true, 1u, 4u, 2u, 4432u, 1696u, 1536u, 64u, cpuBytes, cpuBytes};
        assert(!Sink::validateBudget(b));
        auto sink = Sink::prepareOriginal(b, q, q, diagnostic);
        assert(sink && !sink->pending() && !sink->nativeExecutionProven());
        assert(!Sink::reserveOriginal(sink, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-MAP-OWNER");
        assert(!Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic);
        sink.reset();
        assert(!q->liveBytes());
        std::puts(
            "BINDLESS_RHI_READBACK_PUBLICATION_CONTRACT_OK backends4 cold-original-reservation opaque-snapshot native-evidence-only");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        std::shared_ptr<Mapping> mapping;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> quotas;
        std::shared_ptr<Sink> sink;
        std::optional<Sink::Reservation> reservation;
        Sink::Delivery delivery;
        std::optional<WebGpuOriginalTextureReadbackStatisticsFixture::Context> statistics;
        std::shared_ptr<core::CpuMemoryQuota> requestQuota = core::makeShared<core::CpuMemoryQuota>(
            TextureReadbackRequestDeclarationFixture::cpuBytes);
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request;

        explicit Context(std::shared_ptr<Mapping> original,
            bool verifyBounds = false,
            const std::shared_ptr<const rhi::TextureUploadPacking>& packing = {})
            : mapping(std::move(original))
        {
            statistics.emplace(mapping->resultDeclaration(), packing, verifyBounds);
            statistics->attachNumerical(mapping, verifyBounds);
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const char* diagnostic = nullptr;
            auto b = budget();
            const auto& layout = mapping->resultDeclaration()->layout();
            request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(layout,
                {rhi::allocateTextureReadbackRequestId(), 37u, 43u},
                {1u,
                    true,
                    static_cast<std::uint32_t>(layout->mipCopies().size()),
                    layout->inputOwner()->label().size(),
                    mapping->resultDeclaration()->byteCapacity(),
                    requestQuota->limitBytes()},
                requestQuota,
                diagnostic);
            assert(request);
            const auto registry = mapping->copies()
                                      ->transaction()
                                      ->states()
                                      ->inputs()
                                      ->scalars()
                                      ->usage()
                                      ->projection()
                                      ->originalReadbacks()[0]
                                      ->destination()
                                      ->source()
                                      ->registry();
            const auto factory = [&] {
                return Sink::prepareForOriginalRegistry(
                    registry, b, quotas[0], quotas[1], diagnostic);
            };
            std::array<std::uint64_t, 2> peaks{};
            if (verifyBounds) {
                CpuQuotaFactoryFixture sinkPressure(quotas[0]);
                peaks[0] = sinkPressure.measure(factory);
                sinkPressure.verify(peaks[0], factory);
                --b.maxCpuSinkBytes;
                assert(!factory());
                ++b.maxCpuSinkBytes;
                --b.maxCpuRecordBytes;
                assert(!factory());
                ++b.maxCpuRecordBytes;
                b.maxPendingResults = UINT32_MAX;
                assert(!factory()
                    && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-ALLOCATION");
                b = budget();
                {
                    core::CpuMemoryQuotaLease full(quotas[0], cpuBytes);
                    assert(full.charged() && !factory());
                }
            }
            sink = factory();
            assert(sink);
            const auto reserve = [&] {
                return Sink::reserveTrackedOriginal(
                    sink, mapping, request, statistics->numerical, diagnostic);
            };
            if (verifyBounds) {
                CpuQuotaFactoryFixture recordPressure(quotas[1]);
                peaks[1] = recordPressure.measure(reserve);
                recordPressure.verify(peaks[1], reserve);
                {
                    core::CpuMemoryQuotaLease full(quotas[1], cpuBytes);
                    assert(full.charged() && !reserve()
                        && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-ALLOCATION");
                }
                for (auto* value : {&b.maxRetainedNativeBytes,
                         &b.maxRetainedContentBytes,
                         &b.maxResultBytes,
                         &b.maxLabelBytes}) {
                    --*value;
                    auto constrained = factory();
                    assert(constrained && !Sink::reserveOriginal(constrained, mapping, diagnostic)
                        && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-CAPACITY");
                    ++*value;
                }
                --b.maxMipLayouts;
                {
                    auto constrained = factory();
                    assert(constrained && !Sink::reserveOriginal(constrained, mapping, diagnostic));
                }
                ++b.maxMipLayouts;
                auto forged = std::shared_ptr<Mapping>(quotas[0], mapping.get());
                assert(!Sink::reserveOriginal(sink, forged, diagnostic));
                forged.reset();
                auto forgedSink = std::shared_ptr<Sink>(quotas[0], sink.get());
                assert(!Sink::reserveOriginal(forgedSink, mapping, diagnostic)
                    && !Sink::pollOriginal(forgedSink, delivery, diagnostic));
                forgedSink.reset();
            }
            auto forgedRequest = std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>(
                requestQuota, request.get());
            assert(!Sink::reserveTrackedOriginal(
                sink, mapping, forgedRequest, statistics->numerical, diagnostic));
            auto forgedNumerical =
                std::shared_ptr<WebGpuOriginalTextureReadbackStatisticsFixture::Numerical>(
                    requestQuota, statistics->numerical.get());
            assert(
                !Sink::reserveTrackedOriginal(sink, mapping, request, forgedNumerical, diagnostic));
            auto wrongRegistry = core::makeShared<rhi_webgpu::WebGpuRhiResourceRegistry>();
            auto foreignSink = Sink::prepareForOriginalRegistry(
                wrongRegistry, b, quotas[0], quotas[1], diagnostic);
            assert(foreignSink);
            assert(!Sink::reserveTrackedOriginal(
                       foreignSink, mapping, request, statistics->numerical, diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REGISTRY-MISMATCH");
            foreignSink.reset();
            wrongRegistry.reset();
            reservation = reserve();
            assert(reservation && sink->pending() == 1u);
            assert(!reserve()
                && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE");
            auto other = factory();
            assert(other && !Sink::reserveOriginal(other, mapping, diagnostic)
                && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-SOURCE-BUSY");
            other.reset();
            assert(std::string_view(Mapping::start(mapping))
                == "RHI-WEBGPU-READBACK-MAP-PUBLICATION-OWNER-REQUIRED");
            assert(reservation->start() && mapping->status() == Mapping::Status::Prepared);
            verifyUnavailable();
            // Move-only cancellation retires the original slot/source ticket and
            // permits exact cold retry without another native or result object.
            auto moved = std::move(*reservation);
            reservation.reset();
            assert(moved && moved.cancel() && !sink->pending() && !quotas[1]->liveBytes());
            reservation = reserve();
            assert(reservation && sink->pending() == 1u);
            if (verifyBounds)
                std::printf(
                    "BINDLESS_RHI_READBACK_PUBLICATION_COLD_OK %llu %llu quotas2 source-exclusive exact-less-exhausted bounds cancel-retry\n",
                    static_cast<unsigned long long>(peaks[0]),
                    static_cast<unsigned long long>(peaks[1]));
        }

        Budget budget() const
        {
            const auto& pair = mapping->copies()
                                   ->transaction()
                                   ->states()
                                   ->inputs()
                                   ->scalars()
                                   ->usage()
                                   ->projection()
                                   ->originalReadbacks()[0];
            const auto& result = mapping->resultDeclaration();
            return {1u,
                true,
                1u,
                Mapping::callbackCount + Mapping::scopeCallbackCount,
                static_cast<std::uint32_t>(result->layout()->mipCopies().size()),
                pair->retainedNativeAllocationBytes(),
                pair->destination()->source()->retainedContentBytes(),
                result->byteCapacity(),
                result->layout()->inputOwner()->label().size(),
                cpuBytes,
                cpuBytes};
        }

        const char* start() { return reservation->start(); }

        void verifyUnavailable()
        {
            statistics->verifyUnavailable();
            const char* diagnostic = nullptr;
            assert(!Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic
                && delivery.outcome == Sink::Outcome::Empty && !delivery.result);
        }

        void verifyPublished()
        {
            assert(statistics->numerical->outcome()
                == WebGpuOriginalTextureReadbackStatisticsFixture::Numerical::Outcome::Completed);
            const char* diagnostic = nullptr;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            {
                core::CpuMemoryQuotaLease full(
                    quotas[1], static_cast<std::size_t>(cpuBytes - quotas[1]->liveBytes()));
                assert(full.charged());
                assert(Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic);
                assert(delivery.outcome == Sink::Outcome::Published && delivery.result
                    && delivery.result->publishedBytes());
                assert(core::SharedOwnerProjection::sameOwner(
                           delivery.result, mapping->resultDeclaration())
                    && core::SharedOwnerProjection::sameOwner(
                        delivery.originalInput, delivery.result->layout()->inputOwner()));
                assert(!Sink::pollOriginal(sink, delivery, diagnostic)
                    && std::string_view(diagnostic) == "RHI-READBACK-PUBLICATION-OUTPUT-BUSY");
                assert(!sink->pending() && !*reservation && reservation->start());
            }
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations
                && !quotas[1]->liveBytes());
            statistics->verifyPublished(delivery.result);
            verifyTracked();
            std::puts(
                "BINDLESS_RHI_READBACK_PUBLICATION_NATIVE_OK actual-map-scopes original-padded result-input-storage-identical busy-output allocation0");
        }

        void verifyRejected(bool loss = false)
        {
            if (!loss)
                assert(statistics->numerical->outcome()
                    == WebGpuOriginalTextureReadbackStatisticsFixture::Numerical::Outcome::
                        Rejected);
            const char* diagnostic = nullptr;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic && !delivery.result
                && delivery.originalInput && delivery.diagnostic);
            assert(
                delivery.outcome == (loss ? Sink::Outcome::DeviceLost : Sink::Outcome::Rejected));
            assert(std::string_view(delivery.diagnostic) == mapping->diagnostic()
                && !sink->pending() && !quotas[1]->liveBytes());
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            statistics->verifyFailure(loss);
            verifyTracked();
            std::puts(loss
                    ? "BINDLESS_RHI_READBACK_PUBLICATION_LOSS_OK actual-loss unpublished-source retired original-input diagnostic allocation0"
                    : "BINDLESS_RHI_READBACK_PUBLICATION_FAILURE_OK actual-native-terminal unpublished-result callbacks-all-observed retired allocation0");
        }

        void cancelPending()
        {
            assert(reservation && reservation->cancel());
            reservation.reset();
            assert(sink->pending() == 1u && quotas[1]->liveBytes());
            verifyUnavailable();
        }

        void verifySuppressed()
        {
            const char* diagnostic = nullptr;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Sink::pollOriginal(sink, delivery, diagnostic) && !diagnostic
                && delivery.outcome == Sink::Outcome::Empty);
            assert(!sink->pending() && !quotas[1]->liveBytes()
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
            statistics->verifyFailure();
            std::puts(
                "BINDLESS_RHI_READBACK_PUBLICATION_CANCEL_OK actual-issued-map suppressed-output terminal-drain original-callback-retirement allocation0");
        }

        void verifyTracked() const
        {
            assert(core::SharedOwnerProjection::sameOwner(delivery.originalRequest, request)
                && core::SharedOwnerProjection::sameOwner(
                    delivery.numericalResult, statistics->numerical));
            assert(delivery.originalRequest->identity() == request->identity()
                && core::SharedOwnerProjection::sameOwner(
                    delivery.originalRequest->originalLayout()->inputOwner(),
                    delivery.originalInput));
            std::puts(
                "BINDLESS_RHI_READBACK_TRACKED_PRIVATE_OK exact-caller-identity original-request-numerical registry-scoped allocation0");
        }

        void retire()
        {
            request.reset();
            statistics.reset();
            reservation.reset();
            sink.reset();
            delivery = {};
            mapping.reset();
            assert(!requestQuota->liveBytes());
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }

        ~Context() { retire(); }
    };
#endif
};
} // namespace arp::test
