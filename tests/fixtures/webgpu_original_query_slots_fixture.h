#pragma once
#include "rhi/backends/webgpu/webgpu_original_query_slots.h"
#include "webgpu_original_query_owner_fixture.h"

namespace arp::test {
class WebGpuOriginalQuerySlotsFixture final {
    using Slots = rhi_webgpu::WebGpuOriginalQuerySlots;
    using Owner = rhi_webgpu::WebGpuOriginalQueryOwner;
    using Binding = rhi_webgpu::WebGpuOriginalQueryBinding;
    using Budget = rhi_webgpu::WebGpuOriginalQuerySlotsBudget;
    using Phase = rhi::common::QuerySlotDeclarationControl::Phase;
    using Authoring = WebGpuOriginalQueryOwnerFixture::Context;
    using Promises = WebGpuErrorScopeHandoffFixture<29u>;
    static constexpr std::uint64_t cpuBytes = 65536u;

public:
    class Context final {
    public:
        Authoring authoring;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> quotas;
        std::shared_ptr<Owner> owner;
        std::optional<Binding> binding;

        Context()
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        }

        Budget budget() const
        {
            return {1u, true, 1u, 4u, 2u, authoring.input->label().size(), cpuBytes, cpuBytes};
        }

        void bindOwner()
        {
            const char* diagnostic = nullptr;
            owner = authoring.prepareDefault(diagnostic);
            assert(owner);
            auto input = rhi::QueryPoolOriginalInputArguments::captureOriginal(
                rhi::CommandUsageKind::Timestamp, 3u, authoring.input);
            auto original = Binding::captureOriginal(
                input, owner, {1u, true, 1u, 4u, 2u, authoring.input->label().size()}, diagnostic);
            assert(original);
            binding.emplace(*original);
        }

        auto prepare(const Budget& b, const char*& diagnostic)
        {
            return Slots::prepareOriginal(owner, b, quotas[0], quotas[1], diagnostic);
        }

        void empty() const
        {
            for (const auto& q : quotas)
                if (q)
                    assert(!q->liveBytes());
        }

        void dropCallers()
        {
            binding.reset();
            owner.reset();
            authoring.input.reset();
        }

        void retire()
        {
            dropCallers();
            empty();
            authoring.retire();
        }
    };

    static void verifyContract()
    {
        static_assert(!std::is_default_constructible_v<Slots::Observation>);
        static_assert(!std::is_copy_constructible_v<Slots>);
        Context c;
        const char* diagnostic = nullptr;
        auto b = c.budget();
        assert(!Slots::validateBudget(b));
        b.contractVersion = 0u;
        assert(Slots::validateBudget(b));
        b = c.budget();
        b.prototypeOptIn = false;
        assert(Slots::validateBudget(b));
        b = c.budget();
        b.maxQuerySlots = 0u;
        assert(Slots::validateBudget(b));
        b = c.budget();
        b.maxNativeOwners = 0u;
        assert(Slots::validateBudget(b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto l = Slots::lowering(backend);
            assert(l.coldGraphImplemented == (backend == rhi::Backend::WebGPU)
                && l.validatorOnly == (backend == rhi::Backend::Null) && l.diagnostic);
        }
        assert(!Slots::lowering(static_cast<rhi::Backend>(99u)).coldGraphImplemented);
        assert(!c.prepare(c.budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-SLOTS-NATIVE-OWNER");
        assert(Slots::resetOriginal({}) && Slots::invalidateOriginal({}));
        c.retire();
        std::puts(
            "BINDLESS_RHI_QUERY_SLOTS_CONTRACT_OK backends4 exact-original exclusive-ticket stable-controls native-proof-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        verifyBounds();
        verifyCurrent();
        verifyRejected();
        verifyDrop();
        verifyPendingDrop();
        std::puts(
            "BINDLESS_RHI_QUERY_SLOTS_NATIVE_OK slots4 exclusive graph binding-index revision reset-stale full-drop native-execution-closed allocation0");
    }

    static void prepareActualLoss()
    {
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>();
            auto& c = *lossContexts_[i];
            c.bindOwner();
            const char* diagnostic = nullptr;
            lossGraphs_[i] = c.prepare(c.budget(), diagnostic);
            assert(lossGraphs_[i]);
            auto observed = Slots::observeOriginal(lossGraphs_[i], *c.binding, diagnostic);
            assert(observed);
            lossObservations_[i].emplace(*observed);
            if (i == 0u) {
                assert(!Owner::initialize(c.owner));
                waitReady(c.owner);
            } else if (i == 1u) {
                Promises::holdScopes();
                assert(!Owner::initialize(c.owner));
                Promises::restoreScopes();
                Promises::waitHeldScopes();
            }
        }
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        std::weak_ptr<Owner> pending = lossContexts_[1]->owner;
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            auto& c = *lossContexts_[i];
            const char* diagnostic = nullptr;
            assert(std::string_view(
                       Slots::validateCurrentOriginal(lossGraphs_[i], *lossObservations_[i]))
                == "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST");
            assert(Slots::executionDiagnostic(lossGraphs_[i], *lossObservations_[i])
                && Slots::resetOriginal(lossGraphs_[i]) && !c.prepare(c.budget(), diagnostic));
            lossGraphs_[i].reset();
            lossObservations_[i].reset();
            c.empty();
            c.dropCallers();
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
            "BINDLESS_RHI_QUERY_SLOTS_LOSS_OK prepared-pending-ready reject exclusive-release late-native-callbacks exact-quota-retire");
    }

private:
    static void waitReady(const std::shared_ptr<Owner>& owner)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner->status() != Owner::Status::Pending;
        }));
        assert(!Owner::validateReadyOriginal(owner));
    }

    static void verifyBounds()
    {
        Context c;
        c.bindOwner();
        const char* diagnostic = nullptr;
        auto b = c.budget();
        const auto factory = [&] {
            return c.prepare(b, diagnostic);
        };
        std::array<std::uint64_t, 2> peaks{};
        for (std::size_t i = 0u; i < peaks.size(); ++i) {
            CpuQuotaFactoryFixture fixture(c.quotas[i]);
            peaks[i] = fixture.measure(factory);
            fixture.verify(peaks[i], factory);
            c.empty();
            {
                core::CpuMemoryQuotaLease full(c.quotas[i], static_cast<std::size_t>(cpuBytes));
                assert(full.charged() && !factory());
            }
            c.empty();
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
            c.empty();
        };
        --b.maxQuerySlots;
        reject("RHI-QUERY-SLOTS-CAPACITY");
        ++b.maxQuerySlots;
        --b.maxScopeIds;
        reject("RHI-QUERY-SLOTS-CAPACITY");
        ++b.maxScopeIds;
        --b.maxLabelBytes;
        reject("RHI-QUERY-SLOTS-CAPACITY");
        ++b.maxLabelBytes;
        for (std::size_t i = 0u; i < c.quotas.size(); ++i) {
            auto q = c.quotas[i];
            c.quotas[i].reset();
            reject("RHI-QUERY-SLOTS-QUOTA");
            c.quotas[i] = core::makeShared<core::CpuMemoryQuota>(cpuBytes + 1u);
            reject("RHI-QUERY-SLOTS-QUOTA");
            c.quotas[i] = q;
        }
        auto forged = std::shared_ptr<const Owner>(c.quotas[0], c.owner.get());
        assert(!Slots::prepareOriginal(forged, b, c.quotas[0], c.quotas[1], diagnostic));
        forged.reset();
        c.empty();
        b.maxQuerySlots = UINT32_MAX;
        {
            auto graph = factory();
            assert(graph && graph->slotCount() == 4u);
            assert(!factory() && std::string_view(diagnostic) == "RHI-QUERY-SLOTS-OWNER-BUSY");
        }
        b = c.budget();
        c.empty();
        // Partial cold allocation failure releases every control and leaves the
        // native owner eligible for a fresh exact graph reservation.
        {
            auto graph = factory();
            assert(graph);
            graph.reset();
            assert(factory());
        }
        c.retire();
        std::printf(
            "BINDLESS_RHI_QUERY_SLOTS_COLD_OK %llu %llu quotas2 exact-less-exhausted missing-mismatch count4 oversized-bound-exact-storage\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }

    static void verifyCurrent()
    {
        Context c, other;
        c.bindOwner();
        other.bindOwner();
        const char* diagnostic = nullptr;
        auto graph = c.prepare(c.budget(), diagnostic);
        auto otherGraph = other.prepare(other.budget(), diagnostic);
        assert(graph && otherGraph);
        auto initial = Slots::observeOriginal(graph, *c.binding, diagnostic),
             otherObservation = Slots::observeOriginal(otherGraph, *other.binding, diagnostic);
        assert(initial && otherObservation);
        assert(initial->index() == 3u && initial->phase() == Phase::NotRecorded
            && initial->revision() == 1u && !Slots::validateCurrentOriginal(graph, *initial));
        assert(std::string_view(Slots::executionDiagnostic(graph, *initial))
            == "RHI-QUERY-CREATION-UNINITIALIZED");
        assert(!Slots::observeOriginal(graph, *other.binding, diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-SLOTS-ORIGINAL-BINDING");
        assert(std::string_view(Slots::validateCurrentOriginal(graph, *otherObservation))
            == "RHI-QUERY-SLOTS-OBSERVATION-OWNER");
        std::array<std::optional<Slots::Observation>, 4> allSlots;
        for (std::uint32_t i = 0u; i < allSlots.size(); ++i) {
            auto command = rhi::QueryPoolOriginalInputArguments::captureOriginal(
                rhi::CommandUsageKind::Timestamp, i, c.authoring.input);
            auto binding = Binding::captureOriginal(command,
                c.owner,
                {1u, true, 1u, 4u, 2u, c.authoring.input->label().size()},
                diagnostic);
            assert(binding);
            auto observation = Slots::observeOriginal(graph, *binding, diagnostic);
            assert(observation);
            allSlots[i].emplace(*observation);
        }
        auto forged = std::shared_ptr<Slots>(c.quotas[0], graph.get());
        assert(!Slots::observeOriginal(forged, *c.binding, diagnostic)
            && Slots::resetOriginal(forged) && Slots::invalidateOriginal(forged));
        forged.reset();
        auto sameInputOwner = c.authoring.prepareDefault(diagnostic);
        assert(sameInputOwner);
        auto sameInputCommand = rhi::QueryPoolOriginalInputArguments::captureOriginal(
            rhi::CommandUsageKind::Timestamp, 3u, c.authoring.input);
        auto sameInputBinding = Binding::captureOriginal(sameInputCommand,
            sameInputOwner,
            {1u, true, 1u, 4u, 2u, c.authoring.input->label().size()},
            diagnostic);
        assert(sameInputBinding);
        assert(!Slots::observeOriginal(graph, *sameInputBinding, diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-SLOTS-ORIGINAL-BINDING");
        sameInputBinding.reset();
        sameInputCommand.reset();
        sameInputOwner.reset();
        Promises::holdScopes();
        assert(!Owner::initialize(c.owner));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(!Slots::validateCurrentOriginal(graph, *initial)
            && std::string_view(Slots::executionDiagnostic(graph, *initial))
                == "RHI-QUERY-CREATION-PENDING");
        Promises::releaseScopes();
        waitReady(c.owner);
        assert(!Slots::validateCurrentOriginal(graph, *initial)
            && Slots::executionDiagnostic(graph, *initial));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        {
            core::CpuMemoryQuotaLease a(
                c.quotas[0], static_cast<std::size_t>(cpuBytes - c.quotas[0]->liveBytes())),
                b(c.quotas[1], static_cast<std::size_t>(cpuBytes - c.quotas[1]->liveBytes()));
            assert(a.charged() && b.charged());
            assert(!Slots::resetOriginal(graph));
            for (const auto& observation : allSlots)
                assert(Slots::validateCurrentOriginal(graph, *observation));
            assert(std::string_view(Slots::validateCurrentOriginal(graph, *initial))
                == "RHI-QUERY-SLOTS-STALE-REVISION");
            auto fresh = Slots::observeOriginal(graph, *c.binding, diagnostic);
            assert(fresh && fresh->phase() == Phase::NotRecorded
                && fresh->revision() > initial->revision()
                && !Slots::validateCurrentOriginal(graph, *fresh));
            auto copy = *fresh;
            assert(!Slots::validateCurrentOriginal(graph, copy)
                && Slots::executionDiagnostic(graph, copy));
            assert(!graph->recordingSupported() && !graph->resultAvailable());
            assert(!Slots::resetOriginal(graph) && Slots::validateCurrentOriginal(graph, *fresh));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(old);
        assert(!Slots::invalidateOriginal(graph) && Slots::validateCurrentOriginal(graph, *initial)
            && Slots::resetOriginal(graph)
            && !Slots::observeOriginal(graph, *c.binding, diagnostic));
        assert(!c.prepare(c.budget(), diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-SLOTS-OWNER-BUSY");
        initial.reset();
        for (auto& observation : allSlots)
            observation.reset();
        graph.reset();
        c.empty();
        {
            auto replacement = c.prepare(c.budget(), diagnostic);
            assert(replacement && Slots::observeOriginal(replacement, *c.binding, diagnostic));
        }
        otherObservation.reset();
        otherGraph.reset();
        other.retire();
        c.retire();
        std::puts(
            "BINDLESS_RHI_QUERY_SLOTS_CURRENT_OK cold-pending-ready exact-owners reset-ABA terminal-invalidation exclusive-replacement allocation0");
    }

    static void verifyRejected()
    {
        Context c;
        c.bindOwner();
        const char* diagnostic = nullptr;
        auto graph = c.prepare(c.budget(), diagnostic);
        assert(graph);
        auto observation = Slots::observeOriginal(graph, *c.binding, diagnostic);
        assert(observation);
        EM_ASM({
            // clang-format off
                Module.arpQuerySlotsChanged = Module.preinitializedWebGPUDevice.createQuerySet;
                Module.preinitializedWebGPUDevice.createQuerySet = function(d) {
                    return Module.arpQuerySlotsChanged.call(this, Object.assign({}, d, ({count : 1})));
                };
            // clang-format on
        });
        assert(!Owner::initialize(c.owner));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createQuerySet = Module.arpQuerySlotsChanged;
                delete Module.arpQuerySlotsChanged;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return c.owner->creationScopesObserved();
        }));
        assert(c.owner->status() == Owner::Status::Rejected
            && Slots::validateCurrentOriginal(graph, *observation) && Slots::resetOriginal(graph)
            && !c.prepare(c.budget(), diagnostic));
        observation.reset();
        graph.reset();
        c.retire();
    }

    static void verifyDrop()
    {
        Context c;
        c.bindOwner();
        const char* diagnostic = nullptr;
        auto graph = c.prepare(c.budget(), diagnostic);
        assert(graph);
        assert(!Owner::initialize(c.owner));
        waitReady(c.owner);
        auto observation = Slots::observeOriginal(graph, *c.binding, diagnostic);
        assert(observation);
        std::weak_ptr<Slots> weak = graph;
        std::weak_ptr<Owner> native = c.owner;
        c.dropCallers();
        graph.reset();
        assert(!weak.expired() && !native.expired()
            && !Slots::validateCurrentOriginal(observation->originalGraph(), *observation));
        assert(Slots::executionDiagnostic(observation->originalGraph(), *observation));
        observation.reset();
        assert(weak.expired() && native.expired());
        weak.reset();
        native.reset();
        c.retire();
    }

    static void verifyPendingDrop()
    {
        Context c;
        c.bindOwner();
        const char* diagnostic = nullptr;
        auto graph = c.prepare(c.budget(), diagnostic);
        assert(graph);
        auto observation = Slots::observeOriginal(graph, *c.binding, diagnostic);
        assert(observation);
        Promises::holdScopes();
        assert(!Owner::initialize(c.owner));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        std::weak_ptr<Owner> native = c.owner;
        c.dropCallers();
        graph.reset();
        assert(!native.expired()
            && !Slots::validateCurrentOriginal(observation->originalGraph(), *observation));
        observation.reset();
        c.empty();
        assert(!native.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return native.expired();
        }));
        native.reset();
        c.retire();
    }

    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::shared_ptr<Slots>, 3> lossGraphs_;
    static inline std::array<std::optional<Slots::Observation>, 3> lossObservations_;
#endif
};
} // namespace arp::test
