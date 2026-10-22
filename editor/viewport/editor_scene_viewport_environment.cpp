#include "editor/viewport/editor_scene_viewport_environment.h"

#include "renderer/shader_package_reflection.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

#include <array>
#include <cmath>

namespace arp::editor {

EditorSceneViewportEnvironmentPush buildEditorSceneViewportEnvironmentPush(
    const EditorSceneViewportCamera& camera,
    float aspect,
    bool gridPass,
    const EditorSceneViewportSky& sky) noexcept
{
    const auto vp = camera.viewProjectionMatrix(aspect);
    const auto inverse = core::inverse(vp);
    const auto eye = camera.position();
    // Near-plane points avoid loss of precision at the 10 km far plane.
    const auto origin = core::transformPoint(inverse, {-1.0f, -1.0f, 0.0f});
    const auto horizontal = core::transformPoint(inverse, {1.0f, -1.0f, 0.0f}) - origin;
    const auto vertical = core::transformPoint(inverse, {-1.0f, 1.0f, 0.0f}) - origin;
    const auto ray = origin - eye;
    return {{eye.x, eye.y, eye.z, 1.0f},
        {ray.x, ray.y, ray.z, 0.0f},
        {horizontal.x, horizontal.y, horizontal.z, 0.0f},
        {vertical.x, vertical.y, vertical.z, 0.0f},
        {vp.m[8], vp.m[9], vp.m[10], vp.m[11]},
        {vp.m[12], vp.m[13], vp.m[14], vp.m[15]},
        {1.0f, 10.0f, 100.0f, gridPass ? 1.0f : 0.0f},
        {sky.zenith.x, sky.zenith.y, sky.zenith.z, sky.domeRadius},
        {sky.horizon.x, sky.horizon.y, sky.horizon.z, sky.present ? 1.0f : 0.0f},
        {sky.sunDirection.x, sky.sunDirection.y, sky.sunDirection.z, 0.0f},
        {sky.sunColor.x, sky.sunColor.y, sky.sunColor.z, 0.0f}};
}

bool validateEditorSceneViewportEnvironmentReflection(
    const shader::ShaderReflection& reflection) noexcept
{
    constexpr std::array<std::string_view, 11> members{"camera",
        "rayOrigin",
        "rayHorizontal",
        "rayVertical",
        "depthNumerator",
        "depthDenominator",
        "grid",
        "zenith",
        "horizon",
        "sunDirection",
        "sunColor"};
    if (reflection.reflectionContractVersion != shader::kShaderReflectionContractVersion
        || reflection.entryPoints.size() != 2u || !reflection.bindings.empty()
        || !reflection.vertexAttributes.empty()
        || reflection.pushConstantSizeBytes != sizeof(EditorSceneViewportEnvironmentPush)
        || reflection.pushConstantMembers.size() != members.size()
        || reflection.pushConstantContractVersion != "AtomicFullBlockPush/1"
        || !shader::ShaderPipelineReflectionValidator::validatePushConstantMembers(reflection)
            .success)
        return false;
    const auto stages = rhi::parseShaderReflectionPushConstantStages(reflection.pushConstantStages);
    if (!stages || *stages != (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment))
        return false;
    bool vertex = false, fragment = false;
    for (const auto& entry : reflection.entryPoints) {
        vertex |= entry.name == "vertexMain" && entry.stage == shader::ShaderStage::Vertex;
        fragment |= entry.name == "fragmentMain" && entry.stage == shader::ShaderStage::Fragment;
    }
    if (!vertex || !fragment)
        return false;
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto& member = reflection.pushConstantMembers[i];
        if (member.name != members[i] || member.offset != i * 16u || member.size != 16u
            || member.vecSize != 4u || member.scalarWidth != 32u || member.scalarType != "float"
            || !member.arrayDims.empty() || member.matrixRows > 1u || member.matrixCols > 1u)
            return false;
    }
    return true;
}

bool editorSceneViewportEnvironmentExecutable(const rhi::DeviceCapabilities& capabilities,
    const rhi::ShaderReflectionDesc& reflection) noexcept
{
    // Metal/WebGPU keep defined lowering but are closed until real execution is validated.
    return capabilities.backend == rhi::Backend::Vulkan && capabilities.core.graphics
        && evaluateEditorSceneViewportPipelineLowering(reflection, capabilities).executable();
}

bool EditorSceneViewportEnvironment::initialize(rhi::GpuDevice& device,
    const EditorSceneViewportShaderArtifacts& artifacts,
    core::pmr::String& diagnostics)
{
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
    const auto reflection =
        shader::ShaderReflectionCache::loadFromFile(core::pmr::String(artifacts.reflectionPath));
    if (!reflection.success
        || !validateEditorSceneViewportEnvironmentReflection(reflection.module.reflection)) {
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
    auto& pipeline = contract.pipeline;
    pipeline.label = "EditorViewportSkyAndGrid";
    pipeline.vertexShader = {shaderPackage.slangModulePath, "vertexMain", rhi::ShaderStage::Vertex};
    pipeline.fragmentShader = {
        shaderPackage.slangModulePath, "fragmentMain", rhi::ShaderStage::Fragment};
    pipeline.colorFormat = rhi::TextureFormat::Rgba16Float;
    pipeline.depthFormat = rhi::TextureFormat::D32Float;
    auto& color = pipeline.colorTargets.emplace_back();
    color.format = pipeline.colorFormat;
    color.blendEnabled = true;
    color.colorBlend = {
        rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha, rhi::BlendOp::Add};
    color.alphaBlend = {
        rhi::BlendFactor::One, rhi::BlendFactor::OneMinusSrcAlpha, rhi::BlendOp::Add};
    color.writeMask = rhi::ColorWriteMask::All;
    color.resolveTargetRequired = false;
    pipeline.depthStencil.format = pipeline.depthFormat;
    pipeline.depthStencil.depthTestEnabled = true;
    pipeline.depthStencil.depthWriteEnabled = false;
    pipeline.depthStencil.depthCompare = rhi::CompareOp::LessEqual;
    pipeline.depthStencil.stencilEnabled = false;
    pipeline.depthStencil.front = {
        rhi::StencilOp::Keep, rhi::StencilOp::Keep, rhi::StencilOp::Keep, rhi::CompareOp::Always};
    pipeline.depthStencil.back = pipeline.depthStencil.front;
    pipeline.depthStencil.stencilReadMask = 0xffu;
    pipeline.depthStencil.stencilWriteMask = 0xffu;
    pipeline.depthStencil.depthBiasEnabled = false;
    pipeline.depthStencil.depthBiasConstantFactor = 0;
    pipeline.depthStencil.depthBiasClamp = 0;
    pipeline.depthStencil.depthBiasSlopeFactor = 0;
    pipeline.cullMode = rhi::CullMode::None;
    pipeline.frontFace = rhi::FrontFace::CounterClockwise;
    pipeline.topology = rhi::PrimitiveTopology::TriangleList;
    pipeline.multisample.sampleCount = 1u;
    pipeline.multisample.sampleMask = 0xffffffffffffffffull;
    pipeline.multisample.alphaToCoverageEnabled = false;
    pipeline.hasViewport = false;
    pipeline.hasScissor = false;
    pipeline.dynamicViewportScissor = true;
    pipeline.subpassIndex = 0u;
    contract.stateValidation = rhi::validateRenderPipelineState(pipeline);
    auto result = createEditorSceneViewportDevicePipeline(device, package);
    if (!result.valid()) {
        diagnostics = result.backendDiagnostics;
        return false;
    }
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
        || extent.height != depth.desc().extent.height
        || color.desc().format != rhi::TextureFormat::Rgba16Float
        || depth.desc().format != rhi::TextureFormat::D32Float || color.desc().sampleCount != 1u
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
        && pass->setViewport({0,
                                 0,
                                 static_cast<float>(extent.width),
                                 static_cast<float>(extent.height),
                                 0,
                                 1})
               .success
        && pass->setScissor({0, 0, extent.width, extent.height}).success
        && pass->pushConstants(
                   rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0u, sizeof(push), &push)
               .success
        && pass->draw(3u, 1u, 0u, 0u).success;
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
