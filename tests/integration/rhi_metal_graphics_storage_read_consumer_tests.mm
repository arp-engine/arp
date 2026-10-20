#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_read_consumer_fixture.h"
#include "rhi/backends/metal/metal_native_sync.h"

#import <Metal/Metal.h>
#ifdef NDEBUG
#error Read consumer tests require assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;

class ConsumerFixture final : public test::OrdinaryGraphicsReadConsumerFixture {
public:
    using OrdinaryGraphicsReadConsumerFixture::OrdinaryGraphicsReadConsumerFixture;

    auto command(
        std::uint32_t owners, std::uint32_t producerDraws = 1u, std::uint32_t consumerDraws = 1u)
    {
        auto& g = pass.graphics;
        return MetalRhiOrdinaryWriteCommand::prepare({1u, true, producerDraws, consumerDraws},
            "UniformShaderConsumer",
            g.registry,
            {1u, true, owners, 0u, 0u, 0u, g.cpuBytes},
            g.quota,
            g.diagnostic);
    }

    void producer(MetalRhiNativeCommandPayload& p, std::uint32_t draws = 1u)
    {
        auto& d = pass.graphics.diagnostic;
        assert(p.recordOrdinaryPreparedWriteRenderPassBegin(producerPass, write, d));
        for (unsigned i = 0u; i < draws; ++i)
            assert(p.recordDraw(3u, 1u, 0u, 0u, d));
        assert(p.recordRenderPassEnd(d));
    }

    void consumer(MetalRhiNativeCommandPayload& p,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer>& root,
        std::uint32_t draws = 1u)
    {
        auto& d = pass.graphics.diagnostic;
        assert(p.recordOrdinaryPreparedReadConsumerBegin(root, d));
        for (unsigned i = 0u; i < draws; ++i)
            assert(p.recordDraw(3u, 1u, 0u, 0u, d));
        assert(p.recordRenderPassEnd(d) && p.finish(d));
    }

    void words(bool written)
    {
        auto& g = pass.graphics;
        std::array<std::uint32_t, 4> output{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*buffer),
            output.data(),
            sizeof(output),
            0u,
            sizeof(output),
            g.diagnostic));
        assert(output[0] == (written ? write->proof().replacement : initial[0]));
        for (std::size_t i = 1u; i < output.size(); ++i)
            assert(output[i] == initial[i]);
    }
};

void preparation(const char* vs, const char* fs, const char* readVs, const char* readFs)
{
    test::OrdinaryGraphicsReadConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    const auto& fact = f.provider->native()->graphicsBufferFacts[1].entries[0];
    std::printf("Reader native ABI: access=%u type=%u span=%llu alignment=%llu\n",
        fact.access,
        fact.elementType,
        (unsigned long long)fact.requiredBytes,
        (unsigned long long)fact.alignment);
    auto root = f.prepareConsumer();
    if (!root)
        std::fprintf(stderr, "Read consumer prepare: %s\n", g.diagnostic.c_str());
    assert(root);
    assert(root->validateObservation().success
        && root->proof().replacement == f.write->proof().replacement);
    test::CpuQuotaFactoryFixture budget(g.quota);
    const auto factory = [&] {
        return f.prepareConsumer();
    };
    const auto peak = budget.measure(factory);
    budget.verify(peak, factory);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(g.quota, g.cpuBytes - g.quota->liveBytes());
        auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 1000u; ++i)
            assert(root->validateObservation().success);
        std::pmr::set_default_resource(previous);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void freshness(const char* vs, const char* fs, const char* readVs, const char* readFs)
{
    ConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    auto root = f.prepareConsumer();
    assert(root);
    auto command = f.command(128u);
    auto& p = *command->payload();
    f.producer(p);
    assert(p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
    const auto owners = p.retentionStorage()->resourceOwnerCount();
    const auto rejectBegin = [&] {
        assert(!p.recordOrdinaryPreparedReadConsumerBegin(root, g.diagnostic));
        assert(p.retentionStorage()->resourceOwnerCount() == owners
            && p.stats()->nativeViewportSetCount == 1u && command->commandCount() == 4u);
    };
    auto replacement = g.device->createBuffer({"ConsumerIdentityProbe",
        sizeof(f.initial),
        ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    const auto source = metalRhiNativePayloadFor(*f.buffer),
               other = metalRhiNativePayloadFor(*replacement);
    test::MetalNativeIdentityMutationProbe::verify(source->resource, other->resource, rejectBegin);
    source->resource.hazardsExplicitlyTracked = false;
    rejectBegin();
    source->resource.hazardsExplicitlyTracked = true;
    auto& facts = f.provider->native()->graphicsBufferFacts[1];
    const auto fact = facts.entries[0];
    facts.entries[0].access = MetalRhiAtomicBufferAbiValidator::readWriteAccess;
    rejectBegin();
    facts.entries[0] = fact;
    facts.entries[0].requiredBytes += 4u;
    rejectBegin();
    facts.entries[0] = fact;
    assert(p.recordOrdinaryPreparedReadConsumerBegin(root, g.diagnostic));
    const auto combined = p.retentionStorage()->resourceOwnerCount();
    const auto rejectDraw = [&] {
        assert(!p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic));
        assert(command->commandCount() == 5u
            && p.retentionStorage()->resourceOwnerCount() == combined);
    };
    test::MetalNativeIdentityMutationProbe::verify(source->resource, other->resource, rejectDraw);
    facts.entries[0].elementType = UINT32_MAX;
    rejectDraw();
    facts.entries[0] = fact;
    assert(p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic) && p.recordRenderPassEnd(g.diagnostic)
        && p.finish(g.diagnostic));
    command.reset();
    assert(f.buffer->state() == ResourceState::ShaderWrite);
    f.words(false);
    for (unsigned mode = 0u; mode < 3u; ++mode) {
        ConsumerFixture stale(vs, fs, readVs, readFs);
        auto r = stale.prepareConsumer();
        assert(r);
        auto c = stale.command(128u);
        auto& d = stale.pass.graphics.diagnostic;
        stale.producer(*c->payload());
        assert(c->payload()->recordOrdinaryPreparedWriteDependency(stale.write, d));
        if (!mode)
            assert(stale.set
                    ->write({3u,
                        DescriptorType::UniformBuffer,
                        stale.buffer->id(),
                        DescriptorResourceKind::Buffer})
                    .success);
        else if (mode == 1u)
            stale.pass.graphics.arena->reset();
        else
            stale.set.reset();
        const auto count = c->payload()->retentionStorage()->resourceOwnerCount();
        assert(!c->payload()->recordOrdinaryPreparedReadConsumerBegin(r, d));
        assert(c->payload()->retentionStorage()->resourceOwnerCount() == count
            && c->payload()->stats()->nativeViewportSetCount == 1u);
    }
    assert(!MetalRhiOrdinaryPreparedGraphicsReadConsumer::prepare(
               f.write, f.readBinding, f.producerPass, g.diagnostic)
        && g.diagnostic == "RHI-METAL-ORDINARY-READ-CONSUMER-OWNER-MISMATCH");
    ConsumerFixture sealed(vs, fs, readVs, readFs);
    auto r = sealed.prepareConsumer();
    assert(r);
    auto c = sealed.command(128u);
    sealed.producer(*c->payload());
    assert(c->payload()->recordOrdinaryPreparedWriteDependency(
        sealed.write, sealed.pass.graphics.diagnostic));
    sealed.buffer->sealWrites();
    assert(
        !c->payload()->recordOrdinaryPreparedReadConsumerBegin(r, sealed.pass.graphics.diagnostic));
}

void delayed(const char* vs, const char* fs, const char* readVs, const char* readFs, bool dropped)
{
    std::shared_ptr<core::CpuMemoryQuota> quota;
    @autoreleasepool {
        ConsumerFixture f(vs, fs, readVs, readFs);
        auto& g = f.pass.graphics;
        quota = g.quota;
        auto root = f.prepareConsumer();
        assert(root);
        auto command = f.command(128u);
        f.producer(*command->payload());
        assert(command->payload()->recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
        f.consumer(*command->payload(), root);
        auto replacement = g.device->createBuffer({"ConsumerCompletionReplacement",
            sizeof(f.initial),
            ResourceUsage::UniformBuffer | ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(replacement);
        auto& source = metalRhiNativePayloadFor(*f.buffer)->resource;
        std::weak_ptr<void> object = source.nativeObject, token = source.allocationReleaseToken;
        source.nativeObject = metalRhiNativePayloadFor(*replacement)->resource.nativeObject;
        source.allocationReleaseToken =
            metalRhiNativePayloadFor(*replacement)->resource.allocationReleaseToken;
        std::weak_ptr<const void> observed = root;
        std::weak_ptr<GpuBuffer> wrapper = f.buffer;
        auto gate = createMetalRhiNativeSharedEventPayload("ShaderConsumerDelayedCompletion");
        assert(gate && gate->nativeRuntimeBacked);
        auto done = command->payload()->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        root.reset();
        f.dropReaderInputs();
        assert(command->submit(queue, gate, 1u, g.diagnostic));
        if (dropped)
            command.reset();
        assert(!done->isComplete() && !observed.expired() && !wrapper.expired() && !object.expired()
            && !token.expired());
        assert(done->outcome() == MetalRhiCompletionState::Outcome::Pending);
        signalMetalRhiNativeSharedEvent(gate, 1u);
        done->wait();
        assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
        assert(observed.expired() && wrapper.expired() && object.expired() && token.expired());
        f.pass.verifyColor({0u, 255u, 0u, 255u});
        std::printf(
            "Shader consumer actual delayed completion dropped=%d: original resource/root/wrapper retirement PASS\n",
            dropped);
    }
    assert(!quota->liveBytes());
}

void cancellation(
    const char* vs, const char* fs, const char* readVs, const char* readFs, bool finished)
{
    ConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    auto root = f.prepareConsumer();
    assert(root);
    auto command = f.command(128u);
    auto& p = *command->payload();
    f.producer(p);
    assert(p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic)
        && p.recordOrdinaryPreparedReadConsumerBegin(root, g.diagnostic));
    if (finished)
        assert(p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic) && p.recordRenderPassEnd(g.diagnostic)
            && p.finish(g.diagnostic));
    auto heldBuffer = f.buffer;
    std::weak_ptr<const void> observed = root;
    const auto sourceId = heldBuffer->id();
    root.reset();
    f.dropReaderInputs();
    assert(!observed.expired());
    command.reset();
    assert(observed.expired() && heldBuffer->state() == ResourceState::ShaderWrite);
    observed.reset();
    heldBuffer.reset();
    assert(!g.registry->find(sourceId));
    f.pass.view.reset();
    f.pass.target.reset();
    g.registry.reset();
    g.device.reset();
    assert(!g.quota->liveBytes());
}

void submitSeal(const char* vs, const char* fs, const char* readVs, const char* readFs)
{
    ConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    auto root = f.prepareConsumer();
    assert(root);
    auto command = f.command(128u);
    f.producer(*command->payload());
    assert(command->payload()->recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
    f.consumer(*command->payload(), root);
    f.buffer->sealWrites();
    MetalRhiNativeQueueStats queue;
    assert(!command->submit(queue, {}, 0u, g.diagnostic) && !queue.nativeQueueSubmitCount);
    assert(g.diagnostic == "RHI-METAL-ORDINARY-WRITE-SUBMIT-STATE-OR-SEAL-MISMATCH");
    command.reset();
    assert(f.buffer->state() == ResourceState::ShaderWrite);
    f.words(false);
}

void precommitFailure(const char* vs, const char* fs, const char* readVs, const char* readFs)
{
    ConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    auto root = f.prepareConsumer();
    assert(root);
    auto command = f.command(128u);
    f.producer(*command->payload());
    assert(command->payload()->recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
    f.consumer(*command->payload(), root);
    auto gate = createMetalRhiNativeSharedEventPayload("ConsumerPrecommitQuotaFailure");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->payload()->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    {
        core::CpuMemoryQuotaLease pressure(g.quota, g.cpuBytes - g.quota->liveBytes());
        assert(!command->submit(queue, gate, 1u, g.diagnostic));
    }
    assert(!queue.nativeQueueSubmitCount && done->isComplete()
        && done->outcome() == MetalRhiCompletionState::Outcome::Failed);
    assert(f.buffer->state() == ResourceState::ShaderWrite);
    f.words(false);
    std::puts(
        "Actual command precommit quota rejection: Failed outcome, state rollback and no accepted GPU work PASS");
}

void execute(const char* vs, const char* fs, const char* readVs, const char* readFs)
{
    ConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    auto root = f.prepareConsumer();
    assert(root);
    auto command = f.command(128u, 32u, 32u);
    assert(command);
    auto& p = *command->payload();
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(g.quota, g.cpuBytes - g.quota->liveBytes());
        auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        f.producer(p, 32u);
        assert(p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
        f.consumer(p, root, 32u);
        std::pmr::set_default_resource(previous);
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
    }
    assert(command->commandCount() == 69u && f.buffer->state() == ResourceState::ShaderRead);
    auto done = p.preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, {}, 0u, g.diagnostic));
    done->wait();
    assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
    f.pass.verifyColor({0u, 255u, 0u, 255u});
    f.words(true);
    assert(!semantics::GraphicsStorageWriteLowering::current(Backend::Metal).executable
        && !g.device->capabilities().commandRetention.contractVersion);
    std::puts(
        "Actual same-command atomic producer/Fragment Uniform exact-word consumer: green pixels, value0x12345678, 3 guards, 32+32 draws allocation0 PASS; public versions0");
}

void mismatchControl(const char* vs, const char* fs, const char* readVs, const char* readFs)
{
    ConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    auto buffer = g.device->createBuffer({"AuthoredUniformMismatch",
        4u,
        ResourceUsage::UniformBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(buffer);
    const std::uint32_t mismatch = 0x12345679u;
    assert(g.device->writeBuffer(*buffer, &mismatch, sizeof(mismatch)).success);
    buffer->sealWrites();
    assert(f.set
            ->write(
                {3u, DescriptorType::UniformBuffer, buffer->id(), DescriptorResourceKind::Buffer})
            .success);
    auto binding = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
        f.provider, *f.set, 0u, g.registry, g.budget(), g.quota, g.diagnostic);
    assert(binding);
    auto command = f.pass.command(128u);
    auto& d = g.diagnostic;
    assert(command->recordOrdinaryPreparedRenderPassBegin(f.readPass, binding, d)
        && command->recordDraw(3u, 1u, 0u, 0u, d) && command->recordRenderPassEnd(d)
        && command->finish(d));
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, {}, 0u, {}, 0u, d));
    done->wait();
    f.pass.verifyColor({255u, 0u, 0u, 255u});
    std::puts(
        "Actual selected consumer rejects word0x12345679 with red pixels; equality is full-word, not nonzero PASS");
}

void admissionAndOrder(const char* vs, const char* fs, const char* readVs, const char* readFs)
{
    ConsumerFixture f(vs, fs, readVs, readFs);
    auto& g = f.pass.graphics;
    auto root = f.prepareConsumer();
    assert(root);
    std::uint32_t begin, combined;
    {
        auto probe = f.command(128u);
        f.producer(*probe->payload());
        begin = probe->payload()->retentionStorage()->resourceOwnerCount();
        assert(probe->payload()->recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
        f.consumer(*probe->payload(), root);
        combined = probe->payload()->retentionStorage()->resourceOwnerCount();
        assert(combined > begin);
    }
    assert(f.buffer->state() == ResourceState::ShaderWrite);
    {
        auto less = f.command(begin - 1u);
        assert(!less->payload()->recordOrdinaryPreparedWriteRenderPassBegin(
            f.producerPass, f.write, g.diagnostic));
        assert(!less->payload()->retentionStorage()->resourceOwnerCount()
            && !less->payload()->stats()->nativeRenderEncoderCreated);
    }
    for (const bool exact : {false, true}) {
        auto command = f.command(exact ? combined : combined - 1u);
        auto& p = *command->payload();
        assert(!p.recordOrdinaryPreparedReadConsumerBegin(root, g.diagnostic));
        f.producer(p);
        assert(!p.recordOrdinaryPreparedReadConsumerBegin(root, g.diagnostic));
        auto other = f.prepare();
        assert(other);
        assert(p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
        auto wrong = MetalRhiOrdinaryPreparedGraphicsReadConsumer::prepare(
            other, f.readBinding, f.readPass, g.diagnostic);
        assert(wrong && !p.recordOrdinaryPreparedReadConsumerBegin(wrong, g.diagnostic));
        assert(p.recordOrdinaryPreparedReadConsumerBegin(root, g.diagnostic) == exact);
        if (!exact)
            assert(g.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                && p.retentionStorage()->resourceOwnerCount() == begin
                && p.stats()->nativeViewportSetCount == 1u && command->commandCount() == 4u);
        else {
            assert(!p.recordRenderPassEnd(g.diagnostic)
                && g.diagnostic == "RHI-GRAPHICS-WRITE-SEQUENCE-CONSUMER-DRAW-REQUIRED");
            assert(!p.recordOrdinaryPreparedGraphicsBinding(f.readBinding, g.diagnostic));
            assert(!p.recordSetViewport({0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f}, g.diagnostic)
                && !p.recordSetScissor({0, 0, 4u, 4u}, g.diagnostic)
                && p.stats()->nativeViewportSetCount == 2u);
            assert(p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic));
            assert(!p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic)
                && g.diagnostic == "RHI-METAL-ORDINARY-READ-CONSUMER-DRAW-BUDGET");
            assert(p.recordRenderPassEnd(g.diagnostic) && command->commandCount() == 7u
                && p.finish(g.diagnostic));
            assert(!p.recordOrdinaryPreparedReadConsumerBegin(root, g.diagnostic));
        }
    }
    assert(f.buffer->state() == ResourceState::ShaderWrite);
    f.words(false);
    std::printf(
        "Shader consumer actual atomic owner union begin=%u combined=%u; exact/one-less/order/budget/cancel PASS\n",
        begin,
        combined);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 7);
    @autoreleasepool {
        preparation(argv[1], argv[2], argv[3], argv[4]);
        test::OrdinaryGraphicsReadConsumerFixture storage(
            argv[1], argv[2], argv[5], argv[6], false);
        assert(!storage.prepareConsumer()
            && storage.pass.graphics.diagnostic == "RHI-METAL-ORDINARY-READ-CONSUMER-ABI-DOMAIN");
        admissionAndOrder(argv[1], argv[2], argv[3], argv[4]);
        freshness(argv[1], argv[2], argv[3], argv[4]);
        for (const bool finished : {false, true})
            cancellation(argv[1], argv[2], argv[3], argv[4], finished);
        for (const bool dropped : {false, true})
            delayed(argv[1], argv[2], argv[3], argv[4], dropped);
        submitSeal(argv[1], argv[2], argv[3], argv[4]);
        precommitFailure(argv[1], argv[2], argv[3], argv[4]);
        mismatchControl(argv[1], argv[2], argv[3], argv[4]);
        execute(argv[1], argv[2], argv[3], argv[4]);
    }
}
