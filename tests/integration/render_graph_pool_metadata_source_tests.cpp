#include "core/diagnostics/diagnostic_error.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_compiler.h"
#include "renderer/render_graph/frame_graph_resource_pool.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/semantics/frame_graph_retirement_budget.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace {
using namespace arp;
using Pool = render_graph::FrameGraphResourcePool;
using Reservation = Pool::RetirementMetadataReservation;
using Budget = rhi::semantics::FrameGraphRetirementMetadataBudget;
constexpr std::uint64_t fixtureCpuBudget = 64u * 1024u;

static_assert(!std::is_copy_constructible_v<Reservation>);
static_assert(!std::is_copy_assignable_v<Reservation>);
static_assert(std::is_nothrow_move_constructible_v<Reservation>);
static_assert(std::is_nothrow_move_assignable_v<Reservation>);
static_assert(!std::is_constructible_v<Reservation, core::PoolHandle>);
static_assert(!std::is_constructible_v<Reservation, Budget>);
static_assert(!std::is_constructible_v<Reservation, rhi::QueueSubmitResult>);
static_assert(!std::is_constructible_v<Reservation, rhi::GpuFence*>);

void expect(const char* actual, const char* expected)
{
    assert(actual && std::string_view(actual) == expected);
}

auto device()
{
    auto result = rhi::createNullDevice();
    assert(result);
    auto quota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    assert(!result->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, fixtureCpuBudget}, quota));
    return result;
}

auto prepare(Pool& pool, std::uint32_t capacity = 2u)
{
    auto quota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    assert(!pool.prepareRetirementMetadata({1u, true, capacity, fixtureCpuBudget}, quota));
    return quota;
}

void sourceAndLifetime()
{
    auto original = device();
    auto other = device();
    Pool pool(*original), foreign(*other);
    const char* diagnostic = nullptr;
    auto denied = pool.reserveRetirementMetadata(diagnostic);
    assert(denied.diagnostic());
    expect(diagnostic, "RG-RETIREMENT-METADATA-SOURCE");
    auto quota = prepare(pool, 1u);
    auto foreignQuota = prepare(foreign, 1u);
    Pool sameDevicePool(*original), unpreparedPool(*original);
    auto sameDeviceQuota = prepare(sameDevicePool, 1u);
    auto token = pool.reserveRetirementMetadata(diagnostic);
    assert(!diagnostic && !token.diagnostic() && !token.nativeExecutionProven());
    assert(!pool.validateRetirementMetadata(token));
    Reservation empty;
    expect(pool.validateRetirementMetadata(empty), "RG-RETIREMENT-METADATA-EMPTY");
    expect(pool.cancelRetirementMetadata(empty), "RG-RETIREMENT-METADATA-EMPTY");
    expect(foreign.validateRetirementMetadata(token), "RG-RETIREMENT-METADATA-FOREIGN");
    expect(foreign.cancelRetirementMetadata(token), "RG-RETIREMENT-METADATA-FOREIGN");
    expect(sameDevicePool.validateRetirementMetadata(token), "RG-RETIREMENT-METADATA-FOREIGN");
    expect(sameDevicePool.cancelRetirementMetadata(token), "RG-RETIREMENT-METADATA-FOREIGN");
    expect(unpreparedPool.validateRetirementMetadata(token), "RG-RETIREMENT-METADATA-SOURCE");
    expect(unpreparedPool.cancelRetirementMetadata(token), "RG-RETIREMENT-METADATA-SOURCE");
    auto exhausted = pool.reserveRetirementMetadata(diagnostic);
    expect(diagnostic, "CORE-POOL-CAPACITY");
    expect(exhausted.diagnostic(), "RG-RETIREMENT-METADATA-EMPTY");
    expect(pool.prepareRetirementMetadata({1u, true, 1u, fixtureCpuBudget}, quota),
        "RG-RETIREMENT-METADATA-BUSY");
    assert(!pool.validateRetirementMetadata(token));

    original->retireOriginalDeviceLifetime();
    expect(token.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
    expect(pool.validateRetirementMetadata(token), "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
    auto retired = pool.reserveRetirementMetadata(diagnostic);
    expect(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
    assert(!pool.cancelRetirementMetadata(token));
    expect(pool.cancelRetirementMetadata(token), "RG-RETIREMENT-METADATA-EMPTY");

    Reservation survivor;
    auto retainedQuota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    {
        auto shortDevice = device();
        Pool shortPool(*shortDevice);
        assert(
            !shortPool.prepareRetirementMetadata({1u, true, 1u, fixtureCpuBudget}, retainedQuota));
        survivor = shortPool.reserveRetirementMetadata(diagnostic);
        assert(!diagnostic && !survivor.diagnostic());
        shortDevice.reset();
        expect(survivor.diagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
        // These operations must reject before touching the destroyed facade.
        assert(shortPool.validateRetirementMetadata(survivor));
        auto expired = shortPool.reserveRetirementMetadata(diagnostic);
        assert(diagnostic && expired.diagnostic());
    }
    expect(survivor.diagnostic(), "RG-RETIREMENT-METADATA-CLOSED");
    assert(retainedQuota->liveBytes() > 0u);
    survivor = {};
    assert(retainedQuota->liveBytes() == 0u);
}

void transactionsAndReuse()
{
    auto original = device();
    Pool pool(*original);
    auto quota = prepare(pool, 2u);
    rhi::BufferDesc bufferDesc;
    bufferDesc.sizeBytes = 64u;
    bufferDesc.usage = rhi::ResourceUsage::StorageBuffer;
    auto buffer = pool.acquireBuffer(bufferDesc);
    const auto bufferId = buffer->id();
    pool.releaseBuffer(std::move(buffer));
    const auto before = pool.report();
    auto hot = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    const auto storage = quota->liveBytes();
    {
        core::CpuMemoryQuotaScope scope(hot);
        for (unsigned i = 0u; i < 10000u; ++i) {
            const char* diagnostic = nullptr;
            auto token = pool.reserveRetirementMetadata(diagnostic);
            assert(!diagnostic && !pool.validateRetirementMetadata(token));
            auto moved = std::move(token);
            expect(token.diagnostic(), "RG-RETIREMENT-METADATA-EMPTY");
            assert(!pool.cancelRetirementMetadata(moved));
            auto dropped = pool.reserveRetirementMetadata(diagnostic);
            assert(!diagnostic && !dropped.diagnostic());
            assert(quota->liveBytes() == storage);
        }
    }
    assert(hot->peakBytes() == 0u);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);

    auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
    expect(pool.prepareRetirementMetadata({1u, true, 2u, 1u}, tiny),
        "RG-RETIREMENT-METADATA-ALLOCATION");
    assert(tiny->liveBytes() == 0u && quota->liveBytes() == storage);
    const auto after = pool.report();
    assert(before.cachedBytes == after.cachedBytes && before.pendingBytes == after.pendingBytes
        && before.cachedResourceCount == after.cachedResourceCount
        && before.pendingResourceCount == after.pendingResourceCount
        && before.byteBudget == after.byteBudget && before.resourceBudget == after.resourceBudget
        && before.hitCount == after.hitCount && before.missCount == after.missCount
        && before.evictionCount == after.evictionCount
        && before.reuseFailureCount == after.reuseFailureCount
        && before.lastDiagnostic == after.lastDiagnostic);
    buffer = pool.acquireBuffer(bufferDesc);
    assert(buffer && buffer->id() == bufferId);
    pool.releaseBuffer(std::move(buffer));
    const char* diagnostic = nullptr;
    auto old = pool.reserveRetirementMetadata(diagnostic);
    assert(!diagnostic);
    {
        core::CpuMemoryQuotaScope scope(hot);
        pool.closeRetirementMetadata();
    }
    assert(hot->peakBytes() == 0u);
    expect(old.diagnostic(), "RG-RETIREMENT-METADATA-CLOSED");
    auto replacementQuota = prepare(pool, 1u);
    expect(pool.validateRetirementMetadata(old), "RG-RETIREMENT-METADATA-FOREIGN");
    auto replacement = pool.reserveRetirementMetadata(diagnostic);
    assert(!diagnostic && !pool.validateRetirementMetadata(replacement));
    replacement = std::move(replacement);
    assert(!pool.validateRetirementMetadata(replacement));
    old = {};
    assert(quota->liveBytes() == 0u);
    assert(!pool.cancelRetirementMetadata(replacement));
    assert(original->queue().submitCount() == 0u);
}

void quotaAndIntent()
{
    auto original = device();
    Pool pool(*original);
    auto quota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    expect(pool.prepareRetirementMetadata({}, {}), "RG-RETIREMENT-METADATA-VERSION");
    expect(
        pool.prepareRetirementMetadata({1u, false, 0u, 0u}, {}), "RG-RETIREMENT-METADATA-OPT-IN");
    expect(
        pool.prepareRetirementMetadata({1u, true, 0u, 0u}, {}), "RG-RETIREMENT-METADATA-CAPACITY");
    expect(pool.prepareRetirementMetadata({1u, true, 1u, 0u}, {}),
        "RG-RETIREMENT-METADATA-CPU-BUDGET");
    expect(pool.prepareRetirementMetadata({1u, true, 1u, fixtureCpuBudget}, {}),
        "RG-RETIREMENT-METADATA-QUOTA");
    auto absent = rhi::createNullDevice();
    Pool unprepared(*absent);
    expect(unprepared.prepareRetirementMetadata({1u, true, 1u, fixtureCpuBudget}, quota),
        "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
    assert(quota->liveBytes() == 0u);
    auto mismatched = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget - 1u);
    expect(pool.prepareRetirementMetadata({1u, true, 1u, fixtureCpuBudget}, mismatched),
        "RG-RETIREMENT-METADATA-QUOTA");
    auto sharedDevice = rhi::createNullDevice();
    assert(
        !sharedDevice->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, fixtureCpuBudget}, quota));
    {
        Pool shared(*sharedDevice);
        const auto before = quota->liveBytes();
        expect(shared.prepareRetirementMetadata({1u, true, 1u, fixtureCpuBudget}, quota),
            "RG-RETIREMENT-METADATA-QUOTA-DOMAIN");
        assert(quota->liveBytes() == before);
    }
    sharedDevice.reset();
    assert(quota->liveBytes() == 0u);
    // This is the existing logical-device decision, never native validation.
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)}) {
        const auto decision = rhi::OriginalDeviceLifetimeContract::lowering(backend);
        assert(!decision.executionAvailable && !decision.resourceIssuerAvailable);
        const bool supported = backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan
            || backend == rhi::Backend::Metal || backend == rhi::Backend::WebGPU;
        assert(decision.logicalLifetimeAvailable == supported);
        assert((decision.diagnostic == nullptr) == supported);
    }
    {
        Pool measured(*original);
        assert(!measured.prepareRetirementMetadata({1u, true, 3u, fixtureCpuBudget}, quota));
    }
    const auto exact = quota->peakBytes();
    assert(exact > 1u && exact < fixtureCpuBudget && quota->liveBytes() == 0u);
    for (std::uint64_t bytes = 1u; bytes <= exact; ++bytes) {
        auto boundary = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            Pool checked(*original);
            const auto* diagnostic =
                checked.prepareRetirementMetadata({1u, true, 3u, bytes}, boundary);
            if (bytes == exact)
                assert(!diagnostic);
            else
                expect(diagnostic, "RG-RETIREMENT-METADATA-ALLOCATION");
        }
        assert(boundary->liveBytes() == 0u && boundary->peakBytes() <= bytes);
    }
    std::printf("original pool three-slot CPU charge: %llu bytes\n",
        static_cast<unsigned long long>(exact));
}

auto prepareGraph(render_graph::FrameGraph& graph, std::uint32_t resources = 4u)
{
    auto quota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    assert(!graph.prepareCompiledRetirementSource({1u, true, resources, fixtureCpuBudget}, quota));
    return quota;
}

void expectCompileFailure(render_graph::FrameGraph& graph, const char* expected)
{
    bool rejected = false;
    try {
        graph.compile();
    } catch (const core::DiagnosticError& error) {
        expect(error.what(), expected);
        rejected = true;
    }
    assert(rejected);
}

rhi::TextureDesc sourceTextureDesc()
{
    rhi::TextureDesc desc;
    desc.extent = {8u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment;
    return desc;
}

void compiledSource()
{
    using namespace render_graph;
    auto original = device();
    auto pool = core::makeShared<Pool>(*original);
    auto poolQuota = prepare(*pool, 4u);
    FrameGraph graph(*original, pool), foreign(*original, pool);
    auto sourceQuota = prepareGraph(graph);
    auto foreignQuota = prepareGraph(foreign);
    graph.createTransientTexture(sourceTextureDesc());
    rhi::BufferDesc buffer;
    buffer.sizeBytes = 64u;
    buffer.usage = rhi::ResourceUsage::StorageBuffer;
    graph.createBuffer(buffer);
    graph.addPass("source-pass").externalSideEffects(true);
    auto compiled = graph.compile();
    assert(!compiled.retirementSourceDiagnostic()
        && !graph.validateCompiledRetirementSource(compiled));
    expect(foreign.validateCompiledRetirementSource(compiled), "RG-COMPILED-SOURCE-GRAPH");
    CompiledFrameGraph manual;
    expect(graph.validateCompiledRetirementSource(manual), "RG-COMPILED-SOURCE-MISSING");
    auto pure = FrameGraphCompiler{}.compile(graph);
    expect(pure.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-MISSING");
    auto copied = compiled;
    auto moved = std::move(copied);
    expect(copied.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-MISSING");
    assert(!graph.validateCompiledRetirementSource(moved));
    auto hot = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(hot);
        for (unsigned i = 0u; i < 10000u; ++i) {
            assert(!compiled.retirementSourceDiagnostic());
            assert(!graph.validateCompiledRetirementSource(compiled));
            expect(foreign.validateCompiledRetirementSource(compiled), "RG-COMPILED-SOURCE-GRAPH");
        }
    }
    assert(hot->peakBytes() == 0u);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    graph.reset();
    expect(compiled.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EPOCH");
    expect(graph.validateCompiledRetirementSource(moved), "RG-COMPILED-SOURCE-EPOCH");

    CompiledFrameGraph survivor;
    std::shared_ptr<core::CpuMemoryQuota> survivorQuota;
    {
        FrameGraph shortGraph(*original, pool);
        survivorQuota = prepareGraph(shortGraph);
        survivor = shortGraph.compile();
        assert(!survivor.retirementSourceDiagnostic());
    }
    expect(survivor.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EXPIRED");
    assert(survivorQuota->liveBytes() > 0u);
    survivor = {};
    assert(survivorQuota->liveBytes() == 0u);

    {
        FrameGraph materialized(*original, pool);
        auto quota = prepareGraph(materialized);
        const auto handle = materialized.createTransientTexture(sourceTextureDesc());
        materialized.addPass("materialize")
            .write(handle, rhi::ResourceState::ColorAttachment)
            .execute([](RenderContext&) {});
        auto before = materialized.compile();
        assert(!materialized.validateCompiledRetirementSource(before));
        materialized.execute(before);
        assert(materialized.texture(handle).id().isValid());
        assert(!before.retirementSourceDiagnostic());
        expect(materialized.validateCompiledRetirementSource(before),
            "RG-COMPILED-SOURCE-RESOURCE-MISMATCH");
        auto after = materialized.compile();
        assert(!materialized.validateCompiledRetirementSource(after));
        auto replacementQuota = prepareGraph(materialized);
        expect(after.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EXPIRED");
        auto replaced = materialized.compile();
        assert(!materialized.validateCompiledRetirementSource(replaced));
    }
    {
        FrameGraph imported(*original, pool);
        auto quota = prepareGraph(imported);
        auto texture = core::shareUnique(original->createTexture(sourceTextureDesc()));
        assert(texture);
        const auto handle = imported.importTextureRef(texture);
        auto before = imported.compile();
        assert(imported.importTextureRef(texture).id == handle.id);
        assert(!imported.validateCompiledRetirementSource(before));
        imported.setExternalFinalState(handle, rhi::ResourceState::ColorAttachment);
        expect(before.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EPOCH");
        before = {};
        auto after = imported.compile();
        imported.discardImportedContentsBeforeFirstWrite(handle);
        expect(after.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EPOCH");
    }
    auto otherDevice = device();
    FrameGraph wrongDevice(*otherDevice, pool);
    auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    expect(
        wrongDevice.prepareCompiledRetirementSource({1u, true, 0u, fixtureCpuBudget}, wrongQuota),
        "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
    assert(wrongQuota->liveBytes() == 0u);
    original->retireOriginalDeviceLifetime();
    expectCompileFailure(foreign, "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
}

void compiledMutations()
{
    using namespace render_graph;
    auto original = device();
    auto pool = core::makeShared<Pool>(*original);
    auto poolQuota = prepare(*pool, 4u);
    FrameGraph graph(*original, pool);
    auto sourceQuota = prepareGraph(graph);
    graph.addPass("mutations").externalSideEffects(true);
    auto compiled = graph.compile();
    using Mutation = void (*)(CompiledFrameGraph&);
    const std::array<Mutation, 17> mutations{[](auto& c) {
                                                 c.addPass({});
                                             },
        [](auto& c) {
            c.addExecutablePass(0u);
        },
        [](auto& c) {
            c.addResourceLifetime({});
        },
        [](auto& c) {
            c.addBarrier({});
        },
        [](auto& c) {
            c.addAlias({});
        },
        [](auto& c) {
            c.addLoadStore({});
        },
        [](auto& c) {
            c.addQueuePlan({});
        },
        [](auto& c) {
            c.addExternalResource({});
        },
        [](auto& c) {
            c.addPassBarrierBatch({});
        },
        [](auto& c) {
            c.addPassResourceUse(0u, {});
        },
        [](auto& c) {
            c.addDependency({});
        },
        [](auto& c) {
            c.addRenderTargetFlow({});
        },
        [](auto& c) {
            c.addSubpassTopology({});
        },
        [](auto& c) {
            c.addAliasHandoff({});
        },
        [](auto& c) {
            c.setTransientResourceBudget(0u, 0u);
        },
        [](auto& c) {
            c.addSubgraph({});
        },
        [](auto& c) {
            c.setCommandEncoderDesc({});
        }};
    for (const auto mutate : mutations) {
        auto copy = compiled;
        mutate(copy);
        expect(copy.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-MISSING");
        assert(!graph.validateCompiledRetirementSource(compiled));
    }

    using Authoring = void (*)(FrameGraph&, PassBuilder&);
    const std::array<Authoring, 10> authoring{[](auto& g, auto&) {
                                                  g.addPass("new-pass");
                                              },
        [](auto& g, auto&) {
            g.setCommandEncoderDesc({});
        },
        [](auto& g, auto&) {
            g.setPassCullingEnabled(true);
        },
        [](auto& g, auto&) {
            g.setGpuTimingPassScopes({});
        },
        [](auto& g, auto&) {
            g.createTransientTexture(sourceTextureDesc());
        },
        [](auto&, auto& p) {
            p.kind(PassKind::Copy);
        },
        [](auto&, auto& p) {
            p.queue(QueueRole::Graphics);
        },
        [](auto&, auto& p) {
            p.asyncComputeEligible(false);
        },
        [](auto&, auto& p) {
            p.externalSideEffects(true);
        },
        [](auto&, auto& p) {
            p.execute([](RenderContext&) {});
        }};
    for (const auto author : authoring) {
        FrameGraph edited(*original, pool);
        auto quota = prepareGraph(edited);
        auto pass = edited.addPass("authored");
        auto before = edited.compile();
        author(edited, pass);
        expect(before.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EPOCH");
    }
    for (bool subpass : {false, true}) {
        FrameGraph composition(*original, pool);
        auto quota = prepareGraph(composition);
        auto before = composition.compile();
        if (subpass)
            composition.addSubpassGraph("unsupported-v1");
        else
            composition.addSubgraph({});
        expect(before.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-EPOCH");
        expectCompileFailure(composition, "RG-COMPILED-SOURCE-COMPOSITION");
        expect(composition.prepareCompiledRetirementSource({1u, true, 0u, fixtureCpuBudget}, quota),
            "RG-COMPILED-SOURCE-COMPOSITION");
    }
}

void compiledAssignment()
{
    using namespace render_graph;
    auto original = device();
    auto pool = core::makeShared<Pool>(*original);
    auto poolQuota = prepare(*pool, 2u);
    FrameGraph first(*original, pool), second(*original, pool);
    auto firstQuota = prepareGraph(first);
    auto secondQuota = prepareGraph(second);
    first.addPass("first").externalSideEffects(true);
    second.addPass("second-a").externalSideEffects(true);
    second.addPass("second-b").externalSideEffects(true);
    auto lhs = first.compile();
    auto rhs = second.compile();
    auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
    bool failed = false;
    {
        core::CpuMemoryQuotaScope scope(tiny);
        try {
            lhs = rhs;
        } catch (const std::bad_alloc&) {
            failed = true;
        }
    }
    assert(failed && tiny->liveBytes() == 0u);
    assert(lhs.passCount() == 1u && lhs.passes()[0].name == "first");
    assert(!first.validateCompiledRetirementSource(lhs));
    expect(second.validateCompiledRetirementSource(lhs), "RG-COMPILED-SOURCE-GRAPH");
    assert(!second.validateCompiledRetirementSource(rhs));

    auto measured = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    {
        auto copy = lhs;
        {
            core::CpuMemoryQuotaScope scope(measured);
            copy = rhs;
        }
        assert(!second.validateCompiledRetirementSource(copy) && copy.passCount() == 2u);
    }
    const auto exact = measured->peakBytes();
    assert(exact > 1u && exact < fixtureCpuBudget && measured->liveBytes() == 0u);
    for (std::uint64_t bytes = 1u; bytes <= exact; ++bytes) {
        auto boundary = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            auto candidate = lhs;
            bool rejected = false;
            {
                core::CpuMemoryQuotaScope scope(boundary);
                try {
                    candidate = rhs;
                } catch (const std::bad_alloc&) {
                    rejected = true;
                }
            }
            assert(rejected == (bytes < exact));
            if (rejected) {
                assert(candidate.passCount() == 1u && candidate.passes()[0].name == "first");
                assert(!first.validateCompiledRetirementSource(candidate));
                expect(
                    second.validateCompiledRetirementSource(candidate), "RG-COMPILED-SOURCE-GRAPH");
            } else {
                assert(candidate.passCount() == 2u
                    && !second.validateCompiledRetirementSource(candidate));
            }
        }
        assert(boundary->liveBytes() == 0u && boundary->peakBytes() <= bytes);
    }

    // Existing PMR copy selects the current resource. Whole-object move must
    // retain that resource even when the destination has another allocator.
    std::pmr::monotonic_buffer_resource alternate(&core::defaultCpuMemoryResource());
    auto* previous = std::pmr::set_default_resource(&alternate);
    auto differentAllocator = rhs;
    std::pmr::set_default_resource(previous);
    assert(differentAllocator.passes().get_allocator().resource() == &alternate);
    const auto* data = differentAllocator.passes().data();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(tiny);
        previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        lhs = lhs;
        lhs = std::move(lhs);
        assert(!first.validateCompiledRetirementSource(lhs));
        lhs = std::move(differentAllocator);
        rhs = rhs;
        rhs = std::move(rhs);
        std::pmr::set_default_resource(previous);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(tiny->peakBytes() == 0u);
    assert(lhs.passes().data() == data && lhs.passes().get_allocator().resource() == &alternate);
    assert(lhs.passCount() == 2u && !second.validateCompiledRetirementSource(lhs));
    expect(differentAllocator.retirementSourceDiagnostic(), "RG-COMPILED-SOURCE-MISSING");
    lhs = {};
    std::printf("compiled assignment full-copy CPU charge: %llu bytes\n",
        static_cast<unsigned long long>(exact));
}

void compiledQuota()
{
    using namespace render_graph;
    auto original = device();
    auto pool = core::makeShared<Pool>(*original);
    auto poolQuota = prepare(*pool, 1u);
    FrameGraph graph(*original, pool);
    auto sourceQuota = prepareGraph(graph);
    auto compiled = graph.compile();
    auto copy = compiled;
    expectCompileFailure(graph, "CORE-POOL-CAPACITY");
    compiled = {};
    expectCompileFailure(graph, "CORE-POOL-CAPACITY");
    copy = {};
    compiled = graph.compile();
    assert(!graph.validateCompiledRetirementSource(compiled));
    auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
    expect(graph.prepareCompiledRetirementSource({1u, true, 0u, 1u}, tiny),
        "RG-COMPILED-SOURCE-ALLOCATION");
    assert(tiny->liveBytes() == 0u && !graph.validateCompiledRetirementSource(compiled));
    expect(graph.prepareCompiledRetirementSource({}, {}), "RG-COMPILED-SOURCE-VERSION");
    expect(graph.prepareCompiledRetirementSource({1u, false, 0u, 0u}, {}),
        "RG-COMPILED-SOURCE-OPT-IN");
    expect(graph.prepareCompiledRetirementSource({1u, true, 0u, 0u}, {}),
        "RG-COMPILED-SOURCE-CPU-BUDGET");
    expect(graph.prepareCompiledRetirementSource({1u, true, 0u, fixtureCpuBudget}, {}),
        "RG-COMPILED-SOURCE-QUOTA");
    expect(graph.prepareCompiledRetirementSource({1u, true, 0u, fixtureCpuBudget}, poolQuota),
        "RG-COMPILED-SOURCE-QUOTA-DOMAIN");
    compiled = {};
    for (const std::uint32_t bindingCount : {0u, 2u}) {
        FrameGraph measured(*original, pool);
        for (std::uint32_t i = 0u; i < bindingCount; ++i)
            measured.createTransientTexture(sourceTextureDesc());
        auto quota = prepareGraph(measured, bindingCount);
        auto c = measured.compile();
        const auto exact = quota->peakBytes();
        assert(exact && exact < fixtureCpuBudget);
        c = {};
        // Every byte quota either rejects preparation or rejects source
        // publication. In both cases all temporary slots/CPU storage roll back.
        for (std::uint64_t bytes = 1u; bytes <= exact; ++bytes) {
            auto boundary = core::makeShared<core::CpuMemoryQuota>(bytes);
            {
                FrameGraph checked(*original, pool);
                for (std::uint32_t i = 0u; i < bindingCount; ++i)
                    checked.createTransientTexture(sourceTextureDesc());
                const auto* diagnostic = checked.prepareCompiledRetirementSource(
                    {1u, true, bindingCount, bytes}, boundary);
                if (diagnostic)
                    expect(diagnostic, "RG-COMPILED-SOURCE-ALLOCATION");
                else if (bytes < exact) {
                    const auto cold = boundary->liveBytes();
                    expectCompileFailure(checked, "RG-COMPILED-SOURCE-ALLOCATION");
                    assert(boundary->liveBytes() == cold);
                } else {
                    auto valid = checked.compile();
                    assert(!checked.validateCompiledRetirementSource(valid));
                }
            }
            assert(boundary->liveBytes() == 0u && boundary->peakBytes() <= bytes);
        }
        std::printf("graph source %u bindings CPU charge: %llu bytes\n",
            bindingCount,
            static_cast<unsigned long long>(exact));
    }
    FrameGraph limited(*original, pool);
    auto limitedQuota = prepareGraph(limited, 0u);
    limited.createTransientTexture(sourceTextureDesc());
    expectCompileFailure(limited, "RG-COMPILED-SOURCE-RESOURCE-CAPACITY");
    limited.reset();
    auto empty = limited.compile();
    assert(!limited.validateCompiledRetirementSource(empty));
}

auto storageBudget(std::uint32_t frames = 2u)
{
    return rhi::semantics::FrameGraphRetirementFrameStorageBudget{
        1u, true, frames, 2u, 2u, 1u, 2u, fixtureCpuBudget};
}

void persistentStorage()
{
    auto original = device();
    Pool pool(*original);
    auto metadataQuota = prepare(pool, 3u);
    const char* diagnostic = nullptr;
    {
        auto before = pool.reserveRetirementMetadata(diagnostic);
        expect(before.frameStorageDiagnostic(), "RG-FRAME-STORAGE-SOURCE");
    }
    auto budget = storageBudget();
    auto quota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    assert(!pool.prepareRetirementFrameStorage(budget, quota));
    const auto cold = pool.retirementFrameStorageReport();
    assert(cold.capacity.maxFrames == 2u && cold.reservedFrames == 0u);
    assert(cold.allocatedCpuMetadataBytes == quota->liveBytes() && quota->liveBytes() > 0u);
    auto first = pool.reserveRetirementMetadata(diagnostic);
    auto second = pool.reserveRetirementMetadata(diagnostic);
    assert(!diagnostic && !first.frameStorageDiagnostic() && !second.frameStorageDiagnostic());
    auto denied = pool.reserveRetirementMetadata(diagnostic);
    expect(diagnostic, "RG-FRAME-STORAGE-FRAME-CAPACITY");
    expect(denied.diagnostic(), "RG-RETIREMENT-METADATA-EMPTY");
    assert(pool.retirementFrameStorageReport().reservedFrames == 2u);
    auto replacementQuota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    expect(pool.prepareRetirementFrameStorage(budget, replacementQuota), "RG-FRAME-STORAGE-BUSY");
    assert(replacementQuota->liveBytes() == 0u);
    first = std::move(second);
    assert(!first.frameStorageDiagnostic()
        && pool.retirementFrameStorageReport().reservedFrames == 1u);
    first = std::move(first);
    assert(!pool.cancelRetirementMetadata(first));
    assert(pool.retirementFrameStorageReport().reservedFrames == 0u);
    auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(tiny);
        for (unsigned i = 0u; i < 10000u; ++i) {
            auto token = pool.reserveRetirementMetadata(diagnostic);
            assert(
                !diagnostic && !token.frameStorageDiagnostic() && !token.nativeExecutionProven());
            assert(pool.retirementFrameStorageReport().reservedFrames == 1u);
            if (i % 2u)
                assert(!pool.cancelRetirementMetadata(token));
        }
    }
    assert(tiny->peakBytes() == 0u);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(pool.retirementFrameStorageReport().reservedFrames == 0u);
    assert(quota->liveBytes() == cold.allocatedCpuMetadataBytes);
    // A registry capacity failure cannot mark an otherwise free bundle busy.
    assert(!pool.prepareRetirementMetadata({1u, true, 1u, fixtureCpuBudget}, metadataQuota));
    assert(quota->liveBytes() == 0u);
    assert(!pool.prepareRetirementFrameStorage(budget, quota));
    first = pool.reserveRetirementMetadata(diagnostic);
    second = pool.reserveRetirementMetadata(diagnostic);
    expect(diagnostic, "CORE-POOL-CAPACITY");
    assert(pool.retirementFrameStorageReport().reservedFrames == 1u);
    first = {};
    assert(pool.retirementFrameStorageReport().reservedFrames == 0u);

    Reservation survivor;
    auto lifetimeQuota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    {
        Pool owner(*original);
        auto ownerQuota = prepare(owner);
        assert(!owner.prepareRetirementFrameStorage(budget, lifetimeQuota));
        survivor = owner.reserveRetirementMetadata(diagnostic);
        owner.closeRetirementMetadata();
        expect(survivor.frameStorageDiagnostic(), "RG-RETIREMENT-METADATA-CLOSED");
        assert(owner.retirementFrameStorageReport().reservedFrames == 0u);
    }
    assert(lifetimeQuota->liveBytes() > 0u);
    survivor = {};
    assert(lifetimeQuota->liveBytes() == 0u);
    {
        auto shortDevice = device();
        Pool owner(*shortDevice);
        auto ownerQuota = prepare(owner);
        assert(!owner.prepareRetirementFrameStorage(budget, lifetimeQuota));
        survivor = owner.reserveRetirementMetadata(diagnostic);
        shortDevice.reset();
        expect(survivor.frameStorageDiagnostic(), "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
        auto rejected = owner.reserveRetirementMetadata(diagnostic);
        expect(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED");
        survivor = {};
        assert(owner.retirementFrameStorageReport().reservedFrames == 0u);
    }
    assert(lifetimeQuota->liveBytes() == 0u);
}

void storageQuota()
{
    using Contract = rhi::semantics::FrameGraphRetirementFrameStorageContract;
    auto original = device();
    Pool pool(*original);
    auto budget = storageBudget();
    auto quota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    expect(pool.prepareRetirementFrameStorage({}, {}), "RG-FRAME-STORAGE-VERSION");
    auto invalid = budget;
    invalid.prototypeOptIn = false;
    expect(pool.prepareRetirementFrameStorage(invalid, {}), "RG-FRAME-STORAGE-OPT-IN");
    invalid = budget;
    invalid.maxFrames = 0u;
    expect(pool.prepareRetirementFrameStorage(invalid, {}), "RG-FRAME-STORAGE-CAPACITY");
    invalid = budget;
    invalid.maxCpuMetadataBytes = 0u;
    expect(pool.prepareRetirementFrameStorage(invalid, {}), "RG-FRAME-STORAGE-CPU-BUDGET");
    expect(pool.prepareRetirementFrameStorage(budget, {}), "RG-FRAME-STORAGE-QUOTA");
    expect(pool.prepareRetirementFrameStorage(budget, quota), "RG-RETIREMENT-METADATA-SOURCE");
    auto metadataQuota = prepare(pool);
    rhi::BufferDesc cachedDesc;
    cachedDesc.sizeBytes = 64u;
    cachedDesc.usage = rhi::ResourceUsage::StorageBuffer;
    auto cached = pool.acquireBuffer(cachedDesc);
    const auto* cachedIdentity = cached.get();
    pool.releaseBuffer(std::move(cached));
    expect(
        pool.prepareRetirementFrameStorage(budget, metadataQuota), "RG-FRAME-STORAGE-QUOTA-DOMAIN");
    assert(!pool.prepareRetirementFrameStorage(budget, quota));
    const auto exact = quota->liveBytes();
    assert(exact > 1u && exact < fixtureCpuBudget);
    for (std::uint64_t bytes = 1u; bytes <= exact; ++bytes) {
        auto boundary = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto request = budget;
        request.maxCpuMetadataBytes = bytes;
        const auto before = pool.retirementFrameStorageReport();
        const auto* id = pool.prepareRetirementFrameStorage(request, boundary);
        if (bytes < exact) {
            expect(id, "RG-FRAME-STORAGE-ALLOCATION");
            assert(boundary->liveBytes() == 0u);
            const auto after = pool.retirementFrameStorageReport();
            assert(after.allocatedCpuMetadataBytes == before.allocatedCpuMetadataBytes);
            assert(after.capacity.maxCpuMetadataBytes == before.capacity.maxCpuMetadataBytes);
            assert(pool.report().cachedResourceCount == 1u && pool.report().cachedBytes == 64u);
            const char* diagnostic = nullptr;
            auto token = pool.reserveRetirementMetadata(diagnostic);
            assert(!diagnostic && !token.frameStorageDiagnostic());
        } else {
            assert(!id && boundary->liveBytes() == exact);
            assert(
                !pool.prepareRetirementMetadata({1u, true, 2u, fixtureCpuBudget}, metadataQuota));
            assert(boundary->liveBytes() == 0u);
        }
        assert(boundary->peakBytes() <= bytes);
    }
    assert(quota->liveBytes() == 0u);
    cached = pool.acquireBuffer(cachedDesc);
    assert(cached.get() == cachedIdentity);
    assert(!Contract::validateCapacity(budget, 2u, 2u, 1u, 2u));
    expect(Contract::validateCapacity(budget, 3u, 3u, 2u, 3u), "RG-FRAME-STORAGE-TEXTURE-CAPACITY");
    expect(Contract::validateCapacity(budget, 2u, 3u, 2u, 3u), "RG-FRAME-STORAGE-BUFFER-CAPACITY");
    expect(Contract::validateCapacity(budget, 2u, 2u, 2u, 3u), "RG-FRAME-STORAGE-HEAP-CAPACITY");
    expect(
        Contract::validateCapacity(budget, 2u, 2u, 1u, 3u), "RG-FRAME-STORAGE-REFERENCE-CAPACITY");
    budget.maxTexturesPerFrame = budget.maxBuffersPerFrame = budget.maxAliasHeapsPerFrame =
        budget.maxReferencesPerFrame = 0u;
    assert(!pool.prepareRetirementFrameStorage(budget, quota));
    assert(!Contract::validateCapacity(budget, 0u, 0u, 0u, 0u));
    expect(Contract::validateCapacity(budget, SIZE_MAX, 0u, 0u, 0u),
        "RG-FRAME-STORAGE-TEXTURE-CAPACITY");
    pool.closeRetirementMetadata();
    expect(pool.prepareRetirementFrameStorage(budget, quota), "RG-RETIREMENT-METADATA-CLOSED");
    std::printf("persistent frame storage measured CPU charge: %llu bytes\n",
        static_cast<unsigned long long>(exact));
}

void storageSource()
{
    using namespace render_graph;
    for (unsigned kind = 0u; kind < 5u; ++kind) {
        auto original = device();
        auto pool = core::makeShared<Pool>(*original);
        auto metadataQuota = prepare(*pool, 2u);
        auto budget = storageBudget();
        budget.maxTexturesPerFrame = budget.maxBuffersPerFrame = budget.maxReferencesPerFrame = 0u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
        assert(!pool->prepareRetirementFrameStorage(budget, quota));
        FrameGraph graph(*original, pool);
        auto sourceQuota = prepareGraph(graph);
        rhi::BufferDesc buffer;
        buffer.sizeBytes = 64u;
        buffer.usage = rhi::ResourceUsage::StorageBuffer;
        if (kind == 0u)
            graph.createTransientTexture(sourceTextureDesc());
        else if (kind == 1u)
            graph.createBuffer(buffer);
        else if (kind == 2u)
            graph.importTextureRef(core::shareUnique(original->createTexture(sourceTextureDesc())));
        else if (kind == 3u)
            graph.importBufferRef(core::shareUnique(original->createBuffer(buffer)));
        else
            graph.importTexture(original->createTexture(sourceTextureDesc()));
        expectCompileFailure(graph,
            (kind == 0u || kind == 4u) ? "RG-FRAME-STORAGE-TEXTURE-CAPACITY"
                : kind == 1u           ? "RG-FRAME-STORAGE-BUFFER-CAPACITY"
                                       : "RG-FRAME-STORAGE-REFERENCE-CAPACITY");
        assert(pool->retirementFrameStorageReport().reservedFrames == 0u);
        if (kind == 0u || kind == 4u)
            budget.maxTexturesPerFrame = 1u;
        else if (kind == 1u)
            budget.maxBuffersPerFrame = 1u;
        else
            budget.maxReferencesPerFrame = 1u;
        assert(!pool->prepareRetirementFrameStorage(budget, quota));
        auto compiled = graph.compile();
        auto copied = compiled;
        assert(!graph.validateCompiledRetirementSource(copied));
        assert(pool->retirementFrameStorageReport().reservedFrames == 1u);
        compiled = {};
        assert(pool->retirementFrameStorageReport().reservedFrames == 1u);
        copied = {};
        assert(pool->retirementFrameStorageReport().reservedFrames == 0u);
        auto limitedQuota = core::makeShared<core::CpuMemoryQuota>(sourceQuota->liveBytes());
        assert(!graph.prepareCompiledRetirementSource(
            {1u, true, 4u, limitedQuota->limitBytes()}, limitedQuota));
        expectCompileFailure(graph, "RG-COMPILED-SOURCE-ALLOCATION");
        assert(pool->retirementFrameStorageReport().reservedFrames == 0u);
    }
    auto original = device();
    auto pool = core::makeShared<Pool>(*original);
    auto metadataQuota = prepare(*pool);
    FrameGraph graph(*original, pool);
    auto sharedQuota = prepareGraph(graph);
    assert(!pool->prepareRetirementFrameStorage(storageBudget(), sharedQuota));
    expectCompileFailure(graph, "RG-COMPILED-SOURCE-QUOTA-DOMAIN");
    assert(pool->retirementFrameStorageReport().reservedFrames == 0u);
    expect(graph.prepareCompiledRetirementSource({1u, true, 4u, fixtureCpuBudget}, sharedQuota),
        "RG-COMPILED-SOURCE-QUOTA-DOMAIN");

    // Exercise failure after the token has moved into the source constructor:
    // a subsequent snapshot allocation must release its original bundle too.
    auto checkedPool = core::makeShared<Pool>(*original);
    auto checkedMetadata = prepare(*checkedPool);
    auto storageQuota = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    assert(!checkedPool->prepareRetirementFrameStorage(storageBudget(), storageQuota));
    auto measured = core::makeShared<core::CpuMemoryQuota>(fixtureCpuBudget);
    {
        FrameGraph checked(*original, checkedPool);
        checked.createTransientTexture(sourceTextureDesc());
        assert(
            !checked.prepareCompiledRetirementSource({1u, true, 1u, fixtureCpuBudget}, measured));
        auto valid = checked.compile();
        assert(checkedPool->retirementFrameStorageReport().reservedFrames == 1u);
    }
    const auto exact = measured->peakBytes();
    assert(exact && exact < fixtureCpuBudget && measured->liveBytes() == 0u);
    const auto persistentCharge = storageQuota->liveBytes();
    for (std::uint64_t bytes = 1u; bytes <= exact; ++bytes) {
        auto boundary = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            FrameGraph checked(*original, checkedPool);
            checked.createTransientTexture(sourceTextureDesc());
            const auto* id =
                checked.prepareCompiledRetirementSource({1u, true, 1u, bytes}, boundary);
            if (id)
                expect(id, "RG-COMPILED-SOURCE-ALLOCATION");
            else if (bytes < exact)
                expectCompileFailure(checked, "RG-COMPILED-SOURCE-ALLOCATION");
            else {
                auto valid = checked.compile();
                assert(!checked.validateCompiledRetirementSource(valid));
                assert(checkedPool->retirementFrameStorageReport().reservedFrames == 1u);
            }
        }
        assert(boundary->liveBytes() == 0u && boundary->peakBytes() <= bytes);
        assert(checkedPool->retirementFrameStorageReport().reservedFrames == 0u);
        assert(storageQuota->liveBytes() == persistentCharge);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2) {
        const std::string_view mode(argv[1]);
        if (mode == "compiled-source")
            compiledSource();
        else if (mode == "compiled-mutations")
            compiledMutations();
        else if (mode == "compiled-quota")
            compiledQuota();
        else if (mode == "compiled-assignment")
            compiledAssignment();
        else if (mode == "compiled-storage")
            persistentStorage();
        else if (mode == "compiled-storage-quota")
            storageQuota();
        else if (mode == "compiled-storage-source")
            storageSource();
        else
            return 2;
        std::puts("FrameGraphCompiledRetirementSource/1 CPU provenance only: PASS");
        return 0;
    }
    assert(argc == 1);
    sourceAndLifetime();
    transactionsAndReuse();
    quotaAndIntent();
    std::puts("FrameGraphPoolMetadataSource/1 CPU source only: PASS");
}
