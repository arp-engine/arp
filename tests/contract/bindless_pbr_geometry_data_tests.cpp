#include "../fixtures/bindless_scene_geometry_readback_fixture.h"
#include "core/cpu_allocator.h"
#include "rhi/frontend/bindless_pbr_geometry.h"
#include "rhi/frontend/bindless_scene_content.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "rhi/semantics/bindless_scene_raster.h"

#include <cassert>
#include <limits>

#ifdef NDEBUG
#error PBR geometry tests require assertions
#endif

int main()
{
    using namespace arp;
    using C = rhi::BindlessPbrGeometryContract;
    {
        using Oracle = test::BindlessSceneGeometryReadbackFixture;
        const auto bits = [](float value) {
            std::uint32_t word;
            std::memcpy(&word, &value, sizeof(word));
            return word;
        };
        for (const bool terrain : {false, true}) {
            Oracle::Words words{};
            for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
                const auto first = pixel * Oracle::lanes;
                const auto x = pixel & 1u ? 0.5f : -0.5f;
                const auto y = pixel & 2u ? 0.5f : -0.5f;
                words[first] = bits(x);
                words[first + 1u] = bits(y);
                words[first + 2u] = bits(0.5f);
                for (const auto clip :
                    terrain ? std::array{11u, 15u, 19u} : std::array{6u, 10u, 14u}) {
                    words[first + clip] = bits(x);
                    words[first + clip + 1u] = bits(y);
                    words[first + clip + 2u] = bits(0.5f);
                    words[first + clip + 3u] = bits(1.f);
                }
            }
            assert(!Oracle::inspect(words.data(), sizeof(words), terrain));
            assert(Oracle::inspect(words.data(), sizeof(words) - 1u, terrain));
            assert(Oracle::inspect(nullptr, sizeof(words), terrain));
            auto corrupt = words;
            corrupt[2u] = bits(0.25f);
            assert(core::StringView(Oracle::inspect(corrupt.data(), sizeof(corrupt), terrain))
                == "SCENE-GEOMETRY-READBACK-WORLD");
            corrupt = words;
            corrupt[terrain ? 16u : 11u] = bits(0.25f);
            assert(core::StringView(Oracle::inspect(corrupt.data(), sizeof(corrupt), terrain))
                == "SCENE-GEOMETRY-READBACK-TEMPORAL-CLIP");
            corrupt = words;
            std::copy_n(words.begin(), Oracle::lanes, corrupt.begin() + Oracle::lanes);
            assert(core::StringView(Oracle::inspect(corrupt.data(), sizeof(corrupt), terrain))
                == "SCENE-GEOMETRY-READBACK-COVERAGE");
            corrupt = words;
            corrupt[3u] = bits(std::numeric_limits<float>::quiet_NaN());
            assert(core::StringView(Oracle::inspect(corrupt.data(), sizeof(corrupt), terrain))
                == "SCENE-GEOMETRY-READBACK-NONFINITE");
            if (!terrain) {
                corrupt = words;
                corrupt[18u] = 1u;
                assert(core::StringView(Oracle::inspect(corrupt.data(), sizeof(corrupt), false))
                    == "SCENE-GEOMETRY-READBACK-METADATA");
            }
        }
    }
    {
        using Binding = rhi::OriginalCommandBufferReadbackBinding;
        assert(!Binding::validateBudget({6u, true, 5u, 128u, 65536u}));
        assert(!Binding::validateProfile(6u, 5u, 1u, 7u, 1u, true));
        assert(!Binding::validateProfile(5u, 6u, 1u, 8u, 1u, true));
        // Direct and produced inputs cannot borrow each other's readback profile.
        assert(Binding::validateProfile(6u, 6u, 1u, 8u, 1u, true));
        assert(Binding::validateProfile(5u, 5u, 1u, 7u, 1u, true));
        for (const auto roots : {0u, 4u, 6u, 7u})
            assert(Binding::validateProfile(6u, roots, 1u, 7u, 1u, true));
        for (const auto inputs : {0u, 6u, 8u})
            assert(Binding::validateProfile(6u, 5u, 1u, inputs, 1u, true));
        assert(Binding::validateProfile(6u, 5u, 2u, 7u, 1u, true));
        assert(Binding::validateProfile(6u, 5u, 1u, 7u, 0u, true));
        assert(Binding::validateProfile(6u, 5u, 1u, 7u, 1u, false));
        assert(Binding::validateBudget({7u, true, 5u, 128u, 65536u}));
        rhi::OriginalBindlessRootStateBudget direct{5u, true, 4u, 64u, 8192u, 1u, 1u};
        direct.directResultCopies = {1u, true, 1u, 8192u};
        assert(!direct.diagnostic());
        for (const auto version : {1u, 2u, 3u, 4u, 6u}) {
            auto invalid = direct;
            invalid.contractVersion = version;
            assert(invalid.diagnostic());
        }
        auto invalid = direct;
        invalid.directResultCopies.maxCopies = 0u;
        assert(invalid.diagnostic() && !direct.matches(invalid));
        invalid = direct;
        invalid.directResultCopies.maxDependencyVisits = 0u;
        assert(invalid.diagnostic() && !direct.matches(invalid));
        invalid = direct;
        invalid.directResultCopies.prototypeOptIn = false;
        assert(invalid.diagnostic() && !direct.matches(invalid));
        invalid = direct;
        invalid.directResultCopies.contractVersion = 2u;
        assert(invalid.diagnostic() && !direct.matches(invalid));
        invalid = {};
        invalid.directResultCopies = direct.directResultCopies;
        assert(!invalid.empty());
        for (const auto backend : {rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU,
                 rhi::Backend::WebGL,
                 static_cast<rhi::Backend>(255u)}) {
            const auto decision =
                rhi::semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 6u);
            const bool supported = backend == rhi::Backend::Null || backend == rhi::Backend::Vulkan
                || backend == rhi::Backend::Metal || backend == rhi::Backend::WebGPU;
            assert(decision.cpuMetadataImplemented == supported
                && decision.pendingAssociationImplemented == supported
                && decision.validatorOnly == (backend == rhi::Backend::Null)
                && !decision.nativeExecutionImplemented && bool(decision.diagnostic) != supported);
            const auto copy =
                rhi::semantics::OriginalBindlessDirectResultCopyLowering::lowering(backend, 1u);
            assert(copy.projectionAvailable == supported
                && copy.validatorOnly == (backend == rhi::Backend::Null) && !copy.executionAvailable
                && bool(copy.diagnostic) != supported);
            assert(rhi::semantics::OriginalBindlessDirectResultCopyLowering::lowering(backend, 2u)
                    .diagnostic);
        }
    }
    {
        using namespace rhi;
        using Scene = OriginalBindlessSceneContentContract;
        const OriginalBindlessSceneContentInputs scene{
            1u, true, Scene::Family::Mesh, 128u, 32u, 64u};
        OriginalBindlessRootNativeClosureBudget native{3u, true, 16u, 32u, 64u, 65536u};
        native.maxInitializedGeometryActors = 2u;
        native.maxInitializedGeometryBytes = 72u;
        native.initializedShader = {1u, true, 4u, 4096u, 128u};
        assert(!OriginalBindlessRootNativeClosureContract::validateBudget(native));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            assert(!Scene::validateExecutionBudget(backend, {}, {}));
            assert(bool(Scene::validateExecutionBudget(backend, scene, native))
                == (backend != Backend::Vulkan && backend != Backend::Metal));
        }
        auto emulated = native;
        emulated.contractVersion = 4u;
        emulated.initializedGeometryContractVersion = 1u;
        emulated.initializedGeometryOptIn = true;
        emulated.maxAttachmentViews = 2u;
        emulated.producedAttachments = {1u, true};
        assert(!Scene::validateExecutionBudget(Backend::WebGPU, scene, emulated));
        assert(Scene::validateExecutionBudget(Backend::Vulkan, scene, emulated));
        assert(Scene::validateExecutionBudget(Backend::Metal, scene, emulated));
        auto invalid = emulated;
        invalid.producedAttachments = {};
        assert(Scene::validateExecutionBudget(Backend::WebGPU, scene, invalid));
        invalid = emulated;
        invalid.initializedShader = {};
        assert(Scene::validateExecutionBudget(Backend::WebGPU, scene, invalid));
        invalid = emulated;
        invalid.initializedGeometryContractVersion = 0u;
        invalid.initializedGeometryOptIn = false;
        invalid.maxInitializedGeometryActors = 0u;
        invalid.maxInitializedGeometryBytes = 0u;
        assert(Scene::validateExecutionBudget(Backend::WebGPU, scene, invalid));
        auto unauthorized = scene;
        unauthorized.optIn = false;
        assert(Scene::validateExecutionBudget(Backend::WebGPU, unauthorized, emulated));
    }
    {
        using namespace rhi;
        using Image = OriginalBindlessProducedImageReadContract;
        using Descriptor = OriginalTextureCellDescriptorContract;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Image::validateIntent({}));
        const Image::Intent intent{1u, true, 1u, 4u, 8u};
        assert(!Image::validateIntent(intent));
        auto wrongIntent = intent;
        wrongIntent.optIn = false;
        assert(Image::validateIntent(wrongIntent));
        wrongIntent = intent;
        wrongIntent.maxControls = 5u;
        assert(Image::validateIntent(wrongIntent));
        TextureDesc texture;
        texture.extent = {8u, 4u, 1u};
        texture.format = TextureFormat::Rgba16Float;
        texture.mipLevelCount = 4u;
        texture.usage =
            TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::RenderAttachment;
        TextureViewDesc view;
        view.format = texture.format;
        view.usage = TextureUsage::Sampled;
        view.mipLevelCount = texture.mipLevelCount;
        assert(!Image::inspectView(texture, view));
        auto wrongView = view;
        --wrongView.mipLevelCount;
        assert(Image::inspectView(texture, wrongView));
        wrongView = view;
        wrongView.baseMipLevel = 1u;
        assert(Image::inspectView(texture, wrongView));
        wrongView = view;
        wrongView.usage = TextureUsage::Storage;
        assert(Image::inspectView(texture, wrongView));
        auto wrongTexture = texture;
        wrongTexture.format = TextureFormat::Rgba8Unorm;
        assert(Image::inspectView(wrongTexture, view));
        wrongTexture = texture;
        wrongTexture.sampleCount = 4u;
        assert(Image::inspectView(wrongTexture, view));
        assert(!Image::inspectAccess(ResourceState::ShaderRead,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead,
            CommandUsageKind::Dispatch));
        assert(Image::inspectAccess(ResourceState::ShaderRead,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderRead,
            CommandUsageKind::Draw));
        assert(Image::inspectAccess(ResourceState::ShaderWrite,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderWrite,
            CommandUsageKind::Dispatch));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto mapping = Image::lowering(backend, 1u);
            assert(mapping.preparationAvailable == (backend != Backend::WebGL)
                && mapping.validatorOnly == (backend == Backend::Null)
                && !mapping.executionAvailable);
            assert(!Image::lowering(backend, 2u).preparationAvailable);
            for (const auto version : {3u, 4u}) {
                const auto descriptor = Descriptor::lowering(backend, version);
                assert(descriptor.publicationAvailable
                    == (version == 3u ? backend == Backend::Vulkan || backend == Backend::Metal
                                      : backend == Backend::WebGPU));
                assert(!descriptor.shaderExecutionAvailable);
            }
        }
        DescriptorBindingDesc binding;
        binding.binding = 2u;
        binding.type = DescriptorType::SampledTexture;
        binding.stages = ShaderStage::Compute;
        DescriptorBindingWrite write;
        write.binding = binding.binding;
        write.type = binding.type;
        write.resource = {1u};
        write.resourceKind = DescriptorResourceKind::TextureView;
        write.resourceState = ResourceState::ShaderRead;
        assert(!Descriptor::validateBinding(binding, write, 3u));
        assert(!Descriptor::validateBinding(binding, write, 4u));
        binding.type = write.type = DescriptorType::StorageTexture;
        write.resourceState = ResourceState::ShaderWrite;
        binding.storageTextureFormat = TextureFormat::Rgba16Float;
        binding.storageTextureAccess = StorageTextureAccess::WriteOnly;
        assert(Descriptor::validateBinding(binding, write, 3u));
        assert(Descriptor::validateBinding(binding, write, 4u));
        assert(!Descriptor::validateBinding(binding, write, 1u));
        OriginalFiniteGraphicsContentPlanBudget content{9u, true, 0u, 1u, 1u, 0u, 2u, 8u, 64u};
        content.bindlessGeometryContractVersion = 2u;
        content.bindlessGeometryOptIn = true;
        content.producedImageRead = intent;
        OriginalFiniteGraphicsContentPlanContract::Counts counts{0u, 1u, 1u, 0u, 0u, 2u, 8u, 64u};
        counts.producedImageControls = 1u;
        counts.producedImageCells = 4u;
        counts.producedImageSteps = 8u;
        assert(!OriginalBindlessGraphicsContentPlanContract::validateCounts(content, counts));
        --content.producedImageRead.maxCells;
        assert(OriginalBindlessGraphicsContentPlanContract::validateCounts(content, counts));
        content.producedImageRead = intent;
        content.bindlessGeometryContractVersion = 1u;
        assert(OriginalBindlessGraphicsContentPlanContract::validateCounts(content, counts));
        content.bindlessGeometryContractVersion = 2u;
        content.producedImageRead = {};
        assert(OriginalBindlessGraphicsContentPlanContract::validateCounts(content, counts));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    {
        using namespace rhi;
        using Scope = OriginalBindlessSnapshotScopeContract;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Scope::validateIntent({}));
        Scope::Intent scope{1u, true, 2u, 32u};
        assert(!Scope::validateIntent(scope));
        auto invalid = scope;
        invalid.contractVersion = 2u;
        assert(Scope::validateIntent(invalid));
        invalid = scope;
        invalid.optIn = false;
        assert(Scope::validateIntent(invalid));
        invalid = scope;
        invalid.maxVisits = 0u;
        assert(Scope::validateIntent(invalid));
        invalid = scope;
        invalid.maxSnapshots = 0u;
        assert(Scope::validateIntent(invalid));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto decision = Scope::lowering(backend, 1u);
            assert(decision.preparationAvailable == (backend != Backend::WebGL)
                && decision.validatorOnly == (backend == Backend::Null)
                && !decision.executionAvailable);
            assert(!Scope::lowering(backend, 2u).preparationAvailable);
        }
        OriginalFiniteGraphicsContentPlanBudget content{9u, true, 0u, 1u, 1u, 0u, 2u, 8u, 64u};
        content.bindlessGeometryContractVersion = 2u;
        content.bindlessGeometryOptIn = true;
        content.snapshotScope = scope;
        OriginalFiniteGraphicsContentPlanContract::Counts counts{0u, 1u, 1u, 0u, 0u, 2u, 8u, 64u};
        assert(!OriginalBindlessGraphicsContentPlanContract::validateCounts(content, counts));
        content.bindlessGeometryContractVersion = 1u;
        assert(OriginalBindlessGraphicsContentPlanContract::validateCounts(content, counts));
        content.bindlessGeometryContractVersion = 2u;
        content.contractVersion = 6u;
        assert(OriginalBindlessGraphicsContentPlanContract::validateCounts(content, counts));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    {
        using Raster = rhi::semantics::BindlessSceneRasterContract;
        rhi::RenderPipelineDesc state;
        state.colorFormat = rhi::TextureFormat::Rgba16Float;
        state.depthFormat = rhi::TextureFormat::D32Float;
        state.depthStencil.depthTestEnabled = true;
        state.depthStencil.depthWriteEnabled = true;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto mapping = Raster::lowering(backend);
            assert(mapping.preparationAvailable && !mapping.executionAvailable
                && mapping.validatorOnly == (backend == rhi::Backend::Null));
            assert(!Raster::inspect(backend, state));
            state.colorFormat = rhi::TextureFormat::Unknown;
            assert(!Raster::inspect(backend, state));
            state.depthStencil.depthBiasEnabled = true;
            state.depthStencil.depthBiasConstantFactor = 1.f;
            assert(!Raster::inspect(backend, state));
            state.depthStencil.depthBiasConstantFactor = 0.5f;
            assert(bool(Raster::inspect(backend, state)) == (backend == rhi::Backend::WebGPU));
            state.depthStencil.depthBiasEnabled = false;
            state.colorFormat = rhi::TextureFormat::Rgba16Float;
            state.multisample.sampleCount = 4u;
            assert(Raster::inspect(backend, state));
            state.multisample.sampleCount = 1u;
            assert(Raster::inspect(backend, state, 2u));
        }
        assert(Raster::inspect(rhi::Backend::WebGL, state));
        state.depthStencil.depthBiasEnabled = true;
        state.depthStencil.depthBiasConstantFactor = std::numeric_limits<float>::infinity();
        assert(Raster::inspect(rhi::Backend::WebGPU, state));
        state.depthStencil.depthBiasConstantFactor = float(INT32_MAX);
        assert(Raster::inspect(rhi::Backend::WebGPU, state));
        state.depthStencil.depthBiasConstantFactor = float(INT32_MIN);
        assert(!Raster::inspect(rhi::Backend::WebGPU, state));
        state.depthStencil.depthBiasSlopeFactor = std::numeric_limits<float>::quiet_NaN();
        assert(Raster::inspect(rhi::Backend::WebGPU, state));
    }
    rhi::BindlessPbrGeometryFrame400 frame;
    frame.outputExtent = {2u, 3u, C::numericLanesPerPixel, C::version};
    constexpr auto resultBytes = 2u * 3u * 23u * sizeof(std::uint32_t);
    const auto allocationCount = core::defaultCpuAllocator().stats().allocationCount;
    assert(!C::validateFrame(frame, 2u, 3u, resultBytes));
    assert(C::validateFrame(frame, 2u, 3u, resultBytes - 1u));
    assert(C::validateFrame(frame, 3u, 2u, resultBytes));
    assert(C::validateFrame(frame, 0u, 3u, resultBytes));
    for (auto* matrix : {&frame.renderViewProjection,
             &frame.currentViewProjection,
             &frame.previousViewProjection,
             &frame.shadowViewProjection}) {
        for (auto& lane : *matrix) {
            lane = std::numeric_limits<float>::quiet_NaN();
            assert(C::validateFrame(frame, 2u, 3u, resultBytes));
            lane = 0.f;
        }
    }
    for (auto* wind : {&frame.currentWind, &frame.previousWind})
        for (auto* vector : {&wind->directionSpeed,
                 &wind->timeGustTurb,
                 &wind->flutterParams,
                 &wind->interactionCenter})
            for (auto& lane : *vector) {
                lane = std::numeric_limits<float>::infinity();
                assert(C::validateFrame(frame, 2u, 3u, resultBytes));
                lane = 0.f;
            }
    auto large = frame;
    large.outputExtent = {UINT32_MAX, 2u, 23u, 1u};
    assert(C::validateFrame(large, UINT32_MAX, 2u, UINT64_MAX));
    large.outputExtent = {UINT32_MAX, 1u, 23u, 1u};
    assert(C::validateFrame(large, UINT32_MAX, 1u, UINT64_MAX));
    rhi::BindlessPbrGeometryDraw16 draw{1u, 2u, 0u, 0u};
    assert(!C::validateDraw(draw, 2u, 8u, 3u, 3u, true));
    assert(C::validateDraw(draw, 1u, 8u, 3u, 3u, true));
    assert(C::validateDraw(draw, 2u, 8u, 3u, 4u, true));
    assert(C::validateDraw(draw, 2u, 8u, UINT32_MAX, 1u, true));
    assert(C::validateDraw(draw, 2u, 8u, 3u, 0u, true));
    assert(C::validateDraw(draw, 2u, 8u, 0u, 1u, false));
    draw.instanceBase = 0u;
    assert(!C::validateDraw(draw, 2u, 0u, 0u, 1u, false));
    assert(C::validateDraw(draw, 2u, 0u, 1u, 1u, false));
    draw.reserved1 = 1u;
    assert(C::validateDraw(draw, 2u, 0u, 0u, 1u, false));
    rhi::BindlessPbrTemporalPalette32864 palettes;
    for (auto* palette : {&palettes.current, &palettes.previous}) {
        palette->jointCount = 256u;
        palette->worldTranslationAndScale[3u] = 1.f;
        palette->worldRotation[3u] = 1.f;
    }
    assert(!C::validatePalette(palettes));
    for (auto* palette : {&palettes.current, &palettes.previous}) {
        palette->jointCount = 257u;
        assert(C::validatePalette(palettes));
        palette->jointCount = 256u;
        palette->reserved2 = 1u;
        assert(C::validatePalette(palettes));
        palette->reserved2 = 0u;
        palette->matrices.back()[15u] = std::numeric_limits<float>::infinity();
        assert(C::validatePalette(palettes));
        palette->matrices.back()[15u] = 0.f;
        palette->worldRotation[3u] = 2.f;
        assert(C::validatePalette(palettes));
        palette->worldRotation[3u] = 1.f;
        palette->worldTranslationAndScale[3u] = 0.f;
        assert(C::validatePalette(palettes));
        palette->worldTranslationAndScale[3u] = 1.f;
    }
    rhi::OriginalBindlessSceneContentInputs scene;
    assert(scene.empty() && !scene.diagnostic());
    scene.contractVersion = 1u;
    assert(scene.diagnostic());
    scene.optIn = true;
    scene.maxDescriptorVisits = scene.maxRecordVisits = scene.maxCommandVisits = 1u;
    assert(!scene.diagnostic());
    scene.family = static_cast<decltype(scene.family)>(5u);
    assert(scene.diagnostic());
    using Selection = rhi::OriginalBindlessSceneContentContract;
    const std::array<core::StringView, 15u> vertices{"meshColorVertex",
        "meshDepthVertex",
        "meshShadowVertex",
        "skinColorVertex",
        "skinDepthVertex",
        "skinShadowVertex",
        "grassColorVertex",
        "grassDepthVertex",
        "grassShadowVertex",
        "canopyColorVertex",
        "canopyDepthVertex",
        "canopyShadowVertex",
        "terrainColorVertex",
        "terrainDepthVertex",
        "terrainShadowVertex"};
    const std::array<core::StringView, 6u> fragments{"familyColorMain",
        "familyDepthMain",
        "familyShadowMain",
        "terrainColorMain",
        "terrainDepthMain",
        "terrainShadowMain"};
    for (std::size_t i = 0u; i < vertices.size(); ++i) {
        for (std::size_t j = 0u; j < fragments.size(); ++j) {
            const auto selected = Selection::selectEntries(vertices[i], fragments[j]);
            const auto expected = (i >= 12u ? 3u : 0u) + i % 3u;
            assert(selected.recognized && std::uint32_t(selected.family) == i / 3u
                && selected.pass == i % 3u && bool(selected.diagnostic) == (j != expected));
        }
        assert(Selection::selectEntries(vertices[i], "unrelatedFragment").diagnostic);
    }
    assert(!Selection::selectEntries("vertexMain", "fragmentMain").recognized
        && !Selection::selectEntries("vertexMain", "fragmentMain").diagnostic);
    for (const auto fragment : fragments)
        assert(Selection::selectEntries("unrelatedVertex", fragment).diagnostic);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocationCount);
}
