#include "editor/viewport/editor_scene_viewport_sprite_contract.h"

#include <algorithm>
#include <array>
#include <limits>

namespace arp::editor {

bool validateEditorSceneViewportSpriteReflection(const shader::ShaderReflection& reflection)
{
    if (reflection.entryPoints.size() != 2u || reflection.vertexAttributes.size() != 7u
        || reflection.bindings.size() != 2u || reflection.pushConstantSizeBytes != 0u
        || !reflection.pushConstantMembers.empty())
        return false;
    bool vertex = false;
    bool fragment = false;
    for (const auto& entry : reflection.entryPoints) {
        vertex |= entry.name == "vertexMain" && entry.stage == shader::ShaderStage::Vertex;
        fragment |= entry.name == "fragmentMain" && entry.stage == shader::ShaderStage::Fragment;
    }
    if (!vertex || !fragment)
        return false;
    constexpr std::array<const char*, 7> names{
        "origin", "axisX", "axisY", "uv", "color", "parameters", "corner"};
    constexpr std::array<const char*, 7> semantics{
        "TEXCOORD0", "TEXCOORD1", "TEXCOORD2", "TEXCOORD3", "TEXCOORD4", "TEXCOORD5", "TEXCOORD6"};
    for (std::uint32_t location = 0u; location < names.size(); ++location) {
        const auto found = std::find_if(reflection.vertexAttributes.begin(),
            reflection.vertexAttributes.end(),
            [&](const auto& attribute) {
                return attribute.location == location;
            });
        if (found == reflection.vertexAttributes.end() || found->name != names[location]
            || found->semantic != semantics[location]
            || found->format
                != (location == 6 ? shader::VertexAttributeFormat::Float2
                                  : shader::VertexAttributeFormat::Float4))
            return false;
    }
    const auto texture = std::find_if(
        reflection.bindings.begin(), reflection.bindings.end(), [](const auto& binding) {
            return binding.name == "SpriteAtlas";
        });
    const auto sampler = std::find_if(
        reflection.bindings.begin(), reflection.bindings.end(), [](const auto& binding) {
            return binding.name == "SpriteSampler";
        });
    return texture != reflection.bindings.end() && sampler != reflection.bindings.end()
        && texture->type == shader::BindingType::SampledTexture && texture->set == 0u
        && texture->binding == 0u && texture->arrayCount == 1u
        && texture->stageMask == static_cast<std::uint32_t>(rhi::ShaderStage::Fragment)
        && sampler->type == shader::BindingType::Sampler && sampler->set == 0u
        && sampler->binding == 1u && sampler->arrayCount == 1u
        && sampler->stageMask == static_cast<std::uint32_t>(rhi::ShaderStage::Fragment);
}

rhi::RenderPipelineDesc buildEditorSceneViewportSpritePipeline(
    std::string_view source, EditorSceneViewportSurfaceProfile profile)
{
    rhi::RenderPipelineDesc pipeline;
    pipeline.label = "ARP Editor Viewport Sprite";
    pipeline.vertexShader = {core::pmr::String(source), "vertexMain", rhi::ShaderStage::Vertex};
    pipeline.fragmentShader = {
        core::pmr::String(source), "fragmentMain", rhi::ShaderStage::Fragment};
    pipeline.colorFormat = profile.color;
    pipeline.depthFormat = profile.depth;
    pipeline.hasViewport = false;
    pipeline.hasScissor = false;
    pipeline.dynamicViewportScissor = true;
    pipeline.cullMode = rhi::CullMode::None;
    pipeline.frontFace = rhi::FrontFace::CounterClockwise;
    pipeline.topology = rhi::PrimitiveTopology::TriangleList;
    pipeline.multisample.sampleCount = 1u;
    pipeline.multisample.sampleMask = std::numeric_limits<std::uint64_t>::max();
    pipeline.multisample.alphaToCoverageEnabled = false;
    auto& color = pipeline.colorTargets.emplace_back();
    color.format = profile.color;
    color.blendEnabled = true;
    color.colorBlend = {
        rhi::BlendFactor::One, rhi::BlendFactor::OneMinusSrcAlpha, rhi::BlendOp::Add};
    color.alphaBlend = color.colorBlend;
    color.writeMask = rhi::ColorWriteMask::All;
    pipeline.depthStencil.format = profile.depth;
    pipeline.depthStencil.depthTestEnabled = true;
    pipeline.depthStencil.depthWriteEnabled = false;
    pipeline.depthStencil.depthCompare = rhi::CompareOp::LessEqual;
    pipeline.depthStencil.stencilEnabled = false;
    pipeline.depthStencil.front = {
        rhi::StencilOp::Keep, rhi::StencilOp::Keep, rhi::StencilOp::Keep, rhi::CompareOp::Always};
    pipeline.depthStencil.back = pipeline.depthStencil.front;
    pipeline.depthStencil.stencilReadMask = 0xffu;
    pipeline.depthStencil.stencilWriteMask = 0u;
    pipeline.depthStencil.depthBiasEnabled = false;
    pipeline.depthStencil.depthBiasConstantFactor = 0.0f;
    pipeline.depthStencil.depthBiasClamp = 0.0f;
    pipeline.depthStencil.depthBiasSlopeFactor = 0.0f;
    auto& stream = pipeline.vertexBuffers.emplace_back();
    stream.slot = 0u;
    stream.strideBytes = sizeof(scene2d::SpriteInstance);
    stream.inputRate = rhi::VertexInputRate::Instance;
    for (std::uint32_t location = 0u; location < 6u; ++location)
        stream.attributes.push_back({location,
            location * static_cast<std::uint32_t>(sizeof(core::Vec4)),
            rhi::VertexFormat::Float32x4});
    auto& corners = pipeline.vertexBuffers.emplace_back();
    corners.slot = 1;
    corners.strideBytes = sizeof(core::Vec2);
    corners.attributes.push_back({6, 0, rhi::VertexFormat::Float32x2});
    return pipeline;
}

} // namespace arp::editor
