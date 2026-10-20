#include "../fixtures/single_storage_contract_fixture.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/command_gpu_indirect_output_sequence.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error GPU indirect producer contracts require assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using Contract = GpuIndirectProducerContract;

class Fixture final : public test::SingleStorageContractFixture {
public:
    Fixture() : SingleStorageContractFixture(ShaderStage::Compute)
    {
        footprint.binding = 3u;
        footprint.words = {3u, 2u, 1u, 0xfffffffeu, 1u};
    }

    Contract make() const { return {layout, reflection, footprint}; }

    GpuIndirectProducerFootprint footprint;
};

void expect(const char* actual, const char* expected)
{
    if (!actual || std::strcmp(actual, expected))
        std::fprintf(stderr, "Expected %s, got %s\n", expected, actual ? actual : "success");
    assert(actual && !std::strcmp(actual, expected));
}

void layout()
{
    Fixture f;
    auto c = f.make();
    assert(!c.diagnostic());
    f.footprint.words.fill(99u);
    assert(c.footprint().words[3] == 0xfffffffeu);
    const auto reject = [&](const char* id) {
        auto input = f.make();
        expect(input.diagnostic(), id);
    };
    f = Fixture{};
    f.footprint.contractVersion = 0u;
    reject("RHI-GPU-INDIRECT-PRODUCER-VERSION");
    f = Fixture{};
    f.footprint.operation = static_cast<GpuIndirectProducerFootprint::Operation>(255u);
    reject("RHI-GPU-INDIRECT-PRODUCER-OPERATION");
    f = Fixture{};
    f.reflection.pushConstantByteCount = 4u;
    reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    f = Fixture{};
    f.reflection.descriptorBindings.push_back(f.reflection.descriptorBindings.front());
    reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    f = Fixture{};
    f.reflection.descriptorBindings.clear();
    reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    for (auto stage :
        {ShaderStage::None, ShaderStage::Fragment, ShaderStage::Compute | ShaderStage::Vertex}) {
        f = Fixture{};
        f.reflection.descriptorBindings.front().stages = stage;
        reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    }
    for (auto type : {DescriptorType::ReadOnlyStorageBuffer,
             DescriptorType::StorageTexture,
             DescriptorType::UniformBuffer}) {
        f = Fixture{};
        f.reflection.descriptorBindings.front().type = type;
        reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    }
    for (auto count : {0u, 2u}) {
        f = Fixture{};
        f.reflection.descriptorBindings.front().arrayCount = count;
        reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    }
    f = Fixture{};
    f.footprint.binding++;
    reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    f = Fixture{};
    f.layout.descriptorSets.clear();
    reject("RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN");
    f = Fixture{};
    f.layout.descriptorLayouts.front().bindings.front().type = DescriptorType::UniformBuffer;
    reject("RHI-GPU-INDIRECT-PRODUCER-LAYOUT");
    f = Fixture{};
    f.layout.descriptorLayouts.front().bindings.front().dynamicOffset = true;
    reject("RHI-GPU-INDIRECT-PRODUCER-LAYOUT-DOMAIN");
}

void scope()
{
    auto c = Fixture{}.make();
    constexpr GpuResourceId resource{42u};
    assert(!c.validateBindingRange(20u, 0u, false));
    for (auto bytes : {0u, 19u, 21u})
        expect(c.validateBindingRange(bytes, 0u, false), "RHI-GPU-INDIRECT-PRODUCER-BUFFER-RANGE");
    expect(c.validateBindingRange(20u, 4u, false), "RHI-GPU-INDIRECT-PRODUCER-BUFFER-RANGE");
    expect(c.validateBindingRange(20u, 0u, true), "RHI-GPU-INDIRECT-PRODUCER-BUFFER-RANGE");
    assert(!c.validateDispatch(1u, 1u, 1u));
    for (auto value : {0u, 2u, UINT32_MAX}) {
        expect(c.validateDispatch(value, 1u, 1u), "RHI-GPU-INDIRECT-PRODUCER-DISPATCH");
        expect(c.validateDispatch(1u, value, 1u), "RHI-GPU-INDIRECT-PRODUCER-DISPATCH");
        expect(c.validateDispatch(1u, 1u, value), "RHI-GPU-INDIRECT-PRODUCER-DISPATCH");
    }
    const auto producer = c.producerAccess(resource), consumer = c.consumerAccess(resource);
    assert(producer.bufferSize == 20u && producer.stages == ShaderStage::Compute
        && producer.access == ResourceAccess::ShaderWrite);
    assert(consumer.bufferSize == 20u && consumer.stages == ShaderStage::None
        && consumer.access == ResourceAccess::IndirectCommandRead);
    assert(
        !c.validateScope(resource, producer, true) && !c.validateScope(resource, consumer, false));
    assert(!c.producerAccess({}).resource.isValid());
    expect(c.validateScope({}, producer, true), "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
    for (const bool producing : {false, true}) {
        auto valid = producing ? producer : consumer;
        auto changed = valid;
        changed.bufferSize = 4u;
        expect(c.validateScope(resource, changed, producing), "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
        changed = valid;
        changed.bufferSize = 24u;
        expect(c.validateScope(resource, changed, producing), "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
        changed = valid;
        changed.bufferOffset = 4u;
        expect(c.validateScope(resource, changed, producing), "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
        changed = valid;
        changed.resource = {43u};
        expect(c.validateScope(resource, changed, producing), "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
        changed = valid;
        changed.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        expect(c.validateScope(resource, changed, producing), "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
        changed = valid;
        changed.pipelineStages = PipelineStage::Transfer;
        expect(c.validateScope(resource, changed, producing), "RHI-GPU-INDIRECT-PRODUCER-SCOPE");
    }
    ResourceBarrierDesc barrier;
    barrier.resource = resource;
    barrier.before = producer.state;
    barrier.after = consumer.state;
    barrier.sourceStages = producer.pipelineStages;
    barrier.sourceAccess = producer.access;
    barrier.destinationStages = consumer.pipelineStages;
    barrier.destinationAccess = consumer.access;
    assert(!c.validateDependency(resource, barrier, consumer, true));
    expect(
        c.validateDependency(resource, barrier, consumer, false), "RHI-GPU-INDIRECT-PRODUCER-OPEN");
    auto changed = barrier;
    changed.before = ResourceState::ShaderRead;
    expect(c.validateDependency(resource, changed, consumer, true),
        "RHI-GPU-INDIRECT-PRODUCER-DEPENDENCY");
    changed = barrier;
    changed.after = ResourceState::CopySrc;
    expect(c.validateDependency(resource, changed, consumer, true),
        "RHI-GPU-INDIRECT-PRODUCER-DEPENDENCY");
    changed = barrier;
    changed.sourceAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
    expect(c.validateDependency(resource, changed, consumer, true),
        "RHI-GPU-INDIRECT-PRODUCER-DEPENDENCY");
    changed = barrier;
    changed.resource = {43u};
    assert(c.validateDependency(resource, changed, consumer, true));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    core::CpuMemoryQuotaScope scoped(quota);
    core::CpuMemoryQuotaLease pressure(quota, 1u);
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 100u; ++i)
        assert(!c.validateDependency(resource, barrier, consumer, true));
    std::pmr::set_default_resource(previous);
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
}

void lowering()
{
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto plan = semantics::GpuIndirectProducerLowering::current(backend);
        assert(plan.contractVersion == 1u && !plan.executable
            && plan.validatorOnly == (backend == Backend::Null));
        assert(plan.mode
            == (plan.validatorOnly ? CapabilityMode::Equivalent
                                   : CapabilityMode::DiagnosticFailure));
        expect(plan.diagnosticId,
            plan.validatorOnly ? "RHI-GPU-INDIRECT-PRODUCER-VALIDATOR-ONLY"
                               : "RHI-GPU-INDIRECT-PRODUCER-PAYLOAD-MISSING");
    }
    const auto unknown =
        semantics::GpuIndirectProducerLowering::current(static_cast<Backend>(255u));
    assert(!unknown.executable && !unknown.validatorOnly);
    expect(unknown.diagnosticId, "RHI-GPU-INDIRECT-PRODUCER-BACKEND-UNKNOWN");
}

void expectationAndStorage()
{
    using Arguments = IndirectArgumentConsumptionContract;
    using Span = core::UnsignedElementSpan;
    assert(!GpuIndirectOutputRecordingContract::validateIdentity(1u, 2u, 1u, 2u));
    for (const auto pair : {std::array<std::uint64_t, 2u>{0u, 2u}, {1u, 0u}, {2u, 2u}, {1u, 3u}})
        expect(GpuIndirectOutputRecordingContract::validateIdentity(pair[0], pair[1], 1u, 2u),
            "RHI-GPU-INDIRECT-OUTPUT-RECORDING-MISMATCH");
    std::array<std::uint8_t, 5u> written{9u, 9u, 9u, 9u, 9u};
    assert(Span::writeLittleEndian(0xfffffffeu, Span::Width::Uint32, written.data() + 1u, 4u));
    assert((written == std::array<std::uint8_t, 5u>{9u, 254u, 255u, 255u, 255u}));
    const auto unchanged = written;
    assert(!Span::writeLittleEndian(65536u, Span::Width::Uint16, written.data(), 5u));
    assert(!Span::writeLittleEndian(7u, Span::Width::Uint32, written.data(), 3u));
    assert(!Span::writeLittleEndian(7u, static_cast<Span::Width>(255u), written.data(), 5u));
    assert(!Span::writeLittleEndian(7u, Span::Width::Uint16, nullptr, 5u));
    assert(written == unchanged);
    assert(Span::writeLittleEndian(65535u, Span::Width::Uint16, written.data() + 1u, 2u));
    const std::array<std::uint8_t, 8u> indices{0u, 0u, 2u, 0u, 3u, 0u, 4u, 0u};
    IndexInputConsumptionContract index(indices.data(),
        indices.size(),
        IndexFormat::Uint16,
        Span::ByteOrder::LittleEndian,
        IndexInputConsumptionContract::PrimitiveRestart::Disabled);
    const Arguments::Budget budget{1u, 20u, 20u + sizeof(Arguments::DrawObservation)};
    Arguments::ExpectedGpuWords words{{3u, 2u, 1u, 0xfffffffeu, 1u}};
    Arguments expected(words, budget, index);
    assert(!expected.diagnostic());
    assert(expected.purpose() == Arguments::Purpose::GpuOutputExpectation);
    Arguments::DrawObservation observed;
    assert(!expected.observe(0u, observed) && observed.active && observed.fetch.firstVertex == 0u
        && observed.fetch.vertexCount == 3u && observed.logical.vertexOffset == -2);
    words.words.fill(0u);
    assert(!expected.observe(0u, observed) && observed.active);
    Arguments host({expected.bytes().data(),
                       20u,
                       0u,
                       1u,
                       20u,
                       Span::ByteOrder::LittleEndian,
                       Arguments::Origin::HostAuthoredImmutable},
        budget,
        index);
    assert(!host.diagnostic() && host.purpose() == Arguments::Purpose::ImmutableHostUpload
        && host.bytes() == expected.bytes());
    Arguments rawGpu({expected.bytes().data(),
                         20u,
                         0u,
                         1u,
                         20u,
                         Span::ByteOrder::LittleEndian,
                         Arguments::Origin::ApprovedGpuProduced},
        budget,
        index);
    expect(rawGpu.diagnostic(), "RHI-INDIRECT-ARGUMENT-GPU-PROOF-REQUIRED");
    words.words = {3u, 2u, 1u, 0xfffffffeu, UINT32_MAX};
    Arguments badInstance(words, budget, index);
    expect(badInstance.diagnostic(), "RHI-INDIRECT-ARGUMENT-INSTANCE-RANGE");
    assert(badInstance.bytes().empty() && !badInstance.drawCount()
        && badInstance.purpose() == Arguments::Purpose::GpuOutputExpectation);
    words.words = {0u, UINT32_MAX, UINT32_MAX, 0x80000000u, UINT32_MAX};
    Arguments noop(words, budget, index);
    assert(!noop.diagnostic() && !noop.observe(0u, observed) && !observed.active);
    words.words = {3u, 2u, 2u, 0xfffffffeu, 1u};
    Arguments badIndex(words, budget, index);
    assert(badIndex.diagnostic() && !badIndex.drawCount());
    GpuIndirectOutputPreparationBudget limits{1u, true, 20u, budget, 2u, 100u};
    assert(!limits.validate(2u));
    expect(limits.validate(3u), "RHI-GPU-INDIRECT-OUTPUT-BUDGET-EXCEEDED");
    auto shortBudget = limits;
    shortBudget.maxOutputBytes = 19u;
    expect(shortBudget.validate(2u), "RHI-GPU-INDIRECT-OUTPUT-BUDGET-EXCEEDED");
    shortBudget = limits;
    shortBudget.expectation.maxSnapshotBytes--;
    expect(shortBudget.validate(2u), "RHI-GPU-INDIRECT-OUTPUT-BUDGET-EXCEEDED");
    shortBudget = limits;
    shortBudget.optIn = false;
    expect(shortBudget.validate(2u), "RHI-GPU-INDIRECT-OUTPUT-BUDGET");
    shortBudget = limits;
    shortBudget.maxChildOwners = 0u;
    expect(shortBudget.validate(2u), "RHI-GPU-INDIRECT-OUTPUT-BUDGET");
    BufferDesc desc;
    desc.sizeBytes = 20u;
    desc.usage = GpuIndirectOutputPreparationContract::usage();
    desc.memory = MemoryUsage::CpuToGpu;
    desc.initialState = ResourceState::ShaderWrite;
    assert(!GpuIndirectOutputPreparationContract::validateStorage(desc));
    auto altered = desc;
    altered.sizeBytes = 19u;
    assert(GpuIndirectOutputPreparationContract::validateStorage(altered));
    altered = desc;
    altered.usage = ResourceUsage::IndirectBuffer;
    assert(GpuIndirectOutputPreparationContract::validateStorage(altered));
    altered = desc;
    altered.memory = MemoryUsage::GpuOnly;
    assert(GpuIndirectOutputPreparationContract::validateStorage(altered));
    altered = desc;
    altered.initialState = ResourceState::ShaderRead;
    assert(GpuIndirectOutputPreparationContract::validateStorage(altered));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    core::CpuMemoryQuotaScope scoped(quota);
    core::CpuMemoryQuotaLease pressure(quota, 1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 100u; ++i) {
        assert(
            !limits.validate(2u) && !GpuIndirectOutputPreparationContract::validateStorage(desc));
        assert(!expected.observe(0u, observed));
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
}

void recordingSequence()
{
    using Lifetime = GpuIndirectOutputLifetimeContract;
    using Status = Lifetime::Status;
    for (const auto before : {Status::Cold,
             Status::Recorded,
             Status::Submitted,
             Status::Completed,
             Status::Failed,
             Status::Abandoned})
        for (const auto after : {Status::Cold,
                 Status::Recorded,
                 Status::Submitted,
                 Status::Completed,
                 Status::Failed,
                 Status::Abandoned}) {
            const bool terminalCompletion = before == Status::Submitted
                && (after == Status::Completed || after == Status::Failed);
            const bool admitted = (before == Status::Cold && after == Status::Recorded)
                || (before == Status::Recorded
                    && (after == Status::Submitted || after == Status::Failed
                        || after == Status::Abandoned));
            assert((Lifetime::validateTransition(before, after, false) == nullptr) == admitted);
            assert((Lifetime::validateTransition(before, after, true) == nullptr)
                == (admitted || terminalCompletion));
        }
    expect(Lifetime::validateTransition(Status::Submitted, Status::Completed),
        "RHI-GPU-INDIRECT-LIFETIME-NATIVE-COMPLETION-REQUIRED");
    expect(Lifetime::validateTransition(Status::Submitted, Status::Failed),
        "RHI-GPU-INDIRECT-LIFETIME-NATIVE-COMPLETION-REQUIRED");
    expect(Lifetime::validateTransition(static_cast<Status>(255u), Status::Recorded, true),
        "RHI-GPU-INDIRECT-LIFETIME-TRANSITION");
    using Sequence = GpuIndirectOutputSequence;
    using Command = Sequence::Command;
    const GpuIndirectOutputRecordingIdentity identity{{42u}, 43u, 44u};
    const GpuIndirectOutputRecordingBudget budget{1u, true, 9u};
    const std::array<Command, 9u> path{Command::ProducerBegin,
        Command::Dispatch,
        Command::ProducerEnd,
        Command::Dependency,
        Command::ConsumerBegin,
        Command::StreamBinding,
        Command::StreamBinding,
        Command::Indirect,
        Command::ConsumerEnd};
    const std::array<Command, 8u> domain{Command::ProducerBegin,
        Command::Dispatch,
        Command::ProducerEnd,
        Command::Dependency,
        Command::ConsumerBegin,
        Command::StreamBinding,
        Command::Indirect,
        Command::ConsumerEnd};
    Sequence sequence(budget, identity);
    const auto unchanged = [&](Command command, const GpuIndirectOutputRecordingIdentity& input) {
        const auto count = sequence.commands(), calls = sequence.calls(),
                   records = sequence.records();
        const auto phase = sequence.phase();
        const auto* predicted = sequence.canRecord(command, input);
        assert(predicted);
        expect(sequence.record(command, input), predicted);
        assert(sequence.commands() == count && sequence.calls() == calls
            && sequence.records() == records && sequence.phase() == phase);
    };
    for (std::size_t step = 0u; step <= path.size(); ++step) {
        // Each phase rejects unsupported ordering without poisoning valid retry.
        for (const auto command : domain)
            if (sequence.canRecord(command, identity))
                unchanged(command, identity);
        for (const auto command : domain) {
            auto changed = identity;
            changed.resource = {41u};
            unchanged(command, changed);
            changed = identity;
            changed.generation = 45u;
            unchanged(command, changed);
            changed = identity;
            changed.recording = 45u;
            unchanged(command, changed);
            changed = identity;
            changed.resource = {};
            unchanged(command, changed);
            changed = identity;
            changed.generation = 0u;
            unchanged(command, changed);
            changed = identity;
            changed.recording = 0u;
            unchanged(command, changed);
        }
        unchanged(static_cast<Command>(255u), identity);
        if (step == path.size())
            break;
        assert(sequence.finish());
        const auto count = sequence.commands();
        assert(!sequence.canRecord(path[step], identity) && sequence.commands() == count);
        assert(!sequence.record(path[step], identity) && sequence.commands() == count + 1u);
    }
    assert(!sequence.finish() && sequence.commands() == 9u && sequence.calls() == 1u
        && sequence.records() == 1u);
    assert(sequence.phase() == GraphicsStorageWriteSequence::Phase::ConsumerConsumed);
    // Exact and one-less command ceilings, including the mandatory end reserve.
    for (std::uint32_t limit = 7u; limit <= 9u; ++limit) {
        Sequence bounded({1u, true, limit}, identity);
        bool admitted = true;
        for (const auto command : path) {
            if (bounded.canRecord(command, identity)) {
                admitted = false;
                break;
            }
            assert(!bounded.record(command, identity));
        }
        assert(admitted == (limit == 9u) && bounded.commands() <= limit);
        if (!admitted)
            assert(bounded.finish());
    }
    Sequence minimum({1u, true, 7u}, identity);
    for (const auto command : {Command::ProducerBegin,
             Command::Dispatch,
             Command::ProducerEnd,
             Command::Dependency,
             Command::ConsumerBegin,
             Command::Indirect,
             Command::ConsumerEnd})
        assert(!minimum.record(command, identity));
    assert(!minimum.finish() && minimum.commands() == 7u);
    for (const auto invalid : {GpuIndirectOutputRecordingBudget{0u, true, 9u},
             {1u, false, 9u},
             {1u, true, 0u},
             {1u, true, 6u}}) {
        Sequence denied(invalid, identity);
        assert(invalid.validate());
        expect(denied.record(Command::ProducerBegin, identity), invalid.validate());
        assert(denied.finish() && !denied.commands());
    }
    Sequence invalidIdentity(budget, {{}, 43u, 44u});
    expect(invalidIdentity.record(Command::ProducerBegin, identity),
        "RHI-GPU-INDIRECT-OUTPUT-RECORDING-MISMATCH");
    assert(invalidIdentity.finish() && !invalidIdentity.commands());
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const auto plan = semantics::GpuIndirectOutputRecordingLowering::current(backend);
        assert(plan.contractVersion == 1u && !plan.executable
            && plan.validatorOnly == (backend == Backend::Null));
        assert(plan.mode
            == (plan.validatorOnly ? CapabilityMode::Equivalent
                                   : CapabilityMode::DiagnosticFailure));
        expect(plan.diagnosticId,
            plan.validatorOnly ? "RHI-GPU-INDIRECT-SEQUENCE-VALIDATOR-ONLY"
                               : "RHI-GPU-INDIRECT-SEQUENCE-PAYLOAD-MISSING");
    }
    const auto unknown =
        semantics::GpuIndirectOutputRecordingLowering::current(static_cast<Backend>(255u));
    assert(!unknown.executable && !unknown.validatorOnly
        && unknown.mode == CapabilityMode::DiagnosticFailure);
    expect(unknown.diagnosticId, "RHI-GPU-INDIRECT-SEQUENCE-BACKEND-UNKNOWN");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    core::CpuMemoryQuotaScope scope(quota);
    core::CpuMemoryQuotaLease pressure(quota, 1u);
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned iteration = 0u; iteration < 100u; ++iteration) {
        Sequence local(budget, identity);
        for (const auto command : path)
            assert(!local.canRecord(command, identity) && !local.record(command, identity));
        assert(!local.finish());
        assert(!Lifetime::validateTransition(Status::Submitted, Status::Completed, true));
    }
    std::pmr::set_default_resource(previous);
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
}
} // namespace

int main()
{
    layout();
    scope();
    lowering();
    expectationAndStorage();
    recordingSequence();
    std::puts(
        "ComputeFixedIndirectProducer/1 L2: exact20-byte binding/scope/single dispatch/dependency, four-backend fail-closed and allocation0 PASS; no output/lease/native authority");
}
