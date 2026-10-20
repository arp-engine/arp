#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>

#ifdef NDEBUG
#error Native provider tests require assertions
#endif

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi_metal;
    assert(argc == 4 || argc == 5);
    constexpr std::uint64_t cpuBytes = 1024u * 1024u;
    auto quota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    diagnostic.reserve(512u);
    const shader::SingleEntryShaderLoadBudget loadBudget{cpuBytes, cpuBytes * 4u, cpuBytes, 16u};
    auto vertex = shader::SingleEntryShaderArtifact::load(argv[1], loadBudget, quota, diagnostic);
    assert(vertex);
    auto fragment = shader::SingleEntryShaderArtifact::load(argv[2], loadBudget, quota, diagnostic);
    assert(fragment);
    auto shadow = shader::SingleEntryShaderArtifact::load(argv[3], loadBudget, quota, diagnostic);
    assert(shadow);
    for (int i = 1; i < 4; ++i)
        std::filesystem::remove_all(std::filesystem::path(argv[i]).parent_path());
    const auto artifactBytes = quota->liveBytes();
    assert(artifactBytes);
    auto contract = semantics::nativeBackendCapabilityContract(Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    DescriptorLayoutDesc inputs;
    inputs.name = "OrdinarySealedGraphicsInputs";
    inputs.bindings = {{2u, DescriptorType::SampledTexture, ShaderStage::Fragment},
        {7u, DescriptorType::SampledTexture, ShaderStage::Fragment, 3u}};
    PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts = {inputs};
    layoutDesc.descriptorSets = {{0u, inputs.name}};
    auto layout = createMetalRhiPipelineLayout(layoutDesc);
    assert(layout);
    auto emptyLayout = createMetalRhiPipelineLayout({});
    assert(emptyLayout);
    const MetalRhiOrdinaryBindingBudget budget{1u, true, cpuBytes, 0u, 0u};
    const auto authored = [&](bool depthOnly) {
        RenderPipelineDesc state;
        state.label = "OrdinarySealedNativeGraphicsProvider";
        state.layout = depthOnly ? emptyLayout.get() : layout.get();
        state.dynamicViewportScissor = true;
        if (depthOnly) {
            state.depthStencil.format = TextureFormat::D32Float;
            state.depthStencil.depthTestEnabled = true;
            state.depthStencil.depthWriteEnabled = true;
        } else
            state.colorFormat = TextureFormat::Rgba8Unorm;
        return state;
    };
    if (argc == 5) {
        auto state = authored(false);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto failed = MetalRhiOrdinaryGraphicsProvider::create(
            vertex, fragment, std::move(state), contract, budget, quota, diagnostic);
        std::pmr::set_default_resource(previous);
        if (failed || diagnostic.find(argv[4]) == diagnostic.npos)
            std::fprintf(stderr, "Native failure: %s\n", diagnostic.c_str());
        assert(!failed && diagnostic.find(argv[4]) != diagnostic.npos
            && quota->liveBytes() == artifactBytes);
        std::puts(
            "ordinary graphics provider: actual failed MSL compilation rejects with diagnostic and retires partial pipeline/source storage");
        return 0;
    }
    auto expect = [&](auto selectedVertex,
                      auto selectedFragment,
                      RenderPipelineDesc state,
                      auto selectedContract,
                      auto selectedBudget,
                      auto selectedQuota,
                      const char* expected) {
        const auto before = quota->liveBytes();
        auto rejected = MetalRhiOrdinaryGraphicsProvider::create(std::move(selectedVertex),
            std::move(selectedFragment),
            std::move(state),
            selectedContract,
            selectedBudget,
            std::move(selectedQuota),
            diagnostic);
        if (rejected || diagnostic.find(expected) == diagnostic.npos)
            std::fprintf(stderr, "Expected %s; got %s\n", expected, diagnostic.c_str());
        assert(!rejected && diagnostic.find(expected) != diagnostic.npos);
        assert(quota->liveBytes() == before);
    };
    expect(fragment, fragment, authored(false), contract, budget, quota, "ARTIFACT-STAGE-MISMATCH");
    expect(vertex, vertex, authored(false), contract, budget, quota, "ARTIFACT-STAGE-MISMATCH");
    expect(shadow, fragment, authored(false), contract, budget, quota, "SOURCE-IDENTITY-MISMATCH");
    auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    expect(vertex,
        fragment,
        authored(false),
        contract,
        budget,
        foreignQuota,
        "ARTIFACT-QUOTA-MISMATCH");
    auto wrongContract = contract;
    wrongContract.backend = Backend::Null;
    expect(vertex, fragment, authored(false), wrongContract, budget, quota, "BACKEND-MISMATCH");
    auto noArguments = contract;
    noArguments.features.erase(std::remove(noArguments.features.begin(),
                                   noArguments.features.end(),
                                   semantics::NativeBackendFeature::ArgumentBuffers),
        noArguments.features.end());
    expect(
        vertex, fragment, authored(false), noArguments, budget, quota, "CAPABILITY-NOT-AUTHORIZED");
    auto disabled = budget;
    disabled.optIn = false;
    expect(vertex, fragment, authored(false), contract, disabled, quota, "BUDGET-INVALID");
    auto missingLayout = authored(false);
    missingLayout.layout = nullptr;
    expect(vertex, fragment, std::move(missingLayout), contract, budget, quota, "LAYOUT-MISSING");
    auto wrongStateAllocator = authored(false);
    wrongStateAllocator.colorTargets.~vector();
    // A foreign PMR container is supplied explicitly, never adopted silently.
    new (&wrongStateAllocator.colorTargets)
        core::pmr::Vector<ColorTargetState>(std::pmr::new_delete_resource());
    expect(vertex,
        fragment,
        std::move(wrongStateAllocator),
        contract,
        budget,
        quota,
        "STATE-ALLOCATOR-MISMATCH");
    expect(vertex, nullptr, authored(false), contract, budget, quota, "FRAGMENT-REQUIRED");
    auto missingWrite = authored(true);
    missingWrite.depthStencil.depthWriteEnabled = false;
    expect(vertex, nullptr, std::move(missingWrite), contract, budget, quota, "depth-only");
    {
        core::CpuMemoryQuotaLease pressure(quota, cpuBytes - quota->liveBytes());
        expect(vertex,
            fragment,
            authored(false),
            contract,
            budget,
            quota,
            "PROVIDER-ALLOCATION-FAILED");
    }
    assert(quota->liveBytes() == artifactBytes);
    auto state = authored(false);
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    auto provider = MetalRhiOrdinaryGraphicsProvider::create(
        vertex, fragment, std::move(state), contract, budget, quota, diagnostic);
    std::pmr::set_default_resource(previous);
    if (!provider)
        std::fprintf(stderr, "Provider: %s\n", diagnostic.c_str());
    assert(provider);
    assert(provider->native()->nativeRuntimeBacked
        && provider->native()->renderPipelinePayloadPresent);
    assert(provider->native()->vertexFunction && provider->native()->fragmentFunction
        && provider->native()->fragmentShaderLibrary);
    assert(provider->native()->preparedGraphicsStages
        == (ShaderStage::Vertex | ShaderStage::Fragment));
    const auto& vertexFacts = provider->native()->graphicsBufferFacts[0];
    const auto& fragmentFacts = provider->native()->graphicsBufferFacts[1];
    assert(vertexFacts.stage == ShaderStage::Vertex && vertexFacts.version == 1u
        && vertexFacts.entries.empty());
    assert(fragmentFacts.stage == ShaderStage::Fragment && fragmentFacts.version == 1u
        && fragmentFacts.entries.size() == 1u);
    assert(fragmentFacts.entries[0].slot == provider->stages()[1]->arrays[0].argumentBufferSlot);
    for (const auto& facts : provider->native()->graphicsBufferFacts) {
        assert(facts.entries.capacity() == semantics::MetalVertexStreamMapping::capacity);
        const auto factCharge = core::inspectCpuAllocationContaining(facts.entries.data());
        assert(factCharge.found && factCharge.quota == quota.get());
    }
    const auto& queried = fragmentFacts.entries[0];
    const MetalRhiBufferBindingFact* resolved = nullptr;
    const auto* rangeDiagnostic = validateMetalRhiBufferOffset(fragmentFacts.entries,
        fragmentFacts.version,
        queried.slot,
        0u,
        queried.requiredBytes,
        resolved);
    if (!queried.alignment || !queried.requiredBytes)
        assert(rangeDiagnostic
            && std::string_view(rangeDiagnostic).find("FACTS-MISSING") != std::string_view::npos);
    else
        assert(!rangeDiagnostic && resolved == &queried);
    std::printf(
        "Graphics actual buffer facts: fragment slot=%u alignment=%llu span=%llu; independently queried vertex list empty\n",
        queried.slot,
        static_cast<unsigned long long>(queried.alignment),
        static_cast<unsigned long long>(queried.requiredBytes));
    assert(provider->artifacts()[0] == vertex && provider->artifacts()[1] == fragment
        && provider->quota() == quota);
    assert(provider->stages()[0]->arrays.empty() && provider->stages()[1]->arrays.size() == 1u);
    assert(
        provider->consumption().declaresSet(0u) && provider->consumption().bindings().size() == 2u);
    const auto charge = core::inspectCpuAllocationContaining(provider.get());
    assert(charge.found && charge.quota == quota.get());
    std::weak_ptr<MetalRhiNativePipelinePayload> native = provider->native();
    const auto key = provider->native()->pipelineCacheKey;
    layout.reset();
    state.colorFormat = TextureFormat::Unknown;
    assert(provider->native()->colorFormats[0] == TextureFormat::Rgba8Unorm
        && provider->native()->pipelineCacheKey == key);
    auto depth = MetalRhiOrdinaryGraphicsProvider::create(
        vertex, {}, authored(true), contract, budget, quota, diagnostic);
    if (!depth)
        std::fprintf(stderr, "Depth provider: %s\n", diagnostic.c_str());
    assert(depth);
    assert(depth->native()->nativeRuntimeBacked && depth->native()->renderPipelinePayloadPresent);
    assert(depth->native()->preparedGraphicsStages == ShaderStage::Vertex && !depth->stages()[1]
        && !depth->artifacts()[1]);
    assert(!depth->native()->fragmentFunction && !depth->native()->fragmentShaderLibrary);
    assert(depth->native()->graphicsBufferFacts[0].stage == ShaderStage::Vertex
        && depth->native()->graphicsBufferFacts[0].version == 1u
        && depth->native()->graphicsBufferFacts[0].entries.empty());
    assert(depth->native()->graphicsBufferFacts[1].stage == ShaderStage::None
        && depth->native()->graphicsBufferFacts[1].version == 0u
        && depth->native()->graphicsBufferFacts[1].entries.empty()
        && depth->native()->graphicsBufferFacts[1].entries.capacity() == 0u);
    emptyLayout.reset();
    vertex.reset();
    fragment.reset();
    shadow.reset();
    assert(!provider->artifacts()[0]->bytes(shader::ShaderArtifactRole::Metal).empty());
    provider.reset();
    assert(native.expired());
    native.reset();
    depth.reset();
    assert(!quota->liveBytes());
    std::puts(
        "ordinary graphics provider: real separate VS/FS and depth-only pipelines, same-quota sealed bytes, core allocator, ownership independence and fail-closed cleanup pass; no Graphics execution gate");
}
