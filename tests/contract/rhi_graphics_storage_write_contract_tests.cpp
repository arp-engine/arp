#include "../fixtures/single_storage_contract_fixture.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/command_graphics_storage_write_sequence.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Graphics storage write contracts require assertions
#endif

namespace {
using namespace arp;
using namespace rhi;
using Contract = GraphicsStorageWriteContract;
using Footprint = GraphicsStorageWriteFootprint;
constexpr GpuResourceId resource{42u};

void expect(const char* actual, const char* expected)
{
    assert(actual && std::strcmp(actual, expected) == 0);
}

void expect(const ValidationResult& actual, const char* expected)
{
    assert(!actual.success && actual.diagnostics == expected);
}

class Fixture final : public test::SingleStorageContractFixture {
public:
    Fixture() : SingleStorageContractFixture(ShaderStage::Fragment)
    {
        footprint.binding = 3u;
        footprint.replacement = 0x12345678u;
    }

    Contract make() const { return {layout, reflection, footprint}; }

    Footprint footprint;
};

void layoutDomain()
{
    Fixture fixture;
    auto input = fixture.make();
    assert(!input.diagnostic() && input.descriptors().matchesBindings(fixture.reflection));
    fixture.reflection.descriptorBindings[0].binding++;
    assert(!input.descriptors().matchesBindings(fixture.reflection));
    fixture.reflection.descriptorBindings.clear();
    fixture.layout.descriptorSets.clear();
    assert(input.footprint().replacement == 0x12345678u
        && input.descriptors().bindings().size() == 1u);
    auto moved = std::move(input);
    assert(!moved.diagnostic());
    const auto invalid = [&](const char* id) {
        auto c = fixture.make();
        expect(c.diagnostic(), id);
    };
    fixture = Fixture{};
    fixture.footprint.contractVersion = 0u;
    invalid("RHI-GRAPHICS-STORAGE-WRITE-VERSION");
    fixture = Fixture{};
    fixture.footprint.operation = static_cast<Footprint::Operation>(255);
    invalid("RHI-GRAPHICS-STORAGE-WRITE-DOMAIN");
    fixture = Fixture{};
    fixture.reflection.descriptorBindings.push_back(fixture.reflection.descriptorBindings[0]);
    invalid("RHI-GRAPHICS-STORAGE-WRITE-BINDING-DUPLICATE");
    for (auto type : {DescriptorType::StorageBuffer, DescriptorType::StorageTexture}) {
        fixture = Fixture{};
        fixture.reflection.descriptorBindings.push_back({0u, 4u, type, ShaderStage::Fragment, 1u});
        invalid("RHI-GRAPHICS-STORAGE-WRITE-OTHER-WRITER");
    }
    for (auto type : {DescriptorType::ReadOnlyStorageBuffer,
             DescriptorType::UniformBuffer,
             DescriptorType::StorageTexture}) {
        fixture = Fixture{};
        fixture.reflection.descriptorBindings[0].type = type;
        invalid("RHI-GRAPHICS-STORAGE-WRITE-BINDING-DOMAIN");
    }
    for (auto stages : {ShaderStage::None,
             ShaderStage::Vertex,
             ShaderStage::Compute,
             ShaderStage::Vertex | ShaderStage::Fragment}) {
        fixture = Fixture{};
        fixture.reflection.descriptorBindings[0].stages = stages;
        invalid("RHI-GRAPHICS-STORAGE-WRITE-BINDING-DOMAIN");
    }
    for (auto count : {0u, 2u}) {
        fixture = Fixture{};
        fixture.reflection.descriptorBindings[0].arrayCount = count;
        invalid("RHI-GRAPHICS-STORAGE-WRITE-BINDING-DOMAIN");
    }
    fixture = Fixture{};
    fixture.layout.descriptorSets.clear();
    invalid("RHI-GRAPHICS-STORAGE-WRITE-BINDING-DOMAIN");
    fixture = Fixture{};
    fixture.layout.descriptorLayouts[0].bindings[0].type = DescriptorType::UniformBuffer;
    invalid("RHI-GRAPHICS-STORAGE-WRITE-LAYOUT-MISMATCH");
    fixture = Fixture{};
    fixture.layout.descriptorLayouts[0].bindings[0].dynamicOffset = true;
    invalid("RHI-GRAPHICS-STORAGE-WRITE-LAYOUT-DOMAIN");
    fixture = Fixture{};
    fixture.layout.descriptorLayouts[0].bindings[0].stages =
        ShaderStage::Vertex | ShaderStage::Fragment;
    invalid("RHI-GRAPHICS-STORAGE-WRITE-LAYOUT-DOMAIN");
}

BindlessResourceAccess transferConsumer()
{
    BindlessResourceAccess result;
    result.resource = resource;
    result.kind = BindlessAccessKind::Buffer;
    result.state = ResourceState::CopySrc;
    result.pipelineStages = PipelineStage::Transfer;
    result.access = ResourceAccess::TransferRead;
    result.bufferSize = Contract::footprintBytes;
    return result;
}

ResourceBarrierDesc dependency(
    const BindlessResourceAccess& producer, const BindlessResourceAccess& consumer)
{
    ResourceBarrierDesc result;
    result.resource = resource;
    result.before = producer.state;
    result.after = consumer.state;
    result.sourceStages = producer.pipelineStages;
    result.sourceAccess = producer.access;
    result.destinationStages = consumer.pipelineStages;
    result.destinationAccess = consumer.access;
    return result;
}

void scopeAndDependency()
{
    Fixture fixture;
    auto input = fixture.make();
    assert(!input.validateBindingRange(4u, 0u, false));
    expect(input.validateBindingRange(3u, 0u, false), "RHI-GRAPHICS-STORAGE-WRITE-BUFFER-RANGE");
    assert(!input.validateBindingRange(UINT64_MAX, 0u, false));
    expect(input.validateBindingRange(UINT64_MAX, UINT64_MAX, false),
        "RHI-GRAPHICS-STORAGE-WRITE-OFFSET-DOMAIN");
    expect(input.validateBindingRange(4u, 0u, true), "RHI-GRAPHICS-STORAGE-WRITE-OFFSET-DOMAIN");
    assert(!input.validateDraw(3u, 1u, false, false));
    expect(input.validateDraw(0u, 1u, false, false), "RHI-GRAPHICS-STORAGE-WRITE-DRAW-DOMAIN");
    expect(input.validateDraw(3u, 2u, false, false), "RHI-GRAPHICS-STORAGE-WRITE-DRAW-DOMAIN");
    expect(input.validateDraw(3u, 1u, true, false), "RHI-GRAPHICS-STORAGE-WRITE-DRAW-DOMAIN");
    expect(input.validateDraw(3u, 1u, false, true), "RHI-GRAPHICS-STORAGE-WRITE-DRAW-DOMAIN");
    const auto producer = input.requiredAccess(resource);
    assert(!semantics::validateResourceAccessScope(
        {producer.state, producer.pipelineStages, producer.access}));
    std::array<BindlessResourceAccess, 2> access{producer, producer};
    BindlessAccessArray one(access.data(), 1u), both(access.data(), 2u);
    assert(input.validateProducerScope(resource, one).success);
    expect(input.validateProducerScope(resource, both), "RHI-GRAPHICS-STORAGE-WRITE-ALIAS");
    BindlessAccessArray none(nullptr, 0u), missing(nullptr, 1u);
    expect(input.validateProducerScope(resource, none), "RHI-BINDLESS-ACCESS-SET-INCOMPLETE");
    expect(input.validateProducerScope(resource, missing), "RHI-BINDLESS-ACCESS-SET-INVALID");
    expect(input.validateProducerScope({}, one), "RHI-BINDLESS-REQUIRED-ACCESS-INVALID");
    access[0].bufferSize = 5u;
    expect(input.validateProducerScope(resource, one), "RHI-GRAPHICS-STORAGE-WRITE-SCOPE-MISMATCH");
    access[0] = producer;
    access[0].access = ResourceAccess::ShaderWrite;
    expect(input.validateProducerScope(resource, one), "RHI-BINDLESS-ACCESS-SET-INCOMPLETE");
    access[0] = producer;
    access[0].pipelineStages = PipelineStage::FragmentShader | PipelineStage::ComputeShader;
    expect(input.validateProducerScope(resource, one), "RHI-GRAPHICS-STORAGE-WRITE-SCOPE-MISMATCH");
    access[0] = producer;
    auto consumer = transferConsumer();
    auto barrier = dependency(producer, consumer);
    assert(!input.validateDependency(resource, barrier, consumer, true));
    expect(input.validateDependency(resource, barrier, consumer, false),
        "RHI-GRAPHICS-STORAGE-WRITE-PRODUCER-OPEN");
    auto broken = barrier;
    broken.resource = {43u};
    expect(input.validateDependency(resource, broken, consumer, true),
        "RHI-RESOURCE-MEMORY-BARRIER-RESOURCE-MISMATCH");
    broken = barrier;
    broken.before = ResourceState::ShaderRead;
    expect(input.validateDependency(resource, broken, consumer, true),
        "RHI-GRAPHICS-STORAGE-WRITE-DEPENDENCY-MISMATCH");
    broken = barrier;
    broken.sourceAccess = ResourceAccess::ShaderWrite;
    expect(input.validateDependency(resource, broken, consumer, true),
        "RHI-GRAPHICS-STORAGE-WRITE-DEPENDENCY-MISMATCH");
    broken = barrier;
    broken.destinationStages = PipelineStage::FragmentShader;
    expect(input.validateDependency(resource, broken, consumer, true),
        "RHI-GRAPHICS-STORAGE-WRITE-DEPENDENCY-MISMATCH");
    consumer.bufferSize--;
    expect(input.validateDependency(resource, barrier, consumer, true),
        "RHI-GRAPHICS-STORAGE-WRITE-CONSUMER-RANGE");
    consumer = transferConsumer();
    consumer.access = ResourceAccess::TransferWrite;
    expect(
        input.validateDependency(resource, barrier, consumer, true), "RHI-ACCESS-STATE-MISMATCH");
    consumer = producer;
    expect(input.validateDependency(resource, barrier, consumer, true),
        "RHI-GRAPHICS-STORAGE-WRITE-CONSUMER-DOMAIN");
    consumer = producer;
    consumer.state = ResourceState::ShaderRead;
    consumer.access = ResourceAccess::IndirectCommandRead;
    consumer.stages = ShaderStage::None;
    consumer.pipelineStages = PipelineStage::DrawIndirect;
    expect(input.validateDependency(resource, barrier, consumer, true),
        "RHI-GRAPHICS-STORAGE-WRITE-CONSUMER-DOMAIN");
    for (auto stages : {ShaderStage::Vertex,
             ShaderStage::Fragment,
             ShaderStage::Compute,
             ShaderStage::Vertex | ShaderStage::Fragment}) {
        consumer = producer;
        consumer.state = ResourceState::ShaderRead;
        consumer.access = ResourceAccess::ShaderRead;
        consumer.stages = stages;
        consumer.pipelineStages = bindlessPipelineStagesForShader(stages);
        barrier = dependency(producer, consumer);
        assert(!input.validateDependency(resource, barrier, consumer, true));
    }
    consumer.stages = static_cast<ShaderStage>(255);
    expect(input.validateDependency(resource, barrier, consumer, true),
        "RHI-GRAPHICS-STORAGE-WRITE-CONSUMER-DOMAIN");
    // Reuse bounded declaration retention. It stores values, not write authority.
    BindlessRetainedDeclarations retained(1u, BindlessAccessRetention::GraphicsPass);
    assert(retained.capture(one).success && retained.matches(one));
    access[0].bufferSize++;
    assert(!retained.matches(one));
    assert(input.validateProducerScope(resource, retained).success);
    expect(retained.capture(both), "RHI-BINDLESS-GRAPHICS-PASS-ACCESS-BUDGET");
    assert(!retained.size());
}

void budgetsAndGates()
{
    Fixture fixture;
    auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
    std::uint64_t charge = 0u;
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto input = fixture.make();
        BindlessRetainedDeclarations retained(1u, BindlessAccessRetention::GraphicsPass);
        const auto producer = input.requiredAccess(resource);
        BindlessAccessArray declared(&producer, 1u);
        assert(retained.capture(declared).success && !input.diagnostic());
        std::pmr::set_default_resource(previous);
        charge = quota->liveBytes();
        assert(charge);
        assert(core::inspectCpuAllocationCharge(input.descriptors().bindings().data()).quota
            == quota.get());
        const auto consumer = transferConsumer();
        const auto barrier = dependency(producer, consumer);
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 1000u; ++i) {
            assert(!input.validateBindingRange(4u, 0u, false));
            assert(!input.validateDependency(resource, barrier, consumer, true));
            assert(input.validateProducerScope(resource, retained).success);
            assert(retained.matches(declared));
        }
        std::pmr::set_default_resource(previous);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    assert(!quota->liveBytes());
    auto smaller = core::makeShared<core::CpuMemoryQuota>(charge - 1u);
    bool rejected = false;
    try {
        core::CpuMemoryQuotaScope scope(smaller);
        auto input = fixture.make();
        BindlessRetainedDeclarations retained(1u, BindlessAccessRetention::GraphicsPass);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    assert(rejected && !smaller->liveBytes());
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto plan = semantics::GraphicsStorageWriteLowering::current(backend);
        assert(!plan.executable && plan.diagnosticId);
        if (backend == Backend::Null)
            assert(plan.validatorOnly && plan.mode == CapabilityMode::Equivalent);
        else
            assert(!plan.validatorOnly && plan.mode == CapabilityMode::DiagnosticFailure);
    }
    expect(semantics::GraphicsStorageWriteLowering::current(static_cast<Backend>(255)).diagnosticId,
        "RHI-GRAPHICS-STORAGE-WRITE-BACKEND-UNKNOWN");
}
} // namespace

int main()
{
    {
        GraphicsStorageWriteSequence sequence;
        assert(sequence.finish() && sequence.draw(resource) && sequence.canDepend(resource));
        assert(sequence.begin({}) && !sequence.begin(resource));
        assert(
            sequence.begin(resource) && sequence.canEnd(resource) && sequence.canConsume(resource));
        assert(sequence.draw({43u}) && !sequence.draw(resource));
        assert(!sequence.canEnd(resource) && !sequence.end(resource));
        assert(sequence.end(resource) && sequence.depend({43u}) && !sequence.depend(resource));
        assert(sequence.depend(resource) && sequence.consume({43u}) && !sequence.consume(resource));
        assert(sequence.consume(resource) && !sequence.finish());
    }
    {
        GraphicsStorageWriteSequence sequence;
        assert(sequence.beginConsumer(resource) && sequence.drawConsumer(resource)
            && sequence.endConsumer(resource));
        assert(!sequence.begin(resource) && !sequence.draw(resource) && !sequence.end(resource)
            && !sequence.depend(resource));
        assert(sequence.beginConsumer({43u}) && !sequence.beginConsumer(resource));
        assert(
            sequence.finish() && sequence.canEndConsumer(resource) && sequence.consume(resource));
        assert(sequence.draw(resource) && sequence.drawConsumer({43u})
            && !sequence.drawConsumer(resource));
        assert(!sequence.canEndConsumer(resource) && !sequence.endConsumer(resource)
            && !sequence.finish());
        assert(sequence.beginConsumer(resource) && sequence.drawConsumer(resource)
            && sequence.endConsumer(resource));
    }
    layoutDomain();
    scopeAndDependency();
    budgetsAndGates();
    std::puts(
        "GraphicsStorageWrite/1: immutable bounded intent, exact access/dependency, alias/budget/allocator and four-backend closed gates PASS; no shader proof/native write authority");
}
