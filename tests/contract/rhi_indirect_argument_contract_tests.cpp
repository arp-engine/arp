#include "../fixtures/indirect_argument_fixture.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/command_indirect_recording_budget.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Indirect argument contract tests require assertions
#endif

namespace {
using namespace arp;
using Contract = rhi::IndirectArgumentConsumptionContract;
using Order = core::UnsignedElementSpan::ByteOrder;
using Origin = Contract::Origin;
using Command = rhi::DrawIndexedIndirectCommand;

void expect(const char* actual, const char* expected)
{
    assert(actual && std::strcmp(actual, expected) == 0);
}

class Fixture final {
public:
    Fixture(Order order = Order::LittleEndian) : order_(order)
    {
        source.fill(0xabu);
        put(0u, {3u, 2u, 1u, -2, 1u});
        put(1u, {0u, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
        put(2u, {UINT32_MAX, 0u, UINT32_MAX, INT32_MIN, UINT32_MAX});
    }

    void put(std::size_t ordinal, Command draw)
    {
        const auto record = test::IndirectArgumentFixture<1u>::encode(draw, order_);
        std::copy(record.begin(), record.end(), source.begin() + 4u + ordinal * 24u);
    }

    Contract::Source input() const
    {
        return {
            source.data(), source.size(), 4u, 3u, 24u, order_, Origin::HostAuthoredImmutable, 1u};
    }

    rhi::IndexInputConsumptionContract indices() const
    {
        return {indexBytes.data(),
            indexBytes.size(),
            rhi::IndexFormat::Uint16,
            Order::LittleEndian,
            rhi::IndexInputConsumptionContract::PrimitiveRestart::Disabled};
    }

    static Contract::Budget budget()
    {
        return {3u, 73u, 3u * (sizeof(Command) + sizeof(Contract::DrawObservation))};
    }

    Contract make() const
    {
        auto index = indices();
        return {input(), budget(), index};
    }

    std::array<std::uint8_t, 73> source;
    std::array<std::uint8_t, 16> indexBytes{
        0xff, 0xff, 2, 0, 6, 0, 4, 0, 8, 0, 1, 0, 0, 0, 0xff, 0xff};

private:
    Order order_;
};

void snapshotsAndFetch()
{
    auto little = Fixture{}.make();
    Fixture fixture(Order::BigEndian);
    auto big = fixture.make();
    assert(!little.diagnostic() && !big.diagnostic() && little.bytes() == big.bytes());
    assert(big.bytes().size() == 60u && big.drawCount() == 3u);
    assert(big.retainedBytes() == Fixture::budget().maxSnapshotBytes);
    fixture.source.fill(0u);
    fixture.indexBytes.fill(0u);
    Contract::DrawObservation draw;
    assert(!big.observe(0u, draw) && draw.active);
    assert(draw.logical.indexCount == 3u && draw.logical.instanceCount == 2u
        && draw.logical.firstIndex == 1u);
    assert(draw.logical.vertexOffset == -2 && draw.logical.firstInstance == 1u);
    assert(draw.fetch.firstVertex == 0u && draw.fetch.vertexCount == 5u
        && draw.fetch.baseVertex == -2);
    for (std::uint64_t i : {1u, 2u}) {
        assert(!big.observe(i, draw) && !draw.active && !draw.fetch.indexCount
            && !draw.fetch.vertexCount);
        assert(draw.logical.vertexOffset == INT32_MIN && draw.logical.firstInstance == UINT32_MAX);
    }
    expect(big.observe(3u, draw), "RHI-INDIRECT-ARGUMENT-ORDINAL");
    assert(!draw.active && !draw.logical.indexCount && !draw.fetch.vertexCount);
    expect(big.observe(UINT64_MAX, draw), "RHI-INDIRECT-ARGUMENT-ORDINAL");
    core::pmr::Vector<rhi::VertexBufferLayoutDesc> layouts{&core::defaultCpuMemoryResource()};
    layouts.push_back(
        {3u, 8u, rhi::VertexInputRate::Vertex, {{0u, 0u, rhi::VertexFormat::Float32x2}}});
    layouts.push_back(
        {8u, 16u, rhi::VertexInputRate::Instance, {{1u, 0u, rhi::VertexFormat::Float32x4}}});
    rhi::VertexInputConsumptionContract vertex(layouts);
    assert(!big.validateStream(0u, vertex, 3u, 48u, 8u));
    expect(big.validateStream(0u, vertex, 3u, 47u, 8u), "RHI-VERTEX-INPUT-FETCH-RANGE");
    assert(!big.validateStream(0u, vertex, 8u, 72u, 24u));
    expect(big.validateStream(0u, vertex, 8u, 71u, 24u), "RHI-VERTEX-INPUT-FETCH-RANGE");
    assert(!big.validateStream(1u, vertex, 3u, 0u, UINT64_MAX));
    assert(!big.validateStream(2u, vertex, 8u, 0u, UINT64_MAX));
    expect(big.validateStream(1u, vertex, 9u, 0u, 0u), "RHI-VERTEX-INPUT-SLOT-UNDECLARED");
    expect(big.validateStream(3u, vertex, 3u, 48u, 8u), "RHI-INDIRECT-ARGUMENT-ORDINAL");
    Fixture instance;
    instance.put(0u, {3u, 1u, 1u, -2, UINT32_MAX});
    auto high = instance.make();
    assert(!high.diagnostic());
    assert(!high.validateStream(0u, vertex, 8u, (std::uint64_t{UINT32_MAX} + 1u) * 16u, 0u));
    expect(high.validateStream(0u, vertex, 8u, (std::uint64_t{UINT32_MAX} + 1u) * 16u - 1u, 0u),
        "RHI-VERTEX-INPUT-FETCH-RANGE");
    instance.put(0u, {3u, 2u, 1u, -2, UINT32_MAX});
    auto overflow = instance.make();
    expect(overflow.diagnostic(), "RHI-INDIRECT-ARGUMENT-INSTANCE-RANGE");
    assert(overflow.bytes().empty() && !overflow.drawCount());
    auto moved = std::move(big);
    assert(!moved.observe(0u, draw) && draw.fetch.vertexCount == 5u);
}

void schemaAndBudgets()
{
    Fixture fixture;
    auto index = fixture.indices();
    const auto rejected = [&](Contract::Source source, Contract::Budget budget, const char* id) {
        Contract input(source, budget, index);
        expect(input.diagnostic(), id);
        assert(input.bytes().empty() && !input.drawCount() && !input.retainedBytes());
        Contract::DrawObservation draw;
        expect(input.observe(0u, draw), id);
    };
    auto source = fixture.input();
    source.contractVersion = 0u;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-VERSION");
    for (auto origin : {Origin::Unknown, static_cast<Origin>(255)}) {
        source = fixture.input();
        source.origin = origin;
        rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-ORIGIN-UNKNOWN");
    }
    source = fixture.input();
    source.origin = Origin::ApprovedGpuProduced;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-GPU-PROOF-REQUIRED");
    source = fixture.input();
    source.order = static_cast<Order>(255);
    rejected(source, Fixture::budget(), "CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN");
    source = fixture.input();
    source.order = Contract::Source{}.order;
    rejected(source, Fixture::budget(), "CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN");
    source = fixture.input();
    source.data = nullptr;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE");
    source = fixture.input();
    source.byteExtent = 71u;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE");
    source = fixture.input();
    source.byteExtent = 72u;
    Contract exact(source, Fixture::budget(), index);
    assert(!exact.diagnostic());
    for (auto stride : {0u, 19u, 21u}) {
        source = fixture.input();
        source.strideBytes = stride;
        rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE");
    }
    source = fixture.input();
    source.offsetBytes = 1u;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE");
    source = fixture.input();
    source.offsetBytes = UINT64_MAX - 3u;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE");
    source = fixture.input();
    source.drawCount = 0u;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE");
    auto budget = Fixture::budget();
    budget.maxRecords = 0u;
    rejected(fixture.input(), budget, "RHI-INDIRECT-ARGUMENT-BUDGET");
    budget = Fixture::budget();
    budget.maxSourceBytes = 0u;
    rejected(fixture.input(), budget, "RHI-INDIRECT-ARGUMENT-BUDGET");
    budget = Fixture::budget();
    budget.maxSnapshotBytes = 0u;
    rejected(fixture.input(), budget, "RHI-INDIRECT-ARGUMENT-BUDGET");
    budget = Fixture::budget();
    budget.maxRecords--;
    rejected(fixture.input(), budget, "RHI-INDIRECT-ARGUMENT-BUDGET-EXCEEDED");
    budget = Fixture::budget();
    budget.maxSourceBytes--;
    rejected(fixture.input(), budget, "RHI-INDIRECT-ARGUMENT-BUDGET-EXCEEDED");
    budget = Fixture::budget();
    budget.maxSnapshotBytes--;
    rejected(fixture.input(), budget, "RHI-INDIRECT-ARGUMENT-BUDGET-EXCEEDED");
    source = fixture.input();
    source.drawCount = UINT32_MAX;
    source.strideBytes = UINT32_MAX - 3u;
    rejected(source, Fixture::budget(), "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE");
    fixture.put(1u, {1u, 1u, 99u, 0, 0u});
    rejected(fixture.input(), Fixture::budget(), "CORE-UNSIGNED-SPAN-RANGE-INVALID");
    fixture.put(1u, {3u, 1u, 1u, -3, 0u});
    rejected(fixture.input(), Fixture::budget(), "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE");
    fixture.put(1u, {1u, 1u, 0u, INT32_MAX, 0u});
    auto high = fixture.make();
    Contract::DrawObservation observation;
    assert(!high.observe(1u, observation)
        && observation.fetch.firstVertex == std::uint64_t{65535u} + INT32_MAX);
    rhi::IndexInputConsumptionContract invalid(nullptr,
        0u,
        rhi::IndexFormat::Uint16,
        Order::LittleEndian,
        rhi::IndexInputConsumptionContract::PrimitiveRestart::Disabled);
    Contract invalidIndex(fixture.input(), Fixture::budget(), invalid);
    expect(invalidIndex.diagnostic(), "CORE-UNSIGNED-SPAN-BYTES-INVALID");
    assert(invalidIndex.bytes().empty());
}

void scopeAndLowering()
{
    auto input = Fixture{}.make();
    constexpr rhi::GpuResourceId resource{42u};
    const auto required = input.requiredAccess(resource);
    assert(required.bufferSize == 60u && required.state == rhi::ResourceState::ShaderRead
        && required.pipelineStages == rhi::PipelineStage::DrawIndirect
        && required.access == rhi::ResourceAccess::IndirectCommandRead
        && required.stages == rhi::ShaderStage::None);
    assert(!input.validateConsumerScope(resource, required));
    auto access = required;
    access.bufferSize--;
    expect(input.validateConsumerScope(resource, access), "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE");
    access = required;
    access.bufferSize++;
    expect(input.validateConsumerScope(resource, access), "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE");
    access = required;
    access.bufferOffset = 4u;
    expect(input.validateConsumerScope(resource, access), "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE");
    access = required;
    access.resource = {43u};
    expect(input.validateConsumerScope(resource, access), "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE");
    expect(input.validateConsumerScope({}, required), "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE");
    access = required;
    access.state = rhi::ResourceState::CopySrc;
    expect(input.validateConsumerScope(resource, access), "RHI-ACCESS-STATE-MISMATCH");
    access = required;
    access.pipelineStages = rhi::PipelineStage::ComputeShader;
    expect(input.validateConsumerScope(resource, access), "RHI-ACCESS-STAGE-MISMATCH");
    access = required;
    access.stages = rhi::ShaderStage::Vertex;
    expect(input.validateConsumerScope(resource, access), "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE");
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto plan = rhi::semantics::IndirectArgumentLowering::current(backend);
        assert(plan.contractVersion == 1u && !plan.executable);
        assert(plan.validatorOnly == (backend == rhi::Backend::Null));
        assert(plan.mode
            == (plan.validatorOnly ? rhi::CapabilityMode::Equivalent
                                   : rhi::CapabilityMode::DiagnosticFailure));
        expect(plan.diagnosticId,
            plan.validatorOnly ? "RHI-INDIRECT-ARGUMENT-VALIDATOR-ONLY"
                               : "RHI-INDIRECT-ARGUMENT-PAYLOAD-MISSING");
    }
    const auto unknown =
        rhi::semantics::IndirectArgumentLowering::current(static_cast<rhi::Backend>(255));
    assert(!unknown.executable && !unknown.validatorOnly
        && unknown.mode == rhi::CapabilityMode::DiagnosticFailure);
    expect(unknown.diagnosticId, "RHI-INDIRECT-ARGUMENT-BACKEND-UNKNOWN");
}

void quotaAndHotObservation()
{
    Fixture fixture;
    auto index = fixture.indices();
    auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
    std::uint64_t charge = 0u;
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        Contract input(fixture.input(), Fixture::budget(), index);
        assert(!input.diagnostic());
        charge = quota->liveBytes();
        assert(charge);
        assert(core::inspectCpuAllocationCharge(input.bytes().data()).quota == quota.get());
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::uint64_t i = 0u; i < 1000u; ++i) {
            Contract::DrawObservation draw;
            assert(!input.observe(i % 3u, draw));
            assert(!input.validateConsumerScope({42u}, input.requiredAccess({42u})));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    assert(!quota->liveBytes());
    for (auto limit : {charge, charge - 1u}) {
        auto exact = core::makeShared<core::CpuMemoryQuota>(limit);
        bool rejected = false;
        try {
            core::CpuMemoryQuotaScope scope(exact);
            Contract input(fixture.input(), Fixture::budget(), index);
            assert(!input.diagnostic());
        } catch (const std::bad_alloc&) {
            rejected = true;
        }
        assert(rejected == (limit != charge) && !exact->liveBytes());
    }
}

void reusableEndianElementCopy()
{
    std::array<std::uint8_t, 4> source{0x12u, 0x34u, 0x56u, 0x78u};
    core::UnsignedElementSpan span(
        source.data(), source.size(), core::UnsignedElementSpan::Width::Uint32, Order::BigEndian);
    std::array<std::uint8_t, 4> output{0u, 0u, 0u, 0u};
    assert(span.copyElementLittleEndian(0u, output.data(), output.size()));
    assert((output == std::array<std::uint8_t, 4>{0x78u, 0x56u, 0x34u, 0x12u}));
    const auto saved = output;
    assert(!span.copyElementLittleEndian(1u, output.data(), output.size()) && output == saved);
    assert(!span.copyElementLittleEndian(0u, output.data(), 3u) && output == saved);
    assert(!span.copyElementLittleEndian(0u, nullptr, output.size()));
    // The scalar value is read before writing, so an element can normalize in-place.
    assert(span.copyElementLittleEndian(0u, source.data(), source.size()) && source == output);
}

void recordingBudget()
{
    using State = rhi::IndirectRecordingBudgetState;
    rhi::IndirectRecordingBudget budget{1u, true, 8u, 2u, 4u};
    State state(budget);
    expect(state.draw(1u), "RHI-INDIRECT-RECORDING-PASS-REQUIRED");
    assert(!state.commands());
    expect(state.finish(), "RHI-INDIRECT-RECORDING-INCOMPLETE");
    assert(!state.begin() && state.commands() == 1u);
    expect(state.begin(), "RHI-INDIRECT-RECORDING-PASS-BEGIN");
    expect(state.end(), "RHI-INDIRECT-RECORDING-DRAW-REQUIRED");
    assert(state.commands() == 1u);
    assert(!state.stateCommand() && !state.stateCommand() && state.commands() == 3u);
    assert(
        !state.draw(2u) && state.commands() == 5u && state.calls() == 1u && state.records() == 2u);
    expect(state.draw(3u), "RHI-INDIRECT-RECORDING-RECORD-BUDGET");
    assert(state.commands() == 5u && state.calls() == 1u);
    assert(!state.draw(2u) && state.commands() == 7u);
    expect(state.draw(1u), "RHI-INDIRECT-RECORDING-CALL-BUDGET");
    expect(state.stateCommand(), "RHI-INDIRECT-RECORDING-COMMAND-BUDGET");
    assert(!state.end() && !state.finish() && state.commands() == 8u && state.records() == 4u);
    expect(state.end(), "RHI-INDIRECT-RECORDING-PASS-END");
    State oneLess({1u, true, 7u, 2u, 4u});
    assert(!oneLess.begin() && !oneLess.stateCommand() && !oneLess.stateCommand()
        && !oneLess.draw(2u));
    expect(oneLess.draw(2u), "RHI-INDIRECT-RECORDING-COMMAND-BUDGET");
    assert(oneLess.calls() == 1u && oneLess.records() == 2u && !oneLess.end());
    State high({1u, true, UINT32_MAX, UINT32_MAX, UINT32_MAX});
    assert(!high.begin());
    expect(high.draw(UINT64_MAX), "RHI-INDIRECT-RECORDING-RECORD-BUDGET");
    expect(high.draw(UINT32_MAX), "RHI-INDIRECT-RECORDING-COMMAND-BUDGET");
    assert(!high.draw(UINT32_MAX - 2u) && !high.end() && high.commands() == UINT32_MAX);
    State empty(budget);
    assert(!empty.begin());
    expect(empty.draw(0u), "RHI-INDIRECT-RECORDING-RECORD-BUDGET");
    budget.prototypeOptIn = false;
    expect(budget.validate(), "RHI-INDIRECT-RECORDING-OPT-IN-REQUIRED");
    budget.prototypeOptIn = true;
    budget.contractVersion = 0u;
    expect(budget.validate(), "RHI-INDIRECT-RECORDING-VERSION");
    budget.contractVersion = 1u;
    budget.maxCommands = 2u;
    expect(budget.validate(), "RHI-INDIRECT-RECORDING-BUDGET");
    State invalid(budget);
    expect(invalid.begin(), "RHI-INDIRECT-RECORDING-BUDGET");
    assert(!invalid.commands());
}
} // namespace

int main()
{
    reusableEndianElementCopy();
    snapshotsAndFetch();
    schemaAndBudgets();
    scopeAndLowering();
    quotaAndHotObservation();
    recordingBudget();
    std::puts(
        "IndirectArgumentConsumption/1: owned host schema/fetch/no-op/order/endianness/quota/scope and four-backend fail-closed contracts PASS; GPU proof/native authority unavailable");
}
