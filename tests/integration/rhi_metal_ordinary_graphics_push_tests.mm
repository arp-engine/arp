#include "../fixtures/indirect_argument_fixture.h"
#include "../fixtures/metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/gpu_extensions.h"

#include <Metal/Metal.h>

#ifdef NDEBUG
#error Native Graphics push tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;

class PushFixture final {
public:
    static DescriptorLayoutDesc inputs()
    {
        DescriptorLayoutDesc result;
        result.name = "PushOnlyInputs";
        return result;
    }

    static ShaderStage stages(bool depth)
    {
        return depth ? ShaderStage::Vertex : ShaderStage::Vertex | ShaderStage::Fragment;
    }

    PushFixture(const char* vs, const char* fs, bool depth = false)
        : pass(vs, depth ? nullptr : fs, false, inputs(), {{stages(depth), 0u, sizeof(valueA)}}),
          vs(vs), fs(depth ? nullptr : fs), depth(depth)
    {
        const auto* extension = static_cast<BindlessCrossExtension*>(
            f().device->queryInterface(kExtensionBindlessCross));
        assert(extension && extension->capabilities().preparedGraphicsBindingContractVersion == 0u);
        assert(f().provider->consumption().pushRanges().size() == 1u);
    }

    test::OrdinaryGraphicsFixture& f() { return pass.graphics; }

    auto provider()
    {
        shader::SingleEntryShaderLoadBudget load{
            f().cpuBytes, f().cpuBytes * 4u, f().cpuBytes, 16u};
        auto vertex = shader::SingleEntryShaderArtifact::load(vs, load, f().quota, f().diagnostic);
        assert(vertex);
        std::shared_ptr<const shader::SingleEntryShaderArtifact> fragment;
        if (fs) {
            fragment = shader::SingleEntryShaderArtifact::load(fs, load, f().quota, f().diagnostic);
            assert(fragment);
        }
        PipelineLayoutDesc desc;
        desc.descriptorLayouts = {f().inputs};
        desc.descriptorSets = {{0u, f().inputs.name}};
        desc.pushConstantRanges = {{stages(depth), 0u, sizeof(valueA)}};
        auto layout = f().device->createPipelineLayout(desc);
        assert(layout);
        RenderPipelineDesc state;
        state.label = "SecondPushProvider";
        state.layout = layout.get();
        state.dynamicViewportScissor = true;
        if (fs)
            state.colorFormat = TextureFormat::Rgba8Unorm;
        else {
            state.depthStencil.format = TextureFormat::D32Float;
            state.depthStencil.depthTestEnabled = true;
            state.depthStencil.depthWriteEnabled = true;
        }
        auto capabilities = semantics::nativeBackendCapabilityContract(Backend::Metal);
        capabilities.queriedFromNativeDevice = true;
        capabilities.queriedExtendedFeatures = true;
        auto result = MetalRhiOrdinaryGraphicsProvider::create(vertex,
            fragment,
            std::move(state),
            capabilities,
            f().budget(),
            f().quota,
            f().diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", f().diagnostic.c_str());
        assert(result);
        return result;
    }

    auto binding(const std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>& provider)
    {
        auto result = MetalRhiOrdinaryPreparedGraphicsBinding::prepare(
            provider, *f().set, 0u, f().registry, f().budget(), f().quota, f().diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", f().diagnostic.c_str());
        assert(result);
        return result;
    }

    auto push(const std::array<float, 8>& value,
        const std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>& provider = {})
    {
        PushConstantsDesc desc{stages(depth), 0u, sizeof(value), value.data()};
        auto result = MetalRhiOrdinaryPreparedGraphicsPush::prepare(
            provider ? provider : f().provider, desc, f().budget(), f().quota, f().diagnostic);
        if (!result)
            std::fprintf(stderr, "%s\n", f().diagnostic.c_str());
        assert(result);
        return result;
    }

    void reject(const PushConstantsDesc& desc, const char* expected)
    {
        const auto before = f().quota->liveBytes();
        auto result = MetalRhiOrdinaryPreparedGraphicsPush::prepare(
            f().provider, desc, f().budget(), f().quota, f().diagnostic);
        if (result || f().diagnostic.find(expected) == f().diagnostic.npos)
            std::fprintf(stderr, "Expected %s: %s\n", expected, f().diagnostic.c_str());
        assert(!result && f().diagnostic.find(expected) != f().diagnostic.npos
            && f().quota->liveBytes() == before);
    }

    void verify(bool b)
    {
        if (depth)
            pass.verifyDepth(b ? valueB[3] : valueA[3]);
        else
            pass.verifyColor(b ? colorB : colorA);
    }

    void release()
    {
        f().set.reset();
        f().arena.reset();
        f().provider.reset();
    }

    test::OrdinaryGraphicsPassFixture pass;
    const char* vs;
    const char* fs;
    bool depth;
    const std::array<float, 8> valueA{
        1, 0, 0, 0.25f, 64.0f / UINT8_MAX, 128.0f / UINT8_MAX, 192.0f / UINT8_MAX, 1},
        valueB{1, 0, 0, 0.5f, 192.0f / UINT8_MAX, 64.0f / UINT8_MAX, 128.0f / UINT8_MAX, 1};
    const std::array<std::uint8_t, 4> colorA{64, 128, 192, 255}, colorB{192, 64, 128, 255};
};

void expectFailure(bool failed, const core::pmr::String& diagnostic, const char* expected)
{
    if (!failed || diagnostic.find(expected) == diagnostic.npos)
        std::fprintf(stderr, "Expected %s: %s\n", expected, diagnostic.c_str());
    assert(failed && diagnostic.find(expected) != diagnostic.npos);
}

void negatives(const char* vs, const char* fs)
{
    PushFixture p(vs, fs);
    auto& f = p.f();
    PushConstantsDesc desc{PushFixture::stages(false), 0u, sizeof(p.valueA), p.valueA.data()};
    auto invalid = desc;
    invalid.stages = ShaderStage::None;
    p.reject(invalid, "PC-E001");
    invalid = desc;
    invalid.offsetBytes = 16u;
    p.reject(invalid, "PC-E002");
    invalid = desc;
    invalid.sizeBytes = 0u;
    p.reject(invalid, "PC-E003");
    invalid = desc;
    invalid.data = nullptr;
    p.reject(invalid, "PC-E004");
    invalid = desc;
    invalid.stages = ShaderStage::Compute;
    p.reject(invalid, "PC-E006");
    invalid = desc;
    invalid.sizeBytes = 16u;
    p.reject(invalid, "PC-E009");
    invalid = desc;
    invalid.stages = ShaderStage::Vertex;
    p.reject(invalid, "PC-E016");
    auto& reflection = const_cast<ShaderReflectionDesc&>(f.provider->interface().reflection);
    const auto version = reflection.pushConstantContractVersion,
               visibility = reflection.pushConstantStages;
    reflection.pushConstantContractVersion = "Broken/1";
    p.reject(desc, "PC-E015");
    reflection.pushConstantContractVersion = version;
    reflection.pushConstantStages = "Vertex|Fragment";
    p.reject(desc, "PC-E016");
    reflection.pushConstantStages = visibility;
    for (unsigned i = 0; i < 2; ++i) {
        auto& facts = f.provider->native()->graphicsBufferFacts[i];
        assert(facts.entries.size() == 1u);
        const auto stage = facts.stage;
        const auto fact = facts.entries[0];
        std::printf("ordinary Graphics push fact stage=%u slot=%u alignment=%llu span=%llu\n",
            i,
            fact.slot,
            static_cast<unsigned long long>(fact.alignment),
            static_cast<unsigned long long>(fact.requiredBytes));
        assert(fact.requiredBytes == sizeof(p.valueA));
        facts.stage = ShaderStage::Compute;
        p.reject(desc, "FACTS-STAGE-MISMATCH");
        facts.stage = stage;
        facts.version = 0u;
        p.reject(desc, "FACTS-MISSING");
        facts.version = 1u;
        facts.entries[0].alignment = 0u;
        p.reject(desc, "FACTS-MISSING");
        facts.entries[0] = fact;
        facts.entries[0].requiredBytes = 0u;
        p.reject(desc, "FACTS-MISSING");
        facts.entries[0] = fact;
        facts.entries[0].requiredBytes = 16u;
        p.reject(desc, "NATIVE-SIZE-MISMATCH");
        facts.entries[0] = fact;
        facts.entries[0].requiredBytes = 64u;
        p.reject(desc, "OFFSET-RANGE");
        facts.entries[0] = fact;
        facts.entries[0].slot = UINT32_MAX;
        p.reject(desc, "FACTS-MISSING");
        facts.entries[0] = fact;
        facts.entries.push_back(fact);
        p.reject(desc, "FACTS-DUPLICATE");
        facts.entries.pop_back();
    }
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        p.reject(desc, "ALLOCATION-FAILED");
    }
    auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(f.cpuBytes);
    assert(!MetalRhiOrdinaryPreparedGraphicsPush::prepare(
        f.provider, desc, f.budget(), foreignQuota, f.diagnostic));
    auto alternate = p.provider();
    auto push = p.push(p.valueA);
    const auto native = f.provider->native();
    const auto function = native->fragmentFunction, state = native->pipelineState;
    native->fragmentFunction = alternate->native()->fragmentFunction;
    assert(!push->validate().success);
    native->fragmentFunction = function;
    native->pipelineState = alternate->native()->pipelineState;
    assert(!push->validate().success);
    native->pipelineState = state;
    auto& facts = native->graphicsBufferFacts[1];
    const auto alignment = facts.entries[0].alignment;
    facts.entries[0].alignment = alignment * 2u;
    expectFailure(!push->validate().success, push->validate().diagnostics, "CAPTURE-MISMATCH");
    facts.entries[0].alignment = alignment;
    assert(push->validate().success);
}

void execute(const char* vs, const char* fs, bool depth, bool dropped, bool selectionB)
{
    PushFixture p(vs, fs, depth);
    auto& f = p.f();
    const auto& selected = selectionB ? p.valueB : p.valueA;
    auto value = selected;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto push = p.push(value);
    auto binding = f.prepare();
    auto pass = p.pass.prepare();
    std::pmr::set_default_resource(previous);
    value.fill(0);
    assert(std::memcmp(push->bytes().data(), selected.data(), sizeof(selected)) == 0);
    auto probe = p.pass.command(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic, push));
    const auto owners = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordRenderPassEnd(f.diagnostic));
    probe->abandonUnsubmitted();
    probe.reset();
    auto shortCommand = p.pass.command(owners - 1u);
    expectFailure(
        !shortCommand->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic, push),
        f.diagnostic,
        "OWNER-CAPACITY");
    assert(!shortCommand->stats()->nativeRenderEncoderCreated
        && !shortCommand->retentionStorage()->resourceOwnerCount());
    shortCommand->abandonUnsubmitted();
    shortCommand.reset();
    auto command = p.pass.command(owners);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(f.quota, f.cpuBytes - f.quota->liveBytes());
        previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic, push));
        for (unsigned i = 0; i < 32; ++i)
            assert(command->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        expectFailure(!command->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic, push),
            f.diagnostic,
            "PC-E013");
        auto& facts = f.provider->native()->graphicsBufferFacts[0];
        facts.version = 0u;
        assert(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        facts.version = 1u;
        assert(!command->stats()->nativeDrawEncoded
            && command->retentionStorage()->resourceOwnerCount() == owners);
        const auto retry = core::defaultCpuAllocator().stats().allocationCount;
        assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        for (unsigned i = 0; i < 32; ++i) {
            assert(command->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic, push));
            assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        }
        assert(command->recordRenderPassEnd(f.diagnostic));
        assert(core::defaultCpuAllocator().stats().allocationCount == retry);
        std::pmr::set_default_resource(previous);
    }
    assert(command->stats()->nativePushConstantSetBytesWritten
        && !command->stats()->nativePushConstantUploadBufferWritten
        && command->stats()->nativePushConstantUploadBytes == sizeof(p.valueA));
    assert(command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsPush> observed = push;
    std::weak_ptr<const MetalRhiOrdinaryGraphicsProvider> provider = f.provider;
    push.reset();
    binding.reset();
    pass.reset();
    p.release();
    assert(!observed.expired() && !provider.expired());
    auto gate = createMetalRhiNativeSharedEventPayload("GraphicsPushCompletion");
    assert(gate && gate->nativeRuntimeBacked);
    MetalRhiNativeQueueStats queue;
    auto done = command->preparedCompletionState();
    assert(command->submit(queue, done, gate, 1u, {}, 0u, f.diagnostic));
    if (dropped)
        command.reset();
    assert(!done->isComplete() && !observed.expired() && !provider.expired());
    signalMetalRhiNativeSharedEvent(gate, 1u);
    done->wait();
    assert(observed.expired() && provider.expired());
    p.verify(selectionB);
    std::printf(
        "ordinary Graphics push depth=%d dropped=%d B=%d owners=%zu; copied selected-stage bytes, allocation0 and delayed retirement pass\n",
        depth,
        dropped,
        selectionB,
        owners);
}

void cancellation(const char* vs, const char* fs, bool sealed)
{
    PushFixture p(vs, fs);
    auto& f = p.f();
    auto push = p.push(p.valueA);
    auto binding = f.prepare();
    auto pass = p.pass.prepare();
    auto command = p.pass.command(128u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic, push));
    if (sealed)
        assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsPush> observed = push;
    std::weak_ptr<const MetalRhiOrdinaryGraphicsProvider> provider = f.provider;
    push.reset();
    binding.reset();
    pass.reset();
    p.release();
    assert(!observed.expired() && !provider.expired());
    command->abandonUnsubmitted();
    assert(observed.expired() && provider.expired());
}

void transition(const char* vs, const char* fs, bool admitted)
{
    PushFixture p(vs, fs);
    auto& f = p.f();
    auto next = p.provider();
    auto a = f.prepare(), b = p.binding(next);
    auto pushA = p.push(p.valueA), pushB = p.push(p.valueB, next);
    auto pass = p.pass.prepare();
    auto probe = p.pass.command(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, a, f.diagnostic, pushA));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
    assert(probe->recordOrdinaryPreparedGraphicsBinding(b, f.diagnostic, pushB));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordRenderPassEnd(f.diagnostic));
    probe->abandonUnsubmitted();
    probe.reset();
    assert(combined > initial);
    auto command = p.pass.command(admitted ? combined : combined - 1u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, a, f.diagnostic));
    expectFailure(
        !command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic), f.diagnostic, "FRESH-BLOCK-REQUIRED");
    expectFailure(!command->recordOrdinaryPreparedGraphicsBinding(a, f.diagnostic, pushB),
        f.diagnostic,
        "PUSH-BINDING-MISMATCH");
    assert(command->recordOrdinaryPreparedGraphicsBinding(a, f.diagnostic, pushA));
    expectFailure(!command->recordOrdinaryPreparedGraphicsBinding(b, f.diagnostic, pushB),
        f.diagnostic,
        "PC-E013");
    assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    const bool switched = command->recordOrdinaryPreparedGraphicsBinding(b, f.diagnostic, pushB);
    assert(switched == admitted);
    if (admitted)
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    else
        expectFailure(!switched, f.diagnostic, "OWNER-CAPACITY");
    assert(command->retentionStorage()->resourceOwnerCount() == (admitted ? combined : initial));
    if (admitted) {
        assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        // Returning to an ABI-identical provider still requires a fresh block.
        assert(command->recordOrdinaryPreparedGraphicsBinding(a, f.diagnostic));
        expectFailure(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic),
            f.diagnostic,
            "FRESH-BLOCK-REQUIRED");
        assert(command->recordOrdinaryPreparedGraphicsBinding(a, f.diagnostic, pushA));
        expectFailure(!command->recordOrdinaryPreparedGraphicsBinding(b, f.diagnostic),
            f.diagnostic,
            "PC-E013");
        assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic));
        assert(command->recordOrdinaryPreparedGraphicsBinding(b, f.diagnostic));
        expectFailure(!command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic),
            f.diagnostic,
            "FRESH-BLOCK-REQUIRED");
        assert(command->recordOrdinaryPreparedGraphicsBinding(b, f.diagnostic, pushB));
    }
    assert(command->recordDraw(3u, 1u, 0u, 0u, f.diagnostic)
        && command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const MetalRhiOrdinaryPreparedGraphicsPush> observedA = pushA, observedB = pushB;
    a.reset();
    b.reset();
    pushA.reset();
    pushB.reset();
    pass.reset();
    next.reset();
    p.release();
    assert(!observedA.expired() && (admitted ? !observedB.expired() : observedB.expired()));
    MetalRhiNativeQueueStats queue;
    auto done = command->preparedCompletionState();
    assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
    done->wait();
    assert(observedA.expired() && observedB.expired());
    p.verify(admitted);
    std::printf(
        "ordinary Graphics push transition admitted=%d initial=%zu combined=%zu; provider freshness and atomic native output pass\n",
        admitted,
        initial,
        combined);
}

void indirectPush(const char* vs, const char* fs, bool admitted)
{
    PushFixture p(vs, fs);
    auto& f = p.f();
    auto binding = f.prepare();
    auto pushA = p.push(p.valueA), pushB = p.push(p.valueB);
    auto pass = p.pass.prepare();
    const std::array<std::uint8_t, 6> indexBytes{0u, 0u, 1u, 0u, 2u, 0u};
    auto index = MetalRhiOrdinaryPreparedIndexBinding::prepare(f.provider,
        *f.device,
        indexBytes.data(),
        indexBytes.size(),
        IndexFormat::Uint16,
        core::UnsignedElementSpan::ByteOrder::LittleEndian,
        IndexInputConsumptionContract::PrimitiveRestart::Disabled,
        f.registry,
        f.budget(),
        f.quota,
        f.diagnostic);
    assert(index);
    test::IndirectArgumentFixture<1u> bytes({{{3u, 1u, 0u, 0, 0u}}});
    auto args = MetalRhiOrdinaryPreparedIndirectArguments::prepare(
        index, *f.device, bytes.source(), bytes.budget(), f.budget(), f.diagnostic);
    assert(args);
    const auto scope = args->content().requiredAccess(args->resource().id());
    const auto prepare = [&](std::uint32_t owners) {
        const char* id = nullptr;
        auto storage = common::CommandRetentionStorage::prepare(
            {1u, true, owners, 0u, 0u, 0u, f.cpuBytes}, f.quota, id);
        assert(storage);
        auto result = createMetalRhiOrdinaryIndirectNativeCommandPayload(
            {1u, true, 5u, 2u, 2u}, "IndirectPush", f.registry, std::move(storage), f.diagnostic);
        assert(result);
        return result;
    };
    auto probe = prepare(128u);
    assert(probe->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic));
    assert(!probe->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic)
        && f.diagnostic.find("FRESH-BLOCK-REQUIRED") != f.diagnostic.npos);
    assert(probe->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic, pushA));
    assert(probe->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic));
    const auto initial = probe->retentionStorage()->resourceOwnerCount();
    assert(probe->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic, pushB));
    const auto combined = probe->retentionStorage()->resourceOwnerCount();
    probe->abandonUnsubmitted();
    probe.reset();
    auto command = prepare(admitted ? combined : combined - 1u);
    assert(command->recordOrdinaryPreparedRenderPassBegin(pass, binding, f.diagnostic, pushA));
    // The selected native push range must still match its frozen ABI at draw.
    auto& fact = f.provider->native()->graphicsBufferFacts[0].entries[0];
    const auto alignment = fact.alignment;
    fact.alignment = alignment + sizeof(float);
    assert(!command->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic)
        && !command->stats()->ordinaryIndirectCallCount);
    fact.alignment = alignment;
    assert(command->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic));
    assert(
        command->recordOrdinaryPreparedGraphicsBinding(binding, f.diagnostic, pushB) == admitted);
    if (!admitted)
        assert(f.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
            && command->stats()->ordinaryIndirectCommandCount == 2u);
    assert(command->recordOrdinaryPreparedIndirectDraw(args, scope, f.diagnostic));
    assert(command->recordRenderPassEnd(f.diagnostic) && command->finish(f.diagnostic));
    std::weak_ptr<const void> observedA = pushA, observedB = pushB;
    pushA.reset();
    pushB.reset();
    args.reset();
    index.reset();
    binding.reset();
    pass.reset();
    MetalRhiNativeQueueStats queue;
    auto done = command->preparedCompletionState();
    assert(command->submit(queue, done, {}, 0u, {}, 0u, f.diagnostic));
    done->wait();
    assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed && observedA.expired()
        && observedB.expired());
    p.verify(admitted);
    std::printf(
        "native indirect push admitted=%d owners%zu/%zu, fixed freshness/captured ABI/atomic rebind and exact bytes/pixels PASS\n",
        admitted,
        initial,
        combined);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    @autoreleasepool {
        negatives(argv[1], argv[2]);
        for (const bool depth : {false, true})
            for (const bool dropped : {false, true})
                for (const bool b : {false, true})
                    execute(argv[1], argv[2], depth, dropped, b);
        for (const bool sealed : {false, true})
            cancellation(argv[1], argv[2], sealed);
        for (const bool admitted : {false, true})
            transition(argv[1], argv[2], admitted);
        for (const bool admitted : {false, true})
            indirectPush(argv[1], argv[2], admitted);
    }
    std::puts(
        "ordinary Graphics push: actual selected-stage bytes/data, owner quota, provider freshness and native retirement pass; public versions0");
}
