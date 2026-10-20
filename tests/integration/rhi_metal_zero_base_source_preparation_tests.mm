#include "../fixtures/bindless_package_request_fixture.h"
#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/metal_closed_consumer_fixture.h"
#include "../fixtures/metal_closed_consumer_images_fixture.h"
#include "../fixtures/metal_indirect_execution_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/zero_base_indirect_source_fixture.h"
#include "rhi/backends/metal/metal_closed_consumer_geometry.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_zero_base_indirect_cold_bindings.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_cold_bindings.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_pipeline.h"
#include "rhi/backends/metal/metal_zero_base_indirect_packet_storage.h"
#include "rhi/backends/metal/metal_zero_base_indirect_pipeline.h"
#include "rhi/backends/metal/metal_zero_base_indirect_source.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_recording_validation.h"
#include "rhi/gpu_extensions.h"

#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef NDEBUG
#error Source preparation tests require assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
using Root = MetalRhiZeroBaseIndirectSource;
static_assert(!std::is_default_constructible_v<Root> && !std::is_copy_constructible_v<Root>);

std::shared_ptr<const MetalRhiZeroBaseIndirectColdBindings> coldBindingsPrerequisite(
    test::OrdinaryGraphicsFixture& f,
    const std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage>& packet)
{
    using Cold = MetalRhiZeroBaseIndirectColdBindings;
    using Contract = Cold::Contract;
    { // Genuine previously issued Metal storage-only packet remains excluded.
        using Recording = CanonicalClosedConsumerRecordingContract;
        Recording::Inputs inputs;
        inputs.producer.packet = packet->packet();
        Recording::Facts facts;
        const Recording::Budget budget{
            1u, true, Recording::commandUnits, Recording::readbackBytes, Recording::referenceSlots};
        assert(Recording::verify(inputs, budget, 1u, facts)
                == std::string_view("RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY")
            && !facts.identity.valid() && !facts.actualReadbackBytes);
    }
    static_assert(!std::is_default_constructible_v<Cold> && !std::is_copy_constructible_v<Cold>);
    auto selected = packet;
    Contract::Budget budget{1u, true, Contract::minimumOwnedBytes, 128u};
    const auto prepare = [&] {
        return Cold::prepare(selected, *f.device, budget, f.diagnostic);
    };
    const auto reject = [&](const char* id) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto denied = prepare();
        if (denied || f.diagnostic != id || f.quota->liveBytes() != cpu)
            std::fprintf(stderr,
                "Cold bindings expected %s: %s CPU%llu->%llu\n",
                id,
                f.diagnostic.c_str(),
                static_cast<unsigned long long>(cpu),
                static_cast<unsigned long long>(f.quota->liveBytes()));
        assert(!denied && f.diagnostic == id && f.quota->liveBytes() == cpu
            && f.device->memoryStats().allocatedBytes == gpu);
    };
    budget.prototypeOptIn = false;
    reject("RHI-METAL-ZERO-BASE-COLD-BUDGET");
    budget.prototypeOptIn = true;
    --budget.maxOwnedBytes;
    reject("RHI-METAL-ZERO-BASE-COLD-BYTE-BUDGET");
    ++budget.maxOwnedBytes;
    selected = std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage>(
        packet->pipeline(), packet.get());
    reject("RHI-METAL-ZERO-BASE-COLD-OWNER-MISMATCH");
    selected = packet;
    auto foreign = createMetalRhiDevice(test::OrdinaryGraphicsFixture::deviceDesc());
    assert(foreign);
    assert(!Cold::prepare(packet, *foreign, budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-ZERO-BASE-COLD-ISSUER-MISMATCH");
    foreign.reset();
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Cold bindings: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    const auto owners = root->childOwnerCount();
    budget.maxChildOwners = static_cast<std::uint32_t>(owners - 1u);
    reject("RHI-METAL-ZERO-BASE-COLD-CHILD-OWNER-BUDGET");
    budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    root.reset();
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-ZERO-BASE-COLD-ALLOCATION-FAILED");
    root = prepare();
    assert(root && !root->validate());
    assert(root->ownedBytes() == Contract::minimumOwnedBytes
        && root->generation() == root->faultDomain()->generation());
    assert(root->packet()->packet()->prototypeStorageOnly()
        && !root->packet()->packet()->consumerPlan().success);
    assert(root->bindings()[5].resource == packet->packet()->producerParameters()->id()
        && root->bindings()[5].resource != packet->pipeline()->source()->parameterResource().id());
    Contract::Resources metadataResources{
        {{1u,
             root->bindings()[2].resource,
             packet->pipeline()->source()->sourceResource().desc().sizeBytes},
            {2u, root->bindings()[3].resource, root->uploads()[1].buffer->desc().sizeBytes},
            {3u, root->bindings()[4].resource, root->uploads()[2].buffer->desc().sizeBytes},
            {4u,
                root->bindings()[5].resource,
                packet->packet()->producerParameters()->desc().sizeBytes}}};
    Contract::Words words;
    Contract::Bytes expected;
    assert(!Contract::buildMetadata(packet->pipeline()->source()->package()->artifact(),
        root->generation(),
        metadataResources,
        words,
        expected));
    assert(words[2] == static_cast<std::uint32_t>(root->generation())
        && words[3] == static_cast<std::uint32_t>(root->generation() >> 32u));
    assert(words[8] == 8u && words[13] == 5u && words[18] == 4u && words[23] == 1u);
    const auto latch =
        reinterpret_cast<id<MTLBuffer>>(root->resources()[1].buffer->resource.nativeObject.get());
    const auto readback =
        reinterpret_cast<id<MTLBuffer>>(root->readback().buffer->resource.nativeObject.get());
    assert(
        latch.length == 4u && readback.length == 4u && latch.storageMode == MTLStorageModePrivate);
    for (std::size_t i = 0u; i < root->uploads().size(); ++i) {
        std::array<std::uint8_t, sizeof(Contract::Bytes)> actual{};
        const auto& upload = root->uploads()[i];
        const auto size = upload.buffer->desc().sizeBytes;
        assert(readMetalRhiNativeBuffer(
            upload.resource.buffer, actual.data(), size, 0u, size, f.diagnostic));
        for (std::size_t byte = 0u; byte < size; ++byte)
            assert(actual[byte] == (i == 0u ? expected[byte] : 0u));
        assert(f.device->writeBuffer(*upload.buffer, actual.data(), size).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
        auto encoder = f.device->createCommandEncoder({"DeniedColdScratchTransfer"});
        auto transfer = encoder->beginTransferPass({"Denied"});
        assert(transfer->copyBufferToBuffer({upload.buffer->id(), upload.buffer->id(), 0u, 0u, 4u})
                   .diagnostics
            == "RHI-BUFFER-GPU-WRITES-SEALED");
        assert(transfer->end().success);
    }
    auto alternate = prepare();
    assert(alternate && alternate->generation() != root->generation());
    for (std::size_t i = 0u; i < root->bindings().size(); ++i) {
        auto& bindings = const_cast<Cold::Bindings&>(root->bindings());
        const auto saved = bindings[i];
        ++bindings[i].resource.value;
        assert(root->validate());
        bindings[i] = saved;
        ++bindings[i].abi.slot;
        assert(root->validate());
        bindings[i] = saved;
        ++bindings[i].binding;
        assert(root->validate());
        bindings[i] = saved;
        auto& resources = const_cast<Cold::Resources&>(root->resources());
        const auto savedResource = resources[i];
        resources[i] = alternate->resources()[i];
        if (i != 2u && i != 5u)
            assert(root->validate());
        else
            assert(!root->validate());
        resources[i] = savedResource;
        assert(!root->validate());
        if (i != 2u && i != 5u)
            test::MetalNativeIdentityMutationProbe::verify(root->resources()[i].buffer->resource,
                alternate->resources()[i].buffer->resource,
                [&] {
                    assert(root->validate());
                });
    }
    for (const auto& upload : root->uploads()) {
        auto& desc = const_cast<BufferDesc&>(upload.buffer->desc());
        const auto saved = desc;
        --desc.sizeBytes;
        assert(root->validate());
        desc = saved;
        f.registry->unregisterResource(upload.buffer->id());
        assert(root->validate());
        f.registry->registerBuffer(*upload.buffer);
        assert(!root->validate());
        ResourceBarrierDesc barrier;
        barrier.resource = upload.buffer->id();
        barrier.before = desc.initialState;
        barrier.after = desc.initialState == ResourceState::ShaderRead ? ResourceState::ShaderWrite
                                                                       : ResourceState::ShaderRead;
        assert(
            setMetalRhiBufferStateForBarrier(*upload.buffer, barrier).success && root->validate());
        std::swap(barrier.before, barrier.after);
        assert(
            setMetalRhiBufferStateForBarrier(*upload.buffer, barrier).success && !root->validate());
    }
    for (auto* buffer :
        {root->faultDomain()->faultLatchBuffer(), root->faultDomain()->faultReadbackBuffer()}) {
        auto& allocation = const_cast<ResourceAllocation&>(buffer->allocation());
        const auto savedBytes = allocation.estimatedSizeBytes;
        --allocation.estimatedSizeBytes;
        assert(root->validate());
        allocation.estimatedSizeBytes = savedBytes;
        ++allocation.estimatedSizeBytes;
        assert(root->validate());
        allocation.estimatedSizeBytes = savedBytes;
        ResourceBarrierDesc barrier;
        barrier.resource = buffer->id();
        barrier.before = buffer->desc().initialState;
        barrier.after = ResourceState::ShaderRead;
        assert(setMetalRhiBufferStateForBarrier(*buffer, barrier).success && root->validate());
        std::swap(barrier.before, barrier.after);
        assert(setMetalRhiBufferStateForBarrier(*buffer, barrier).success && !root->validate());
    }
    {
        auto& fault = const_cast<std::shared_ptr<BindlessOwnedFaultDomain>&>(root->faultDomain());
        const auto original = fault;
        fault = std::shared_ptr<BindlessOwnedFaultDomain>(packet, fault.get());
        assert(root->validate());
        fault = original;
        assert(!root->validate());
        fault = alternate->faultDomain();
        assert(root->validate());
        fault = original;
        test::MetalNativeIdentityMutationProbe::verify(
            root->readback().buffer->resource, alternate->readback().buffer->resource, [&] {
                assert(root->validate());
            });
    }
    {
        auto lease = BindlessFaultSubmissionLease::acquire(root->faultDomain(), f.diagnostic);
        assert(lease.valid());
        assert(!BindlessFaultSubmissionLease::acquire(root->faultDomain(), f.diagnostic).valid()
            && f.diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
        assert(!root->validate());
        lease.release();
        auto alias = std::shared_ptr<const BindlessFaultDomain>(packet, root->faultDomain().get());
        assert(!BindlessFaultSubmissionLease::acquire(alias, f.diagnostic).valid()
            && f.diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH");
        auto terminal =
            BindlessFaultSubmissionLease::acquire(alternate->faultDomain(), f.diagnostic);
        assert(terminal.valid());
        terminal.markFaultReadbackCompleted();
        assert(alternate->validate());
        terminal.release();
    }
    alternate.reset();
    const auto gpu = f.device->memoryStats().allocatedBytes,
               capacity = f.device->memoryStats().budgetCapacityBytes;
    for (const auto remaining : {Contract::minimumOwnedBytes, Contract::minimumOwnedBytes - 1u}) {
        auto pressure = f.device->createBuffer({"ColdBindingsGpuBudget",
            capacity - gpu - remaining,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(pressure);
        auto candidate = prepare();
        if (remaining == Contract::minimumOwnedBytes)
            assert(candidate && f.device->memoryStats().allocatedBytes == capacity);
        else
            assert(!candidate && f.diagnostic == "RHI-METAL-ZERO-BASE-COLD-BUFFER-CREATION-FAILED");
        candidate.reset();
        pressure.reset();
        assert(f.device->memoryStats().allocatedBytes == gpu);
    }
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate());
            auto& bindings = const_cast<Cold::Bindings&>(root->bindings());
            ++bindings[5].abi.slot;
            assert(root->validate());
            --bindings[5].abi.slot;
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    std::weak_ptr<const BindlessOwnedFaultDomain> fault = root->faultDomain();
    std::weak_ptr<void> object = root->uploads()[0].resource.buffer->resource.nativeObject,
                        token = root->uploads()[0].resource.buffer->resource.allocationReleaseToken;
    root->uploads()[0].resource.buffer->resource.nativeObject.reset();
    root->uploads()[0].resource.buffer->resource.allocationReleaseToken.reset();
    assert(root->validate() && !object.expired() && !token.expired());
    std::printf(
        "Canonical cold six native ABI resources metadata96 scratch20+16 fault4+4 owned%llu childOwners%zu coldPeak%llu; real fault generation, original params, sealed bytes, budgets/owners/allocation0 PASS; awaiting clear, no dispatch authority\n",
        static_cast<unsigned long long>(Contract::minimumOwnedBytes),
        owners,
        static_cast<unsigned long long>(peak));
    root.reset();
    assert(fault.expired() && object.expired() && token.expired());
    auto retained = prepare();
    assert(retained && !retained->validate());
    return retained;
}

void packetPrerequisite(test::OrdinaryGraphicsFixture& f,
    const std::shared_ptr<const Root>& source,
    const char* consumerPublication,
    bool cold = false)
{
    using Pipeline = MetalRhiZeroBaseIndirectPipeline;
    using Packet = MetalRhiZeroBaseIndirectPacketStorage;
    static_assert(
        !std::is_default_constructible_v<Packet> && !std::is_copy_constructible_v<Packet>);
    auto pipeline = Pipeline::prepare(source,
        semantics::nativeBackendCapabilityContract(Backend::Metal),
        f.budget(),
        {1u, true, 128u},
        f.diagnostic);
    if (source->package()->artifact().execution == BindlessExecution::Native) {
        assert(!pipeline && f.diagnostic == "RHI-METAL-ZERO-BASE-PIPELINE-SCALAR-PACKAGE-REQUIRED");
        return;
    }
    assert(pipeline && !pipeline->validate());
    core::UniquePtr<BindlessIndirectPacketPool> pool;
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        pool = core::makeUnique<BindlessIndirectPacketPool>(*f.device,
            BindlessIndirectPacketBudget{
                2u, Packet::Contract::minimumBytes, 2u * Packet::Contract::minimumBytes});
    }
    auto selected = pipeline;
    Packet::Budget budget{1u, true, Packet::Contract::minimumBytes, 128u};
    const auto prepare = [&] {
        return Packet::prepare(selected, *pool, budget, f.diagnostic);
    };
    const auto reject = [&](const char* id) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes,
                   live = pool->livePackets(), bytes = pool->chargedBytes();
        auto denied = prepare();
        if (denied || f.diagnostic != id || f.quota->liveBytes() != cpu)
            std::fprintf(stderr,
                "Packet expected %s: %s CPU%llu->%llu\n",
                id,
                f.diagnostic.c_str(),
                static_cast<unsigned long long>(cpu),
                static_cast<unsigned long long>(f.quota->liveBytes()));
        assert(!denied && f.diagnostic == id && f.quota->liveBytes() == cpu
            && f.device->memoryStats().allocatedBytes == gpu && pool->livePackets() == live
            && pool->chargedBytes() == bytes);
    };
    budget.prototypeOptIn = false;
    reject("RHI-METAL-ZERO-BASE-PACKET-BUDGET");
    budget.prototypeOptIn = true;
    --budget.maxPacketBytes;
    reject("RHI-METAL-ZERO-BASE-PACKET-BYTE-BUDGET");
    ++budget.maxPacketBytes;
    selected = std::shared_ptr<const Pipeline>(source, pipeline.get());
    reject("RHI-METAL-ZERO-BASE-PACKET-OWNER-MISMATCH");
    selected = pipeline;
    auto foreign = createMetalRhiDevice(test::OrdinaryGraphicsFixture::deviceDesc());
    assert(foreign);
    BindlessIndirectPacketPool foreignPool(
        *foreign, {1u, Packet::Contract::minimumBytes, Packet::Contract::minimumBytes});
    assert(!Packet::prepare(pipeline, foreignPool, budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-ZERO-BASE-PACKET-ISSUER-MISMATCH");
    foreign.reset();
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Cold packet: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    assert(root->validateClosedReadbackReady()
        == std::string_view("RHI-METAL-CLOSED-CONSUMER-READY-ORIGINAL-PURPOSE"));
    if (cold) {
        auto retained = coldBindingsPrerequisite(f, root);
        std::weak_ptr<const BindlessIndirectPacket> original = root->packet();
        std::weak_ptr<void> object = retained->uploads()[0].resource.buffer->resource.nativeObject,
                            token = retained->uploads()[0]
                                        .resource.buffer->resource.allocationReleaseToken;
        root.reset();
        pool.reset();
        assert(!original.expired() && !object.expired() && !token.expired());
        // The packet inherits the device issuer epoch, not pool lifetime.
        // Its count/byte leases and original native owners outlive the pool.
        assert(!retained->validate());
        retained.reset();
        assert(original.expired() && object.expired() && token.expired());
        return;
    }
    const auto owners = root->childOwnerCount();
    budget.maxChildOwners = static_cast<std::uint32_t>(owners - 1u);
    reject("RHI-METAL-ZERO-BASE-PACKET-CHILD-OWNER-BUDGET");
    budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    root.reset();
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-ZERO-BASE-PACKET-ALLOCATION-FAILED");
    root = prepare();
    assert(root && !root->validate());
    assert(root->packet()->prototypeStorageOnly() && root->packet()->validateStorage().success
        && root->packet()->validate().diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY"
        && !root->packet()->consumerPlan().success && !root->packet()->consumerPlan().executable);
    assert(root->packet()->producerParameters()->id() != source->parameterResource().id());
    Packet::Contract::ParameterBytes expected;
    assert(!Packet::Contract::encodeParameters(source->content().parameters(), expected));
    for (std::size_t i = 0u; i < root->resources().size(); ++i) {
        const auto spec = Packet::Contract::descriptor(Packet::Contract::roles[i]);
        std::array<std::uint8_t, sizeof(DrawIndexedIndirectCommand)> bytes{};
        assert(readMetalRhiNativeBuffer(root->resources()[i].buffer,
            bytes.data(),
            spec.sizeBytes,
            0u,
            spec.sizeBytes,
            f.diagnostic));
        for (std::size_t byte = 0u; byte < spec.sizeBytes; ++byte)
            assert(bytes[byte] == (i == 2u ? expected[byte] : 0u));
        assert(f.device->writeBuffer(*root->buffers()[i], bytes.data(), spec.sizeBytes).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
    }
    auto alternate = prepare();
    assert(alternate && pool->livePackets() == 2u);
    reject("RHI-BINDLESS-INDIRECT-PACKET-POOL-FULL");
    for (std::size_t i = 0u; i < root->resources().size(); ++i) {
        auto& buffer = *root->buffers()[i];
        const auto spec = Packet::Contract::descriptor(Packet::Contract::roles[i]);
        auto encoder = f.device->createCommandEncoder({"DeniedColdPacketTransfer"});
        auto transfer = encoder->beginTransferPass({"Denied"});
        assert(
            transfer->copyBufferToBuffer({alternate->buffers()[i]->id(), buffer.id(), 0u, 0u, 4u})
                .diagnostics
            == "RHI-BUFFER-GPU-WRITES-SEALED");
        assert(transfer->end().success);
        auto& desc = const_cast<BufferDesc&>(buffer.desc());
        const auto saved = desc;
        --desc.sizeBytes;
        assert(root->validate());
        desc = saved;
        f.registry->unregisterResource(buffer.id());
        assert(root->validate());
        f.registry->registerBuffer(buffer);
        ResourceBarrierDesc barrier;
        barrier.resource = buffer.id();
        barrier.before = spec.initialState;
        barrier.after = spec.initialState == ResourceState::ShaderRead ? ResourceState::ShaderWrite
                                                                       : ResourceState::ShaderRead;
        assert(setMetalRhiBufferStateForBarrier(buffer, barrier).success && root->validate());
        std::swap(barrier.before, barrier.after);
        assert(setMetalRhiBufferStateForBarrier(buffer, barrier).success && !root->validate());
        test::MetalNativeIdentityMutationProbe::verify(
            root->resources()[i].buffer->resource, alternate->resources()[i].buffer->resource, [&] {
                assert(root->validate());
            });
        auto& aliases = const_cast<Packet::Buffers&>(root->buffers());
        const auto savedOwner = aliases[i];
        aliases[i] = std::shared_ptr<GpuBuffer>(pipeline, savedOwner.get());
        assert(root->validate());
        aliases[i] = savedOwner;
    }
    auto& parameters = const_cast<ZeroBaseIndirectProducerParams&>(root->packet()->parameters());
    ++parameters.sourceWordOffset;
    assert(root->validate());
    --parameters.sourceWordOffset;
    assert(!root->validate());
    auto* extension = static_cast<const BindlessCrossExtension*>(
        f.device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    assert(!extension->capabilities().indirectPacketStorageContractVersion
        && !extension->capabilities().indirectInstanceConsumerContractVersion
        && !extension->capabilities().zeroBaseIndirectProducerContractVersion);
    assert(!pool->prepare(
               {}, source->package()->artifact(), source->content().parameters(), f.diagnostic)
        && f.diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-UNAVAILABLE");
    assert(root->packet()
               ->validateProducerAccess(source->sourceResource(), BindlessAccessArray(nullptr, 0u))
               .diagnostics
        == "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY");
    {
        // Real Null snapshot is only a negative admission probe; it never
        // becomes a native consumer or supplies this packet's storage.
        auto validator = createNullDevice();
        const auto* validationExtension = static_cast<const BindlessCrossExtension*>(
            validator->queryInterface(kExtensionBindlessCross));
        auto consumer = BindlessShaderPackage::load(
            consumerPublication, {f.cpuBytes, f.cpuBytes * 4u, 64u, 8u, 64u, 8u}, f.diagnostic);
        assert(consumer);
        const auto& artifact = consumer->artifact();
        BindlessRequirements request;
        request.optIn = true;
        request.allowEmulated = true;
        request.deviceIdentity = validationExtension->capabilities().deviceIdentity;
        request.shaderPackageIdentity = artifact.shaderPackageIdentity;
        std::uint64_t descriptors = 0u;
        for (const auto& binding : artifact.physicalLayout.bindings)
            descriptors += binding.arrayCount;
        assert(descriptors && descriptors <= UINT32_MAX);
        request.budget = {1u,
            static_cast<std::uint32_t>(descriptors),
            artifact.snapshotBytes,
            artifact.switchCases};
        for (const auto& table : artifact.tables) {
            const auto binding = std::find_if(artifact.physicalLayout.bindings.begin(),
                artifact.physicalLayout.bindings.end(),
                [&](const auto& value) {
                    return value.binding == table.firstBinding;
                });
            assert(binding != artifact.physicalLayout.bindings.end());
            request.tables.push_back({table.tableId,
                *binding,
                1u,
                table.operations,
                table.nonUniform,
                table.bufferLayout});
        }
        BindlessSnapshotPool snapshots(*validator, request.budget);
        auto snapshot = snapshots.prepareShared(request, artifact, f.diagnostic);
        if (!snapshot)
            std::fprintf(stderr, "Null snapshot lease probe: %s\n", f.diagnostic.c_str());
        assert(snapshot);
        auto encoder = f.device->createCommandEncoder({"ColdPacketLeaseRejected", 1u, 1u});
        assert(encoder && encoder->recordingIdentity());
        assert(root->packet()
                   ->validateConsumer(*snapshot, snapshot->plan(), snapshot->generation())
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY");
        assert(!BindlessIndirectPacketRecordingLease::acquire(root->packet(),
                   snapshot,
                   snapshot->plan(),
                   snapshot->generation(),
                   encoder->recordingIdentity(),
                   f.diagnostic)
                   .valid()
            && f.diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY");
        assert(!root->validate() && pool->livePackets() == 2u);
    }
    alternate.reset();
    const auto gpu = f.device->memoryStats().allocatedBytes,
               capacity = f.device->memoryStats().budgetCapacityBytes;
    for (const auto remaining :
        {Packet::Contract::minimumBytes, Packet::Contract::minimumBytes - 1u}) {
        auto pressure = f.device->createBuffer({"ColdPacketGpuBudget",
            capacity - gpu - remaining,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(pressure);
        auto candidate = prepare();
        if (remaining == Packet::Contract::minimumBytes)
            assert(candidate && f.device->memoryStats().allocatedBytes == capacity);
        else
            assert(!candidate
                && f.diagnostic == "RHI-BINDLESS-INDIRECT-PACKET-RESOURCE-CREATION-FAILED");
        candidate.reset();
        pressure.reset();
        assert(f.device->memoryStats().allocatedBytes == gpu && pool->livePackets() == 1u);
    }
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate() && !root->packet()->storageDiagnostic());
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    std::weak_ptr<const BindlessIndirectPacket> packet = root->packet();
    std::weak_ptr<void> object = root->resources()[2].buffer->resource.nativeObject,
                        token = root->resources()[2].buffer->resource.allocationReleaseToken;
    root->resources()[2].buffer->resource.nativeObject.reset();
    root->resources()[2].buffer->resource.allocationReleaseToken.reset();
    assert(root->validate() && !object.expired() && !token.expired());
    std::printf(
        "Canonical cold Prototype packet52 bytes, distinct actual params16, childOwners%zu coldPeak%llu; exact/one-less CPU/GPU/pool/owners, issuer/seals/control blocks, allocation0 PASS; consumer plan unissued\n",
        owners,
        static_cast<unsigned long long>(peak));
    pool.reset();
    root.reset();
    assert(packet.expired() && object.expired() && token.expired());
}

void pipelinePrerequisite(
    test::OrdinaryGraphicsFixture& f, const std::shared_ptr<const Root>& source)
{
    using Pipeline = MetalRhiZeroBaseIndirectPipeline;
    static_assert(
        !std::is_default_constructible_v<Pipeline> && !std::is_copy_constructible_v<Pipeline>);
    auto selected = source;
    auto capability = semantics::nativeBackendCapabilityContract(Backend::Metal);
    Pipeline::Budget budget{1u, true, 128u};
    const auto prepare = [&] {
        return Pipeline::prepare(selected, capability, f.budget(), budget, f.diagnostic);
    };
    const auto reject = [&](const char* id) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto denied = prepare();
        if (denied || f.diagnostic != id)
            std::fprintf(stderr, "Pipeline expected %s: %s\n", id, f.diagnostic.c_str());
        assert(!denied && f.diagnostic == id && f.quota->liveBytes() == cpu
            && f.device->memoryStats().allocatedBytes == gpu);
    };
    if (source->package()->artifact().execution == BindlessExecution::Native) {
        reject("RHI-METAL-ZERO-BASE-PIPELINE-SCALAR-PACKAGE-REQUIRED");
        std::puts(
            "Canonical Native pipeline: inactive array target refused before native compilation; no package substitution PASS");
        return;
    }
    selected = std::shared_ptr<const Root>(source->index(), source.get());
    reject("RHI-METAL-ZERO-BASE-PIPELINE-OWNER-MISMATCH");
    selected = source;
    budget.prototypeOptIn = false;
    reject("RHI-METAL-ZERO-BASE-PIPELINE-BUDGET");
    budget.prototypeOptIn = true;
    capability.backend = Backend::Vulkan;
    reject("RHI-METAL-ZERO-BASE-PIPELINE-BACKEND");
    capability.backend = Backend::Metal;
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Canonical pipeline: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    const auto owners = root->childOwnerCount();
    budget.maxChildOwners = static_cast<std::uint32_t>(owners - 1u);
    reject("RHI-METAL-ZERO-BASE-PIPELINE-CHILD-OWNER-BUDGET");
    budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    root.reset();
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-ZERO-BASE-PIPELINE-ALLOCATION-FAILED"
        || f.diagnostic == "RHI-METAL-ZERO-BASE-PIPELINE-NATIVE-PAYLOAD-MISSING");
    root = prepare();
    assert(root && !root->validate());
    auto& native = *root->native();
    for (const auto& fact : root->facts())
        std::printf("Canonical ABI slot%u alignment%llu minimum%llu access%u element%u\n",
            fact.slot,
            static_cast<unsigned long long>(fact.alignment),
            static_cast<unsigned long long>(fact.requiredBytes),
            fact.access,
            fact.elementType);
    const auto mutate = [&](auto& field, auto replacement) {
        const auto saved = field;
        field = replacement;
        assert(root->validate());
        field = saved;
        assert(!root->validate());
    };
    mutate(native.computeBufferFactsVersion, 0u);
    for (auto& fact : native.computeBufferFacts) {
        mutate(fact.access, UINT32_MAX);
        mutate(fact.elementType, UINT32_MAX);
        mutate(fact.requiredBytes, std::uint64_t{0u});
        mutate(fact.alignment, fact.alignment * 2u);
    }
    for (auto& map : native.metalBindingSpace) {
        mutate(map.active, false);
        mutate(map.binding, UINT32_MAX);
    }
    mutate(native.computeWorkgroupSizeX, 2u);
    mutate(native.nativeRuntimeDiagnosticOnly, true);
    native.computeBufferFacts.push_back(native.computeBufferFacts.front());
    assert(root->validate());
    native.computeBufferFacts.pop_back();
    for (auto* field : {&native.nativeDevice,
             &native.shaderLibrary,
             &native.computeFunction,
             &native.pipelineState})
        test::MetalNativeIdentityMutationProbe::verifyOwner(
            *field, source->sourceBuffer()->resource.nativeObject, [&] {
                assert(root->validate());
            });
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate());
            for (std::size_t j = 0u; j < owners; ++j)
                assert(root->childOwnerAt(j));
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    assert(!semantics::ZeroBaseIndirectSourceLowering::current(Backend::Metal).executable);
    std::weak_ptr<void> function = native.computeFunction, library = native.shaderLibrary,
                        pipeline = native.pipelineState;
    std::printf(
        "Canonical Emulated pipeline six real queried scalar roles, childOwners%zu coldPeak%llu; hostile ABI/mapping/function owners, exact/one-less budgets, allocation0 PASS; no dispatch authority\n",
        owners,
        static_cast<unsigned long long>(peak));
    root.reset();
    assert(function.expired() && library.expired() && pipeline.expired());
}

void consumerPipelinePrerequisite(test::OrdinaryGraphicsFixture& f,
    const std::shared_ptr<const Root>& source,
    const char* publication,
    const char* nativePublication)
{
    using Pipeline = MetalRhiZeroBaseIndirectConsumerPipeline;
    static_assert(
        !std::is_default_constructible_v<Pipeline> && !std::is_copy_constructible_v<Pipeline>);
    static_assert(MTLDataTypeUInt4 == 36u && MTLDataTypeUInt == 33u
        && MTLBindingAccessReadWrite == 1u && MTLTextureType2D == 2u && MTLDataTypeFloat == 3u);
    test::MetalClosedConsumerFixture fixture(f, publication, nativePublication);
    auto package = fixture.package, nativePackage = fixture.nativePackage;
    auto& state = fixture.state;
    Pipeline::Budget budget{1u, true, 128u};
    auto selected = package;
    auto selectedSource = source;
    auto capability = semantics::nativeBackendCapabilityContract(Backend::Metal);
    const auto prepare = [&] {
        return Pipeline::prepare(
            selectedSource, selected, state, capability, f.budget(), budget, f.diagnostic);
    };
    const auto reject = [&](const char* id) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto denied = prepare();
        if (denied || f.diagnostic != id)
            std::fprintf(stderr, "Closed consumer expected %s: %s\n", id, f.diagnostic.c_str());
        assert(!denied && f.diagnostic == id && f.quota->liveBytes() == cpu
            && f.device->memoryStats().allocatedBytes == gpu);
    };
    budget.prototypeOptIn = false;
    reject("RHI-METAL-CLOSED-CONSUMER-BUDGET");
    budget.prototypeOptIn = true;
    selected = nativePackage;
    reject("RHI-METAL-CLOSED-CONSUMER-SCALAR-PACKAGE-REQUIRED");
    selected = package;
    selected = std::shared_ptr<const BindlessShaderPackage>(source, package.get());
    reject("RHI-METAL-CLOSED-CONSUMER-OWNER-MISMATCH");
    selected = package;
    selectedSource = std::shared_ptr<const Root>(package, source.get());
    reject("RHI-METAL-CLOSED-CONSUMER-OWNER-MISMATCH");
    selectedSource = source;
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
        core::CpuMemoryQuotaScope scope(quota);
        selected = BindlessShaderPackage::load(
            publication, {f.cpuBytes, f.cpuBytes * 4u, 64u, 8u, 32u, 8u}, f.diagnostic);
        assert(selected);
        reject("RHI-METAL-CLOSED-CONSUMER-QUOTA-MISMATCH");
        selected = package;
    }
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::WebGPU}) {
        capability.backend = backend;
        reject("RHI-METAL-CLOSED-CONSUMER-BACKEND");
        assert(!Pipeline::Contract::lowering(backend).executable);
    }
    capability.backend = Backend::Metal;
    const auto savedFormat = state.vertexBuffers[0].attributes[0].format;
    state.vertexBuffers[0].attributes[0].format = VertexFormat::Float32x3;
    reject("RHI-METAL-CLOSED-CONSUMER-VERTEX-INTERFACE");
    state.vertexBuffers[0].attributes[0].format = savedFormat;
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Closed consumer: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    const auto owners = root->childOwnerCount();
    assert(owners
        == 3u + source->childOwnerCount() + MetalRhiPreparedFunctionOwnership::renderOwnerCount);
    assert(root->sharedVertexInput()->sharedOwnerMatches(root->sharedVertexInput()));
    budget.maxChildOwners = static_cast<std::uint32_t>(owners - 1u);
    reject("RHI-METAL-CLOSED-CONSUMER-CHILD-OWNER-BUDGET");
    budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    root.reset();
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-CLOSED-CONSUMER-ALLOCATION-FAILED");
    root = prepare();
    assert(root && !root->validate());
    auto& native = *root->native();
    const auto mutate = [&](auto& field, auto replacement) {
        const auto saved = field;
        field = replacement;
        assert(root->validate());
        field = saved;
        assert(!root->validate());
    };
    for (auto& stage : native.graphicsBufferFacts) {
        mutate(stage.version, 0u);
        mutate(stage.stage, ShaderStage::Compute);
        for (auto& fact : stage.entries) {
            mutate(fact.access, UINT32_MAX);
            mutate(fact.elementType, UINT32_MAX);
            mutate(fact.requiredBytes, std::uint64_t{0u});
            mutate(fact.alignment, fact.alignment * 2u);
        }
        stage.entries.push_back(stage.entries.front());
        assert(root->validate());
        stage.entries.pop_back();
    }
    {
        const auto stages = native.nativeBindingStages;
        native.nativeBindingStages = {};
        assert(root->validate());
        native.nativeBindingStages = stages;
        assert(
            native.nativeBindingStages.record(DescriptorType::Sampler, 0u, ShaderStage::Compute));
        assert(root->validate());
        native.nativeBindingStages = stages;
        assert(!root->validate());
    }
    mutate(native.graphicsTextureFactsVersion, 0u);
    for (auto& fact : native.graphicsTextureFacts) {
        mutate(fact.stage, ShaderStage::Vertex);
        mutate(fact.slot, UINT32_MAX);
        mutate(fact.argumentIndex, 0u);
        mutate(fact.textureType, UINT32_MAX);
        mutate(fact.scalarDataType, UINT32_MAX);
        mutate(fact.depth, true);
    }
    for (auto& stage : const_cast<Pipeline::Stages&>(root->stages())) {
        mutate(stage.stage, ShaderStage::Compute);
        for (auto& map : stage.direct) {
            mutate(map.active, false);
            mutate(map.binding, UINT32_MAX);
            mutate(map.metalSlot, UINT32_MAX);
        }
    }
    mutate(native.vertexInputFacts.contractVersion, 0u);
    for (auto& fact : native.vertexInputFacts.streams) {
        mutate(fact.stride, fact.stride + 4u);
        mutate(fact.physicalSlot, UINT32_MAX);
    }
    for (auto& fact : native.vertexInputFacts.shaderInputs) {
        mutate(fact.active, false);
        mutate(fact.type, UINT32_MAX);
    }
    mutate(native.cullMode, CullMode::Back);
    mutate(native.sampleCount, 2u);
    mutate(native.nativeRuntimeDiagnosticOnly, true);
    for (auto* owner : {&native.nativeDevice,
             &native.shaderLibrary,
             &native.fragmentShaderLibrary,
             &native.vertexFunction,
             &native.fragmentFunction,
             &native.pipelineState,
             &native.depthStencilState})
        test::MetalNativeIdentityMutationProbe::verifyOwner(
            *owner, source->sourceBuffer()->resource.nativeObject, [&] {
                assert(root->validate());
            });
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate());
            ++native.graphicsBufferFacts[0].entries.front().elementType;
            assert(root->validate());
            --native.graphicsBufferFacts[0].entries.front().elementType;
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    for (const auto& fact : root->facts().buffers)
        std::printf("Closed consumer buffer slot%u alignment%llu bytes%llu access%u element%u\n",
            fact.slot,
            static_cast<unsigned long long>(fact.alignment),
            static_cast<unsigned long long>(fact.requiredBytes),
            fact.access,
            fact.elementType);
    for (const auto& fact : root->facts().textures)
        std::printf("Closed consumer texture stage%u slot%u type%u scalar%u depth%d\n",
            static_cast<unsigned>(fact.stage),
            fact.slot,
            fact.textureType,
            fact.scalarDataType,
            fact.depth);
    const auto* extension = static_cast<const BindlessCrossExtension*>(
        f.device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    assert(!extension->capabilities().indirectInstanceConsumerContractVersion
        && !extension->capabilities().zeroBaseIndirectProducerContractVersion);
    // Cold native pipeline facts cannot authorize a public consumer snapshot.
    // This device has no public executable payload opt-in. Both compiler
    // targets fail the actual generic execution gate before leases/resources.
    for (const auto& target : {package, nativePackage}) {
        auto request = test::BindlessPackageRequestFixture::make(
            target->artifact(), extension->capabilities().deviceIdentity);
        core::CpuMemoryQuotaScope scope(f.quota);
        BindlessSnapshotPool snapshots(*f.device, request.budget);
        const auto gpu = f.device->memoryStats().allocatedBytes;
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!snapshots.prepare(request, target->artifact(), f.diagnostic)
                && f.diagnostic == "RHI-BINDLESS-EXECUTION-UNAVAILABLE");
            assert(!snapshots.liveSnapshots() && !snapshots.chargedBytes()
                && f.device->memoryStats().allocatedBytes == gpu);
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    std::puts(
        "Closed consumer Native/Emulated public snapshot gates reject allocation0 with no leases/GPU allocations");
    std::weak_ptr<void> pipeline = native.pipelineState, function = native.vertexFunction,
                        library = native.fragmentShaderLibrary;
    std::printf(
        "Closed consumer real uint4 Vertex, two uint32 Fragment and three images; childOwners%zu coldPeak%llu, hostile facts/owners/budgets/allocation0 PASS; no consumer authorization\n",
        owners,
        static_cast<unsigned long long>(peak));
    root.reset();
    assert(pipeline.expired() && function.expired() && library.expired());
}

void consumerGeometryPrerequisite(test::OrdinaryGraphicsFixture& f,
    const std::shared_ptr<const Root>& source,
    const char* publication,
    const char* nativePublication,
    core::UnsignedElementSpan::ByteOrder order,
    bool images = false,
    bool issuance = false,
    unsigned recordingStage = 0u)
{
    using Geometry = MetalRhiClosedConsumerGeometry;
    using Values = Geometry::Contract;
    static_assert(
        !std::is_default_constructible_v<Geometry> && !std::is_copy_constructible_v<Geometry>);
    test::MetalClosedConsumerFixture fixture(f, publication, nativePublication);
    auto pipeline = fixture.prepare(source);
    assert(pipeline);
    std::array<std::uint8_t, 32u> bytes;
    core::UnsignedElementSpan::encodeWordsLittleEndian(std::array<std::uint32_t, 8u>{0xbf800000u,
                                                           0xbf800000u,
                                                           0x3f800000u,
                                                           0xbf800000u,
                                                           0u,
                                                           0x3f800000u,
                                                           0x3f000000u,
                                                           0x3f000000u},
        bytes);
    if (order == core::UnsignedElementSpan::ByteOrder::BigEndian)
        for (std::size_t i = 0u; i < bytes.size(); i += sizeof(std::uint32_t))
            std::reverse(bytes.begin() + i, bytes.begin() + i + sizeof(std::uint32_t));
    Values::Input input{bytes.data(), bytes.size(), Values::Encoding::Ieee754Binary32, order};
    Geometry::Budget budget{
        {1u, true, bytes.size(), bytes.size() + source->content().sourceBytes().size()},
        bytes.size(),
        128u};
    auto selected = pipeline;
    const auto prepare = [&] {
        return Geometry::prepare(selected, *f.device, input, budget, f.diagnostic);
    };
    if (images) {
        auto geometry = prepare();
        assert(geometry && !geometry->validate());
        test::MetalClosedConsumerImagesFixture imageFixture(f);
        imageFixture.verify(geometry, issuance, recordingStage);
        return;
    }
    const auto reject = [&](const char* expected) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto denied = prepare();
        if (denied || f.diagnostic != expected)
            std::fprintf(stderr, "Geometry expected %s got %s\n", expected, f.diagnostic.c_str());
        assert(!denied && f.diagnostic == expected && cpu == f.quota->liveBytes()
            && gpu == f.device->memoryStats().allocatedBytes);
    };
    budget.values.prototypeOptIn = false;
    reject("RHI-CANONICAL-CONSUMER-GEOMETRY-BUDGET");
    budget.values.prototypeOptIn = true;
    --budget.maxUploadBytes;
    reject("RHI-METAL-CLOSED-GEOMETRY-UPLOAD-BUDGET");
    ++budget.maxUploadBytes;
    const auto owners = 2u + pipeline->childOwnerCount() + 1u
        + MetalRhiResourceOwnerProjection::independentBufferOwnerCount;
    budget.maxChildOwners = owners - 1u;
    reject("RHI-METAL-CLOSED-GEOMETRY-CHILD-OWNER-BUDGET");
    budget.maxChildOwners = owners;
    selected = std::shared_ptr<const Geometry::Pipeline>(source, pipeline.get());
    reject("RHI-METAL-CLOSED-GEOMETRY-ORIGINAL-OWNER");
    selected = pipeline;
    auto foreign = createMetalRhiDevice(test::OrdinaryGraphicsFixture::deviceDesc());
    assert(foreign);
    assert(!Geometry::prepare(pipeline, *foreign, input, budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-CLOSED-GEOMETRY-ISSUER-MISMATCH");
    foreign.reset();
    // Establish the existing registry's bounded capacity before measuring a
    // repeatable root factory; its retained capacity is not a root allocation.
    {
        auto warm = prepare();
        assert(warm && !warm->validate());
    }
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-CLOSED-GEOMETRY-ALLOCATION-FAILED");
    const auto gpu = f.device->memoryStats().allocatedBytes,
               capacity = f.device->memoryStats().budgetCapacityBytes;
    for (const auto available : {bytes.size(), bytes.size() - 1u}) {
        auto pressure = f.device->createBuffer({"GeometryGpuBudget",
            capacity - gpu - available,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(pressure);
        auto candidate = prepare();
        if (available == bytes.size())
            assert(candidate && f.device->memoryStats().allocatedBytes == capacity);
        else
            assert(
                !candidate && f.diagnostic == "RHI-METAL-CLOSED-GEOMETRY-BUFFER-CREATION-FAILED");
        candidate.reset();
        pressure.reset();
        assert(f.device->memoryStats().allocatedBytes == gpu);
    }
    auto root = prepare();
    assert(root && !root->validate() && root->childOwnerCount() == owners);
    std::array<std::uint8_t, 32u> native;
    assert(readMetalRhiNativeBuffer(
        root->resource().buffer, native.data(), native.size(), 0u, native.size(), f.diagnostic));
    assert(std::equal(native.begin(), native.end(), root->content().bytes().begin()));
    bytes.fill(0xffu);
    assert(!root->validate());
    auto& desc = const_cast<BufferDesc&>(root->buffer()->desc());
    const auto savedDesc = desc;
    --desc.sizeBytes;
    assert(root->validate());
    desc = savedDesc;
    desc.usage = ResourceUsage::StorageBuffer;
    assert(root->validate());
    desc = savedDesc;
    auto& allocation = const_cast<ResourceAllocation&>(root->buffer()->allocation());
    const auto reported = allocation.estimatedSizeBytes;
    ++allocation.estimatedSizeBytes;
    assert(root->validate());
    allocation.estimatedSizeBytes = reported;
    auto& range = const_cast<MetalRhiStageBufferRange&>(root->range());
    auto savedRange = range;
    ++range.fact.slot;
    assert(root->validate());
    range = savedRange;
    ++range.fact.alignment;
    assert(root->validate());
    range = savedRange;
    range.offset = 4u;
    assert(root->validate());
    range = savedRange;
    test::MetalNativeIdentityMutationProbe::verify(
        root->resource().buffer->resource, source->sourceBuffer()->resource, [&] {
            assert(root->validate());
        });
    f.registry->unregisterResource(root->buffer()->id());
    assert(root->validate());
    f.registry->registerBuffer(*root->buffer());
    assert(!root->validate());
    ResourceBarrierDesc barrier;
    barrier.resource = root->buffer()->id();
    barrier.before = ResourceState::ShaderRead;
    barrier.after = ResourceState::CopySrc;
    assert(setMetalRhiBufferStateForBarrier(*root->buffer(), barrier).success && root->validate());
    std::swap(barrier.before, barrier.after);
    assert(setMetalRhiBufferStateForBarrier(*root->buffer(), barrier).success && !root->validate());
    assert(f.device->writeBuffer(*root->buffer(), native.data(), native.size()).diagnostics
        == "RHI-BUFFER-HOST-WRITES-SEALED");
    auto encoder = f.device->createCommandEncoder({"GeometryTransferDenied"});
    assert(encoder);
    auto pass = encoder->beginTransferPass({"GeometryTransferDenied"});
    assert(pass);
    assert(
        pass->copyBufferToBuffer({source->sourceResource().id(), root->buffer()->id(), 0u, 0u, 4u})
            .diagnostics
        == "RHI-BUFFER-GPU-WRITES-SEALED");
    assert(pass->end().success);
    pass.reset();
    encoder.reset();
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate());
            ++range.fact.slot;
            assert(root->validate());
            --range.fact.slot;
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    std::weak_ptr<void> token = root->resource().buffer->resource.allocationReleaseToken;
    std::printf(
        "MetalClosedConsumerGeometryPreparation/1: actual sealed VertexBuffer32, source/package/index/pipeline originals, childOwners%zu coldPeak%llu, native slot%u alignment%llu extent%llu; exact endian GPU bytes, exact/one-less CPU/GPU/owners, mutations/retirement/allocation0 PASS; no draw\n",
        owners,
        static_cast<unsigned long long>(peak),
        range.fact.slot,
        static_cast<unsigned long long>(range.fact.alignment),
        static_cast<unsigned long long>(range.fact.requiredBytes));
    savedRange = {};
    pipeline.reset();
    selected.reset();
    root.reset();
    assert(token.expired() && f.device->memoryStats().allocatedBytes == gpu);
    // A new original root retains native storage but cannot revive its issuer.
    bytes = native;
    input.order = core::UnsignedElementSpan::ByteOrder::LittleEndian;
    selected = fixture.prepare(source);
    assert(selected);
    root = prepare();
    assert(root && !root->validate());
    token = root->resource().buffer->resource.allocationReleaseToken;
    f.device.reset();
    assert(root->validate() == std::string_view("RHI-METAL-CLOSED-GEOMETRY-ISSUER-RETIRED")
        && !token.expired());
    root.reset();
    selected.reset();
    assert(token.expired());
    assert(!inspectSharedMetalRhiCommandCapacity().charged);
}

void canonicalRecordingPrerequisite(test::OrdinaryGraphicsFixture& f,
    const std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerColdBindings>& cold)
{
    using Prep = MetalRhiCanonicalProducerRecordingPreparation;
    using Values = Prep::Contract;
    static_assert(!std::is_default_constructible_v<Prep> && !std::is_copy_constructible_v<Prep>);
    const auto makeCommand = [&] {
        const char* id = nullptr;
        auto retention = common::CommandRetentionStorage::prepare(
            {1u, true, 256u, 0u, 0u, 0u, f.cpuBytes}, f.quota, id);
        assert(retention);
        auto command = createMetalRhiNativeCommandPayload(
            "CanonicalColdRecording", f.registry, &f.diagnostic, 0u, 0u, 0u, std::move(retention));
        assert(command);
        return command;
    };
    auto command = makeCommand();
    Prep::Budget budget{
        {1u, true, Values::commandUnits, Values::readbackBytes, Values::referenceSlots},
        Prep::minimumNewBytes,
        256u};
    auto selected = cold;
    const auto prepare = [&] {
        return Prep::prepare(selected, *f.device, command, budget, f.diagnostic);
    };
    const auto reject = [&](const char* expected) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto result = prepare();
        if (result || f.diagnostic != expected)
            std::fprintf(
                stderr, "Canonical prep expected %s, got %s\n", expected, f.diagnostic.c_str());
        assert(!result && f.diagnostic == expected && cpu == f.quota->liveBytes()
            && gpu == f.device->memoryStats().allocatedBytes);
    };
    budget.values.prototypeOptIn = false;
    reject("RHI-CANONICAL-PRODUCER-RECORDING-BUDGET");
    budget.values.prototypeOptIn = true;
    --budget.maxNewBytes;
    reject("RHI-METAL-CANONICAL-PRODUCER-PREPARATION-BUDGET");
    ++budget.maxNewBytes;
    selected = std::shared_ptr<const Prep::Cold>(cold->producer(), cold.get());
    reject("RHI-METAL-CANONICAL-PRODUCER-ORIGINAL-OWNER");
    selected = cold;
    const auto required = 1u + cold->childOwnerCount() + Values::referenceSlots + 1u
        + Prep::Buffers{}.size()
            * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount);
    budget.maxChildOwners = static_cast<std::uint32_t>(required - 1u);
    reject("RHI-METAL-CANONICAL-PRODUCER-CHILD-OWNER-BUDGET");
    budget.maxChildOwners = static_cast<std::uint32_t>(required);
    {
        auto blocker =
            BindlessFaultSubmissionLease::acquire(cold->producer()->faultDomain(), f.diagnostic);
        assert(blocker.valid());
        reject("RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
    }
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-PREPARATION-ALLOCATION-FAILED");
    const auto gpu = f.device->memoryStats().allocatedBytes,
               capacity = f.device->memoryStats().budgetCapacityBytes;
    for (const auto remaining : {Prep::minimumNewBytes, Prep::minimumNewBytes - 1u}) {
        auto pressure = f.device->createBuffer({"CanonicalPrepGpuBudget",
            capacity - gpu - remaining,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(pressure);
        auto candidate = prepare();
        if (remaining == Prep::minimumNewBytes)
            assert(candidate && f.device->memoryStats().allocatedBytes == capacity);
        else
            assert(!candidate
                && f.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-READBACK-CREATION-FAILED");
        candidate.reset();
        pressure.reset();
        assert(f.device->memoryStats().allocatedBytes == gpu);
    }
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Canonical preparation: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    assert(!command->recordCanonicalProducer(root, f.diagnostic)
        && f.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-RECORDING-DOMAIN");
    assert(root->ownedBytes() == Prep::minimumNewBytes && root->childOwnerCount() == required
        && root->facts().actualReadbackBytes == Values::readbackBytes
        && root->facts().identity.recording == command->recordingIdentity());
    reject("RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
    auto consumerLease = BindlessFaultSubmissionLease::acquire(cold->faultDomain(), f.diagnostic);
    assert(consumerLease.valid());
    assert(!BindlessFaultSubmissionLease::acquire(cold->producer()->faultDomain(), f.diagnostic)
            .valid());
    {
        auto& stats = *command->stats();
        stats.nativeRuntimeBacked = false;
        assert(root->validate() == std::string_view("RHI-METAL-CANONICAL-PRODUCER-COMMAND-CLOSED"));
        stats.nativeRuntimeBacked = true;
        stats.nativeCommandBufferFinished = true;
        assert(root->validate());
        stats.nativeCommandBufferFinished = false;
        assert(!root->validate());
    }
    auto other = makeCommand();
    assert(root->validateForCommand(other)
        == std::string_view("RHI-METAL-CANONICAL-PRODUCER-COMMAND-MISMATCH"));
    other->abandonUnsubmitted();
    other.reset();
    auto spoof = command->preparedCompletionState();
    spoof->complete(true);
    assert(!root->validate()
        && !BindlessFaultSubmissionLease::acquire(cold->producer()->faultDomain(), f.diagnostic)
            .valid());
    spoof->reset();
    spoof.reset();
    for (std::size_t i = 0u; i < root->buffers().size(); ++i) {
        auto& buffer = const_cast<Prep::Buffers&>(root->buffers())[i];
        const auto saved = buffer;
        buffer = std::shared_ptr<GpuBuffer>(command, buffer.get());
        assert(root->validate());
        buffer = saved;
        auto& allocation = const_cast<ResourceAllocation&>(buffer->allocation());
        const auto reported = allocation.estimatedSizeBytes;
        ++allocation.estimatedSizeBytes;
        assert(root->validate());
        allocation.estimatedSizeBytes = reported;
        auto& desc = const_cast<BufferDesc&>(buffer->desc());
        const auto memory = desc.memory;
        desc.memory = MemoryUsage::CpuToGpu;
        assert(root->validate());
        desc.memory = memory;
        test::MetalNativeIdentityMutationProbe::verify(root->resources()[i].buffer->resource,
            cold->producer()->resources()[0u].buffer->resource,
            [&] {
                assert(root->validate());
            });
        f.registry->unregisterResource(buffer->id());
        assert(root->validate());
        f.registry->registerBuffer(*buffer);
        assert(!root->validate());
        std::array<std::uint8_t, 20u> bytes{};
        assert(f.device->writeBuffer(*buffer, bytes.data(), buffer->desc().sizeBytes).diagnostics
            == "RHI-BUFFER-HOST-WRITES-SEALED");
    }
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate());
            assert(root->validateForCommand({}));
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    assert(!command->stats()->nativeDispatchEncoded && !command->stats()->nativeIndexedDrawEncoded
        && !command->retentionStorage()->resourceOwnerCount());
    command->abandonUnsubmitted();
    assert(root->validate() == std::string_view("RHI-METAL-CANONICAL-PRODUCER-COMMAND-CLOSED"));
    command.reset();
    assert(root->validate() == std::string_view("RHI-METAL-CANONICAL-PRODUCER-ORIGINAL-OWNER"));
    std::weak_ptr<void> token = root->resources()[0u].buffer->resource.allocationReleaseToken;
    root.reset();
    assert(token.expired() && f.device->memoryStats().allocatedBytes == gpu);
    command = makeCommand();
    root = prepare();
    assert(root && !root->validate());
    command->abandonUnsubmitted();
    root.reset();
    command.reset();
    consumerLease.release();
    assert(BindlessFaultSubmissionLease::acquire(cold->producer()->faultDomain(), f.diagnostic)
            .valid());
    assert(!inspectSharedMetalRhiCommandCapacity().charged);
    std::printf(
        "MetalCanonicalProducerRecordingPreparation/1: actual readbacks20+16 newBytes%llu childOwners%zu coldPeak%llu; original command/source/control blocks, exclusive producer and independent consumer leases, exact/one-less CPU/GPU/owners, native substitutions and100 allocation0 PASS; no dispatch/completion\n",
        static_cast<unsigned long long>(Prep::minimumNewBytes),
        required,
        static_cast<unsigned long long>(peak));
}

void canonicalProducerExecution(test::OrdinaryGraphicsFixture& f,
    const std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerColdBindings>& cold,
    bool dropCommand,
    bool dropRoot)
{
    using Prep = MetalRhiCanonicalProducerRecordingPreparation;
    using Contract = Prep::Contract;
    using Status = Prep::Status;
    using Role = rhi::CanonicalZeroBaseIndirectSourceContract::Role;
    Contract::Budget values{
        1u, true, Contract::commandUnits, Contract::readbackBytes, Contract::referenceSlots};
    const auto makeCommand = [&](std::uint32_t owners = 256u) {
        const char* id = nullptr;
        auto retention = common::CommandRetentionStorage::prepare(
            {1u, true, owners, 0u, 0u, 0u, f.cpuBytes}, f.quota, id);
        if (!retention) {
            f.diagnostic = id;
            return std::shared_ptr<MetalRhiNativeCommandPayload>{};
        }
        return createMetalRhiCanonicalProducerNativeCommandPayload(
            values, "CanonicalProducerExecution", f.registry, std::move(retention), f.diagnostic);
    };
    auto command = makeCommand();
    assert(command);
    Prep::Budget budget{values, Prep::minimumNewBytes, 256u};
    const auto prepare = [&] {
        return Prep::prepare(cold, *f.device, command, budget, f.diagnostic);
    };
    test::CpuQuotaFactoryFixture factories(f.quota);
    const auto commandFactory = [&] {
        return makeCommand();
    };
    const auto commandPeak = factories.measure(commandFactory),
               rootPeak = factories.measure(prepare);
    factories.verify(commandPeak, commandFactory);
    factories.verify(rootPeak, prepare);
    assert(f.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-PREPARATION-ALLOCATION-FAILED");
    --values.maxCommands;
    assert(!makeCommand() && f.diagnostic == "RHI-CANONICAL-PRODUCER-RECORDING-BUDGET");
    ++values.maxCommands;
    values.prototypeOptIn = false;
    assert(!makeCommand());
    values.prototypeOptIn = true;
    assert(!command->finish(f.diagnostic)
        && f.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-PREPARATION-ONLY");
    assert(!command->recordComputePassBegin(f.diagnostic)
        && f.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-EXCLUSIVE-COMMAND");
    auto root = prepare();
    assert(root && !root->validate());
    const auto owners = root->childOwnerCount();
    budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    assert(owners
        == 1u + cold->childOwnerCount() + Contract::referenceSlots + 1u
            + 2u * (1u + MetalRhiResourceOwnerProjection::independentBufferOwnerCount) + 4u);
    const auto deny = [&](const auto& selected, const char* expected) {
        assert(!command->recordCanonicalProducer(selected, f.diagnostic));
        if (f.diagnostic != expected)
            std::fprintf(
                stderr, "Native canonical expected %s, got %s\n", expected, f.diagnostic.c_str());
        assert(f.diagnostic == expected && root->status() == Status::Cold
            && !command->stats()->nativeDispatchEncoded
            && !command->retentionStorage()->resourceOwnerCount());
    };
    deny(std::shared_ptr<const Prep>(cold, root.get()),
        "RHI-METAL-CANONICAL-PRODUCER-RECORDING-DOMAIN");
    auto other = makeCommand();
    assert(other && !other->recordCanonicalProducer(root, f.diagnostic));
    other->abandonUnsubmitted();
    other.reset();
    auto& bindings =
        const_cast<MetalRhiZeroBaseIndirectColdBindings::Bindings&>(cold->producer()->bindings());
    const auto savedSlot = bindings[3u].abi.slot;
    bindings[3u].abi.slot = UINT32_MAX;
    const auto* mismatched = root->validate();
    assert(mismatched);
    deny(root, mismatched);
    bindings[3u].abi.slot = savedSlot;
    core::MutationJournal foreign;
    auto& args = *cold->producer()->uploads()[1u].buffer;
    assert(prepareMetalRhiRecordedState(args, foreign));
    deny(root, "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
    foreign.rollback();
    const auto record = [&] {
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const bool result = command->recordCanonicalProducer(root, f.diagnostic);
        if (!result)
            std::fprintf(stderr, "Canonical record: %s\n", f.diagnostic.c_str());
        assert(result);
        assert(root->status() == Status::Recorded
            && command->stats()->canonicalProducerCommandCount == Contract::commandUnits);
        assert(command->stats()->nativeDispatchEncoded && command->stats()->nativeBlitCopyEncoded
            && !command->stats()->nativeIndexedDrawEncoded);
        assert(args.state() == ResourceState::CopySrc
            && cold->producer()->uploads()[2u].buffer->state() == ResourceState::CopySrc
            && cold->producer()->faultDomain()->faultLatchBuffer()->state()
                == ResourceState::CopySrc);
        assert(cold->producer()->packet()->packet()->instanceMappings()->state()
                == ResourceState::ShaderWrite
            && !cold->producer()->packet()->packet()->consumerPlan().success);
        assert(command->finish(f.diagnostic));
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    };
    record();
    const auto retainedOwners = command->retentionStorage()->resourceOwnerCount();
    assert(retainedOwners);
    command->abandonUnsubmitted();
    assert(root->status() == Status::Abandoned && args.state() == ResourceState::ShaderWrite
        && !cold->validate());
    root.reset();
    command.reset();
    command = makeCommand(static_cast<std::uint32_t>(retainedOwners - 1u));
    root = prepare();
    assert(root);
    deny(root, "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
    command->abandonUnsubmitted();
    root.reset();
    command.reset();
    command = makeCommand(static_cast<std::uint32_t>(retainedOwners));
    root = prepare();
    assert(root);
    record();
    auto failedRoot = root;
    command.reset();
    assert(failedRoot->status() == Status::Abandoned && !cold->validate());
    root.reset();
    failedRoot.reset();
    command = makeCommand(static_cast<std::uint32_t>(retainedOwners));
    root = prepare();
    assert(root);
    record();
    MetalRhiNativeQueueStats queue;
    assert(!command->submit(queue, {}, {}, 0u, {}, 0u, f.diagnostic));
    assert(root->status() == Status::Failed && args.state() == ResourceState::ShaderWrite
        && !cold->validate());
    root.reset();
    command.reset();
    command = makeCommand(static_cast<std::uint32_t>(retainedOwners));
    root = prepare();
    assert(root);
    record();
    auto rejectedGate = createMetalRhiNativeSharedEventPayload("CanonicalWaitAllocationFailure");
    assert(rejectedGate);
    auto rejectedDone = command->preparedCompletionState();
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!command->submit(queue, rejectedDone, rejectedGate, 1u, {}, 0u, f.diagnostic));
        std::pmr::set_default_resource(previous);
    }
    assert(f.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED"
        && root->status() == Status::Failed
        && rejectedDone->outcome() == MetalRhiCompletionState::Outcome::Failed
        && !cold->validate());
    root.reset();
    command.reset();
    rejectedGate.reset();
    rejectedDone.reset();
    command = makeCommand(static_cast<std::uint32_t>(retainedOwners));
    root = prepare();
    assert(root);
    const auto& content = cold->producer()->packet()->pipeline()->source()->content();
    Root::Content::Facts oracle;
    assert(!content.observe(oracle));
    const auto expectedArgs = IndexedIndirectCommandSchema::words(oracle.physical);
    const std::array<std::uint32_t, 4u> expectedMapping{oracle.mapping.firstInstance,
        oracle.mapping.instanceCount,
        oracle.mapping.instanceCapacity,
        oracle.mapping.contractVersion};
    // FaultReporting/1 generated latch uses bit0 for every explicit fault.
    const std::uint32_t expectedFault =
        oracle.outcome == Root::Content::Outcome::FaultSuppressed ? 1u : 0u;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i)
            assert(!root->validate());
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    record();
    auto gate = createMetalRhiNativeSharedEventPayload("CanonicalProducerDelayedExecution");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    assert(root->status() == Status::Submitted && !done->isComplete());
    assert(!BindlessFaultSubmissionLease::acquire(cold->producer()->faultDomain(), f.diagnostic)
            .valid());
    auto independent = BindlessFaultSubmissionLease::acquire(cold->faultDomain(), f.diagnostic);
    assert(independent.valid());
    done->complete(true);
    assert(root->status() == Status::Submitted);
    done->reset();
    std::array<std::uint8_t, 20u> pending{};
    pending.fill(0xffu);
    assert(readMetalRhiNativeBuffer(
        cold->producer()->resources()[static_cast<std::size_t>(Role::PhysicalArguments)].buffer,
        pending.data(),
        pending.size(),
        0u,
        pending.size(),
        f.diagnostic));
    for (const auto byte : pending)
        assert(!byte);
    const auto argsReadback = root->resources()[0u].buffer,
               mappingReadback = root->resources()[1u].buffer;
    std::weak_ptr<const Prep> weakRoot = root;
    if (dropRoot)
        root.reset();
    if (dropCommand)
        command.reset();
    assert(!weakRoot.expired() && !done->isComplete());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
    if (dropRoot)
        assert(weakRoot.expired());
    else
        assert(root->status() == Status::Completed);
    const auto read = [&](const auto& native, const auto& expected) {
        std::array<std::uint8_t, 20u> data{};
        const auto bytes = expected.size() * sizeof(std::uint32_t);
        assert(readMetalRhiNativeBuffer(native, data.data(), bytes, 0u, bytes, f.diagnostic));
        core::UnsignedElementSpan words(data.data(),
            bytes,
            core::UnsignedElementSpan::Width::Uint32,
            core::UnsignedElementSpan::ByteOrder::LittleEndian);
        for (std::size_t i = 0u; i < expected.size(); ++i) {
            std::uint32_t word = 0u;
            assert(words.read(i, word) && word == expected[i]);
        }
    };
    read(argsReadback, expectedArgs);
    read(mappingReadback, expectedMapping);
    read(cold->producer()->readback().buffer, std::array<std::uint32_t, 1u>{expectedFault});
    read(cold->producer()->packet()->resources()[0u].buffer, std::array<std::uint32_t, 5u>{});
    read(cold->producer()->packet()->resources()[1u].buffer, std::array<std::uint32_t, 4u>{});
    assert(cold->producer()->faultDomain()->faultLatchPriorState() == ResourceState::CopySrc
        && cold->faultDomain()->faultLatchPriorState() == ResourceState::Undefined);
    assert(BindlessFaultSubmissionLease::acquire(cold->producer()->faultDomain(), f.diagnostic)
            .valid());
    auto retry = makeCommand();
    assert(retry);
    assert(!Prep::prepare(cold, *f.device, retry, budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-CANONICAL-PRODUCER-GENERATION-CONSUMED");
    retry->abandonUnsubmitted();
    retry.reset();
    if (command) {
        assert(!command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
        command.reset();
    }
    root.reset();
    independent.release();
    // A different genuine producer generation proves no-wait submission can
    // complete with an exhausted caller quota, using only cold prepared owners.
    auto siblingProducer = MetalRhiZeroBaseIndirectColdBindings::prepare(cold->producer()->packet(),
        *f.device,
        {1u, true, MetalRhiZeroBaseIndirectColdBindings::Contract::minimumOwnedBytes, 128u},
        f.diagnostic);
    assert(siblingProducer);
    MetalRhiZeroBaseIndirectConsumerColdBindings::ImageIds imageIds;
    for (std::size_t i = 0u; i < imageIds.size(); ++i)
        imageIds[i] = cold->images()[i].values().view;
    auto sibling = MetalRhiZeroBaseIndirectConsumerColdBindings::prepare(siblingProducer,
        cold->pipeline(),
        *f.device,
        imageIds,
        {1u, true, MetalRhiZeroBaseIndirectConsumerColdBindings::Contract::minimumOwnedBytes, 256u},
        f.diagnostic);
    assert(sibling);
    auto noWait = makeCommand(static_cast<std::uint32_t>(retainedOwners));
    auto noWaitRoot = Prep::prepare(sibling, *f.device, noWait, budget, f.diagnostic);
    assert(noWaitRoot);
    auto noWaitDone = noWait->preparedCompletionState();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(noWait->recordCanonicalProducer(noWaitRoot, f.diagnostic)
            && noWait->finish(f.diagnostic)
            && noWait->submit(queue, noWaitDone, {}, 0u, {}, 0u, f.diagnostic));
        std::pmr::set_default_resource(previous);
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    noWaitDone->wait();
    assert(noWaitRoot->status() == Status::Completed
        && noWaitDone->outcome() == MetalRhiCompletionState::Outcome::Completed);
    read(noWaitRoot->resources()[0u].buffer, expectedArgs);
    read(noWaitRoot->resources()[1u].buffer, expectedMapping);
    read(siblingProducer->readback().buffer, std::array<std::uint32_t, 1u>{expectedFault});
    noWait.reset();
    noWaitRoot.reset();
    noWaitDone.reset();
    sibling.reset();
    siblingProducer.reset();
    assert(!inspectSharedMetalRhiCommandCapacity().charged);
    std::printf(
        "MetalCanonicalProducerRecording/1: GPU args[%u,%u,%u,%u,%u] map[%u,%u,%u,%u] fault%u exact;15 operations, childOwners%zu retainedUnique%zu CPU commandPeak%llu rootPeak%llu; allocation0 record/finish/no-wait submit, delayed actual completion dropCommand%d dropRoot%d, CPU spoof, rollback/abandonment and terminal generation PASS; common packet untouched, no draw\n",
        expectedArgs[0],
        expectedArgs[1],
        expectedArgs[2],
        expectedArgs[3],
        expectedArgs[4],
        expectedMapping[0],
        expectedMapping[1],
        expectedMapping[2],
        expectedMapping[3],
        expectedFault,
        owners,
        retainedOwners,
        static_cast<unsigned long long>(commandPeak),
        static_cast<unsigned long long>(rootPeak),
        dropCommand,
        dropRoot);
}

void consumerColdPrerequisite(test::OrdinaryGraphicsFixture& f,
    const std::shared_ptr<const Root>& source,
    const char* publication,
    const char* nativePublication,
    bool recording = false,
    bool execution = false,
    bool dropCommand = false,
    bool dropRoot = false)
{
    using Cold = MetalRhiZeroBaseIndirectConsumerColdBindings;
    using Packet = MetalRhiZeroBaseIndirectPacketStorage;
    using Producer = MetalRhiZeroBaseIndirectColdBindings;
    static_assert(!std::is_default_constructible_v<Cold> && !std::is_copy_constructible_v<Cold>);
    auto producerPipeline = MetalRhiZeroBaseIndirectPipeline::prepare(source,
        semantics::nativeBackendCapabilityContract(Backend::Metal),
        f.budget(),
        {1u, true, 128u},
        f.diagnostic);
    if (source->package()->artifact().execution == BindlessExecution::Native) {
        assert(!producerPipeline
            && f.diagnostic == "RHI-METAL-ZERO-BASE-PIPELINE-SCALAR-PACKAGE-REQUIRED");
        return;
    }
    assert(producerPipeline);
    core::CpuMemoryQuotaScope scope(f.quota);
    auto pool = core::makeUnique<BindlessIndirectPacketPool>(*f.device,
        BindlessIndirectPacketBudget{
            1u, Packet::Contract::minimumBytes, Packet::Contract::minimumBytes});
    auto packet = Packet::prepare(
        producerPipeline, *pool, {1u, true, Packet::Contract::minimumBytes, 128u}, f.diagnostic);
    assert(packet);
    auto producer = Producer::prepare(
        packet, *f.device, {1u, true, Producer::Contract::minimumOwnedBytes, 128u}, f.diagnostic);
    assert(producer);
    test::MetalClosedConsumerFixture fixture(f, publication, nativePublication);
    auto pipeline = fixture.prepare(source);
    assert(pipeline);
    std::array<core::UniquePtr<GpuTexture>, 2u> textures;
    std::array<core::UniquePtr<GpuTextureView>, 2u> views;
    Cold::ImageIds ids{f.view->id(), {}, {}};
    for (std::size_t i = 0u; i < textures.size(); ++i) {
        textures[i] = f.device->createTexture({"ColdImage",
            {2u, 2u, 1u},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead,
            2u,
            1u,
            1u});
        assert(textures[i]);
        TextureViewDesc desc{"ColdImageMip", TextureFormat::Rgba8Unorm, TextureUsage::Sampled};
        desc.baseMipLevel = static_cast<std::uint32_t>(i);
        views[i] = f.device->createTextureView(*textures[i], desc);
        assert(views[i]);
        ids[i + 1u] = views[i]->id();
    }
    Cold::Budget budget{1u, true, Cold::Contract::minimumOwnedBytes, 256u};
    auto selectedProducer = producer;
    auto selectedPipeline = pipeline;
    const auto prepare = [&] {
        return Cold::prepare(
            selectedProducer, selectedPipeline, *f.device, ids, budget, f.diagnostic);
    };
    const auto reject = [&](const char* expected) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto failed = prepare();
        if (failed || f.diagnostic != expected)
            std::fprintf(stderr, "Consumer cold expected %s: %s\n", expected, f.diagnostic.c_str());
        assert(!failed && f.diagnostic == expected && f.quota->liveBytes() == cpu
            && f.device->memoryStats().allocatedBytes == gpu);
    };
    budget.prototypeOptIn = false;
    reject("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BUDGET");
    budget.prototypeOptIn = true;
    --budget.maxOwnedBytes;
    reject("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-BYTE-BUDGET");
    ++budget.maxOwnedBytes;
    selectedProducer = std::shared_ptr<const Producer>(pipeline, producer.get());
    reject("RHI-METAL-CONSUMER-COLD-OWNER-MISMATCH");
    selectedProducer = producer;
    selectedPipeline = std::shared_ptr<const Cold::Pipeline>(producer, pipeline.get());
    reject("RHI-METAL-CONSUMER-COLD-OWNER-MISMATCH");
    selectedPipeline = pipeline;
    const auto savedId = ids[0u];
    ids[0u] = {};
    reject("RHI-METAL-TEXTURE-INVENTORY-VIEW-NOT-LIVE");
    ids[0u] = savedId;
    auto foreign = createMetalRhiDevice(test::OrdinaryGraphicsFixture::deviceDesc());
    assert(foreign);
    assert(!Cold::prepare(producer, pipeline, *foreign, ids, budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-CONSUMER-COLD-ISSUER-MISMATCH");
    foreign.reset();
    {
        const auto& content = source->content();
        Root::Content::Arguments::Source original{content.sourceBytes().data(),
            content.sourceBytes().size(),
            std::uint64_t{content.parameters().sourceWordOffset} * sizeof(std::uint32_t),
            1u,
            sizeof(DrawIndexedIndirectCommand),
            core::UnsignedElementSpan::ByteOrder::LittleEndian,
            Root::Content::Arguments::Origin::HostAuthoredImmutable,
            1u};
        auto differentSource = Root::prepare(source->package(),
            source->index(),
            *f.device,
            original,
            content.parameters(),
            content.consumer(),
            test::ZeroBaseIndirectSourceFixture::budget(),
            {1u, true, 32u, 16u, 128u},
            f.diagnostic);
        assert(differentSource);
        selectedPipeline = fixture.prepare(differentSource);
        assert(selectedPipeline);
        reject("RHI-METAL-CONSUMER-COLD-ORIGINAL-SOURCE");
        selectedPipeline = pipeline;
    }
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Consumer cold: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    const auto owners = root->childOwnerCount();
    budget.maxChildOwners = static_cast<std::uint32_t>(owners - 1u);
    reject("RHI-CANONICAL-CONSUMER-COLD-RESOURCES-OWNER-BUDGET");
    budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    // Each stage measures a fresh quota history. Earlier exhausted probes set
    // the historical peak to the limit and cannot serve a later measurement.
    root.reset();
    std::uint64_t peak = 0u;
    if (!recording && !execution) {
        test::CpuQuotaFactoryFixture factory(f.quota);
        peak = factory.measure(prepare);
        factory.verify(peak, prepare);
        assert(f.diagnostic == "RHI-METAL-CONSUMER-COLD-ALLOCATION-FAILED");
    }
    root = prepare();
    assert(root && !root->validate());
    if (execution) {
        canonicalProducerExecution(f, root, dropCommand, dropRoot);
        return;
    }
    if (recording)
        canonicalRecordingPrerequisite(f, root);
    assert(root->ownedBytes() == 52u
        && root->facts().mapping == packet->packet()->instanceMappings()->id());
    assert(root->facts().consumerFaultGeneration == root->faultDomain()->generation()
        && root->facts().consumerFaultGeneration != producer->generation());
    assert(packet->packet()->instanceMappings()->state() == ResourceState::ShaderWrite
        && !packet->packet()->consumerPlan().success);
    const auto& native = root->metadata().resource.buffer;
    const auto buffer = reinterpret_cast<id<MTLBuffer>>(native->resource.nativeObject.get());
    assert(buffer.length == sizeof(Cold::Contract::Bytes));
    Cold::Contract::Bytes bytes{};
    assert(
        readMetalRhiNativeBuffer(native, bytes.data(), bytes.size(), 0u, bytes.size(), f.diagnostic)
        && bytes == root->facts().bytes);
    assert(f.device->writeBuffer(*root->metadata().buffer, bytes.data(), bytes.size()).diagnostics
        == "RHI-BUFFER-HOST-WRITES-SEALED");
    @autoreleasepool {
        auto encoder = f.device->createCommandEncoder({"DeniedConsumerMetadataWrite"});
        auto transfer = encoder->beginTransferPass({"Denied"});
        assert(transfer
                   ->copyBufferToBuffer(
                       {root->metadata().buffer->id(), root->metadata().buffer->id(), 0u, 0u, 4u})
                   .diagnostics
            == "RHI-BUFFER-GPU-WRITES-SEALED");
        assert(transfer->end().success);
    }
    assert(!inspectSharedMetalRhiCommandCapacity().charged);
    auto alternate = prepare();
    assert(
        alternate && alternate->faultDomain()->generation() != root->faultDomain()->generation());
    {
        const auto saved = ids[1u];
        ids[1u] = ids[0u];
        auto repeated = prepare();
        assert(repeated && !repeated->validate());
        ids[1u] = saved;
    }
    const auto mutate = [&](auto& field, auto replacement) {
        const auto saved = field;
        field = replacement;
        assert(root->validate());
        field = saved;
        assert(!root->validate());
    };
    for (auto& binding : const_cast<Cold::Buffers&>(root->buffers())) {
        mutate(binding.resource, GpuResourceId{});
        mutate(binding.binding, UINT32_MAX);
        mutate(binding.range.offset, std::uint64_t{4u});
        mutate(binding.range.fact.slot, UINT32_MAX);
        mutate(binding.range.fact.alignment, std::uint64_t{0u});
        mutate(binding.range.stage, ShaderStage::Compute);
    }
    for (auto& binding : const_cast<Cold::Textures&>(root->textures())) {
        mutate(binding.resource, GpuResourceId{});
        mutate(binding.binding, UINT32_MAX);
        mutate(binding.abi.slot, UINT32_MAX);
        mutate(binding.abi.stage, ShaderStage::Vertex);
    }
    auto& facts = const_cast<Cold::Contract::Facts&>(root->facts());
    mutate(facts.consumerFaultGeneration, producer->generation());
    mutate(facts.words[2u], facts.words[2u] + 1u);
    mutate(facts.bytes[0u], static_cast<std::uint8_t>(0u));
    mutate(facts.images[0u].parent, GpuResourceId{});
    for (std::size_t i = 0u; i < root->resources().size(); ++i) {
        auto& resources = const_cast<Cold::Resources&>(root->resources());
        const auto saved = resources[i];
        if (i) {
            resources[i] = alternate->resources()[i];
            assert(root->validate());
            resources[i] = saved;
        }
        test::MetalNativeIdentityMutationProbe::verify(resources[i].buffer->resource,
            alternate->resources()[i ? i : 1u].buffer->resource,
            [&] {
                assert(root->validate());
            });
    }
    auto& fault = const_cast<std::shared_ptr<BindlessOwnedFaultDomain>&>(root->faultDomain());
    const auto originalFault = fault;
    fault = std::shared_ptr<BindlessOwnedFaultDomain>(producer, fault.get());
    assert(root->validate());
    fault = originalFault;
    fault = producer->faultDomain();
    assert(root->validate());
    fault = originalFault;
    fault = alternate->faultDomain();
    assert(root->validate());
    fault = originalFault;
    test::MetalNativeIdentityMutationProbe::verify(
        root->readback().buffer->resource, alternate->readback().buffer->resource, [&] {
            assert(root->validate());
        });
    auto& metadataDesc = const_cast<BufferDesc&>(root->metadata().buffer->desc());
    const auto savedDesc = metadataDesc;
    --metadataDesc.sizeBytes;
    assert(root->validate());
    metadataDesc = savedDesc;
    auto& allocation = const_cast<ResourceAllocation&>(root->metadata().buffer->allocation());
    mutate(allocation.estimatedSizeBytes, allocation.estimatedSizeBytes + 1u);
    f.registry->unregisterResource(root->metadata().buffer->id());
    assert(root->validate());
    f.registry->registerBuffer(*root->metadata().buffer);
    assert(!root->validate());
    ResourceBarrierDesc barrier;
    barrier.resource = root->metadata().buffer->id();
    barrier.before = ResourceState::ShaderRead;
    barrier.after = ResourceState::CopySrc;
    assert(setMetalRhiBufferStateForBarrier(*root->metadata().buffer, barrier).success
        && root->validate());
    std::swap(barrier.before, barrier.after);
    assert(setMetalRhiBufferStateForBarrier(*root->metadata().buffer, barrier).success
        && !root->validate());
    {
        auto producerLease =
            BindlessFaultSubmissionLease::acquire(producer->faultDomain(), f.diagnostic);
        auto consumerLease =
            BindlessFaultSubmissionLease::acquire(root->faultDomain(), f.diagnostic);
        assert(producerLease.valid() && consumerLease.valid() && !root->validate());
        assert(!BindlessFaultSubmissionLease::acquire(root->faultDomain(), f.diagnostic).valid());
        // Terminal marking is a hostile admission probe, never a GPU completion claim.
        auto terminal =
            BindlessFaultSubmissionLease::acquire(alternate->faultDomain(), f.diagnostic);
        assert(terminal.valid());
        terminal.markFaultReadbackCompleted();
        assert(alternate->validate());
    }
    alternate.reset();
    const auto gpu = f.device->memoryStats().allocatedBytes,
               capacity = f.device->memoryStats().budgetCapacityBytes;
    for (const auto remaining :
        {Cold::Contract::minimumOwnedBytes, Cold::Contract::minimumOwnedBytes - 1u}) {
        auto pressure = f.device->createBuffer({"ConsumerColdGpuBudget",
            capacity - gpu - remaining,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(pressure);
        auto candidate = prepare();
        if (remaining == Cold::Contract::minimumOwnedBytes)
            assert(candidate && f.device->memoryStats().allocatedBytes == capacity);
        else
            assert(!candidate && f.diagnostic == "RHI-METAL-CONSUMER-COLD-BUFFER-CREATION-FAILED");
        candidate.reset();
        pressure.reset();
        assert(f.device->memoryStats().allocatedBytes == gpu);
    }
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate());
            ++facts.words[3u];
            assert(root->validate());
            --facts.words[3u];
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    std::weak_ptr<const BindlessIndirectPacket> originalPacket = packet->packet();
    std::weak_ptr<void> ownedToken =
        root->metadata().resource.buffer->resource.allocationReleaseToken;
    const auto ordered = ids;
    ids.fill(f.view->id());
    auto issuerRoot = prepare();
    assert(issuerRoot && !issuerRoot->validate());
    ids = ordered;
    f.set.reset();
    f.arena.reset();
    pool.reset();
    packet.reset();
    producer.reset();
    selectedProducer.reset();
    producerPipeline.reset();
    pipeline.reset();
    selectedPipeline.reset();
    assert(!root->validate() && !originalPacket.expired());
    // The root keeps native storage alive while logical parent/view retirement
    // closes admission through weak tickets before any wrapper dereference.
    const auto retained = f.device->memoryStats().allocatedBytes;
    views[0u].reset();
    textures[0u].reset();
    assert(root->validate() && f.device->memoryStats().allocatedBytes == retained);
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i)
            assert(root->validate());
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    if (!recording)
        std::printf(
            "MetalClosedConsumerColdResources/1: original mapping16 ShaderWrite, metadata44 fault4+4, three actual images, owned%llu childOwners%zu coldPeak%llu; sealed bytes, independent leases, exact/one-less CPU/GPU/owners, retirement and100 allocation0 PASS; no snapshot/plan/execution\n",
            static_cast<unsigned long long>(root->ownedBytes()),
            owners,
            static_cast<unsigned long long>(peak));
    root.reset();
    assert(!originalPacket.expired() && ownedToken.expired() && !issuerRoot->validate());
    f.device.reset();
    const auto* stale = issuerRoot->validate();
    assert(stale && !std::strcmp(stale, "RHI-BINDLESS-INDIRECT-PACKET-STALE"));
    issuerRoot.reset();
    assert(originalPacket.expired());
}

std::shared_ptr<core::CpuMemoryQuota> run(char** argv,
    const char* publication,
    core::UnsignedElementSpan::ByteOrder order,
    bool fault,
    unsigned preparationStage)
{
    test::MetalIndirectExecutionFixture native(argv[1], argv[2], argv[3]);
    auto& f = native.vertex.pass.graphics;
    test::ZeroBaseIndirectSourceFixture values(order);
    if (preparationStage == 7u && !fault
        && order == core::UnsignedElementSpan::ByteOrder::BigEndian)
        values.put({2u, 1u, 2u, -2, 2u});
    if (fault)
        values.put({UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
    std::shared_ptr<const BindlessShaderPackage> package;
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        package = BindlessShaderPackage::load(
            publication, {f.cpuBytes, f.cpuBytes * 4u, 64u, 8u, 32u, 8u}, f.diagnostic);
    }
    if (!package)
        std::fprintf(stderr, "Package: %s\n", f.diagnostic.c_str());
    assert(package && package->sharedOwnerMatches(package));
    auto index = native.index;
    Root::Budget budget{
        1u, true, values.source.size(), sizeof(ZeroBaseIndirectProducerParams), 128u};
    const auto prepare = [&] {
        return Root::prepare(package,
            index,
            *f.device,
            values.input(),
            values.parameters,
            values.consumer,
            values.budget(),
            budget,
            f.diagnostic);
    };
    const auto reject = [&](const char* id) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto denied = prepare();
        if (denied || f.diagnostic != id || f.quota->liveBytes() != cpu
            || f.device->memoryStats().allocatedBytes != gpu)
            std::fprintf(stderr,
                "Expected %s: %s CPU%llu->%llu GPU%llu->%llu\n",
                id,
                f.diagnostic.c_str(),
                static_cast<unsigned long long>(cpu),
                static_cast<unsigned long long>(f.quota->liveBytes()),
                static_cast<unsigned long long>(gpu),
                static_cast<unsigned long long>(f.device->memoryStats().allocatedBytes));
        assert(!denied && f.diagnostic == id && f.quota->liveBytes() == cpu
            && f.device->memoryStats().allocatedBytes == gpu);
    };
    const auto originalBudget = budget;
    budget.prototypeOptIn = false;
    reject("RHI-METAL-ZERO-BASE-SOURCE-PREPARATION-BUDGET");
    budget = originalBudget;
    --budget.maxSourceBytes;
    reject("RHI-METAL-ZERO-BASE-SOURCE-BYTE-BUDGET");
    budget = originalBudget;
    --budget.maxParameterBytes;
    reject("RHI-METAL-ZERO-BASE-SOURCE-BYTE-BUDGET");
    budget = originalBudget;
    const auto originalPackage = package;
    package = std::shared_ptr<const BindlessShaderPackage>(index, package.get());
    reject("RHI-METAL-ZERO-BASE-SOURCE-OWNER-MISMATCH");
    package = originalPackage;
    const auto originalIndex = index;
    index = std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>(package, index.get());
    reject("RHI-METAL-ZERO-BASE-SOURCE-OWNER-MISMATCH");
    index = originalIndex;
    auto foreign = createMetalRhiDevice(test::OrdinaryGraphicsFixture::deviceDesc());
    assert(foreign);
    assert(!Root::prepare(package,
               index,
               *foreign,
               values.input(),
               values.parameters,
               values.consumer,
               values.budget(),
               budget,
               f.diagnostic)
        && f.diagnostic == "RHI-METAL-ZERO-BASE-SOURCE-ISSUER-MISMATCH");
    foreign.reset();
    values.parameters.reserved = 1u;
    reject("RHI-ZERO-BASE-SOURCE-PARAMETERS");
    values.parameters.reserved = 0u;
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Source: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    const auto owners = root->childOwnerCount();
    assert(owners);
    budget.maxChildOwners = static_cast<std::uint32_t>(owners - 1u);
    reject("RHI-METAL-ZERO-BASE-SOURCE-CHILD-OWNER-BUDGET");
    budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    // Each factory owns a fresh quota history. Exhausted probes intentionally
    // raise its high water to the limit, so a later phase cannot measure there.
    if (preparationStage) {
        if (preparationStage >= 8u)
            consumerGeometryPrerequisite(f,
                root,
                argv[8],
                argv[7],
                order,
                preparationStage >= 9u,
                preparationStage >= 10u,
                preparationStage >= 11u ? preparationStage - 10u : 0u);
        else if (preparationStage >= 5u)
            consumerColdPrerequisite(f,
                root,
                argv[8],
                argv[7],
                preparationStage == 6u,
                preparationStage == 7u,
                fault,
                order == core::UnsignedElementSpan::ByteOrder::BigEndian);
        else if (preparationStage == 4u)
            consumerPipelinePrerequisite(f, root, argv[8], argv[7]);
        else if (preparationStage == 1u)
            pipelinePrerequisite(f, root);
        else
            packetPrerequisite(f, root, argv[6], preparationStage == 3u);
        return f.quota;
    }
    root.reset();
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-ZERO-BASE-SOURCE-PREPARATION-ALLOCATION-FAILED");
    root = prepare();
    assert(root && !root->validate());
    const auto gpu = f.device->memoryStats().allocatedBytes,
               capacity = f.device->memoryStats().budgetCapacityBytes;
    const auto required = values.source.size() + sizeof(ZeroBaseIndirectProducerParams);
    for (const auto remaining : {required, required - 1u}) {
        auto pressure = f.device->createBuffer({"ExactSourceGpuBudget",
            capacity - gpu - remaining,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(pressure);
        auto candidate = prepare();
        if (remaining == required)
            assert(candidate && f.device->memoryStats().allocatedBytes == capacity);
        else
            assert(
                !candidate && f.diagnostic == "RHI-METAL-ZERO-BASE-SOURCE-BUFFER-CREATION-FAILED");
        candidate.reset();
        pressure.reset();
        assert(f.device->memoryStats().allocatedBytes == gpu);
    }
    std::array<std::uint8_t, 32u> actual{};
    assert(readMetalRhiNativeBuffer(
        root->sourceBuffer(), actual.data(), actual.size(), 0u, actual.size(), f.diagnostic));
    assert(std::equal(actual.begin(), actual.end(), root->content().sourceBytes().begin()));
    std::array<std::uint8_t, 16u> params{};
    assert(readMetalRhiNativeBuffer(
        root->parameterBuffer(), params.data(), params.size(), 0u, params.size(), f.diagnostic));
    const core::UnsignedElementSpan words(params.data(),
        params.size(),
        core::UnsignedElementSpan::Width::Uint32,
        core::UnsignedElementSpan::ByteOrder::LittleEndian);
    const std::array<std::uint32_t, 4u> expected{2u, 3u, 0u, 1u};
    for (std::size_t i = 0u; i < expected.size(); ++i) {
        std::uint32_t value = 0u;
        assert(words.read(i, value) && value == expected[i]);
    }
    values.source.fill(0u);
    values.index.fill(0u);
    assert(!root->validate() && root->content().sourceBytes()[0] == 0x78u);
    ZeroBaseIndirectSourceConsumptionContract::Facts observed;
    assert(!root->content().observe(observed)
        && observed.outcome
            == (fault ? ZeroBaseIndirectSourceConsumptionContract::Outcome::FaultSuppressed
                      : ZeroBaseIndirectSourceConsumptionContract::Outcome::Produced));
    // Restore caller data solely for alternate preparation probes.
    values = test::ZeroBaseIndirectSourceFixture(order);
    if (fault)
        values.put({UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
    auto alternate = prepare();
    assert(alternate);
    for (const auto* resource : {&root->sourceResource(), &root->parameterResource()}) {
        assert(resource->hostWritesSealed() && resource->gpuWritesSealed()
            && resource->gpuTransferWritesSealed());
        auto& writable = const_cast<GpuBuffer&>(*resource);
        const auto denied = f.device->writeBuffer(writable, params.data(), params.size());
        assert(!denied.success && denied.diagnostics == "RHI-BUFFER-HOST-WRITES-SEALED");
        auto encoder = f.device->createCommandEncoder({"NoSealedSourceTransfer"});
        assert(encoder);
        auto transfer = encoder->beginTransferPass({"Denied"});
        assert(transfer);
        const auto copy = transfer->copyBufferToBuffer(
            {alternate->sourceResource().id(), resource->id(), 0u, 0u, 4u});
        assert(!copy.success && copy.diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
        assert(transfer->end().success);
        auto& desc = const_cast<BufferDesc&>(resource->desc());
        const auto original = desc;
        --desc.sizeBytes;
        assert(root->validate());
        desc = original;
        desc.memory = MemoryUsage::GpuOnly;
        assert(root->validate());
        desc = original;
        desc.usage = ResourceUsage::CopyDst;
        assert(root->validate());
        desc = original;
        f.registry->unregisterResource(resource->id());
        assert(root->validate());
        f.registry->registerBuffer(writable);
        ResourceBarrierDesc transition;
        transition.resource = resource->id();
        transition.before = ResourceState::ShaderRead;
        transition.after = ResourceState::CopySrc;
        assert(setMetalRhiBufferStateForBarrier(writable, transition).success && root->validate());
        transition.before = ResourceState::CopySrc;
        transition.after = ResourceState::ShaderRead;
        assert(setMetalRhiBufferStateForBarrier(writable, transition).success && !root->validate());
    }
    auto arena = f.device->createDescriptorArena({"NoSourceWrite", 1u, 1u});
    assert(arena);
    DescriptorLayoutDesc layout;
    layout.name = "NoSourceWrite";
    layout.bindings = {{4u, DescriptorType::StorageBuffer, ShaderStage::Compute}};
    auto descriptor = arena->allocateDescriptorSet({"NoSourceWrite", layout});
    assert(descriptor);
    auto write = descriptor->write({4u,
        DescriptorType::StorageBuffer,
        root->sourceResource().id(),
        DescriptorResourceKind::Buffer});
    assert(!write.success && write.diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
    descriptor.reset();
    arena.reset();
    for (const auto& buffer : {root->sourceBuffer(), root->parameterBuffer()}) {
        test::MetalNativeIdentityMutationProbe::verify(
            buffer->resource, alternate->sourceBuffer()->resource, [&] {
                const auto* id = root->validate();
                assert(
                    id && !std::strcmp(id, "RHI-METAL-ZERO-BASE-SOURCE-NATIVE-IDENTITY-MISMATCH"));
            });
        auto& native = buffer->resource;
        native.hazardsExplicitlyTracked = false;
        assert(root->validate());
        native.hazardsExplicitlyTracked = true;
        const auto storage = native.storageMode;
        native.storageMode = MetalRhiNativeStorageMode::Private;
        assert(root->validate());
        native.storageMode = storage;
    }
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!root->validate());
            for (std::size_t j = 0u; j < owners; ++j)
                assert(root->childOwnerAt(j));
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    assert(!semantics::ZeroBaseIndirectSourceLowering::current(Backend::Metal).executable);
    assert(!f.device->capabilities().commandRetention.contractVersion);
    std::weak_ptr<void> object = root->sourceBuffer()->resource.nativeObject,
                        token = root->sourceBuffer()->resource.allocationReleaseToken;
    alternate.reset();
    package.reset();
    index.reset();
    root->sourceBuffer()->resource.nativeObject.reset();
    root->sourceBuffer()->resource.allocationReleaseToken.reset();
    assert(root->validate() && !object.expired() && !token.expired());
    std::printf(
        "Canonical source %s order%s fault%d: native32+16 bytes, childOwners%zu coldPeak%llu; sealed writes, exact/one-less CPU/GPU/owners, issuer/identity/state negatives, allocation0 PASS; no dispatch authority\n",
        publication,
        order == core::UnsignedElementSpan::ByteOrder::LittleEndian ? "Little" : "Big",
        fault,
        owners,
        static_cast<unsigned long long>(peak));
    root.reset();
    assert(object.expired() && token.expired());
    return f.quota;
}
} // namespace

int main(int argc, char** argv)
{
    assert((argc == 9 || argc == 10) && metalRhiNativeDeviceHandle());
    const bool consumerOnly = argc == 10 && !std::strcmp(argv[9], "closed-consumer-only");
    const bool coldOnly = argc == 10 && !std::strcmp(argv[9], "closed-consumer-cold-only");
    const bool executionOnly = argc == 10 && !std::strcmp(argv[9], "canonical-execution-only");
    const bool recordingOnly = argc == 10 && !std::strcmp(argv[9], "canonical-recording-only");
    const bool geometryOnly = argc == 10 && !std::strcmp(argv[9], "closed-consumer-geometry-only");
    const bool imagesOnly = argc == 10 && !std::strcmp(argv[9], "closed-consumer-images-only");
    const bool issuanceOnly = argc == 10 && !std::strcmp(argv[9], "closed-consumer-issuance-only");
    const bool nativePreparationOnly =
        argc == 10 && !std::strcmp(argv[9], "closed-consumer-native-preparation-only");
    const bool storageObservationOnly =
        argc == 10 && !std::strcmp(argv[9], "closed-consumer-storage-observation-only");
    const bool nativeExecutionOnly =
        argc == 10 && !std::strcmp(argv[9], "closed-consumer-native-execution-only");
    const bool nativeRecordingOnly =
        argc == 10 && !std::strcmp(argv[9], "closed-consumer-native-recording-only");
    const bool recordingReservationOnly =
        argc == 10 && !std::strcmp(argv[9], "closed-consumer-recording-reservation-only");
    const bool recordingPreparationOnly =
        argc == 10 && !std::strcmp(argv[9], "closed-consumer-recording-preparation-only");
    const bool recordingInputsOnly =
        argc == 10 && !std::strcmp(argv[9], "closed-consumer-recording-inputs-only");
    const unsigned firstStage = nativeExecutionOnly ? 17u
        : nativeRecordingOnly                       ? 16u
        : storageObservationOnly                    ? 15u
        : nativePreparationOnly                     ? 14u
        : recordingReservationOnly                  ? 13u
        : recordingPreparationOnly                  ? 12u
        : recordingInputsOnly                       ? 11u
        : issuanceOnly                              ? 10u
        : imagesOnly                                ? 9u
        : geometryOnly                              ? 8u
        : executionOnly                             ? 7u
        : recordingOnly                             ? 6u
        : coldOnly                                  ? 5u
        : consumerOnly                              ? 4u
                                                    : 0u;
    const unsigned endStage = firstStage ? firstStage + 1u : 4u;
    for (int mode = 4; mode <= 5; ++mode)
        for (const auto order : {core::UnsignedElementSpan::ByteOrder::LittleEndian,
                 core::UnsignedElementSpan::ByteOrder::BigEndian})
            for (const bool fault : {false, true})
                for (unsigned preparationStage = firstStage; preparationStage < endStage;
                    ++preparationStage) {
                    std::shared_ptr<core::CpuMemoryQuota> quota;
                    @autoreleasepool {
                        quota = run(argv, argv[mode], order, fault, preparationStage);
                    }
                    if (quota->liveBytes())
                        std::fprintf(stderr,
                            "Source fixture unreleased CPU bytes%llu stage%u\n",
                            static_cast<unsigned long long>(quota->liveBytes()),
                            preparationStage);
                    assert(!quota->liveBytes());
                }
    if (executionOnly)
        std::puts(
            "MetalCanonicalProducerRecording/1: private Prototype real source-derived dispatch/readback/completion; no draw, public versions/capture gates unchanged");
    else if (nativeExecutionOnly)
        std::puts(
            "ClosedConsumerNativeCompletion/1: genuine Metal private native38 execution/completion; public versions/capture gates unchanged");
    else if (nativeRecordingOnly)
        std::puts(
            "ClosedConsumerNativeRecording/1: real original private native38 encoded; submit/completion/public versions/capture gates unchanged");
    else
        std::puts(
            "CanonicalZeroBaseSourcePreparation/1: Metal real cold preparation only; public versions/execution/capture gates unchanged");
}
