#include "core/memory/cpu_allocator.h"
#include "renderer/gui/gui_draw_plan.h"
#include "renderer/gui/gui_rhi_draw_commands.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <string>

namespace {

arp::rhi::ShaderPackageDesc makeGuiShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "GuiShaderPackage";
    shaderPackage.slangModulePath = "gui.slang";
    shaderPackage.entrypoints.push_back({"gui.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back({"gui.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.artifacts.spirvPath = "gui.spv";
    shaderPackage.artifacts.mslPath = "gui.metal";
    shaderPackage.artifacts.reflectionPath = "gui.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = sizeof(arp::renderer::GuiPushConstants);
    return shaderPackage;
}

} // namespace

int main()
{
    using namespace arp::renderer;
    using namespace arp::rhi;

    arp::gui::DrawData DrawData;
    DrawData.quads.push_back(arp::gui::DrawQuad{
        100,
        arp::gui::NodeType::Panel,
        {0.0f, 0.0f, 320.0f, 48.0f},
    });
    DrawData.textQuads.push_back(arp::gui::DrawTextQuad{
        101,
        {12.0f, 14.0f, 120.0f, 18.0f},
        {0.9f, 0.95f, 1.0f, 1.0f},
    });
    const auto guiPlan = buildGuiDrawPlan(DrawData, {640, 360});
    assert(guiPlan.success);
    assert(guiPlan.commands.size() == 2u);

    const auto drawPlanDesc = buildGuiRhiDrawPlanDesc(guiPlan,
        GuiRhiFramePlanDesc{
            1,
            "GuiPipelineLayout",
            sizeof(GuiPushConstants),
        });
    assert(drawPlanDesc.name == std::pmr::string("Gui"));
    assert(drawPlanDesc.workKind == RendererRhiWorkKind::Gui);
    assert(drawPlanDesc.frameSlot == 1u);
    assert(drawPlanDesc.drawCommandCount == guiPlan.commands.size());
    assert(drawPlanDesc.pushConstantSizeBytes == sizeof(GuiPushConstants));
    assert(drawPlanDesc.pipelineLayoutName == std::pmr::string("GuiPipelineLayout"));
    assert(drawPlanDesc.expectedPipelineLayoutName == std::pmr::string("GuiPipelineLayout"));
    assert(drawPlanDesc.descriptorSetCount == 0u);

    auto device = createNullDevice();
    auto colorTarget = device->createTexture({
        "GuiBackbuffer",
        {640, 360, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(colorTarget);
    auto guiIndexBuffer = device->createBuffer({
        "GuiQuadIndices",
        6u * sizeof(std::uint32_t),
        ResourceUsage::IndexBuffer,
        MemoryUsage::CpuToGpu,
        ResourceState::ShaderRead,
    });
    assert(guiIndexBuffer);

    PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(GuiPushConstants)});
    auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDesc);
    assert(pipelineLayout);

    auto shaderPackage = makeGuiShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);

    RenderPipelineDesc pipelineDesc;
    pipelineDesc.label = "GuiPipeline";
    pipelineDesc.layout = pipelineLayout.get();
    pipelineDesc.shaderPackage = &shaderPackage;
    pipelineDesc.vertexShader = {"gui.slang", "vsMain", ShaderStage::Vertex};
    pipelineDesc.fragmentShader = {"gui.slang", "fsMain", ShaderStage::Fragment};
    pipelineDesc.colorFormat = TextureFormat::Bgra8Unorm;
    auto pipeline = device->createRenderPipeline(pipelineDesc);
    assert(pipeline);
    assert(!pipeline->nativeHandleExposedToUpperLayer());

    auto encoder = device->createCommandEncoder({"GuiFirstRhiPass"});
    auto recorded = recordGuiRhiDrawPlan(*encoder,
        GuiRhiDrawDesc{
            *colorTarget,
            *pipeline,
            guiPlan,
            {
                1,
                "GuiPipelineLayout",
                sizeof(GuiPushConstants),
            },
            nullptr,
            guiIndexBuffer.get(),
        });

    assert(recorded.success);
    assert(recorded.diagnostics.empty());
    assert(recorded.framePlan.success);
    assert(recorded.framePlan.plan.frameSlot == 1u);
    assert(recorded.framePlan.plan.passes.size() == 1u);
    assert(recorded.framePlan.plan.passes[0].name == std::pmr::string("Gui"));
    assert(recorded.framePlan.plan.passes[0].drawCommandCount == guiPlan.commands.size());
    assert(recorded.framePlan.plan.passes[0].pushConstantSizeBytes == sizeof(GuiPushConstants));
    assert(!recorded.framePlan.nativeHandleExposedToUpperLayer);
    assert(!recorded.framePlan.backendSpecificBranchingRequired);
    assert(recorded.recordedThroughCommandEncoder);
    assert(recorded.removedModuleCompatibilityExplicit);
    assert(!recorded.usedRemovedModuleCompatibility);
    assert(!recorded.nativeHandleExposedToRenderer);
    assert(recorded.recordedDrawCommandCount == guiPlan.commands.size());
    assert(recorded.recordedPushConstantCount == guiPlan.commands.size());
    assert(recorded.recordedCommandCount == 8u);

    auto commandBuffer = encoder->finish();
    assert(commandBuffer);
    assert(commandBuffer->commandCount() == recorded.recordedCommandCount);
    const auto submitted = device->queue().submit(*commandBuffer);
    assert(submitted.success);
    assert(submitted.submittedCommandCount == recorded.recordedCommandCount);

    auto invalidRecorded =
        recordGuiRhiDrawPlan(*device->createCommandEncoder({"InvalidGuiFirstRhiPass"}),
            GuiRhiDrawDesc{
                *colorTarget,
                *pipeline,
                GuiDrawPlan{},
                {
                    0,
                    "GuiPipelineLayout",
                    sizeof(GuiPushConstants),
                },
            });
    assert(!invalidRecorded.success);
    assert(invalidRecorded.diagnostics.find("GUI draw data is empty") != std::pmr::string::npos
        || invalidRecorded.diagnostics.find("empty") != std::pmr::string::npos);
    assert(!invalidRecorded.usedRemovedModuleCompatibility);

    return 0;
}
