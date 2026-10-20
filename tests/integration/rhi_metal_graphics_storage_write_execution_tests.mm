#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_write_fixture.h"
#include "rhi/backends/metal/metal_native_sync.h"

#import <Metal/Metal.h>
#include <cstdio>

#ifdef NDEBUG
#error Writable execution requires assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
using Readback = MetalRhiOrdinaryPreparedWriteReadback;

class ExecutionFixture final : public test::OrdinaryGraphicsWriteFixture {
public:
    ExecutionFixture(const char* vs, const char* fs)
        : OrdinaryGraphicsWriteFixture(vs, fs, Consumer::Transfer)
    {
        buffer->sealHostWrites();
        write = prepare();
        assert(write);
        passRoot = pass.prepare();
    }

    auto readback()
    {
        auto& f = pass.graphics;
        auto result = Readback::prepare(write, *f.device, f.diagnostic);
        if (!result)
            std::fprintf(stderr, "Readback prepare: %s\n", f.diagnostic.c_str());
        assert(result);
        return result;
    }

    void producer(MetalRhiNativeCommandPayload& payload, std::uint32_t draws = 1u)
    {
        auto& d = pass.graphics.diagnostic;
        const auto began = payload.recordOrdinaryPreparedWriteRenderPassBegin(passRoot, write, d);
        if (!began)
            std::fprintf(stderr, "Begin: %s\n", d.c_str());
        assert(began);
        assert(payload.stats()->nativeRenderEncoderCreated);
        for (unsigned i = 0; i < draws; ++i)
            assert(payload.recordDraw(3u, 1u, 0u, 0u, d));
        assert(payload.recordRenderPassEnd(d));
    }

    void finish(MetalRhiNativeCommandPayload& payload, const std::shared_ptr<Readback>& result)
    {
        auto& d = pass.graphics.diagnostic;
        assert(payload.recordOrdinaryPreparedWriteDependency(write, d));
        assert(payload.recordOrdinaryPreparedWriteReadback(result, d));
        assert(payload.finish(d));
    }

    void verifyGuards()
    {
        std::array<std::uint32_t, 4> words{};
        auto& f = pass.graphics;
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*buffer),
            words.data(),
            sizeof(words),
            0u,
            sizeof(words),
            f.diagnostic));
        assert(words[0] == write->proof().replacement);
        for (std::size_t i = 1; i < words.size(); ++i)
            assert(words[i] == initial[i]);
    }

    void verifyUnchanged()
    {
        std::array<std::uint32_t, 4> words{};
        auto& g = pass.graphics;
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*buffer),
            words.data(),
            sizeof(words),
            0u,
            sizeof(words),
            g.diagnostic));
        assert(words == initial);
    }

    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> write;
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> passRoot;
};

void admission(const char* vs, const char* fs)
{
    ExecutionFixture f(vs, fs);
    auto& g = f.pass.graphics;
    std::uint32_t beginOwners, allOwners;
    {
        auto probe = f.command(128u);
        auto result = f.readback();
        f.producer(*probe->payload());
        beginOwners = probe->payload()->retentionStorage()->resourceOwnerCount();
        f.finish(*probe->payload(), result);
        allOwners = probe->payload()->retentionStorage()->resourceOwnerCount();
        assert(allOwners > beginOwners && beginOwners > 1u);
        probe.reset();
        assert(result->status() == Readback::Status::Abandoned
            && f.buffer->state() == ResourceState::ShaderWrite);
    }
    {
        auto command = f.command(beginOwners - 1u);
        auto& p = *command->payload();
        assert(!p.recordOrdinaryPreparedWriteRenderPassBegin(f.passRoot, f.write, g.diagnostic));
        assert(g.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && !p.stats()->nativeRenderEncoderCreated && !p.retentionStorage()->resourceOwnerCount()
            && !command->commandCount());
        core::MutationJournal other;
        assert(canPrepareMetalRhiRecordedState(*f.buffer, other));
    }
    for (const bool exact : {false, true}) {
        auto command = f.command(exact ? allOwners : allOwners - 1u);
        auto result = f.readback();
        auto& p = *command->payload();
        f.producer(p);
        assert(p.retentionStorage()->resourceOwnerCount() == beginOwners);
        assert(p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
        assert(p.recordOrdinaryPreparedWriteReadback(result, g.diagnostic) == exact);
        if (!exact) {
            assert(g.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                && !p.stats()->nativeBlitEncoderCreated && !result->recorded()
                && p.retentionStorage()->resourceOwnerCount() == beginOwners);
            core::MutationJournal other;
            assert(canPrepareMetalRhiRecordedState(result->resource(), other));
            assert(!p.finish(g.diagnostic));
        } else
            assert(
                p.retentionStorage()->resourceOwnerCount() == allOwners && p.finish(g.diagnostic));
        command.reset();
        assert(f.buffer->state() == ResourceState::ShaderWrite);
        assert(
            result->status() == (exact ? Readback::Status::Abandoned : Readback::Status::Pending));
    }
    f.verifyUnchanged();
    std::printf(
        "Writable atomic owner admission: begin=%u combined=%u; exact/one-less before encoder/journal mutation PASS\n",
        beginOwners,
        allOwners);
}

void sequence(const char* vs, const char* fs)
{
    ExecutionFixture f(vs, fs);
    auto& g = f.pass.graphics;
    auto command = f.command(128u);
    auto result = f.readback();
    auto& p = *command->payload();
    assert(!p.finish(g.diagnostic)
        && !p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic)
        && !p.recordOrdinaryPreparedWriteReadback(result, g.diagnostic));
    assert(!command->commandCount() && !p.retentionStorage()->resourceOwnerCount());
    assert(p.recordOrdinaryPreparedWriteRenderPassBegin(f.passRoot, f.write, g.diagnostic));
    const auto owners = p.retentionStorage()->resourceOwnerCount();
    assert(!p.recordRenderPassEnd(g.diagnostic)
        && g.diagnostic == "RHI-GRAPHICS-WRITE-SEQUENCE-DRAW-REQUIRED");
    assert(!p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic)
        && !p.recordOrdinaryPreparedWriteReadback(result, g.diagnostic)
        && !p.recordOrdinaryPreparedWriteRenderPassBegin(f.passRoot, f.write, g.diagnostic)
        && !p.recordOrdinaryPreparedGraphicsBinding(f.binding, g.diagnostic));
    assert(command->commandCount() == 1u && p.retentionStorage()->resourceOwnerCount() == owners);
    assert(p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic));
    assert(!p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic)
        && g.diagnostic == "RHI-METAL-ORDINARY-WRITE-DRAW-BUDGET");
    assert(command->commandCount() == 2u && p.recordRenderPassEnd(g.diagnostic));
    assert(!p.recordOrdinaryPreparedWriteReadback(result, g.diagnostic));
    auto other = f.prepare();
    assert(other && other != f.write);
    assert(!p.recordOrdinaryPreparedWriteDependency(other, g.diagnostic));
    auto wrongResult = Readback::prepare(other, *g.device, g.diagnostic);
    assert(wrongResult);
    assert(p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
    assert(!p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic)
        && !p.recordOrdinaryPreparedWriteReadback(wrongResult, g.diagnostic));
    assert(p.recordOrdinaryPreparedWriteReadback(result, g.diagnostic));
    assert(!p.recordOrdinaryPreparedWriteReadback(result, g.diagnostic)
        && command->commandCount() == 5u);
    assert(p.finish(g.diagnostic));
    command.reset();
    assert(result->status() == Readback::Status::Abandoned
        && f.buffer->state() == ResourceState::ShaderWrite);
    auto retry = f.command(128u);
    f.producer(*retry->payload());
    assert(retry->payload()->recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
    assert(!retry->payload()->recordOrdinaryPreparedWriteReadback(result, g.diagnostic));
    assert(!retry->payload()->stats()->nativeBlitEncoderCreated);
    retry.reset();
    f.verifyUnchanged();
}

void freshness(const char* vs, const char* fs)
{
    ExecutionFixture f(vs, fs);
    auto& g = f.pass.graphics;
    auto command = f.command(128u);
    auto result = f.readback();
    auto& p = *command->payload();
    auto replacement = g.device->createBuffer({"WritableIdentityProbe",
        sizeof(f.initial),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderWrite});
    assert(replacement);
    const auto rejectBegin = [&] {
        assert(!p.recordOrdinaryPreparedWriteRenderPassBegin(f.passRoot, f.write, g.diagnostic));
        assert(!p.stats()->nativeRenderEncoderCreated && !p.retentionStorage()->resourceOwnerCount()
            && !command->commandCount());
    };
    core::MutationJournal conflict;
    assert(prepareMetalRhiRecordedState(*f.buffer, conflict));
    rejectBegin();
    assert(g.diagnostic == "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
    conflict.rollback();
    const auto native = metalRhiNativePayloadFor(*f.buffer),
               other = metalRhiNativePayloadFor(*replacement);
    test::MetalNativeIdentityMutationProbe::verify(native->resource, other->resource, rejectBegin);
    native->resource.hazardsExplicitlyTracked = false;
    rejectBegin();
    native->resource.hazardsExplicitlyTracked = true;
    const auto sourceFacts = g.provider->native()->graphicsBufferFacts[1].entries[0];
    g.provider->native()->graphicsBufferFacts[1].entries[0].requiredBytes += 4u;
    rejectBegin();
    g.provider->native()->graphicsBufferFacts[1].entries[0] = sourceFacts;
    assert(p.recordOrdinaryPreparedWriteRenderPassBegin(f.passRoot, f.write, g.diagnostic));
    const auto owners = p.retentionStorage()->resourceOwnerCount();
    const auto rejectDraw = [&] {
        assert(!p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic));
        assert(
            command->commandCount() == 1u && p.retentionStorage()->resourceOwnerCount() == owners);
    };
    test::MetalNativeIdentityMutationProbe::verify(native->resource, other->resource, rejectDraw);
    assert(p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic) && p.recordRenderPassEnd(g.diagnostic));
    assert(p.recordOrdinaryPreparedWriteDependency(f.write, g.diagnostic));
    const auto rejectCopy = [&] {
        assert(!p.recordOrdinaryPreparedWriteReadback(result, g.diagnostic));
        assert(!p.stats()->nativeBlitEncoderCreated && !result->recorded()
            && command->commandCount() == 4u
            && p.retentionStorage()->resourceOwnerCount() == owners);
    };
    auto& output = const_cast<GpuBuffer&>(result->resource());
    assert(prepareMetalRhiRecordedState(output, conflict));
    rejectCopy();
    conflict.rollback();
    const auto outNative = metalRhiNativePayloadFor(output);
    test::MetalNativeIdentityMutationProbe::verify(native->resource, other->resource, rejectCopy);
    test::MetalNativeIdentityMutationProbe::verify(
        outNative->resource, other->resource, rejectCopy);
    outNative->resource.hazardsExplicitlyTracked = false;
    rejectCopy();
    outNative->resource.hazardsExplicitlyTracked = true;
    output.sealWrites();
    rejectCopy();
    command.reset();
    assert(f.buffer->state() == ResourceState::ShaderWrite
        && result->status() == Readback::Status::Pending);
    f.verifyUnchanged();
    for (unsigned mode = 0; mode < 3; ++mode) {
        ExecutionFixture stale(vs, fs);
        auto& s = stale.pass.graphics;
        auto c = stale.command(128u);
        auto& q = *c->payload();
        if (!mode)
            assert(s.set
                    ->write({3u,
                        DescriptorType::StorageBuffer,
                        stale.buffer->id(),
                        DescriptorResourceKind::Buffer})
                    .success);
        else if (mode == 1u)
            s.arena->reset();
        else
            s.set.reset();
        assert(!q.recordOrdinaryPreparedWriteRenderPassBegin(
            stale.passRoot, stale.write, s.diagnostic));
        assert(
            !q.stats()->nativeRenderEncoderCreated && !q.retentionStorage()->resourceOwnerCount());
    }
    ExecutionFixture sealed(vs, fs);
    auto c = sealed.command(128u);
    sealed.buffer->sealWrites();
    assert(!c->payload()->recordOrdinaryPreparedWriteRenderPassBegin(
        sealed.passRoot, sealed.write, sealed.pass.graphics.diagnostic));
    assert(!c->payload()->retentionStorage()->resourceOwnerCount());
}

void cancellation(const char* vs, const char* fs, bool sealed)
{
    ExecutionFixture f(vs, fs);
    auto& g = f.pass.graphics;
    auto command = f.command(128u);
    auto result = f.readback();
    auto& p = *command->payload();
    assert(p.recordOrdinaryPreparedWriteRenderPassBegin(f.passRoot, f.write, g.diagnostic));
    if (sealed) {
        assert(p.recordDraw(3u, 1u, 0u, 0u, g.diagnostic) && p.recordRenderPassEnd(g.diagnostic));
        f.finish(p, result);
    }
    std::weak_ptr<const void> root = f.write;
    const auto sourceId = f.buffer->id(), outputId = result->resource().id();
    f.write.reset();
    f.passRoot.reset();
    result.reset();
    f.dropInputs();
    assert(!root.expired());
    command.reset();
    assert(root.expired());
    root.reset();
    assert(!g.registry->find(sourceId) && !g.registry->find(outputId));
    // Device-owned registration/telemetry control blocks can retain cold quota
    // charges. Their last weak owners retire with the device's actual objects.
    f.pass.view.reset();
    f.pass.target.reset();
    g.registry.reset();
    g.device.reset();
    assert(!g.quota->liveBytes());
}

void delayed(const char* vs, const char* fs, bool dropped)
{
    std::shared_ptr<core::CpuMemoryQuota> observedQuota;
    @autoreleasepool {
        ExecutionFixture f(vs, fs);
        auto& g = f.pass.graphics;
        auto command = f.command(128u);
        auto result = f.readback();
        observedQuota = g.quota;
        f.producer(*command->payload());
        f.finish(*command->payload(), result);
        auto replacement = g.device->createBuffer({"CompletionReplacement",
            sizeof(f.initial),
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});
        assert(replacement);
        std::array<std::weak_ptr<void>, 2> objects, tokens;
        for (std::size_t i = 0; i < 2; ++i) {
            auto& resource = metalRhiNativePayloadFor(i ? result->resource() : *f.buffer)->resource;
            objects[i] = resource.nativeObject;
            tokens[i] = resource.allocationReleaseToken;
            resource.nativeObject = metalRhiNativePayloadFor(*replacement)->resource.nativeObject;
            resource.allocationReleaseToken =
                metalRhiNativePayloadFor(*replacement)->resource.allocationReleaseToken;
        }
        std::weak_ptr<const void> root = f.write;
        std::weak_ptr<Readback> output = result;
        auto gate = createMetalRhiNativeSharedEventPayload("WritableDelayedCompletion");
        assert(gate && gate->nativeRuntimeBacked);
        auto done = command->payload()->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        f.write.reset();
        f.passRoot.reset();
        result.reset();
        f.dropInputs();
        assert(command->submit(queue, gate, 1u, g.diagnostic));
        if (dropped)
            command.reset();
        assert(!done->isComplete() && !root.expired() && !output.expired());
        assert(output.lock()->status() == Readback::Status::Pending);
        for (std::size_t i = 0; i < 2; ++i)
            assert(!objects[i].expired() && !tokens[i].expired());
        signalMetalRhiNativeSharedEvent(gate, 1u);
        done->wait();
        for (std::size_t i = 0; i < 2; ++i)
            assert(objects[i].expired() && tokens[i].expired());
        assert(root.expired() && output.expired());
        if (command) {
            assert(!command->submit(queue, {}, 0u, g.diagnostic)
                && g.diagnostic == "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED");
        }
        command.reset();
        root.reset();
        output.reset();
        objects = {};
        tokens = {};
        f.pass.verifyColor({32u, 64u, 128u, 255u});
        replacement.reset();
        f.pass.view.reset();
        f.pass.target.reset();
        g.registry.reset();
        g.device.reset();
        std::printf(
            "Writable actual shared-event completion: dropped=%d, replaced original objects/control blocks retained then retired PASS\n",
            dropped);
    }
    assert(!observedQuota->liveBytes());
}

void submitSeals(const char* vs, const char* fs)
{
    for (const bool destination : {false, true}) {
        ExecutionFixture f(vs, fs);
        auto& g = f.pass.graphics;
        auto command = f.command(128u);
        auto result = f.readback();
        f.producer(*command->payload());
        f.finish(*command->payload(), result);
        if (destination)
            const_cast<GpuBuffer&>(result->resource()).sealWrites();
        else
            f.buffer->sealWrites();
        MetalRhiNativeQueueStats queue;
        assert(!command->submit(queue, {}, 0u, g.diagnostic));
        assert(g.diagnostic == "RHI-METAL-ORDINARY-WRITE-SUBMIT-STATE-OR-SEAL-MISMATCH"
            && !queue.nativeQueueSubmitCount && result->status() == Readback::Status::Pending);
        command.reset();
        assert(result->status() == Readback::Status::Abandoned
            && f.buffer->state() == ResourceState::ShaderWrite);
        std::uint32_t value = 9u;
        assert(!result->read(value).success && !value);
        f.verifyUnchanged();
    }
}

void poisoned(const char* vs, const char* fs)
{
    ExecutionFixture f(vs, fs);
    auto& g = f.pass.graphics;
    auto result = f.readback();
    auto command = f.command(128u);
    f.producer(*command->payload());
    f.finish(*command->payload(), result);
    command->payload()->poisonRecording("TestTerminalRecordedFailure");
    assert(result->status() == Readback::Status::Failed
        && f.buffer->state() == ResourceState::ShaderWrite);
    assert(!command->payload()->finish(g.diagnostic)
        && g.diagnostic == "RHI-METAL-COMMAND-BUFFER-RECORDING-POISONED");
    MetalRhiNativeQueueStats queue;
    assert(!command->submit(queue, {}, 0u, g.diagnostic) && !queue.nativeQueueSubmitCount);
    std::uint32_t value = 1u;
    assert(!result->read(value).success && !value);
    command.reset();
    assert(result->status() == Readback::Status::Failed);
    f.verifyUnchanged();
}

void coldBudget(const char* vs, const char* fs)
{
    ExecutionFixture f(vs, fs);
    auto& g = f.pass.graphics;
    const auto create = [&](MetalRhiOrdinaryWritePrototypeIntent intent) {
        return MetalRhiOrdinaryWriteCommand::prepare(intent,
            "IntentProbe",
            g.registry,
            {1u, true, 128u, 0u, 0u, 0u, g.cpuBytes},
            g.quota,
            g.diagnostic);
    };
    for (const auto intent : {MetalRhiOrdinaryWritePrototypeIntent{},
             MetalRhiOrdinaryWritePrototypeIntent{2u, true, 1u},
             MetalRhiOrdinaryWritePrototypeIntent{1u, true, 0u},
             MetalRhiOrdinaryWritePrototypeIntent{1u, true, UINT32_MAX}}) {
        const auto baseline = g.quota->liveBytes();
        assert(!create(intent) && g.quota->liveBytes() == baseline);
    }
    // Measure real cold construction's high-water charge, then use exactly that
    // capacity and one byte less. This includes temporary quota-owned metadata.
    test::CpuQuotaFactoryFixture budget(g.quota);
    const auto commandFactory = [&] {
        return create({1u, true, 1u});
    };
    const auto resultFactory = [&] {
        return Readback::prepare(f.write, *g.device, g.diagnostic);
    };
    const auto commandPeak = budget.measure(commandFactory),
               resultPeak = budget.measure(resultFactory);
    budget.verify(commandPeak, commandFactory);
    budget.verify(resultPeak, resultFactory);
    auto ordinary = f.pass.command(128u);
    assert(!ordinary->recordOrdinaryPreparedWriteRenderPassBegin(f.passRoot, f.write, g.diagnostic)
        && !ordinary->stats()->nativeRenderEncoderCreated
        && !ordinary->retentionStorage()->resourceOwnerCount());
    test::OrdinaryGraphicsWriteFixture shaderConsumer(vs, fs);
    shaderConsumer.buffer->sealHostWrites();
    auto shaderRoot = shaderConsumer.prepare();
    assert(shaderRoot);
    auto shaderPass = shaderConsumer.pass.prepare();
    auto writer = shaderConsumer.command(128u);
    assert(writer);
    assert(!Readback::prepare(
        shaderRoot, *shaderConsumer.pass.graphics.device, shaderConsumer.pass.graphics.diagnostic));
    assert(!writer->payload()->recordOrdinaryPreparedWriteRenderPassBegin(
               shaderPass, shaderRoot, shaderConsumer.pass.graphics.diagnostic)
        && !writer->payload()->stats()->nativeRenderEncoderCreated
        && !writer->payload()->retentionStorage()->resourceOwnerCount());
    assert(!Readback::prepare(f.write, *shaderConsumer.pass.graphics.device, g.diagnostic)
        && g.diagnostic == "RHI-METAL-ORDINARY-WRITE-READBACK-ISSUER-MISMATCH");
    auto foreign = MetalRhiOrdinaryWriteCommand::prepare({1u, true, 1u},
        "ForeignRegistry",
        shaderConsumer.pass.graphics.registry,
        {1u, true, 128u, 0u, 0u, 0u, g.cpuBytes},
        g.quota,
        g.diagnostic);
    assert(foreign);
    assert(!foreign->payload()->recordOrdinaryPreparedWriteRenderPassBegin(
               f.passRoot, f.write, g.diagnostic)
        && !foreign->payload()->retentionStorage()->resourceOwnerCount());
    auto differentQuota = core::makeShared<core::CpuMemoryQuota>(g.cpuBytes);
    auto wrongQuota = MetalRhiOrdinaryWriteCommand::prepare({1u, true, 1u},
        "DifferentQuota",
        g.registry,
        {1u, true, 128u, 0u, 0u, 0u, g.cpuBytes},
        differentQuota,
        g.diagnostic);
    assert(wrongQuota);
    assert(!wrongQuota->payload()->recordOrdinaryPreparedWriteRenderPassBegin(
               f.passRoot, f.write, g.diagnostic)
        && g.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH"
        && !wrongQuota->payload()->retentionStorage()->resourceOwnerCount());
}

void execute(const char* vs, const char* fs)
{
    ExecutionFixture f(vs, fs);
    auto& g = f.pass.graphics;
    auto result = f.readback();
    auto command = f.command(128u, 32u);
    assert(command);
    auto& payload = *command->payload();
    std::uint32_t value = 99u;
    assert(!result->read(value).success && !value);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(g.quota, g.cpuBytes - g.quota->liveBytes());
        auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        f.producer(payload, 32u);
        f.finish(payload, result);
        std::pmr::set_default_resource(previous);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(command->commandCount() == 36u && f.buffer->state() == ResourceState::CopySrc);
    auto done = payload.preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, {}, 0u, g.diagnostic));
    done->wait();
    assert(result->status() == Readback::Status::Completed && result->read(value).success
        && value == f.write->proof().replacement);
    g.set.reset();
    assert(result->read(value).success && value == f.write->proof().replacement);
    f.verifyGuards();
    f.pass.verifyColor({32u, 64u, 128u, 255u});
    auto replacement = g.device->createBuffer({"CompletedReadbackReplacement",
        sizeof(std::uint32_t),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(replacement);
    test::MetalNativeIdentityMutationProbe::verify(
        metalRhiNativePayloadFor(result->resource())->resource,
        metalRhiNativePayloadFor(*replacement)->resource,
        [&] {
            value = 9u;
            assert(!result->read(value).success && !value);
        });
    assert(result->read(value).success && value == f.write->proof().replacement);
    assert(!semantics::GraphicsStorageWriteLowering::current(Backend::Metal).executable);
    std::printf(
        "Actual fragment atomic value=0x%08x, exact transfer readback, 3 unchanged guard words, 32 draws/finish allocation0; public versions0\n",
        value);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    @autoreleasepool {
        coldBudget(argv[1], argv[2]);
        admission(argv[1], argv[2]);
        sequence(argv[1], argv[2]);
        freshness(argv[1], argv[2]);
        for (const bool sealed : {false, true})
            cancellation(argv[1], argv[2], sealed);
        submitSeals(argv[1], argv[2]);
        poisoned(argv[1], argv[2]);
        for (const bool dropped : {false, true})
            delayed(argv[1], argv[2], dropped);
        execute(argv[1], argv[2]);
    }
}
