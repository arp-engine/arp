#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/indirect_argument_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_vertex_fixture.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_native_sync.h"

#import <Metal/Metal.h>
#include <array>

#ifdef NDEBUG
#error Native vertex tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;
static_assert(!std::is_copy_constructible_v<MetalRhiOrdinaryPreparedIndexBinding>);
static_assert(!std::is_copy_constructible_v<MetalRhiOrdinaryPreparedIndexedDraw>);
static_assert(!std::is_copy_constructible_v<MetalRhiOrdinaryPreparedIndirectArguments>);
static_assert(
    sizeof(MTLDrawIndexedPrimitivesIndirectArguments) == sizeof(DrawIndexedIndirectCommand));
static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, indexCount)
    == offsetof(DrawIndexedIndirectCommand, indexCount));
static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, instanceCount)
    == offsetof(DrawIndexedIndirectCommand, instanceCount));
static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, indexStart)
    == offsetof(DrawIndexedIndirectCommand, firstIndex));
static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, baseVertex)
    == offsetof(DrawIndexedIndirectCommand, vertexOffset));
static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, baseInstance)
    == offsetof(DrawIndexedIndirectCommand, firstInstance));
using VertexFixture = test::OrdinaryGraphicsVertexFixture;

void negatives(const char* vs, const char* fs)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    const auto reject = [&](const GpuBuffer& buffer,
                            std::uint32_t slot,
                            std::uint64_t offset,
                            const char* expected,
                            std::shared_ptr<MetalRhiResourceRegistry> registry = {}) {
        const auto charge = f.quota->liveBytes();
        auto result = MetalRhiOrdinaryPreparedVertexBinding::prepare(f.provider,
            buffer,
            slot,
            offset,
            registry ? registry : f.registry,
            f.budget(),
            f.quota,
            f.diagnostic);
        if (result || f.diagnostic.find(expected) == f.diagnostic.npos)
            std::fprintf(stderr, "Expected %s got %s\n", expected, f.diagnostic.c_str());
        assert(!result && f.diagnostic.find(expected) != f.diagnostic.npos
            && charge == f.quota->liveBytes());
    };
    reject(*s.buffers[0], 2u, 0u, "MAPPING-MISMATCH");
    reject(*s.buffers[0], 31u, 0u, "MAPPING-MISMATCH");
    reject(*s.buffers[0], 3u, 1u, "OFFSET-ALIGNMENT");
    reject(*s.buffers[0], 3u, 36u, "BUFFER-RANGE");
    auto foreign = core::makeShared<MetalRhiResourceRegistry>();
    reject(*s.buffers[0], 3u, 0u, "RESOURCE-MISMATCH", foreign);
    auto wrong = f.device->createBuffer({"WrongVertexUsage",
        40u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(wrong);
    reject(*wrong, 3u, 0u, "RESOURCE-MISMATCH");
    const auto charged = f.quota->liveBytes();
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - charged);
        reject(*s.buffers[0], 3u, 0u, "PREPARATION-ALLOCATION-FAILED");
    }
    const auto before = f.quota->liveBytes();
    auto measured = s.prepare(*s.buffers[0], 3u, 8u);
    const auto preparationBytes = f.quota->liveBytes() - before;
    measured.reset();
    assert(preparationBytes && f.quota->liveBytes() == before);
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - before - preparationBytes + 1u);
        reject(*s.buffers[0], 3u, 8u, "PREPARATION-ALLOCATION-FAILED");
    }
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    auto command = s.pass.command(128u);
    assert(!command->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic));
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic)
        && f.diagnostic.find("VERTEX-BINDING-MISSING") != f.diagnostic.npos);
    assert(!command->recordVertexBufferBind(
        3u, metalRhiNativePayloadFor(*s.buffers[0]), 8u, f.diagnostic));
    assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic));
    assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[1], f.diagnostic));
    const auto owners = command->retentionStorage()->resourceOwnerCount();
    for (const auto args : {std::array<std::uint32_t, 4>{4u, 1u, 1u, 1u},
             {3u, 1u, 2u, 2u},
             {3u, 1u, UINT32_MAX, 1u}}) {
        assert(!command->recordDraw(args[0], args[1], args[2], args[3], f.diagnostic));
        assert(
            f.diagnostic == "RHI-VERTEX-INPUT-FETCH-RANGE" && !command->stats()->nativeDrawEncoded);
    }
    auto& mapping = f.provider->native()->vertexStreams;
    const auto slots = mapping.slots;
    mapping.slots[3u] = 5u;
    assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic)
        && f.diagnostic.find("MAPPING-MISMATCH") != f.diagnostic.npos);
    assert(!command->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic));
    mapping.slots = slots;
    for (std::size_t i = 0; i < 2; ++i) {
        test::MetalNativeIdentityMutationProbe::verify(
            metalRhiNativePayloadFor(*s.buffers[i])->resource,
            metalRhiNativePayloadFor(*s.buffers[2])->resource,
            [&] {
                assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic));
                assert(f.diagnostic.find("NATIVE-IDENTITY-MISMATCH") != f.diagnostic.npos);
                assert(!command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
            });
    }
    const auto id = s.buffers[0]->id();
    f.registry->unregisterResource(id);
    assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic)
        && f.diagnostic.find("RESOURCE-MISMATCH") != f.diagnostic.npos);
    f.registry->registerBuffer(*s.buffers[0]);
    auto& desc = const_cast<BufferDesc&>(s.buffers[0]->desc());
    const auto originalBytes = desc.sizeBytes;
    desc.sizeBytes--;
    f.registry->registerBuffer(*s.buffers[0]);
    assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic)
        && f.diagnostic.find("RESOURCE-MISMATCH") != f.diagnostic.npos);
    desc.sizeBytes = originalBytes;
    f.registry->registerBuffer(*s.buffers[0]);
    assert(command->retentionStorage()->resourceOwnerCount() == owners
        && !command->recordingPoisoned());
    assert(!command->recordIndexBufferBind(
        metalRhiNativePayloadFor(*s.buffers[0]), IndexFormat::Uint32, 0u, f.diagnostic));
    assert(!command->recordDrawIndexed(3u, 1u, 0u, 0, 0u, f.diagnostic));
    assert(f.diagnostic == "RHI-METAL-COMMAND-RETENTION-OPERATION-UNIMPLEMENTED");
    assert(!command->stats()->nativeIndexedDrawEncoded);
    command->abandonUnsubmitted();
}

void boundaries(const char* vs, const char* fs)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    PipelineLayoutDesc authored;
    authored.descriptorLayouts = {f.inputs};
    authored.descriptorSets = {{0u, f.inputs.name}};
    auto layout = f.device->createPipelineLayout(authored);
    assert(layout);
    RenderPipelineDesc state;
    state.layout = layout.get();
    state.colorFormat = TextureFormat::Rgba8Unorm;
    state.dynamicViewportScissor = true;
    state.vertexBuffers = VertexFixture::layouts();
    auto capability = semantics::nativeBackendCapabilityContract(Backend::Metal);
    auto other = MetalRhiOrdinaryGraphicsProvider::create(f.provider->artifacts()[0],
        f.provider->artifacts()[1],
        std::move(state),
        capability,
        f.budget(),
        f.quota,
        f.diagnostic);
    assert(other);
    layout.reset();
    authored = {};
    auto otherBinding = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
        other, *f.set, 0u, f.registry, f.budget(), f.quota, f.diagnostic);
    assert(otherBinding);
    std::array<std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>, 2> newStreams;
    for (std::size_t i = 0; i < newStreams.size(); ++i) {
        newStreams[i] = MetalRhiOrdinaryPreparedVertexBinding::prepare(other,
            *s.buffers[i],
            i ? 8u : 3u,
            i ? 24u : 8u,
            f.registry,
            f.budget(),
            f.quota,
            f.diagnostic);
        assert(newStreams[i]);
    }
    auto command = s.pass.command(128u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    for (std::size_t i = 0; i < 2; ++i)
        assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    assert(command->recordOrdinaryPreparedGraphicsBinding(otherBinding, f.diagnostic));
    assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic)
        && f.diagnostic.find("VERTEX-BINDING-MISSING") != f.diagnostic.npos);
    assert(!command->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic));
    for (const auto& stream : newStreams)
        assert(command->recordOrdinaryPreparedVertexBinding(stream, f.diagnostic));
    assert(command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic));
    assert(command->recordRenderPassEnd(f.diagnostic));
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic)
        && f.diagnostic.find("VERTEX-BINDING-MISSING") != f.diagnostic.npos);
    for (std::size_t i = 0; i < 2; ++i)
        assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    assert(command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic));
    assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
    done->wait();
    s.pass.verifyColor({255, 0, 0, 255});
    std::puts("provider and pass boundaries require fresh prepared vertex streams: PASS");
}

void execute(const char* vs, const char* fs, bool transition, bool dropped)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    auto probe = s.pass.command(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    const auto begin = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic));
    const auto vertex = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedVertexBinding(s.prepared[1], f.diagnostic));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedVertexBinding(s.prepared[2], f.diagnostic));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    assert(begin == 29u && vertex == 33u && initial == 37u && combined == 41u);
    assert(vertex == begin + 4u && initial == vertex + 4u && combined == initial + 4u);
    std::printf("vertex owners begin=%zu vertex=%zu initial=%zu combined=%zu\n",
        begin,
        vertex,
        initial,
        combined);
    probe->abandonUnsubmitted();
    probe.reset();
    for (const auto capacity : {vertex - 1u, initial - 1u}) {
        auto shortCommand = s.pass.command(capacity);
        assert(shortCommand->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        const auto first =
            shortCommand->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic);
        assert(first == (capacity == initial - 1u));
        if (first)
            assert(!shortCommand->recordOrdinaryPreparedVertexBinding(s.prepared[1], f.diagnostic));
        assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        assert(shortCommand->retentionStorage()->resourceOwnerCount() == (first ? vertex : begin));
        assert(!shortCommand->recordDraw(3u, 1u, 1u, 1u, f.diagnostic));
        shortCommand->abandonUnsubmitted();
    }
    auto command = s.pass.command(transition ? combined : combined - 1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (unsigned repeat = 0; repeat < 32; ++repeat) {
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic));
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[1], f.diagnostic));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[2], f.diagnostic)
            == transition);
        if (!transition)
            assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        const auto accepted = core::defaultCpuAllocator().stats().allocationCount;
        assert(command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic));
        assert(command->recordRenderPassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == accepted);
        std::pmr::set_default_resource(previous);
    }
    assert(command->finish(f.diagnostic));
    auto replacement = f.device->createBuffer({"VertexIdentityReplacement",
        96u,
        ResourceUsage::VertexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    std::array<std::weak_ptr<void>, 2> objects, tokens;
    for (std::size_t i = 0; i < 2; ++i) {
        auto& resource = metalRhiNativePayloadFor(*s.buffers[i && transition ? 2u : i])->resource;
        objects[i] = resource.nativeObject;
        tokens[i] = resource.allocationReleaseToken;
        resource.nativeObject = metalRhiNativePayloadFor(*replacement)->resource.nativeObject;
        resource.allocationReleaseToken =
            metalRhiNativePayloadFor(*replacement)->resource.allocationReleaseToken;
    }
    binding.reset();
    pass.reset();
    s.dropInputs();
    auto gate = createMetalRhiNativeSharedEventPayload("VertexDelayedCompletion");
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
    s.pass.verifyColor(transition ? std::array<std::uint8_t, 4>{0, 0, 255, 255}
                                  : std::array<std::uint8_t, 4>{255, 0, 0, 255});
    std::printf(
        "prepared Vertex/Instance transition=%d dropped=%d firstVertex1/firstInstance1 exact pixels, bounded admission and actual completion PASS\n",
        transition,
        dropped);
}

void instances(const char* vs, const char* fs)
{
    for (const bool two : {false, true}) {
        VertexFixture s(vs, fs);
        auto& f = s.pass.graphics;
        auto binding = f.prepare();
        auto pass = s.pass.prepare();
        auto command = s.pass.command(128u);
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[0], f.diagnostic));
        assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[1], f.diagnostic));
        assert(command->recordDraw(3u, two ? 2u : 1u, 1u, two ? 1u : 2u, f.diagnostic));
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
        auto done = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, done, {}, {}, {}, {}, f.diagnostic));
        done->wait();
        s.pass.verifyColor({0, 255, 0, 255});
    }
}

void cancellation(const char* vs, const char* fs, bool sealed)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    auto command = s.pass.command(128u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    for (std::size_t i = 0; i < 2; ++i)
        assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    if (sealed)
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const void> observed = s.prepared[0];
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

void indexPreparation(const char* vs, const char* fs)
{
    using Order = core::UnsignedElementSpan::ByteOrder;
    using Restart = IndexInputConsumptionContract::PrimitiveRestart;
    for (const auto format : {IndexFormat::Uint16, IndexFormat::Uint32}) {
        VertexFixture s(vs, fs);
        auto& f = s.pass.graphics;
        const std::array<std::uint8_t, 12> source16{
            0xff, 0xff, 0xff, 0xff, 0, 2, 0, 3, 0, 4, 0xff, 0xff};
        const std::array<std::uint8_t, 24> source32{0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0,
            0,
            0,
            2,
            0,
            0,
            0,
            3,
            0,
            0,
            0,
            4,
            0xff,
            0xff,
            0xff,
            0xff};
        const auto* source = format == IndexFormat::Uint16 ? source16.data() : source32.data();
        const auto bytes = format == IndexFormat::Uint16 ? source16.size() : source32.size();
        auto index = MetalRhiOrdinaryPreparedIndexBinding::prepare(f.provider,
            *f.device,
            source,
            bytes,
            format,
            Order::BigEndian,
            Restart::Disabled,
            f.registry,
            f.budget(),
            f.quota,
            f.diagnostic);
        if (!index)
            std::fprintf(stderr, "%s\n", f.diagnostic.c_str());
        assert(index && !index->validate());
        assert(index->resource().hostWritesSealed() && index->resource().gpuWritesSealed()
            && index->resource().gpuTransferWritesSealed());
        assert(core::inspectCpuAllocationCharge(index->content().bytes().data()).quota
            == f.quota.get());
        std::array<std::uint8_t, 24> actual{};
        assert(readMetalRhiNativeBuffer(
            index->buffer(), actual.data(), bytes, 0u, bytes, f.diagnostic));
        assert(std::equal(
            index->content().bytes().begin(), index->content().bytes().end(), actual.begin()));
        auto& buffer = const_cast<GpuBuffer&>(index->resource());
        assert(!f.device->writeBuffer(buffer, source, bytes).success);
        DescriptorLayoutDesc storageLayout;
        storageLayout.name = "IndexWritableProbe";
        storageLayout.bindings = {{0u, DescriptorType::StorageBuffer, ShaderStage::Compute}};
        auto storageSet = f.arena->allocateDescriptorSet({"IndexWritableProbe", storageLayout});
        assert(storageSet);
        const auto writable = storageSet->write(
            {0u, DescriptorType::StorageBuffer, buffer.id(), DescriptorResourceKind::Buffer});
        assert(!writable.success
            && writable.diagnostics.find("usage mismatch") != writable.diagnostics.npos);
        const auto width = index->content().elementBytes();
        const auto rejectIndex = [&](std::size_t extent,
                                     IndexFormat inputFormat,
                                     Restart restart,
                                     std::shared_ptr<MetalRhiResourceRegistry> registry,
                                     const char* expected) {
            const auto charged = f.quota->liveBytes(),
                       nativeBytes = f.device->memoryStats().allocatedBytes;
            auto rejected = MetalRhiOrdinaryPreparedIndexBinding::prepare(f.provider,
                *f.device,
                source,
                extent,
                inputFormat,
                Order::BigEndian,
                restart,
                registry,
                f.budget(),
                f.quota,
                f.diagnostic);
            assert(!rejected && f.diagnostic.find(expected) != f.diagnostic.npos);
            assert(f.quota->liveBytes() == charged
                && f.device->memoryStats().allocatedBytes == nativeBytes);
        };
        rejectIndex(bytes - 1u, format, Restart::Disabled, f.registry, "BYTES-INVALID");
        rejectIndex(
            bytes, static_cast<IndexFormat>(255), Restart::Disabled, f.registry, "FORMAT-UNKNOWN");
        rejectIndex(bytes, format, Restart::Enabled, f.registry, "RESTART-UNSUPPORTED");
        rejectIndex(bytes,
            format,
            Restart::Disabled,
            core::makeShared<MetalRhiResourceRegistry>(),
            "ISSUER-MISMATCH");
        const auto beforeIndex = f.quota->liveBytes();
        auto measured = MetalRhiOrdinaryPreparedIndexBinding::prepare(f.provider,
            *f.device,
            source,
            bytes,
            format,
            Order::BigEndian,
            Restart::Disabled,
            f.registry,
            f.budget(),
            f.quota,
            f.diagnostic);
        assert(measured);
        const auto indexCharge = f.quota->liveBytes() - beforeIndex;
        measured.reset();
        assert(indexCharge && f.quota->liveBytes() == beforeIndex);
        {
            core::CpuMemoryQuotaLease pressure(
                f.quota, f.cpuBytes - beforeIndex - indexCharge + 1u);
            rejectIndex(
                bytes, format, Restart::Disabled, f.registry, "PREPARATION-ALLOCATION-FAILED");
        }
        {
            core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - beforeIndex);
            rejectIndex(
                bytes, format, Restart::Disabled, f.registry, "PREPARATION-ALLOCATION-FAILED");
        }
        auto plan = MetalRhiOrdinaryPreparedIndexedDraw::prepare(
            index, width, 1u, 3u, -1, 1u, 1u, f.diagnostic);
        assert(plan && !plan->validate() && plan->offset() == width * 2u
            && plan->range().firstVertex == 1u && plan->range().vertexCount == 3u);
        assert(!MetalRhiOrdinaryPreparedIndexedDraw::prepare(
            index, 1u, 1u, 3u, -1, 1u, 1u, f.diagnostic));
        assert(!MetalRhiOrdinaryPreparedIndexedDraw::prepare(
            index, width, 1u, 5u, -1, 1u, 1u, f.diagnostic));
        assert(!MetalRhiOrdinaryPreparedIndexedDraw::prepare(
            index, width, 1u, 3u, -3, 1u, 1u, f.diagnostic));
        const auto charge = f.quota->liveBytes();
        {
            core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - charge);
            assert(!MetalRhiOrdinaryPreparedIndexedDraw::prepare(
                index, width, 1u, 3u, -1, 1u, 1u, f.diagnostic));
            assert(f.diagnostic.find("PREPARATION-ALLOCATION-FAILED") != f.diagnostic.npos
                && f.quota->liveBytes() == f.cpuBytes);
        }
        auto replacement = f.device->createBuffer({"IndexIdentityReplacement",
            bytes,
            ResourceUsage::IndexBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(replacement);
        test::MetalNativeIdentityMutationProbe::verify(
            index->buffer()->resource, metalRhiNativePayloadFor(*replacement)->resource, [&] {
                assert(index->validate()
                    && std::strcmp(
                           index->validate(), "RHI-METAL-ORDINARY-INDEX-NATIVE-IDENTITY-MISMATCH")
                        == 0);
            });
        const auto id = buffer.id();
        f.registry->unregisterResource(id);
        assert(index->validate()
            && std::strcmp(index->validate(), "RHI-METAL-ORDINARY-INDEX-RESOURCE-MISMATCH") == 0);
        f.registry->registerBuffer(buffer);
        assert(!index->validate());
        plan.reset();
        index.reset();
        std::puts(
            "dedicated immutable typed index upload, exact normalized native bytes, seals, cold plans/quota/native identity PASS");
    }
}

void indirectPreparation(const char* vs, const char* fs)
{
    using Contract = rhi::IndirectArgumentConsumptionContract;
    using Order = core::UnsignedElementSpan::ByteOrder;
    for (const auto format : {IndexFormat::Uint16, IndexFormat::Uint32}) {
        VertexFixture s(vs, fs);
        auto& f = s.pass.graphics;
        auto plan = s.indexPlan(format);
        auto index = plan->index();
        plan.reset();
        // Explicit big-endian records at offset4/stride24, including a complete
        // no-op whose unused fetch fields cannot authorize or require a fetch.
        std::array<std::uint8_t, 48> source{0xab,
            0xab,
            0xab,
            0xab,
            0,
            0,
            0,
            3,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            2,
            0xff,
            0xff,
            0xff,
            0xff,
            0,
            0,
            0,
            1,
            0xcd,
            0xcd,
            0xcd,
            0xcd,
            0,
            0,
            0,
            0,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0x80,
            0,
            0,
            0,
            0xff,
            0xff,
            0xff,
            0xff};
        Contract::Source input{source.data(),
            source.size(),
            4u,
            2u,
            24u,
            Order::BigEndian,
            Contract::Origin::HostAuthoredImmutable,
            1u};
        const Contract::Budget budget{2u,
            source.size(),
            2u * (sizeof(DrawIndexedIndirectCommand) + sizeof(Contract::DrawObservation))};
        const auto factory = [&] {
            return MetalRhiOrdinaryPreparedIndirectArguments::prepare(
                index, *f.device, input, budget, f.budget(), f.diagnostic);
        };
        auto root = factory();
        if (!root)
            std::fprintf(stderr, "%s\n", f.diagnostic.c_str());
        assert(root && !root->validate());
        assert(root->index() == index && root->content().drawCount() == 2u
            && root->content().bytes().size() == 40u);
        assert(root->resource().desc().usage == ResourceUsage::IndirectBuffer
            && root->resource().state() == ResourceState::ShaderRead);
        assert(root->resource().hostWritesSealed() && root->resource().gpuWritesSealed()
            && root->resource().gpuTransferWritesSealed());
        assert(core::inspectCpuAllocationCharge(root->content().bytes().data()).quota
            == f.quota.get());
        std::array<std::uint8_t, 40> actual{};
        assert(readMetalRhiNativeBuffer(
            root->buffer(), actual.data(), actual.size(), 0u, actual.size(), f.diagnostic));
        assert(std::equal(
            root->content().bytes().begin(), root->content().bytes().end(), actual.begin()));
        assert(actual[0] == 3u && actual[8] == 2u && actual[12] == 0xffu && actual[32] == 0u
            && actual[35] == 0x80u);
        const auto saved = source;
        source.fill(0u);
        Contract::DrawObservation draw;
        assert(!root->content().observe(0u, draw) && draw.active && draw.fetch.firstVertex == 1u
            && draw.fetch.vertexCount == 3u);
        assert(!root->content().observe(1u, draw) && !draw.active
            && draw.logical.vertexOffset == INT32_MIN);
        std::array<std::uint8_t, 40> unchanged{};
        assert(readMetalRhiNativeBuffer(root->buffer(),
                   unchanged.data(),
                   unchanged.size(),
                   0u,
                   unchanged.size(),
                   f.diagnostic)
            && unchanged == actual);
        source = saved;
        auto& buffer = const_cast<GpuBuffer&>(root->resource());
        assert(!f.device->writeBuffer(buffer, source.data(), 40u).success);
        DescriptorLayoutDesc writable;
        writable.name = "IndirectWritableProbe";
        writable.bindings = {{0u, DescriptorType::StorageBuffer, ShaderStage::Compute}};
        auto writableSet = f.arena->allocateDescriptorSet({"IndirectWritableProbe", writable});
        assert(writableSet);
        assert(!writableSet
                ->write({0u,
                    DescriptorType::StorageBuffer,
                    buffer.id(),
                    DescriptorResourceKind::Buffer})
                .success);
        const auto reject = [&](Contract::Source selected,
                                Contract::Budget selectedBudget,
                                const char* expected) {
            const auto cpu = f.quota->liveBytes(), gpu = f.device->memoryStats().allocatedBytes;
            const auto rejected = MetalRhiOrdinaryPreparedIndirectArguments::prepare(
                index, *f.device, selected, selectedBudget, f.budget(), f.diagnostic);
            if (rejected || f.diagnostic.find(expected) == f.diagnostic.npos)
                std::fprintf(stderr, "expected %s got %s\n", expected, f.diagnostic.c_str());
            assert(!rejected && f.diagnostic.find(expected) != f.diagnostic.npos);
            assert(f.quota->liveBytes() == cpu && f.device->memoryStats().allocatedBytes == gpu);
        };
        auto wrong = input;
        wrong.origin = Contract::Origin::ApprovedGpuProduced;
        reject(wrong, budget, "GPU-PROOF-REQUIRED");
        wrong = input;
        wrong.byteExtent--;
        reject(wrong, budget, "SOURCE-RANGE");
        wrong = input;
        wrong.contractVersion = 0u;
        reject(wrong, budget, "ARGUMENT-VERSION");
        auto shortBudget = budget;
        shortBudget.maxSnapshotBytes--;
        reject(input, shortBudget, "BUDGET-EXCEEDED");
        auto invalidBinding = f.budget();
        invalidBinding.optIn = false;
        assert(!MetalRhiOrdinaryPreparedIndirectArguments::prepare(
            index, *f.device, input, budget, invalidBinding, f.diagnostic));
        assert(!MetalRhiOrdinaryPreparedIndirectArguments::prepare(
                   {}, *f.device, input, budget, f.budget(), f.diagnostic)
            && f.diagnostic == "RHI-METAL-ORDINARY-INDIRECT-INDEX-MISSING");
        VertexFixture foreign(vs, fs);
        assert(!MetalRhiOrdinaryPreparedIndirectArguments::prepare(
                   index, *foreign.pass.graphics.device, input, budget, f.budget(), f.diagnostic)
            && f.diagnostic == "RHI-METAL-ORDINARY-INDIRECT-ISSUER-MISMATCH");
        test::CpuQuotaFactoryFixture measured(f.quota);
        const auto peak = measured.measure(factory);
        measured.verify(peak, factory);
        assert(f.diagnostic == "RHI-METAL-ORDINARY-INDIRECT-PREPARATION-ALLOCATION-FAILED");
        const auto gpuBefore = f.device->memoryStats().allocatedBytes;
        const auto argumentBytes = root->content().bytes().size();
        for (const auto available : {argumentBytes, argumentBytes - 1u}) {
            const auto reserveBytes =
                f.device->memoryStats().budgetCapacityBytes - gpuBefore - available;
            auto reserve = f.device->createBuffer({"IndirectGpuBudgetProbe",
                reserveBytes,
                ResourceUsage::CopySrc,
                MemoryUsage::CpuToGpu,
                ResourceState::CopySrc});
            assert(reserve);
            auto candidate = factory();
            assert(static_cast<bool>(candidate) == (available == argumentBytes));
            if (!candidate)
                assert(f.diagnostic == "RHI-METAL-ORDINARY-INDIRECT-BUFFER-CREATION-FAILED");
            candidate.reset();
            reserve.reset();
            assert(f.device->memoryStats().allocatedBytes == gpuBefore);
        }
        auto binding = f.prepare();
        auto preparedPass = s.pass.prepare();
        auto command = s.pass.command(128u);
        assert(command->recordOrdinaryPreparedRenderPassBegin(preparedPass, binding, f.diagnostic));
        const auto recordedOwners = command->retentionStorage()->resourceOwnerCount();
        assert(!command->recordDrawIndexedIndirect(root->buffer(), 0u, 2u, 20u, f.diagnostic));
        assert(f.diagnostic == "RHI-METAL-COMMAND-RETENTION-OPERATION-UNIMPLEMENTED"
            && !command->stats()->nativeIndexedDrawEncoded
            && command->retentionStorage()->resourceOwnerCount() == recordedOwners);
        command->abandonUnsubmitted();
        command.reset();
        preparedPass.reset();
        binding.reset();
        auto replacement = f.device->createBuffer({"IndirectIdentityReplacement",
            40u,
            ResourceUsage::IndirectBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(replacement);
        const auto replacementNative = metalRhiNativePayloadFor(*replacement);
        test::MetalNativeIdentityMutationProbe::verify(
            root->buffer()->resource, replacementNative->resource, [&] {
                assert(root->validate()
                    && std::strcmp(
                           root->validate(), "RHI-METAL-ORDINARY-INDIRECT-NATIVE-IDENTITY-MISMATCH")
                        == 0);
            });
        const auto tracked = root->buffer()->resource.hazardsExplicitlyTracked;
        root->buffer()->resource.hazardsExplicitlyTracked = !tracked;
        assert(root->validate());
        root->buffer()->resource.hazardsExplicitlyTracked = tracked;
        const auto storage = root->buffer()->resource.storageMode;
        root->buffer()->resource.storageMode = MetalRhiNativeStorageMode::Private;
        assert(root->validate());
        root->buffer()->resource.storageMode = storage;
        auto& desc = const_cast<BufferDesc&>(buffer.desc());
        const auto bytes = desc.sizeBytes;
        desc.sizeBytes--;
        assert(root->validate());
        desc.sizeBytes = bytes;
        const auto usage = desc.usage;
        desc.usage = ResourceUsage::StorageBuffer;
        assert(root->validate());
        desc.usage = usage;
        const auto memory = desc.memory;
        desc.memory = MemoryUsage::GpuOnly;
        assert(root->validate());
        desc.memory = memory;
        const auto initial = desc.initialState;
        desc.initialState = ResourceState::CopySrc;
        assert(root->validate());
        desc.initialState = initial;
        const auto id = buffer.id();
        f.registry->unregisterResource(id);
        assert(root->validate()
            && std::strcmp(root->validate(), "RHI-METAL-ORDINARY-INDIRECT-RESOURCE-MISMATCH") == 0);
        f.registry->registerBuffer(buffer);
        assert(!root->validate());
        ResourceBarrierDesc barrier;
        barrier.resource = buffer.id();
        barrier.before = ResourceState::ShaderRead;
        barrier.after = ResourceState::CopySrc;
        const auto state = setMetalRhiBufferStateForBarrier(buffer, barrier);
        assert(state.success && root->validate()
            && std::strcmp(root->validate(), "RHI-METAL-ORDINARY-INDIRECT-STATE-MISMATCH") == 0);
        barrier.before = ResourceState::CopySrc;
        barrier.after = ResourceState::ShaderRead;
        assert(setMetalRhiBufferStateForBarrier(buffer, barrier).success);
        assert(!root->validate());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease pressure(
                f.quota, f.quota->limitBytes() - f.quota->liveBytes());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            for (unsigned repeat = 0u; repeat < 1000u; ++repeat) {
                assert(!root->validate() && !root->content().observe(0u, draw));
                for (std::size_t i = 0u; i < root->childOwnerCount(); ++i)
                    assert(root->childOwnerAt(i));
            }
            std::pmr::set_default_resource(previous);
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        // The original native object/token survive internal identity replacement
        // through the root's observation/projection, then retire with that root.
        std::weak_ptr<const void> originalObject = root->buffer()->resource.nativeObject;
        std::weak_ptr<const void> originalToken = root->buffer()->resource.allocationReleaseToken;
        std::weak_ptr<const void> retainedIndex = index;
        const auto owners = root->childOwnerCount();
        root->buffer()->resource.nativeObject = replacementNative->resource.nativeObject;
        root->buffer()->resource.allocationReleaseToken =
            replacementNative->resource.allocationReleaseToken;
        index.reset();
        assert(!originalObject.expired() && !originalToken.expired() && !retainedIndex.expired());
        root.reset();
        assert(originalObject.expired() && originalToken.expired() && retainedIndex.expired()
            && !f.registry->find(id));
        std::printf(
            "immutable indirect storage format=%u canonical40 bytes/2 ordered records, owners%zu, measured quota peak%llu, seals/identity/lifetime/allocation0 PASS; no execution authority\n",
            unsigned(format),
            owners,
            static_cast<unsigned long long>(peak));
    }
}

void indexedExecution(
    const char* vs, const char* fs, IndexFormat format, bool transition, bool dropped)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    auto first = s.indexPlan(format), second = s.indexPlan(format, true, 1u, 2u);
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    const auto begin = [&](const auto& command) {
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    };
    auto probe = s.pass.command(128u);
    begin(probe);
    const auto vertexOwners = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedIndexedDraw(first, f.diagnostic));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedIndexedDraw(second, f.diagnostic));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    assert(vertexOwners == 37u && initial == 43u && combined == 49u);
    probe->abandonUnsubmitted();
    probe.reset();
    auto shortCommand = s.pass.command(initial - 1u);
    begin(shortCommand);
    assert(!shortCommand->recordOrdinaryPreparedIndexedDraw(first, f.diagnostic));
    assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
        && !shortCommand->stats()->nativeIndexedDrawEncoded
        && shortCommand->retentionStorage()->resourceOwnerCount() == vertexOwners);
    shortCommand->abandonUnsubmitted();
    shortCommand.reset();
    auto command = s.pass.command(transition ? combined : combined - 1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        begin(command);
        for (unsigned repeat = 0; repeat < 32u; ++repeat)
            assert(command->recordOrdinaryPreparedIndexedDraw(first, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        assert(command->recordOrdinaryPreparedIndexedDraw(second, f.diagnostic) == transition);
        if (!transition)
            assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
                && command->retentionStorage()->resourceOwnerCount() == initial);
        const auto accepted = core::defaultCpuAllocator().stats().allocationCount;
        assert(command->recordRenderPassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == accepted);
        std::pmr::set_default_resource(previous);
    }
    assert(command->finish(f.diagnostic));
    auto replacement = f.device->createBuffer({"IndexedNativeReplacement",
        24u,
        ResourceUsage::IndexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    std::weak_ptr<void> object, token;
    const auto& executed = transition ? second : first;
    auto& resource = executed->index()->buffer()->resource;
    object = resource.nativeObject;
    token = resource.allocationReleaseToken;
    resource.nativeObject = metalRhiNativePayloadFor(*replacement)->resource.nativeObject;
    resource.allocationReleaseToken =
        metalRhiNativePayloadFor(*replacement)->resource.allocationReleaseToken;
    first.reset();
    second.reset();
    binding.reset();
    pass.reset();
    s.dropInputs();
    auto gate = createMetalRhiNativeSharedEventPayload("IndexedDelayedCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    if (dropped)
        command.reset();
    assert(!done->isComplete() && !object.expired() && !token.expired());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    assert(object.expired() && token.expired());
    s.pass.verifyColor(transition ? std::array<std::uint8_t, 4>{0, 255, 0, 255}
                                  : std::array<std::uint8_t, 4>{255, 0, 0, 255});
    std::printf(
        "immutable indexed format=%u transition=%d dropped=%d owners37/43/49 exact pixels, signed bases/nonzero offsets, allocation0/completion PASS\n",
        static_cast<unsigned>(format),
        transition,
        dropped);
}

void indexedNegativesAndCancellation(const char* vs, const char* fs)
{
    for (const bool sealed : {false, true}) {
        VertexFixture s(vs, fs);
        auto& f = s.pass.graphics;
        auto draw = s.indexPlan(IndexFormat::Uint32);
        auto binding = f.prepare();
        auto pass = s.pass.prepare();
        auto command = s.pass.command(128u);
        assert(!command->recordOrdinaryPreparedIndexedDraw(draw, f.diagnostic));
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        assert(!command->recordOrdinaryPreparedIndexedDraw(draw, f.diagnostic)
            && f.diagnostic.find("VERTEX-BINDING-MISSING") != f.diagnostic.npos);
        for (std::size_t i = 0; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
        auto range = MetalRhiOrdinaryPreparedIndexedDraw::prepare(
            draw->index(), sizeof(std::uint32_t), 1u, 3u, 9, 1u, 1u, f.diagnostic);
        assert(range);
        assert(!command->recordOrdinaryPreparedIndexedDraw(range, f.diagnostic)
            && f.diagnostic == "RHI-VERTEX-INPUT-FETCH-RANGE");
        auto instance = MetalRhiOrdinaryPreparedIndexedDraw::prepare(
            draw->index(), sizeof(std::uint32_t), 1u, 3u, -1, 3u, 1u, f.diagnostic);
        assert(instance);
        assert(!command->recordOrdinaryPreparedIndexedDraw(instance, f.diagnostic)
            && f.diagnostic == "RHI-VERTEX-INPUT-FETCH-RANGE");
        auto replacement = f.device->createBuffer({"IndexObservationReplacement",
            24u,
            ResourceUsage::IndexBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        assert(replacement);
        const auto owners = command->retentionStorage()->resourceOwnerCount();
        test::MetalNativeIdentityMutationProbe::verify(draw->index()->buffer()->resource,
            metalRhiNativePayloadFor(*replacement)->resource,
            [&] {
                assert(!command->recordOrdinaryPreparedIndexedDraw(draw, f.diagnostic));
                assert(f.diagnostic.find("INDEX-NATIVE-IDENTITY-MISMATCH") != f.diagnostic.npos);
                assert(command->retentionStorage()->resourceOwnerCount() == owners
                    && !command->stats()->nativeIndexedDrawEncoded);
            });
        assert(command->recordOrdinaryPreparedIndexedDraw(draw, f.diagnostic));
        if (sealed)
            assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
        std::weak_ptr<const void> observed = draw;
        range.reset();
        instance.reset();
        draw.reset();
        binding.reset();
        pass.reset();
        s.dropInputs();
        assert(!observed.expired());
        command->abandonUnsubmitted();
        assert(observed.expired());
        observed.reset();
        command.reset();
    }
}

void indexedInstancing(const char* vs, const char* fs)
{
    for (const auto format : {IndexFormat::Uint16, IndexFormat::Uint32}) {
        VertexFixture s(vs, fs);
        auto& f = s.pass.graphics;
        auto draw = s.indexPlan(format, false, 2u, 1u);
        auto binding = f.prepare();
        auto pass = s.pass.prepare();
        auto command = s.pass.command(128u);
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
        assert(command->recordOrdinaryPreparedIndexedDraw(draw, f.diagnostic));
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
        auto done = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
        done->wait();
        s.pass.verifyColor({0, 255, 0, 255});
    }
}

void indirectExecution(
    const char* vs, const char* fs, IndexFormat format, bool positive, bool dropped)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    const auto base = positive ? 1 : -1;
    const std::array<DrawIndexedIndirectCommand, 4> records{{{3u, 1u, 2u, base, 1u},
        {0u, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX},
        {3u, 1u, 2u, base, 2u},
        {UINT32_MAX, 0u, UINT32_MAX, INT32_MIN, UINT32_MAX}}};
    auto args = s.indirectArguments(format, records, positive);
    const auto scope = args->content().requiredAccess(args->resource().id());
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    const auto begin = [&](const auto& command) {
        assert(
            command && command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0u; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    };
    auto probe = s.indirectCommand(128u);
    begin(probe);
    const auto vertexOwners = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic));
    const auto owners = probe->retentionStorage()->resourceOwnerCount();
    assert(vertexOwners == 37u && owners > vertexOwners);
    probe->abandonUnsubmitted();
    probe.reset();
    auto shortCommand = s.indirectCommand(owners - 1u);
    begin(shortCommand);
    assert(!shortCommand->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic));
    assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
        && !shortCommand->stats()->nativeIndexedDrawEncoded
        && shortCommand->stats()->ordinaryIndirectCommandCount == 3u
        && !shortCommand->stats()->ordinaryIndirectCallCount
        && shortCommand->retentionStorage()->resourceOwnerCount() == vertexOwners);
    shortCommand->abandonUnsubmitted();
    shortCommand.reset();
    auto command = s.indirectCommand(owners);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        begin(command);
        for (unsigned i = 0u; i < 32u; ++i)
            assert(command->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic));
        assert(command->stats()->ordinaryIndirectCommandCount == 131u
            && command->stats()->ordinaryIndirectCallCount == 32u
            && command->stats()->ordinaryIndirectRecordCount == 128u);
        assert(!command->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic)
            && f.diagnostic == "RHI-INDIRECT-RECORDING-CALL-BUDGET");
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
        assert(command->stats()->ordinaryIndirectCommandCount == 132u);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    // Freeze original argument/index identities, then replace mutable native
    // observation fields after encoding. Completion must own the originals.
    auto replacement = f.device->createBuffer({"IndirectLifetimeReplacement",
        80u,
        ResourceUsage::IndirectBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    std::array<std::weak_ptr<void>, 4> originals{args->buffer()->resource.nativeObject,
        args->buffer()->resource.allocationReleaseToken,
        args->index()->buffer()->resource.nativeObject,
        args->index()->buffer()->resource.allocationReleaseToken};
    std::weak_ptr<const void> observedRoot = args;
    for (auto* resource : {&args->buffer()->resource, &args->index()->buffer()->resource}) {
        resource->nativeObject = metalRhiNativePayloadFor(*replacement)->resource.nativeObject;
        resource->allocationReleaseToken =
            metalRhiNativePayloadFor(*replacement)->resource.allocationReleaseToken;
    }
    args.reset();
    binding.reset();
    pass.reset();
    s.dropInputs();
    auto gate = createMetalRhiNativeSharedEventPayload("IndirectDelayedCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    if (dropped)
        command.reset();
    assert(!done->isComplete() && !observedRoot.expired());
    for (const auto& weak : originals)
        assert(!weak.expired());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    assert(
        done->outcome() == MetalRhiCompletionState::Outcome::Completed && observedRoot.expired());
    for (const auto& weak : originals)
        assert(weak.expired());
    s.pass.verifyColor({0u, 255u, 0u, 255u});
    std::printf(
        "native host indirect format=%u positiveBase=%d dropped=%d owners%u/%u, 32calls/128records/132commands including extreme no-ops, exact green pixels/allocation0/delayed original retirement PASS\n",
        unsigned(format),
        positive,
        dropped,
        vertexOwners,
        owners);
}

void indirectNegatives(const char* vs, const char* fs)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    const std::array<DrawIndexedIndirectCommand, 2> records{
        {{3u, 1u, 2u, -1, 1u}, {0u, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX}}};
    auto args = s.indirectArguments(IndexFormat::Uint32, records);
    const auto access = args->content().requiredAccess(args->resource().id());
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    test::CpuQuotaFactoryFixture measured(f.quota);
    const auto factory = [&] {
        return s.indirectCommand(47u);
    };
    const auto peak = measured.measure(factory);
    measured.verify(peak, factory);
    assert(f.diagnostic.find("ALLOCATION-FAILED") != f.diagnostic.npos);
    const auto invalidIntent = [&](rhi::IndirectRecordingBudget budget, const char* expected) {
        const auto before = f.quota->liveBytes();
        assert(!s.indirectCommand(47u, budget));
        assert(f.diagnostic == expected && f.quota->liveBytes() == before);
    };
    invalidIntent({1u, false, 6u, 1u, 2u}, "RHI-INDIRECT-RECORDING-OPT-IN-REQUIRED");
    invalidIntent({0u, true, 6u, 1u, 2u}, "RHI-INDIRECT-RECORDING-VERSION");
    invalidIntent({1u, true, 2u, 1u, 2u}, "RHI-INDIRECT-RECORDING-BUDGET");
    auto normal = s.pass.command(128u);
    assert(normal->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    assert(!normal->recordOrdinaryPreparedIndirectDraw(args, access, f.diagnostic)
        && f.diagnostic == "RHI-METAL-ORDINARY-INDIRECT-PROTOTYPE-REQUIRED");
    normal->abandonUnsubmitted();
    normal.reset();
    auto command = s.indirectCommand(128u);
    assert(!command->finish(f.diagnostic) && f.diagnostic == "RHI-INDIRECT-RECORDING-INCOMPLETE");
    assert(!command->recordOrdinaryPreparedIndirectDraw(args, access, f.diagnostic));
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    assert(!command->recordRenderPassEnd(f.diagnostic)
        && f.diagnostic == "RHI-INDIRECT-RECORDING-DRAW-REQUIRED");
    assert(!command->recordOrdinaryPreparedIndirectDraw(args, access, f.diagnostic)
        && f.diagnostic.find("VERTEX-BINDING-MISSING") != f.diagnostic.npos);
    for (std::size_t i = 0u; i < 2u; ++i)
        assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    const auto owners = command->retentionStorage()->resourceOwnerCount();
    const auto count = command->stats()->ordinaryIndirectCommandCount;
    const auto reject = [&](const auto& selected,
                            const BindlessResourceAccess& scope,
                            const char* expected = nullptr) {
        assert(!command->recordOrdinaryPreparedIndirectDraw(selected, scope, f.diagnostic));
        if (expected) {
            if (f.diagnostic.find(expected) == f.diagnostic.npos)
                std::fprintf(stderr, "Expected %s got %s\n", expected, f.diagnostic.c_str());
            assert(f.diagnostic.find(expected) != f.diagnostic.npos);
        }
        assert(command->retentionStorage()->resourceOwnerCount() == owners
            && command->stats()->ordinaryIndirectCommandCount == count
            && !command->stats()->ordinaryIndirectCallCount
            && !command->stats()->nativeIndexedDrawEncoded && !command->recordingPoisoned());
    };
    auto wrong = access;
    wrong.bufferSize--;
    reject(args, wrong, "CONSUMER-SCOPE");
    wrong = access;
    wrong.pipelineStages = PipelineStage::ComputeShader;
    reject(args, wrong, "STAGE-MISMATCH");
    const std::array<DrawIndexedIndirectCommand, 2> laterVertex{{records[0], {3u, 1u, 2u, 9, 1u}}};
    auto invalid = s.indirectArguments(IndexFormat::Uint32, laterVertex);
    reject(invalid, invalid->content().requiredAccess(invalid->resource().id()), "FETCH-RANGE");
    const std::array<DrawIndexedIndirectCommand, 2> laterInstance{
        {records[0], {3u, 1u, 2u, -1, 99u}}};
    invalid = s.indirectArguments(IndexFormat::Uint32, laterInstance);
    reject(invalid, invalid->content().requiredAccess(invalid->resource().id()), "FETCH-RANGE");
    VertexFixture foreign(vs, fs);
    auto foreignArgs = foreign.indirectArguments(IndexFormat::Uint32, records);
    reject(foreignArgs,
        foreignArgs->content().requiredAccess(foreignArgs->resource().id()),
        "RECORDING-MISMATCH");
    auto replacement = f.device->createBuffer({"IndirectRejectReplacement",
        40u,
        ResourceUsage::IndirectBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(replacement);
    for (auto* native : {&args->buffer()->resource,
             &args->index()->buffer()->resource,
             &s.prepared[0]->buffer()->resource})
        test::MetalNativeIdentityMutationProbe::verify(
            *native, metalRhiNativePayloadFor(*replacement)->resource, [&] {
                reject(args, access);
            });
    const auto id = args->resource().id();
    f.registry->unregisterResource(id);
    reject(args, access, "RESOURCE-MISMATCH");
    f.registry->registerBuffer(args->resource());
    auto& mapping = f.provider->native()->vertexStreams.slots;
    const auto savedSlot = mapping[3u];
    mapping[3u] = 5u;
    reject(args, access);
    mapping[3u] = savedSlot;
    auto& descriptor = const_cast<BufferDesc&>(args->resource().desc());
    const auto size = descriptor.sizeBytes;
    descriptor.sizeBytes--;
    reject(args, access, "CONTENT-MISMATCH");
    descriptor.sizeBytes = size;
    for (const auto* selected : {&args->resource(), &args->index()->resource()}) {
        auto& buffer = const_cast<GpuBuffer&>(*selected);
        ResourceBarrierDesc barrier;
        barrier.resource = buffer.id();
        barrier.before = ResourceState::ShaderRead;
        barrier.after = ResourceState::CopySrc;
        assert(setMetalRhiBufferStateForBarrier(buffer, barrier).success);
        reject(args, access, "STATE");
        barrier.before = ResourceState::CopySrc;
        barrier.after = ResourceState::ShaderRead;
        assert(setMetalRhiBufferStateForBarrier(buffer, barrier).success);
    }
    assert(!command->recordDraw(3u, 1u, 1u, 1u, f.diagnostic));
    assert(!command->recordOrdinaryPreparedIndexedDraw(
        s.indexPlan(IndexFormat::Uint32), f.diagnostic));
    assert(!command->recordDrawIndexedIndirect(args->buffer(), 0u, 2u, 20u, f.diagnostic));
    assert(!command->recordSetViewport({0u, 0u, 4u, 4u, 0.f, 1.f}, f.diagnostic));
    assert(!command->recordSetScissor({0u, 0u, 4u, 4u}, f.diagnostic));
    assert(!command->recordComputePassBegin(f.diagnostic)
        && !command->recordBlitPassBegin(f.diagnostic));
    assert(f.diagnostic == "RHI-METAL-ORDINARY-INDIRECT-OPERATION-DOMAIN");
    // A rejected batch whose first record would fill red must encode none of it.
    // Follow with only native no-ops and verify the original clear in GPU pixels.
    const std::array<DrawIndexedIndirectCommand, 2> noops{
        {records[1], {UINT32_MAX, 0u, UINT32_MAX, INT32_MIN, UINT32_MAX}}};
    auto noop = s.indirectArguments(IndexFormat::Uint32, noops);
    assert(command->recordOrdinaryPreparedIndirectDraw(
        noop, noop->content().requiredAccess(noop->resource().id()), f.diagnostic));
    assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    assert(!command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    MetalRhiNativeQueueStats queue;
    auto done = command->preparedCompletionState();
    assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
    done->wait();
    assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
    s.pass.verifyColor({51u, 102u, 153u, 204u});
    for (const auto budget : {rhi::IndirectRecordingBudget{1u, true, 5u, 2u, 4u},
             {1u, true, 6u, 2u, 1u},
             {1u, true, 8u, 1u, 4u},
             {1u, true, 8u, 2u, 3u}}) {
        auto limited = s.indirectCommand(128u, budget);
        assert(limited);
        assert(limited->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0u; i < 2u; ++i)
            assert(limited->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
        if (budget.maxCommands == 5u || budget.maxRecords == 1u) {
            assert(!limited->recordOrdinaryPreparedIndirectDraw(args, access, f.diagnostic));
            assert(!limited->stats()->ordinaryIndirectCallCount
                && limited->stats()->ordinaryIndirectCommandCount == 3u);
        } else {
            assert(limited->recordOrdinaryPreparedIndirectDraw(args, access, f.diagnostic));
            assert(!limited->recordOrdinaryPreparedIndirectDraw(args, access, f.diagnostic));
            assert(limited->stats()->ordinaryIndirectCallCount == 1u
                && limited->stats()->ordinaryIndirectCommandCount == 5u);
            assert(limited->recordRenderPassEnd(f.diagnostic) && limited->finish(f.diagnostic));
        }
        limited->abandonUnsubmitted();
    }
    // Descriptor retirement must reject the same previously valid fixed bind,
    // including a no-op batch; zero fetch does not bypass descriptor validity.
    auto stale = s.indirectCommand(128u);
    assert(stale->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    for (std::size_t i = 0u; i < 2u; ++i)
        assert(stale->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    f.arena->reset();
    assert(!stale->recordOrdinaryPreparedIndirectDraw(
        noop, noop->content().requiredAccess(noop->resource().id()), f.diagnostic));
    assert(!stale->stats()->ordinaryIndirectCallCount && !stale->stats()->nativeIndexedDrawEncoded);
    stale->abandonUnsubmitted();
    std::printf(
        "indirect cold command peak%llu exact/one-less; entire-batch later-range rejection/no-op exact clear, scope/issuer/descriptor/identity/state/ABI/domain/call-record-command budgets PASS\n",
        static_cast<unsigned long long>(peak));
}

void indirectTransitions(const char* vs, const char* fs, bool admitted)
{
    VertexFixture s(vs, fs);
    auto& f = s.pass.graphics;
    const std::array<DrawIndexedIndirectCommand, 1> red{{{3u, 1u, 2u, -1, 1u}}},
        green{{{3u, 1u, 2u, -1, 2u}}};
    auto first = s.indirectArguments(IndexFormat::Uint32, red),
         second = s.indirectArguments(IndexFormat::Uint32, green);
    auto binding = f.prepare();
    auto pass = s.pass.prepare();
    const auto begin = [&](const auto& command) {
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0u; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
    };
    const auto draw = [&](const auto& command, const auto& args) {
        return command->recordOrdinaryPreparedIndirectDraw(
            args, args->content().requiredAccess(args->resource().id()), f.diagnostic);
    };
    auto probe = s.indirectCommand(128u, {1u, true, 6u, 2u, 2u});
    begin(probe);
    assert(draw(probe, first));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(draw(probe, second));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    assert(initial == 47u && combined == 57u);
    probe->abandonUnsubmitted();
    probe.reset();
    auto command = s.indirectCommand(admitted ? combined : combined - 1u, {1u, true, 6u, 2u, 2u});
    begin(command);
    assert(draw(command, first));
    assert(draw(command, second) == admitted);
    if (!admitted)
        assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && command->retentionStorage()->resourceOwnerCount() == initial
            && command->stats()->ordinaryIndirectCallCount == 1u);
    assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    auto done = command->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
    done->wait();
    assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
    s.pass.verifyColor(admitted ? std::array<std::uint8_t, 4>{0u, 255u, 0u, 255u}
                                : std::array<std::uint8_t, 4>{255u, 0u, 0u, 255u});
    std::printf(
        "indirect root replacement admitted=%d owners47/57 exact/one-less and unchanged counters/data on rejection PASS\n",
        admitted);
}

void indirectCancellation(const char* vs, const char* fs, bool finished)
{
    std::shared_ptr<core::CpuMemoryQuota> quota;
    @autoreleasepool {
        VertexFixture s(vs, fs);
        auto& f = s.pass.graphics;
        quota = f.quota;
        const std::array<DrawIndexedIndirectCommand, 1> records{{{3u, 1u, 2u, -1, 1u}}};
        auto args = s.indirectArguments(IndexFormat::Uint32, records);
        auto binding = f.prepare();
        auto pass = s.pass.prepare();
        auto command = s.indirectCommand(128u, {1u, true, 5u, 1u, 1u});
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0u; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
        assert(command->recordOrdinaryPreparedIndirectDraw(
            args, args->content().requiredAccess(args->resource().id()), f.diagnostic));
        if (finished)
            assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
        std::weak_ptr<const void> root = args, object = args->buffer()->resource.nativeObject,
                                  token = args->buffer()->resource.allocationReleaseToken;
        args.reset();
        binding.reset();
        pass.reset();
        s.dropInputs();
        assert(!root.expired() && !object.expired() && !token.expired());
        command->abandonUnsubmitted();
        assert(root.expired() && object.expired() && token.expired());
        assert(
            !command->finish(f.diagnostic) && f.diagnostic == "RHI-METAL-COMMAND-BUFFER-ABANDONED");
    }
    assert(!quota->liveBytes());
}

void indirectRestartAudit(const char* vs, const char* fs)
{
    for (const auto topology :
        {PrimitiveTopology::TriangleList, PrimitiveTopology::TriangleStrip}) {
        VertexFixture s(vs, fs, topology);
        auto& f = s.pass.graphics;
        const std::array<std::uint8_t, 6> bytes{0xffu, 0xffu, 0xfeu, 0xffu, 0xfdu, 0xffu};
        auto index = MetalRhiOrdinaryPreparedIndexBinding::prepare(f.provider,
            *f.device,
            bytes.data(),
            bytes.size(),
            IndexFormat::Uint16,
            core::UnsignedElementSpan::ByteOrder::LittleEndian,
            IndexInputConsumptionContract::PrimitiveRestart::Disabled,
            f.registry,
            f.budget(),
            f.quota,
            f.diagnostic);
        assert(index);
        test::IndirectArgumentFixture<1u> source(
            {{{3u, 1u, 0u, -static_cast<std::int32_t>(UINT16_MAX - 3u), 1u}}});
        auto args = MetalRhiOrdinaryPreparedIndirectArguments::prepare(
            index, *f.device, source.source(), source.budget(), f.budget(), f.diagnostic);
        assert(args);
        auto binding = f.prepare();
        auto pass = s.pass.prepare();
        auto command = s.indirectCommand(128u, {1u, true, 5u, 1u, 1u});
        if (topology == PrimitiveTopology::TriangleStrip) {
            assert(!command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
            assert(f.diagnostic == "RHI-METAL-ORDINARY-GRAPHICS-DOMAIN-UNIMPLEMENTED");
            assert(command->stats()->ordinaryIndirectCommandCount == 0u);
            std::puts(
                "indirect restart-disabled strip rejected by existing ordinary topology domain before encoding PASS");
            continue;
        }
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0u; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(s.prepared[i], f.diagnostic));
        assert(command->recordOrdinaryPreparedIndirectDraw(
            args, args->content().requiredAccess(args->resource().id()), f.diagnostic));
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
        auto done = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
        done->wait();
        assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
        const auto pixels = s.pass.readback();
        std::printf(
            "indirect restart-disabled native audit topology%u effective vertices1..3 first pixel%u/%u/%u/%u\n",
            unsigned(topology),
            pixels[0],
            pixels[1],
            pixels[2],
            pixels[3]);
        std::fflush(stdout);
        s.pass.verifyColor({255u, 0u, 0u, 255u});
    }
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    negatives(argv[1], argv[2]);
    boundaries(argv[1], argv[2]);
    indexPreparation(argv[1], argv[2]);
    indirectPreparation(argv[1], argv[2]);
    for (const bool transition : {false, true})
        for (const bool dropped : {false, true})
            execute(argv[1], argv[2], transition, dropped);
    instances(argv[1], argv[2]);
    for (const bool sealed : {false, true})
        cancellation(argv[1], argv[2], sealed);
    for (const auto format : {IndexFormat::Uint16, IndexFormat::Uint32})
        for (const bool transition : {false, true})
            for (const bool dropped : {false, true})
                indexedExecution(argv[1], argv[2], format, transition, dropped);
    indexedNegativesAndCancellation(argv[1], argv[2]);
    indexedInstancing(argv[1], argv[2]);
    for (auto format : {IndexFormat::Uint16, IndexFormat::Uint32})
        for (bool positive : {false, true})
            for (bool dropped : {false, true})
                indirectExecution(argv[1], argv[2], format, positive, dropped);
    indirectNegatives(argv[1], argv[2]);
    for (bool finished : {false, true})
        indirectCancellation(argv[1], argv[2], finished);
    for (bool admitted : {false, true})
        indirectTransitions(argv[1], argv[2], admitted);
    indirectRestartAudit(argv[1], argv[2]);
    std::puts(
        "private ordinary prepared vertex/index data/ownership/range: PASS; public versions0; raw bounded index/indirect closed");
}
