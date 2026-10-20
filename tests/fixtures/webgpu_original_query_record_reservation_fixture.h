#pragma once
#include "rhi/backends/webgpu/webgpu_original_query_record_reservation.h"
#include "webgpu_original_query_slots_fixture.h"

namespace arp::test {
class WebGpuOriginalQueryRecordReservationFixture final {
    using Reservation = rhi_webgpu::WebGpuOriginalQueryRecordReservation;
    using Budget = rhi_webgpu::WebGpuOriginalQueryRecordReservationBudget;
    using Slots = rhi_webgpu::WebGpuOriginalQuerySlots;
    using Owner = rhi_webgpu::WebGpuOriginalQueryOwner;
    using Binding = rhi_webgpu::WebGpuOriginalQueryBinding;
    using Points = rhi::QueryCommandPoints;
    using A = rhi::CommandScalarArguments;
    using K = rhi::CommandUsageKind;
    using Phase = rhi::common::QuerySlotDeclarationControl::Phase;
    using Promises = WebGpuErrorScopeHandoffFixture<30u>;
    static constexpr std::uint64_t cpuBytes = 65536u;

public:
    static void verifyContract()
    {
        static_assert(!std::is_copy_constructible_v<Reservation>);
        Budget b;
        assert(Reservation::validateBudget(b));
        b.contractVersion = 1u;
        assert(Reservation::validateBudget(b));
        b.prototypeOptIn = true;
        assert(Reservation::validateBudget(b));
        b = {1u, true, 3u, 12u, 6u, 60u, cpuBytes};
        assert(!Reservation::validateBudget(b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Reservation::lowering(backend);
            assert(l.reservationImplemented == (backend == rhi::Backend::WebGPU)
                && l.validatorOnly == (backend == rhi::Backend::Null) && l.diagnostic);
        }
        assert(!Reservation::lowering(static_cast<rhi::Backend>(99u)).reservationImplemented);
        assert(Reservation::reserveOriginal({}) && Reservation::validateOriginal({}));
        const char* diagnostic = nullptr;
        auto q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        assert(!Reservation::prepareOriginal({}, nullptr, 0u, b, q, diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-RECORD-RESERVATION-POINTS-OWNER");
        assert(!q->liveBytes());
        std::puts(
            "BINDLESS_RHI_QUERY_RECORD_RESERVATION_CONTRACT_OK backends4 CPU-only original-point-slot journal native-proof-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Context final {
    public:
        WebGpuOriginalQuerySlotsFixture::Context native;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        std::shared_ptr<Slots> graph;
        std::shared_ptr<const rhi::CommandVariableArguments> stream;
        std::shared_ptr<const Points> points;
        std::optional<std::array<Points::Input, 3>> inputs;
        std::optional<std::array<Slots::Observation, 3>> observations;

        Context(bool duplicate = false)
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            native.bindOwner();
            const char* diagnostic = nullptr;
            graph = native.prepare(native.budget(), diagnostic);
            assert(graph);
            const std::array<std::uint32_t, 3> indices{{0u, duplicate ? 0u : 1u, 3u}};
            const auto scalar = [](K kind) {
                return *A::capture(kind, A::None{});
            };
            const std::string_view label = "原始 query pass";
            const std::array<rhi::CommandVariableArguments::Input, 8> commands{
                {{scalar(K::BeginComputePass), label},
                    {*A::capture(K::Timestamp, A::Query{indices[0]})},
                    {*A::capture(K::Timestamp, A::Query{indices[1]})},
                    {scalar(K::EndComputePass)},
                    {scalar(K::BeginRenderPass)},
                    {*A::capture(K::Timestamp, A::Query{indices[2]})},
                    {scalar(K::EndRenderPass)},
                    {scalar(K::Finish)}}};
            stream = rhi::CommandVariableArguments::prepareOriginal(commands.data(),
                commands.size(),
                {1u, true, 8u, 0u, label.size(), 0u, cpuBytes},
                quotas[0],
                diagnostic);
            assert(stream);
            const auto input = [&](std::uint32_t i) {
                return *rhi::QueryPoolOriginalInputArguments::captureOriginal(
                    K::Timestamp, i, native.authoring.input);
            };
            inputs.emplace(std::array<Points::Input, 3>{
                {{1u, input(indices[0])}, {2u, input(indices[1])}, {5u, input(indices[2])}}});
            points = Points::prepareOriginal(stream,
                inputs->data(),
                inputs->size(),
                {1u,
                    true,
                    8u,
                    2u,
                    3u,
                    0u,
                    label.size(),
                    0u,
                    0u,
                    12u,
                    6u,
                    native.authoring.input->label().size() * 3u,
                    cpuBytes},
                quotas[1],
                diagnostic);
            assert(points);
            observations.emplace(std::array<Slots::Observation, 3>{
                {observe(indices[0]), observe(indices[1]), observe(indices[2])}});
        }

        Slots::Observation observe(std::uint32_t i)
        {
            const char* diagnostic = nullptr;
            const auto command = rhi::QueryPoolOriginalInputArguments::captureOriginal(
                K::Timestamp, i, native.authoring.input);
            const auto binding = Binding::captureOriginal(command,
                native.owner,
                {1u, true, 1u, 4u, 2u, native.authoring.input->label().size()},
                diagnostic);
            assert(binding);
            const auto observation = Slots::observeOriginal(graph, *binding, diagnostic);
            assert(observation);
            return *observation;
        }

        Budget budget() const
        {
            return {1u, true, 3u, 12u, 6u, native.authoring.input->label().size() * 3u, cpuBytes};
        }

        auto prepare(const Budget& b, const char*& diagnostic)
        {
            return Reservation::prepareOriginal(
                points, observations->data(), observations->size(), b, quotas[2], diagnostic);
        }

        void ready()
        {
            assert(!Owner::initialize(native.owner));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return native.owner->status() != Owner::Status::Pending;
            }));
            assert(!Owner::validateReadyOriginal(native.owner));
        }

        void dropCallers()
        {
            observations.reset();
            inputs.reset();
            points.reset();
            stream.reset();
            graph.reset();
            native.dropCallers();
        }

        void retire()
        {
            dropCallers();
            for (const auto& q : quotas)
                assert(!q->liveBytes());
            native.retire();
        }
    };

    static void verifyActual()
    {
        verifyBounds();
        verifyReservation();
        verifyDuplicates();
        verifyLatePreflightFailure();
        verifyDrop();
        verifyPendingDrop();
        std::puts(
            "BINDLESS_RHI_QUERY_RECORD_RESERVATION_NATIVE_OK points3 exact-native-owners pending-journal duplicate-exclusion rollback-ABA full-drop execution-closed allocation0");
    }

    static void prepareActualLoss()
    {
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>();
            auto& c = *lossContexts_[i];
            const char* diagnostic = nullptr;
            lossReservations_[i] = c.prepare(c.budget(), diagnostic);
            assert(lossReservations_[i]);
            if (i == 0u)
                c.ready();
            else if (i == 1u) {
                Promises::holdScopes();
                assert(!Owner::initialize(c.native.owner));
                Promises::restoreScopes();
                Promises::waitHeldScopes();
            }
            assert(!Reservation::reserveOriginal(lossReservations_[i]));
        }
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        std::weak_ptr<Owner> pending = lossContexts_[1]->native.owner;
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            auto& c = *lossContexts_[i];
            auto& reservation = lossReservations_[i];
            assert(std::string_view(Reservation::validateOriginal(reservation))
                == "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST");
            assert(reservation->executionDiagnostic() && !reservation->nativeExecutionProven()
                && !reservation->nativeAvailabilityProven());
            c.dropCallers();
            reservation->abandon();
            assert(reservation->journalEmpty());
            reservation.reset();
            for (const auto& q : c.quotas)
                assert(!q->liveBytes());
            c.native.empty();
        }
        assert(!pending.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.expired();
        }));
        pending.reset();
        for (auto& c : lossContexts_) {
            c->retire();
            c.reset();
        }
        std::puts(
            "BINDLESS_RHI_QUERY_RECORD_RESERVATION_LOSS_OK prepared-pending-ready owners reject rollback late-callbacks exact-retire");
    }

private:
    static void verifyBounds()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto b = c.budget();
        const auto factory = [&] {
            return c.prepare(b, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(c.quotas[2]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !c.quotas[2]->liveBytes());
        };
        --b.maxCommandPoints;
        reject("RHI-QUERY-RECORD-RESERVATION-COVERAGE");
        ++b.maxCommandPoints;
        for (auto* count :
            {&b.maxReferencedQuerySlots, &b.maxReferencedScopeIds, &b.maxReferencedLabelBytes}) {
            --*count;
            reject("RHI-QUERY-RECORD-RESERVATION-CAPACITY");
            ++*count;
        }
        --b.maxCpuMetadataBytes;
        reject("RHI-QUERY-RECORD-RESERVATION-QUOTA");
        ++b.maxCpuMetadataBytes;
        assert(
            !Reservation::prepareOriginal(c.points, c.observations->data(), 3u, b, {}, diagnostic));
        assert(!Reservation::prepareOriginal(c.points, nullptr, 3u, b, c.quotas[2], diagnostic));
        assert(!Reservation::prepareOriginal(
            c.points, c.observations->data(), 2u, b, c.quotas[2], diagnostic));
        {
            core::CpuMemoryQuotaLease full(c.quotas[2], cpuBytes);
            assert(full.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-QUERY-RECORD-RESERVATION-ALLOCATION");
        }
        auto forged = std::shared_ptr<const Points>(c.quotas[0], c.points.get());
        assert(!Reservation::prepareOriginal(
            forged, c.observations->data(), 3u, b, c.quotas[2], diagnostic));
        forged.reset();
        {
            const std::array<Slots::Observation, 3> reordered{
                {(*c.observations)[1], (*c.observations)[0], (*c.observations)[2]}};
            assert(!Reservation::prepareOriginal(
                       c.points, reordered.data(), 3u, b, c.quotas[2], diagnostic)
                && std::string_view(diagnostic) == "RHI-QUERY-RECORD-RESERVATION-ORIGINAL-POINT");
        }
        Context foreign;
        assert(!Reservation::prepareOriginal(
            c.points, foreign.observations->data(), 3u, b, c.quotas[2], diagnostic));
        foreign.retire();
        b.maxCommandPoints = UINT32_MAX;
        b.maxReferencedQuerySlots = UINT64_MAX;
        {
            auto bounded = factory();
            assert(bounded);
        }
        b = c.budget();
        c.retire();
        std::printf(
            "BINDLESS_RHI_QUERY_RECORD_RESERVATION_COLD_OK %llu quota exact-less-exhausted original-point-coverage references3 exact-storage\n",
            static_cast<unsigned long long>(peak));
    }

    static void verifyReservation()
    {
        Context c;
        c.ready();
        const char* diagnostic = nullptr;
        auto reserved = c.prepare(c.budget(), diagnostic),
             competitor = c.prepare(c.budget(), diagnostic);
        assert(reserved && competitor);
        auto forged = std::shared_ptr<Reservation>(c.quotas[0], reserved.get());
        assert(Reservation::reserveOriginal(forged) && Reservation::validateOriginal(forged));
        forged.reset();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        {
            core::CpuMemoryQuotaLease full(
                c.quotas[2], static_cast<std::size_t>(cpuBytes - c.quotas[2]->liveBytes()));
            assert(full.charged());
            assert(!Reservation::reserveOriginal(reserved)
                && reserved->status() == Reservation::Status::Pending && !reserved->journalEmpty());
            assert(!Reservation::validateOriginal(reserved) && reserved->executionDiagnostic());
            assert(
                Reservation::reserveOriginal(reserved) && Reservation::reserveOriginal(competitor));
            for (const auto& original : *c.observations)
                assert(Slots::validateCurrentOriginal(c.graph, original));
            assert(Slots::resetOriginal(c.graph)
                && !Slots::observeOriginal(
                    c.graph, (*c.observations)[0].originalBinding(), diagnostic));
            reserved->abandon();
            assert(reserved->journalEmpty() && Reservation::validateOriginal(reserved));
            assert(Reservation::reserveOriginal(competitor));
            for (const auto& original : *c.observations)
                assert(Slots::validateCurrentOriginal(c.graph, original));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(old);
        reserved.reset();
        competitor.reset();
        c.observations.emplace(
            std::array<Slots::Observation, 3>{{c.observe(0u), c.observe(1u), c.observe(3u)}});
        for (const auto& observation : *c.observations)
            assert(observation.phase() == Phase::NotRecorded && observation.revision() > 1u);
        auto fresh = c.prepare(c.budget(), diagnostic);
        assert(fresh && !Reservation::reserveOriginal(fresh));
        fresh.reset();
        c.observations.emplace(
            std::array<Slots::Observation, 3>{{c.observe(0u), c.observe(1u), c.observe(3u)}});
        fresh = c.prepare(c.budget(), diagnostic);
        assert(fresh && !Reservation::reserveOriginal(fresh));
        assert(!Slots::invalidateOriginal(c.graph));
        assert(Reservation::reserveOriginal(fresh) && Reservation::validateOriginal(fresh));
        fresh->abandon();
        assert(fresh->journalEmpty());
        assert(
            !Slots::observeOriginal(c.graph, (*c.observations)[0].originalBinding(), diagnostic));
        fresh.reset();
        c.retire();
    }

    static void verifyDuplicates()
    {
        Context c(true);
        const char* diagnostic = nullptr;
        assert(!c.prepare(c.budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-RECORD-RESERVATION-DUPLICATE-SLOT");
        auto owner = c.native.authoring.prepareDefault(diagnostic);
        assert(owner);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes),
             slotQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto graph =
            Slots::prepareOriginal(owner, c.native.budget(), graphQuota, slotQuota, diagnostic);
        assert(graph);
        auto binding = Binding::captureOriginal((*c.inputs)[1].original,
            owner,
            {1u, true, 1u, 4u, 2u, c.native.authoring.input->label().size()},
            diagnostic);
        assert(binding);
        auto observation = Slots::observeOriginal(graph, *binding, diagnostic);
        assert(observation);
        {
            const std::array<Slots::Observation, 3> distinct{
                {(*c.observations)[0], *observation, (*c.observations)[2]}};
            auto reservation = Reservation::prepareOriginal(
                c.points, distinct.data(), 3u, c.budget(), c.quotas[2], diagnostic);
            assert(reservation);
            assert(!Reservation::reserveOriginal(reservation)
                && !Reservation::validateOriginal(reservation));
            reservation.reset();
        }
        observation.reset();
        binding.reset();
        graph.reset();
        owner.reset();
        assert(!graphQuota->liveBytes() && !slotQuota->liveBytes());
        c.retire();
    }

    static void verifyDrop()
    {
        Context c;
        c.ready();
        const char* diagnostic = nullptr;
        auto reservation = c.prepare(c.budget(), diagnostic);
        assert(reservation && !Reservation::reserveOriginal(reservation));
        std::weak_ptr<const Points> points = c.points;
        std::weak_ptr<Slots> graph = c.graph;
        std::weak_ptr<Owner> owner = c.native.owner;
        c.dropCallers();
        assert(!points.expired() && !graph.expired() && !owner.expired()
            && !Reservation::validateOriginal(reservation));
        assert(reservation->originalPoints()->originalStream()->text(0u) == "原始 query pass");
        reservation.reset();
        assert(points.expired() && graph.expired() && owner.expired());
        points.reset();
        graph.reset();
        owner.reset();
        c.retire();
    }

    static void verifyLatePreflightFailure()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto owner = c.native.authoring.prepareDefault(diagnostic);
        assert(owner);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes),
             slotQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto graph =
            Slots::prepareOriginal(owner, c.native.budget(), graphQuota, slotQuota, diagnostic);
        assert(graph);
        auto binding = Binding::captureOriginal((*c.inputs)[2].original,
            owner,
            {1u, true, 1u, 4u, 2u, c.native.authoring.input->label().size()},
            diagnostic);
        assert(binding);
        auto last = Slots::observeOriginal(graph, *binding, diagnostic);
        assert(last);
        {
            const std::array<Slots::Observation, 3> mixed{
                {(*c.observations)[0], (*c.observations)[1], *last}};
            auto reservation = Reservation::prepareOriginal(
                c.points, mixed.data(), 3u, c.budget(), c.quotas[2], diagnostic);
            assert(reservation);
            assert(!Slots::resetOriginal(graph));
            assert(std::string_view(Reservation::reserveOriginal(reservation))
                == "RHI-QUERY-SLOTS-STALE-REVISION");
            assert(reservation->status() == Reservation::Status::Prepared
                && reservation->journalEmpty());
            // A stale final entry must leave every earlier slot entirely unchanged.
            for (std::size_t i = 0u; i < 2u; ++i)
                assert(!Slots::validateCurrentOriginal(c.graph, mixed[i])
                    && c.observe(static_cast<std::uint32_t>(i)).revision() == 1u);
        }
        last.reset();
        binding.reset();
        graph.reset();
        owner.reset();
        assert(!graphQuota->liveBytes() && !slotQuota->liveBytes());
        c.retire();
    }

    static void verifyPendingDrop()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto reservation = c.prepare(c.budget(), diagnostic);
        assert(reservation && !Reservation::reserveOriginal(reservation));
        Promises::holdScopes();
        assert(!Owner::initialize(c.native.owner));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        std::weak_ptr<Owner> owner = c.native.owner;
        c.dropCallers();
        assert(!Reservation::validateOriginal(reservation));
        reservation.reset();
        for (const auto& q : c.quotas)
            assert(!q->liveBytes());
        c.native.empty();
        assert(!owner.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner.expired();
        }));
        owner.reset();
        c.retire();
    }

    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::shared_ptr<Reservation>, 3> lossReservations_;
#endif
};
} // namespace arp::test
