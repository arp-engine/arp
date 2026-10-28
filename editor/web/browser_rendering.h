#pragma once

#include "platform/render_system.h"
#include "platform/render_system_ordered_profile.h"

namespace arp::editor {
// Explicit Prototype budgets include the 2048x3072 atlas and a complete editor
// frame. They describe logical/CPU capacity, not physical driver residency.
class BrowserRendering final {
public:
    static constexpr std::uint32_t maxCommands = 16384;
    static constexpr std::uint64_t MiB = 1024 * 1024;

    static platform::RenderSystem create()
    {
        platform::RenderSystemOrderedProfile profile;
        profile.contractVersion = 2;
        profile.prototypeOptIn = true;
        profile.derivativesOptIn = true;
        profile.mainBrowserThreadWithWorkersOptIn = true;
        auto& b = profile.budgets;
        b.contextMetadataBytes = 256 * 1024;
        b.resourceMetadataBytes = 16 * MiB;
        b.uploadBytes = 64 * MiB;
        b.logicalBufferBytes = MiB;
        b.logicalTextureBytes = 64 * MiB;
        b.resourceObjects = 64;
        b.deviceMetadataBytes = MiB;
        b.identityFieldBytes = 4096;
        b.surfaceMutations = 4096;
        b.pipelines = {1, "editor-pipelines", 1};
        b.descriptors = {1, "editor-descriptors", 4};
        auto& e = profile.execution;
        e.contractVersion = 1;
        e.prototypeOptIn = true;
        e.maxSubmissions = 1000000;
        e.maxCpuMetadataBytes = MiB;
        // GuiUnified publication declares seven vertex uniform leaves.
        e.requirements = {1, 1, 4096, 4096, 1, 7, 2, 0, 1};
        auto& f = profile.frames;
        f.contractVersion = 1;
        f.prototypeOptIn = true;
        f.surface = {"#canvas", {1440, 810}, rhi::TextureFormat::Rgba8Unorm, 0};
        f.commands = {4,
            true,
            maxCommands,
            2 * MiB,
            1,
            {1, true, maxCommands + 2, 128, 2 * MiB, 64 * MiB, 131072},
            1,
            1,
            1,
            8};
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
