#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/bindless_ordinary_opaque_binding.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_domain.h"
#include "rhi/semantics/bindless_ordinary_opaque_lowering.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
// Exercises the real public Root query over the existing PipelineImages
// original Sources. This is metadata/custody validation, never GPU proof.
struct OriginalOrdinaryOpaqueBindingFixture {
    bool graphics = false;
    mutable std::weak_ptr<const rhi::OriginalCommandBindlessRootSource> observedRoot;

    template <class Root, class Sources>
    void operator()(const Root& root, const Sources& sources, rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using Source = OriginalCommandBindlessRootSource;
        using Lowering = semantics::OriginalBindlessOrdinaryOpaqueLowering;
        const auto entry = graphics ? "fragmentMain" : "computeMain";
        const auto wrongEntry = graphics ? "computeMain" : "fragmentMain";
        const auto stage = graphics ? ShaderStage::Fragment : ShaderStage::Compute;
        if (!device) {
            // The builder has dropped caller aliases before this callback.
            // Borrow the genuine Root still held by its original usage graph.
            const auto original = observedRoot.lock();
            assert(original);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{original->originalMetadataQuota()}, [&]() noexcept -> const char* {
                    for (std::uint32_t binding = 0u; binding < 7u; ++binding) {
                        BindlessOrdinaryOpaqueBinding facts;
                        facts.contractVersion = 99u;
                        facts.sourceIndex = 0u;
                        const auto* id = Source::originalOrdinaryOpaqueBinding(
                            original, entry, 2u, binding, facts);
                        assert(id && core::StringView(id) == "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                            && !facts.contractVersion && facts.sourceIndex == SIZE_MAX
                            && !facts.resource.isValid() && !facts.access.resource.isValid());
                    }
                    return nullptr;
                }));
            observedRoot.reset();
            return;
        }
        observedRoot = root;
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision = Lowering::lowering(backend);
            assert(decision.inspectionAvailable && !decision.executionAvailable
                && decision.validatorOnly == (backend == Backend::Null) && !decision.diagnostic);
            assert(Lowering::lowering(backend, 2u).diagnostic);
            using Color = semantics::OriginalBindlessOrdinaryColorDomain;
            const auto color = Color::lowering(backend);
            assert(color.inspectionAvailable && color.validatorOnly == (backend == Backend::Null)
                && color.nativeClosureAvailable
                    == (backend == Backend::Vulkan || backend == Backend::Metal));
            assert(
                Color::lowering(backend, 0u).diagnostic && Color::lowering(backend, 2u).diagnostic);
        }
        {
            using Color = semantics::OriginalBindlessOrdinaryColorDomain;
            TextureDesc texture;
            texture.format = TextureFormat::Rgba8Unorm;
            texture.usage = TextureUsage::CopyDst | TextureUsage::Sampled;
            texture.extent = {2u, 2u, 1u};
            texture.mipLevelCount = 2u;
            texture.arrayLayerCount = 1u;
            TextureViewDesc view;
            view.format = texture.format;
            view.aspect = TextureAspect::Color;
            view.usage = TextureUsage::Sampled;
            view.mipLevelCount = 2u;
            view.arrayLayerCount = 1u;
            view.dimension = TextureViewDimension::Texture2D;
            assert(!Color::completedViewDiagnostic(texture, view, 3u));
            assert(Color::completedViewDiagnostic(texture, view, 1u));
            auto wrongView = view;
            wrongView.baseMipLevel = 1u;
            assert(Color::completedViewDiagnostic(texture, wrongView, 3u));
            wrongView = view;
            wrongView.mipLevelCount = 1u;
            assert(Color::completedViewDiagnostic(texture, wrongView, 3u));
            wrongView = view;
            wrongView.aspect = TextureAspect::Depth;
            assert(Color::completedViewDiagnostic(texture, wrongView, 3u));
            wrongView = view;
            wrongView.usage = TextureUsage::Storage;
            assert(Color::completedViewDiagnostic(texture, wrongView, 3u));
            auto wrongTexture = texture;
            wrongView = view;
            wrongTexture.format = wrongView.format = TextureFormat::Rgba16Float;
            assert(Color::completedViewDiagnostic(wrongTexture, wrongView, 3u));
            assert(!Color::completedViewDiagnostic(wrongTexture, wrongView, 3u, 2u));
            for (const auto version : {1u, 3u, UINT32_MAX})
                assert(Color::completedViewDiagnostic(wrongTexture, wrongView, 3u, version));
            assert(Color::completedViewDiagnostic(wrongTexture, wrongView, 1u, 2u));
            wrongView.baseMipLevel = 1u;
            assert(Color::completedViewDiagnostic(wrongTexture, wrongView, 3u, 2u));
            wrongView.baseMipLevel = 0u;
            wrongTexture.format = wrongView.format = TextureFormat::R32Float;
            assert(Color::completedViewDiagnostic(wrongTexture, wrongView, 3u, 2u));
            wrongTexture = texture;
            wrongTexture.usage = TextureUsage::CopyDst;
            assert(Color::completedViewDiagnostic(wrongTexture, view, 3u));
            wrongTexture = texture;
            wrongTexture.sampleCount = 4u;
            assert(Color::completedViewDiagnostic(wrongTexture, view, 3u));
            texture.arrayLayerCount = view.arrayLayerCount = 2u;
            assert(Color::completedViewDiagnostic(texture, view, 3u));
            view.dimension = TextureViewDimension::Texture2DArray;
            assert(!Color::completedViewDiagnostic(texture, view, 3u));
            texture.arrayLayerCount = view.arrayLayerCount = 6u;
            texture.cubeCompatible = true;
            assert(Color::completedViewDiagnostic(texture, view, 3u));
            view.dimension = TextureViewDimension::TextureCube;
            assert(!Color::completedViewDiagnostic(texture, view, 3u));
            texture.format = view.format = TextureFormat::Rgba16Float;
            assert(!Color::completedViewDiagnostic(texture, view, 3u, 2u));
            assert(Color::completedViewDiagnostic(texture, view, 3u));
            texture.extent.height = 1u;
            assert(Color::completedViewDiagnostic(texture, view, 3u, 2u));
        }
        const auto quota = root->originalMetadataQuota();
        // The Root and canonical inventory share this metadata quota. Pressure
        // its real owner once, rather than charging two aliases of one budget.
        assert(core::SharedOwnerProjection::sameOwner(quota, sources->originalMetadataQuota()));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                // Real selected-entry declarations, queried from the same
                // genuine Root. Pair inspection does not prove Source6 depth
                // ownership or authorize shader sampling/completion.
                BindlessOrdinaryOpaqueBinding depth, comparison;
                assert(!Source::originalOrdinaryOpaqueBinding(root, entry, 2u, 3u, depth));
                assert(!Source::originalOrdinaryOpaqueBinding(root, entry, 2u, 4u, comparison));
                assert(!semantics::OriginalBindlessOrdinaryDepthDomain::inspectSelectedDeclarations(
                    depth.declaration, comparison.declaration, depth.selectedStage));
                BindlessOrdinaryOpaqueBinding facts;
                const auto cleared = [&]() {
                    return !facts.contractVersion && facts.sourceIndex == SIZE_MAX
                        && facts.selectedStage == ShaderStage::None && !facts.resource.isValid()
                        && !facts.parentResource.isValid() && !facts.registrationGeneration
                        && !facts.parentRegistrationGeneration && !facts.access.resource.isValid();
                };
                const auto reject = [&](const auto& candidate,
                                        const char* entry,
                                        std::uint32_t set,
                                        std::uint32_t binding,
                                        std::uint32_t version = 1u) {
                    facts.contractVersion = 99u;
                    facts.sourceIndex = 0u;
                    assert(Source::originalOrdinaryOpaqueBinding(
                        candidate, entry, set, binding, facts, version));
                    assert(cleared());
                };
                reject(root, entry, 2u, 0u, 0u);
                reject(root, entry, 2u, 0u, 2u);
                reject(root, wrongEntry, 2u, 0u);
                reject(root, "missingMain", 2u, 0u);
                reject(root, entry, 1u, 3u);
                reject(root, entry, 0u, 3u);
                reject(root, entry, UINT32_MAX, 0u);
                reject(root, entry, 2u, UINT32_MAX);
                auto forged = std::shared_ptr<const Source>(quota, root.get());
                reject(forged, entry, 2u, 0u);
                std::uint32_t images = 0u, samplers = 0u;
                for (std::uint32_t binding = 0u; binding < 7u; ++binding) {
                    const auto* id =
                        Source::originalOrdinaryOpaqueBinding(root, entry, 2u, binding, facts);
                    if (id)
                        std::fprintf(stderr, "Ordinary opaque Root: %s\n", id);
                    assert(!id && facts.contractVersion == 1u && facts.set == 2u
                        && facts.declaration.binding == binding
                        && facts.declaration.arrayCount == 1u && facts.selectedStage == stage
                        && facts.sourceIndex < sources->sources().size() && facts.resource.isValid()
                        && facts.registrationGeneration);
                    const auto& original = sources->sources()[facts.sourceIndex];
                    if (facts.declaration.type == DescriptorType::Sampler) {
                        ++samplers;
                        const auto& source =
                            std::get<std::shared_ptr<const OriginalCommandSamplerLease>>(original);
                        assert(facts.resource == source->resource()
                            && facts.sampler.matches(source->originalObject()->desc())
                            && !facts.parentResource.isValid()
                            && !facts.parentRegistrationGeneration
                            && !facts.access.resource.isValid());
                        assert(facts.declaration.comparisonSampler == (binding == 4u));
                    } else {
                        ++images;
                        const auto& source =
                            std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                original);
                        assert(facts.declaration.type == DescriptorType::SampledTexture
                            && facts.resource == source->resource()
                            && facts.parentResource == source->originalParentLease()->resource()
                            && facts.parentRegistrationGeneration
                            && facts.view.matches(source->originalObject()->desc())
                            && facts.access.kind == BindlessAccessKind::Texture
                            && facts.access.resource == facts.parentResource
                            && facts.access.state == ResourceState::ShaderRead
                            && facts.access.textureRange.baseMipLevel == facts.view.baseMipLevel
                            && facts.access.textureRange.mipLevelCount == facts.view.mipLevelCount
                            && facts.access.textureRange.baseArrayLayer == facts.view.baseArrayLayer
                            && facts.access.textureRange.arrayLayerCount
                                == facts.view.arrayLayerCount
                            && facts.access.textureRange.aspect == facts.view.aspect);
                        assert(facts.declaration.depthTexture == (binding == 3u));
                    }
                    using Color = semantics::OriginalBindlessOrdinaryColorDomain;
                    const bool color = binding != 3u && binding != 4u && binding != 6u;
                    assert(Color::acceptsBinding(facts.declaration) == color);
                    if (color) {
                        for (const auto stages : {ShaderStage::Vertex,
                                 ShaderStage::Fragment,
                                 ShaderStage::Compute,
                                 ShaderStage::Vertex | ShaderStage::Fragment}) {
                            auto declaration = facts.declaration;
                            declaration.stages = stages;
                            assert(Color::acceptsBinding(declaration));
                            assert(Color::acceptsGraphicsBinding(declaration)
                                == (stages != ShaderStage::Compute));
                        }
                        auto invalid = facts.declaration;
                        invalid.stages = ShaderStage::None;
                        assert(!Color::acceptsBinding(invalid));
                        invalid = facts.declaration;
                        invalid.arrayCount = 2u;
                        assert(!Color::acceptsBinding(invalid));
                        invalid = facts.declaration;
                        invalid.dynamicOffset = true;
                        assert(!Color::acceptsBinding(invalid));
                    }
                    // Failure after a positive query cannot leak old identity/access facts.
                    reject(root, wrongEntry, 2u, binding);
                    if (graphics) {
                        // The preceding refusal cleared the output, so obtain
                        // the genuine selected Fragment facts again.
                        assert(!Source::originalOrdinaryOpaqueBinding(
                            root, entry, 2u, binding, facts));
                        const auto fragmentFacts = facts;
                        facts.contractVersion = 99u;
                        id = Source::originalOrdinaryOpaqueBinding(
                            root, "vertexMain", 2u, binding, facts);
                        if (binding == 0u || binding == 2u) {
                            assert(!id && facts.selectedStage == ShaderStage::Vertex
                                && facts.resource == fragmentFacts.resource
                                && facts.parentResource == fragmentFacts.parentResource
                                && facts.sourceIndex == fragmentFacts.sourceIndex
                                && facts.registrationGeneration
                                    == fragmentFacts.registrationGeneration
                                && facts.parentRegistrationGeneration
                                    == fragmentFacts.parentRegistrationGeneration);
                            if (binding == 0u)
                                assert(facts.access.resource == fragmentFacts.access.resource
                                    && facts.access.textureRange
                                        == fragmentFacts.access.textureRange);
                        } else
                            assert(id
                                && core::StringView(id) == "RHI-BINDLESS-ORDINARY-OPAQUE-INACTIVE"
                                && cleared());
                    }
                }
                assert(images == 5u && samplers == 2u);
                return nullptr;
            }));
        if (device)
            std::puts(graphics
                    ? "BINDLESS_ORDINARY_OPAQUE_GRAPHICS_ROOT1_OK images5 samplers2 selected-fragment vertex-cube-sampler inactive-vertex generation-range hot0 execution-closed"
                    : "BINDLESS_ORDINARY_OPAQUE_ROOT1_OK images5 samplers2 Cube-2D-Array-3D-depth-comparison selected-original generation-range hot0 execution-closed");
    }
};
} // namespace arp::test
