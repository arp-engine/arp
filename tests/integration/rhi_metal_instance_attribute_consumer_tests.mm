#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/metal_native_identity_fixture.h"
#include "../fixtures/metal_ordinary_graphics_vertex_fixture.h"
#include "rhi/backends/metal/metal_instance_attribute_consumer.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef NDEBUG
#error Native instance consumer tests require assertions
#endif
namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
using Consumer = MetalRhiPreparedInstanceAttributeConsumer;
static_assert(!std::is_copy_constructible_v<Consumer>);
static_assert(MetalRhiVertexInputFacts::perVertex == MTLVertexStepFunctionPerVertex);
static_assert(MetalRhiVertexInputFacts::perInstance == MTLVertexStepFunctionPerInstance);
static_assert(MTLVertexFormatFloat == 28u && MTLVertexFormatFloat2 == 29u
    && MTLVertexFormatFloat3 == 30u && MTLVertexFormatFloat4 == 31u);
static_assert(MTLDataTypeFloat == 3u && MTLDataTypeFloat2 == 4u && MTLDataTypeFloat3 == 5u
    && MTLDataTypeFloat4 == 6u);

std::shared_ptr<core::CpuMemoryQuota> run(const char* vs, const char* fs)
{
    test::OrdinaryGraphicsVertexFixture fixture(vs, fs);
    auto& f = fixture.pass.graphics;
    InstanceAttributeFetchIntent intent{1u, true};
    const auto prepare = [&] {
        return Consumer::prepare(f.provider, intent, f.budget(), f.quota, f.diagnostic);
    };
    auto consumer = prepare();
    if (!consumer)
        std::fprintf(stderr,
            "Consumer: %s factsVersion%u inputs%zu\n",
            f.diagnostic.c_str(),
            f.provider->native()->vertexInputFacts.contractVersion,
            f.provider->native()->vertexInputFacts.shaderInputs.size());
    assert(consumer && !consumer->validate());
    assert(consumer->proof().inputCount == 3u && consumer->facts().streams.size() == 2u
        && consumer->facts().attributes.size() == 3u);
    for (const auto& stream : consumer->facts().streams)
        std::printf(
            "native instance ABI stream%u -> physical%u stride%llu stepFunction%u rate%llu\n",
            stream.slot,
            stream.physicalSlot,
            static_cast<unsigned long long>(stream.stride),
            stream.stepFunction,
            static_cast<unsigned long long>(stream.stepRate));
    for (const auto& attribute : consumer->facts().shaderInputs)
        std::printf("actual function input%u type%u active%d\n",
            attribute.location,
            attribute.type,
            attribute.active);
    test::CpuQuotaFactoryFixture factory(f.quota);
    const auto peak = factory.measure(prepare);
    factory.verify(peak, prepare);
    assert(f.diagnostic == "RHI-METAL-INSTANCE-ATTRIBUTE-PREPARATION-ALLOCATION-FAILED");
    const auto reject = [&](const char* id) {
        const auto* actual = consumer->validate();
        if (!actual || std::strcmp(actual, id))
            std::fprintf(stderr, "Expected %s got %s\n", id, actual ? actual : "success");
        assert(actual && !std::strcmp(actual, id));
        const auto charged = f.quota->liveBytes();
        assert(!prepare() && f.diagnostic == id && charged == f.quota->liveBytes());
    };
    auto& facts = f.provider->native()->vertexInputFacts;
    const auto original = facts;
    facts.contractVersion = 0u;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-MISSING");
    facts = original;
    facts.streams.pop_back();
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-COUNT-MAPPING");
    facts = original;
    facts.attributes.pop_back();
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-COUNT-MAPPING");
    facts = original;
    facts.shaderInputs.pop_back();
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-COUNT-MAPPING");
    facts = original;
    facts.streams[1].stepRate = 2u;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-STREAM-ABI");
    facts = original;
    facts.streams[1].stepFunction = MTLVertexStepFunctionPerVertex;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-STREAM-ABI");
    facts = original;
    facts.streams[0].stride++;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-STREAM-ABI");
    facts = original;
    facts.streams[0].physicalSlot++;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-STREAM-ABI");
    facts = original;
    facts.streams[1] = facts.streams[0];
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-STREAM-DUPLICATE");
    facts = original;
    facts.attributes[0].offset++;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-ATTRIBUTE-ABI");
    facts = original;
    facts.attributes[0].format = MTLVertexFormatFloat4;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-ATTRIBUTE-ABI");
    facts = original;
    facts.attributes[0].physicalSlot++;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-ATTRIBUTE-ABI");
    facts = original;
    facts.attributes[1] = facts.attributes[0];
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-ATTRIBUTE-DUPLICATE");
    facts = original;
    facts.shaderInputs[0].type = MTLDataTypeInt2;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-SHADER-ABI");
    facts = original;
    facts.shaderInputs[0].active = false;
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-SHADER-ABI");
    facts = original;
    facts.shaderInputs[1] = facts.shaderInputs[0];
    reject("RHI-METAL-INSTANCE-ATTRIBUTE-SHADER-DUPLICATE");
    facts = original;
    // An individually equivalent reordered set is not the captured creation
    // projection of this root. A new cold preparation may capture a new order.
    std::swap(facts.shaderInputs[0], facts.shaderInputs[1]);
    assert(consumer->validate()
        && !std::strcmp(
            consumer->validate(), "RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-CAPTURE-MISMATCH"));
    {
        auto reordered = prepare();
        assert(reordered && !reordered->validate());
    }
    facts = original;
    intent.optIn = false;
    assert(!prepare() && f.diagnostic == "RHI-INSTANCE-ATTRIBUTE-FETCH-VERSION-OPT-IN");
    intent.optIn = true;
    intent.stepRate = 2u;
    assert(!prepare() && f.diagnostic == "RHI-INSTANCE-ATTRIBUTE-FETCH-ABI-DOMAIN");
    intent.stepRate = 1u;
    auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
    assert(!Consumer::prepare(f.provider, intent, f.budget(), wrongQuota, f.diagnostic)
        && f.diagnostic == "RHI-METAL-INSTANCE-ATTRIBUTE-PROVIDER-MISMATCH");
    auto alias =
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>(f.registry, f.provider.get());
    assert(!Consumer::prepare(alias, intent, f.budget(), f.quota, f.diagnostic));
    assert(consumer->validateForProvider(alias));
    alias.reset();
    const auto native = f.provider->native();
    auto originalFunction = native->vertexFunction;
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        native->vertexFunction, native->fragmentFunction, [&] {
            const auto* id = consumer->validate();
            assert(id && !std::strcmp(id, "RHI-METAL-INSTANCE-ATTRIBUTE-OWNER-MISMATCH"));
        });
    {
        core::CpuMemoryQuotaScope scope(f.quota);
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (unsigned i = 0u; i < 100u; ++i)
            assert(!consumer->validate() && !consumer->validateForProvider(f.provider));
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    }
    // Native firstInstance controls use the same queried/proven provider and
    // exact immutable host argument path. They do not authorize GPU production.
    for (const auto firstInstance : {0u, 1u, 2u}) {
        const std::array<DrawIndexedIndirectCommand, 1u> record{{{3u, 1u, 2u, -1, firstInstance}}};
        auto arguments = fixture.indirectArguments(IndexFormat::Uint16, record);
        assert(!consumer->validateForProvider(arguments->index()->provider()));
        auto binding = f.prepare();
        auto pass = fixture.pass.prepare();
        auto command = fixture.indirectCommand(128u);
        assert(
            command && command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
        for (std::size_t i = 0u; i < 2u; ++i)
            assert(command->recordOrdinaryPreparedVertexBinding(fixture.prepared[i], f.diagnostic));
        assert(command->recordOrdinaryPreparedIndirectDraw(arguments,
            arguments->content().requiredAccess(arguments->resource().id()),
            f.diagnostic));
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
        auto done = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
        done->wait();
        fixture.pass.verifyColor(firstInstance == 0u
                ? std::array<std::uint8_t, 4u>{51u, 102u, 153u, 204u}
                : firstInstance == 1u ? std::array<std::uint8_t, 4u>{255u, 0u, 0u, 255u}
                                      : std::array<std::uint8_t, 4u>{0u, 255u, 0u, 255u});
    }
    std::printf(
        "selected instance consumer childOwners%zu coldPeak%llu; native firstInstance0/1/2 exact clear/red/green, shader/descriptor facts/mutations/quota/allocation0 PASS\n",
        consumer->childOwnerCount(),
        static_cast<unsigned long long>(peak));
    assert(!semantics::InstanceAttributeFetchLowering::current(Backend::Metal).executable);
    std::weak_ptr<void> function = originalFunction;
    std::weak_ptr<const MetalRhiOrdinaryGraphicsProvider> provider = f.provider;
    originalFunction.reset();
    fixture.dropInputs();
    native->vertexFunction.reset();
    assert(consumer->validate() && !function.expired() && !provider.expired());
    consumer.reset();
    assert(provider.expired() && function.expired());
    return f.quota;
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3 && metalRhiNativeDeviceHandle());
    @autoreleasepool {
        auto quota = run(argv[1], argv[2]);
        assert(!quota->liveBytes());
    }
    std::puts(
        "InstanceAttributeFetch/1 cold selected Metal ABI prerequisite PASS; public execution closed; no GPU producer/dependency/completion-transfer authority");
}
