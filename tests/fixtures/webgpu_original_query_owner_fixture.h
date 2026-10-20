#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_query_owner.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_original_query_source_gate_fixture.h"

#include <cassert>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class WebGpuOriginalQueryOwnerFixture final {
    using Owner = rhi_webgpu::WebGpuOriginalQueryOwner;
    using Budget = rhi_webgpu::WebGpuOriginalQueryOwnerBudget;
    using Lowering = Owner::Lowering;
    using Input = rhi::QueryPoolInputSnapshot;
    using Promises = WebGpuErrorScopeHandoffFixture<27u>;
    static constexpr std::uint64_t cpuBytes = 65536u;
    static constexpr std::string_view label =
        "original query \xE6\x9F\xA5\xE8\xAF\xA2 exact immutable cold label retained";

    static Budget budget()
    {
        return {1u, true, 1u, 1u, 4u, Owner::callbackCount, 2u, label.size(), cpuBytes, cpuBytes};
    }

public:
    class Context final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> inputQuotas;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2> quotas;
        std::shared_ptr<const Input> input;

        explicit Context(rhi::QueryType type = rhi::QueryType::Timestamp,
            std::uint32_t count = 4u,
            std::string_view name = label)
        {
            for (auto& q : inputQuotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
            const std::array<std::uint64_t, 2> scopes{UINT64_MAX, UINT64_MAX};
            rhi::QueryPoolDesc d;
            d.type = type;
            d.count = count;
            d.label = name;
            if (type == rhi::QueryType::Timestamp && count == 4u) {
                d.timingScopeIds = scopes.data();
                d.timingScopeIdCount = scopes.size();
            }
            if (type == rhi::QueryType::PipelineStatistics)
                d.pipelineStatisticsMask = rhi::kAllPipelineStatistics;
            const char* diagnostic = nullptr;
            input = Input::prepareOriginal(d,
                {1u, true, 2u, name.size(), cpuBytes, cpuBytes, cpuBytes},
                inputQuotas[0],
                inputQuotas[1],
                inputQuotas[2],
                diagnostic);
            assert(input);
        }

        auto prepare(const Budget& b, const char*& diagnostic)
        {
            return Owner::prepareOriginal(input, b, quotas[0], quotas[1], diagnostic);
        }

        auto prepareDefault(const char*& diagnostic) { return prepare(budget(), diagnostic); }

        void empty() const
        {
            for (const auto& q : quotas)
                if (q)
                    assert(!q->liveBytes());
        }

        void retire()
        {
            input.reset();
            empty();
            for (const auto& q : inputQuotas)
                assert(!q->liveBytes());
        }
    };

public:
    static void verifyContract()
    {
        using namespace rhi;
        using Facts = rhi::semantics::OriginalQueryCreationFacts;
        static_assert(!std::is_default_constructible_v<Lowering::Decision>);
        static_assert(!std::is_copy_constructible_v<Owner>);
        Context c, other, statistics(QueryType::PipelineStatistics),
            maximum(QueryType::Timestamp, GpuTimingSample::kMaxScopes * 2u),
            over(QueryType::Timestamp, GpuTimingSample::kMaxScopes * 2u + 1u);
        const Facts complete{true, true, true, true, true, true, true};
        assert(Owner::validateBudget({}));
        auto b = budget();
        assert(!Owner::validateBudget(b));
        b.contractVersion = 0u;
        assert(Owner::validateBudget(b));
        b = budget();
        b.prototypeOptIn = false;
        assert(Owner::validateBudget(b));
        b = budget();
        --b.maxCallbacks;
        assert(Owner::validateBudget(b));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision = Lowering::decide(backend, 1u, true, c.input, complete);
            assert(decision.creationEligible() == (backend == Backend::WebGPU)
                && !decision.executionSupported());
            assert(decision.validatorOnly() == (backend == Backend::Null));
            assert(decision.matchesOriginal(c.input) && !decision.matchesOriginal(other.input));
        }
        const std::array<bool Facts::*, 7> fields{&Facts::timestampFeature,
            &Facts::create,
            &Facts::pushScope,
            &Facts::popScope,
            &Facts::countAccess,
            &Facts::typeAccess,
            &Facts::destroy};
        for (const auto field : fields) {
            auto facts = complete;
            facts.*field = false;
            const auto d = Lowering::decide(Backend::WebGPU, 1u, true, c.input, facts);
            assert(!d.creationEligible() && d.mode() == CapabilityMode::Degraded && d.diagnostic());
        }
        assert(!Lowering::decide(Backend::WebGPU, 0u, true, c.input, complete).creationEligible());
        assert(!Lowering::decide(Backend::WebGPU, 1u, false, c.input, complete).creationEligible());
        assert(!Lowering::decide(Backend::WebGPU, 1u, true, {}, complete).creationEligible());
        assert(!Lowering::decide(static_cast<Backend>(99u), 1u, true, c.input, complete)
                .creationEligible());
        assert(Lowering::decide(Backend::WebGPU, 1u, true, maximum.input, complete)
                .creationEligible());
        assert(std::string_view(
                   Lowering::decide(Backend::WebGPU, 1u, true, over.input, complete).diagnostic())
            == "RHI-TIMESTAMP-QUERY-DESCRIPTOR-INVALID");
        assert(Lowering::decide(Backend::WebGPU, 1u, true, statistics.input, complete).mode()
            == CapabilityMode::Degraded);
        auto forged = std::shared_ptr<const Input>(c.inputQuotas[0], c.input.get());
        assert(!Lowering::decide(Backend::WebGPU, 1u, true, forged, complete).creationEligible());
        forged.reset();
        assert(std::string_view(Owner::initialize({})) == "RHI-QUERY-CREATION-OWNER");
        assert(Owner::validateReadyOriginal({}));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(old);
        c.retire();
        other.retire();
        statistics.retire();
        maximum.retire();
        over.retire();
        std::puts(
            "BINDLESS_RHI_QUERY_CREATION_CONTRACT_OK backends4 facts7 l2-count-bound creation-only source-closed allocation0");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        // A negative native environment must remain explicit; never fake readiness.
        Context c;
        {
            const auto decision =
                Lowering::decide(rhi::Backend::WebGPU, 1u, true, c.input, Owner::nativeFacts());
            assert(decision.creationEligible());
        }
        c.retire();
        verifyBounds();
        verifyReady();
        verifyFailures();
        verifyDrop();
        std::puts(
            "BINDLESS_RHI_QUERY_CREATION_NATIVE_OK scopes3 real-queryset exact-input count-type-device source-closed allocation0");
    }

    static void prepareActualLoss()
    {
        EM_ASM({
            // clang-format off
                Module.arpQueryLossObjects = [];
                Module.arpQueryLossDestroyed = 0;
                Module.arpQueryLossOriginal = Module.preinitializedWebGPUDevice.createQuerySet;
                Module.preinitializedWebGPUDevice.createQuerySet = function(d) {
                    const q = Module.arpQueryLossOriginal.call(this, d);
                    Module.arpQueryLossObjects.push(q);
                    const destroy = q.destroy;
                    q.destroy = function() {
                        Module.arpQueryLossDestroyed++;
                        destroy.call(this);
                    };
                    return q;
                };
            // clang-format on
        });
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>();
            const char* diagnostic = nullptr;
            lossOwners_[i] = lossContexts_[i]->prepare(budget(), diagnostic);
            assert(lossOwners_[i]);
        }
        assert(!Owner::initialize(lossOwners_[0]));
        waitReady(lossOwners_[0]);
        Promises::holdScopes();
        assert(!Owner::initialize(lossOwners_[1]));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(lossOwners_[1]->status() == Owner::Status::Pending
            && lossOwners_[2]->status() == Owner::Status::Prepared);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createQuerySet = Module.arpQueryLossOriginal;
                delete Module.arpQueryLossOriginal;
            // clang-format on
        });
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        for (const auto& object : lossOwners_) {
            assert(object->status() == Owner::Status::DeviceLost && !object->resultAvailable());
            assert(std::string_view(object->validate()) == "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST");
            assert(Owner::initialize(object));
        }
        std::weak_ptr<Owner> pending = lossOwners_[1];
        for (auto& object : lossOwners_)
            object.reset();
        assert(!pending.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.expired();
        }));
        pending.reset();
        // Pinned emdawn contract: release removes the original wrapper table entry.
        assert(EM_ASM_INT({
            // clang-format off
                const wrappers = Object.values(WebGPU.Internals.jsObjects);
                return Module.arpQueryLossDestroyed === 2 && Module.arpQueryLossObjects.length === 2 &&
                       !Module.arpQueryLossObjects.some(q => wrappers.includes(q));
            // clang-format on
        }) == 1);
        EM_ASM({
            // clang-format off
                delete Module.arpQueryLossObjects;
            // clang-format on
        });
        for (auto& c : lossContexts_) {
            const char* diagnostic = nullptr;
            assert(!c->prepare(budget(), diagnostic)
                && std::string_view(diagnostic) == "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST");
            c->retire();
            c.reset();
        }
        std::puts(
            "BINDLESS_RHI_QUERY_CREATION_LOSS_OK prepared-pending-ready reject late-callbacks exact-cpu-retirement");
    }

private:
    static void waitReady(const std::shared_ptr<Owner>& owner)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner->status() != Owner::Status::Pending;
        }));
        assert(owner->status() == Owner::Status::CreationReady
            && !Owner::validateReadyOriginal(owner));
    }

    static void verifyBounds()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto b = budget();
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
        for (auto field : {&Budget::maxQuerySets,
                 &Budget::maxDeviceOwners,
                 &Budget::maxQuerySlots,
                 &Budget::maxCallbacks}) {
            const auto original = b.*field;
            b.*field = 0u;
            reject("RHI-QUERY-CREATION-BUDGET");
            b.*field = original;
        }
        --b.maxQuerySlots;
        reject("RHI-QUERY-CREATION-CAPACITY");
        ++b.maxQuerySlots;
        --b.maxRetainedScopeIds;
        reject("RHI-QUERY-CREATION-CAPACITY");
        ++b.maxRetainedScopeIds;
        --b.maxRetainedLabelBytes;
        reject("RHI-QUERY-CREATION-CAPACITY");
        ++b.maxRetainedLabelBytes;
        auto original = c.input;
        c.input = std::shared_ptr<const Input>(c.inputQuotas[0], original.get());
        reject("RHI-QUERY-CREATION-INPUT");
        c.input = std::move(original);
        for (std::size_t i = 0u; i < c.quotas.size(); ++i) {
            auto q = c.quotas[i];
            c.quotas[i].reset();
            reject("RHI-QUERY-CREATION-QUOTA");
            c.quotas[i] = core::makeShared<core::CpuMemoryQuota>(cpuBytes + 1u);
            reject("RHI-QUERY-CREATION-QUOTA");
            c.quotas[i] = q;
        }
        {
            auto object = factory();
            assert(object && object->status() == Owner::Status::Prepared
                && !object->creationScopesObserved()
                && object->queryState() == rhi::QueryExecutionState::NotRecorded
                && !object->resultAvailable());
        }
        {
            Context binary(rhi::QueryType::Timestamp, 4u, std::string_view("a\0b", 3u));
            assert(!binary.prepare(budget(), diagnostic)
                && std::string_view(diagnostic) == "RHI-QUERY-CREATION-LABEL");
            binary.retire();
        }
        c.retire();
        std::printf(
            "BINDLESS_RHI_QUERY_CREATION_COLD_OK %llu %llu quotas2 exact-less-exhausted object1 device1 slots4 callbacks3\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]));
    }

    static void verifyReady()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto object = c.prepare(budget(), diagnostic);
        assert(object);
        auto forged = std::shared_ptr<Owner>(c.quotas[0], object.get());
        assert(Owner::initialize(forged) && Owner::validateReadyOriginal(forged));
        forged.reset();
        EM_ASM({
            // clang-format off
                Module.arpQueryCreationCount = 0;
                Module.arpQueryCreationDestroyed = 0;
                Module.arpQueryCreationOriginal = Module.preinitializedWebGPUDevice.createQuerySet;
                Module.preinitializedWebGPUDevice.createQuerySet = function(d) {
                    Module.arpQueryCreationCount++;
                    Module.arpQueryCreationDesc = d;
                    const q = Module.arpQueryCreationOriginal.call(this, d);
                    const destroy = q.destroy;
                    q.destroy = function() {
                        Module.arpQueryCreationDestroyed++;
                        destroy.call(this);
                    };
                    return q;
                };
            // clang-format on
        });
        const auto startAllocations = core::defaultCpuAllocator().stats().allocationCount;
        Promises::holdScopes();
        {
            core::CpuMemoryQuotaLease a(
                c.quotas[0], static_cast<std::size_t>(cpuBytes - c.quotas[0]->liveBytes())),
                b(c.quotas[1], static_cast<std::size_t>(cpuBytes - c.quotas[1]->liveBytes()));
            assert(a.charged() && b.charged() && !Owner::initialize(object));
        }
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(object->status() == Owner::Status::Pending && !object->creationScopesObserved()
            && Owner::validateReadyOriginal(object));
        Promises::releaseScope(2u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->creationScopeObserved(2u);
        }));
        assert(object->status() == Owner::Status::Pending && !object->creationScopesObserved());
        Promises::releaseScope(0u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object->creationScopeObserved(0u);
        }));
        assert(object->status() == Owner::Status::Pending && !object->creationScopesObserved());
        Promises::releaseScopes();
        waitReady(object);
        assert(core::defaultCpuAllocator().stats().allocationCount == startAllocations);
        assert(EM_ASM_INT({
            // clang-format off
                const d = Module.arpQueryCreationDesc;
                return Module.arpQueryCreationCount === 1 && Module.arpQueryCreationDestroyed === 0 &&
                       d.type === 'timestamp' && d.count === 4;
            // clang-format on
        }) == 1);
        assert(Owner::initialize(object)); // repeated initialization is rejected
        auto input = c.input;
        assert(object->originalInput() == input
            && object->originalInput()->scopeIds()[0] == UINT64_MAX
            && object->originalInput()->scopeIds()[1] == UINT64_MAX
            && object->originalInput()->label() == label);
        EM_ASM({
            // clang-format off
                Module.arpQueryCreationOldDevice = Module.preinitializedWebGPUDevice;
                Module.preinitializedWebGPUDevice = ({});
            // clang-format on
        });
        assert(object->validate());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice = Module.arpQueryCreationOldDevice;
                delete Module.arpQueryCreationOldDevice;
            // clang-format on
        });
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        {
            core::CpuMemoryQuotaLease a(
                c.quotas[0], static_cast<std::size_t>(cpuBytes - c.quotas[0]->liveBytes())),
                b(c.quotas[1], static_cast<std::size_t>(cpuBytes - c.quotas[1]->liveBytes()));
            assert(a.charged() && b.charged());
            assert(!object->validate() && !object->resultAvailable()
                && object->queryState() == rhi::QueryExecutionState::NotRecorded
                && object->executionDiagnostic());
            const auto command = rhi::QueryPoolOriginalInputArguments::captureOriginal(
                rhi::CommandUsageKind::Timestamp, 0u, input);
            const auto source =
                rhi_webgpu::WebGpuOriginalQuerySourceGate::decideOriginal(1u, true, command);
            assert(!source.nativeSourceSupported());
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(old);
        c.input.reset();
        input.reset();
        assert(!object->validate());
        object.reset();
        c.retire();
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpQueryCreationDestroyed === 1 && Module.arpQueryCreationCount === 1;
            // clang-format on
        }) == 1);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createQuerySet = Module.arpQueryCreationOriginal;
                delete Module.arpQueryCreationOriginal;
            // clang-format on
        });
    }

    static void verifyFailures()
    {
        // Each case still creates a real query set. Actual validation failure is
        // captured by scopes; legal but changed count/type fails identity binding.
        for (std::uint32_t kind = 0u; kind < 3u; ++kind) {
            Context c;
            const char* diagnostic = nullptr;
            auto object = c.prepare(budget(), diagnostic);
            assert(object);
            EM_ASM(
                {
                    // clang-format off
                        Module.arpQueryBadOriginal = Module.preinitializedWebGPUDevice.createQuerySet;
                        Module.preinitializedWebGPUDevice.createQuerySet = function(d) {
                            const changed = Object.assign({}, d);
                            if ($0 === 2)
                                changed.type = 'occlusion';
                            else
                                changed.count = $0 === 0 ? $1 : $0;
                            return Module.arpQueryBadOriginal.call(this, changed);
                        };
                    // clang-format on
                },
                kind,
                Lowering::maximumWebGpuQueryCount + 1u);
            assert(!Owner::initialize(object));
            EM_ASM({
                // clang-format off
                    Module.preinitializedWebGPUDevice.createQuerySet = Module.arpQueryBadOriginal;
                    delete Module.arpQueryBadOriginal;
                // clang-format on
            });
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object->creationScopesObserved();
            }));
            assert(object->status() == Owner::Status::Rejected && !object->resultAvailable());
            std::printf(
                "BINDLESS_RHI_QUERY_CREATION_FAILURE_OBSERVED %u %s\n", kind, object->validate());
            assert(std::string_view(object->validate())
                == (kind == 0u ? "RHI-QUERY-CREATION-NATIVE-SCOPE"
                               : "RHI-QUERY-CREATION-NATIVE-LAYOUT"));
            assert(Owner::initialize(object));
            object.reset();
            c.retire();
        }
        // Live API admission is rechecked after cold preparation, before any call.
        Context c;
        const char* diagnostic = nullptr;
        auto object = c.prepare(budget(), diagnostic);
        assert(object);
        EM_ASM({
            // clang-format off
                Module.arpQueryMissingOriginal = Module.preinitializedWebGPUDevice.createQuerySet;
                Module.preinitializedWebGPUDevice.createQuerySet = undefined;
            // clang-format on
        });
        assert(std::string_view(Owner::initialize(object)) == "RHI-WEBGPU-QUERY-CREATION-API"
            && object->status() == Owner::Status::Prepared);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createQuerySet = Module.arpQueryMissingOriginal;
                delete Module.arpQueryMissingOriginal;
            // clang-format on
        });
        object.reset();
        c.retire();
        std::puts(
            "BINDLESS_RHI_QUERY_CREATION_FAILURES_OK real-scope-error altered-count-type changed-api forged-owner repeated-start");
    }

    static void verifyDrop()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto object = c.prepare(budget(), diagnostic);
        assert(object);
        Promises::holdScopes();
        assert(!Owner::initialize(object));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        std::weak_ptr<Owner> weak = object;
        object.reset();
        c.input.reset();
        assert(!weak.expired() && c.quotas[0]->liveBytes() && c.quotas[1]->liveBytes());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        c.retire();
        std::puts(
            "BINDLESS_RHI_QUERY_CREATION_DROP_OK original-input pending-callbacks-retain exact-retirement");
    }

    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::shared_ptr<Owner>, 3> lossOwners_;
#endif
};
} // namespace arp::test
