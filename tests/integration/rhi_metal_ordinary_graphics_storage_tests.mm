#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_native_sync.h"

#include <array>

#ifdef NDEBUG
#error Native storage tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;

class StorageFixture final {
public:
    static DescriptorLayoutDesc layout(bool dynamic = false)
    {
        DescriptorLayoutDesc inputs;
        inputs.name = "OrdinaryGraphicsStorage";
        inputs.bindings = {{8u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Fragment},
            {3u,
                DescriptorType::ReadOnlyStorageBuffer,
                ShaderStage::Vertex | ShaderStage::Fragment}};
        if (dynamic)
            for (auto& binding : inputs.bindings)
                binding.dynamicOffset = true;
        return inputs;
    }

    StorageFixture(const char* vs, const char* fs, bool dynamic = false)
        : pass(vs, fs, false, layout(dynamic))
    {
        auto& f = pass.graphics;
        const std::array<std::uint32_t, 2> keys{3u, 8u};
        for (std::size_t i = 0; i < keys.size(); ++i) {
            MetalRhiNativeArgumentBindingPayload logical;
            logical.binding = keys[i];
            logical.type = DescriptorType::ReadOnlyStorageBuffer;
            logical.stages =
                i ? ShaderStage::Fragment : ShaderStage::Vertex | ShaderStage::Fragment;
            logical.resourceKind = DescriptorResourceKind::Buffer;
            logical.argumentKind = MetalRhiArgumentKind::Buffer;
            const auto bytes = i ? sizeof(fragment) : sizeof(shared);
            for (std::size_t stage = 0; stage < 2; ++stage) {
                MetalRhiPreparedBindingLocation location;
                assert(f.provider->stages()[stage]
                        ->resolve(logical, 0u, f.provider->native()->nativeBindingStages, location)
                        .success);
                if (!location.active)
                    continue;
                assert(!location.array);
                const auto& facts = f.provider->native()->graphicsBufferFacts[stage];
                const MetalRhiBufferBindingFact* fact = nullptr;
                const auto* id = validateMetalRhiBufferOffset(
                    facts.entries, facts.version, location.slot, 0u, bytes, fact);
                if (id)
                    std::fprintf(stderr,
                        "Storage native fact: logical=%u stage=%zu diagnostic=%s\n",
                        keys[i],
                        stage,
                        id);
                assert(!id && fact && fact->requiredBytes == bytes && fact->alignment);
                std::printf(
                    "ordinary Graphics storage fact: logical=%u stage=%zu slot=%u alignment=%llu span=%llu\n",
                    keys[i],
                    stage,
                    location.slot,
                    static_cast<unsigned long long>(fact->alignment),
                    static_cast<unsigned long long>(fact->requiredBytes));
            }
            buffers[i] = f.device->createBuffer({"OrdinaryGraphicsStorage",
                bytes,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(buffers[i]);
            assert(f.device->writeBuffer(*buffers[i], i ? fragment.data() : shared.data(), bytes)
                    .success);
            assert(f.set
                    ->write({keys[i],
                        DescriptorType::ReadOnlyStorageBuffer,
                        buffers[i]->id(),
                        DescriptorResourceKind::Buffer})
                    .success);
        }
    }

    auto prepareSecond()
    {
        auto& f = pass.graphics;
        secondSet = f.arena->allocateDescriptorSet({"StorageB", f.inputs});
        assert(secondSet);
        const std::array<std::uint32_t, 2> keys{3u, 8u};
        const std::array<float, 8> changed{
            1, 0, 0, 0, 192.0f / UINT8_MAX, 64.0f / UINT8_MAX, 128.0f / UINT8_MAX, 1};
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto bytes = i ? sizeof(fragment) : sizeof(changed);
            secondBuffers[i] = f.device->createBuffer({"StorageB",
                bytes,
                ResourceUsage::StorageBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(secondBuffers[i]);
            assert(f.device
                    ->writeBuffer(*secondBuffers[i], i ? fragment.data() : changed.data(), bytes)
                    .success);
            assert(secondSet
                    ->write({keys[i],
                        DescriptorType::ReadOnlyStorageBuffer,
                        secondBuffers[i]->id(),
                        DescriptorResourceKind::Buffer})
                    .success);
        }
        auto result = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
            f.provider, *secondSet, 0u, f.registry, f.budget(), f.quota, f.diagnostic);
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

    test::OrdinaryGraphicsPassFixture pass;
    std::array<core::UniquePtr<GpuBuffer>, 2> buffers;
    std::array<core::UniquePtr<GpuBuffer>, 2> secondBuffers;
    core::UniquePtr<DescriptorSet> secondSet;
    std::array<float, 8> shared{
        1, 0, 0, 0, 64.0f / UINT8_MAX, 128.0f / UINT8_MAX, 192.0f / UINT8_MAX, 1};
    std::array<float, 4> fragment{};
};

void negatives(const char* vs, const char* fs)
{
    StorageFixture s(vs, fs);
    auto& f = s.pass.graphics;
    const auto reject = [&](const char* expected) {
        const auto charge = f.quota->liveBytes();
        auto result = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
            f.provider, *f.set, 0u, f.registry, f.budget(), f.quota, f.diagnostic);
        assert(!result && f.diagnostic.find(expected) != f.diagnostic.npos
            && f.quota->liveBytes() == charge);
    };
    for (auto& facts : f.provider->native()->graphicsBufferFacts) {
        const auto saved = facts.entries[0];
        facts.version = 0u;
        reject("FACTS-MISSING");
        facts.version = 1u;
        facts.entries[0].requiredBytes = 0u;
        reject("FACTS-MISSING");
        facts.entries[0] = saved;
        facts.entries[0].alignment = 0u;
        reject("FACTS-MISSING");
        facts.entries[0] = saved;
        facts.entries.push_back(saved);
        reject("FACTS-DUPLICATE");
        facts.entries.pop_back();
    }
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        reject("ALLOCATION-FAILED");
    }
    const auto baseline = f.quota->liveBytes();
    auto prepared = f.prepare();
    const auto charge = f.quota->liveBytes() - baseline;
    prepared.reset();
    assert(charge && f.quota->liveBytes() == baseline);
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - baseline - charge + 1u);
        reject("ALLOCATION-FAILED");
    }
    auto small = f.device->createBuffer({"StorageTooSmall",
        sizeof(s.shared) - 1u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(small);
    assert(f.set
            ->write({3u,
                DescriptorType::ReadOnlyStorageBuffer,
                small->id(),
                DescriptorResourceKind::Buffer})
            .success);
    reject("OFFSET-RANGE");
    StorageFixture dynamic(vs, fs, true);
    std::array<std::uint32_t, 2> offsets{};
    auto& d = dynamic.pass.graphics;
    const auto dynamicCharge = d.quota->liveBytes();
    assert(!MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
        d.provider, *d.set, 0u, d.registry, d.budget(), d.quota, d.diagnostic, offsets.data(), 2u));
    assert(d.diagnostic.find("OFFSET-DOMAIN-UNIMPLEMENTED") != d.diagnostic.npos
        && d.quota->liveBytes() == dynamicCharge);
}

void liveness(const char* vs, const char* fs)
{
    StorageFixture s(vs, fs);
    auto& f = s.pass.graphics;
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    auto command = s.pass.command(128u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    assert(f.set
            ->write({3u,
                DescriptorType::ReadOnlyStorageBuffer,
                s.buffers[0]->id(),
                DescriptorResourceKind::Buffer})
            .success);
    assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
        && f.diagnostic.find("CONTENT-CHANGED") != f.diagnostic.npos);
    binding = f.prepare();
    assert(command->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic));
    const auto owners = command->retentionStorage()->resourceOwnerCount();
    s.buffers[0].reset();
    assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
        && f.diagnostic.find("RESOURCE-NOT-LIVE") != f.diagnostic.npos);
    assert(!command->stats()->nativeDrawEncoded
        && command->retentionStorage()->resourceOwnerCount() == owners);
    command->abandonUnsubmitted();
}

void execute(const char* vs, const char* fs, bool transition, bool dropped)
{
    StorageFixture s(vs, fs);
    auto& f = s.pass.graphics;
    auto first = f.prepare(), second = s.prepareSecond();
    auto pass = s.pass.prepare();
    auto probe = s.pass.command(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedGraphicsBinding(second, f.diagnostic));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    assert(initial == 35u && combined == 48u);
    probe->abandonUnsubmitted();
    probe.reset();
    std::printf("storage owners initial=%zu combined=%zu\n", initial, combined);
    auto shortBegin = s.pass.command(initial - 1u);
    assert(!shortBegin->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
    assert(!shortBegin->stats()->nativeRenderEncoderCreated
        && !shortBegin->retentionStorage()->resourceOwnerCount());
    shortBegin->abandonUnsubmitted();
    shortBegin.reset();
    auto command = s.pass.command(transition ? combined : combined - 1u);
    auto replacement = f.device->createBuffer({"StorageIdentityReplacement",
        sizeof(s.shared),
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
        auto& element = group->bindings.front();
        const auto captured = element.graphicsBufferRanges;
        element.graphicsBufferRanges[1].stage = ShaderStage::Compute;
        reject("CAPTURE-MISMATCH");
        element.graphicsBufferRanges = captured;
        element.bufferOffsetBytes = 4u;
        reject("OFFSET-DOMAIN-UNIMPLEMENTED");
        element.bufferOffsetBytes = 0u;
        auto& facts = f.provider->native()->graphicsBufferFacts[1];
        const auto saved = facts.entries[0];
        facts.entries[0].requiredBytes += 4u;
        reject("OFFSET-RANGE");
        facts.entries[0] = saved;
        auto& resource = const_cast<ShaderPackageDesc&>(f.provider->interface())
                             .reflection.descriptorBindings.front();
        const auto type = resource.type;
        resource.type = DescriptorType::StorageBuffer;
        reject("RESOURCE-DOMAIN-UNIMPLEMENTED");
        resource.type = type;
        const auto count = resource.arrayCount;
        resource.arrayCount = 2u;
        reject("REFLECTION-CAPTURE-MISMATCH");
        resource.arrayCount = count;
        const auto retryAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(command->recordOrdinaryPreparedGraphicsBinding(second, f.diagnostic) == transition);
        if (!transition)
            assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
            && command->recordRenderPassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == retryAllocations);
        std::pmr::set_default_resource(previous);
    }
    assert(command->finish(f.diagnostic));
    std::array<std::weak_ptr<void>, 2> objects, tokens;
    auto& inputs = transition ? s.secondBuffers : s.buffers;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
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
    auto gate = createMetalRhiNativeSharedEventPayload("StorageDelayedCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    if (dropped)
        command.reset();
    assert(!done->isComplete());
    for (std::size_t i = 0; i < 2; ++i)
        assert(!objects[i].expired() && !tokens[i].expired());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    for (std::size_t i = 0; i < 2; ++i)
        assert(objects[i].expired() && tokens[i].expired());
    s.pass.verifyColor(transition ? std::array<std::uint8_t, 4>{192, 64, 128, 255}
                                  : std::array<std::uint8_t, 4>{64, 128, 192, 255});
    std::printf(
        "ordinary Graphics storage: transition=%d dropped=%d exact pixels, initial/combined one-less, record32 allocation0 and native replacement completion pass\n",
        transition,
        dropped);
}

void cancellation(const char* vs, const char* fs, bool sealed)
{
    StorageFixture s(vs, fs);
    auto& f = s.pass.graphics;
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    auto command = s.pass.command(128u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    if (sealed)
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const void> observed = binding;
    binding.reset();
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
    assert(argc == 3);
    negatives(argv[1], argv[2]);
    liveness(argv[1], argv[2]);
    for (const bool transition : {false, true})
        for (const bool dropped : {false, true})
            execute(argv[1], argv[2], transition, dropped);
    for (const bool sealed : {false, true})
        cancellation(argv[1], argv[2], sealed);
    std::puts(
        "ordinary Graphics storage: private direct read-only domain, actual VS/FS facts and data/lifetime; public versions0");
}
