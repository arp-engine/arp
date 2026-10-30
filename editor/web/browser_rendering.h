#pragma once

#include "editor/viewport/editor_scene_viewport_environment.h"
#include "editor/viewport/editor_scene_viewport_pipeline_contract.h"
#include "editor/viewport/editor_scene_viewport_sprite_contract.h"
#include "platform/render_system.h"
#include "platform/render_system_ordered_profile.h"
#include "shader/shader_reflection_cache.h"

namespace arp::editor {
// Explicit Prototype budgets include the 2048x3072 atlas and a complete editor
// frame. They describe logical/CPU capacity, not physical driver residency.
class BrowserRendering final {
public:
    static constexpr std::uint32_t maxCommands = 16384;
    static constexpr std::uint64_t MiB = 1024 * 1024;
    static constexpr std::uint32_t maxViewportExtent = 4096;
    static constexpr std::uint32_t maxSceneObjects = 4096;
    static constexpr std::uint32_t maxSceneDraws = 1024;

    static platform::RenderSystem create()
    {
        platform::RenderSystemOrderedProfile profile;
        profile.contractVersion = 3;
        profile.fragmentDepthOptIn = true;
        profile.sampledDepthOptIn = true;
        profile.srgbTexturesOptIn = true;
        profile.uint32IndicesOptIn = true;
        profile.instancingOptIn = true;
        profile.prototypeOptIn = true;
        profile.derivativesOptIn = true;
        profile.mainBrowserThreadWithWorkersOptIn = true;
        auto& b = profile.budgets;
        b.contextMetadataBytes = 256 * 1024;
        b.resourceMetadataBytes = 16 * MiB;
        b.uploadBytes = 64 * MiB;
        b.logicalBufferBytes = 16 * MiB;
        b.logicalTextureBytes = 64 * MiB;
        b.resourceObjects = 512;
        b.deviceMetadataBytes = MiB;
        b.identityFieldBytes = 4096;
        b.surfaceMutations = 4096;
        b.pipelines = {1, "editor-pipelines", 4};
        b.descriptors = {1, "editor-descriptors", 4};
        auto& e = profile.execution;
        e.contractVersion = 1;
        e.prototypeOptIn = true;
        e.maxSubmissions = 1000000;
        e.maxCpuMetadataBytes = MiB;
        // Publications: GUI VS has seven leaves; environment FS has eleven.
        e.requirements = {1, 1, 4096, 4096, 7, 16, 21, 0, 5};
        auto& f = profile.frames;
        f.contractVersion = 1;
        f.prototypeOptIn = true;
        f.surface = {"#canvas", {1440, 810}, rhi::TextureFormat::Rgba8Unorm, 0};
        f.commands = {5,
            true,
            maxCommands + maxSceneDraws + 2,
            // The existing catalog reserves maxDraws snapshots per distinct ABI.
            (maxCommands + maxSceneDraws + 2)
                * (sizeof(EditorSceneViewportEnvironmentPush) + 64 + 256),
            3 * MiB,
            {1, true, maxCommands + maxSceneDraws + 12, 512, 2 * MiB, 64 * MiB, 131072},
            4,
            2,
            5,
            // Attachments, five PBR bindings, Sprite atlases and both input streams.
            64,
            2,
            2,
            5};
        f.maxFrameCpuMetadataBytes = 32 * MiB;
        f.frameQuota = core::makeShared<core::CpuMemoryQuota>(f.maxFrameCpuMetadataBytes);
        f.commandQuota =
            core::makeShared<core::CpuMemoryQuota>(f.commands.issue.maxCpuMetadataBytes);
        platform::RenderSystemDesc creation;
        creation.applicationName = "ArpEditor";
        creation.backendPreference = platform::BackendPreference::WebGL;
        creation.orderedGraphics = &profile;
        return platform::createRenderSystem(creation);
    }

    static std::shared_ptr<rhi::RenderPipeline> spritePipeline(
        platform::RenderSystem& system, core::pmr::String& diagnostic)
    {
        const auto loaded = shader::ShaderReflectionCache::loadFromFile(
            "/artifacts/sprite/editor_viewport_sprite.reflection.yaml");
        if (!loaded.success
            || !validateEditorSceneViewportSpriteReflection(loaded.module.reflection)) {
            diagnostic = "EDITOR-WEB-SPRITE-REFLECTION";
            return {};
        }
        platform::OrderedPipelineLoadDesc request;
        request.artifactPath = "/artifacts/sprite/editor_viewport_sprite.webgl-program.yaml";
        request.artifactBudget = {4 * MiB, 32 * MiB, 32 * MiB, 32};
        request.maxUniformLeaves = 256;
        request.maxInterfaces = 32;
        request.maxTypeNodes = 1024;
        request.maxInterfaceNameBytes = 1024;
        request.maxDiagnosticBytes = 65536;
        request.pipeline = buildEditorSceneViewportSpritePipeline(
            "editor/viewport_sprite.slang", EditorSceneViewportSurfaceProfile::baseline());
        request.pipeline.vertexShader = {};
        request.pipeline.fragmentShader = {};
        return system.createOrderedPipeline(request, diagnostic);
    }

    static std::shared_ptr<rhi::RenderPipeline> pbrPipeline(platform::RenderSystem& system,
        shader::ShaderReflection& reflection,
        core::pmr::String& diagnostic)
    {
        const auto loaded = shader::ShaderReflectionCache::loadFromFile(
            "/artifacts/pbr/editor_viewport_pbr.reflection.yaml");
        if (!loaded.success) {
            diagnostic = "EDITOR-WEB-PBR-REFLECTION";
            return {};
        }
        const auto surface = EditorSceneViewportSurfaceProfile::baseline();
        const auto contract = buildEditorSceneViewportPipelineContract(
            loaded.module.reflection, "editor/viewport_pbr.slang", surface.color, surface.depth, 1);
        if (!contract.valid()) {
            diagnostic = editorSceneViewportPipelineDiagnosticCode(contract.diagnostic);
            return {};
        }
        platform::OrderedPipelineLoadDesc request;
        request.artifactPath = "/artifacts/pbr/editor_viewport_pbr.webgl-program.yaml";
        request.artifactBudget = {4 * MiB, 32 * MiB, 32 * MiB, 32};
        request.maxUniformLeaves = 256;
        request.maxInterfaces = 32;
        request.maxTypeNodes = 1024;
        request.maxInterfaceNameBytes = 1024;
        request.maxDiagnosticBytes = 65536;
        request.pipeline = contract.pipeline;
        request.pipeline.vertexShader = {};
        request.pipeline.fragmentShader = {};
        reflection = loaded.module.reflection;
        return system.createOrderedPipeline(request, diagnostic);
    }

    static std::shared_ptr<rhi::RenderPipeline> environmentPipeline(
        platform::RenderSystem& system, core::pmr::String& diagnostic)
    {
        platform::OrderedPipelineLoadDesc request;
        request.artifactPath =
            "/artifacts/environment/editor_viewport_environment.webgl-program.yaml";
        request.artifactBudget = {4 * MiB, 32 * MiB, 32 * MiB, 32};
        request.maxUniformLeaves = 256;
        request.maxInterfaces = 32;
        request.maxTypeNodes = 1024;
        request.maxInterfaceNameBytes = 1024;
        request.maxDiagnosticBytes = 65536;
        request.pipeline = buildEditorSceneViewportEnvironmentPipeline(
            "editor/viewport_environment.slang", EditorSceneViewportSurfaceProfile::baseline());
        // The loader owns entrypoint identities from the original publication.
        request.pipeline.vertexShader = {};
        request.pipeline.fragmentShader = {};
        return system.createOrderedPipeline(request, diagnostic);
    }

    static std::shared_ptr<rhi::RenderPipeline> pipeline(
        platform::RenderSystem& system, core::pmr::String& diagnostic)
    {
        using namespace rhi;
        platform::OrderedPipelineLoadDesc request;
        request.artifactPath = "/artifacts/gui_unified.webgl-program.yaml";
        request.artifactBudget = {4 * MiB, 32 * MiB, 32 * MiB, 32};
        request.maxUniformLeaves = 256;
        request.maxInterfaces = 32;
        request.maxTypeNodes = 1024;
        request.maxInterfaceNameBytes = 1024;
        request.maxDiagnosticBytes = 65536;
        request.useReflectedLayout = false;
        request.layout.descriptorLayouts.push_back({"GuiBitmapSet",
            {{0, DescriptorType::SampledTexture, ShaderStage::Fragment},
                {1, DescriptorType::Sampler, ShaderStage::Fragment}}});
        request.layout.descriptorSets.push_back({0, "GuiBitmapSet"});
        request.layout.pushConstantRanges.push_back({ShaderStage::Vertex, 0, 64});
        auto& p = request.pipeline;
        p.colorFormat = TextureFormat::Rgba8Unorm;
        p.dynamicViewportScissor = true;
        p.cullMode = CullMode::None;
        p.frontFace = FrontFace::CounterClockwise;
        ColorTargetState color;
        color.format = p.colorFormat;
        color.blendEnabled = true;
        color.colorBlend =
            color.alphaBlend = {BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add};
        p.colorTargets.push_back(color);
        VertexBufferLayoutDesc stream;
        stream.strideBytes = sizeof(core::Vec2);
        stream.attributes = {{0, 0, VertexFormat::Float32x2}};
        p.vertexBuffers.push_back(std::move(stream));
        return system.createOrderedPipeline(request, diagnostic);
    }
};
} // namespace arp::editor
