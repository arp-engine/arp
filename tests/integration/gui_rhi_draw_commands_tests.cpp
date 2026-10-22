#include "core/memory/cpu_allocator.h"
#include "renderer/gui/gui_draw_plan.h"
#include "renderer/gui/gui_rhi_draw_commands.h"
#include "renderer/renderers.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

arp::rhi::ShaderPackageDesc makeGuiShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "GuiRhiShaderPackage";
    shaderPackage.slangModulePath = "gui.slang";
    shaderPackage.entrypoints.push_back({"gui.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back({"gui.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.artifacts.spirvPath = "gui.spv";
    shaderPackage.artifacts.mslPath = "gui.metal";
    shaderPackage.artifacts.reflectionPath = "gui.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = sizeof(arp::renderer::GuiPushConstants);
    shaderPackage.reflection.descriptorBindings.push_back({
        0,
        0,
        arp::rhi::DescriptorType::UniformBuffer,
        arp::rhi::ShaderStage::Vertex | arp::rhi::ShaderStage::Fragment,
    });
    shaderPackage.reflection.descriptorBindings.push_back({
        1,
        0,
        arp::rhi::DescriptorType::SampledTexture,
        arp::rhi::ShaderStage::Fragment,
    });
    shaderPackage.reflection.descriptorBindings.push_back({
        1,
        1,
        arp::rhi::DescriptorType::Sampler,
        arp::rhi::ShaderStage::Fragment,
    });
    return shaderPackage;
}

arp::gui::DebugDrawData makeDrawData()
{
    arp::gui::DebugDrawData DebugDrawData;
    DebugDrawData.quads.push_back(arp::gui::DebugDrawQuad{
        1,
        arp::gui::NodeType::Panel,
        {0.0f, 0.0f, 320.0f, 48.0f},
    });
    DebugDrawData.textQuads.push_back(arp::gui::DebugDrawTextQuad{
        2,
        {12.0f, 14.0f, 120.0f, 18.0f},
        {0.9f, 0.95f, 1.0f, 1.0f},
    });
    return DebugDrawData;
}

arp::renderer::GuiDrawPlan makeDrawPlan()
{
    const auto DebugDrawData = makeDrawData();
    return arp::renderer::buildGuiDrawPlan(DebugDrawData, {640, 360});
}

bool isNeutralGray(const arp::renderer::GuiPushConstants& push)
{
    return std::abs(push.color[0] - push.color[1]) <= 0.02f
        && std::abs(push.color[0] - push.color[2]) <= 0.02f;
}

std::filesystem::path findSourceFile(const std::filesystem::path& relativePath)
{
    auto candidateRoot = std::filesystem::current_path();
    for (std::uint32_t depth = 0; depth < 8; ++depth) {
        const auto candidate = candidateRoot / relativePath;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!candidateRoot.has_parent_path()) {
            break;
        }
        candidateRoot = candidateRoot.parent_path();
    }
    return relativePath;
}

std::pmr::string readSourceFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    assert(file);
    return std::pmr::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void rendererFacadeSourceIsRhiOnly()
{
    const auto facade = readSourceFile(findSourceFile("src/renderer/renderers.h"));
    assert(facade.find("renderer/ui/gui_renderer.h") != std::pmr::string::npos);
    assert(facade.find("renderer/postprocess/fxaa_renderer.h") != std::pmr::string::npos);
    assert(facade.find("renderer/particles/particle_renderer.h") != std::pmr::string::npos);
    assert(facade.find("renderer/postprocess/ssao_renderer.h") != std::pmr::string::npos);

    const auto source = readSourceFile(findSourceFile("src/renderer/renderer_common.h"))
        + readSourceFile(findSourceFile("src/renderer/ui/gui_renderer.h"))
        + readSourceFile(findSourceFile("src/renderer/postprocess/fxaa_renderer.h"))
        + readSourceFile(findSourceFile("src/renderer/particles/particle_renderer.h"))
        + readSourceFile(findSourceFile("src/renderer/postprocess/ssao_renderer.h"));
    assert(source.find("rhi/rhi.h") == std::pmr::string::npos);
    assert(source.find("rhi"
                       "::")
        == std::pmr::string::npos);
    assert(source.find("rhi::GpuDevice") != std::pmr::string::npos);
    assert(source.find("rhi::RenderPipeline") != std::pmr::string::npos);
    assert(source.find("recordGuiRhiDrawPlan") != std::pmr::string::npos);
    assert(source.find("recordFxaaPass") != std::pmr::string::npos);
    assert(source.find("recordParticleDrawPlan") != std::pmr::string::npos);
    assert(source.find("createCommandList") == std::pmr::string::npos);
}

void guiRhiDrawCommandsUseExplicitPushConstantData()
{
    const auto source = readSourceFile(findSourceFile("src/renderer/gui/gui_rhi_draw_commands.h"));
    assert(source.find("pass->pushConstants") != std::pmr::string::npos);
    assert(source.find("&command.pushConstants") != std::pmr::string::npos);
    assert(source.find("kGuiQuadIndexCount") != std::pmr::string::npos);
    assert(source.find("pass->bindIndexBuffer(*desc.indexBuffer, rhi::IndexFormat::Uint32, 0u)")
        != std::pmr::string::npos);
    assert(source.find("drawIndexed(\n            kGuiQuadIndexCount") != std::pmr::string::npos);
    assert(source.find("command.firstVertex,\n            0") == std::pmr::string::npos);
}

void rendererFacadeRecordsGuiThroughRhiDevice()
{
    using namespace arp::renderer;
    using namespace arp::rhi;

    auto device = createNullDevice();
    assert(device);

    GuiRenderer renderer(*device);
    const auto created = renderer.create({
        "gui.slang",
        TextureFormat::Bgra8Unorm,
        {640, 360},
    });
    assert(created.success);
    assert(renderer.graphicsPipeline());

    auto colorTarget = device->createTexture({
        "GuiFacadeBackbuffer",
        {640, 360, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::ColorAttachment,
    });
    assert(colorTarget);

    RenderFrameContext frame{
        AcquiredFrame{true, "", 1u, 0u},
        RenderTargetDesc{
            "GuiFacadeBackbuffer",
            colorTarget->id(),
            {640, 360},
            TextureFormat::Bgra8Unorm,
            ResourceState::Present,
        },
        nullptr,
        colorTarget.get(),
        nullptr,
    };
    const auto submitCountBeforeDraw = device->queue().submitCount();
    const auto drawn = renderer.drawFrame(frame, makeDrawData());
    assert(drawn.success);
    assert(device->queue().submitCount() == submitCountBeforeDraw + 1u);
}

void guiRendererPipelineUsesExplicitViewportAndScissor()
{
    const auto source = readSourceFile(findSourceFile("src/renderer/renderer_common.h"));
    assert(
        source.find(
            "makeRenderPipelineDesc(\n    const std::pmr::string& label,\n    const rhi::PipelineLayout& layout,\n    const rhi::ShaderPackageDesc& shaderPackage,\n    rhi::TextureFormat colorFormat,\n    const rhi::Extent2D& extent)")
        != std::pmr::string::npos);
    assert(source.find("desc.hasViewport = true") != std::pmr::string::npos);
    assert(
        source.find(
            "desc.viewport = {\n        0.0f,\n        0.0f,\n        static_cast<float>(extent.width),\n        static_cast<float>(extent.height)")
        != std::pmr::string::npos);
    assert(source.find("desc.hasScissor = true") != std::pmr::string::npos);
    assert(source.find("desc.scissor = { 0, 0, extent.width, extent.height }")
        != std::pmr::string::npos);
}

void guiRendererPipelineUsesL2AlphaBlendAndVulkanLowersIt()
{
    const auto rendererSource = readSourceFile(findSourceFile("src/renderer/ui/gui_renderer.h"));
    const auto particleRendererSource =
        readSourceFile(findSourceFile("src/renderer/particles/particle_renderer.h"));
    const auto vulkanPipelineSource =
        readSourceFile(findSourceFile("src/rhi/backends/vulkan/vulkan_native_pipelines.cpp"));
    assert(rendererSource.find("BlendFactor::SrcAlpha") != std::pmr::string::npos);
    assert(rendererSource.find("BlendFactor::OneMinusSrcAlpha") != std::pmr::string::npos);
    assert(rendererSource.find("desc.colorTargets.push_back") != std::pmr::string::npos);
    assert(particleRendererSource.find("enableParticleAlphaBlend") != std::pmr::string::npos);
    assert(particleRendererSource.find("BlendFactor::OneMinusSrcAlpha") != std::pmr::string::npos);
    assert(particleRendererSource.find(
               "rhi::BlendFactor::One,\n        rhi::BlendFactor::OneMinusSrcAlpha")
        != std::pmr::string::npos);
    assert(vulkanPipelineSource.find("blendEnable = target.blendEnabled ? VK_TRUE : VK_FALSE")
        != std::pmr::string::npos);
    assert(vulkanPipelineSource.find("blendFactorFor(target.colorBlend.sourceFactor)")
        != std::pmr::string::npos);
    assert(
        vulkanPipelineSource.find("colorWriteMaskFor(target.writeMask)") != std::pmr::string::npos);
}

void guiDrawPlanFallbackControlsUseNeutralGrayPalette()
{
    using namespace arp::gui;
    using namespace arp::renderer;

    DebugDrawData DebugDrawData;
    DebugDrawData.quads.push_back(DebugDrawQuad{
        7000,
        NodeType::Panel,
        {0.0f, 0.0f, 100.0f, 20.0f},
        VisualState::Normal,
    });
    DebugDrawData.quads.push_back(DebugDrawQuad{
        7001,
        NodeType::Button,
        {0.0f, 24.0f, 100.0f, 20.0f},
        VisualState::Normal,
    });
    DebugDrawData.quads.push_back(DebugDrawQuad{
        7002,
        NodeType::Slider,
        {0.0f, 48.0f, 100.0f, 20.0f},
        VisualState::Hovered,
    });
    DebugDrawData.quads.push_back(DebugDrawQuad{
        7003,
        NodeType::Dropdown,
        {0.0f, 72.0f, 100.0f, 20.0f},
        VisualState::Active,
    });

    const auto plan = buildGuiDrawPlan(DebugDrawData, {320, 180});
    assert(plan.success);
    assert(plan.commands.size() == 4u);
    assert(plan.commands[0].pushConstants.color[0] >= 0.10f);
    assert(plan.commands[0].pushConstants.color[0] <= 0.16f);
    assert(isNeutralGray(plan.commands[0].pushConstants));
    for (const auto& command : plan.commands) {
        assert(isNeutralGray(command.pushConstants));
        assert(command.pushConstants.color[3] >= 0.80f);
    }
}

void guiPushConstantsMatchSlangStd430Layout()
{
    static_assert(offsetof(arp::renderer::GuiPushConstants, screenSize) == 0u);
    static_assert(offsetof(arp::renderer::GuiPushConstants, rectOrigin) == 8u);
    static_assert(offsetof(arp::renderer::GuiPushConstants, rectSize) == 16u);
    static_assert(offsetof(arp::renderer::GuiPushConstants, uvOrigin) == 24u);
    static_assert(offsetof(arp::renderer::GuiPushConstants, uvSize) == 32u);
    static_assert(offsetof(arp::renderer::GuiPushConstants, flags) == 40u);
    static_assert(offsetof(arp::renderer::GuiPushConstants, color) == 48u);
    static_assert(sizeof(arp::renderer::GuiPushConstants) == 64u);

    const auto push = arp::renderer::buildGuiPushConstants(arp::gui::Rect{1.0f, 2.0f, 3.0f, 4.0f},
        arp::gui::Color{0.25f, 0.50f, 0.75f, 1.0f},
        arp::rhi::Extent2D{640u, 360u});
    assert(push.color[0] == 0.25f);
    assert(push.color[1] == 0.50f);
    assert(push.color[2] == 0.75f);
    assert(push.color[3] == 1.0f);
}

void guiCoordinatesAreLoweredByRhiBackend()
{
    const arp::gui::Rect rect{10.0f, 20.0f, 30.0f, 40.0f};
    const arp::gui::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    const arp::rhi::Extent2D extent{640u, 360u};

    const auto web =
        arp::renderer::buildGuiPushConstants(rect, color, extent, arp::rhi::Backend::WebGPU);
    assert(web.rectOrigin[1] == 300.0f);

    const auto native =
        arp::renderer::buildGuiPushConstants(rect, color, extent, arp::rhi::Backend::Vulkan);
    assert(native.rectOrigin[1] == 20.0f);

    const auto metal =
        arp::renderer::buildGuiPushConstants(rect, color, extent, arp::rhi::Backend::Metal);
    assert(metal.rectOrigin[1] == 300.0f);

    const arp::gui::Rect uv{0.2f, 0.3f, 0.4f, 0.1f};
    const auto webTextured = arp::renderer::buildGuiPushConstants(
        rect, color, uv, true, extent, arp::rhi::Backend::WebGPU);
    assert(webTextured.uvOrigin[1] == 0.4f);
    assert(webTextured.uvSize[1] == -0.1f);

    const auto nativeTextured = arp::renderer::buildGuiPushConstants(
        rect, color, uv, true, extent, arp::rhi::Backend::Vulkan);
    assert(nativeTextured.uvOrigin[1] == 0.3f);
    assert(nativeTextured.uvSize[1] == 0.1f);

    // CPU-authored UI atlases, including FreeType glyph rasters, have a
    // top-left first row. Metal therefore needs the same sampled-V lowering
    // as WebGPU even though its render-target origin is a separate contract.
    const auto metalTextured = arp::renderer::buildGuiPushConstants(
        rect, color, uv, true, extent, arp::rhi::Backend::Metal);
    assert(metalTextured.uvOrigin[1] == 0.4f);
    assert(metalTextured.uvSize[1] == -0.1f);
}

void guiDrawPlanCarriesTexturedQuadMetadata()
{
    arp::gui::DebugDrawData DebugDrawData;
    arp::gui::DebugDrawQuad quad;
    quad.nodeId = 9000u;
    quad.type = arp::gui::NodeType::Image;
    quad.rect = {10.0f, 12.0f, 64.0f, 64.0f};
    quad.color = {1.0f, 1.0f, 1.0f, 1.0f};
    quad.customColor = true;
    quad.textureResourceId = "ui/pika/backpack_slot";
    quad.textureUvRect = {0.5f, 0.0f, 0.25f, 0.5f};
    quad.textured = true;
    DebugDrawData.quads.push_back(std::move(quad));

    const auto plan = arp::renderer::buildGuiDrawPlan(DebugDrawData, {1280u, 720u});
    assert(plan.success);
    assert(plan.commands.size() == 1u);
    assert(plan.commands[0].textured);
    assert(plan.commands[0].textureResourceId == std::pmr::string("ui/pika/backpack_slot"));
    assert(plan.commands[0].pushConstants.flags[0] == 1.0f);
    assert(plan.commands[0].pushConstants.uvOrigin[0] == 0.5f);
    assert(plan.commands[0].pushConstants.uvSize[1] == 0.5f);
}

void unifiedUiTextCommandsRequireCachedTextAtlas()
{
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    arp::renderer::ui::UiDrawCommand panel;
    panel.kind = arp::renderer::ui::UiDrawCommandKind::Box;
    panel.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 128.0f, 32.0f);
    panel.clipRect = panel.rect;
    commands.push_back(std::move(panel));

    arp::renderer::ui::UiDrawCommand text;
    text.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    text.rect = arp::ui::LayoutRect::fromOriginSize(8.0f, 4.0f, 96.0f, 18.0f);
    text.clipRect = text.rect;
    text.text = arp::core::pmr::String("HP", &arp::core::defaultCpuMemoryResource());
    commands.push_back(std::move(text));

    const auto missingAtlasPlan = arp::renderer::buildGuiDrawPlan(commands, {1280u, 720u});
    assert(!missingAtlasPlan.success);
    assert(missingAtlasPlan.diagnostics
        == std::pmr::string("UI text command requires a cached text atlas source rect"));

    commands[1].textureResourceId = arp::core::pmr::String(
        "texture/pika_ui_atlas+ui_text_atlas", &arp::core::defaultCpuMemoryResource());
    commands[1].textureSourceRect = arp::ui::LayoutRect::fromOriginSize(0.4f, 0.5f, 0.1f, 0.05f);
    commands[1].hasTextureSourceRect = true;

    arp::renderer::ui::UiDrawCommand image;
    image.kind = arp::renderer::ui::UiDrawCommandKind::Image;
    image.rect = arp::ui::LayoutRect::fromOriginSize(16.0f, 8.0f, 32.0f, 32.0f);
    image.clipRect = image.rect;
    image.textureResourceId =
        arp::core::pmr::String("texture/pika_ui_atlas", &arp::core::defaultCpuMemoryResource());
    image.textureSourceRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 0.25f, 0.25f);
    image.hasTextureSourceRect = true;
    commands.push_back(std::move(image));

    const auto plan = arp::renderer::buildGuiDrawPlan(commands, {1280u, 720u});
    assert(plan.success);
    assert(plan.commands.size() == 3u);
    assert(!plan.commands[0].text);
    assert(!plan.commands[0].textured);
    assert(plan.commands[0].pushConstants.flags[0] == 0.0f);
    assert(plan.commands[1].text);
    assert(plan.commands[1].textured);
    assert(plan.commands[1].textureResourceId
        == std::pmr::string("texture/pika_ui_atlas+ui_text_atlas"));
    assert(plan.commands[1].pushConstants.flags[0] == 1.0f);
    assert(plan.commands[1].pushConstants.uvOrigin[0] == 0.4f);
    assert(!plan.commands.back().text);
    assert(plan.commands.back().textured);
    assert(plan.commands.back().textureResourceId == std::pmr::string("texture/pika_ui_atlas"));
}

void unifiedUiGlyphQuadCommandsRequireExplicitAtlasPage()
{
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    arp::renderer::ui::UiDrawCommand glyph;
    glyph.kind = arp::renderer::ui::UiDrawCommandKind::GlyphQuad;
    glyph.rect = arp::ui::LayoutRect::fromOriginSize(8.0f, 4.0f, 12.0f, 16.0f);
    glyph.clipRect = glyph.rect;
    glyph.textureResourceId =
        arp::core::pmr::String("texture/pika_ui_atlas", &arp::core::defaultCpuMemoryResource());
    glyph.textureSourceRect = arp::ui::LayoutRect::fromOriginSize(0.4f, 0.5f, 0.1f, 0.05f);
    glyph.hasTextureSourceRect = true;
    commands.push_back(glyph);

    const auto missingPagePlan = arp::renderer::buildGuiDrawPlan(commands, {1280u, 720u});
    assert(!missingPagePlan.success);
    assert(missingPagePlan.diagnostics
        == std::pmr::string(
            "UI glyph quad command requires explicit atlas resource, source rect, and page index"));

    commands[0].hasAtlasPageIndex = true;
    commands[0].atlasPageIndex = 0u;

    arp::renderer::ui::UiDrawCommand image;
    image.kind = arp::renderer::ui::UiDrawCommandKind::Image;
    image.rect = arp::ui::LayoutRect::fromOriginSize(16.0f, 8.0f, 32.0f, 32.0f);
    image.clipRect = image.rect;
    image.textureResourceId =
        arp::core::pmr::String("texture/pika_ui_atlas", &arp::core::defaultCpuMemoryResource());
    image.textureSourceRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 0.25f, 0.25f);
    image.hasTextureSourceRect = true;
    commands.push_back(std::move(image));

    const auto plan = arp::renderer::buildGuiDrawPlan(commands, {1280u, 720u});
    assert(plan.success);
    assert(plan.commands.size() == 2u);
    assert(plan.commands[0].text);
    assert(plan.commands[0].textured);
    assert(plan.commands[0].textureResourceId == std::pmr::string("texture/pika_ui_atlas"));
    assert(plan.commands[0].pushConstants.flags[0] == 1.0f);
    assert(plan.commands[0].pushConstants.flags[1] == 0.0f);
    assert(plan.commands[1].textureResourceId == plan.commands[0].textureResourceId);
}

void rendererUiGlyphPageContractIsExplicit()
{
    const auto source = readSourceFile(findSourceFile("src/renderer/ui/ui_draw_plan.h"))
        + readSourceFile(findSourceFile("src/renderer/gui/gui_draw_plan.h"));
    assert(source.find("UiDrawCommandKind::GlyphQuad") != std::pmr::string::npos);
    assert(source.find("atlasPageIndex") != std::pmr::string::npos);
    assert(source.find("hasAtlasPageIndex") != std::pmr::string::npos);
    assert(source.find("atlas page binding") != std::pmr::string::npos);
    assert(source.find("renderer parses YAML") == std::pmr::string::npos);
    assert(source.find("UiTextAtlasBuilder().build") == std::pmr::string::npos);
}

void uiAtlasPagePoliciesReportBackendLoweringContracts()
{
    using namespace arp::renderer::ui;

    const UiAtlasPagePolicy policies[]{
        UiAtlasPagePolicy::SingleTexture,
        UiAtlasPagePolicy::TextureArray,
        UiAtlasPagePolicy::MultiPage,
    };

    for (const auto policy : policies) {
        const auto contract = UiRhiBackendContract().evaluate(policy);
        assert(contract.semantic.pagePolicy == policy);
        assert(contract.semantic.explicitAtlasPageIndex);
        assert(contract.semantic.backendOwnedUploadStaging);
        assert(contract.semantic.backendOwnedReadback);
        assert(!contract.semantic.exposesNativeBackendHandles);
        assert(contract.backendReports.size() == 5u);

        bool sawNull = false;
        bool sawVulkan = false;
        bool sawMetal = false;
        bool sawWebGPU = false;
        bool sawWebGL = false;
        for (const auto& report : contract.backendReports) {
            assert(report.pagePolicy == policy);
            assert(report.loweringMode != UiAtlasPageLoweringMode::Unspecified);
            assert(!report.exposesNativeBackendHandles);
            assert(report.backendOwnedUploadStaging);
            if (report.backend == arp::rhi::Backend::Null) {
                sawNull = true;
                assert(report.supported);
                assert(report.diagnostics.empty());
            } else {
                assert(!report.supported);
                assert(!report.diagnostics.empty());
                assert(
                    report.diagnostics.find("deterministic diagnostic") != std::pmr::string::npos);
                if (policy == UiAtlasPagePolicy::TextureArray) {
                    assert(report.diagnostics.find("texture array") != std::pmr::string::npos);
                }
                if (policy == UiAtlasPagePolicy::MultiPage) {
                    assert(report.diagnostics.find("multi-page") != std::pmr::string::npos);
                }
                if (report.backend == arp::rhi::Backend::Vulkan) {
                    sawVulkan = true;
                } else if (report.backend == arp::rhi::Backend::Metal) {
                    sawMetal = true;
                } else if (report.backend == arp::rhi::Backend::WebGPU) {
                    sawWebGPU = true;
                } else if (report.backend == arp::rhi::Backend::WebGL) {
                    sawWebGL = true;
                    assert(report.diagnostics.find("RHI-WEBGL1-SEMANTIC-REPORT-UNAVAILABLE") == 0u);
                }
            }
        }
        assert(sawNull);
        assert(sawVulkan);
        assert(sawMetal);
        assert(sawWebGPU);
        assert(sawWebGL);
    }
}

void guiRendererAtlasDescriptorContractIsExplicit()
{
    const auto layout = arp::renderer::detail::guiAtlasDescriptorLayout();
    assert(layout.name == std::pmr::string("GuiAtlasSet"));
    assert(layout.bindings.size() == 2u);
    assert(layout.bindings[0].binding == 0u);
    assert(layout.bindings[0].type == arp::rhi::DescriptorType::SampledTexture);
    assert(layout.bindings[0].stages == arp::rhi::ShaderStage::Fragment);
    assert(layout.bindings[0].textureViewDimensionConstrained);
    assert(layout.bindings[0].textureViewDimension == arp::rhi::TextureViewDimension::Texture2D);
    assert(layout.bindings[1].binding == 1u);
    assert(layout.bindings[1].type == arp::rhi::DescriptorType::Sampler);
    assert(layout.bindings[1].stages == arp::rhi::ShaderStage::Fragment);

    const auto pipelineLayout = arp::renderer::detail::guiPipelineLayoutDesc();
    assert(pipelineLayout.descriptorSets.size() == 2u);
    assert(pipelineLayout.descriptorSets[0].layoutName == std::pmr::string("GuiFrameSet"));
    assert(pipelineLayout.descriptorSets[1].layoutName == std::pmr::string("GuiAtlasSet"));

    const auto source = readSourceFile(findSourceFile("src/renderer/gui/gui_rhi_draw_commands.h"));
    assert(source.find("bindDescriptorSet(1, *desc.atlasDescriptorSet)") != std::pmr::string::npos);
    assert(source.find("requires a descriptor-backed atlas set") != std::pmr::string::npos);
}

} // namespace

int main()
{
    using namespace arp::renderer;
    using namespace arp::rhi;

    rendererFacadeSourceIsRhiOnly();
    guiRhiDrawCommandsUseExplicitPushConstantData();
    guiRendererPipelineUsesExplicitViewportAndScissor();
    guiRendererPipelineUsesL2AlphaBlendAndVulkanLowersIt();
    guiDrawPlanFallbackControlsUseNeutralGrayPalette();
    guiPushConstantsMatchSlangStd430Layout();
    guiCoordinatesAreLoweredByRhiBackend();
    guiDrawPlanCarriesTexturedQuadMetadata();
    unifiedUiTextCommandsRequireCachedTextAtlas();
    unifiedUiGlyphQuadCommandsRequireExplicitAtlasPage();
    rendererUiGlyphPageContractIsExplicit();
    uiAtlasPagePoliciesReportBackendLoweringContracts();
    guiRendererAtlasDescriptorContractIsExplicit();
    rendererFacadeRecordsGuiThroughRhiDevice();

    const auto guiPlan = makeDrawPlan();
    assert(guiPlan.success);
    assert(guiPlan.commands.size() == 2u);

    auto device = createNullDevice();
    auto colorTarget = device->createTexture({
        "GuiRhiBackbuffer",
        {640, 360, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    auto uploadSource = device->createBuffer({
        "GuiRhiUploadSource",
        256,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined,
    });
    auto vertexBuffer = device->createBuffer({
        "GuiRhiVertices",
        256,
        ResourceUsage::VertexBuffer | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    auto indexBuffer = device->createBuffer({
        "GuiRhiIndices",
        6u * sizeof(std::uint32_t),
        ResourceUsage::IndexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead,
    });
    auto frameConstants = device->createBuffer({
        "GuiRhiFrameConstants",
        256,
        ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined,
    });
    assert(colorTarget && uploadSource && vertexBuffer && indexBuffer && frameConstants);

    DescriptorLayoutDesc frameLayout;
    frameLayout.name = "GuiFrameSet";
    frameLayout.bindings.push_back({
        0,
        DescriptorType::UniformBuffer,
        ShaderStage::Vertex | ShaderStage::Fragment,
    });
    const auto atlasLayout = arp::renderer::detail::guiAtlasDescriptorLayout();
    PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.descriptorLayouts.push_back(frameLayout);
    pipelineLayoutDesc.descriptorLayouts.push_back(atlasLayout);
    pipelineLayoutDesc.descriptorSets.push_back({0, frameLayout.name});
    pipelineLayoutDesc.descriptorSets.push_back({1, atlasLayout.name});
    pipelineLayoutDesc.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(GuiPushConstants)});
    auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDesc);
    assert(pipelineLayout);

    auto shaderPackage = makeGuiShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);

    RenderPipelineDesc pipelineDesc;
    pipelineDesc.label = "GuiRhiPipeline";
    pipelineDesc.layout = pipelineLayout.get();
    pipelineDesc.shaderPackage = &shaderPackage;
    pipelineDesc.vertexShader = {"gui.slang", "vsMain", ShaderStage::Vertex};
    pipelineDesc.fragmentShader = {"gui.slang", "fsMain", ShaderStage::Fragment};
    pipelineDesc.colorFormat = TextureFormat::Bgra8Unorm;
    auto pipeline = device->createRenderPipeline(pipelineDesc);
    assert(pipeline);

    auto atlasTexture = device->createTexture({
        "GuiRhiAtlas",
        {1, 1, 1},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
    });
    assert(atlasTexture);
    auto atlasView = device->createTextureView(*atlasTexture,
        {
            "GuiRhiAtlasView",
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            TextureViewDimension::Texture2D,
        });
    auto atlasSampler = device->createSampler({
        "GuiRhiAtlasSampler",
        SamplerFilter::Linear,
        SamplerFilter::Linear,
        SamplerAddressMode::ClampToEdge,
        SamplerAddressMode::ClampToEdge,
        SamplerAddressMode::ClampToEdge,
    });
    assert(atlasView && atlasSampler);

    auto arena = device->createDescriptorArena({"GuiRhiArena", 2, 3});
    assert(arena);
    auto descriptorSet = arena->allocateDescriptorSet({"GuiFrameSet", frameLayout});
    assert(descriptorSet);
    const auto descriptorWritten = descriptorSet->write({
        0,
        DescriptorType::UniformBuffer,
        frameConstants->id(),
        DescriptorResourceKind::Buffer,
    });
    assert(descriptorWritten.success);
    auto atlasSet = arena->allocateDescriptorSet({"GuiAtlasSet", atlasLayout});
    assert(atlasSet);
    const auto atlasViewWritten = atlasSet->write({
        0,
        DescriptorType::SampledTexture,
        atlasView->id(),
        DescriptorResourceKind::TextureView,
    });
    assert(atlasViewWritten.success);
    const auto atlasSamplerWritten = atlasSet->write({
        1,
        DescriptorType::Sampler,
        atlasSampler->id(),
        DescriptorResourceKind::Sampler,
    });
    assert(atlasSamplerWritten.success);

    auto rejectedEncoder = device->createCommandEncoder({"GuiRhiMissingAtlasSetTest"});
    assert(rejectedEncoder);
    auto rejected = recordGuiRhiDrawPlan(*rejectedEncoder,
        GuiRhiDrawDesc{
            *colorTarget,
            *pipeline,
            guiPlan,
            {
                1,
                "GuiPipelineLayout",
                sizeof(GuiPushConstants),
                2,
                2,
                0,
                1,
                2,
                "GuiFrameSet",
                "GuiAtlasSet",
            },
            vertexBuffer.get(),
            indexBuffer.get(),
            uploadSource.get(),
            descriptorSet.get(),
        });
    assert(!rejected.success);
    assert(rejected.diagnostics.find("atlas set") != std::pmr::string::npos);

    auto encoder = device->createCommandEncoder({"GuiRhiDrawCommandTest"});
    assert(encoder);
    auto recorded = recordGuiRhiDrawPlan(*encoder,
        GuiRhiDrawDesc{
            *colorTarget,
            *pipeline,
            guiPlan,
            {
                1,
                "GuiPipelineLayout",
                sizeof(GuiPushConstants),
                2,
                2,
                0,
                1,
                2,
                "GuiFrameSet",
                "GuiAtlasSet",
            },
            vertexBuffer.get(),
            indexBuffer.get(),
            uploadSource.get(),
            descriptorSet.get(),
            atlasSet.get(),
        });

    assert(recorded.success);
    assert(recorded.diagnostics.empty());
    assert(recorded.framePlan.success);
    assert(recorded.framePlan.plan.passes.size() == 1u);
    assert(recorded.framePlan.plan.passes[0].descriptorSetCount == 2u);
    assert(recorded.framePlan.plan.passes[0].descriptorLayoutNameCount == 2u);
    assert(recorded.framePlan.plan.passes[0].descriptorLayoutNames[0]
        == std::pmr::string("GuiFrameSet"));
    assert(recorded.framePlan.plan.passes[0].descriptorLayoutNames[1]
        == std::pmr::string("GuiAtlasSet"));
    assert(recorded.descriptorSetBound);
    assert(recorded.backendOwnedUploadStaging);
    assert(recorded.recordedUploadCommandCount == 1u);
    assert(recorded.recordedDrawCommandCount == guiPlan.commands.size());
    assert(recorded.recordedThroughCommandEncoder);
    assert(!recorded.usedRemovedModuleCompatibility);
    assert(!recorded.nativeHandleExposedToRenderer);

    auto commandBuffer = encoder->finish();
    assert(commandBuffer);
    const auto submitted = device->queue().submit(*commandBuffer);
    assert(submitted.success);
    assert(submitted.submittedCommandCount == commandBuffer->commandCount());

    return 0;
}
