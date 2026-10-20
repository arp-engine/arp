#pragma once
#include "cpu_quota_factory_fixture.h"
#include "descriptor_buffer_binding_range_fixture.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalDescriptorFactoryContentFixture final {
    using Kind = rhi::OriginalRegisteredResourceKind;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "descriptor factory/content expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

    static rhi::OriginalResourceRegistrationBudget policy()
    {
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = 3u;
        b.prototypeOptIn = true;
        b.maxDescriptorArenas = 1u;
        b.maxDescriptorSets = 1u;
        b.maxDescriptorTables = 1u;
        b.maxRetainedOwners = 11u;
        b.maxCpuMetadataBytes = bytes;
        return b;
    }

public:
    template <class Factory> static void verify(Factory&& create)
    {
        static_assert(!std::is_default_constructible_v<rhi::DescriptorSetWriteObservation>);
        static_assert(!std::is_constructible_v<rhi::DescriptorSetWriteObservation,
            const rhi::DescriptorSet&,
            const rhi::DescriptorBindingWrite&>);
        static_assert(std::is_copy_constructible_v<rhi::DescriptorSetWriteObservation>
            && !std::is_copy_assignable_v<rhi::DescriptorSetWriteObservation>);
        core::LifetimeEpoch firstEpoch, foreignEpoch;
        const auto prior = firstEpoch.ticket();
        assert(prior.sameCurrentEpoch(firstEpoch.ticket())
            && !prior.sameCurrentEpoch(foreignEpoch.ticket()));
        firstEpoch.advance();
        assert(!prior.sameCurrentEpoch(firstEpoch.ticket()) && !prior.sameCurrentEpoch(prior));
        assert(!core::LifetimeEpoch::Ticket{}.sameCurrentEpoch(foreignEpoch.ticket()));
        auto b = policy();
        assert(!rhi::OriginalResourceRegistrationContract::validateIntent(b));
        for (const auto version : {1u, 2u}) {
            b.contractVersion = version;
            is(rhi::OriginalResourceRegistrationContract::validateIntent(b),
                "RHI-ORIGINAL-REGISTRATION-DESCRIPTOR-VERSION");
        }
        b = policy();
        b.maxDescriptorArenas = 0u;
        is(rhi::OriginalResourceRegistrationContract::validateIntent(b),
            "RHI-ORIGINAL-REGISTRATION-PARENT-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = rhi::OriginalResourceRegistrationContract::lowering(backend, 3u);
            assert(d.descriptorLifetimeIssuerAvailable && d.pipelineLifetimeIssuerAvailable
                && !d.nativeLossObserved && !d.executionAvailable);
            assert(!rhi::OriginalResourceRegistrationContract::lowering(backend, 2u)
                    .descriptorLifetimeIssuerAvailable);
        }
        auto domain = quota(), metadata = quota(), objects = quota();
        auto original = create();
        assert(original);
        assert(!original->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        assert(!original->prepareOriginalResourceRegistration(policy(), metadata));
        rhi::DescriptorArenaDesc arenaDesc;
        arenaDesc.label = "OriginalDescriptorArena";
        arenaDesc.maxSets = 2u;
        arenaDesc.maxDescriptors = 4u;
        const auto arenaFactory = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto result = original->createOriginalDescriptorArena(arenaDesc);
            std::pmr::set_default_resource(previous);
            return result;
        };
        CpuQuotaFactoryFixture pressure(metadata);
        const auto arenaPeak = pressure.measure(arenaFactory);
        auto unique = arenaFactory();
        assert(unique);
        auto arena = core::shareUnique(std::move(unique));
        assert(arena->sharedOwnerMatches(arena));
        rhi::DescriptorSetDesc setDesc;
        setDesc.label = "OriginalDescriptorSet";
        setDesc.layout.name = "OriginalDescriptorBindings";
        setDesc.layout.bindings.push_back(
            {7u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Compute, 2u});
        rhi::DescriptorTableDesc tableDesc;
        tableDesc.label = "OriginalDescriptorTable";
        tableDesc.type = rhi::DescriptorType::UniformBuffer;
        tableDesc.stages = rhi::ShaderStage::Compute;
        tableDesc.capacity = 2u;
        const auto setFactory = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto result = original->allocateOriginalDescriptorSet(arena, setDesc);
            std::pmr::set_default_resource(previous);
            return result;
        };
        const auto tableFactory = [&] {
            core::CpuMemoryQuotaScope scope(objects);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto result = original->allocateOriginalDescriptorTable(arena, tableDesc);
            std::pmr::set_default_resource(previous);
            return result;
        };
        const auto setMeasure = [&] {
            arena->reset();
            return setFactory();
        };
        const auto tableMeasure = [&] {
            arena->reset();
            return tableFactory();
        };
        const auto setPeak = pressure.measure(setMeasure),
                   tablePeak = pressure.measure(tableMeasure);
        pressure.verify(setPeak, setMeasure);
        pressure.verify(tablePeak, tableMeasure);
        CpuQuotaFactoryFixture wrapperPressure(objects);
        const auto setObjectPeak = wrapperPressure.measure(setMeasure),
                   tableObjectPeak = wrapperPressure.measure(tableMeasure);
        wrapperPressure.verify(setObjectPeak, setMeasure);
        wrapperPressure.verify(tableObjectPeak, tableMeasure);
        assert(arena->stats().allocatedSets == 0u && arena->stats().allocatedTables == 0u
            && arena->stats().allocatedDescriptors == 0u);
        arena->reset();
        const auto baseline = metadata->liveBytes();
        const auto allocations = arena->stats();
        {
            core::CpuMemoryQuotaLease full(metadata, bytes - baseline);
            assert(full.charged());
            assert(!setFactory());
            is(original->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-ALLOCATION");
            assert(arena->stats().allocatedSets == allocations.allocatedSets
                && arena->stats().allocatedDescriptors == allocations.allocatedDescriptors);
        }
        auto forged = std::shared_ptr<rhi::DescriptorArena>(objects, arena.get());
        assert(!original->allocateOriginalDescriptorSet(forged, setDesc));
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-DESCRIPTOR-ARENA-OWNER");
        auto foreign = create();
        assert(foreign && !foreign->allocateOriginalDescriptorSet(arena, setDesc));
        is(foreign->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-UNPREPARED");
        auto foreignDomain = quota(), foreignMetadata = quota();
        assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, foreignDomain));
        assert(!foreign->prepareOriginalResourceRegistration(policy(), foreignMetadata));
        assert(!foreign->allocateOriginalDescriptorSet(arena, setDesc));
        is(foreign->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        auto set = setFactory();
        auto table = tableFactory();
        assert(set && table);
        assert(!original->validateOriginalResourceRegistration(*set)
            && !original->validateOriginalResourceRegistration(*table));
        assert(
            set->originalResourceRegistration().matchesObjectIdentity(set.get(), Kind::Descriptor));
        assert(set->originalResourceRegistration().matchesParent(
            arena->originalResourceRegistration()));
        assert(table->originalResourceRegistration().matchesParent(
            arena->originalResourceRegistration()));
        assert(set->allocationEpochTicket().sameCurrentEpoch(arena->allocationEpochTicket()));
        assert(!setFactory());
        is(original->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-KIND-CAPACITY");
        // Content observations are facts only; no ordinary observation is
        // adopted into the original registry or used as Source authority.
        auto contentDevice = create();
        assert(contentDevice);
        auto contentQuota = verifyContents(*contentDevice);
        contentDevice.reset();
        assert(!contentQuota->liveBytes());
        auto parent = arena->originalResourceRegistration();
        auto receipt = set->originalResourceRegistration();
        auto* actualArena = arena.get();
        arena.reset();
        forged.reset();
        assert(!parent.diagnostic() && !receipt.diagnostic());
        actualArena->reset();
        is(receipt.diagnostic(), "RHI-ORIGINAL-REGISTRATION-ALLOCATION-STALE");
        table.reset();
        set.reset();
        is(parent.diagnostic(), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        receipt = {};
        parent = {};
        pressure.verify(arenaPeak, arenaFactory);
        const auto backend = original->backend();
        original.reset();
        foreign.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !objects->liveBytes()
            && !foreignDomain->liveBytes() && !foreignMetadata->liveBytes());
        verifyOwnerAndVersion(create, arenaDesc, setDesc);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_FACTORY_OK backend%u arena%llu set%llu table%llu version3 exact-facade-kind-parent core-shared-owner actual-arena-epoch reset retained-owner cold-exact-less-exhausted execution-closed\n",
            static_cast<unsigned>(backend),
            static_cast<unsigned long long>(arenaPeak),
            static_cast<unsigned long long>(setPeak),
            static_cast<unsigned long long>(tablePeak));
    }

    static std::shared_ptr<core::CpuMemoryQuota> verifyContents(rhi::GpuDevice& device)
    {
        DescriptorBufferBindingRangeFixture::verifyWriteGate(device);
        auto objects = quota();
        core::CpuMemoryQuotaScope scope(objects);
        rhi::DescriptorArenaDesc desc;
        desc.maxSets = 2u;
        desc.maxDescriptors = 4u;
        auto arena = device.createDescriptorArena(desc);
        assert(arena);
        rhi::DescriptorLayoutDesc layout;
        layout.name = "ExactDescriptorContent";
        layout.bindings.push_back(
            {7u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Compute, 2u});
        auto set = arena->allocateDescriptorSet({"ExactSet", layout});
        rhi::DescriptorTableDesc td;
        td.label = "ExactTable";
        td.type = rhi::DescriptorType::UniformBuffer;
        td.stages = rhi::ShaderStage::Compute;
        td.capacity = 2u;
        auto table = arena->allocateDescriptorTable(td);
        assert(set && table);
        is(device.validateOriginalResourceRegistration(*set), "RHI-ORIGINAL-REGISTRATION-EXPIRED");
        rhi::BufferDesc bd;
        bd.sizeBytes = 64u;
        bd.usage = rhi::ResourceUsage::UniformBuffer;
        auto buffer = device.createBuffer(bd);
        assert(buffer);
        const char* diagnostic = nullptr;
        assert(!set->observeSuccessfulBindingWrite(7u, 1u, diagnostic));
        is(diagnostic, "RHI-DESCRIPTOR-CONTENT-NOT-WRITTEN");
        rhi::DescriptorBindingWrite write{7u,
            rhi::DescriptorType::UniformBuffer,
            buffer->id(),
            rhi::DescriptorResourceKind::Buffer,
            rhi::ResourceState::ShaderRead};
        write.arrayElement = 1u;
        write.physicalLowering.physicalType = rhi::DescriptorType::ReadOnlyStorageBuffer;
        write.physicalLowering.logicalAttachmentIndex = 9u;
        write.physicalLowering.shaderPackageHash = 23u;
        assert(set->write(write).success);
        auto observation = set->observeSuccessfulBindingWrite(7u, 1u, diagnostic);
        assert(observation && !diagnostic && !observation->diagnostic()
            && observation->matchesObject(*set));
        assert(rhi::descriptorBindingWriteMatches(write, observation->write())
            && !observation->nativeExecutionProven());
        auto invalid = write;
        invalid.arrayElement = 2u;
        assert(!set->write(invalid).success && !observation->diagnostic());
        assert(set->write(write).success);
        is(observation->diagnostic(), "RHI-DESCRIPTOR-CONTENT-CHANGED");
        const auto refreshed = set->observeSuccessfulBindingWrite(7u, 1u, diagnostic);
        assert(refreshed);
        observation.emplace(*refreshed);
        rhi::DescriptorTableWrite tw{
            1u, td.type, buffer->id(), rhi::DescriptorResourceKind::Buffer};
        assert(table->write(tw).success);
        auto to = table->observeSuccessfulDescriptorWrite(1u, diagnostic);
        assert(to && !diagnostic);
        assert(to->write().index == tw.index && to->write().type == tw.type
            && to->write().resource == tw.resource && to->write().resourceKind == tw.resourceKind);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{objects}, [&]() noexcept {
            auto hot = set->observeSuccessfulBindingWrite(7u, 1u, diagnostic);
            if (!hot)
                return diagnostic;
            if (const auto* id = hot->diagnostic())
                return id;
            auto tableHot = table->observeSuccessfulDescriptorWrite(1u, diagnostic);
            return tableHot ? tableHot->diagnostic() : diagnostic;
        }));
        buffer.reset();
        assert(
            !set->validateBindingElement(7u, 1u).success && !table->validateDescriptor(1u).success);
        assert(
            !observation->diagnostic() && !to->diagnostic()); // content is not resource liveness.
        arena->reset();
        is(observation->diagnostic(), "RHI-DESCRIPTOR-CONTENT-ALLOCATION-STALE");
        is(to->diagnostic(), "RHI-DESCRIPTOR-CONTENT-ALLOCATION-STALE");
        set.reset();
        table.reset();
        is(observation->diagnostic(), "RHI-DESCRIPTOR-CONTENT-EXPIRED");
        observation.reset();
        to.reset();
        arena.reset();
        buffer.reset();
        // The device registry may retain its bucket capacity until facade
        // destruction. Return its genuine quota to the caller for that check.
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_CONTENT_OK exact-successful-array-table-fields weak-wrapper revision identical-write failed-write reset destruction hot-allocation0 content-not-factory-or-native-proof");
        return objects;
    }

    template <class Factory> static void verifyNativeLossGate(Factory&& create)
    {
        auto domain = quota(), metadata = quota(), subscription = quota();
        auto device = create();
        assert(device);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        assert(!device->prepareOriginalDeviceNativeLoss(
            {{1u, true, 1u, 1u, bytes}, 1u}, subscription));
        assert(!device->prepareOriginalResourceRegistration(policy(), metadata));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{domain, metadata}, [&]() noexcept {
                assert(!device->createOriginalDescriptorArena({}));
                is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING");
                assert(!device->allocateOriginalDescriptorSet({}, {}));
                is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING");
                assert(!device->allocateOriginalDescriptorTable({}, {}));
                is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                    "RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING");
                return static_cast<const char*>(nullptr);
            }));
        device.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes() && !subscription->liveBytes());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_LOSS_GATE_OK genuine-subscription arena-set-table rejected-before-factory hot-allocation0 execution-closed");
    }

private:
    template <class Factory>
    static void verifyOwnerAndVersion(Factory&& create,
        const rhi::DescriptorArenaDesc& arenaDesc,
        const rhi::DescriptorSetDesc& setDesc)
    {
        auto domain = quota(), metadata = quota();
        auto device = create();
        assert(device);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
        auto b = policy();
        b.maxRetainedOwners = 7u; // fixed3 + arena2 leaves a child one owner short.
        assert(!device->prepareOriginalResourceRegistration(b, metadata));
        auto arena = core::shareUnique(device->createOriginalDescriptorArena(arenaDesc));
        assert(arena);
        assert(!device->allocateOriginalDescriptorSet(arena, setDesc));
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-REGISTRATION-OWNER-CAPACITY");
        assert(!arena->stats().allocatedSets && !arena->stats().allocatedDescriptors);
        device->retireOriginalDeviceLifetime();
        assert(!device->allocateOriginalDescriptorSet(arena, setDesc));
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        arena.reset();
        device.reset();
        assert(!domain->liveBytes() && !metadata->liveBytes());
        for (const auto version : {1u, 2u}) {
            device = create();
            assert(device
                && !device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, domain));
            b = {version, true, 1u, 0u, 0u, 5u, bytes};
            assert(!device->prepareOriginalResourceRegistration(b, metadata));
            assert(!device->createOriginalDescriptorArena(arenaDesc));
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-REGISTRATION-DESCRIPTOR-VERSION");
            device.reset();
            assert(!domain->liveBytes() && !metadata->liveBytes());
        }
    }
};
} // namespace arp::test
