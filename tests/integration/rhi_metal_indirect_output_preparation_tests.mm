#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/metal_indirect_execution_fixture.h"
#include "../fixtures/metal_indirect_producer_pipeline_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef NDEBUG
#error Protected output tests require assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
using Root = MetalRhiIndirectOutputPreparation;
static_assert(!std::is_copy_constructible_v<Root>);
using Fixture = test::MetalIndirectExecutionFixture;

void recordingLeases(Fixture& s, const std::shared_ptr<const Root>& root)
{
    using Lease = MetalRhiIndirectOutputRecordingLease;
    static_assert(
        !std::is_copy_constructible_v<Lease> && std::is_nothrow_move_constructible_v<Lease>);
    auto& f = s.vertex.pass.graphics;
    auto command = s.vertex.pass.command(128u), other = s.vertex.pass.command(128u);
    auto alternative = s.prepare();
    assert(alternative);
    assert(root->generation() && root->generation() != alternative->generation());
    assert(
        command->recordingIdentity() && command->recordingIdentity() != other->recordingIdentity());
    const char* diagnostic = nullptr;
    auto alias = std::shared_ptr<MetalRhiNativeCommandPayload>(other, command.get());
    assert(!Lease::acquire(root, alias, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-METAL-INDIRECT-OUTPUT-RECORDING-OWNER-MISMATCH"));
    alias.reset();
    auto outputAlias = std::shared_ptr<const Root>(alternative, root.get());
    assert(!Lease::acquire(outputAlias, command, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-METAL-INDIRECT-OUTPUT-RECORDING-OWNER-MISMATCH"));
    outputAlias.reset();
    auto direct = core::makeShared<MetalRhiNativeCommandPayload>(
        command->stats(), std::shared_ptr<void>{}, std::shared_ptr<void>{}, f.registry);
    assert(!direct->recordingIdentity() && !Lease::acquire(root, direct, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-METAL-INDIRECT-OUTPUT-RECORDING-CLOSED"));
    direct.reset();
    assert(!Lease::acquire({}, command, diagnostic).valid());
    assert(!Lease::acquire(root, {}, diagnostic).valid() && !root->leasedRecordings());
    auto lease = Lease::acquire(root, command, diagnostic);
    assert(lease.valid() && !diagnostic && root->leasedRecordings() == 1u);
    assert(!lease.validateForRecord(*root, *command, root->generation()));
    assert(lease.validateForRecord(*root, *command, 0u));
    assert(lease.validateForRecord(*root, *command, alternative->generation()));
    assert(lease.validateForRecord(*alternative, *command, root->generation()));
    assert(lease.validateForRecord(*root, *other, root->generation()));
    assert(!Lease::acquire(root, command, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-METAL-INDIRECT-OUTPUT-IN-FLIGHT"));
    assert(!Lease::acquire(root, other, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-METAL-INDIRECT-OUTPUT-IN-FLIGHT"));
    auto moved = std::move(lease);
    assert(!lease.valid() && moved.valid() && root->leasedRecordings() == 1u);
    auto target = Lease::acquire(alternative, command, diagnostic);
    assert(target.valid() && alternative->leasedRecordings() == 1u);
    target = std::move(moved);
    assert(!moved.valid() && target.valid() && !alternative->leasedRecordings()
        && root->leasedRecordings() == 1u);
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 1000u; ++i) {
            assert(!target.validateForRecord(*root, *command, root->generation()));
            assert(!Lease::acquire(root, other, diagnostic).valid());
            auto temporary = Lease::acquire(alternative, other, diagnostic);
            assert(temporary.valid());
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    assert(command->finish(f.diagnostic));
    assert(target.validateForRecord(*root, *command, root->generation()));
    assert(!Lease::acquire(alternative, command, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-METAL-INDIRECT-OUTPUT-RECORDING-CLOSED"));
    command->abandonUnsubmitted();
    std::weak_ptr<MetalRhiNativeCommandPayload> observed = command;
    command.reset();
    assert(observed.expired() && target.valid() && root->leasedRecordings() == 1u);
    assert(target.validateForRecord(*root, *other, root->generation()));
    target = Lease{};
    assert(!root->leasedRecordings());
    auto retry = Lease::acquire(root, other, diagnostic);
    assert(retry.valid());
    other->abandonUnsubmitted();
    assert(retry.validateForRecord(*root, *other, root->generation()));
    retry = Lease{};
    assert(!root->leasedRecordings());
    const auto makeForeign = [&](std::shared_ptr<core::CpuMemoryQuota> quota,
                                 std::shared_ptr<MetalRhiResourceRegistry> registry) {
        const char* id = nullptr;
        auto retention = common::CommandRetentionStorage::prepare(
            {1u, true, 128u, 0u, 0u, 0u, f.cpuBytes}, quota, id);
        assert(retention);
        return createMetalRhiNativeCommandPayload(
            "ForeignLease", registry, &f.diagnostic, 0u, 0u, 0u, std::move(retention));
    };
    auto foreign = makeForeign(core::makeShared<core::CpuMemoryQuota>(f.cpuBytes), f.registry);
    assert(foreign);
    assert(!Lease::acquire(root, foreign, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH"));
    foreign->abandonUnsubmitted();
    foreign = makeForeign(f.quota, core::makeShared<MetalRhiResourceRegistry>());
    assert(foreign);
    assert(!Lease::acquire(root, foreign, diagnostic).valid()
        && !std::strcmp(diagnostic, "RHI-METAL-INDIRECT-OUTPUT-RECORDING-ISSUER-MISMATCH"));
    foreign->abandonUnsubmitted();
    assert(!root->leasedRecordings() && !alternative->leasedRecordings());
    assert(!other->stats()->nativeDispatchEncoded && !other->stats()->nativeIndexedDrawEncoded);
    std::puts(
        "protected output lease: issued canonical command/root, exclusive generation, alias/raw/foreign/closed/stale refusal, moves/cancellation/weak command lifetime/allocation0 PASS; no completion transfer or execution authority");
}

void executionPreparation(Fixture& s, const std::shared_ptr<const Root>& output)
{
    using Execution = MetalRhiIndirectExecutionPreparation;
    static_assert(!std::is_copy_constructible_v<Execution>);
    auto& f = s.vertex.pass.graphics;
    auto consumer = MetalRhiPreparedInstanceAttributeConsumer::prepare(
        f.provider, {1u, true}, f.budget(), f.quota, f.diagnostic);
    auto binding = f.prepare();
    auto pass = s.vertex.pass.prepare();
    assert(consumer && binding && pass);
    const auto makeCommand = [&](std::uint32_t owners,
                                 std::uint32_t commands = 9u,
                                 std::shared_ptr<core::CpuMemoryQuota> quota = {},
                                 std::shared_ptr<MetalRhiResourceRegistry> registry = {}) {
        const char* id = nullptr;
        auto retention = common::CommandRetentionStorage::prepare(
            {1u, true, owners, 0u, 0u, 0u, f.cpuBytes}, quota ? quota : f.quota, id);
        assert(retention);
        return createMetalRhiIndirectOutputNativeCommandPayload({1u, true, commands},
            "ProtectedIndirectCold",
            registry ? registry : f.registry,
            std::move(retention),
            f.diagnostic);
    };
    auto command = makeCommand(128u);
    assert(command);
    MetalRhiIndirectExecutionPreparationBudget limits{1u, true, 256u};
    const auto prepare = [&] {
        return Execution::prepare(output, consumer, binding, pass, command, limits, f.diagnostic);
    };
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto commandPeak = factory.measure([&] {
        return makeCommand(128u);
    });
    const auto peak = factory.measure(prepare);
    factory.verify(commandPeak, [&] {
        return makeCommand(128u);
    });
    assert(f.diagnostic == "RHI-METAL-INDIRECT-EXECUTION-COMMAND-ALLOCATION-FAILED"
        || f.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED");
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-INDIRECT-EXECUTION-PREPARATION-ALLOCATION-FAILED"
        && !output->leasedRecordings());
    auto root = prepare();
    if (!root)
        std::fprintf(stderr, "Execution cold: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    assert(output->leasedRecordings() == 1u && !command->retentionStorage()->resourceOwnerCount());
    const auto children = root->childOwnerCount();
    std::size_t unionCount = 0u;
    {
        core::BoundedRetainer<void> owners(children + 1u);
        assert(owners.retain(root));
        for (std::size_t i = 0u; i < children; ++i)
            assert(owners.retain(root->childOwnerAt(i)));
        unionCount = owners.values().size();
    }
    root.reset();
    assert(!output->leasedRecordings());
    const auto reject = [&](const char* id) {
        const auto cpu = f.quota->liveBytes();
        auto denied = prepare();
        if (denied || f.diagnostic != id)
            std::fprintf(stderr, "Expected %s got %s\n", id, f.diagnostic.c_str());
        assert(!denied && f.diagnostic == id && f.quota->liveBytes() == cpu
            && !output->leasedRecordings() && !command->retentionStorage()->resourceOwnerCount());
    };
    limits.maxChildOwners = static_cast<std::uint32_t>(children - 1u);
    reject("RHI-METAL-INDIRECT-EXECUTION-CHILD-OWNER-BUDGET");
    limits.maxChildOwners = static_cast<std::uint32_t>(children);
    root = prepare();
    assert(root && !root->validate());
    root.reset();
    limits.prototypeOptIn = false;
    reject("RHI-METAL-INDIRECT-EXECUTION-PREPARATION-BUDGET");
    limits.prototypeOptIn = true;
    auto originalCommand = command;
    command = makeCommand(static_cast<std::uint32_t>(unionCount - 1u));
    assert(command);
    reject("RHI-COMMAND-RETENTION-OWNER-CAPACITY");
    command->abandonUnsubmitted();
    command = makeCommand(static_cast<std::uint32_t>(unionCount));
    root = prepare();
    assert(root && !root->validate());
    root.reset();
    command->abandonUnsubmitted();
    command = makeCommand(128u, 8u);
    reject("RHI-INDIRECT-RECORDING-COMMAND-BUDGET");
    command->abandonUnsubmitted();
    command = originalCommand;
    auto originalPass = pass;
    auto originalBinding = binding;
    pass = std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>(binding, pass.get());
    reject("RHI-METAL-INDIRECT-EXECUTION-OWNER-MISMATCH");
    pass = originalPass;
    binding = std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>(pass, binding.get());
    reject("RHI-METAL-INDIRECT-EXECUTION-OWNER-MISMATCH");
    binding = originalBinding;
    auto originalConsumer = consumer;
    consumer =
        std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer>(pass, consumer.get());
    reject("RHI-METAL-INDIRECT-EXECUTION-OWNER-MISMATCH");
    consumer = originalConsumer;
    command = std::shared_ptr<MetalRhiNativeCommandPayload>(binding, command.get());
    reject("RHI-METAL-INDIRECT-EXECUTION-OWNER-MISMATCH");
    command = originalCommand;
    command = s.vertex.pass.command(128u);
    reject("RHI-METAL-INDIRECT-EXECUTION-COMMAND-PREPARATION-MISMATCH");
    command->abandonUnsubmitted();
    command = makeCommand(128u, 9u, core::makeShared<core::CpuMemoryQuota>(f.cpuBytes));
    reject("RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    command->abandonUnsubmitted();
    command = makeCommand(128u, 9u, {}, core::makeShared<MetalRhiResourceRegistry>());
    reject("RHI-METAL-INDIRECT-EXECUTION-PROVIDER-ISSUER-MISMATCH");
    command->abandonUnsubmitted();
    command = originalCommand;
    root = prepare();
    assert(root && !root->validate());
    assert(!prepare() && f.diagnostic == "RHI-METAL-INDIRECT-OUTPUT-IN-FLIGHT"
        && output->leasedRecordings() == 1u);
    auto other = makeCommand(128u);
    assert(root->validateForCommand(other));
    other->abandonUnsubmitted();
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i)
            assert(!root->validate() && !root->validateForCommand(command));
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    assert(!command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic)
        && f.diagnostic == Execution::executionDiagnostic());
    assert(!command->recordComputePassBegin(f.diagnostic)
        && f.diagnostic == Execution::executionDiagnostic());
    assert(!command->recordDispatch(1u, 1u, 1u, f.diagnostic)
        && f.diagnostic == Execution::executionDiagnostic());
    assert(!command->finish(f.diagnostic) && f.diagnostic == Execution::executionDiagnostic());
    MetalRhiNativeQueueStats queue;
    assert(!command->submit(queue, command->preparedCompletionState(), {}, 0u, {}, 0u, f.diagnostic)
        && f.diagnostic == Execution::executionDiagnostic() && !queue.nativeQueueSubmitCount);
    assert(!command->stats()->nativeDispatchEncoded && !command->stats()->nativeIndexedDrawEncoded
        && !command->retentionStorage()->resourceOwnerCount() && !root->validate());
    command->abandonUnsubmitted();
    assert(root->validate() && output->leasedRecordings() == 1u);
    root.reset();
    assert(!output->leasedRecordings());
    auto retry = makeCommand(128u);
    command = retry;
    root = prepare();
    assert(root);
    retry.reset();
    std::weak_ptr<MetalRhiNativeCommandPayload> observed = command;
    command.reset();
    assert(observed.expired() && root->validate() && output->leasedRecordings() == 1u);
    std::weak_ptr<const MetalRhiOrdinaryPreparedRenderPass> observedPass = pass;
    std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observedBinding = binding;
    std::weak_ptr<const MetalRhiPreparedInstanceAttributeConsumer> observedConsumer = consumer;
    std::weak_ptr<void> observedDescriptor = pass->native().descriptor;
    pass.reset();
    binding.reset();
    consumer.reset();
    originalPass.reset();
    originalBinding.reset();
    originalConsumer.reset();
    originalCommand.reset();
    assert(!observedPass.expired() && !observedBinding.expired() && !observedConsumer.expired()
        && !observedDescriptor.expired());
    root.reset();
    assert(!output->leasedRecordings() && observedPass.expired() && observedBinding.expired()
        && observedConsumer.expired() && observedDescriptor.expired());
    std::printf(
        "protected execution cold childOwners%zu union%zu commandPeak%llu rootPeak%llu; exact/one-less child/union/command/CPU quotas, aliases/foreign/second lease, weak command/cancellation, allocation0 and closed native gates PASS\n",
        children,
        unionCount,
        static_cast<unsigned long long>(commandPeak),
        static_cast<unsigned long long>(peak));
}

std::shared_ptr<core::CpuMemoryQuota> run(const char* vs, const char* fs, const char* compute)
{
    Fixture s(vs, fs, compute);
    auto& f = s.vertex.pass.graphics;
    const auto reject = [&](const char* id) {
        const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
        auto rejected = s.prepare();
        if (rejected || f.diagnostic != id)
            std::fprintf(stderr, "Expected %s got %s\n", id, f.diagnostic.c_str());
        assert(!rejected && f.diagnostic == id && f.quota->liveBytes() == cpu
            && f.device->memoryStats().allocatedBytes == gpu);
    };
    reject("RHI-GPU-INDIRECT-OUTPUT-FETCH-WRITES-NOT-SEALED");
    s.streams[0].wrapper->sealHostWrites();
    reject("RHI-GPU-INDIRECT-OUTPUT-FETCH-WRITES-NOT-SEALED");
    s.streams[0].wrapper->sealWrites();
    reject("RHI-GPU-INDIRECT-OUTPUT-FETCH-WRITES-NOT-SEALED");
    s.seal();
    auto input = s.streams;
    s.streams[1] = s.streams[0];
    reject("RHI-METAL-INDIRECT-OUTPUT-STREAM-DUPLICATE");
    s.streams = input;
    s.streams[1].wrapper = s.vertex.buffers[2];
    s.streams[1].wrapper->sealWrites();
    reject("RHI-METAL-INDIRECT-OUTPUT-STREAM-WRAPPER-MISMATCH");
    s.streams = input;
    s.streams[0].binding.reset();
    reject("RHI-METAL-INDIRECT-OUTPUT-STREAM-ROOT-MISMATCH");
    s.streams = input;
    s.streams[0].wrapper =
        std::shared_ptr<GpuBuffer>(s.streams[1].wrapper, s.streams[0].wrapper.get());
    reject("RHI-METAL-INDIRECT-OUTPUT-STREAM-WRAPPER-OWNER-MISMATCH");
    s.streams = input;
    s.streams[0].binding = std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>(
        s.streams[1].wrapper, s.streams[0].binding.get());
    reject("RHI-METAL-INDIRECT-OUTPUT-STREAM-ROOT-OWNER-MISMATCH");
    s.streams = input;
    auto originalPipeline = s.pipeline;
    auto originalIndex = s.index;
    s.pipeline = std::shared_ptr<const MetalRhiIndirectProducerPipeline>(s.index, s.pipeline.get());
    reject("RHI-METAL-INDIRECT-OUTPUT-ROOT-OWNER-MISMATCH");
    s.pipeline = originalPipeline;
    s.index =
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>(s.pipeline, s.index.get());
    reject("RHI-METAL-INDIRECT-OUTPUT-ROOT-OWNER-MISMATCH");
    s.index = originalIndex;
    originalPipeline.reset();
    originalIndex.reset();
    assert(!Root::prepare(s.pipeline, s.index, *f.device, nullptr, 2u, s.budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-INDIRECT-OUTPUT-STREAM-COUNT");
    assert(
        !Root::prepare(s.pipeline, s.index, *f.device, s.streams.data(), 1u, s.budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-INDIRECT-OUTPUT-STREAM-COUNT");
    const auto budget = s.budget;
    s.budget.maxOutputBytes--;
    reject("RHI-GPU-INDIRECT-OUTPUT-BUDGET-EXCEEDED");
    s.budget = budget;
    s.budget.maxStreamRoots--;
    reject("RHI-GPU-INDIRECT-OUTPUT-BUDGET-EXCEEDED");
    s.budget = budget;
    s.budget.expectation.maxSnapshotBytes--;
    reject("RHI-GPU-INDIRECT-OUTPUT-BUDGET-EXCEEDED");
    s.budget = budget;
    s.budget.optIn = false;
    reject("RHI-GPU-INDIRECT-OUTPUT-BUDGET");
    s.budget = budget;
    auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
    test::MetalIndirectProducerPipelineFixture foreign(compute, wrongQuota);
    auto otherPipeline = foreign.prepare();
    assert(otherPipeline);
    assert(!Root::prepare(
               otherPipeline, s.index, *f.device, s.streams.data(), 2u, s.budget, f.diagnostic)
        && f.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH");
    auto foreignDevice = createMetalRhiDevice(test::OrdinaryGraphicsFixture::deviceDesc());
    assert(foreignDevice);
    assert(!Root::prepare(
               s.pipeline, s.index, *foreignDevice, s.streams.data(), 2u, s.budget, f.diagnostic)
        && f.diagnostic == "RHI-METAL-INDIRECT-OUTPUT-ISSUER-MISMATCH");
    foreignDevice.reset();
    // Full producer range, including instanceCount and firstInstance, is proven
    // before output allocation. Each too-short immutable source remains a valid
    // ordinary one-element binding, but fails the expected indirect footprint.
    for (std::size_t i = 0u; i < 2u; ++i) {
        const auto slot = i ? 8u : 3u, offset = i ? 24u : 8u;
        auto shortBuffer = core::shareUnique(f.device->createBuffer({"ShortImmutableFetch",
            i ? 95u : 31u,
            ResourceUsage::VertexBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead}));
        assert(shortBuffer);
        shortBuffer->sealWrites();
        auto binding = s.vertex.prepare(*shortBuffer, slot, offset);
        s.streams[i] = {binding, shortBuffer};
        reject("RHI-VERTEX-INPUT-FETCH-RANGE");
        s.streams = input;
    }
    auto root = s.prepare();
    if (!root)
        std::fprintf(stderr, "Prepare: %s\n", f.diagnostic.c_str());
    assert(root && !root->validate());
    const auto owners = root->childOwnerCount();
    assert(owners > 0u && owners <= s.budget.maxChildOwners);
    s.budget.maxChildOwners = static_cast<std::uint32_t>(owners - 1u);
    reject("RHI-GPU-INDIRECT-OUTPUT-OWNER-BUDGET-EXCEEDED");
    s.budget.maxChildOwners = static_cast<std::uint32_t>(owners);
    auto exactOwners = s.prepare();
    assert(exactOwners);
    exactOwners.reset();
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure([&] {
        return s.prepare();
    });
    factory.verify(peak, [&] {
        return s.prepare();
    });
    assert(f.diagnostic == "RHI-METAL-INDIRECT-OUTPUT-PREPARATION-ALLOCATION-FAILED");
    const auto gpuBaseline = f.device->memoryStats().allocatedBytes;
    const auto gpuCapacity = f.device->memoryStats().budgetCapacityBytes;
    assert(gpuCapacity > gpuBaseline + sizeof(DrawIndexedIndirectCommand));
    for (const auto remaining :
        {sizeof(DrawIndexedIndirectCommand), sizeof(DrawIndexedIndirectCommand) - 1u}) {
        auto pressure = f.device->createBuffer({"ExactGpuOutputBudget",
            gpuCapacity - gpuBaseline - remaining,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(pressure);
        auto admitted = s.prepare();
        if (remaining == sizeof(DrawIndexedIndirectCommand)) {
            assert(admitted && f.device->memoryStats().allocatedBytes == gpuCapacity);
        } else {
            assert(!admitted && f.diagnostic == "RHI-METAL-INDIRECT-OUTPUT-BUFFER-CREATION-FAILED");
        }
        admitted.reset();
        pressure.reset();
        assert(f.device->memoryStats().allocatedBytes == gpuBaseline);
    }
    std::array<std::uint8_t, sizeof(DrawIndexedIndirectCommand)> zeros{};
    auto actual = zeros;
    actual.fill(255u);
    assert(readMetalRhiNativeBuffer(
        root->buffer(), actual.data(), actual.size(), 0u, actual.size(), f.diagnostic));
    assert(actual == zeros);
    assert(root->expectation().purpose()
        == IndirectArgumentConsumptionContract::Purpose::GpuOutputExpectation);
    const MetalRhiSealedBufferUpload::Diagnostics testIds{
        "Content", "Seal", "Resource", "Identity", "Create", "Upload"};
    assert(!MetalRhiSealedBufferUpload::prepare(*f.device,
               f.registry,
               s.pipeline->native()->nativeDevice,
               root->expectation().bytes(),
               MetalRhiSealedBufferUpload::Domain::SuppressedGpuIndirect,
               testIds,
               f.diagnostic)
        && f.diagnostic == "Content" && f.device->memoryStats().allocatedBytes == gpuBaseline);
    assert(root->expectation().bytes()
        != core::ByteBuffer(zeros.begin(), zeros.end(), &core::defaultCpuMemoryResource()));
    assert(root->resource().hostWritesSealed() && root->resource().gpuWritesSealed()
        && root->resource().gpuTransferWritesSealed());
    auto& output = const_cast<GpuBuffer&>(root->resource());
    const auto write =
        f.device->writeBuffer(output, root->expectation().bytes().data(), zeros.size());
    assert(!write.success && write.diagnostics == "RHI-BUFFER-HOST-WRITES-SEALED");
    auto arena = f.device->createDescriptorArena({"NoProtectedWrite", 1u, 1u});
    assert(arena);
    auto descriptor =
        arena->allocateDescriptorSet({"Protected", s.producer.layout.descriptorLayouts.front()});
    assert(descriptor);
    DescriptorBindingWrite writable{
        3u, DescriptorType::StorageBuffer, output.id(), DescriptorResourceKind::Buffer};
    const auto refused = descriptor->write(writable);
    assert(!refused.success && refused.diagnostics == "RHI-BUFFER-GPU-WRITES-SEALED");
    descriptor.reset();
    arena.reset();
    assert(!semantics::GpuIndirectProducerLowering::current(Backend::Metal).executable);
    assert(std::strcmp(Root::executionDiagnostic(), "RHI-METAL-INDIRECT-OUTPUT-PREPARATION-ONLY")
        == 0);
    recordingLeases(s, root);
    auto alternate = s.prepare();
    assert(alternate);
    test::MetalNativeIdentityMutationProbe::verify(
        root->buffer()->resource, alternate->buffer()->resource, [&] {
            const auto* id = root->validate();
            assert(id && !std::strcmp(id, "RHI-METAL-INDIRECT-OUTPUT-NATIVE-IDENTITY-MISMATCH"));
        });
    for (std::size_t i = 0u; i < 2u; ++i)
        test::MetalNativeIdentityMutationProbe::verify(
            s.streams[i].binding->buffer()->resource, alternate->buffer()->resource, [&] {
                assert(root->validate());
            });
    auto& native = root->buffer()->resource;
    native.hazardsExplicitlyTracked = false;
    assert(root->validate());
    native.hazardsExplicitlyTracked = true;
    const auto storage = native.storageMode;
    native.storageMode = MetalRhiNativeStorageMode::Private;
    assert(root->validate());
    native.storageMode = storage;
    auto& desc = const_cast<BufferDesc&>(output.desc());
    const auto saved = desc;
    desc.sizeBytes--;
    assert(root->validate());
    desc = saved;
    desc.usage = ResourceUsage::IndirectBuffer;
    assert(root->validate());
    desc = saved;
    desc.memory = MemoryUsage::GpuOnly;
    assert(root->validate());
    desc = saved;
    desc.initialState = ResourceState::ShaderRead;
    assert(root->validate());
    desc = saved;
    f.registry->unregisterResource(output.id());
    assert(root->validate());
    f.registry->registerBuffer(output);
    ResourceBarrierDesc barrier;
    barrier.resource = output.id();
    barrier.before = ResourceState::ShaderWrite;
    barrier.after = ResourceState::ShaderRead;
    assert(setMetalRhiBufferStateForBarrier(output, barrier).success && root->validate());
    barrier.before = ResourceState::ShaderRead;
    barrier.after = ResourceState::ShaderWrite;
    assert(setMetalRhiBufferStateForBarrier(output, barrier).success && !root->validate());
    for (const auto& stream : s.streams) {
        auto& d = const_cast<BufferDesc&>(stream.wrapper->desc());
        const auto savedBytes = d.sizeBytes;
        --d.sizeBytes;
        assert(root->validate());
        d.sizeBytes = savedBytes;
        barrier.resource = stream.wrapper->id();
        barrier.before = ResourceState::ShaderRead;
        barrier.after = ResourceState::CopySrc;
        assert(
            setMetalRhiBufferStateForBarrier(*stream.wrapper, barrier).success && root->validate());
        barrier.before = ResourceState::CopySrc;
        barrier.after = ResourceState::ShaderRead;
        assert(setMetalRhiBufferStateForBarrier(*stream.wrapper, barrier).success
            && !root->validate());
    }
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 1000u; ++i) {
            assert(!root->validate());
            for (std::size_t j = 0u; j < root->childOwnerCount(); ++j)
                assert(root->childOwnerAt(j));
        }
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    alternate.reset();
    std::weak_ptr<GpuBuffer> wrapper0 = s.streams[0].wrapper, wrapper1 = s.streams[1].wrapper;
    std::weak_ptr<void> object = native.nativeObject, token = native.allocationReleaseToken;
    input = {};
    s.streams = {};
    s.vertex.dropInputs();
    s.index.reset();
    s.pipeline.reset();
    // Input copies intentionally release before testing root-only ownership.
    // The original output object/token survive internal native field replacement.
    native.nativeObject.reset();
    native.allocationReleaseToken.reset();
    assert(root->validate() && !object.expired() && !token.expired() && !wrapper0.expired()
        && !wrapper1.expired());
    std::printf(
        "protected output suppressed20 bytes, expected words3/2/1/fffffffe/1, childOwners%zu, coldPeak%llu; writes/identity/fetch/budgets/allocation0 PASS\n",
        owners,
        static_cast<unsigned long long>(peak));
    root.reset();
    assert(object.expired() && token.expired() && wrapper0.expired() && wrapper1.expired());
    return f.quota;
}

std::shared_ptr<core::CpuMemoryQuota> runExecution(
    const char* vs, const char* fs, const char* compute)
{
    Fixture fixture(vs, fs, compute);
    fixture.seal();
    auto output = fixture.prepare();
    assert(output);
    executionPreparation(fixture, output);
    return fixture.vertex.pass.graphics.quota;
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 4 && metalRhiNativeDeviceHandle());
    @autoreleasepool {
        auto quota = runExecution(argv[1], argv[2], argv[3]);
        assert(!quota->liveBytes());
    }
    @autoreleasepool {
        auto quota = run(argv[1], argv[2], argv[3]);
        assert(!quota->liveBytes());
    }
    std::puts(
        "ProtectedIndirectOutputPreparation/1: real Metal cold storage only; no instance ABI/completion transfer/dispatch/indirect execution authority");
}
