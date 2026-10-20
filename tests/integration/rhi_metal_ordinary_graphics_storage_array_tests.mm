#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_native_sync.h"

#include <Metal/Metal.h>
#include <array>
#include <simd/simd.h>

#ifdef NDEBUG
#error Native array tests require assertions
#endif
namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;

class ReadOnlyBufferArrayFixture final {
public:
    static DescriptorLayoutDesc layout(
        DescriptorType type = DescriptorType::ReadOnlyStorageBuffer, bool dynamic = false)
    {
        DescriptorLayoutDesc inputs;
        inputs.name = "StorageArrayFacts";
        inputs.bindings = {{3u, type, ShaderStage::Vertex | ShaderStage::Fragment, 2u},
            {8u, type, ShaderStage::Fragment, 2u}};
        if (dynamic)
            for (auto& binding : inputs.bindings)
                binding.dynamicOffset = true;
        return inputs;
    }

    ReadOnlyBufferArrayFixture(
        const char* vs, const char* fs, DescriptorType type = DescriptorType::ReadOnlyStorageBuffer)
        // Exact selected-entry map: Shared VS + Shared FS + Fragment FS.
        : type(type), pass(vs, fs, true, layout(type), {}, {}, 3u)
    {
        write(*pass.graphics.set, buffers, false);
    }

    void write(
        DescriptorSet& set, std::array<core::UniquePtr<GpuBuffer>, 4>& destination, bool changed)
    {
        auto& f = pass.graphics;
        const std::array<std::array<float, 8>, 4> data{
            {{1, 0, 0, 0, (changed ? 192.0f : 32.0f) / UINT8_MAX, 0, 0, 64.0f / UINT8_MAX},
                {1, 0, 0, 0, 0, (changed ? 128.0f : 64.0f) / UINT8_MAX, 0, 64.0f / UINT8_MAX},
                {0, 0, (changed ? 64.0f : 128.0f) / UINT8_MAX, 64.0f / UINT8_MAX},
                {0, 0, 0, 63.0f / UINT8_MAX}}};
        for (std::size_t i = 0; i < 4; ++i) {
            const auto bytes = i < 2 ? sizeof(float) * 8u : sizeof(float) * 4u;
            destination[i] = f.device->createBuffer({"ReadonlyArrayElement",
                bytes,
                type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                      : ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(destination[i]);
            assert(f.device->writeBuffer(*destination[i], data[i].data(), bytes).success);
            DescriptorBindingWrite w{
                i < 2 ? 3u : 8u, type, destination[i]->id(), DescriptorResourceKind::Buffer};
            w.arrayElement = i % 2u;
            assert(set.write(w).success);
        }
    }

    auto second()
    {
        auto& f = pass.graphics;
        secondSet = f.arena->allocateDescriptorSet({"StorageArrayB", f.inputs});
        assert(secondSet);
        write(*secondSet, secondBuffers, true);
        auto result = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
            f.provider, *secondSet, 0u, f.registry, f.budget(), f.quota, f.diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", f.diagnostic.c_str());
        assert(result);
        return result;
    }

    void dropInputs()
    {
        auto& f = pass.graphics;
        f.set.reset();
        secondSet.reset();
        f.arena.reset();
        f.provider.reset();
        for (auto& buffer : buffers)
            buffer.reset();
        for (auto& buffer : secondBuffers)
            buffer.reset();
    }

    DescriptorType type;
    test::OrdinaryGraphicsPassFixture pass;
    std::array<core::UniquePtr<GpuBuffer>, 4> buffers, secondBuffers;
    core::UniquePtr<DescriptorSet> secondSet;
};

void nativeQueries(ReadOnlyBufferArrayFixture& s)
{
    auto& f = s.pass.graphics;
    std::size_t activeFacts = 0u;
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned e = 0; e < 2; ++e) {
            const auto bytes = s.buffers[b * 2u + e]->desc().sizeBytes;
            MetalRhiNativeArgumentBindingPayload logical;
            logical.binding = b ? 8u : 3u;
            logical.type = s.type;
            logical.arrayCount = 2u;
            logical.arrayElement = e;
            logical.stages =
                b ? ShaderStage::Fragment : ShaderStage::Vertex | ShaderStage::Fragment;
            logical.resourceKind = DescriptorResourceKind::Buffer;
            logical.argumentKind = MetalRhiArgumentKind::Buffer;
            for (std::size_t i = 0; i < 2; ++i) {
                auto stage = f.provider->stages()[i];
                MetalRhiPreparedBindingLocation location;
                assert(
                    stage->resolve(logical, 0u, f.provider->native()->nativeBindingStages, location)
                        .success);
                if (!location.active)
                    continue;
                assert(location.array);
                const auto& facts = f.provider->native()->graphicsBufferPointeeFacts[i];
                assert(facts.version == 1u && !facts.queryDiagnostic);
                MetalRhiStageBufferPointeeRange captured;
                const auto* id = MetalRhiStageBufferPointeeRangeValidator::capture(facts,
                    stage->stage,
                    location.slot,
                    location.argumentIndex + e,
                    2u,
                    e,
                    0u,
                    bytes,
                    captured);
                if (id)
                    std::fprintf(stderr, "Native query: %s\n", id);
                assert(!id && captured.fact.requiredBytes == bytes
                    && captured.fact.access == MTLArgumentAccessReadOnly);
                assert(captured.fact.alignment
                    == (s.type == DescriptorType::UniformBuffer ? alignof(simd_float4)
                                                                : alignof(float)));
                assert(captured.fact.elementType
                    == static_cast<std::uint32_t>(
                        b && s.type == DescriptorType::ReadOnlyStorageBuffer ? MTLDataTypeFloat4
                                                                             : MTLDataTypeStruct));
                std::printf(
                    "pointee binding=%u element=%u stage=%zu slot=%u index=%u count=%u stride=%u alignment=%llu span=%llu\n",
                    logical.binding,
                    e,
                    i,
                    location.slot,
                    location.argumentIndex + e,
                    captured.fact.arrayCount,
                    captured.fact.argumentIndexStride,
                    static_cast<unsigned long long>(captured.fact.alignment),
                    static_cast<unsigned long long>(captured.fact.requiredBytes));
                assert(MetalRhiStageBufferPointeeRangeValidator::capture(facts,
                    stage->stage,
                    location.slot,
                    location.argumentIndex + e,
                    2u,
                    e,
                    0u,
                    bytes - 1u,
                    captured));
                const auto charge = core::inspectCpuAllocationCharge(facts.entries.data());
                assert(charge.found && charge.quota == f.quota.get());
                ++activeFacts;
            }
        }
    assert(activeFacts == 6u && f.device->capabilities().commandRetention.contractVersion == 0u);
}

void negatives(const char* vs, const char* fs, DescriptorType type)
{
    ReadOnlyBufferArrayFixture s(vs, fs, type);
    auto& f = s.pass.graphics;
    nativeQueries(s);
    const auto reject = [&](const char* expected) {
        f.reject(f.budget(), expected);
    };
    for (auto& facts : f.provider->native()->graphicsBufferPointeeFacts) {
        const auto original = facts.entries.front();
        facts.version = 0u;
        reject("FACTS-MISSING");
        facts.version = 1u;
        facts.entries.front().requiredBytes = 0u;
        reject("FACTS-MISSING");
        facts.entries.front() = original;
        facts.entries.front().alignment = 0u;
        reject("FACTS-MISSING");
        facts.entries.front() = original;
        facts.entries.front().access = MTLArgumentAccessReadWrite;
        reject("DOMAIN-UNSUPPORTED");
        facts.entries.front() = original;
        facts.entries.front().argumentBuffer = true;
        reject("DOMAIN-UNSUPPORTED");
        facts.entries.front() = original;
        facts.entries.front().argumentIndexStride = 2u;
        reject("MAP-MISMATCH");
        facts.entries.front() = original;
        facts.entries.front().arrayCount = 3u;
        reject("MAP-MISMATCH");
        facts.entries.front() = original;
        facts.entries.push_back(original);
        reject("FACTS-DUPLICATE");
        facts.entries.pop_back();
    }
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        reject("ALLOCATION-FAILED");
    }
    const auto baseline = f.quota->liveBytes();
    auto binding = f.prepare();
    const auto charge = f.quota->liveBytes() - baseline, bytes = binding->payload()->argumentBytes;
    const auto count = binding->payload()->argumentCount;
    assert(count == 3u && bytes && binding->payload()->encodedArgumentBytes <= bytes);
    std::uint32_t trackedTables = 0u;
    for (const auto& stage : binding->payload()->stages)
        if (stage)
            for (const auto& table : stage->arrays) {
                const auto native = reinterpret_cast<id<MTLBuffer>>(table.buffer.get());
                assert(native && native.hazardTrackingMode == MTLHazardTrackingModeTracked);
                ++trackedTables;
            }
    assert(trackedTables == count);
    std::printf("array arguments count=%u encoded=%llu allocated=%llu CPU=%llu\n",
        count,
        static_cast<unsigned long long>(binding->payload()->encodedArgumentBytes),
        static_cast<unsigned long long>(bytes),
        static_cast<unsigned long long>(charge));
    binding.reset();
    assert(f.quota->liveBytes() == baseline);
    auto exact = f.budget();
    exact.maxArgumentBuffers = count;
    exact.maxArgumentBufferBytes = bytes;
    auto accepted = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
        f.provider, *f.set, 0u, f.registry, exact, f.quota, f.diagnostic);
    assert(accepted && accepted->payload()->argumentBytes == bytes);
    accepted.reset();
    assert(f.quota->liveBytes() == baseline);
    auto budget = f.budget();
    budget.maxArgumentBuffers = count - 1u;
    f.reject(budget, "ARGUMENT-COUNT-BUDGET");
    budget = f.budget();
    budget.maxArgumentBufferBytes = bytes - 1u;
    f.reject(budget, "ARGUMENT-BYTE-BUDGET");
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - baseline - charge + 1u);
        reject("ALLOCATION-FAILED");
    }
    for (std::size_t i = 0; i < 4; ++i) {
        auto small = f.device->createBuffer({"PointeeOneByteShort",
            s.buffers[i]->desc().sizeBytes - 1u,
            s.buffers[i]->desc().usage,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(small);
        DescriptorBindingWrite w{
            i < 2 ? 3u : 8u, type, small->id(), DescriptorResourceKind::Buffer};
        w.arrayElement = i % 2u;
        assert(f.set->write(w).success);
        reject("POINTEE-RANGE");
        w.resource = s.buffers[i]->id();
        assert(f.set->write(w).success);
    }
    const auto dynamic = validateDescriptorLayout(ReadOnlyBufferArrayFixture::layout(type, true));
    assert(!dynamic.success
        && dynamic.diagnostics
            == "descriptor binding dynamic offset does not support descriptor arrays");
    PipelineLayoutDesc invalid;
    invalid.descriptorLayouts = {ReadOnlyBufferArrayFixture::layout(type, true)};
    assert(!f.device->createPipelineLayout(invalid));
    std::array<std::uint32_t, 4> offsets{};
    const auto before = f.quota->liveBytes();
    assert(!MetalRhiOrdinaryPreparedGraphicsBinding::prepare(f.provider,
        *f.set,
        0u,
        f.registry,
        f.budget(),
        f.quota,
        f.diagnostic,
        offsets.data(),
        offsets.size()));
    assert(f.diagnostic == "RHI-METAL-ORDINARY-OFFSET-COUNT" && f.quota->liveBytes() == before);
}

void execute(const char* vs, const char* fs, bool transition, bool dropped, DescriptorType type)
{
    ReadOnlyBufferArrayFixture s(vs, fs, type);
    auto& f = s.pass.graphics;
    auto first = f.prepare(), second = s.second();
    auto pass = s.pass.prepare();
    auto probe = s.pass.command(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedGraphicsBinding(second, f.diagnostic));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    probe->abandonUnsubmitted();
    probe.reset();
    assert(initial == 44u && combined == 66u);
    std::printf("array owners initial=%zu combined=%zu\n", initial, combined);
    auto shortBegin = s.pass.command(initial - 1u);
    assert(!shortBegin->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
    assert(!shortBegin->stats()->nativeRenderEncoderCreated
        && !shortBegin->retentionStorage()->resourceOwnerCount());
    shortBegin->abandonUnsubmitted();
    shortBegin.reset();
    auto command = s.pass.command(transition ? combined : combined - 1u);
    auto replacement = f.device->createBuffer({"ArrayIdentityReplacement",
        sizeof(float) * 8u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    auto group = std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(
        first->payload()->descriptors);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
        for (unsigned repeat = 0; repeat < 32; ++repeat)
            assert(command->recordOrdinaryPreparedGraphicsBinding(first, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        const auto reject = [&](const char* expected) {
            assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
            assert(f.diagnostic.find(expected) != f.diagnostic.npos);
            assert(!command->recordOrdinaryPreparedGraphicsBinding(first, f.diagnostic));
            assert(f.diagnostic.find(expected) != f.diagnostic.npos);
            assert(command->retentionStorage()->resourceOwnerCount() == initial
                && !command->recordingPoisoned());
        };
        for (const auto& buffer : s.buffers)
            test::MetalNativeIdentityMutationProbe::verify(
                metalRhiNativePayloadFor(*buffer)->resource,
                metalRhiNativePayloadFor(*replacement)->resource,
                [&] {
                    reject("RESOURCE-NATIVE-IDENTITY-MISMATCH");
                });
        for (const auto& stage : first->payload()->stages) {
            auto mutableStage =
                std::const_pointer_cast<MetalRhiNativePreparedFunctionBindings>(stage);
            for (auto& array : mutableStage->arrays) {
                test::MetalNativeIdentityMutationProbe::verifyOwner(array.buffer,
                    metalRhiNativePayloadFor(*replacement)->resource.nativeObject,
                    [&] {
                        reject("OWNER-PROJECTION-MISMATCH");
                    });
                const auto identity = array.preparedNativeIdentity;
                array.preparedNativeIdentity =
                    MetalRhiNativeResourceObservation(f.provider->native()->nativeDevice,
                        metalRhiNativePayloadFor(*replacement)->resource.nativeObject);
                reject("ARGUMENT-STORAGE-IDENTITY-MISMATCH");
                array.preparedNativeIdentity = identity;
                const auto slot = array.slot;
                array.slot += 1u;
                reject("PREPARED-GRAPHICS-STORAGE-MISMATCH");
                array.slot = slot;
            }
        }
        for (auto& binding : group->bindings)
            for (std::size_t stage = 0; stage < 2; ++stage) {
                auto& captured = binding.graphicsBufferPointeeRanges[stage];
                const auto original = captured;
                if (captured.stage == ShaderStage::None) {
                    captured.stage = ShaderStage::Vertex;
                    reject("INACTIVE-CAPTURE-MISMATCH");
                    captured = original;
                    continue;
                }
                captured.stage = ShaderStage::None;
                reject("POINTEE-CAPTURE-MISMATCH");
                captured = original;
                captured.fact.requiredBytes -= sizeof(float);
                reject("POINTEE-CAPTURE-MISMATCH");
                captured = original;
                captured.fact.argumentIndex += 1u;
                reject("POINTEE-CAPTURE-MISMATCH");
                captured = original;
                binding.graphicsBufferRanges[stage].stage = captured.stage;
                reject("POINTEE-CAPTURE-MISMATCH");
                binding.graphicsBufferRanges[stage] = {};
            }
        auto& element = group->bindings.front();
        element.bufferOffsetBytes = 4u;
        reject("OFFSET-DOMAIN-UNIMPLEMENTED");
        element.bufferOffsetBytes = 0u;
        auto& facts = f.provider->native()->graphicsBufferPointeeFacts[1];
        const auto saved = facts.entries.front();
        facts.entries.front().requiredBytes -= sizeof(float);
        reject("POINTEE-CAPTURE-MISMATCH");
        facts.entries.front() = saved;
        facts.entries.front().access = MTLArgumentAccessReadWrite;
        reject("DOMAIN-UNSUPPORTED");
        facts.entries.front() = saved;
        auto& resource = const_cast<ShaderPackageDesc&>(f.provider->interface())
                             .reflection.descriptorBindings.front();
        const auto type = resource.type;
        resource.type = DescriptorType::StorageBuffer;
        reject("RESOURCE-DOMAIN-UNIMPLEMENTED");
        resource.type = type;
        const auto logicalCount = resource.arrayCount;
        resource.arrayCount += 1u;
        reject("REFLECTION-CAPTURE-MISMATCH");
        resource.arrayCount = logicalCount;
        const auto retryAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(command->recordOrdinaryPreparedGraphicsBinding(second, f.diagnostic) == transition);
        if (!transition)
            assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        for (unsigned repeat = 0; repeat < 32u; ++repeat)
            assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        assert(command->recordRenderPassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == retryAllocations);
        std::pmr::set_default_resource(previous);
    }
    assert(command->finish(f.diagnostic));
    std::array<std::weak_ptr<void>, 4> objects, tokens;
    std::array<std::weak_ptr<void>, 3> argumentBuffers;
    std::size_t ordinal = 0;
    for (const auto& stage : (transition ? second : first)->payload()->stages)
        for (const auto& array : stage->arrays)
            argumentBuffers[ordinal++] = array.buffer;
    assert(ordinal == 3u);
    auto& inputs = transition ? s.secondBuffers : s.buffers;
    for (std::size_t i = 0; i < 4; ++i) {
        auto& resource = metalRhiNativePayloadFor(*inputs[i])->resource;
        objects[i] = resource.nativeObject;
        tokens[i] = resource.allocationReleaseToken;
        resource.nativeObject = metalRhiNativePayloadFor(*replacement)->resource.nativeObject;
        resource.allocationReleaseToken =
            metalRhiNativePayloadFor(*replacement)->resource.allocationReleaseToken;
    }
    group.reset();
    first.reset();
    second.reset();
    pass.reset();
    s.dropInputs();
    auto gate = createMetalRhiNativeSharedEventPayload("ArrayDelayedCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    if (dropped)
        command.reset();
    assert(!done->isComplete());
    for (std::size_t i = 0; i < 4; ++i)
        assert(!objects[i].expired() && !tokens[i].expired());
    for (const auto& buffer : argumentBuffers)
        assert(!buffer.expired());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    for (std::size_t i = 0; i < 4; ++i)
        assert(objects[i].expired() && tokens[i].expired());
    for (const auto& buffer : argumentBuffers)
        assert(buffer.expired());
    s.pass.verifyColor(transition ? std::array<std::uint8_t, 4>{192, 128, 64, 255}
                                  : std::array<std::uint8_t, 4>{32, 64, 128, 255});
    std::printf("readonly array: transition=%d dropped=%d exact data and real completion pass\n",
        transition,
        dropped);
}

void liveness(const char* vs, const char* fs, DescriptorType type)
{
    for (std::size_t i = 0; i < 4; ++i) {
        ReadOnlyBufferArrayFixture s(vs, fs, type);
        auto& f = s.pass.graphics;
        auto first = f.prepare();
        auto pass = s.pass.prepare();
        auto command = s.pass.command(128u);
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
        DescriptorBindingWrite w{
            i < 2 ? 3u : 8u, type, s.buffers[i]->id(), DescriptorResourceKind::Buffer};
        w.arrayElement = i % 2u;
        assert(f.set->write(w).success);
        assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
            && f.diagnostic.find("CONTENT-CHANGED") != f.diagnostic.npos);
        first = f.prepare();
        assert(command->recordOrdinaryPreparedGraphicsBinding(first, f.diagnostic));
        const auto owners = command->retentionStorage()->resourceOwnerCount();
        s.buffers[i].reset();
        assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
            && f.diagnostic.find("RESOURCE-NOT-LIVE") != f.diagnostic.npos);
        assert(command->retentionStorage()->resourceOwnerCount() == owners
            && !command->stats()->nativeDrawEncoded);
        command->abandonUnsubmitted();
    }
}

void cancellation(const char* vs, const char* fs, bool sealed, DescriptorType type)
{
    ReadOnlyBufferArrayFixture s(vs, fs, type);
    auto& f = s.pass.graphics;
    auto first = f.prepare();
    auto pass = s.pass.prepare();
    auto command = s.pass.command(128u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
    if (sealed)
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const void> observed = first;
    first.reset();
    pass.reset();
    s.dropInputs();
    assert(!observed.expired());
    command->abandonUnsubmitted();
    assert(observed.expired());
    observed.reset();
    command.reset();
    assert(!f.quota->liveBytes());
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 4 || argc == 5);
    const auto type = argc >= 4 && std::strcmp(argv[3], "UniformBuffer") == 0
        ? DescriptorType::UniformBuffer
        : DescriptorType::ReadOnlyStorageBuffer;
    if (argc >= 4)
        assert(std::strcmp(argv[3], "UniformBuffer") == 0);
    if (argc == 5) {
        assert(std::strcmp(argv[4], "query-only") == 0);
        ReadOnlyBufferArrayFixture s(argv[1], argv[2], type);
        nativeQueries(s);
        return 0;
    }
    negatives(argv[1], argv[2], type);
    liveness(argv[1], argv[2], type);
    for (bool transition : {false, true})
        for (bool dropped : {false, true})
            execute(argv[1], argv[2], transition, dropped, type);
    for (bool sealed : {false, true})
        cancellation(argv[1], argv[2], sealed, type);
}
