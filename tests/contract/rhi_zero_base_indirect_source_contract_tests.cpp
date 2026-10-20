#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/zero_base_indirect_source_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/gpu_extensions.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#ifdef NDEBUG
#error Immutable zero-base source contracts require assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using Contract = ZeroBaseIndirectSourceConsumptionContract;
using Args = IndirectArgumentConsumptionContract;
using Order = core::UnsignedElementSpan::ByteOrder;
using Schema = IndexedIndirectCommandSchema;

void expect(const char* actual, const char* expected)
{
    assert(actual && !std::strcmp(actual, expected));
}

using Fixture = test::ZeroBaseIndirectSourceFixture;

void schema()
{
    for (const auto order : {Order::LittleEndian, Order::BigEndian})
        for (const auto base : {INT32_MIN, -2, 0, INT32_MAX}) {
            const DrawIndexedIndirectCommand input{UINT32_MAX, 2u, UINT32_MAX, base, UINT32_MAX};
            const auto bytes = test::IndirectArgumentFixture<1u>::encode(input, order);
            std::array<std::uint8_t, 21u> unaligned{};
            std::copy(bytes.begin(), bytes.end(), unaligned.begin() + 1u);
            DrawIndexedIndirectCommand output;
            assert(!Schema::decode(unaligned.data() + 1u, 20u, order, output)
                && Schema::words(output) == Schema::words(input));
            std::array<std::uint8_t, 20u> canonical{};
            assert(Schema::encodeLittleEndian(
                Schema::words(output), canonical.data(), canonical.size()));
            assert(!Schema::decode(canonical.data(), 20u, Order::LittleEndian, output)
                && Schema::words(output) == Schema::words(input));
            expect(
                Schema::decode(bytes.data(), 19u, order, output), "RHI-INDIRECT-ARGUMENT-SCHEMA");
            assert(Schema::words(output) == Schema::Words{});
            assert(!Schema::encodeLittleEndian(Schema::words(input), nullptr, 20u));
            assert(!Schema::encodeLittleEndian(Schema::words(input), canonical.data(), 19u));
            expect(Schema::decode(nullptr, 20u, order, output), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
            expect(Schema::decode(bytes.data(), 20u, Order::Unknown, output),
                "CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN");
            assert(Schema::words(output) == Schema::Words{});
        }
}

void contentAndMapping()
{
    Fixture little, big(Order::BigEndian);
    auto a = little.make(), b = big.make();
    assert(!a.diagnostic() && !b.diagnostic() && a.sourceBytes() == b.sourceBytes());
    assert(a.retainedBytes() == Fixture::budget().maxSnapshotBytes);
    little.source.fill(0u);
    little.index.fill(0u);
    Contract::Facts facts;
    assert(!a.observe(facts) && facts.contractVersion == 1u
        && facts.outcome == Contract::Outcome::Produced);
    assert((Schema::words(facts.logical) == Schema::Words{3u, 2u, 1u, 0xfffffffeu, 1u}));
    assert((Schema::words(facts.physical) == Schema::Words{3u, 2u, 1u, 0xfffffffeu, 0u}));
    assert(facts.mapping.firstInstance == 1u && facts.mapping.instanceCount == 2u
        && facts.mapping.instanceCapacity == 3u && facts.mapping.contractVersion == 1u);
    for (std::uint32_t instance = 0u; instance < 2u; ++instance) {
        const auto resolved =
            semantics::IndirectInstanceBaseLowering::resolve(facts.mapping, instance, 3u);
        assert(resolved.valid() && resolved.logicalInstance == 1u + instance);
    }
    assert(!semantics::IndirectInstanceBaseLowering::resolve(facts.mapping, 2u, 3u).valid());
    assert(!semantics::IndirectInstanceBaseLowering::resolve(facts.mapping, 0u, 2u).valid());
    core::pmr::Vector<VertexBufferLayoutDesc> layouts{&core::defaultCpuMemoryResource()};
    layouts.push_back({3u, 8u, VertexInputRate::Vertex, {{0u, 0u, VertexFormat::Float32x2}}});
    layouts.push_back({8u, 16u, VertexInputRate::Instance, {{1u, 0u, VertexFormat::Float32x4}}});
    VertexInputConsumptionContract vertex(layouts);
    assert(!a.validateStream(vertex, 3u, 40u, 8u));
    expect(a.validateStream(vertex, 3u, 39u, 8u), "RHI-VERTEX-INPUT-FETCH-RANGE");
    // Physical instances start at zero; logical mapping retains firstInstance1.
    assert(!a.validateStream(vertex, 8u, 32u, 0u));
    expect(a.validateStream(vertex, 8u, 31u, 0u), "RHI-VERTEX-INPUT-FETCH-RANGE");
    for (const auto draw :
        {DrawIndexedIndirectCommand{UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX},
            DrawIndexedIndirectCommand{0u, 1u, UINT32_MAX, INT32_MIN, UINT32_MAX},
            DrawIndexedIndirectCommand{UINT32_MAX, 0u, UINT32_MAX, INT32_MIN, UINT32_MAX}}) {
        Fixture fault;
        fault.put(draw);
        auto suppressed = fault.make();
        assert(!suppressed.diagnostic());
        assert(!suppressed.observe(facts) && facts.outcome == Contract::Outcome::FaultSuppressed);
        assert(facts.logical.firstInstance == UINT32_MAX && !facts.physical.instanceCount
            && !facts.physical.firstInstance && !facts.mapping.firstInstance
            && !facts.mapping.instanceCount && !facts.mapping.instanceCapacity
            && !facts.mapping.contractVersion);
        assert(!suppressed.validateStream(vertex, 3u, 0u, UINT64_MAX)
            && !suppressed.validateStream(vertex, 8u, 0u, UINT64_MAX));
    }
    Fixture empty;
    empty.put({UINT32_MAX, 0u, UINT32_MAX, INT32_MIN, 3u});
    auto zero = empty.make();
    assert(!zero.observe(facts) && facts.outcome == Contract::Outcome::Produced
        && facts.mapping.contractVersion == 1u && facts.mapping.firstInstance == 3u
        && !facts.mapping.instanceCount);
    assert(!zero.validateStream(vertex, 3u, 0u, UINT64_MAX));
    auto moved = std::move(a);
    assert(!moved.observe(facts));
    expect(a.observe(facts), "RHI-ZERO-BASE-SOURCE-MOVED");
    assert(!facts.contractVersion && !a.retainedBytes());
}

void hostile()
{
    Fixture f;
    auto index = f.indices();
    const auto reject = [&](Args::Source source,
                            ZeroBaseIndirectProducerParams parameters,
                            BindlessIndirectInstanceConsumerLayout consumer,
                            Contract::Budget budget,
                            const char* id) {
        Contract denied(source, parameters, consumer, budget, index);
        expect(denied.diagnostic(), id);
        assert(denied.sourceBytes().empty() && !denied.retainedBytes());
        Contract::Facts facts;
        facts.contractVersion = 99u;
        expect(denied.observe(facts), id);
        assert(!facts.contractVersion && !facts.mapping.contractVersion
            && Schema::words(facts.physical) == Schema::Words{});
    };
    auto source = f.input();
    source.contractVersion = 0u;
    reject(source, f.parameters, f.consumer, f.budget(), "RHI-INDIRECT-ARGUMENT-VERSION");
    for (const auto origin : {Args::Origin::Unknown,
             Args::Origin::ApprovedGpuProduced,
             static_cast<Args::Origin>(255u)}) {
        source = f.input();
        source.origin = origin;
        reject(source,
            f.parameters,
            f.consumer,
            f.budget(),
            "RHI-ZERO-BASE-SOURCE-IMMUTABLE-PROOF-REQUIRED");
    }
    for (unsigned variant = 0u; variant < 3u; ++variant) {
        auto parameters = f.parameters;
        if (!variant)
            parameters.reserved = 1u;
        else if (variant == 1u)
            parameters.contractVersion = 0u;
        else
            parameters.instanceCapacity = 0u;
        reject(f.input(), parameters, f.consumer, f.budget(), "RHI-ZERO-BASE-SOURCE-PARAMETERS");
        auto consumer = f.consumer;
        if (!variant)
            consumer.contractVersion = 0u;
        else if (variant == 1u)
            consumer.recordIndex = 1u;
        else
            consumer.instanceCapacity = 2u;
        reject(
            f.input(), f.parameters, consumer, f.budget(), "RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
    }
    constexpr auto oversizedExtent = (std::uint64_t{UINT32_MAX} + 1u) * 4u;
    for (unsigned variant = 0u; variant < 5u; ++variant) {
        source = f.input();
        if (!variant)
            source.offsetBytes = 4u;
        else if (variant == 1u)
            source.drawCount = 2u;
        else if (variant == 2u)
            source.strideBytes = 24u;
        else if (variant == 3u)
            source.byteExtent = 24u;
        else
            source.byteExtent = static_cast<std::size_t>(oversizedExtent);
        // The hostile extent must reject on both pointer widths. A wasm32 view
        // cannot represent this extent and its narrowed zero-byte span rejects
        // before the subsequent uint32 element-count/range validation.
        const auto* diagnostic =
            variant == 4u && oversizedExtent > std::numeric_limits<std::size_t>::max()
            ? "CORE-UNSIGNED-SPAN-BYTES-INVALID"
            : "RHI-ZERO-BASE-SOURCE-RANGE";
        reject(source, f.parameters, f.consumer, f.budget(), diagnostic);
    }
    source = f.input();
    source.data = nullptr;
    reject(source, f.parameters, f.consumer, f.budget(), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    source = f.input();
    source.byteExtent = 31u;
    reject(source, f.parameters, f.consumer, f.budget(), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    source = f.input();
    source.order = Order::Unknown;
    reject(source, f.parameters, f.consumer, f.budget(), "CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN");
    for (const auto budget : {Contract::Budget{},
             Contract::Budget{1u, false, 32u, 256u},
             Contract::Budget{2u, true, 32u, 256u}})
        reject(f.input(), f.parameters, f.consumer, budget, "RHI-ZERO-BASE-SOURCE-BUDGET");
    auto budget = f.budget();
    --budget.maxSourceBytes;
    reject(f.input(), f.parameters, f.consumer, budget, "RHI-ZERO-BASE-SOURCE-BUDGET-EXCEEDED");
    budget = f.budget();
    --budget.maxSnapshotBytes;
    reject(f.input(), f.parameters, f.consumer, budget, "RHI-ZERO-BASE-SOURCE-BUDGET-EXCEEDED");
    f.put({4u, 1u, 1u, -2, 1u});
    auto unsafe = f.make();
    expect(unsafe.diagnostic(), "CORE-UNSIGNED-SPAN-RANGE-INVALID");
    assert(!unsafe.retainedBytes());
    f.put({3u, 1u, 1u, -4, 1u});
    auto negative = f.make();
    expect(negative.diagnostic(), "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE");
    assert(!negative.retainedBytes());
}

void storageAndQuotas()
{
    Fixture f;
    auto index = f.indices();
    auto quota = core::makeShared<core::CpuMemoryQuota>(64u * 1024u);
    const auto factory = [&]() -> core::UniquePtr<Contract> {
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return core::makeUnique<Contract>(
                f.input(), f.parameters, f.consumer, f.budget(), index);
        } catch (const std::bad_alloc&) {
            return {};
        }
    };
    test::CpuQuotaFactoryFixture q(quota);
    const auto peak = q.measure(factory);
    q.verify(peak, factory);
    auto contract = factory();
    assert(contract && !contract->diagnostic());
    auto device = createNullDevice();
    auto buffer = device->createBuffer({"ImmutableSource",
        32u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(buffer);
    expect(contract->validateSourceStorage(*buffer), "RHI-ZERO-BASE-SOURCE-WRITES-NOT-SEALED");
    buffer->sealHostWrites();
    expect(contract->validateSourceStorage(*buffer), "RHI-ZERO-BASE-SOURCE-WRITES-NOT-SEALED");
    buffer->sealWrites();
    assert(!contract->validateSourceStorage(*buffer));
    const auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    assert(!extension->capabilities().executablePayload
        && !extension->capabilities().zeroBaseIndirectProducerContractVersion);
    auto other = device->createBuffer({"WrongSourceExtent",
        28u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead});
    assert(other);
    other->sealWrites();
    expect(contract->validateSourceStorage(*other), "RHI-ZERO-BASE-SOURCE-STORAGE-DOMAIN");
    for (unsigned field = 0u; field < 4u; ++field) {
        auto consumer = f.consumer;
        if (!field)
            ++consumer.contractVersion;
        else if (field == 1u)
            ++consumer.binding;
        else if (field == 2u)
            ++consumer.recordIndex;
        else
            ++consumer.instanceCapacity;
        expect(contract->validateConsumer(consumer), "RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
        auto parameters = f.parameters;
        if (!field)
            ++parameters.sourceWordOffset;
        else if (field == 1u)
            ++parameters.instanceCapacity;
        else if (field == 2u)
            ++parameters.reserved;
        else
            ++parameters.contractVersion;
        expect(contract->validateParameters(parameters), "RHI-ZERO-BASE-SOURCE-PARAMETERS");
    }
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            Contract::Facts facts;
            assert(!contract->observe(facts) && !contract->validateConsumer(f.consumer)
                && !contract->validateParameters(f.parameters)
                && !contract->validateSourceStorage(*buffer));
        }
        std::pmr::set_default_resource(previous);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    contract.reset();
    assert(!quota->liveBytes());
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto plan = semantics::ZeroBaseIndirectSourceLowering::current(backend);
        assert(plan.contractVersion == 1u && !plan.executable
            && plan.validatorOnly == (backend == Backend::Null)
            && plan.mode
                == (backend == Backend::Null ? CapabilityMode::Equivalent
                                             : CapabilityMode::DiagnosticFailure));
        expect(plan.diagnosticId,
            backend == Backend::Null ? "RHI-ZERO-BASE-SOURCE-VALIDATOR-ONLY"
                                     : "RHI-ZERO-BASE-SOURCE-PAYLOAD-MISSING");
    }
    expect(
        semantics::ZeroBaseIndirectSourceLowering::current(static_cast<Backend>(255u)).diagnosticId,
        "RHI-ZERO-BASE-SOURCE-BACKEND-UNKNOWN");
    std::printf(
        "ZeroBaseIndirectSourceConsumption/1: full32-byte immutable/endian snapshot, logical/physical/mapping/fault suppression and fetch/tuple gates; measured cold quota%llu exact/one-less, observation allocation0, final quota0, four-backend no-execution PASS\n",
        static_cast<unsigned long long>(peak));
}
} // namespace

int main()
{
    schema();
    contentAndMapping();
    hostile();
    storageAndQuotas();
}
