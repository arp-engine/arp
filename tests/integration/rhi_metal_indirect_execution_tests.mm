#include "../fixtures/metal_indirect_execution_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_sync.h"

#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef NDEBUG
#error Protected indirect execution requires assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
using Fixture = test::MetalIndirectExecutionFixture;
using Execution = MetalRhiIndirectExecutionPreparation;

void expect(const core::pmr::String& actual, const char* expected)
{
    if (actual != expected)
        std::fprintf(stderr, "Expected %s got %s\n", expected, actual.c_str());
    assert(actual == expected);
}

std::shared_ptr<core::CpuMemoryQuota> controls(const char* vs,
    const char* fs,
    const char* compute,
    IndexFormat format,
    unsigned variant,
    bool dropCommand,
    bool keepRoot)
{
    Fixture f(vs,
        fs,
        compute,
        format,
        variant == 3u       ? Fixture::Inputs::PositiveBase
            : variant == 4u ? Fixture::Inputs::ZeroInstanceBase
                            : Fixture::Inputs::NegativeBase);
    auto& g = f.vertex.pass.graphics;
    f.seal();
    auto output = f.prepare();
    assert(output);
    auto command = f.command();
    assert(command);
    auto root = f.execution(output, command);
    assert(root);
    std::array<std::uint8_t, 20u> before{};
    before.fill(255u);
    assert(readMetalRhiNativeBuffer(
        output->buffer(), before.data(), before.size(), 0u, before.size(), g.diagnostic));
    for (const auto byte : before)
        assert(!byte);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(g.quota);
        core::CpuMemoryQuotaLease pressure(g.quota, g.cpuBytes - g.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(Fixture::record(command, root, g.diagnostic));
        assert(root->status() == Execution::Status::Recorded && output->leasedRecordings() == 1u
            && !output->generationConsumed());
        assert(output->resource().state() == ResourceState::ShaderRead
            && command->stats()->nativeComputeEncoderCreated
            && command->stats()->nativeComputePipelineBound
            && command->stats()->nativeDispatchEncoded
            && command->stats()->nativeResourceBarrierEncoded
            && command->stats()->nativeIndexedDrawEncoded);
        assert(command->stats()->protectedIndirectCommandCount == 9u
            && command->stats()->protectedIndirectCallCount == 1u
            && command->stats()->protectedIndirectRecordCount == 1u
            && command->retentionStorage()->resourceOwnerCount() == 59u);
        assert(command->finish(g.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    // Never derive submitted commands from this expectation. Readback below is
    // solely acceptance evidence after the actual native completion callback.
    const auto expected = output->producer()->proof().fixedWords;
    auto gate = createMetalRhiNativeSharedEventPayload("ProtectedIndirectDelayedCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, gate, 1u, {}, 0u, g.diagnostic));
    assert(root->status() == Execution::Status::Submitted && output->leasedRecordings() == 1u
        && output->generationConsumed());
    assert(output->validate() && !done->isComplete());
    done->complete(true);
    assert(root->status() == Execution::Status::Submitted && output->leasedRecordings() == 1u);
    done->reset(); // an unrelated CPU completion flag never retires the lease
    before.fill(255u);
    assert(readMetalRhiNativeBuffer(
        output->buffer(), before.data(), before.size(), 0u, before.size(), g.diagnostic));
    for (const auto byte : before)
        assert(!byte); // the prologue wait gates execution, not only completion
    if (dropCommand)
        command.reset();
    std::weak_ptr<const Execution> weakRoot = root;
    if (!keepRoot)
        root.reset();
    assert(!weakRoot.expired() && output->leasedRecordings() == 1u && !done->isComplete());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed
        && !output->leasedRecordings());
    if (keepRoot)
        assert(root->status() == Execution::Status::Completed && root->validate());
    else
        assert(weakRoot.expired());
    std::array<std::uint8_t, 20u> bytes{};
    assert(readMetalRhiNativeBuffer(
        output->buffer(), bytes.data(), bytes.size(), 0u, bytes.size(), g.diagnostic));
    core::UnsignedElementSpan words(bytes.data(),
        bytes.size(),
        core::UnsignedElementSpan::Width::Uint32,
        core::UnsignedElementSpan::ByteOrder::LittleEndian);
    for (std::size_t i = 0u; i < expected.size(); ++i) {
        std::uint32_t value = 0u;
        assert(words.read(i, value) && value == expected[i]);
    }
    const bool noop = variant == 1u || variant == 2u;
    f.vertex.pass.verifyColor(noop ? std::array<std::uint8_t, 4u>{51u, 102u, 153u, 204u}
            : variant == 4u        ? std::array<std::uint8_t, 4u>{255u, 0u, 0u, 255u}
                                   : std::array<std::uint8_t, 4u>{0u, 255u, 0u, 255u});
    // Resource state alone cannot reopen a submitted generation.
    auto& wrapper = const_cast<GpuBuffer&>(output->resource());
    ResourceBarrierDesc reset;
    reset.resource = wrapper.id();
    reset.before = ResourceState::ShaderRead;
    reset.after = ResourceState::ShaderWrite;
    reset.sourceStages = PipelineStage::DrawIndirect;
    reset.sourceAccess = ResourceAccess::IndirectCommandRead;
    reset.destinationStages = PipelineStage::ComputeShader;
    reset.destinationAccess = ResourceAccess::ShaderWrite;
    const auto resetResult = setMetalRhiBufferStateForBarrier(wrapper, reset);
    assert(resetResult.success);
    assert(output->validate()
        && std::strcmp(output->validate(), "RHI-METAL-INDIRECT-OUTPUT-GENERATION-CONSUMED") == 0);
    auto retry = f.command();
    assert(!f.execution(output, retry));
    expect(g.diagnostic, "RHI-METAL-INDIRECT-OUTPUT-GENERATION-CONSUMED");
    retry->abandonUnsubmitted();
    if (command) {
        assert(!command->submit(queue, done, {}, 0u, {}, 0u, g.diagnostic));
        expect(g.diagnostic, "RHI-METAL-COMMAND-BUFFER-ALREADY-SUBMITTED");
    }
    std::printf(
        "native protected indirect variant%u format%u droppedCommand%d keptRoot%d: GPU five words exact, 9commands/1dispatch/1indirect, %s pixels, allocation0, real completion lease release, terminal generation PASS\n",
        variant,
        unsigned(format),
        dropCommand,
        keepRoot,
        noop                ? "clear"
            : variant == 4u ? "red"
                            : "green");
    return g.quota;
}

std::shared_ptr<core::CpuMemoryQuota> cancellation(
    const char* vs, const char* fs, const char* compute, bool finished, bool destroyed)
{
    Fixture f(vs, fs, compute);
    auto& g = f.vertex.pass.graphics;
    f.seal();
    auto output = f.prepare();
    auto command = f.command();
    auto root = f.execution(output, command);
    assert(root && Fixture::record(command, root, g.diagnostic));
    if (finished)
        assert(command->finish(g.diagnostic));
    if (destroyed)
        command.reset();
    else
        command->abandonUnsubmitted();
    assert(root->status() == Execution::Status::Abandoned && !output->leasedRecordings()
        && !output->generationConsumed() && output->resource().state() == ResourceState::ShaderWrite
        && !output->validate());
    auto retry = f.command();
    auto next = f.execution(output, retry);
    assert(next && Fixture::record(retry, next, g.diagnostic));
    assert(retry->finish(g.diagnostic));
    auto done = retry->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(retry->submit(queue, done, {}, 0u, {}, 0u, g.diagnostic));
    done->wait();
    f.vertex.pass.verifyColor({0u, 255u, 0u, 255u});
    assert(next->status() == Execution::Status::Completed && !output->leasedRecordings());
    std::printf(
        "protected indirect cancellation finished%d destroyed%d: journal rollback, caller-kept root lease release and fresh native retry PASS\n",
        finished,
        destroyed);
    return g.quota;
}

std::shared_ptr<core::CpuMemoryQuota> negatives(const char* vs, const char* fs, const char* compute)
{
    Fixture f(vs, fs, compute);
    auto& g = f.vertex.pass.graphics;
    f.seal();
    auto output = f.prepare();
    auto command = f.command();
    auto root = f.execution(output, command);
    assert(root);
    const auto& c = output->producer()->contract();
    const auto p = c.producerAccess(output->resource().id()),
               r = c.consumerAccess(output->resource().id());
    const auto d = Fixture::dependency(*output);
    const auto deny = [&](const auto& input,
                          const BindlessResourceAccess& producer,
                          const ResourceBarrierDesc& dependency,
                          const BindlessResourceAccess& consumer,
                          const char* id) {
        assert(!command->recordProtectedIndirectExecution(
            input, producer, dependency, consumer, g.diagnostic));
        expect(g.diagnostic, id);
        assert(root->status() == Execution::Status::Cold && !command->stats()->nativeDispatchEncoded
            && !command->stats()->nativeIndexedDrawEncoded
            && !command->retentionStorage()->resourceOwnerCount()
            && !command->stats()->protectedIndirectCommandCount
            && output->resource().state() == ResourceState::ShaderWrite
            && output->leasedRecordings() == 1u);
    };
    auto producer = p;
    producer.bufferSize = 4u;
    deny(root, producer, d, r, "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
    auto consumer = r;
    consumer.access = ResourceAccess::ShaderRead;
    deny(root, p, d, consumer, "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
    auto dependency = d;
    dependency.sourceAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
    deny(root, p, dependency, r, "RHI-GPU-INDIRECT-PRODUCER-DEPENDENCY");
    auto& native = *output->producer()->native();
    const auto capturedAlignment = native.computeBufferFacts.front().alignment;
    native.computeBufferFacts.front().alignment = 0u;
    const auto* abiFailure = output->producer()->validate();
    assert(abiFailure);
    deny(root, p, d, r, abiFailure);
    native.computeBufferFacts.front().alignment = capturedAlignment;
    const auto capturedPipeline = native.pipelineState;
    native.pipelineState = output->buffer()->resource.nativeObject;
    deny(root, p, d, r, "RHI-METAL-INDIRECT-PRODUCER-OWNER-MISMATCH");
    native.pipelineState = capturedPipeline;
    core::MutationJournal foreignJournal;
    assert(
        prepareMetalRhiRecordedState(const_cast<GpuBuffer&>(output->resource()), foreignJournal));
    deny(root, p, d, r, "RHI-RESOURCE-STATE-RECORDING-CONFLICT");
    foreignJournal.rollback();
    assert(!root->validate());
    auto alias = std::shared_ptr<const Execution>(output, root.get());
    deny(alias, p, d, r, "RHI-METAL-INDIRECT-EXECUTION-RECORDING-MISMATCH");
    alias.reset();
    auto other = f.command();
    assert(!Fixture::record(other, root, g.diagnostic));
    expect(g.diagnostic, "RHI-METAL-INDIRECT-EXECUTION-COMMAND-MISMATCH");
    other->abandonUnsubmitted();
    assert(Fixture::record(command, root, g.diagnostic) && command->finish(g.diagnostic));
    assert(!Fixture::record(command, root, g.diagnostic));
    expect(g.diagnostic, "RHI-METAL-INDIRECT-EXECUTION-RECORDING-MISMATCH");
    assert(!command->recordDispatch(1u, 1u, 1u, g.diagnostic));
    expect(g.diagnostic, Execution::executionDiagnostic());
    assert(command->stats()->protectedIndirectCommandCount == 9u
        && command->retentionStorage()->resourceOwnerCount() == 59u
        && root->status() == Execution::Status::Recorded && output->leasedRecordings() == 1u);
    auto gate = createMetalRhiNativeSharedEventPayload("ProtectedIndirectFailedAdmission");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    // Real wait-prologue allocation fails before native commit at exhausted quota.
    {
        core::CpuMemoryQuotaScope scope(g.quota);
        core::CpuMemoryQuotaLease pressure(g.quota, g.cpuBytes - g.quota->liveBytes());
        assert(!command->submit(queue, done, gate, 1u, {}, 0u, g.diagnostic));
    }
    expect(g.diagnostic, "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED");
    assert(root->status() == Execution::Status::Failed
        && done->outcome() == MetalRhiCompletionState::Outcome::Failed
        && !queue.nativeQueueSubmitCount && !output->leasedRecordings()
        && !output->generationConsumed() && output->resource().state() == ResourceState::ShaderWrite
        && !output->validate());
    auto retry = f.command();
    auto next = f.execution(output, retry);
    assert(next && Fixture::record(retry, next, g.diagnostic));
    assert(retry->finish(g.diagnostic));
    done = retry->preparedCompletionState();
    assert(retry->submit(queue, done, {}, 0u, {}, 0u, g.diagnostic));
    done->wait();
    f.vertex.pass.verifyColor({0u, 255u, 0u, 255u});
    std::puts(
        "protected indirect scope/dependency/native ABI/owner/journal/alias/cross-command admission atomic; second/raw recording denied; actual precommit wait allocation failure rollback and fresh retry PASS");
    return g.quota;
}

std::shared_ptr<core::CpuMemoryQuota> retirement(
    const char* vs, const char* fs, const char* compute, bool dropped, bool keepRoot)
{
    Fixture f(vs, fs, compute);
    auto& g = f.vertex.pass.graphics;
    f.seal();
    auto output = f.prepare();
    auto command = f.command();
    auto root = f.execution(output, command);
    assert(root && Fixture::record(command, root, g.diagnostic));
    assert(command->finish(g.diagnostic));
    auto replacement = g.device->createBuffer({"OriginalOwnerReplacement",
        40u,
        ResourceUsage::VertexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    const auto replacementPayload = metalRhiNativePayloadFor(*replacement);
    std::array<std::weak_ptr<void>, 8u> buffers;
    const std::array<MetalRhiNativeResourcePayload*, 4u> resources{&output->buffer()->resource,
        &output->index()->buffer()->resource,
        &f.streams[0].binding->buffer()->resource,
        &f.streams[1].binding->buffer()->resource};
    for (std::size_t i = 0u; i < resources.size(); ++i) {
        buffers[i * 2u] = resources[i]->nativeObject;
        buffers[i * 2u + 1u] = resources[i]->allocationReleaseToken;
        resources[i]->nativeObject = replacementPayload->resource.nativeObject;
        resources[i]->allocationReleaseToken = replacementPayload->resource.allocationReleaseToken;
    }
    const auto producer = output->producer()->native(),
               consumer = output->index()->provider()->native();
    std::array<std::weak_ptr<void>, 5u> functions{producer->pipelineState,
        producer->shaderLibrary,
        producer->computeFunction,
        consumer->vertexFunction,
        consumer->fragmentFunction};
    producer->pipelineState = replacementPayload->resource.nativeObject;
    producer->shaderLibrary = producer->pipelineState;
    producer->computeFunction = producer->pipelineState;
    consumer->vertexFunction = producer->pipelineState;
    consumer->fragmentFunction = producer->pipelineState;
    std::weak_ptr<void> passDescriptor = root->pass()->native().descriptor;
    std::weak_ptr<const Execution> weakRoot = root;
    std::weak_ptr<const MetalRhiIndirectOutputPreparation> weakOutput = output;
    auto gate = createMetalRhiNativeSharedEventPayload("ProtectedOriginalOwnerCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    // Submit consumes the frozen native stream, never the replaced observations.
    assert(command->submit(queue, done, gate, 1u, {}, 0u, g.diagnostic));
    output.reset();
    f.index.reset();
    f.pipeline.reset();
    f.streams = {};
    f.vertex.dropInputs();
    if (!keepRoot)
        root.reset();
    if (dropped)
        command.reset();
    assert(!done->isComplete() && !weakRoot.expired() && !weakOutput.expired()
        && !passDescriptor.expired());
    for (const auto& owner : buffers)
        assert(!owner.expired());
    for (const auto& owner : functions)
        assert(!owner.expired());
    {
        const auto observed = weakOutput.lock();
        assert(observed->leasedRecordings() == 1u && observed->generationConsumed());
    }
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
    if (keepRoot) {
        assert(
            root->status() == Execution::Status::Completed && !root->output()->leasedRecordings());
        for (const auto& owner : buffers)
            assert(!owner.expired());
        root.reset();
    }
    assert(weakRoot.expired() && weakOutput.expired() && passDescriptor.expired());
    for (const auto& owner : buffers)
        assert(owner.expired());
    for (const auto& owner : functions)
        assert(owner.expired());
    f.vertex.pass.verifyColor({0u, 255u, 0u, 255u});
    std::printf(
        "native protected original output/index/streams/producer/consumer/pass retirement: droppedCommand%d keptRoot%d real event completion exact green PASS\n",
        dropped,
        keepRoot);
    return g.quota;
}

std::shared_ptr<core::CpuMemoryQuota> rejectedSubmit(
    const char* vs, const char* fs, const char* compute, unsigned variant)
{
    Fixture f(vs, fs, compute);
    auto& g = f.vertex.pass.graphics;
    f.seal();
    auto output = f.prepare();
    auto command = f.command();
    auto root = f.execution(output, command);
    assert(root && Fixture::record(command, root, g.diagnostic));
    if (variant != 0u)
        assert(command->finish(g.diagnostic));
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    auto gate = createMetalRhiNativeSharedEventPayload("ProtectedRejectedSubmit");
    assert(gate && gate->nativeRuntimeBacked);
    if (variant == 1u)
        g.quota->setLimitBytes(g.cpuBytes + 1u);
    if (variant == 2u)
        gate->nativeDevice = output->buffer()->resource.nativeObject;
    if (variant == 3u)
        gate->nativeRuntimeBacked = false;
    const auto supplied = variant == 4u ? std::shared_ptr<MetalRhiCompletionState>{} : done;
    assert(!command->submit(queue,
        supplied,
        variant == 2u || variant == 3u ? gate : nullptr,
        1u,
        {},
        0u,
        g.diagnostic));
    const std::array<const char*, 5u> expected{
        "RHI-METAL-COMMAND-BUFFER-MUST-BE-FINISHED-BEFORE-SUBMIT",
        "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH",
        "RHI-METAL-SHARED-EVENT-DEVICE-MISMATCH",
        "RHI-METAL-SHARED-EVENT-PAYLOAD-MISSING",
        "RHI-METAL-COMPLETION-PAYLOAD-MISSING"};
    expect(g.diagnostic, expected[variant]);
    g.quota->setLimitBytes(g.cpuBytes);
    assert(root->status() == Execution::Status::Failed && !output->leasedRecordings()
        && !output->generationConsumed() && output->resource().state() == ResourceState::ShaderWrite
        && !output->validate() && done->outcome() == MetalRhiCompletionState::Outcome::Failed
        && !queue.nativeQueueSubmitCount);
    auto retry = f.command();
    auto next = f.execution(output, retry);
    assert(next && Fixture::record(retry, next, g.diagnostic));
    assert(retry->finish(g.diagnostic));
    done = retry->preparedCompletionState();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(g.quota);
        core::CpuMemoryQuotaLease pressure(g.quota, g.cpuBytes - g.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(retry->submit(queue, done, {}, 0u, {}, 0u, g.diagnostic));
        std::pmr::set_default_resource(previous);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    done->wait();
    f.vertex.pass.verifyColor({0u, 255u, 0u, 255u});
    std::printf(
        "protected precommit rejection%u: terminal failure, journal rollback, lease release, fresh native retry and no-wait submit allocation0 PASS\n",
        variant);
    return g.quota;
}

std::shared_ptr<core::CpuMemoryQuota> unsafeProducer(
    const char* vs, const char* fs, const char* compute, IndexFormat format, bool instance)
{
    Fixture f(vs, fs, compute, format);
    auto& g = f.vertex.pass.graphics;
    f.seal();
    // The real selected binary and native pipeline are valid; independent
    // immutable fetch proof must refuse to create the protected output.
    assert(!f.pipeline->validate());
    g.diagnostic.reserve(128u);
    const auto cpu = g.quota->liveBytes(), gpu = g.device->memoryStats().allocatedBytes;
    auto rejected = f.prepare();
    assert(!rejected);
    expect(g.diagnostic,
        instance ? "RHI-VERTEX-INPUT-FETCH-RANGE" : "CORE-UNSIGNED-SPAN-RANGE-INVALID");
    assert(g.quota->liveBytes() == cpu && g.device->memoryStats().allocatedBytes == gpu);
    std::printf(
        "actual proven producer %s overflow format%u: independent immutable fetch rejection, no output allocation and quota unchanged PASS\n",
        instance ? "instance" : "index",
        unsigned(format));
    return g.quota;
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 10 && metalRhiNativeDeviceHandle());
    for (unsigned variant = 0u; variant < 5u; ++variant)
        for (const auto format : {IndexFormat::Uint16, IndexFormat::Uint32})
            for (const bool dropped : {false, true})
                for (const bool retained : {false, true}) {
                    std::shared_ptr<core::CpuMemoryQuota> quota;
                    @autoreleasepool {
                        quota = controls(argv[1],
                            argv[2],
                            argv[3u + variant],
                            format,
                            variant,
                            dropped,
                            retained);
                    }
                    assert(!quota->liveBytes());
                }
    for (const bool finished : {false, true})
        for (const bool destroyed : {false, true}) {
            std::shared_ptr<core::CpuMemoryQuota> quota;
            @autoreleasepool {
                quota = cancellation(argv[1], argv[2], argv[3], finished, destroyed);
            }
            assert(!quota->liveBytes());
        }
    for (const bool dropped : {false, true})
        for (const bool retained : {false, true}) {
            std::shared_ptr<core::CpuMemoryQuota> quota;
            @autoreleasepool {
                quota = retirement(argv[1], argv[2], argv[3], dropped, retained);
            }
            assert(!quota->liveBytes());
        }
    for (unsigned variant = 0u; variant < 5u; ++variant) {
        std::shared_ptr<core::CpuMemoryQuota> quota;
        @autoreleasepool {
            quota = rejectedSubmit(argv[1], argv[2], argv[3], variant);
        }
        assert(!quota->liveBytes());
    }
    for (const auto format : {IndexFormat::Uint16, IndexFormat::Uint32})
        for (const bool instance : {false, true}) {
            std::shared_ptr<core::CpuMemoryQuota> quota;
            @autoreleasepool {
                quota =
                    unsafeProducer(argv[1], argv[2], argv[instance ? 9u : 8u], format, instance);
            }
            assert(!quota->liveBytes());
        }
    std::shared_ptr<core::CpuMemoryQuota> quota;
    @autoreleasepool {
        quota = negatives(argv[1], argv[2], argv[3]);
    }
    assert(!quota->liveBytes());
    assert(!semantics::GpuIndirectOutputRecordingLowering::current(Backend::Metal).executable);
    std::puts(
        "ProtectedIndirectExecution/1 native selected private prototype PASS; public capability/visual/platform/Production/full-plan gates open");
}
