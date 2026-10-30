#include "editor/viewport/editor_scene_viewport_environment.h"

#include "core/math/quad_geometry.h"
#include "renderer/shader_package_reflection.h"

#include <cmath>

namespace arp::editor {

bool editorSceneViewportEnvironmentExecutable(const rhi::DeviceCapabilities& capabilities,
    const rhi::ShaderReflectionDesc& reflection) noexcept
{
    // Metal/WebGPU keep defined lowering but are closed until real execution is validated.
    return capabilities.backend == rhi::Backend::Vulkan && capabilities.core.graphics
        && evaluateEditorSceneViewportPipelineLowering(reflection, capabilities).executable();
}

bool EditorSceneViewportEnvironment::initialize(rhi::GpuDevice& device,
    const EditorSceneViewportShaderArtifacts& artifacts,
    core::pmr::String& diagnostics,
    EditorSceneViewportSurfaceProfile profile)
{
    if (!profile.valid()) {
        diagnostics = "ARP-EDITOR-ENVIRONMENT-SURFACE-PROFILE";
        return false;
    }
    if (device_ != nullptr && device_ != &device) {
        diagnostics = "ARP-EDITOR-ENVIRONMENT-DEVICE-MISMATCH";
        return false;
    }
    EditorSceneViewportShaderPackageResult package;
    auto& shaderPackage = package.package;
    shaderPackage.label = "EditorViewportEnvironment/2";
    shaderPackage.slangModulePath = artifacts.sourcePath;
    shaderPackage.artifacts = {core::pmr::String(artifacts.spirvPath),
        core::pmr::String(artifacts.mslPath),
        core::pmr::String(artifacts.wgslPath),
        core::pmr::String(artifacts.reflectionPath)};
    shaderPackage.entrypoints.push_back(
        {shaderPackage.slangModulePath, "vertexMain", rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {shaderPackage.slangModulePath, "fragmentMain", rhi::ShaderStage::Fragment});
    if (!renderer::hydrateShaderPackageReflectionFromArtifact(shaderPackage)) {
        diagnostics = "ARP-EDITOR-ENVIRONMENT-ARTIFACT-INVALID";
        return false;
    }
    const auto& reflection = shaderPackage.generatedArtifactSet->module().reflection;
    if (!validateEditorSceneViewportEnvironmentReflection(reflection)) {
        diagnostics = "ARP-EDITOR-ENVIRONMENT-REFLECTION-ABI-INVALID";
        return false;
    }
    if (device.backend() != device.capabilities().backend
        || !editorSceneViewportEnvironmentExecutable(
            device.capabilities(), shaderPackage.reflection)) {
        diagnostics = "ARP-EDITOR-ENVIRONMENT-EXECUTION-UNAVAILABLE";
        return false;
    }
    auto& contract = package.pipelineContract;
    contract.shaderReflection = shaderPackage.reflection;
    contract.pipelineLayout =
        rhi::buildPipelineLayoutDescFromShaderReflection(shaderPackage.reflection);
    if (!rhi::validatePipelineLayoutAgainstShaderReflection(
            contract.pipelineLayout, shaderPackage.reflection)
            .success) {
        diagnostics = "ARP-EDITOR-ENVIRONMENT-LAYOUT-INVALID";
        return false;
    }
    contract.pipeline =
        buildEditorSceneViewportEnvironmentPipeline(shaderPackage.slangModulePath, profile);
    contract.stateValidation = rhi::validateRenderPipelineState(contract.pipeline);
    auto result = createEditorSceneViewportDevicePipeline(device, package);
    if (!result.valid()) {
        diagnostics = result.backendDiagnostics;
        return false;
    }
    auto corners = device.createBuffer({"EditorEnvironmentCorners",
        sizeof(core::QuadTriangleTopology::corners),
        rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead});
    if (!corners
        || !device
            .writeBuffer(*corners,
                core::QuadTriangleTopology::corners.data(),
                sizeof(core::QuadTriangleTopology::corners))
            .success) {
        diagnostics = "ARP-EDITOR-ENVIRONMENT-VERTICES-FAILED";
        return false;
    }
    corners_ = std::move(corners);
    profile_ = profile;
    resources_ = std::move(result.resources);
    device_ = &device;
    return true;
}

EditorSceneViewportDrawExecutionResult EditorSceneViewportEnvironment::record(
    rhi::GpuDevice& device,
    rhi::GpuTexture& color,
    rhi::GpuTexture& depth,
    const EditorSceneViewportCamera& camera,
    bool gridPass,
    const EditorSceneViewportSky& sky)
{
    EditorSceneViewportDrawExecutionResult result;
    auto fail = [&](const char* message) {
        result.diagnostics = message;
        return std::move(result);
    };
    const auto extent = color.desc().extent;
    if (!valid() || device_ != &device || device.backend() != device.capabilities().backend
        || !editorSceneViewportEnvironmentExecutable(
            device.capabilities(), resources_.package.reflection))
        return fail("ARP-EDITOR-ENVIRONMENT-EXECUTION-UNAVAILABLE");
    if (extent.width == 0u || extent.height == 0u || extent.depth != 1u
        || depth.desc().extent.depth != 1u || extent.width != depth.desc().extent.width
        || extent.height != depth.desc().extent.height || color.desc().format != profile_.color
        || depth.desc().format != profile_.depth || color.desc().sampleCount != 1u
        || depth.desc().sampleCount != 1u
        || (color.desc().usage & rhi::TextureUsage::RenderAttachment) == rhi::TextureUsage::None
        || (depth.desc().usage & rhi::TextureUsage::RenderAttachment) == rhi::TextureUsage::None)
        return fail("ARP-EDITOR-ENVIRONMENT-ATTACHMENTS-INVALID");
    const auto push = buildEditorSceneViewportEnvironmentPush(camera,
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        gridPass,
        sky);
    auto encoder = device.createCommandEncoder({gridPass ? "EditorGroundGrid" : "EditorSkybox"});
    if (!encoder)
        return fail("ARP-EDITOR-ENVIRONMENT-ENCODER-FAILED");
    if (!encoder->transition(color, color.state(), rhi::ResourceState::ColorAttachment).success
        || !encoder->transition(depth, depth.state(), rhi::ResourceState::DepthAttachment).success)
        return fail("ARP-EDITOR-ENVIRONMENT-TRANSITION-FAILED");
    rhi::RenderPassDesc desc;
    desc.label = gridPass ? "EditorGroundGrid" : "EditorSkybox";
    desc.colorTargetName = color.desc().label;
    desc.colorAttachments.push_back({desc.label,
        color.id(),
        color.desc().format,
        gridPass ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
        rhi::StoreOp::Store});
    desc.hasDepthStencilAttachment = true;
    desc.depthStencilAttachment = {"EditorEnvironmentDepth",
        depth.id(),
        depth.desc().format,
        gridPass ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::DontCare,
        1.0f,
        0u,
        false,
        false};
    auto pass = encoder->beginRenderPass(desc);
    if (!pass)
        return fail("ARP-EDITOR-ENVIRONMENT-PASS-FAILED");
    const bool recorded = pass->setPipeline(*resources_.pipeline).success
        && pass->bindVertexBuffer(0u, *corners_).success
        && pass->setViewport({0,
                                 0,
                                 static_cast<float>(extent.width),
                                 static_cast<float>(extent.height),
                                 0,
                                 1})
               .success
        && pass->setScissor({0, 0, extent.width, extent.height}).success
        && pass->pushConstants(rhi::ShaderStage::Fragment, 0u, sizeof(push), &push).success
        && pass->draw(core::QuadTriangleTopology::vertexCount, 1u, 0u, 0u).success;
    const auto ended = pass->end();
    if (!recorded || !ended.success)
        return fail("ARP-EDITOR-ENVIRONMENT-DRAW-FAILED");
    if (gridPass
        && !encoder->transition(color, color.state(), rhi::ResourceState::ShaderRead).success)
        return fail("ARP-EDITOR-ENVIRONMENT-SAMPLE-TRANSITION-FAILED");
    result.commandBuffer = encoder->finish();
    if (!result.commandBuffer)
        return fail("ARP-EDITOR-ENVIRONMENT-FINISH-FAILED");
    result.drawCount = 1u;
    return result;
}

} // namespace arp::editor
