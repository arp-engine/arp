#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/gpu_device.h"

namespace arp::test {
class OriginalTypedNativeLossRegistrationFixture final {
    using Contract = rhi::OriginalTypedNativeLossJoinContract;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && core::StringView(actual) == expected);
    }

    static rhi::OriginalResourceRegistrationBudget budget()
    {
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = 7u;
        b.prototypeOptIn = true;
        b.maxPipelineLayouts = b.maxComputePipelines = b.maxDescriptorArenas = b.maxDescriptorSets =
            1u;
        // Fixed3 + layout2 + arena2 + set3; compute admission is unused here.
        b.maxRetainedOwners = 10u;
        b.maxCpuMetadataBytes = bytes;
        return b;
    }

public:
    static void verifyContract()
    {
        const rhi::OriginalTypedNativeLossJoinRequest request{1u, true};
        const auto b = budget();
        assert(!Contract::validate(request, b));
        is(Contract::validate({0u, true}, b), "RHI-ORIGINAL-TYPED-LOSS-JOIN-VERSION");
        is(Contract::validate({1u, false}, b), "RHI-ORIGINAL-TYPED-LOSS-JOIN-OPT-IN");
        auto invalid = b;
        invalid.contractVersion = 6u;
        is(Contract::validate(request, invalid), "RHI-ORIGINAL-TYPED-LOSS-REGISTRATION-VERSION");
        invalid = b;
        invalid.maxComputePipelines = 0u;
        is(Contract::validate(request, invalid), "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-BUDGET");
        invalid = b;
        invalid.maxDescriptorTables = 1u;
        is(Contract::validate(request, invalid), "RHI-ORIGINAL-TYPED-LOSS-DESCRIPTOR-TABLE-CLOSED");
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d = Contract::lowering(backend);
            assert(d.registrationAvailable == (backend == rhi::Backend::WebGPU)
                && d.validatorOnly == (backend == rhi::Backend::Null));
            assert(bool(d.diagnostic) == !d.registrationAvailable);
            assert(d.acceptedPipelineJoinAvailable == (backend == rhi::Backend::WebGPU)
                && d.scopedGroupJoinAvailable == (backend == rhi::Backend::WebGPU)
                && !d.recordingAvailable && !d.executionAvailable);
            is(rhi::OriginalResourceRegistrationContract::lowering(backend, 7u).diagnostic,
                "RHI-ORIGINAL-TYPED-LOSS-EXPLICIT-REQUEST-REQUIRED");
        }
        assert(!Contract::lowering(static_cast<rhi::Backend>(UINT32_MAX)).registrationAvailable);
        std::puts(
            "BINDLESS_RHI_TYPED_LOSS_REGISTRATION_CONTRACT_OK request1 registry7 explicit-opt-in four-lowerings CPU-parents-only recording-closed");
    }

    template <class Factory> static void verifyGate(Factory&& create)
    {
        auto device = create();
        assert(device);
        auto metadata = quota();
        rhi::OriginalDeviceNativeLossObservation unissued;
        const auto b = budget();
        const auto d = Contract::lowering(device->backend());
        const std::array quotas{metadata};
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                is(device->prepareOriginalResourceRegistration(b, metadata),
                    "RHI-ORIGINAL-TYPED-LOSS-EXPLICIT-REQUEST-REQUIRED");
                is(device->prepareOriginalTypedNativeLossRegistration(
                       {1u, false}, b, metadata, unissued),
                    "RHI-ORIGINAL-TYPED-LOSS-JOIN-OPT-IN");
                is(device->prepareOriginalTypedNativeLossRegistration(
                       {1u, true}, b, metadata, unissued),
                    d.registrationAvailable ? "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED"
                                            : d.diagnostic);
                return nullptr;
            }));
        assert(!metadata->liveBytes());
    }

    template <class Factory> static void verifyNative(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        verifyContract();
        verifyGate(create);
        auto domain = quota(), subscription = quota(), metadata = quota(), objects = quota();
        const auto prepare = [&] {
            auto device = create();
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            assert(!device->prepareOriginalDeviceNativeLoss(
                {{1u, true, 1u, 1u, bytes}, 1u}, subscription));
            return device;
        };
        for (const auto version : std::array{1u, 2u, 3u, 4u, 5u, 6u}) {
            auto legacy = prepare();
            auto old = budget();
            old.contractVersion = version;
            if (version == 1u) {
                old.maxPipelineLayouts = old.maxComputePipelines = 0u;
                old.maxBuffers = 1u;
            }
            if (version < 3u)
                old.maxDescriptorArenas = old.maxDescriptorSets = 0u;
            assert(!legacy->prepareOriginalResourceRegistration(old, metadata));
            rhi::PipelineLayoutDesc ld;
            rhi::DescriptorArenaDesc ad;
            ad.maxSets = ad.maxDescriptors = 1u;
            assert(!legacy->createOriginalPipelineLayout(ld));
            is(legacy->lastOriginalResourceRegistrationDiagnostic().data(),
                version == 1u ? "RHI-ORIGINAL-REGISTRATION-TYPED-VERSION"
                              : "RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING");
            assert(!legacy->createOriginalDescriptorArena(ad));
            is(legacy->lastOriginalResourceRegistrationDiagnostic().data(),
                version < 3u ? "RHI-ORIGINAL-REGISTRATION-DESCRIPTOR-VERSION"
                             : "RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING");
            legacy.reset();
            assert(!domain->liveBytes() && !subscription->liveBytes() && !metadata->liveBytes());
        }
        auto device = prepare();
        const char* id = nullptr;
        auto loss = device->observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        auto b = budget();
        const auto registration = [&] {
            return device->prepareOriginalTypedNativeLossRegistration(
                       {1u, true}, b, metadata, *loss)
                == nullptr;
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto peak = pressure.measure(registration);
        // A registry is one-shot: recreate the facade/subscription for each
        // exact/one-less/exhausted cold allocation probe.
        loss.reset();
        device.reset();
        assert(!domain->liveBytes() && !subscription->liveBytes() && !metadata->liveBytes());
        const auto registryFactory = [&] {
            auto candidate = prepare();
            const char* diagnostic = nullptr;
            auto observed = candidate->observeOriginalDeviceNativeLoss(diagnostic);
            assert(observed);
            const auto* result = candidate->prepareOriginalTypedNativeLossRegistration(
                {1u, true}, b, metadata, *observed);
            if (result) {
                is(result, "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
                candidate.reset();
            }
            return candidate;
        };
        pressure.verify(peak, registryFactory);
        {
            core::CpuMemoryQuotaLease full(metadata, bytes);
            assert(full.charged() && !registryFactory());
        }
        device = prepare();
        auto observed = device->observeOriginalDeviceNativeLoss(id);
        assert(observed);
        loss.emplace(*observed);
        observed.reset();
        is(device->prepareOriginalTypedNativeLossRegistration({1u, true}, b, subscription, *loss),
            "RHI-ORIGINAL-TYPED-LOSS-QUOTA-DOMAIN");
        auto foreign = create();
        assert(foreign);
        auto foreignDomain = quota(), foreignSubscription = quota();
        assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, foreignDomain));
        assert(!foreign->prepareOriginalDeviceNativeLoss(
            {{1u, true, 1u, 1u, bytes}, 1u}, foreignSubscription));
        auto foreignLoss = foreign->observeOriginalDeviceNativeLoss(id);
        assert(foreignLoss);
        is(device->prepareOriginalTypedNativeLossRegistration(
               {1u, true}, b, metadata, *foreignLoss),
            "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        assert(registration());
        is(device->prepareOriginalTypedNativeLossRegistration({1u, true}, b, metadata, *loss),
            "RHI-ORIGINAL-REGISTRATION-ALREADY-PREPARED");
        is(device->prepareOriginalDeviceNativeLoss({{1u, true, 1u, 1u, bytes}, 1u}, subscription),
            "RHI-ORIGINAL-DEVICE-LOSS-ALREADY-PREPARED");
        rhi::PipelineLayoutDesc ld;
        std::shared_ptr<rhi::PipelineLayout> layout;
        rhi::DescriptorArenaDesc ad;
        ad.maxSets = ad.maxDescriptors = 1u;
        std::shared_ptr<rhi::DescriptorArena> arena;
        core::UniquePtr<rhi::DescriptorSet> set;
        {
            core::CpuMemoryQuotaScope scope(objects);
            layout = core::shareUnique(device->createOriginalPipelineLayout(ld));
            assert(layout);
            assert(!device->createOriginalPipelineLayout(ld));
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
            arena = core::shareUnique(device->createOriginalDescriptorArena(ad));
            assert(arena);
            rhi::DescriptorSetDesc sd;
            sd.layout.name = "OriginalTypedLossStorage";
            sd.layout.bindings.push_back(
                {0u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute, 1u});
            set = device->allocateOriginalDescriptorSet(arena, sd);
            assert(set);
        }
        {
            rhi::OriginalFiniteDescriptorBufferWrite finite;
            rhi::OriginalWholeDescriptorBufferWrite whole;
            const rhi::OriginalFiniteDescriptorWriteRequest f{1u,
                true,
                1u,
                static_cast<std::uint32_t>(
                    rhi::OriginalFiniteDescriptorWriteContract::ownerSlots(1u)),
                rhi::OriginalFiniteDescriptorWriteContract::callbackCount,
                {bytes, bytes, bytes}};
            const rhi::OriginalWholeDescriptorWriteRequest w{1u,
                true,
                1u,
                static_cast<std::uint32_t>(
                    rhi::OriginalWholeDescriptorWriteContract::ownerSlots(1u)),
                rhi::OriginalWholeDescriptorWriteContract::callbackCount,
                {bytes, bytes, bytes}};
            const std::array groupQuotas{quota(), quota(), quota()};
            is(device->beginOriginalFiniteDescriptorWrite(
                   *set, std::shared_ptr<rhi::ComputePipeline>{}, 0u, &finite, 1u, f, groupQuotas),
                "RHI-ORIGINAL-FINITE-WRITE-PIPELINE");
            is(device->beginOriginalWholeDescriptorWrite(
                   *set, std::shared_ptr<rhi::ComputePipeline>{}, 0u, &whole, 1u, w, groupQuotas),
                "RHI-ORIGINAL-FINITE-WRITE-PIPELINE");
            for (const auto& q : groupQuotas)
                assert(!q->liveBytes());
        }
        auto ticket = set->originalResourceRegistration();
        assert(!ticket.diagnostic() && ticket.matchesParent(arena->originalResourceRegistration()));
        const std::array quotas{domain, subscription, metadata, objects};
        const auto armedBytes = subscription->liveBytes();
        assert(armedBytes);
        loss.reset();
        assert(!ticket.diagnostic()); // Registry holds only weak subscription custody.
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    subscription->setLimitBytes(bytes - 1u);
                    is(ticket.diagnostic(), "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
                    is(device->originalResourceRegistrationDiagnostic(),
                        "RHI-DEVICE-LOSS-REGISTRATION-QUOTA");
                    subscription->setLimitBytes(bytes);
                    assert(!ticket.diagnostic());
                    is(ticket.independentCpuQuotaDiagnostic(subscription),
                        "RHI-ORIGINAL-REGISTRATION-QUOTA-DOMAIN");
                    device.reset();
                    is(ticket.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                    return nullptr;
                }));
        });
        // A weak receipt keeps the original shared control allocation (not the
        // subscription object/callback) alive until the last registry retires.
        const auto weakControlBytes = subscription->liveBytes();
        assert(weakControlBytes && weakControlBytes < armedBytes);
        set.reset();
        arena.reset();
        layout.reset();
        ticket = {};
        foreignLoss.reset();
        foreign.reset();
        assert(!foreignDomain->liveBytes() && !foreignSubscription->liveBytes());
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        b.maxRetainedOwners = 9u;
        device = prepare();
        auto ownerObserved = device->observeOriginalDeviceNativeLoss(id);
        assert(ownerObserved);
        assert(!device->prepareOriginalTypedNativeLossRegistration(
            {1u, true}, b, metadata, *ownerObserved));
        ownerObserved.reset();
        {
            core::CpuMemoryQuotaScope scope(objects);
            layout = core::shareUnique(device->createOriginalPipelineLayout(ld));
            assert(layout);
            arena = core::shareUnique(device->createOriginalDescriptorArena(ad));
            assert(arena);
            rhi::DescriptorSetDesc sd;
            sd.layout.name = "OriginalTypedLossStorage";
            sd.layout.bindings.push_back(
                {0u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute, 1u});
            assert(!device->allocateOriginalDescriptorSet(arena, sd));
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
            assert(!arena->stats()
                    .allocatedSets); // Reservation fails before the arena consumes capacity.
        }
        arena.reset();
        layout.reset();
        device.reset();
        for (const auto& q : quotas)
            assert(!q->liveBytes());
        std::printf(
            "BINDLESS_RHI_TYPED_LOSS_REGISTRATION_NATIVE_OK registry7 weak-subscription CPU-layout-arena-set parent-epoch quota-mutation foreign-refused caller-drop shutdown exact-less-exhausted peak%llu owners10-less9 weak-control%llu hot0 query0 recording-closed\n",
            static_cast<unsigned long long>(peak),
            static_cast<unsigned long long>(weakControlBytes));
#else
        (void)create;
#endif
    }
};
} // namespace arp::test
