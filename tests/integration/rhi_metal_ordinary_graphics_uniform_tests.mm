#include "../fixtures/metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/gpu_extensions.h"

#include <Metal/Metal.h>
#include <cstring>

#ifdef NDEBUG
#error Native uniform tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;

class UniformFixture final {
public:
    static DescriptorLayoutDesc layout(bool dynamic, bool depthOnly = false)
    {
        DescriptorLayoutDesc inputs;
        inputs.name = "OrdinaryGraphicsUniforms";
        // Offset order is by logical binding, independently of authored order.
        inputs.bindings = {
            {8u, DescriptorType::UniformBuffer, ShaderStage::Fragment, 1u, false, dynamic},
            {3u,
                DescriptorType::UniformBuffer,
                ShaderStage::Vertex | ShaderStage::Fragment,
                1u,
                false,
                dynamic}};
        if (depthOnly)
            inputs.bindings = {
                {3u, DescriptorType::UniformBuffer, ShaderStage::Vertex, 1u, false, dynamic}};
        return inputs;
    }

    UniformFixture(const char* vs, const char* fs, bool dynamic = true, bool depthOnly = false)
        : pass(vs, depthOnly ? nullptr : fs, false, layout(dynamic, depthOnly)), dynamic(dynamic),
          depthOnly(depthOnly)
    {
        const auto* indexed = static_cast<BindlessCrossExtension*>(
            f().device->queryInterface(kExtensionBindlessCross));
        assert(indexed && indexed->capabilities().preparedGraphicsBindingContractVersion == 0u);
        const std::array<std::uint32_t, 2> keys{3u, 8u};
        for (unsigned i = 0; i < (depthOnly ? 1u : 2u); ++i) {
            MetalRhiNativeArgumentBindingPayload logical;
            logical.binding = keys[i];
            logical.type = DescriptorType::UniformBuffer;
            logical.stages = depthOnly ? ShaderStage::Vertex
                : i                    ? ShaderStage::Fragment
                                       : ShaderStage::Vertex | ShaderStage::Fragment;
            logical.resourceKind = DescriptorResourceKind::Buffer;
            logical.argumentKind = MetalRhiArgumentKind::Buffer;
            std::uint64_t alignment = 0u, span = 0u;
            for (unsigned stage = 0; stage < 2; ++stage) {
                if (!f().provider->stages()[stage])
                    continue;
                MetalRhiPreparedBindingLocation location;
                assert(
                    f().provider->stages()[stage]
                        ->resolve(
                            logical, 0u, f().provider->native()->nativeBindingStages, location)
                        .success);
                if (!location.active)
                    continue;
                assert(!location.array);
                const auto& facts = f().provider->native()->graphicsBufferFacts[stage];
                const MetalRhiBufferBindingFact* fact = nullptr;
                assert(!validateMetalRhiBufferOffset(
                    facts.entries, facts.version, location.slot, 0u, UINT64_MAX, fact));
                alignment = std::max(alignment, fact->alignment);
                span = std::max(span, fact->requiredBytes);
                std::printf(
                    "ordinary Graphics uniform: logical=%u stage=%u physical=%u align=%llu span=%llu\n",
                    keys[i],
                    stage,
                    location.slot,
                    static_cast<unsigned long long>(fact->alignment),
                    static_cast<unsigned long long>(fact->requiredBytes));
            }
            const auto expectedBytes = i ? sizeof(fragmentA) : sizeof(sharedA);
            assert(span == expectedBytes && alignment);
            const auto aligned = core::alignCpuAllocationOffsetUp(span, alignment);
            assert(aligned.success && aligned.alignedValue <= UINT32_MAX / 2u);
            offsetsA[i] = static_cast<std::uint32_t>(aligned.alignedValue);
            offsetsB[i] = offsetsA[i] * 2u;
            buffers[i] = f().device->createBuffer({"OrdinaryGraphicsUniform",
                offsetsB[i] + span,
                ResourceUsage::UniformBuffer,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead});
            assert(buffers[i]);
            core::pmr::Vector<std::uint8_t> data(
                buffers[i]->desc().sizeBytes, 0u, &core::defaultCpuMemoryResource());
            std::memcpy(data.data(), i ? fragmentA.data() : sharedZero.data(), expectedBytes);
            std::memcpy(
                data.data() + offsetsA[i], i ? fragmentA.data() : sharedA.data(), expectedBytes);
            std::memcpy(
                data.data() + offsetsB[i], i ? fragmentB.data() : sharedB.data(), expectedBytes);
            assert(f().device->writeBuffer(*buffers[i], data.data(), data.size()).success);
            assert(
                f().set
                    ->write({keys[i],
                        DescriptorType::UniformBuffer,
                        buffers[i]->id(),
                        DescriptorResourceKind::Buffer})
                    .success);
        }
    }

    test::OrdinaryGraphicsFixture& f() { return pass.graphics; }

    auto prepare(const std::array<std::uint32_t, 2>& offsets)
    {
        return f().prepare(
            dynamic ? offsets.data() : nullptr, dynamic ? (depthOnly ? 1u : 2u) : 0u);
    }

    void reject(const std::uint32_t* offsets, std::uint32_t count, const char* expected)
    {
        const auto before = f().quota->liveBytes();
        auto result = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(f().provider,
            *f().set,
            0u,
            f().registry,
            f().budget(),
            f().quota,
            f().diagnostic,
            offsets,
            count);
        if (result || f().diagnostic.find(expected) == f().diagnostic.npos)
            std::fprintf(stderr, "Expected %s: %s\n", expected, f().diagnostic.c_str());
        assert(!result && f().diagnostic.find(expected) != f().diagnostic.npos
            && f().quota->liveBytes() == before);
    }

    void verifyPixels(const std::array<std::uint8_t, 4>& expected)
    {
        if (depthOnly)
            pass.verifyDepth(0.0f);
        else
            pass.verifyColor(expected);
    }

    test::OrdinaryGraphicsPassFixture pass;
    bool dynamic, depthOnly;
    std::array<core::UniquePtr<GpuBuffer>, 2> buffers;
    std::array<std::uint32_t, 2> offsetsA{}, offsetsB{};
    static constexpr float rgba8Maximum = UINT8_MAX;
    const std::array<float, 8> sharedZero{1, 0, 0, 0, 1, 0, 0, 1},
        sharedA{1, 0, 0, 0, 64.0f / rgba8Maximum, 128.0f / rgba8Maximum, 192.0f / rgba8Maximum, 1},
        sharedB{1, 0, 0, 0, 192.0f / rgba8Maximum, 64.0f / rgba8Maximum, 128.0f / rgba8Maximum, 1};
    const std::array<float, 4> fragmentA{0, 0, 0, 0}, fragmentB{0, 64.0f / rgba8Maximum, 0, 0};
};

void negatives(const char* vs, const char* fs)
{
    UniformFixture u(vs, fs);
    auto& f = u.f();
    u.reject(nullptr, 2u, "OFFSET-COUNT");
    u.reject(u.offsetsA.data(), 1u, "OFFSET-COUNT");
    auto offsets = u.offsetsA;
    ++offsets[0];
    u.reject(offsets.data(), 2u, "OFFSET-ALIGNMENT");
    offsets = u.offsetsA;
    offsets[0] = static_cast<std::uint32_t>(u.buffers[0]->desc().sizeBytes);
    u.reject(offsets.data(), 2u, "OFFSET-RANGE");
    auto& facts = f.provider->native()->graphicsBufferFacts[1];
    const auto originalStage = facts.stage;
    facts.stage = ShaderStage::Compute;
    u.reject(u.offsetsA.data(), 2u, "FACTS-STAGE-MISMATCH");
    facts.stage = originalStage;
    facts.version = 0u;
    u.reject(u.offsetsA.data(), 2u, "FACTS-MISSING");
    facts.version = 1u;
    assert(!facts.entries.empty());
    const auto saved = facts.entries[0];
    facts.entries[0].slot = UINT32_MAX;
    u.reject(u.offsetsA.data(), 2u, "FACTS-MISSING");
    facts.entries[0] = saved;
    facts.entries[0].requiredBytes = 0u;
    u.reject(u.offsetsA.data(), 2u, "FACTS-MISSING");
    facts.entries[0] = saved;
    facts.entries[0].alignment = 0u;
    u.reject(u.offsetsA.data(), 2u, "FACTS-MISSING");
    facts.entries[0] = saved;
    facts.entries.push_back(saved);
    u.reject(u.offsetsA.data(), 2u, "FACTS-DUPLICATE");
    facts.entries.pop_back();
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        u.reject(u.offsetsA.data(), 2u, "ALLOCATION-FAILED");
    }
    auto binding = u.prepare(u.offsetsA);
    assert(binding->validateObservation().success);
    auto replacement = f.device->createBuffer({"UniformNativeReplacement",
        u.buffers[0]->desc().sizeBytes,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    auto payload = metalRhiNativePayloadFor(*u.buffers[0]);
    auto other = metalRhiNativePayloadFor(*replacement);
    const auto nativeObject = payload->resource.nativeObject;
    const auto allocation = payload->resource.allocationReleaseToken;
    payload->resource.nativeObject = other->resource.nativeObject;
    assert(binding->validateObservation().diagnostics.find("NATIVE-IDENTITY-MISMATCH")
        != f.diagnostic.npos);
    payload->resource.nativeObject = nativeObject;
    payload->resource.allocationReleaseToken = other->resource.allocationReleaseToken;
    assert(binding->validateObservation().diagnostics.find("NATIVE-IDENTITY-MISMATCH")
        != f.diagnostic.npos);
    payload->resource.allocationReleaseToken = allocation;
    assert(binding->validateObservation().success);
    assert(f.set
            ->write({3u,
                DescriptorType::UniformBuffer,
                u.buffers[0]->id(),
                DescriptorResourceKind::Buffer})
            .success);
    assert(!binding->validateObservation().success);
    binding = u.prepare(u.offsetsA);
    u.buffers[0].reset();
    assert(!binding->validateObservation().success);
}

void execute(const char* vs, const char* fs, unsigned selection, bool dropCommand)
{
    UniformFixture u(vs, fs, selection != 0u, selection == 3u);
    auto& f = u.f();
    auto input = selection == 2u ? u.offsetsB : u.offsetsA;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto binding = u.prepare(input);
    std::pmr::set_default_resource(previous);
    input = {UINT32_MAX, UINT32_MAX};
    auto pass = u.pass.prepare();
    assert(pass->native().sampleCount == u.pass.target->desc().sampleCount);
    if (u.depthOnly)
        assert(pass->native().colorCount == 0u
            && pass->native().depthFormat == TextureFormat::D32Float);
    auto probe = u.pass.command(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    const auto owners = probe->retentionStorage()->resourceOwnerCount();
    assert(owners > 1u && owners < 128u);
    assert(probe->recordRenderPassEnd(f.diagnostic));
    probe->abandonUnsubmitted();
    probe.reset();
    auto shortCommand = u.pass.command(owners - 1u);
    assert(!shortCommand->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    assert(!shortCommand->stats()->nativeRenderEncoderCreated
        && !shortCommand->retentionStorage()->resourceOwnerCount());
    shortCommand->abandonUnsubmitted();
    shortCommand.reset();
    auto command = u.pass.command(owners);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (unsigned i = 0; i < 32; ++i)
            assert(command->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto descriptors = std::const_pointer_cast<MetalRhiNativeDescriptorGroupPayload>(
            binding->payload()->descriptors);
        auto& element = descriptors->bindings.front();
        const auto captured = element.graphicsBufferRanges;
        const auto offset = element.bufferOffsetBytes;
        element.graphicsBufferRanges[u.depthOnly ? 0u : 1u].offset = UINT64_MAX;
        assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        element.graphicsBufferRanges = captured;
        element.bufferOffsetBytes = offset ? 0u : u.offsetsA[1];
        assert(!command->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic));
        element.bufferOffsetBytes = offset;
        auto& facts = f.provider->native()->graphicsBufferFacts[u.depthOnly ? 0u : 1u];
        const auto version = facts.version;
        facts.version = 0u;
        assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        facts.version = version;
        assert(command->retentionStorage()->resourceOwnerCount() == owners);
        // Existing owning ValidationResult diagnostics may allocate on rejected
        // observations; successful begin/rebind and draw/end are measured separately.
        const auto retryAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
            && command->recordRenderPassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == retryAllocations);
        std::pmr::set_default_resource(previous);
    }
    assert(command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed = binding;
    std::weak_ptr<MetalRhiNativeBufferPayload> resource = metalRhiNativePayloadFor(*u.buffers[0]);
    binding.reset();
    pass.reset();
    f.set.reset();
    f.arena.reset();
    f.provider.reset();
    for (auto& buffer : u.buffers)
        buffer.reset();
    assert(!observed.expired() && !resource.expired());
    auto gate = createMetalRhiNativeSharedEventPayload("UniformDelayedCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    MetalRhiNativeQueueStats queue;
    auto done = command->preparedCompletionState();
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    if (dropCommand)
        command.reset();
    assert(!done->isComplete() && !observed.expired() && !resource.expired());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    assert(observed.expired() && resource.expired());
    u.verifyPixels(selection == 0u ? std::array<std::uint8_t, 4>{255, 0, 0, 255}
            : selection == 1u      ? std::array<std::uint8_t, 4>{64, 128, 192, 255}
                                   : std::array<std::uint8_t, 4>{192, 128, 128, 255});
    std::printf(
        "ordinary Graphics uniform selection=%u dropped=%d owners=%zu; exact 4x4 output, allocation0 and native retirement pass\n",
        selection,
        dropCommand,
        owners);
}

void cancellation(const char* vs, const char* fs, bool sealed)
{
    UniformFixture u(vs, fs);
    auto& f = u.f();
    auto binding = u.prepare(u.offsetsA);
    auto pass = u.pass.prepare();
    auto command = u.pass.command(128u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    if (sealed)
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> observed = binding;
    std::weak_ptr<MetalRhiNativeBufferPayload> resource = metalRhiNativePayloadFor(*u.buffers[0]);
    binding.reset();
    pass.reset();
    f.set.reset();
    f.provider.reset();
    for (auto& buffer : u.buffers)
        buffer.reset();
    assert(!observed.expired() && !resource.expired());
    command->abandonUnsubmitted();
    assert(observed.expired() && resource.expired());
}

void transition(const char* vs, const char* fs, bool admitted)
{
    UniformFixture u(vs, fs);
    auto& f = u.f();
    auto first = u.prepare(u.offsetsA);
    auto second = u.prepare(u.offsetsB);
    auto pass = u.pass.prepare();
    auto probe = u.pass.command(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedGraphicsBinding(second, f.diagnostic));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    assert(combined > initial);
    assert(probe->recordRenderPassEnd(f.diagnostic));
    probe->abandonUnsubmitted();
    probe.reset();
    auto command = u.pass.command(admitted ? combined : combined - 1u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, first, f.diagnostic));
    assert(command->recordOrdinaryPreparedGraphicsBinding(second, f.diagnostic) == admitted);
    assert(command->retentionStorage()->resourceOwnerCount() == (admitted ? combined : initial));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0; i < 32; ++i)
            assert(command->recordOrdinaryPreparedGraphicsBinding(
                admitted ? second : first, f.diagnostic));
        assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
            && command->recordRenderPassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    assert(command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> a = first, b = second;
    first.reset();
    second.reset();
    assert(!a.expired() && (admitted ? !b.expired() : b.expired()));
    MetalRhiNativeQueueStats queue;
    auto done = command->preparedCompletionState();
    assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
    done->wait();
    assert(a.expired() && b.expired());
    u.verifyPixels(admitted ? std::array<std::uint8_t, 4>{192, 128, 128, 255}
                            : std::array<std::uint8_t, 4>{64, 128, 192, 255});
    std::printf(
        "ordinary Graphics uniform transition admitted=%d initial=%zu combined=%zu; rejected B preserves native A or accepted B updates exact output\n",
        admitted,
        initial,
        combined);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    @autoreleasepool {
        negatives(argv[1], argv[2]);
        for (unsigned selection = 0; selection < 4; ++selection)
            for (const bool dropped : {false, true})
                execute(argv[1], argv[2], selection, dropped);
        for (const bool sealed : {false, true})
            cancellation(argv[1], argv[2], sealed);
        for (const bool admitted : {false, true})
            transition(argv[1], argv[2], admitted);
    }
    std::puts(
        "ordinary Graphics direct uniforms: independent selected-stage facts, sorted copied offsets, native data/lifetime and fail-closed range gates pass; public versions0");
}
