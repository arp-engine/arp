#include "editor/viewport/editor_scene_viewport_environment.h"
#include "shader/shader_pipeline_reflection_validator.h"

#include <array>

namespace arp::editor {

rhi::RenderPipelineDesc buildEditorSceneViewportEnvironmentPipeline(
    core::StringView sourcePath, EditorSceneViewportSurfaceProfile profile)
{
    rhi::RenderPipelineDesc pipeline;
    pipeline.label = "EditorViewportSkyAndGrid";
    pipeline.vertexShader = {core::pmr::String(sourcePath), "vertexMain", rhi::ShaderStage::Vertex};
    pipeline.fragmentShader = {
        core::pmr::String(sourcePath), "fragmentMain", rhi::ShaderStage::Fragment};
    pipeline.colorFormat = profile.color;
    pipeline.depthFormat = profile.depth;
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
    auto& vertices = pipeline.vertexBuffers.emplace_back();
    vertices.strideBytes = sizeof(core::Vec2);
    vertices.attributes.push_back({0u, 0u, rhi::VertexFormat::Float32x2});
    return pipeline;
}

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
    if (!stages || *stages != rhi::ShaderStage::Fragment)
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

} // namespace arp::editor
