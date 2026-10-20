#include "scene_renderer.h"

#include "assets/glb_static_mesh_importer.h"
#include "core/math.h"
#include "core/yaml_contract.h"
#include "hud_renderer.h"
#include "renderer/mesh/gpu_skinning_contract.h"
#include "renderer/renderer_common.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace arp::build_a_queen {
namespace {

constexpr std::array<core::Vec4, 5> kStyleColors{{
    {0.92f, 0.18f, 0.42f, 1.0f},
    {0.12f, 0.62f, 0.84f, 1.0f},
    {0.44f, 0.76f, 0.18f, 1.0f},
    {0.52f, 0.30f, 0.84f, 1.0f},
    {0.96f, 0.64f, 0.12f, 1.0f},
}};
constexpr core::Vec4 kTrackColor{0.98f, 0.89f, 0.94f, 1.0f};
constexpr core::Vec4 kTrackEdgeColor{0.86f, 0.42f, 0.66f, 1.0f};
constexpr core::Vec4 kWhite{1.0f, 0.98f, 1.0f, 1.0f};
constexpr core::Vec3 kLightDirection{0.42f, 0.78f, -0.46f};
constexpr float kPi = 3.14159265358979323846f;

renderer::RendererResult fail(const char* diagnostic)
{
    return {false, diagnostic};
}

core::Vec3 add(core::Vec3 lhs, core::Vec3 rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

core::Vec3 multiply(core::Vec3 value, float scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

} // namespace

SceneRenderer::FrameSlot::FrameSlot()
    : playerAppearanceVertices(&core::defaultCpuMemoryResource()),
      targetAppearanceVertices(&core::defaultCpuMemoryResource())
{}

SceneRenderer::SceneRenderer(rhi::GpuDevice& device) : device_(device)
{
    slots_.reserve(kFrameSlots);
}

SceneRenderer::~SceneRenderer()
{
    for (auto& slot : slots_)
        if (slot.pending)
            slot.completion->hostWait(slot.submittedValue, UINT64_MAX);
}

renderer::RendererResult SceneRenderer::create(const core::pmr::String& shaderPath,
    const core::pmr::String& characterDirectory,
    rhi::TextureFormat colorFormat,
    rhi::Extent2D extent)
{
    if (pipeline_ || shaderPath.empty() || colorFormat == rhi::TextureFormat::Unknown
        || extent.width == 0 || extent.height == 0)
        return fail("ARP-QUEEN-3D-RENDERER-CONFIG");
    if (device_.backend() == rhi::Backend::Null)
        return fail("ARP-QUEEN-3D-NULL-VALIDATOR-ONLY");
    const auto content = createContent(characterDirectory);
    if (!content.success)
        return content;
    const auto capabilities = device_.capabilities();
    if (!capabilities.core.graphics || !capabilities.features.timelineSync)
        return fail("ARP-QUEEN-3D-EXECUTION-CAPABILITY");

    colorFormat_ = colorFormat;
    rhi::DescriptorLayoutDesc shadowReceiverLayout;
    shadowReceiverLayout.name = "QueenShadowReceiverSet";
    rhi::DescriptorBindingDesc shadowTextureBinding{
        0u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment};
    shadowTextureBinding.depthTexture = true;
    shadowReceiverLayout.bindings.push_back(shadowTextureBinding);
    rhi::DescriptorBindingDesc shadowSamplerBinding{
        1u, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment};
    shadowSamplerBinding.sampledTextureFilterable = false;
    shadowReceiverLayout.bindings.push_back(shadowSamplerBinding);
    rhi::PipelineLayoutDesc layoutDesc;
    extent_ = extent;
    layoutDesc.descriptorLayouts.push_back(shadowReceiverLayout);
    layoutDesc.pushConstantRanges.push_back(
        {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0, sizeof(core::Mat4) * 2u});
    layoutDesc.descriptorSets.push_back({0u, shadowReceiverLayout.name});
    layout_ = device_.createPipelineLayout(layoutDesc);
    if (!layout_)
        return fail("ARP-QUEEN-3D-PIPELINE-LAYOUT");

    shaderPackage_ = renderer::detail::makeGraphicsShaderPackage(
        core::pmr::String("BuildAQueen3D", &core::defaultCpuMemoryResource()),
        shaderPath,
        sizeof(core::Mat4) * 2u);
    if (!shaderPackage_.generatedReflectionHydrated
        || shaderPackage_.reflection.pushConstantByteCount != sizeof(core::Mat4) * 2u)
        return fail("ARP-QUEEN-3D-SHADER-REFLECTION");
    shaderPackage_.reflection.descriptorBindings.push_back({0u,
        0u,
        rhi::DescriptorType::SampledTexture,
        rhi::ShaderStage::Fragment,
        1u,
        UINT32_MAX,
        true});
    shaderPackage_.reflection.descriptorBindings.push_back(
        {0u, 1u, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment, 1u});

    auto pipelineDesc = renderer::detail::makeRenderPipelineDesc(
        core::pmr::String("BuildAQueen3D", &core::defaultCpuMemoryResource()),
        *layout_,
        shaderPackage_,
        colorFormat_,
        extent_);
    pipelineDesc.hasViewport = false;
    pipelineDesc.hasScissor = false;
    pipelineDesc.dynamicViewportScissor = true;
    pipelineDesc.cullMode = rhi::CullMode::None;
    pipelineDesc.frontFace = rhi::FrontFace::CounterClockwise;
    pipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.depthFormat = rhi::TextureFormat::D32Float;
    pipelineDesc.depthStencil = {rhi::TextureFormat::D32Float,
        true,
        true,
        rhi::CompareOp::LessEqual,
        false,
        {},
        {},
        0xffu,
        0xffu,
        false,
        0.0f,
        0.0f,
        0.0f};
    rhi::ColorTargetState color;
    color.format = colorFormat_;
    color.blendEnabled = false;
    color.writeMask = rhi::ColorWriteMask::All;
    pipelineDesc.colorTargets.push_back(color);
    rhi::VertexBufferLayoutDesc stream;
    stream.slot = 0;
    stream.strideBytes = sizeof(Vertex);
    stream.attributes.push_back({0, offsetof(Vertex, position), rhi::VertexFormat::Float32x3});
    stream.attributes.push_back({1, offsetof(Vertex, normal), rhi::VertexFormat::Float32x3});
    stream.attributes.push_back({2, offsetof(Vertex, color), rhi::VertexFormat::Float32x4});
    pipelineDesc.vertexBuffers.push_back(std::move(stream));
    pipeline_ = device_.createRenderPipeline(pipelineDesc);
    if (!pipeline_)
        return fail("ARP-QUEEN-3D-PIPELINE");

    core::pmr::String skyShaderPath = shaderPath;
    const auto sceneShaderName = skyShaderPath.find("queen_scene");
    if (sceneShaderName == core::pmr::String::npos)
        return fail("ARP-QUEEN-3D-SKY-SHADER-PATH");
    skyShaderPath.replace(sceneShaderName, 11u, "queen_sky");
    rhi::PipelineLayoutDesc skyLayoutDesc;
    skyLayout_ = device_.createPipelineLayout(skyLayoutDesc);
    if (!skyLayout_)
        return fail("ARP-QUEEN-3D-SKY-PIPELINE-LAYOUT");
    skyShaderPackage_ = renderer::detail::makeGraphicsShaderPackage(
        core::pmr::String("BuildAQueenSky", &core::defaultCpuMemoryResource()), skyShaderPath, 0u);
    if (!skyShaderPackage_.generatedReflectionHydrated
        || skyShaderPackage_.reflection.pushConstantByteCount != 0u
        || !skyShaderPackage_.reflection.descriptorBindings.empty())
        return fail("ARP-QUEEN-3D-SKY-SHADER-REFLECTION");
    auto skyPipelineDesc = renderer::detail::makeRenderPipelineDesc(
        core::pmr::String("BuildAQueenSky", &core::defaultCpuMemoryResource()),
        *skyLayout_,
        skyShaderPackage_,
        colorFormat_,
        extent_);
    skyPipelineDesc.hasViewport = false;
    skyPipelineDesc.hasScissor = false;
    skyPipelineDesc.dynamicViewportScissor = true;
    skyPipelineDesc.cullMode = rhi::CullMode::None;
    skyPipelineDesc.frontFace = rhi::FrontFace::CounterClockwise;
    skyPipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
    skyPipelineDesc.depthFormat = rhi::TextureFormat::D32Float;
    skyPipelineDesc.depthStencil = {rhi::TextureFormat::D32Float,
        false,
        false,
        rhi::CompareOp::Always,
        false,
        {},
        {},
        0xffu,
        0xffu,
        false,
        0.0f,
        0.0f,
        0.0f};
    skyPipelineDesc.colorTargets.push_back(color);
    skyPipeline_ = device_.createRenderPipeline(skyPipelineDesc);
    if (!skyPipeline_)
        return fail("ARP-QUEEN-3D-SKY-PIPELINE");

    core::pmr::String skinnedShaderPath = shaderPath;
    const auto shaderName = skinnedShaderPath.find("queen_scene");
    if (shaderName == core::pmr::String::npos)
        return fail("ARP-QUEEN-3D-SKINNED-SHADER-PATH");
    skinnedShaderPath.replace(shaderName, 11u, "queen_skinned");
    rhi::DescriptorLayoutDesc skinningLayoutDesc;
    skinningLayoutDesc.name = "QueenSkinningSet";
    skinningLayoutDesc.bindings.push_back(
        {0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Vertex});
    rhi::PipelineLayoutDesc skinnedPipelineLayoutDesc;
    skinnedPipelineLayoutDesc.descriptorLayouts.push_back(skinningLayoutDesc);
    skinnedPipelineLayoutDesc.descriptorSets.push_back({0u, skinningLayoutDesc.name});
    skinnedPipelineLayoutDesc.descriptorLayouts.push_back(shadowReceiverLayout);
    skinnedPipelineLayoutDesc.descriptorSets.push_back({1u, shadowReceiverLayout.name});
    skinnedPipelineLayoutDesc.pushConstantRanges.push_back(
        {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0u, 160u});
    skinnedLayout_ = device_.createPipelineLayout(skinnedPipelineLayoutDesc);
    if (!skinnedLayout_)
        return fail("ARP-QUEEN-3D-SKINNED-PIPELINE-LAYOUT");
    skinnedShaderPackage_ = renderer::detail::makeGraphicsShaderPackage(
        core::pmr::String("BuildAQueenSkinned", &core::defaultCpuMemoryResource()),
        skinnedShaderPath,
        160u);
    if (!skinnedShaderPackage_.generatedReflectionHydrated
        || skinnedShaderPackage_.reflection.pushConstantByteCount != 160u)
        return fail("ARP-QUEEN-3D-SKINNED-SHADER-REFLECTION");
    skinnedShaderPackage_.reflection.descriptorBindings.push_back(
        {0u, 0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Vertex, 1u});
    skinnedShaderPackage_.reflection.descriptorBindings.push_back({1u,
        0u,
        rhi::DescriptorType::SampledTexture,
        rhi::ShaderStage::Fragment,
        1u,
        UINT32_MAX,
        true});
    skinnedShaderPackage_.reflection.descriptorBindings.push_back(
        {1u, 1u, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment, 1u});
    auto skinnedPipelineDesc = renderer::detail::makeRenderPipelineDesc(
        core::pmr::String("BuildAQueenSkinned", &core::defaultCpuMemoryResource()),
        *skinnedLayout_,
        skinnedShaderPackage_,
        colorFormat_,
        extent_);
    skinnedPipelineDesc.hasViewport = false;
    skinnedPipelineDesc.hasScissor = false;
    skinnedPipelineDesc.dynamicViewportScissor = true;
    skinnedPipelineDesc.cullMode = rhi::CullMode::Back;
    skinnedPipelineDesc.frontFace = rhi::FrontFace::CounterClockwise;
    skinnedPipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
    skinnedPipelineDesc.depthFormat = rhi::TextureFormat::D32Float;
    skinnedPipelineDesc.depthStencil = pipelineDesc.depthStencil;
    skinnedPipelineDesc.colorTargets.push_back(color);
    rhi::VertexBufferLayoutDesc skinnedStream;
    skinnedStream.slot = 0;
    skinnedStream.strideBytes = sizeof(Vertex);
    skinnedStream.attributes.push_back(
        {0, offsetof(Vertex, position), rhi::VertexFormat::Float32x3});
    skinnedStream.attributes.push_back({1, offsetof(Vertex, normal), rhi::VertexFormat::Float32x3});
    skinnedStream.attributes.push_back({2, offsetof(Vertex, color), rhi::VertexFormat::Float32x4});
    skinnedStream.attributes.push_back({3, offsetof(Vertex, joints), rhi::VertexFormat::Float32x4});
    skinnedStream.attributes.push_back(
        {4, offsetof(Vertex, weights), rhi::VertexFormat::Float32x4});
    skinnedPipelineDesc.vertexBuffers.push_back(std::move(skinnedStream));
    skinnedPipeline_ = device_.createRenderPipeline(skinnedPipelineDesc);
    if (!skinnedPipeline_)
        return fail("ARP-QUEEN-3D-SKINNED-PIPELINE");

    core::pmr::String skinnedShadowPath = skinnedShaderPath;
    const auto skinnedShadowName = skinnedShadowPath.find("queen_skinned");
    if (skinnedShadowName == core::pmr::String::npos)
        return fail("ARP-QUEEN-SKINNED-SHADOW-SHADER-PATH");
    skinnedShadowPath.replace(skinnedShadowName, 13u, "queen_skinned_shadow");
    skinnedShadowShaderPackage_ = renderer::detail::makeGraphicsShaderPackage(
        core::pmr::String("BuildAQueenSkinnedShadow", &core::defaultCpuMemoryResource()),
        skinnedShadowPath,
        160u);
    if (!skinnedShadowShaderPackage_.generatedReflectionHydrated
        || skinnedShadowShaderPackage_.reflection.pushConstantByteCount != 160u)
        return fail("ARP-QUEEN-SKINNED-SHADOW-REFLECTION");
    skinnedShadowShaderPackage_.reflection.descriptorBindings.push_back(
        {0u, 0u, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Vertex, 1u});
    rhi::PipelineLayoutDesc skinnedShadowLayoutDesc;
    skinnedShadowLayoutDesc.descriptorLayouts.push_back(skinningLayoutDesc);
    skinnedShadowLayoutDesc.descriptorSets.push_back({0u, skinningLayoutDesc.name});
    skinnedShadowLayoutDesc.pushConstantRanges.push_back(
        {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0u, 160u});
    skinnedShadowLayout_ = device_.createPipelineLayout(skinnedShadowLayoutDesc);
    if (!skinnedShadowLayout_)
        return fail("ARP-QUEEN-SKINNED-SHADOW-PIPELINE-LAYOUT");
    auto skinnedShadowDesc = renderer::detail::makeRenderPipelineDesc(
        core::pmr::String("BuildAQueenSkinnedShadow", &core::defaultCpuMemoryResource()),
        *skinnedShadowLayout_,
        skinnedShadowShaderPackage_,
        colorFormat_,
        extent_);
    skinnedShadowDesc.colorTargets.clear();
    skinnedShadowDesc.colorFormat = rhi::TextureFormat::Unknown;
    skinnedShadowDesc.hasViewport = false;
    skinnedShadowDesc.hasScissor = false;
    skinnedShadowDesc.dynamicViewportScissor = true;
    skinnedShadowDesc.cullMode = rhi::CullMode::Back;
    skinnedShadowDesc.frontFace = rhi::FrontFace::CounterClockwise;
    skinnedShadowDesc.topology = rhi::PrimitiveTopology::TriangleList;
    skinnedShadowDesc.depthFormat = rhi::TextureFormat::D32Float;
    skinnedShadowDesc.depthStencil = pipelineDesc.depthStencil;
    skinnedShadowDesc.vertexBuffers = skinnedPipelineDesc.vertexBuffers;
    skinnedShadowPipeline_ = device_.createRenderPipeline(skinnedShadowDesc);
    if (!skinnedShadowPipeline_)
        return fail("ARP-QUEEN-SKINNED-SHADOW-PIPELINE");

    core::pmr::String shadowShaderPath = shaderPath;
    const auto shadowSourceName = shadowShaderPath.find("queen_scene");
    if (shadowSourceName == core::pmr::String::npos)
        return fail("ARP-QUEEN-SHADOW-SHADER-PATH");
    shadowShaderPath.replace(shadowSourceName, 11u, "queen_shadow");
    rhi::PipelineLayoutDesc shadowLayoutDesc;
    shadowLayoutDesc.pushConstantRanges.push_back(
        {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0u, 64u});
    shadowLayout_ = device_.createPipelineLayout(shadowLayoutDesc);
    if (!shadowLayout_)
        return fail("ARP-QUEEN-SHADOW-PIPELINE-LAYOUT");
    shadowShaderPackage_ = renderer::detail::makeGraphicsShaderPackage(
        core::pmr::String("BuildAQueenShadow", &core::defaultCpuMemoryResource()),
        shadowShaderPath,
        64u);
    if (!shadowShaderPackage_.generatedReflectionHydrated
        || shadowShaderPackage_.reflection.pushConstantByteCount != 64u
        || !shadowShaderPackage_.reflection.descriptorBindings.empty())
        return fail("ARP-QUEEN-SHADOW-SHADER-REFLECTION");
    auto shadowPipelineDesc = renderer::detail::makeRenderPipelineDesc(
        core::pmr::String("BuildAQueenShadow", &core::defaultCpuMemoryResource()),
        *shadowLayout_,
        shadowShaderPackage_,
        colorFormat_,
        extent_);
    shadowPipelineDesc.colorTargets.clear();
    shadowPipelineDesc.colorFormat = rhi::TextureFormat::Unknown;
    shadowPipelineDesc.hasViewport = false;
    shadowPipelineDesc.hasScissor = false;
    shadowPipelineDesc.dynamicViewportScissor = true;
    shadowPipelineDesc.cullMode = rhi::CullMode::None;
    shadowPipelineDesc.frontFace = rhi::FrontFace::CounterClockwise;
    shadowPipelineDesc.topology = rhi::PrimitiveTopology::TriangleList;
    shadowPipelineDesc.depthFormat = rhi::TextureFormat::D32Float;
    shadowPipelineDesc.depthStencil = pipelineDesc.depthStencil;
    shadowPipelineDesc.vertexBuffers.push_back(pipelineDesc.vertexBuffers[0]);
    shadowPipeline_ = device_.createRenderPipeline(shadowPipelineDesc);
    if (!shadowPipeline_)
        return fail("ARP-QUEEN-SHADOW-PIPELINE");

    skinningDescriptorArena_ = device_.createDescriptorArena(
        {"QueenSkinningDescriptors", kFrameSlots * 3u, kFrameSlots * 3u});
    if (!skinningDescriptorArena_)
        return fail("ARP-QUEEN-3D-SKINNED-DESCRIPTOR-ARENA");

    for (auto& character : animatedCharacters_) {
        if (!character.player || !character.player->valid())
            return fail("ARP-QUEEN-3D-SKINNED-PLAYER");
        character.vertexBuffer = device_.createBuffer({"QueenSkinnedCharacterVertices",
            character.vertices.size() * sizeof(Vertex),
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        if (!character.vertexBuffer
            || !device_
                .writeBuffer(*character.vertexBuffer,
                    character.vertices.data(),
                    character.vertices.size() * sizeof(Vertex))
                .success)
            return fail("ARP-QUEEN-3D-SKINNED-VERTEX-BUFFER");
    }
    depth_ = device_.createTexture({"QueenDepth",
        {extent.width, extent.height, 1},
        rhi::TextureFormat::D32Float,
        rhi::TextureUsage::RenderAttachment,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    if (!depth_)
        return fail("ARP-QUEEN-3D-DEPTH-TARGET");

    shadowDepth_ = device_.createTexture({"QueenShadowDepth",
        {extent.width, extent.height, 1u},
        rhi::TextureFormat::D32Float,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    if (!shadowDepth_)
        return fail("ARP-QUEEN-SHADOW-DEPTH-TARGET");
    shadowView_ = device_.createTextureView(*shadowDepth_,
        {"QueenShadowDepthView",
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::Sampled,
            rhi::TextureViewDimension::Texture2D,
            0u,
            1u,
            0u,
            1u,
            rhi::TextureAspect::Depth});
    rhi::SamplerDesc shadowSamplerDesc;
    shadowSamplerDesc.label = "QueenShadowSampler";
    shadowSamplerDesc.minFilter = rhi::SamplerFilter::Nearest;
    shadowSamplerDesc.magFilter = rhi::SamplerFilter::Nearest;
    shadowSamplerDesc.mipFilter = rhi::SamplerMipFilter::None;
    shadowSampler_ = device_.createSampler(shadowSamplerDesc);
    shadowDescriptorArena_ = device_.createDescriptorArena({"QueenShadowDescriptors", 2u, 2u});
    if (!shadowView_ || !shadowSampler_ || !shadowDescriptorArena_)
        return fail("ARP-QUEEN-SHADOW-RESOURCES");
    shadowDescriptorSet_ = shadowDescriptorArena_->allocateDescriptorSet(
        {"QueenShadowReceiverSet", shadowReceiverLayout});
    if (!shadowDescriptorSet_
        || !shadowDescriptorSet_
            ->write({0u,
                rhi::DescriptorType::SampledTexture,
                shadowView_->id(),
                rhi::DescriptorResourceKind::TextureView})
            .success
        || !shadowDescriptorSet_
            ->write({1u,
                rhi::DescriptorType::Sampler,
                shadowSampler_->id(),
                rhi::DescriptorResourceKind::Sampler})
            .success)
        return fail("ARP-QUEEN-SHADOW-DESCRIPTOR-WRITE");

    for (std::uint32_t index = 0; index < kFrameSlots; ++index) {
        FrameSlot slot;
        slot.vertices = device_.createBuffer({"Queen3DVertices",
            std::uint64_t(vertexCapacity_) * sizeof(Vertex),
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        slot.completion = device_.createTimelineSemaphore();
        if (!slot.vertices || !slot.completion)
            return fail("ARP-QUEEN-3D-FRAME-SLOT");
        const auto& playerVertices = animatedCharacters_[0].vertices;
        slot.playerAppearanceVertices.assign(playerVertices.begin(), playerVertices.end());
        slot.playerAppearanceBuffer = device_.createBuffer({"QueenPlayerAppearanceVertices",
            std::uint64_t(playerVertices.size()) * sizeof(Vertex),
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        if (!slot.playerAppearanceBuffer)
            return fail("ARP-QUEEN-PLAYER-APPEARANCE-BUFFER");
        slot.targetAppearanceVertices.assign(playerVertices.begin(), playerVertices.end());
        slot.targetAppearanceBuffer = device_.createBuffer({"QueenTargetAppearanceVertices",
            std::uint64_t(playerVertices.size()) * sizeof(Vertex),
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        if (!slot.targetAppearanceBuffer)
            return fail("ARP-QUEEN-TARGET-APPEARANCE-BUFFER");
        for (std::size_t characterIndex = 0; characterIndex < 3u; ++characterIndex) {
            auto& palette = slot.skinningPalettes[characterIndex];
            palette = device_.createBuffer({"QueenSkinningPalette",
                sizeof(renderer::GpuSkinningPalette),
                rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::CpuToGpu,
                rhi::ResourceState::ShaderRead});
            if (!palette)
                return fail("ARP-QUEEN-3D-SKINNED-PALETTE");
            slot.skinningSets[characterIndex] = skinningDescriptorArena_->allocateDescriptorSet(
                {"QueenSkinningPaletteSet", skinningLayoutDesc});
            if (!slot.skinningSets[characterIndex]
                || !slot.skinningSets[characterIndex]
                    ->write({0u,
                        rhi::DescriptorType::UniformBuffer,
                        palette->id(),
                        rhi::DescriptorResourceKind::Buffer})
                    .success)
                return fail("ARP-QUEEN-3D-SKINNED-DESCRIPTOR-WRITE");
        }
        slots_.push_back(std::move(slot));
    }
    return {true, {}};
}

renderer::RendererResult SceneRenderer::resize(rhi::Extent2D extent)
{
    if (!pipeline_ || extent.width == 0 || extent.height == 0)
        return fail("ARP-QUEEN-3D-RESIZE-CONFIG");
    if (extent.width == extent_.width && extent.height == extent_.height)
        return {true, {}};
    for (const auto& slot : slots_)
        if (slot.pending && slot.completion->getValue() < slot.submittedValue)
            return renderer::RendererResult::deferred(
                "Queen depth target resize waits for in-flight frames",
                "ARP-QUEEN-3D-RESIZE-PENDING");
    auto depth = device_.createTexture({"QueenDepth",
        {extent.width, extent.height, 1},
        rhi::TextureFormat::D32Float,
        rhi::TextureUsage::RenderAttachment,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    if (!depth)
        return fail("ARP-QUEEN-3D-DEPTH-RESIZE");
    auto shadowDepth = device_.createTexture({"QueenShadowDepth",
        {extent.width, extent.height, 1u},
        rhi::TextureFormat::D32Float,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    if (!shadowDepth)
        return fail("ARP-QUEEN-SHADOW-DEPTH-RESIZE");
    auto shadowView = device_.createTextureView(*shadowDepth,
        {"QueenShadowDepthView",
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::Sampled,
            rhi::TextureViewDimension::Texture2D,
            0u,
            1u,
            0u,
            1u,
            rhi::TextureAspect::Depth});
    if (!shadowView
        || !shadowDescriptorSet_
            ->write({0u,
                rhi::DescriptorType::SampledTexture,
                shadowView->id(),
                rhi::DescriptorResourceKind::TextureView})
            .success)
        return fail("ARP-QUEEN-SHADOW-DESCRIPTOR-RESIZE");
    depth_ = std::move(depth);
    shadowView_ = std::move(shadowView);
    shadowDepth_ = std::move(shadowDepth);
    extent_ = extent;
    return {true, {}};
}

bool SceneRenderer::readyForFrame(std::uint64_t frameIndex) const
{
    if (slots_.empty())
        return false;
    const auto& slot = slots_[frameIndex % slots_.size()];
    return !slot.pending || slot.completion->getValue() >= slot.submittedValue;
}

renderer::RendererResult SceneRenderer::draw(rhi::GpuTexture& target,
    const rhi::AcquiredFrame& acquiredFrame,
    const Snapshot& state,
    const LevelDesc& level,
    const Pickup* pickups,
    const bool* resolvedPickups,
    std::uint32_t pickupCount,
    HudRenderer& hud,
    std::uint64_t frameIndex)
{
    lastVertexCount_ = 0;
    if (!pipeline_ || !depth_ || !shadowDepth_ || !shadowDescriptorSet_
        || target.desc().format != colorFormat_ || target.desc().extent.width != extent_.width
        || target.desc().extent.height != extent_.height || pickupCount > Game::kMaxPickups
        || (pickupCount > 0 && (pickups == nullptr || resolvedPickups == nullptr)))
        return fail("ARP-QUEEN-3D-DRAW-CONFIG");
    auto& slot = slots_[frameIndex % slots_.size()];
    if (slot.pending && slot.completion->getValue() < slot.submittedValue)
        return renderer::RendererResult::deferred(
            "Queen vertex frame slot is still in flight", "ARP-QUEEN-3D-FRAME-PENDING");
    slot.commands.reset();
    slot.pending = false;
    if (!buildScene(state, pickups, resolvedPickups, pickupCount))
        return fail("ARP-QUEEN-3D-SCENE-VERTEX-BUDGET");
    if (vertices_.empty())
        return fail("ARP-QUEEN-3D-SCENE-EMPTY");
    const auto& playerVertices = animatedCharacters_[0].vertices;
    std::copy(playerVertices.begin(), playerVertices.end(), slot.playerAppearanceVertices.begin());
    for (auto& vertex : slot.playerAppearanceVertices) {
        const auto outfitSlot = slotIndex(static_cast<MaterialRole>(vertex.materialRole));
        if (outfitSlot >= state.equipped.size() || !state.equipped[outfitSlot])
            continue;
        const auto color = kStyleColors[static_cast<std::size_t>(state.outfit[outfitSlot])];
        vertex.color[0] = color.x;
        vertex.color[1] = color.y;
        vertex.color[2] = color.z;
    }
    std::copy(playerVertices.begin(), playerVertices.end(), slot.targetAppearanceVertices.begin());
    for (auto& vertex : slot.targetAppearanceVertices) {
        const auto outfitSlot = slotIndex(static_cast<MaterialRole>(vertex.materialRole));
        if (outfitSlot >= state.equipped.size())
            continue;
        if (state.phase == Phase::Result && !state.equipped[outfitSlot])
            continue;
        const auto style =
            state.phase == Phase::Result ? state.outfit[outfitSlot] : level.target[outfitSlot];
        const auto color = kStyleColors[static_cast<std::size_t>(style)];
        vertex.color[0] = color.x;
        vertex.color[1] = color.y;
        vertex.color[2] = color.z;
    }
    const auto appearanceUpload = device_.writeBuffer(*slot.playerAppearanceBuffer,
        slot.playerAppearanceVertices.data(),
        slot.playerAppearanceVertices.size() * sizeof(Vertex));
    if (!appearanceUpload.success)
        return {false, appearanceUpload.diagnostics};
    const auto targetAppearanceUpload = device_.writeBuffer(*slot.targetAppearanceBuffer,
        slot.targetAppearanceVertices.data(),
        slot.targetAppearanceVertices.size() * sizeof(Vertex));
    if (!targetAppearanceUpload.success)
        return {false, targetAppearanceUpload.diagnostics};
    const auto written =
        device_.writeBuffer(*slot.vertices, vertices_.data(), vertices_.size() * sizeof(Vertex));
    if (!written.success)
        return {false, written.diagnostics};

    auto encoder = device_.createCommandEncoder({"BuildAQueen3D"});
    if (!encoder)
        return fail("ARP-QUEEN-3D-ENCODER");
    const auto eye = core::Vec3{0.0f, 4.8f, -7.5f};
    const auto focus = core::Vec3{0.0f, 1.0f, 3.0f};
    const auto view = core::lookAt(eye, focus);
    auto projection =
        core::perspective(50.0f, float(extent_.width) / float(extent_.height), 0.1f, 60.0f);
    if (rhi::semantics::projectionRequiresYFlip(device_.backend()))
        projection.m[5] = -projection.m[5];
    const auto viewProjection = core::multiply(projection, view);
    const auto lightPosition = add(focus, multiply(core::normalize(kLightDirection), 18.0f));
    const auto lightView = core::lookAt(lightPosition, focus);
    const auto lightProjection = core::orthographic(8.0f, 12.0f, 0.1f, 40.0f);
    lightViewProjection_ = core::multiply(lightProjection, lightView);

    auto transition =
        encoder->transition(target, target.state(), rhi::ResourceState::ColorAttachment);
    if (!transition.success)
        return {false, transition.diagnostics};
    transition = encoder->transition(*depth_, depth_->state(), rhi::ResourceState::DepthAttachment);
    if (!transition.success)
        return {false, transition.diagnostics};
    transition = encoder->transition(
        *shadowDepth_, shadowDepth_->state(), rhi::ResourceState::DepthAttachment);
    if (!transition.success)
        return {false, transition.diagnostics};

    rhi::RenderPassDesc shadowPassDesc;
    shadowPassDesc.label = "BuildAQueenShadowDepth";
    shadowPassDesc.hasDepthStencilAttachment = true;
    shadowPassDesc.depthStencilAttachment = {"QueenShadowDepth",
        shadowDepth_->id(),
        rhi::TextureFormat::D32Float,
        rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        1.0f,
        0};
    auto shadowPass = encoder->beginRenderPass(shadowPassDesc);
    if (!shadowPass)
        return {false, encoder->lastValidation().diagnostics};
    auto shadowStatus = shadowPass->setPipeline(*shadowPipeline_);
    if (shadowStatus.success)
        shadowStatus = shadowPass->setViewport(
            {0.0f, 0.0f, float(extent_.width), float(extent_.height), 0.0f, 1.0f});
    if (shadowStatus.success)
        shadowStatus = shadowPass->setScissor({0u, 0u, extent_.width, extent_.height});
    if (shadowStatus.success)
        shadowStatus = shadowPass->bindVertexBuffer(0u, *slot.vertices, 0u);
    if (shadowStatus.success)
        shadowStatus = shadowPass->pushConstants(
            rhi::ShaderStage::Vertex, 0u, sizeof(lightViewProjection_), &lightViewProjection_);
    if (shadowStatus.success)
        shadowStatus = shadowPass->draw(static_cast<std::uint32_t>(vertices_.size()), 1u, 0u, 0u);
    if (!shadowStatus.success)
        return {false, shadowStatus.diagnostics};

    auto& playerModel = animatedCharacters_[0];
    const float playerX = (static_cast<float>(state.lane) - 1.0f) * 1.88f;
    const bool showRival = state.phase == Phase::RunwayBattle || state.phase == Phase::Result;
    const bool showFinalLooks = state.phase == Phase::Result;
    const float playerDrawX = showFinalLooks ? -1.15f : playerX;
    const float playerDrawZ = showFinalLooks ? 2.0f : 0.0f;
    const float characterScale = showFinalLooks ? 1.35f : 1.0f;
    const auto animationTime = static_cast<float>(frameIndex) / 60.0f;
    if (shadowStatus.success
        && !drawAnimatedCharacter(*shadowPass,
            playerModel,
            0u,
            0u,
            playerDrawX,
            playerDrawZ,
            characterScale,
            showFinalLooks ? kPi : 0.0f,
            state.phase == Phase::Running ? "Run" : "Idle",
            animationTime,
            viewProjection,
            lightViewProjection_,
            true,
            slot)) {
        shadowStatus.success = false;
        shadowStatus.diagnostics = "ARP-QUEEN-PLAYER-SHADOW-DRAW";
    }
    if (shadowStatus.success && showRival
        && !drawAnimatedCharacter(*shadowPass,
            animatedCharacters_[1],
            1u,
            1u,
            showFinalLooks ? 1.15f : 1.2f,
            showFinalLooks ? 2.0f : 1.8f,
            characterScale,
            kPi,
            state.phase == Phase::Result ? "Wave" : "Idle",
            animationTime,
            viewProjection,
            lightViewProjection_,
            true,
            slot)) {
        shadowStatus.success = false;
        shadowStatus.diagnostics = "ARP-QUEEN-RIVAL-SHADOW-DRAW";
    }
    const auto shadowEnded = shadowPass->end();
    if (!shadowStatus.success)
        return {false, shadowStatus.diagnostics};
    if (!shadowEnded.success)
        return {false, shadowEnded.diagnostics};
    transition = encoder->transition(
        *shadowDepth_, rhi::ResourceState::DepthAttachment, rhi::ResourceState::ShaderRead);
    if (!transition.success)
        return {false, transition.diagnostics};

    rhi::RenderPassDesc passDesc;
    passDesc.label = "BuildAQueen3D";
    passDesc.colorTargetName = target.desc().label;
    passDesc.colorAttachments.push_back({"QueenColor",
        target.id(),
        target.desc().format,
        rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        false,
        {},
        {0.96f, 0.78f, 0.90f, 1.0f}});
    passDesc.hasDepthStencilAttachment = true;
    passDesc.depthStencilAttachment = {"QueenDepth",
        depth_->id(),
        rhi::TextureFormat::D32Float,
        rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        1.0f,
        0};
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass)
        return {false, encoder->lastValidation().diagnostics};

    auto status = pass->setPipeline(*skyPipeline_);
    if (status.success)
        status = pass->setViewport(
            {0.0f, 0.0f, float(extent_.width), float(extent_.height), 0.0f, 1.0f});
    if (status.success)
        status = pass->setScissor({0, 0, extent_.width, extent_.height});
    if (status.success)
        status = pass->draw(3u, 1u, 0u, 0u);
    if (!status.success)
        return {false, status.diagnostics};

    status = pass->setPipeline(*pipeline_);
    if (status.success)
        status = pass->setViewport(
            {0.0f, 0.0f, float(extent_.width), float(extent_.height), 0.0f, 1.0f});
    if (status.success)
        status = pass->setScissor({0, 0, extent_.width, extent_.height});
    if (status.success)
        status = pass->bindVertexBuffer(0, *slot.vertices, 0);
    if (status.success)
        status = pass->bindDescriptorSet(0u, *shadowDescriptorSet_);

    struct ScenePushConstants {
        core::Mat4 viewProjection;
        core::Mat4 lightViewProjection;
    } scenePush{viewProjection, lightViewProjection_};

    if (status.success)
        status = pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
            0u,
            sizeof(scenePush),
            &scenePush);
    if (status.success)
        status = pass->draw(static_cast<std::uint32_t>(vertices_.size()), 1, 0, 0);
    if (status.success
        && !drawAnimatedCharacter(*pass,
            playerModel,
            0u,
            0u,
            playerDrawX,
            playerDrawZ,
            characterScale,
            showFinalLooks ? kPi : 0.0f,
            state.phase == Phase::Running ? "Run" : "Idle",
            animationTime,
            viewProjection,
            lightViewProjection_,
            false,
            slot)) {
        status.success = false;
        status.diagnostics = "ARP-QUEEN-3D-PLAYER-ANIMATION-DRAW";
    }
    if (status.success && showRival
        && !drawAnimatedCharacter(*pass,
            animatedCharacters_[1],
            1u,
            1u,
            showFinalLooks ? 1.15f : 1.2f,
            showFinalLooks ? 2.0f : 1.8f,
            characterScale,
            kPi,
            state.phase == Phase::Result ? "Wave" : "Idle",
            animationTime,
            viewProjection,
            lightViewProjection_,
            false,
            slot)) {
        status.success = false;
        status.diagnostics = "ARP-QUEEN-3D-RIVAL-ANIMATION-DRAW";
    }
    if (status.success) {
        const auto previewViewport = hud.targetPreviewViewport();
        const auto previewScissor = hud.targetPreviewScissor();
        const auto previewEye = core::Vec3{0.0f, 1.4f, -3.4f};
        const auto previewFocus = core::Vec3{0.0f, 0.9f, 0.0f};
        const auto previewView = core::lookAt(previewEye, previewFocus);
        auto previewProjection =
            core::perspective(38.0f, previewViewport.width / previewViewport.height, 0.1f, 12.0f);
        if (rhi::semantics::projectionRequiresYFlip(device_.backend()))
            previewProjection.m[5] = -previewProjection.m[5];
        const auto previewViewProjection = core::multiply(previewProjection, previewView);
        status = pass->setViewport(previewViewport);
        if (status.success)
            status = pass->setScissor(previewScissor);
        if (status.success
            && !drawAnimatedCharacter(*pass,
                playerModel,
                0u,
                2u,
                0.0f,
                0.0f,
                1.0f,
                kPi,
                "Idle",
                animationTime,
                previewViewProjection,
                lightViewProjection_,
                false,
                slot,
                slot.targetAppearanceBuffer.get())) {
            status.success = false;
            status.diagnostics = "ARP-QUEEN-3D-TARGET-PREVIEW-DRAW";
        }
    }
    const auto ended = pass->end();
    if (!status.success)
        return {false, status.diagnostics};
    if (!ended.success)
        return {false, ended.diagnostics};
    const auto hudResult = hud.record(*encoder, target, acquiredFrame, state, level);
    if (!hudResult.success)
        return hudResult;
    transition = encoder->transition(
        target, rhi::ResourceState::ColorAttachment, rhi::ResourceState::Present);
    if (!transition.success)
        return {false, transition.diagnostics};

    slot.commands = encoder->finish();
    if (!slot.commands)
        return {false, encoder->lastValidation().diagnostics};
    const auto submitted = device_.queue().submit(
        *slot.commands, nullptr, 0, slot.completion.get(), ++slot.submittedValue);
    slot.pending = submitted.hasAcceptedWork();
    lastVertexCount_ = static_cast<std::uint32_t>(vertices_.size());
    return {submitted.success, submitted.diagnostics};
}

bool SceneRenderer::drawAnimatedCharacter(rhi::RenderPassEncoder& pass,
    AnimatedCharacter& character,
    std::size_t characterIndex,
    std::size_t paletteIndex,
    float x,
    float z,
    float scaleMultiplier,
    float yawRadians,
    const char* clip,
    float timeSeconds,
    const core::Mat4& viewProjection,
    const core::Mat4& lightViewProjection,
    bool shadowPass,
    FrameSlot& slot,
    rhi::GpuBuffer* appearanceBuffer)
{
    if (!character.player || characterIndex >= slot.skinningPalettes.size()
        || paletteIndex >= slot.skinningPalettes.size())
        return false;
    const core::pmr::String clipName(clip, &core::defaultCpuMemoryResource());
    const bool loopingClip = clipName == "Run" || clipName == "Idle";
    const bool poseUpdated = loopingClip ? character.player->updateLooping(clipName, timeSeconds)
                                         : character.player->update(clipName, timeSeconds);
    if (!poseUpdated)
        return false;
    const auto& matrices = character.player->skinMatrices();
    if (matrices.empty() || matrices.size() > renderer::kMaxGpuSkinningJoints)
        return false;
    renderer::GpuSkinningPalette palette{};
    palette.jointCount = static_cast<std::uint32_t>(matrices.size());
    for (std::size_t joint = 0; joint < matrices.size(); ++joint)
        renderer::packColumnMajorMatrixForShader(palette.skinMatrices[joint], matrices[joint].m);
    const auto paletteWrite =
        device_.writeBuffer(*slot.skinningPalettes[paletteIndex], &palette, sizeof(palette));
    if (!paletteWrite.success)
        return false;

    struct SkinnedPushConstants {
        core::Mat4 viewProjection;
        core::Mat4 lightViewProjection;
        float modelTranslationScale[4];
        float modelOriginYaw[4];
    } push{viewProjection,
        lightViewProjection,
        {x, 0.0f, z, character.scale * scaleMultiplier},
        {character.origin.x, character.origin.y, character.origin.z, yawRadians}};

    static_assert(sizeof(SkinnedPushConstants) == 160u);

    auto status = pass.setPipeline(shadowPass ? *skinnedShadowPipeline_ : *skinnedPipeline_);
    if (status.success)
        status = pass.bindVertexBuffer(0u,
            appearanceBuffer != nullptr
                ? *appearanceBuffer
                : (characterIndex == 0u ? *slot.playerAppearanceBuffer : *character.vertexBuffer),
            0u);
    if (status.success)
        status = pass.bindDescriptorSet(0u, *slot.skinningSets[paletteIndex]);
    if (status.success)
        status = shadowPass ? pass.pushConstants(rhi::ShaderStage::Vertex, 0u, sizeof(push), &push)
                            : pass.bindDescriptorSet(1u, *shadowDescriptorSet_);
    if (status.success && !shadowPass)
        status = pass.pushConstants(
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0u, sizeof(push), &push);
    if (status.success)
        status = pass.draw(character.vertexCount, 1u, 0u, 0u);
    return status.success;
}

} // namespace arp::build_a_queen
