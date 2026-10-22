#include "core/diagnostics/diagnostic_error.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_compiler.h"
#include "renderer/render_graph/frame_graph_executor.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#ifdef ARP_TEST_VULKAN
#include "tests/fixtures/vulkan_heap_owner_fixture.h"
#endif

#include <cassert>
#include <cstdio>
#include <string_view>
#include <thread>

namespace {
using namespace arp;
using Pool = render_graph::FrameGraphResourcePool;
using Graph = render_graph::FrameGraph;
using Compiled = render_graph::CompiledFrameGraph;
constexpr std::uint64_t cpuBytes = 65536u;

auto quota()
{
    return core::makeShared<core::CpuMemoryQuota>(cpuBytes);
}

void expect(const char* actual, const char* expected)
{
    if (!actual || std::string_view(actual) != expected)
        std::fprintf(stderr,
            "diagnostic mismatch: expected=%s actual=%s\n",
            expected,
            actual ? actual : "<none>");
    assert(actual && std::string_view(actual) == expected);
}

template <class F> void rejects(F&& operation, const char* diagnostic)
{
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception& error) {
        expect(error.what(), diagnostic);
        rejected = true;
    }
    assert(rejected);
}

void prepare(rhi::GpuDevice& device)
{
    assert(!device.prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, quota()));
    assert(!device.prepareOriginalResourceRegistration(
        {1u, true, 8u, 8u, 0u, 19u, cpuBytes}, quota()));
}

auto allocationBudget()
{
    rhi::semantics::FrameGraphPoolAllocationBudget b{1u, true, 8u, 8u, 128u, 512u, cpuBytes};
    b.partitions = {1u, true, 16u, {}};
    b.partitions.capacity.textureCount = 8u;
    b.partitions.capacity.bufferCount = 8u;
    b.partitions.capacity.logicalTextureBytes = 128u;
    b.partitions.capacity.logicalBufferBytes = 512u;
    b.partitions.capacity.cpuMetadataBytes = cpuBytes;
    b.graphOwners = {1u, true, 2u};
    return b;
}

auto sourceBudget(bool mixed = false)
{
    rhi::semantics::FrameGraphCompiledRetirementSourceBudget b{1u, true, 8u, cpuBytes};
    b.ownedResources = {mixed ? 2u : 1u, true};
    if (mixed)
        b.sharedImports = {1u, true};
    return b;
}

auto mixedAllocationBudget()
{
    auto b = allocationBudget();
    b.partitions.capacity.frameBundleCount = 2u;
    b.partitions.capacity.referenceCount = 4u;
    return b;
}

rhi::TextureDesc textureDesc()
{
    rhi::TextureDesc d;
    d.extent = {2u, 2u, 1u};
    d.format = rhi::TextureFormat::Rgba8Unorm;
    d.usage = rhi::TextureUsage::Sampled;
    return d;
}

rhi::BufferDesc bufferDesc()
{
    return {"owned resource",
        64u,
        rhi::ResourceUsage::CopySrc,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::Undefined};
}

struct Fixture {
    explicit Fixture(rhi::GpuDevice& device,
        std::uint32_t textures = 4u,
        std::uint32_t references = 0u,
        rhi::semantics::FrameGraphPoolAllocationBudget allocation = allocationBudget())
        : pool(core::makeShared<Pool>(device))
    {
        assert(!pool->prepareAllocationAccounting(allocation, accounting));
        assert(!pool->prepareRetirementMetadata({1u, true, 2u, cpuBytes}, quota()));
        assert(!pool->prepareRetirementFrameStorage(
            {1u, true, 2u, textures, 4u, 0u, references, cpuBytes}, persistent));
    }

    std::shared_ptr<core::CpuMemoryQuota> accounting = quota(), persistent = quota();
    std::shared_ptr<Pool> pool;
};

void contract()
{
    using Request = rhi::semantics::FrameGraphOwnedResourcesRequest;
    using Contract = rhi::semantics::FrameGraphOwnedResourcesContract;
    using PoolContract = rhi::semantics::FrameGraphPoolAllocationContract;
    assert(!Request{}.requested() && !Request{}.diagnostic());
    expect((Request{4u, true}).diagnostic(), "RG-OWNED-RESOURCES-VERSION");
    assert(!(Request{2u, true}).diagnostic());
    assert(!(Request{3u, true}).diagnostic());
    expect((Request{1u, false}).diagnostic(), "RG-OWNED-RESOURCES-OPT-IN");
    expect((Request{2u, false}).diagnostic(), "RG-OWNED-RESOURCES-OPT-IN");
    expect((Request{3u, false}).diagnostic(), "RG-OWNED-RESOURCES-OPT-IN");
    auto b = allocationBudget();
    b.graphOwners = {0u, false, 2u};
    expect(PoolContract::validateIntent(b), "RG-OWNED-RESOURCES-VERSION");
    b = allocationBudget();
    b.graphOwners = {2u, true, 2u};
    expect(PoolContract::validateIntent(b), "RG-OWNED-RESOURCES-VERSION");
    b = allocationBudget();
    b.graphOwners.prototypeOptIn = false;
    expect(PoolContract::validateIntent(b), "RG-OWNED-RESOURCES-OPT-IN");
    b = allocationBudget();
    b.graphOwners.maxGraphs = 0u;
    expect(PoolContract::validateIntent(b), "RG-OWNED-RESOURCES-GRAPH-CAPACITY");
    b = allocationBudget();
    b.partitions = {};
    expect(PoolContract::validateIntent(b), "RG-OWNED-RESOURCES-PARTITIONS");
    auto source = sourceBudget();
    source.sharedImports = {1u, true};
    expect(rhi::semantics::FrameGraphCompiledRetirementSourceContract::validateIntent(source),
        "RG-OWNED-RESOURCES-MIXED-REQUEST");
    assert(!rhi::semantics::FrameGraphCompiledRetirementSourceContract::validateIntent(
        sourceBudget(true)));
    source = sourceBudget(true);
    source.sharedImports = {};
    expect(rhi::semantics::FrameGraphCompiledRetirementSourceContract::validateIntent(source),
        "RG-OWNED-RESOURCES-MIXED-REQUEST");
    for (auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)}) {
        const auto d = Contract::lowering(backend);
        assert(!d.nativeExecutionProven);
        if (backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan) {
            assert(d.retentionAvailable && !d.diagnostic);
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
        } else {
            assert(!d.retentionAvailable);
            expect(d.diagnostic,
                backend == static_cast<rhi::Backend>(255)
                    ? "RG-OWNED-RESOURCES-BACKEND"
                    : "RG-OWNED-RESOURCES-BACKEND-UNAVAILABLE");
        }
    }
}

void lifetime(rhi::GpuDevice& device)
{
    prepare(device);
    Fixture f(device);
    Compiled compiled, copy;
    const auto baseline = f.accounting->liveBytes();
    {
        Graph graph(device, f.pool);
        auto texture = graph.createTexture(textureDesc());
        auto buffer = graph.createTransientBuffer(bufferDesc());
        auto* image = &graph.texture(texture);
        auto* data = &graph.buffer(buffer);
        assert(!f.pool->validateAllocationAccounting(*image));
        assert(!f.pool->validateAllocationAccounting(*data));
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(), quota()));
        auto before = f.pool->allocationAccountingReport();
        assert(before.partitions.inUse.textureCount == 1u
            && before.partitions.inUse.bufferCount == 1u);
        compiled = graph.compile();
        copy = compiled;
        assert(!graph.validateCompiledRetirementSource(compiled));
        assert(&graph.texture(texture) == image && &graph.buffer(buffer) == data);
        auto storage = f.pool->retirementFrameStorageReport();
        assert(storage.retainedTextures == 1u && storage.retainedBuffers == 1u);
        auto after = f.pool->allocationAccountingReport();
        assert(after.partitions.inUse.cpuMetadataBytes == before.partitions.inUse.cpuMetadataBytes);
        assert(!after.partitions.pending.textureCount && !after.partitions.cached.textureCount);
        rejects(
            [&] {
                graph.compile();
            },
            "RG-OWNED-RESOURCES-ALREADY-PUBLISHED");
        rejects(
            [&] {
                render_graph::FrameGraphCompiler{}.compile(graph);
            },
            "RG-OWNED-RESOURCES-ALREADY-PUBLISHED");
        rejects(
            [&] {
                graph.execute(compiled);
            },
            "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE");
        rejects(
            [&] {
                render_graph::FrameGraphExecutor(graph).execute(Compiled{});
            },
            "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE");
        expect(graph.prepareCompiledRetirementSource(sourceBudget(), quota()),
            "RG-OWNED-RESOURCES-ALREADY-PUBLISHED");
        graph.reset();
        assert(f.pool->retirementFrameStorageReport().retainedTextures == 1u);
        expect(compiled.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EPOCH");
        // Reset removes graph aliases; a new graph epoch may publish another bundle.
        graph.createBuffer(bufferDesc());
        auto next = graph.compile();
        graph.reset();
        assert(f.pool->retirementFrameStorageReport().reservedFrames == 2u);
    }
    assert(f.pool->allocationAccountingReport().textures == 1u);
    f.pool->closeRetirementMetadata();
    assert(f.pool->retirementFrameStorageReport().retainedTextures == 1u);
    assert(!f.pool->prepareRetirementMetadata({1u, true, 2u, cpuBytes}, quota()));
    auto oldCharge = f.persistent->liveBytes();
    auto replacement = quota();
    assert(!f.pool->prepareRetirementFrameStorage(
        {1u, true, 2u, 4u, 4u, 0u, 0u, cpuBytes}, replacement));
    assert(f.persistent->liveBytes() == oldCharge && oldCharge);
    compiled = {};
    assert(f.pool->allocationAccountingReport().textures == 1u);
    std::thread lastOwner([&] {
        copy = {};
    });
    lastOwner.join();
    assert(!f.pool->allocationAccountingReport().textures);
    assert(!f.pool->allocationAccountingReport().buffers);
    assert(f.accounting->liveBytes() == baseline && !f.persistent->liveBytes());
}

void failures()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    Fixture f(*device, 0u);
    auto foreign = rhi::createNullDevice();
    prepare(*foreign);
    rejects(
        [&] {
            Graph alien(*foreign, f.pool);
        },
        "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
    std::thread wrongConstructor([&] {
        rejects(
            [&] {
                Graph alien(*device, f.pool);
            },
            "CORE-PARTITION-THREAD");
    });
    wrongConstructor.join();
    Graph graph(*device, f.pool);
    auto texture = graph.createTexture(textureDesc());
    rejects(
        [&] {
            graph.compile();
        },
        "RG-OWNED-RESOURCES-SOURCE");
    assert(!graph.prepareCompiledRetirementSource(sourceBudget(), quota()));
    std::thread wrongCompiler([&] {
        rejects(
            [&] {
                graph.compile();
            },
            "CORE-PARTITION-THREAD");
    });
    wrongCompiler.join();
    auto* original = &graph.texture(texture);
    rejects(
        [&] {
            graph.compile();
        },
        "RG-FRAME-STORAGE-TEXTURE-CAPACITY");
    assert(
        &graph.texture(texture) == original && f.pool->allocationAccountingReport().textures == 1u);
    assert(!f.pool->retirementFrameStorageReport().reservedFrames);
    rejects(
        [&] {
            graph.createTransientTexture(textureDesc());
        },
        "RG-OWNED-RESOURCES-MATERIALIZATION-UNAVAILABLE");
    rejects(
        [&] {
            graph.importTexture(device->createTexture(textureDesc()));
        },
        "RG-OWNED-RESOURCES-IMPORT-UNAVAILABLE");
    rejects(
        [&] {
            graph.importTextureRef(core::shareUnique(device->createTexture(textureDesc())));
        },
        "RG-OWNED-RESOURCES-IMPORT-UNAVAILABLE");
    rejects(
        [&] {
            graph.importBufferRef(core::shareUnique(device->createBuffer(bufferDesc())));
        },
        "RG-OWNED-RESOURCES-IMPORT-UNAVAILABLE");
    rejects(
        [&] {
            graph.importSwapchainTexture(nullptr, {});
        },
        "RG-OWNED-RESOURCES-IMPORT-UNAVAILABLE");
    {
        Graph second(*device, f.pool);
        rejects(
            [&] {
                Graph third(*device, f.pool);
            },
            "RG-OWNED-RESOURCES-GRAPH-CAPACITY");
        graph.reset();
        rejects(
            [&] {
                Graph third(*device, f.pool);
            },
            "RG-OWNED-RESOURCES-GRAPH-CAPACITY");
    }
    assert(!f.pool->allocationAccountingReport().textures);
    auto wrong = sourceBudget();
    wrong.ownedResources = {};
    expect(graph.prepareCompiledRetirementSource(wrong, quota()), "RG-OWNED-RESOURCES-SOURCE");
    expect(f.pool->prepareAllocationAccounting(allocationBudget(), quota()),
        "RG-POOL-ALLOCATION-GRAPH-ACTIVE");
    rejects(
        [&] {
            graph.execute(Compiled{});
        },
        "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE");
    Graph legacy(*device);
    expect(
        legacy.prepareCompiledRetirementSource(sourceBudget(), quota()), "RG-OWNED-RESOURCES-POOL");
    // Empty publication and source mutation do not introduce an owner cycle.
    {
        Fixture empty(*device);
        Graph authoring(*device, empty.pool);
        assert(!authoring.prepareCompiledRetirementSource(sourceBudget(), quota()));
        auto compiled = authoring.compile();
        assert(!authoring.validateCompiledRetirementSource(compiled));
    }
    {
        Fixture aliases(*device);
        const auto baseline = aliases.accounting->liveBytes();
        Graph authoring(*device, aliases.pool);
        auto t = authoring.createTexture(textureDesc());
        auto b = authoring.createBuffer(bufferDesc());
        auto* originalTexture = &authoring.texture(t);
        auto* originalBuffer = &authoring.buffer(b);
        assert(!authoring.prepareCompiledRetirementSource(sourceBudget(), quota()));
        auto compiled = authoring.compile();
        compiled.addPass({}); // Drops the public compiled source; graph aliases still own it.
        assert(&authoring.texture(t) == originalTexture && &authoring.buffer(b) == originalBuffer);
        assert(aliases.pool->retirementFrameStorageReport().retainedTextures == 1u);
        authoring.reset();
        assert(aliases.accounting->liveBytes() == baseline);
        assert(!aliases.pool->retirementFrameStorageReport().reservedFrames);
    }
}

void rollback()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    Fixture f(*device);
    auto source = quota();
    std::uint64_t measured = 0u;
    {
        Graph graph(*device, f.pool);
        graph.createTexture(textureDesc());
        graph.createBuffer(bufferDesc());
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(), source));
        const auto before = source->liveBytes();
        auto compiled = graph.compile();
        measured = source->liveBytes() - before;
    }
    assert(measured && !source->liveBytes());
    for (std::uint64_t available = 0u; available <= measured; ++available) {
        Graph graph(*device, f.pool);
        auto t = graph.createTexture(textureDesc());
        auto b = graph.createBuffer(bufferDesc());
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(), source));
        const auto charge = f.pool->allocationAccountingReport().partitions.inUse.cpuMetadataBytes;
        auto* original = &graph.texture(t);
        {
            core::CpuMemoryQuotaLease pressure(source, cpuBytes - source->liveBytes() - available);
            assert(pressure.charged());
            if (available == measured) {
                auto compiled = graph.compile();
                assert(!graph.validateCompiledRetirementSource(compiled));
            } else {
                rejects(
                    [&] {
                        graph.compile();
                    },
                    "RG-COMPILED-SOURCE-ALLOCATION");
                assert(&graph.texture(t) == original && &graph.buffer(b));
                assert(!f.pool->retirementFrameStorageReport().retainedTextures);
                assert(!f.pool->retirementFrameStorageReport().reservedFrames);
            }
        }
        assert(f.pool->allocationAccountingReport().partitions.inUse.cpuMetadataBytes == charge);
    }
    std::printf(
        "owned-resource source exact quota=%llu; every smaller byte refused without transfer\n",
        static_cast<unsigned long long>(measured));
}

void mixedLifetime(rhi::GpuDevice& device)
{
    prepare(device);
    Fixture f(device, 4u, 2u, mixedAllocationBudget());
    const auto baseline = f.accounting->liveBytes();
    std::weak_ptr<rhi::GpuTexture> importedTexture;
    std::weak_ptr<rhi::GpuBuffer> importedBuffer;
    Compiled first, copy, second;
    {
        Graph graph(device, f.pool);
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(true), quota()));
        rejects(
            [&] {
                graph.createTransientTexture(textureDesc());
            },
            "RG-OWNED-RESOURCES-MATERIALIZATION-UNAVAILABLE");
        rejects(
            [&] {
                graph.importTexture(nullptr);
            },
            "RG-OWNED-RESOURCES-IMPORT-UNAVAILABLE");
        rejects(
            [&] {
                graph.importSwapchainTexture(nullptr, {});
            },
            "RG-OWNED-RESOURCES-IMPORT-UNAVAILABLE");
        auto sharedTexture = core::shareUnique(device.createTexture(textureDesc()));
        auto sharedBuffer = core::shareUnique(device.createBuffer(bufferDesc()));
        importedTexture = sharedTexture;
        importedBuffer = sharedBuffer;
        auto ti = graph.importTextureRef(sharedTexture);
        auto bi = graph.importBufferRef(sharedBuffer);
        assert(graph.importTextureRef(sharedTexture).id == ti.id);
        assert(graph.importBufferRef(sharedBuffer).id == bi.id);
        auto t = graph.createTexture(textureDesc());
        auto b = graph.createTransientBuffer(bufferDesc());
        auto* texture = &graph.texture(t);
        auto* buffer = &graph.buffer(b);
        // A downgrade cannot discard the original mixed intent or its guards.
        expect(graph.prepareCompiledRetirementSource(sourceBudget(), quota()),
            "RG-OWNED-RESOURCES-MIXED-REQUEST");
        first = graph.compile();
        copy = first;
        sharedTexture.reset();
        sharedBuffer.reset();
        assert(!graph.validateCompiledRetirementSource(first));
        assert(&graph.texture(t) == texture && &graph.buffer(b) == buffer);
        auto live = f.pool->allocationAccountingReport();
        assert(live.partitions.inUse.frameBundleCount == 1u);
        assert(live.partitions.inUse.referenceCount == 2u);
        assert(live.textures == 1u && live.buffers == 1u);
        assert(live.logicalTextureBytes == 16u && live.logicalBufferBytes == 64u);
        assert(!live.partitions.pending.frameBundleCount && !live.partitions.cached.referenceCount);
        auto storage = f.pool->retirementFrameStorageReport();
        assert(storage.retainedReferences == 2u && storage.retainedTextures == 1u
            && storage.retainedBuffers == 1u);
        rejects(
            [&] {
                graph.execute(first);
            },
            "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE");
        rejects(
            [&] {
                render_graph::FrameGraphExecutor(graph).execute(Compiled{});
            },
            "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE");
        graph.reset();
        assert(&graph.texture(ti) && &graph.buffer(bi));
        graph.createTexture(textureDesc());
        graph.createBuffer(bufferDesc());
        second = graph.compile();
        graph.reset();
        live = f.pool->allocationAccountingReport();
        assert(live.partitions.inUse.frameBundleCount == 2u
            && live.partitions.inUse.referenceCount == 4u);
        assert(live.textures == 2u && live.buffers == 2u);
        rejects(
            [&] {
                graph.compile();
            },
            "RG-FRAME-STORAGE-FRAME-CAPACITY");
    }
    f.pool->closeRetirementMetadata();
    assert(!f.pool->prepareRetirementMetadata({1u, true, 2u, cpuBytes}, quota()));
    auto replacement = quota();
    assert(!f.pool->prepareRetirementFrameStorage(
        {1u, true, 2u, 4u, 4u, 0u, 2u, cpuBytes}, replacement));
    first = {};
    second = {};
    auto live = f.pool->allocationAccountingReport();
    assert(
        live.partitions.inUse.frameBundleCount == 1u && live.partitions.inUse.referenceCount == 2u);
    assert(!importedTexture.expired() && !importedBuffer.expired());
    std::thread lastOwner([&] {
        copy = {};
    });
    lastOwner.join();
    live = f.pool->allocationAccountingReport();
    assert(!live.textures && !live.buffers && !live.partitions.inUse.frameBundleCount
        && !live.partitions.inUse.referenceCount);
    assert(f.accounting->liveBytes() == baseline && !f.persistent->liveBytes());
    assert(importedTexture.expired() && importedBuffer.expired());
    // Empty bundles consume frame slots, even with a zero reference budget.
    // Copies share the charge; final cancellation permits a fresh generation.
    {
        auto budget = mixedAllocationBudget();
        budget.partitions.capacity.referenceCount = 0u;
        Fixture empty(device, 0u, 0u, budget);
        Graph graph(device, empty.pool);
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(true), quota()));
        auto a = graph.compile();
        auto b = graph.compile();
        auto copy = a;
        auto usage = empty.pool->allocationAccountingReport().partitions;
        assert(usage.inUse.frameBundleCount == 2u && !usage.inUse.referenceCount);
        rejects(
            [&] {
                graph.compile();
            },
            "RG-FRAME-STORAGE-FRAME-CAPACITY");
        a = {};
        assert(empty.pool->allocationAccountingReport().partitions.inUse.frameBundleCount == 2u);
        b = {};
        copy = {};
        assert(!empty.pool->allocationAccountingReport().partitions.inUse.frameBundleCount);
        assert(!empty.pool->retirementFrameStorageReport().reservedFrames);
        auto next = graph.compile();
        assert(!graph.validateCompiledRetirementSource(next));
        assert(empty.pool->allocationAccountingReport().partitions.inUse.frameBundleCount == 1u);
    }
    // An import-only bundle's frame/reference lease also pins the original domain.
    {
        Fixture imports(device, 0u, 2u, mixedAllocationBudget());
        Compiled result;
        {
            Graph graph(device, imports.pool);
            assert(!graph.prepareCompiledRetirementSource(sourceBudget(true), quota()));
            graph.importBufferRef(core::shareUnique(device.createBuffer(bufferDesc())));
            result = graph.compile();
        }
        assert(!imports.pool->allocationAccountingReport().buffers);
        expect(imports.pool->prepareAllocationAccounting(mixedAllocationBudget(), quota()),
            "RG-POOL-ALLOCATION-BUSY");
        result = {};
        assert(!imports.pool->allocationAccountingReport().partitions.inUse.referenceCount);
        assert(!imports.pool->prepareAllocationAccounting(mixedAllocationBudget(), quota()));
    }
    // Genuine imports retain their original pool receipts; this bundle charges
    // only its reference entries, never a second copy of their allocation bytes.
    {
        Fixture imports(device, 0u, 2u, mixedAllocationBudget());
        Fixture external(device);
        Compiled result;
        {
            auto samePool = core::shareUnique(imports.pool->acquireBuffer(bufferDesc()));
            auto otherPool = core::shareUnique(external.pool->acquireBuffer(bufferDesc()));
            Graph graph(device, imports.pool);
            assert(!graph.prepareCompiledRetirementSource(sourceBudget(true), quota()));
            graph.importBufferRef(samePool);
            graph.importBufferRef(otherPool);
            result = graph.compile();
        }
        auto own = imports.pool->allocationAccountingReport();
        auto other = external.pool->allocationAccountingReport();
        assert(own.buffers == 1u && own.logicalBufferBytes == 64u);
        assert(other.buffers == 1u && other.logicalBufferBytes == 64u);
        assert(own.partitions.inUse.referenceCount == 2u
            && own.partitions.inUse.frameBundleCount == 1u);
        result = {};
        assert(!imports.pool->allocationAccountingReport().buffers);
        assert(!external.pool->allocationAccountingReport().buffers);
    }
}

void mixedRejection(rhi::GpuDevice& device, rhi::GpuDevice& foreign, bool materialization = false)
{
    prepare(device);
    prepare(foreign);
    for (unsigned failure = 0u; failure != 16u; ++failure) {
        auto budget = mixedAllocationBudget();
        if (failure == 7u)
            budget.partitions.capacity.frameBundleCount = 0u;
        if (failure == 8u)
            budget.partitions.capacity.referenceCount = 1u;
        if (failure == 9u)
            budget.partitions.maxResourceOwners = 2u;
        Fixture f(device, failure == 10u ? 0u : 4u, failure == 6u ? 1u : 2u, budget);
        Graph graph(device, f.pool);
        auto source = sourceBudget(true);
        if (materialization) {
            source.ownedResources.contractVersion = 3u;
            ++source.maxResourceBindings;
        }
        assert(!graph.prepareCompiledRetirementSource(source, quota()));
        auto t = graph.createTexture(textureDesc());
        auto b = graph.createBuffer(bufferDesc());
        if (materialization)
            graph.createTransientTexture(textureDesc());
        auto* original = &graph.texture(t);
        auto texture = core::shareUnique(device.createTexture(textureDesc()));
        auto buffer = core::shareUnique(device.createBuffer(bufferDesc()));
        auto unrelated = core::makeShared<unsigned>(0u);
        if (failure == 0u || failure == 2u)
            graph.importTextureRef(std::shared_ptr<rhi::GpuTexture>(unrelated, texture.get()));
        else if (failure == 1u || failure == 3u)
            graph.importBufferRef(std::shared_ptr<rhi::GpuBuffer>(unrelated, buffer.get()));
        else if (failure == 4u)
            graph.importTextureRef(core::shareUnique(foreign.createTexture(textureDesc())));
        else if (failure == 5u)
            graph.importBufferRef(core::shareUnique(foreign.createBuffer(bufferDesc())));
        else if (failure == 12u)
            graph.importTextureRef(
                std::shared_ptr<rhi::GpuTexture>(std::shared_ptr<void>{}, texture.get()));
        else if (failure == 13u)
            graph.importBufferRef(
                std::shared_ptr<rhi::GpuBuffer>(std::shared_ptr<void>{}, buffer.get()));
        else if (failure >= 14u) {
            auto unregistered = rhi::createNullDevice();
            if (failure == 14u)
                graph.importTextureRef(
                    core::shareUnique(unregistered->createTexture(textureDesc())));
            else
                graph.importBufferRef(core::shareUnique(unregistered->createBuffer(bufferDesc())));
        } else {
            graph.importTextureRef(texture);
            graph.importBufferRef(buffer);
        }
        if (failure == 2u)
            texture.reset();
        if (failure == 3u)
            buffer.reset();
        const auto textureOwners = texture.use_count(), bufferOwners = buffer.use_count();
        const char* diagnostic = failure < 4u || failure == 12u || failure == 13u
            ? "RG-SHARED-IMPORTS-SHARED-OWNER"
            : failure < 6u   ? "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE"
            : failure >= 14u ? "RHI-ORIGINAL-REGISTRATION-EXPIRED"
            : failure == 6u  ? "RG-FRAME-STORAGE-REFERENCE-CAPACITY"
            : failure == 9u  ? "CORE-POOL-CAPACITY"
            : failure == 10u ? "RG-FRAME-STORAGE-TEXTURE-CAPACITY"
                             : "CORE-PARTITION-CAPACITY";
        // The final case exercises the buffer count independently of reference limits.
        if (failure == 11u) {
            graph.createBuffer(bufferDesc());
            graph.createBuffer(bufferDesc());
            graph.createBuffer(bufferDesc());
            graph.createBuffer(bufferDesc());
            diagnostic = "RG-FRAME-STORAGE-BUFFER-CAPACITY";
        }
        if (failure < 6u || failure >= 12u)
            rejects(
                [&] {
                    render_graph::FrameGraphCompiler{}.compile(graph);
                },
                diagnostic);
        const auto misses = f.pool->report().missCount;
        rejects(
            [&] {
                graph.compile();
            },
            diagnostic);
        assert(f.pool->report().missCount == misses);
        assert(&graph.texture(t) == original && &graph.buffer(b));
        assert(texture.use_count() == textureOwners && buffer.use_count() == bufferOwners);
        auto report = f.pool->retirementFrameStorageReport();
        auto usage = f.pool->allocationAccountingReport().partitions;
        assert(!report.reservedFrames && !report.retainedReferences && !report.retainedTextures);
        assert(!usage.inUse.frameBundleCount && !usage.inUse.referenceCount);
        assert(!usage.reserved.frameBundleCount && !usage.reserved.referenceCount);
    }
}

void mixedRollback()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    Fixture f(*device, 4u, 2u, mixedAllocationBudget());
    auto texture = core::shareUnique(device->createTexture(textureDesc()));
    auto buffer = core::shareUnique(device->createBuffer(bufferDesc()));
    auto source = quota();
    const auto author = [&](Graph& graph) {
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(true), source));
        graph.importTextureRef(texture);
        graph.importBufferRef(buffer);
        graph.createTexture(textureDesc());
        graph.createBuffer(bufferDesc());
    };
    std::uint64_t measured = 0u;
    {
        Graph graph(*device, f.pool);
        author(graph);
        const auto before = source->liveBytes();
        auto compiled = graph.compile();
        measured = source->liveBytes() - before;
    }
    assert(measured && !source->liveBytes());
    for (std::uint64_t available = 0u; available <= measured; ++available) {
        {
            Graph graph(*device, f.pool);
            author(graph);
            const auto textureOwners = texture.use_count(), bufferOwners = buffer.use_count();
            core::CpuMemoryQuotaLease pressure(source, cpuBytes - source->liveBytes() - available);
            assert(pressure.charged());
            if (available == measured) {
                auto compiled = graph.compile();
                assert(!graph.validateCompiledRetirementSource(compiled));
                auto live = f.pool->allocationAccountingReport().partitions;
                assert(live.inUse.frameBundleCount == 1u && live.inUse.referenceCount == 2u);
            } else {
                rejects(
                    [&] {
                        graph.compile();
                    },
                    "RG-COMPILED-SOURCE-ALLOCATION");
                assert(texture.use_count() == textureOwners && buffer.use_count() == bufferOwners);
                auto report = f.pool->retirementFrameStorageReport();
                assert(!report.reservedFrames && !report.retainedReferences
                    && !report.retainedTextures);
                auto live = f.pool->allocationAccountingReport().partitions;
                assert(!live.inUse.frameBundleCount && !live.reserved.frameBundleCount);
            }
        }
        auto live = f.pool->allocationAccountingReport().partitions;
        assert(!live.inUse.textureCount && !live.inUse.bufferCount && !live.inUse.referenceCount);
        assert(!live.inUse.frameBundleCount);
    }
    std::printf(
        "mixed-source exact quota=%llu; smaller byte boundaries preserve all owners/charges\n",
        static_cast<unsigned long long>(measured));
}

auto materializationBudget()
{
    auto b = sourceBudget(true);
    b.ownedResources.contractVersion = 3u;
    return b;
}

rhi::TextureDesc deferredDesc()
{
    auto d = textureDesc();
    d.usage = d.usage | rhi::TextureUsage::RenderAttachment;
    d.initialState = rhi::ResourceState::ColorAttachment;
    return d;
}

void materializationLifetime(rhi::GpuDevice& device)
{
    prepare(device);
    assert(!device.capabilities().features.transientAliasing);
    Fixture f(device, 4u, 1u, mixedAllocationBudget());
    Compiled compiled, copy;
    {
        Graph graph(device, f.pool);
        assert(!graph.prepareCompiledRetirementSource(materializationBudget(), quota()));
        const auto ordinary = graph.createTexture(textureDesc());
        auto* original = &graph.texture(ordinary);
        graph.createBuffer(bufferDesc());
        graph.importTextureRef(core::shareUnique(device.createTexture(textureDesc())));
        auto first = graph.createTransientTexture(deferredDesc());
        auto second = graph.createTransientTexture(deferredDesc());
        unsigned executions = 0u;
        graph.addPass("desc-only writes")
            .write(first, rhi::ResourceState::ColorAttachment)
            .write(second, rhi::ResourceState::ColorAttachment)
            .execute([&](render_graph::RenderContext&) {
                ++executions;
            });
        graph.addPass("keep desc-only writes live")
            .read(first, rhi::ResourceState::ShaderRead)
            .read(second, rhi::ResourceState::ShaderRead)
            .execute([&](render_graph::RenderContext&) {
                ++executions;
            });
        const auto misses = f.pool->report().missCount;
        auto plan = render_graph::FrameGraphCompiler{}.compile(graph);
        assert(plan.aliasPlan().empty() && f.pool->report().missCount == misses);
        assert(f.pool->allocationAccountingReport().textures == 1u);
        expect(graph.prepareCompiledRetirementSource(sourceBudget(true), quota()),
            "RG-OWNED-RESOURCES-MATERIALIZATION-UNAVAILABLE");
        compiled = graph.compile();
        assert(compiled.aliasPlan().empty() && !graph.validateCompiledRetirementSource(compiled));
        assert(&graph.texture(ordinary) == original && executions == 0u);
        assert(graph.texture(first).state() == rhi::ResourceState::Undefined);
        assert(graph.texture(first).desc().memory == rhi::MemoryUsage::Transient);
        assert(!f.pool->validateAllocationAccounting(graph.texture(first)));
        assert(!f.pool->validateAllocationAccounting(graph.texture(second)));
        assert(f.pool->report().missCount == misses + 2u);
        auto live = f.pool->allocationAccountingReport().partitions;
        assert(live.inUse.textureCount == 3u && live.inUse.logicalTextureBytes == 48u);
        assert(live.inUse.frameBundleCount == 1u && live.inUse.referenceCount == 1u);
        assert(!live.reserved.textureCount && !live.reserved.frameBundleCount);
        copy = compiled;
        rejects(
            [&] {
                graph.execute(compiled);
            },
            "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE");
        rejects(
            [&] {
                render_graph::FrameGraphExecutor(graph).execute(compiled);
            },
            "RG-OWNED-RESOURCES-COMMAND-CUSTODY-UNAVAILABLE");
        assert(executions == 0u);
        graph.reset();
        assert(f.pool->retirementFrameStorageReport().retainedTextures == 3u);
    }
    compiled = {};
    assert(f.pool->retirementFrameStorageReport().retainedTextures == 3u);
    f.pool->closeRetirementMetadata();
    assert(!f.pool->prepareRetirementMetadata({1u, true, 2u, cpuBytes}, quota()));
    std::thread remote([source = std::move(copy)]() mutable {
        source = {};
    });
    remote.join();
    const auto live = f.pool->allocationAccountingReport().partitions;
    assert(!live.inUse.textureCount && !live.inUse.bufferCount && !live.inUse.frameBundleCount);
    assert(!live.inUse.referenceCount && !live.reserved.textureCount);
    assert(!f.persistent->liveBytes());
}

void materializationFailures(rhi::GpuDevice& device)
{
    prepare(device);
    auto b = mixedAllocationBudget();
    b.maxTextures = 3u;
    Fixture f(device, 4u, 1u, b);
    Graph graph(device, f.pool);
    auto source = materializationBudget();
    source.sharedImports = {};
    expect(
        graph.prepareCompiledRetirementSource(source, quota()), "RG-OWNED-RESOURCES-MIXED-REQUEST");
    assert(!graph.prepareCompiledRetirementSource(materializationBudget(), quota()));
    auto ordinary = graph.createTexture(textureDesc());
    auto* original = &graph.texture(ordinary);
    auto imported = core::shareUnique(device.createTexture(textureDesc()));
    graph.importTextureRef(imported);
    graph.createTransientTexture(deferredDesc());
    graph.createTransientTexture(deferredDesc());
    auto pressure = f.pool->acquireTexture(textureDesc());
    const auto references = imported.use_count();
    const auto baseline = f.accounting->liveBytes();
    const auto misses = f.pool->report().missCount;
    rejects(
        [&] {
            graph.compile();
        },
        "RG-POOL-ALLOCATION-RESOURCE-CAPACITY");
    assert(&graph.texture(ordinary) == original && imported.use_count() == references);
    assert(f.pool->report().missCount == misses + 1u);
    assert(f.accounting->liveBytes() == baseline);
    auto live = f.pool->allocationAccountingReport().partitions;
    assert(live.inUse.textureCount == 2u && live.inUse.logicalTextureBytes == 32u);
    assert(!live.inUse.frameBundleCount && !live.reserved.textureCount);
    assert(!live.reserved.frameBundleCount && !live.inUse.referenceCount);
    assert(!f.pool->retirementFrameStorageReport().reservedFrames);
    assert(!f.pool->retirementFrameStorageReport().retainedTextures);
    pressure.reset();
    auto compiled = graph.compile();
    assert(!graph.validateCompiledRetirementSource(compiled));
    assert(f.pool->allocationAccountingReport().textures == 3u);
    assert(f.pool->retirementFrameStorageReport().retainedTextures == 3u);
}

void materializationReceiptQuota(rhi::GpuDevice& device)
{
    prepare(device);
    Fixture f(device, 4u, 1u, mixedAllocationBudget());
    Graph graph(device, f.pool);
    assert(!graph.prepareCompiledRetirementSource(materializationBudget(), quota()));
    const auto ordinary = graph.createTexture(textureDesc());
    const auto* original = &graph.texture(ordinary);
    graph.createTransientTexture(deferredDesc());
    const auto baseline = f.accounting->liveBytes();
    {
        core::CpuMemoryQuotaLease pressure(f.accounting, cpuBytes - baseline);
        assert(pressure.charged());
        rejects(
            [&] {
                graph.compile();
            },
            "RG-COMPILED-SOURCE-ALLOCATION");
        assert(&graph.texture(ordinary) == original);
        auto live = f.pool->allocationAccountingReport().partitions;
        assert(live.inUse.textureCount == 1u && !live.reserved.textureCount);
        assert(!live.inUse.frameBundleCount && !live.reserved.frameBundleCount);
        assert(!f.pool->retirementFrameStorageReport().reservedFrames);
    }
    assert(f.accounting->liveBytes() == baseline);
    auto compiled = graph.compile();
    assert(!graph.validateCompiledRetirementSource(compiled));
    assert(f.pool->allocationAccountingReport().textures == 2u);
}

void materializationQuota()
{
    auto device = rhi::createNullDevice();
    prepare(*device);
    Fixture f(*device, 4u, 1u, mixedAllocationBudget());
    auto source = quota();
    core::UniquePtr<Graph> graph;
    const auto prepareGraph = [&] {
        graph.reset();
        graph = core::makeUnique<Graph>(*device, f.pool);
        assert(!graph->prepareCompiledRetirementSource(materializationBudget(), source));
        graph->createTexture(textureDesc());
        graph->createTransientTexture(deferredDesc());
        graph->createTransientTexture(deferredDesc());
    };
    const auto attempt = [&]() -> std::optional<Compiled> {
        try {
            auto compiled = graph->compile();
            graph->reset();
            return compiled;
        } catch (const core::DiagnosticError& error) {
            expect(error.what(), "RG-COMPILED-SOURCE-ALLOCATION");
            auto live = f.pool->allocationAccountingReport().partitions;
            assert(live.inUse.textureCount == 1u && !live.reserved.textureCount);
            assert(!live.inUse.frameBundleCount && !live.reserved.frameBundleCount);
            assert(!f.pool->retirementFrameStorageReport().reservedFrames);
            return std::nullopt;
        }
    };
    test::CpuQuotaFactoryFixture probe(source);
    const auto exact = probe.measureSingleUse(prepareGraph, attempt);
    probe.verifySingleUse(exact, prepareGraph, attempt);
    graph.reset();
    assert(!source->liveBytes());
    assert(!f.pool->allocationAccountingReport().textures);
    std::printf(
        "desc-only materialization measured source quota=%llu; exact/one-less/zero rollback: PASS\n",
        static_cast<unsigned long long>(exact));
}

#ifdef ARP_TEST_VULKAN
void materializationNativeOwners()
{
    auto native = core::makeUnique<tests::VulkanHeadlessRuntime>();
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native->runtime(), false);
    prepare(*device);
    auto f = core::makeUnique<Fixture>(*device, 4u, 1u, mixedAllocationBudget());
    auto accounting = f->accounting, persistent = f->persistent;
    std::weak_ptr<rhi_vulkan::VulkanRhiRuntime> runtime = native->runtime();
    std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> owner = native->nativeOwner();
    std::array<std::weak_ptr<rhi_vulkan::VulkanRhiNativeResourcePayload>, 2u> images;
    Compiled compiled;
    {
        Graph graph(*device, f->pool);
        assert(!graph.prepareCompiledRetirementSource(materializationBudget(), quota()));
        auto first = graph.createTransientTexture(deferredDesc());
        auto second = graph.createTransientTexture(deferredDesc());
        compiled = graph.compile();
        images = {rhi_vulkan::vulkanRhiNativePayload(graph.texture(first)),
            rhi_vulkan::vulkanRhiNativePayload(graph.texture(second))};
    }
    f->pool->closeRetirementMetadata();
    f.reset();
    device.reset();
    native.reset();
    assert(!runtime.expired() && !owner.expired());
    for (auto& observed : images) {
        auto image = observed.lock();
        assert(image);
        VkMemoryRequirements actual{};
        vkGetImageMemoryRequirements(
            image->backendVulkanDeviceHandle(), image->backendVulkanImageHandle(), &actual);
        assert(actual.size);
    }
    compiled = {};
    assert(runtime.expired() && owner.expired() && !accounting->liveBytes()
        && !persistent->liveBytes());
    for (auto& image : images)
        assert(image.expired());
    std::puts(
        "genuine compile-materialized Vulkan images retain original receipt/bundle/runtime: PASS");
}

void materializationAliasRefusal()
{
    tests::VulkanHeadlessRuntime native;
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), true);
    prepare(*device);
    assert(device->capabilities().features.transientAliasing);
    Fixture f(*device, 4u, 1u, mixedAllocationBudget());
    Graph graph(*device, f.pool);
    assert(!graph.prepareCompiledRetirementSource(materializationBudget(), quota()));
    auto desc = deferredDesc();
    desc.initialState = rhi::ResourceState::Undefined;
    auto first = graph.createTransientTexture(desc);
    graph.addPass("alias plan retained")
        .write(first, rhi::ResourceState::ColorAttachment)
        .execute([](render_graph::RenderContext&) {});
    graph.addPass("alias live consumer")
        .read(first, rhi::ResourceState::ShaderRead)
        .execute([](render_graph::RenderContext&) {});
    auto plan = render_graph::FrameGraphCompiler{}.compile(graph);
    assert(!plan.aliasPlan().empty());
    const auto misses = f.pool->report().missCount;
    rejects(
        [&] {
            graph.compile();
        },
        "RG-OWNED-RESOURCES-ALIAS-MATERIALIZATION-UNAVAILABLE");
    assert(!f.pool->allocationAccountingReport().textures);
    assert(f.pool->report().missCount == misses);
    assert(!f.pool->retirementFrameStorageReport().reservedFrames);
}

void mixedNativeOwners()
{
    auto native = core::makeUnique<tests::VulkanHeadlessRuntime>();
    assert(native->runtime() && native->runtime()->valid());
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native->runtime(), false);
    prepare(*device);
    auto f = core::makeUnique<Fixture>(*device, 4u, 2u, mixedAllocationBudget());
    auto accounting = f->accounting, persistent = f->persistent;
    std::weak_ptr<rhi_vulkan::VulkanRhiRuntime> runtime = native->runtime();
    std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> owner = native->nativeOwner();
    std::array<std::weak_ptr<rhi_vulkan::VulkanRhiNativeResourcePayload>, 4u> payloads;
    Compiled compiled;
    {
        Graph graph(*device, f->pool);
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(true), quota()));
        auto t = graph.createTexture(textureDesc());
        auto b = graph.createBuffer(bufferDesc());
        auto ti = graph.importTextureRef(core::shareUnique(device->createTexture(textureDesc())));
        auto bi = graph.importBufferRef(core::shareUnique(device->createBuffer(bufferDesc())));
        payloads = {rhi_vulkan::vulkanRhiNativePayload(graph.texture(t)),
            rhi_vulkan::vulkanRhiNativePayload(graph.buffer(b)),
            rhi_vulkan::vulkanRhiNativePayload(graph.texture(ti)),
            rhi_vulkan::vulkanRhiNativePayload(graph.buffer(bi))};
        compiled = graph.compile();
    }
    f->pool->closeRetirementMetadata();
    f.reset();
    device.reset();
    native.reset();
    assert(!runtime.expired() && !owner.expired() && accounting->liveBytes()
        && persistent->liveBytes());
    for (std::size_t i = 0u; i < payloads.size(); ++i) {
        auto payload = payloads[i].lock();
        assert(payload);
        VkMemoryRequirements requirements{};
        if (i % 2u == 0u)
            vkGetImageMemoryRequirements(payload->backendVulkanDeviceHandle(),
                payload->backendVulkanImageHandle(),
                &requirements);
        else
            vkGetBufferMemoryRequirements(payload->backendVulkanDeviceHandle(),
                payload->backendVulkanBufferHandle(),
                &requirements);
        assert(requirements.size);
    }
    compiled = {};
    for (const auto& payload : payloads)
        assert(payload.expired());
    assert(runtime.expired() && owner.expired() && !accounting->liveBytes()
        && !persistent->liveBytes());
    std::puts(
        "four genuine mixed Vulkan payloads retain original bundle/ledger/runtime until final source: PASS");
}

void nativeOwners()
{
    auto native = core::makeUnique<tests::VulkanHeadlessRuntime>();
    assert(native->runtime() && native->runtime()->valid());
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native->runtime(), false);
    prepare(*device);
    auto f = core::makeUnique<Fixture>(*device);
    auto accounting = f->accounting, persistent = f->persistent;
    std::weak_ptr<rhi_vulkan::VulkanRhiRuntime> runtime = native->runtime();
    std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> owner = native->nativeOwner();
    std::weak_ptr<rhi_vulkan::VulkanRhiNativeResourcePayload> image, buffer;
    Compiled compiled;
    {
        Graph graph(*device, f->pool);
        auto t = graph.createTexture(textureDesc());
        auto b = graph.createBuffer(bufferDesc());
        image = rhi_vulkan::vulkanRhiNativePayload(graph.texture(t));
        buffer = rhi_vulkan::vulkanRhiNativePayload(graph.buffer(b));
        assert(!graph.prepareCompiledRetirementSource(sourceBudget(), quota()));
        compiled = graph.compile();
    }
    f->pool->closeRetirementMetadata();
    f.reset();
    device.reset();
    native.reset();
    assert(!runtime.expired() && !owner.expired() && accounting->liveBytes()
        && persistent->liveBytes());
    {
        auto t = image.lock(), b = buffer.lock();
        assert(t && b);
        VkMemoryRequirements tr{}, br{};
        vkGetImageMemoryRequirements(
            t->backendVulkanDeviceHandle(), t->backendVulkanImageHandle(), &tr);
        vkGetBufferMemoryRequirements(
            b->backendVulkanDeviceHandle(), b->backendVulkanBufferHandle(), &br);
        assert(tr.size && br.size >= bufferDesc().sizeBytes);
    }
    compiled = {};
    assert(image.expired() && buffer.expired() && runtime.expired() && owner.expired());
    assert(!accounting->liveBytes() && !persistent->liveBytes());
    std::puts("actual graph-owned Vulkan payloads retain original receipt/storage/runtime: PASS");
}
#endif
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "contract";
    if (mode == "contract")
        contract();
    else if (mode == "failures")
        failures();
    else if (mode == "quota")
        rollback();
    else if (mode == "mixed") {
        auto device = rhi::createNullDevice();
        mixedLifetime(*device);
    } else if (mode == "mixed-rejection") {
        auto device = rhi::createNullDevice(), foreign = rhi::createNullDevice();
        mixedRejection(*device, *foreign);
    } else if (mode == "mixed-quota")
        mixedRollback();
    else if (mode == "materialization") {
        auto device = rhi::createNullDevice();
        materializationLifetime(*device);
    } else if (mode == "materialization-rejection") {
        auto device = rhi::createNullDevice();
        materializationFailures(*device);
        device = rhi::createNullDevice();
        materializationReceiptQuota(*device);
        device = rhi::createNullDevice();
        auto foreign = rhi::createNullDevice();
        mixedRejection(*device, *foreign, true);
    } else if (mode == "materialization-quota")
        materializationQuota();
    else if (mode == "owners") {
        auto device = rhi::createNullDevice();
        lifetime(*device);
    }
#ifdef ARP_TEST_VULKAN
    else if (mode == "materialization-native") {
        tests::VulkanHeadlessRuntime native;
        if (!native.runtime() || !native.runtime()->valid())
            return 77;
        auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        materializationLifetime(*device);
        device.reset();
        device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        materializationFailures(*device);
        device.reset();
        device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        materializationReceiptQuota(*device);
        device.reset();
        device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        auto foreign = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        mixedRejection(*device, *foreign, true);
        device.reset();
        foreign.reset();
        materializationAliasRefusal();
        materializationNativeOwners();
    } else if (mode == "mixed-native") {
        tests::VulkanHeadlessRuntime native;
        if (!native.runtime() || !native.runtime()->valid())
            return 77;
        auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        mixedLifetime(*device);
        device.reset();
        device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        auto foreign = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        mixedRejection(*device, *foreign);
        device.reset();
        foreign.reset();
        mixedNativeOwners();
    } else if (mode == "native") {
        tests::VulkanHeadlessRuntime native;
        if (!native.runtime() || !native.runtime()->valid())
            return 77;
        auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), false);
        lifetime(*device);
        device.reset();
        nativeOwners();
    }
#endif
    else
        return 2;
    std::puts("owned-resource retention transaction: PASS");
}
