#include "3d/camera_parameters.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/renderers.h"
#include "samples/stylized/stylized_scene_renderer.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

struct CountingStats {
    uint32_t bufferCreates = 0;
    uint32_t textureCreates = 0;
    uint32_t shaderCreates = 0;
    uint32_t graphicsPipelineCreates = 0;
    uint32_t computePipelineCreates = 0;
    uint32_t samplerCreates = 0;
    uint32_t descriptorSetCreates = 0;
    uint32_t descriptorSetUpdates = 0;
    uint32_t textureUploads = 0;
    uint32_t bufferUploads = 0;
    uint32_t commandListCreates = 0;
    uint32_t submitCalls = 0;
    uint32_t renderPassBegins = 0;
    uint32_t graphicsPipelineBinds = 0;
    uint32_t computePipelineBinds = 0;
    uint32_t descriptorSetBinds = 0;
    uint32_t lastDescriptorWriteCount = 0;
    uint32_t textureTransitions = 0;
    uint32_t vertexBufferBinds = 0;
    uint32_t indexBufferBinds = 0;
    uint32_t pushConstantCalls = 0;
    uint32_t dispatchCalls = 0;
    uint32_t drawCalls = 0;
    uint32_t drawIndexedCalls = 0;
    uint32_t lastDrawVertexCount = 0;
    uint32_t lastDrawIndexedIndexCount = 0;
    uint64_t lastBufferUploadSizeBytes = 0;
    std::pmr::string lastBufferUploadName;
    std::pmr::vector<char> lastBufferUploadBytes;
    std::pmr::vector<std::pmr::string> descriptorSetNames;
    std::pmr::vector<std::pmr::string> descriptorUpdateNames;
    std::pmr::vector<uint32_t> descriptorWriteCounts;
    std::pmr::vector<std::pmr::vector<uint32_t>> descriptorWriteBindingsByUpdate;
    std::pmr::vector<std::pmr::vector<std::pmr::string>> descriptorTextureViewNamesByUpdate;
    std::pmr::vector<std::pmr::string> graphicsPipelineNames;
    std::pmr::vector<std::pmr::string> computePipelineNames;
    std::pmr::vector<std::pmr::string> computePipelineEntryPoints;
    std::pmr::vector<std::pmr::string> graphicsPipelineBindNames;
    std::pmr::vector<arp::rhi::CullMode> graphicsPipelineCullModes;
    std::pmr::vector<arp::rhi::FrontFace> graphicsPipelineFrontFaces;
    std::pmr::vector<bool> graphicsPipelineAlphaBlendModes;
    std::pmr::vector<bool> graphicsPipelineDepthWriteModes;
    std::pmr::vector<arp::rhi::PipelineLayoutDesc> graphicsPipelineLayouts;
    std::pmr::vector<std::pmr::string> textureNames;
    std::pmr::vector<arp::rhi::TextureFormat> textureFormats;
    std::pmr::vector<uint32_t> textureMipCounts;
    std::pmr::vector<arp::rhi::TextureUsage> textureUsages;
    std::pmr::vector<std::pmr::string> bufferNames;
    std::pmr::vector<arp::rhi::BufferUsage> bufferUsages;
};

class CountingCommandList final : public arp::rhi::CommandList {
public:
    explicit CountingCommandList(CountingStats& stats) : stats_(stats) {}

    void begin(const std::pmr::string&) override {}

    void transition(
        arp::rhi::Texture& texture, arp::rhi::ResourceState, arp::rhi::ResourceState after) override
    {
        ++stats_.textureTransitions;
        texture.setState(after);
    }

    void transition(
        arp::rhi::Buffer& buffer, arp::rhi::ResourceState, arp::rhi::ResourceState after) override
    {
        buffer.setState(after);
    }

    void clearColor(arp::rhi::Texture&, arp::rhi::Color) override {}

    void beginRenderPass(const arp::rhi::RenderPassDesc&) override { ++stats_.renderPassBegins; }

    void endRenderPass() override {}

    void bindGraphicsPipeline(arp::rhi::GraphicsPipeline& pipeline) override
    {
        ++stats_.graphicsPipelineBinds;
        stats_.graphicsPipelineBindNames.push_back(pipeline.name());
    }

    void bindComputePipeline(arp::rhi::ComputePipeline&) override { ++stats_.computePipelineBinds; }

    void bindDescriptorSet(arp::rhi::DescriptorSet& descriptorSet) override
    {
        ++stats_.descriptorSetBinds;
        stats_.descriptorSetNames.push_back(descriptorSet.name());
    }

    void bindVertexBuffer(arp::rhi::Buffer&) override { ++stats_.vertexBufferBinds; }

    void bindIndexBuffer(arp::rhi::Buffer&) override { ++stats_.indexBufferBinds; }

    void pushConstants(const void*, uint64_t) override { ++stats_.pushConstantCalls; }

    void draw(uint32_t vertexCount, uint32_t, uint32_t, uint32_t) override
    {
        ++stats_.drawCalls;
        stats_.lastDrawVertexCount = vertexCount;
    }

    void drawIndexed(uint32_t indexCount, uint32_t, uint32_t, int32_t, uint32_t) override
    {
        ++stats_.drawIndexedCalls;
        stats_.lastDrawIndexedIndexCount = indexCount;
    }

    void dispatch(uint32_t, uint32_t, uint32_t) override { ++stats_.dispatchCalls; }

    void end() override {}

private:
    CountingStats& stats_;
};

class CountingDevice final : public arp::rhi::Device {
public:
    CountingStats stats;

    arp::rhi::Backend backend() const override { return arp::rhi::Backend::Null; }

    std::unique_ptr<arp::rhi::Texture> createTexture(const arp::rhi::TextureDesc& desc) override
    {
        ++stats.textureCreates;
        stats.textureNames.push_back(desc.name);
        stats.textureFormats.push_back(desc.format);
        stats.textureMipCounts.push_back(desc.mipCount);
        stats.textureUsages.push_back(desc.usage);
        return std::make_unique<arp::rhi::Texture>(desc.name,
            desc.extent,
            desc.initialState,
            desc.format,
            desc.usage,
            desc.mipCount,
            desc.dimension,
            desc.arrayLayers,
            desc.sampleCount);
    }

    std::unique_ptr<arp::rhi::Buffer> createBuffer(const arp::rhi::BufferDesc& desc) override
    {
        ++stats.bufferCreates;
        stats.bufferNames.push_back(desc.name);
        stats.bufferUsages.push_back(desc.usage);
        return std::make_unique<arp::rhi::Buffer>(desc.name,
            desc.sizeBytes,
            desc.initialState,
            desc.usage,
            desc.memoryLocation,
            desc.initialData ? desc.initialDataSizeBytes : 0);
    }

    std::unique_ptr<arp::rhi::Shader> createShader(const arp::rhi::ShaderDesc& desc) override
    {
        ++stats.shaderCreates;
        return std::make_unique<arp::rhi::Shader>(desc.name,
            desc.stages,
            desc.bytecodeSizeBytes,
            desc.format,
            desc.entryPoints,
            desc.reflection);
    }

    std::unique_ptr<arp::rhi::GraphicsPipeline> createGraphicsPipeline(
        const arp::rhi::GraphicsPipelineDesc& desc) override
    {
        ++stats.graphicsPipelineCreates;
        stats.graphicsPipelineNames.push_back(desc.name);
        stats.graphicsPipelineCullModes.push_back(desc.cullMode);
        stats.graphicsPipelineFrontFaces.push_back(desc.frontFace);
        stats.graphicsPipelineAlphaBlendModes.push_back(desc.alphaBlendEnabled);
        stats.graphicsPipelineDepthWriteModes.push_back(desc.depthWriteEnabled);
        stats.graphicsPipelineLayouts.push_back(desc.pipelineLayout);
        return std::make_unique<arp::rhi::GraphicsPipeline>(desc.name,
            desc.vertexLayout,
            desc.colorFormat,
            desc.viewportExtent,
            desc.pushConstantSizeBytes,
            desc.alphaBlendEnabled,
            desc.depthTestEnabled,
            desc.depthFormat,
            static_cast<uint32_t>(desc.descriptorSets.size()),
            desc.cullMode,
            desc.depthWriteEnabled,
            desc.frontFace,
            desc.pipelineLayout);
    }

    std::unique_ptr<arp::rhi::ComputePipeline> createComputePipeline(
        const arp::rhi::ComputePipelineDesc& desc) override
    {
        ++stats.computePipelineCreates;
        stats.computePipelineNames.push_back(desc.name);
        stats.computePipelineEntryPoints.push_back(desc.entryPoint);
        return std::make_unique<arp::rhi::ComputePipeline>(desc.name);
    }

    std::unique_ptr<arp::rhi::Sampler> createSampler(const arp::rhi::SamplerDesc& desc) override
    {
        ++stats.samplerCreates;
        return std::make_unique<arp::rhi::Sampler>(desc.name, desc.filter, desc.addressMode);
    }

    std::unique_ptr<arp::rhi::DescriptorSet> createDescriptorSet(
        const arp::rhi::DescriptorSetDesc& desc) override
    {
        ++stats.descriptorSetCreates;
        return std::make_unique<arp::rhi::DescriptorSet>(desc.name, desc.bindings);
    }

    arp::rhi::ResourceValidationResult updateDescriptorSet(arp::rhi::DescriptorSet& descriptorSet,
        const std::pmr::vector<arp::rhi::DescriptorWrite>& writes) override
    {
        ++stats.descriptorSetUpdates;
        stats.lastDescriptorWriteCount = static_cast<uint32_t>(writes.size());
        stats.descriptorUpdateNames.push_back(descriptorSet.name());
        stats.descriptorWriteCounts.push_back(static_cast<uint32_t>(writes.size()));
        std::pmr::vector<uint32_t> bindings;
        std::pmr::vector<std::pmr::string> textureViewNames;
        bindings.reserve(writes.size());
        textureViewNames.reserve(writes.size());
        for (const auto& write : writes) {
            bindings.push_back(write.binding);
            textureViewNames.push_back(write.texture.name);
        }
        stats.descriptorWriteBindingsByUpdate.push_back(std::move(bindings));
        stats.descriptorTextureViewNamesByUpdate.push_back(std::move(textureViewNames));
        return arp::rhi::Device::updateDescriptorSet(descriptorSet, writes);
    }

    arp::rhi::ResourceValidationResult uploadTexture(
        const arp::rhi::TextureUploadDesc& desc) override
    {
        ++stats.textureUploads;
        return arp::rhi::Device::uploadTexture(desc);
    }

    arp::rhi::ResourceValidationResult uploadBuffer(const arp::rhi::BufferUploadDesc& desc) override
    {
        ++stats.bufferUploads;
        stats.lastBufferUploadName =
            desc.destination ? desc.destination->name() : std::pmr::string{};
        stats.lastBufferUploadSizeBytes = desc.sizeBytes;
        stats.lastBufferUploadBytes.clear();
        if (desc.data && desc.sizeBytes > 0) {
            const auto* begin = static_cast<const char*>(desc.data);
            stats.lastBufferUploadBytes.assign(begin, begin + desc.sizeBytes);
        }
        return arp::rhi::Device::uploadBuffer(desc);
    }

    std::unique_ptr<arp::rhi::CommandList> createCommandList() override
    {
        ++stats.commandListCreates;
        return std::make_unique<CountingCommandList>(stats);
    }

    void submit(arp::rhi::CommandList&) override { ++stats.submitCalls; }
};

std::pmr::string writeShaderFixture(const std::pmr::string& name)
{
    const auto path = std::filesystem::current_path() / name;
    std::ofstream file(path, std::ios::binary);
    const char bytecode[] = {'s', 'h', 'd', 'r'};
    file.write(bytecode, sizeof(bytecode));
    return arp::core::toPmrString(path.string());
}

arp::renderer::RenderFrameContext testFrameContext(arp::rhi::Texture* colorTexture = nullptr,
    arp::rhi::Texture* depthTexture = nullptr,
    const arp::rhi::DepthTargetView* depthTarget = nullptr)
{
    return arp::renderer::RenderFrameContext{
        arp::rhi::AcquiredFrame{true, "", 0, 1},
        arp::rhi::RenderTargetView{
            "Backbuffer",
            {640, 360},
            arp::rhi::TextureFormat::Bgra8Unorm,
            arp::rhi::ResourceState::ColorAttachment,
            0,
        },
        depthTarget,
        colorTexture,
        depthTexture,
    };
}

arp::renderer::MeshAsset twoMaterialTriangleMesh()
{
    arp::renderer::MeshAsset mesh;
    mesh.vertices = {
        arp::renderer::MeshVertex{{-0.8f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        arp::renderer::MeshVertex{{-0.1f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        arp::renderer::MeshVertex{{-0.45f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}},
        arp::renderer::MeshVertex{{0.1f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        arp::renderer::MeshVertex{{0.8f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        arp::renderer::MeshVertex{{0.45f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 20},
    };
    return mesh;
}

arp::renderer::MeshAsset twoPikaMaterialTriangleMesh()
{
    auto mesh = twoMaterialTriangleMesh();
    mesh.submeshes[0].materialSlot = 10;
    mesh.submeshes[1].materialSlot = 13;
    return mesh;
}

bool containsString(
    const std::pmr::vector<std::pmr::string>& values, const std::pmr::string& expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}

bool containsBinding(const std::pmr::vector<uint32_t>& values, uint32_t expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}

arp::samples::stylized::CharacterMaterialSlot testCharacterMaterialSlot(uint32_t slot)
{
    auto material = arp::samples::stylized::CharacterMaterialSlot{};
    material.slot = slot;
    material.npr = arp::samples::stylized::CharacterNprMaterial::defaults();
    return material;
}

void rhi_scene_renderer_records_scene_plan_through_abstract_rhi()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_test_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        "",
    });
    assert(created.success);
    assert(device.stats.shaderCreates == 1);
    assert(device.stats.graphicsPipelineCreates == 8);
    assert(device.stats.graphicsPipelineNames.size() == 8);
    assert(device.stats.graphicsPipelineNames[4]
        == std::pmr::string("RhiStylizedScenePiKaXRayMaskPipeline"));
    assert(device.stats.graphicsPipelineNames[5]
        == std::pmr::string("RhiStylizedScenePiKaOutlinePipeline"));
    assert(device.stats.graphicsPipelineNames[6]
        == std::pmr::string("RhiStylizedScenePiKaPlanarShadowPipeline"));
    assert(device.stats.graphicsPipelineNames[7]
        == std::pmr::string("RhiStylizedScenePiKaFurShellPipeline"));
    assert(device.stats.graphicsPipelineCullModes.size() == 8);
    assert(device.stats.graphicsPipelineFrontFaces.size() == 8);
    assert(device.stats.graphicsPipelineAlphaBlendModes.size() == 8);
    assert(device.stats.graphicsPipelineDepthWriteModes.size() == 8);
    assert(device.stats.graphicsPipelineCullModes[0] == arp::rhi::CullMode::Back);
    assert(device.stats.graphicsPipelineCullModes[1] == arp::rhi::CullMode::None);
    assert(device.stats.graphicsPipelineCullModes[2] == arp::rhi::CullMode::Back);
    assert(device.stats.graphicsPipelineCullModes[3] == arp::rhi::CullMode::None);
    assert(device.stats.graphicsPipelineCullModes[4] == arp::rhi::CullMode::Back);
    assert(device.stats.graphicsPipelineCullModes[5] == arp::rhi::CullMode::Front);
    assert(device.stats.graphicsPipelineCullModes[6] == arp::rhi::CullMode::Back);
    assert(device.stats.graphicsPipelineCullModes[7] == arp::rhi::CullMode::None);
    assert(std::all_of(device.stats.graphicsPipelineFrontFaces.begin(),
        device.stats.graphicsPipelineFrontFaces.end(),
        [](const arp::rhi::FrontFace frontFace) {
            return frontFace == arp::rhi::FrontFace::Clockwise;
        }));
    assert(!device.stats.graphicsPipelineAlphaBlendModes[0]);
    assert(!device.stats.graphicsPipelineAlphaBlendModes[1]);
    assert(device.stats.graphicsPipelineAlphaBlendModes[2]);
    assert(device.stats.graphicsPipelineAlphaBlendModes[3]);
    assert(device.stats.graphicsPipelineAlphaBlendModes[4]);
    assert(device.stats.graphicsPipelineAlphaBlendModes[5]);
    assert(device.stats.graphicsPipelineAlphaBlendModes[6]);
    assert(device.stats.graphicsPipelineAlphaBlendModes[7]);
    assert(device.stats.graphicsPipelineDepthWriteModes[0]);
    assert(device.stats.graphicsPipelineDepthWriteModes[1]);
    assert(!device.stats.graphicsPipelineDepthWriteModes[2]);
    assert(!device.stats.graphicsPipelineDepthWriteModes[3]);
    assert(!device.stats.graphicsPipelineDepthWriteModes[4]);
    assert(!device.stats.graphicsPipelineDepthWriteModes[5]);
    assert(!device.stats.graphicsPipelineDepthWriteModes[6]);
    assert(!device.stats.graphicsPipelineDepthWriteModes[7]);
    assert(device.stats.textureCreates == 10);
    assert(device.stats.samplerCreates == 4);
    assert(device.stats.descriptorSetCreates == 2);
    assert(device.stats.descriptorSetUpdates == 2);
    assert(device.stats.descriptorUpdateNames[0] == std::pmr::string("StylizedSceneMaterialSet"));
    assert(device.stats.descriptorWriteCounts[0]
        == arp::samples::stylized::stylizedSceneMaterialDescriptorBindings().size());
    assert(device.stats.descriptorUpdateNames[1] == std::pmr::string("StylizedSceneSceneSet"));
    assert(device.stats.descriptorWriteCounts[1]
        == arp::samples::stylized::stylizedSceneSceneDescriptorBindings().size());
    assert(device.stats.bufferCreates == 1);
    const auto shadowParamsBuffer = std::find(device.stats.bufferNames.begin(),
        device.stats.bufferNames.end(),
        "StylizedSceneShadowParams");
    assert(shadowParamsBuffer != device.stats.bufferNames.end());
    const auto shadowParamsBufferIndex =
        static_cast<size_t>(std::distance(device.stats.bufferNames.begin(), shadowParamsBuffer));
    assert((device.stats.bufferUsages[shadowParamsBufferIndex] & arp::rhi::BufferUsage::Uniform)
        != arp::rhi::BufferUsage::None);
    assert((device.stats.bufferUsages[shadowParamsBufferIndex] & arp::rhi::BufferUsage::TransferDst)
        != arp::rhi::BufferUsage::None);
    assert(device.stats.textureUploads == 6);
    assert(renderer.graphicsPipeline());
    assert(renderer.graphicsPipeline()->depthTestEnabled());
    assert(renderer.graphicsPipeline()->depthWriteEnabled());
    assert(renderer.graphicsPipeline()->cullMode() == arp::rhi::CullMode::Back);
    assert(renderer.graphicsPipeline()->vertexLayout().attributes.size() == 4);
    assert(renderer.graphicsPipeline()->vertexLayout().attributes[3].location == 3);
    assert(renderer.graphicsPipeline()->vertexLayout().attributes[3].format
        == arp::rhi::VertexFormat::Float4);
    assert(renderer.doubleSidedGraphicsPipeline());
    assert(renderer.doubleSidedGraphicsPipeline()->cullMode() == arp::rhi::CullMode::None);
    assert(renderer.doubleSidedGraphicsPipeline()->depthWriteEnabled());
    assert(renderer.alphaBlendGraphicsPipeline());
    assert(renderer.alphaBlendGraphicsPipeline()->alphaBlendEnabled());
    assert(renderer.alphaBlendGraphicsPipeline()->cullMode() == arp::rhi::CullMode::Back);
    assert(renderer.alphaBlendGraphicsPipeline()->depthTestEnabled());
    assert(!renderer.alphaBlendGraphicsPipeline()->depthWriteEnabled());
    assert(renderer.doubleSidedAlphaBlendGraphicsPipeline());
    assert(renderer.doubleSidedAlphaBlendGraphicsPipeline()->alphaBlendEnabled());
    assert(
        renderer.doubleSidedAlphaBlendGraphicsPipeline()->cullMode() == arp::rhi::CullMode::None);
    assert(renderer.doubleSidedAlphaBlendGraphicsPipeline()->depthTestEnabled());
    assert(!renderer.doubleSidedAlphaBlendGraphicsPipeline()->depthWriteEnabled());
    assert(renderer.graphicsPipeline()->descriptorSetLayoutCount() == 2);
    assert(renderer.graphicsPipeline()->pipelineLayout().slots.size() == 3u);
    assert(renderer.graphicsPipeline()->pipelineLayout().slots[0].kind
        == arp::rhi::PipelineLayoutSlotKind::SystemPushConstants);
    assert(renderer.graphicsPipeline()->pipelineLayout().slots[0].setIndex
        == arp::samples::stylized::kStylizedScenePushConstantSetIndex);
    assert(renderer.graphicsPipeline()->pipelineLayout().slots[1].descriptorSet->name()
        == std::pmr::string("StylizedSceneMaterialSet"));
    assert(renderer.graphicsPipeline()->pipelineLayout().slots[1].setIndex
        == arp::samples::stylized::kStylizedSceneMaterialSetIndex);
    assert(renderer.graphicsPipeline()->pipelineLayout().slots[2].descriptorSet->name()
        == std::pmr::string("StylizedSceneSceneSet"));
    assert(renderer.graphicsPipeline()->pipelineLayout().slots[2].setIndex
        == arp::samples::stylized::kStylizedSceneSceneSetIndex);
    const auto prefilteredCube = std::find(device.stats.textureNames.begin(),
        device.stats.textureNames.end(),
        "StylizedSceneFallbackPrefilteredSpecularCube");
    assert(prefilteredCube != device.stats.textureNames.end());
    const auto prefilteredCubeIndex =
        static_cast<size_t>(std::distance(device.stats.textureNames.begin(), prefilteredCube));
    assert(device.stats.textureMipCounts[prefilteredCubeIndex] == 1u);
    const auto shadowAtlas = std::find(device.stats.textureNames.begin(),
        device.stats.textureNames.end(),
        "StylizedSceneFallbackShadowAtlas");
    assert(shadowAtlas != device.stats.textureNames.end());
    const auto shadowAtlasIndex =
        static_cast<size_t>(std::distance(device.stats.textureNames.begin(), shadowAtlas));
    assert(device.stats.textureFormats[shadowAtlasIndex] == arp::rhi::TextureFormat::Rgba8Unorm);
    assert((device.stats.textureUsages[shadowAtlasIndex] & arp::rhi::TextureUsage::DepthAttachment)
        == arp::rhi::TextureUsage::None);

    auto mesh = arp::renderer::uploadMeshToRhi(device, arp::renderer::MeshAsset::debugTriangle());
    assert(mesh.success);

    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    arp::rhi::Texture frameColor{
        "ProvidedBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };
    arp::rhi::DepthTargetView depthView{
        "ProvidedDepthView",
        {640, 360},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::ResourceState::DepthAttachment,
    };
    arp::rhi::Texture frameDepth{
        "ProvidedDepth",
        {640, 360},
        arp::rhi::ResourceState::DepthAttachment,
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::DepthAttachment,
        1,
    };
    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor, &frameDepth, &depthView),
        mesh.mesh,
        scene,
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet{});

    assert(drawn.success);
    assert(device.stats.textureCreates == 10);
    assert(device.stats.commandListCreates == 1);
    assert(device.stats.submitCalls == 1);
    assert(device.stats.renderPassBegins == 1);
    assert(device.stats.graphicsPipelineBinds == 1);
    assert(device.stats.descriptorSetBinds == 2);
    assert(device.stats.descriptorSetNames[0] == std::pmr::string("StylizedSceneSceneSet"));
    assert(device.stats.descriptorSetNames[1] == std::pmr::string("StylizedSceneMaterialSet"));
    assert(device.stats.vertexBufferBinds == 1);
    assert(device.stats.indexBufferBinds == 1);
    assert(device.stats.pushConstantCalls == 1);
    assert(device.stats.drawIndexedCalls == 1);
    assert(device.stats.lastDrawIndexedIndexCount == 3);
}

void rhi_scene_renderer_records_pika_runtime_pass_pipelines()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_pika_pass_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        "",
    });
    assert(created.success);

    auto mesh = arp::renderer::uploadMeshToRhi(device, twoPikaMaterialTriangleMesh());
    assert(mesh.success);

    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    arp::rhi::Texture frameColor{
        "ProvidedPikaBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };
    arp::rhi::Texture frameDepth{
        "ProvidedPikaDepth",
        {640, 360},
        arp::rhi::ResourceState::DepthAttachment,
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::DepthAttachment,
        1,
    };

    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor, &frameDepth),
        mesh.mesh,
        scene,
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet::defaultPiKaNprCharacter());

    assert(drawn.success);
    assert(containsString(device.stats.graphicsPipelineBindNames, "RhiStylizedScenePipeline"));
    assert(containsString(
        device.stats.graphicsPipelineBindNames, "RhiStylizedScenePiKaXRayMaskPipeline"));
    assert(containsString(
        device.stats.graphicsPipelineBindNames, "RhiStylizedScenePiKaOutlinePipeline"));
    assert(containsString(
        device.stats.graphicsPipelineBindNames, "RhiStylizedScenePiKaPlanarShadowPipeline"));
    assert(containsString(
        device.stats.graphicsPipelineBindNames, "RhiStylizedScenePiKaFurShellPipeline"));
    assert(device.stats.drawIndexedCalls == 15);
}

void rhi_scene_renderer_records_gi_compute_when_enabled()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_gi_scene_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        writeShaderFixture("rhi_scene_renderer_gi_compute_shader.bin"),
    });
    assert(created.success);
    assert(device.stats.shaderCreates == 2);
    assert(device.stats.graphicsPipelineCreates == 8);
    assert(device.stats.computePipelineCreates == 2);
    assert(containsString(device.stats.computePipelineNames, "GiProbeUpdate"));
    assert(containsString(device.stats.computePipelineNames, "GiIrradianceResolve"));
    assert(containsString(device.stats.computePipelineEntryPoints, "GiProbeUpdate"));
    assert(containsString(device.stats.computePipelineEntryPoints, "GiIrradianceResolve"));
    assert(device.stats.textureCreates == 10);
    assert(device.stats.descriptorSetCreates == 3);
    assert(device.stats.samplerCreates == 4);
    assert(device.stats.descriptorSetUpdates == 2);
    assert(device.stats.textureUploads == 6);

    auto mesh = arp::renderer::uploadMeshToRhi(device, arp::renderer::MeshAsset::debugTriangle());
    assert(mesh.success);
    assert(device.stats.bufferCreates == 3);

    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    arp::rhi::Texture frameColor{
        "ProvidedGiBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };
    arp::rhi::Texture frameDepth{
        "ProvidedGiDepth",
        {640, 360},
        arp::rhi::ResourceState::DepthAttachment,
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::DepthAttachment,
        1,
    };
    auto settings = arp::samples::stylized::StylizedSceneRenderSettings{};
    settings.globalIllumination =
        arp::renderer::globalIlluminationDescForProfile(arp::renderer::QualityProfile::Medium);
    settings.globalIlluminationIntensity = 0.65f;

    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor, &frameDepth),
        mesh.mesh,
        scene,
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet{},
        settings);

    assert(drawn.success);
    assert(device.stats.bufferCreates == 4);
    assert(device.stats.textureCreates == 11);
    assert(device.stats.descriptorSetUpdates == 4);
    assert(device.stats.commandListCreates == 1);
    assert(device.stats.computePipelineBinds == 2);
    assert(device.stats.descriptorSetBinds == 4);
    assert(device.stats.dispatchCalls == 2);
    assert(device.stats.graphicsPipelineBinds == 1);
    assert(device.stats.drawIndexedCalls == 1);

    const auto drawnAgain = renderer.drawFrame(testFrameContext(&frameColor, &frameDepth),
        mesh.mesh,
        scene,
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet{},
        settings);

    assert(drawnAgain.success);
    assert(device.stats.bufferCreates == 4);
    assert(device.stats.textureCreates == 11);
    assert(device.stats.descriptorSetUpdates == 6);
    assert(device.stats.commandListCreates == 2);
    assert(device.stats.computePipelineBinds == 4);
    assert(device.stats.descriptorSetBinds == 8);
    assert(device.stats.dispatchCalls == 4);
    assert(device.stats.graphicsPipelineBinds == 2);
    assert(device.stats.drawIndexedCalls == 2);
}

void rhi_scene_renderer_uploads_shadow_params_from_render_settings()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_shadow_params_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        "",
    });
    assert(created.success);

    auto mesh = arp::renderer::uploadMeshToRhi(device, arp::renderer::MeshAsset::debugTriangle());
    assert(mesh.success);

    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    auto settings = arp::samples::stylized::StylizedSceneRenderSettings{};
    settings.shadowsEnabled = true;
    settings.shadowCascades = arp::samples::stylized::ShadowCascadeSet::forCamera(
        camera, arp::samples::stylized::CsmSettings{4, 160.0f, 2048, 0.5f});
    settings.shadowAtlas = arp::samples::stylized::ShadowAtlasLayout::forCsmAndCharacter(
        settings.shadowCascades, arp::samples::stylized::ShadowAtlasSettings{});

    arp::rhi::Texture frameColor{
        "ProvidedShadowBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };
    arp::rhi::Texture frameDepth{
        "ProvidedShadowDepth",
        {640, 360},
        arp::rhi::ResourceState::DepthAttachment,
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::DepthAttachment,
        1,
    };

    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor, &frameDepth),
        mesh.mesh,
        scene,
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet{},
        settings);

    assert(drawn.success);
    assert(device.stats.bufferUploads == 1);
    assert(device.stats.lastBufferUploadName == std::pmr::string("StylizedSceneShadowParams"));
    assert(device.stats.lastBufferUploadSizeBytes
        == sizeof(arp::samples::stylized::ShadowSceneParams));
    assert(device.stats.lastBufferUploadBytes.size()
        == sizeof(arp::samples::stylized::ShadowSceneParams));

    arp::samples::stylized::ShadowSceneParams uploadedShadow{};
    std::memcpy(&uploadedShadow, device.stats.lastBufferUploadBytes.data(), sizeof(uploadedShadow));
    assert(uploadedShadow.enabled == 1.0f);
    assert(uploadedShadow.cascadeCount == 4.0f);
    assert(uploadedShadow.atlasResolution == 2048.0f);
    assert(uploadedShadow.lastCascadeFar > uploadedShadow.firstCascadeFar);
}

void rhi_scene_renderer_uploads_and_binds_material_textures_by_slot()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_material_texture_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        "",
    });
    assert(created.success);

    const std::pmr::vector<uint8_t> bluePixels{
        0,
        72,
        255,
        255,
        0,
        96,
        255,
        255,
    };
    const std::pmr::vector<uint8_t> redPixels{
        255,
        48,
        32,
        255,
        255,
        72,
        48,
        255,
    };
    const auto materialUploaded = renderer.updateMaterialTextures({
        arp::renderer::MaterialTextureSource{
            10,
            "BlueHeroBaseColor",
            {2, 1},
            bluePixels,
        },
        arp::renderer::MaterialTextureSource{
            20,
            "RedHeroBaseColor",
            {2, 1},
            redPixels,
        },
    });
    assert(materialUploaded.success);
    assert(device.stats.textureCreates == 12);
    assert(device.stats.textureUploads == 8);
    assert(device.stats.descriptorSetCreates == 4);
    assert(device.stats.descriptorSetUpdates == 4);

    auto mesh = arp::renderer::uploadMeshToRhi(device, twoMaterialTriangleMesh());
    assert(mesh.success);
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(testCharacterMaterialSlot(10));
    materials.slots.push_back(testCharacterMaterialSlot(20));
    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    arp::rhi::Texture frameColor{
        "ProvidedMaterialBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };

    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor),
        mesh.mesh,
        scene,
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);

    assert(drawn.success);
    assert(device.stats.drawIndexedCalls == 2);
    assert(device.stats.descriptorSetBinds == 3);
    assert(device.stats.descriptorSetNames[0] == std::pmr::string("StylizedSceneSceneSet"));
    assert(device.stats.descriptorSetNames[1] == std::pmr::string("StylizedSceneMaterialSet10"));
    assert(device.stats.descriptorSetNames[2] == std::pmr::string("StylizedSceneMaterialSet20"));
}

void rhi_scene_renderer_uploads_full_material_texture_set_by_slot()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_full_material_texture_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        "",
    });
    assert(created.success);

    const std::pmr::vector<uint8_t> whitePixels{255, 255, 255, 255};
    const std::pmr::vector<uint8_t> normalPixels{128, 128, 255, 255};
    const std::pmr::vector<uint8_t> rmaPixels{255, 180, 32, 255};
    const std::pmr::vector<uint8_t> occlusionPixels{96, 96, 96, 255};
    const std::pmr::vector<uint8_t> emissivePixels{16, 32, 48, 255};
    const auto materialUploaded = renderer.updateMaterialTextures({
        arp::renderer::MaterialTextureSource{88,
            "MatBase",
            {1, 1},
            whitePixels,
            true,
            arp::renderer::MaterialTextureSemantic::BaseColor},
        arp::renderer::MaterialTextureSource{88,
            "MatNormal",
            {1, 1},
            normalPixels,
            false,
            arp::renderer::MaterialTextureSemantic::Normal},
        arp::renderer::MaterialTextureSource{88,
            "MatRoughMetal",
            {1, 1},
            rmaPixels,
            false,
            arp::renderer::MaterialTextureSemantic::MetallicRoughness},
        arp::renderer::MaterialTextureSource{88,
            "MatOcclusion",
            {1, 1},
            occlusionPixels,
            false,
            arp::renderer::MaterialTextureSemantic::Occlusion},
        arp::renderer::MaterialTextureSource{88,
            "MatEmissive",
            {1, 1},
            emissivePixels,
            true,
            arp::renderer::MaterialTextureSemantic::Emissive},
    });

    assert(materialUploaded.success);
    assert(device.stats.textureUploads == 11);
    assert(device.stats.descriptorSetUpdates == 3);
    assert(device.stats.lastDescriptorWriteCount
        == arp::samples::stylized::stylizedSceneMaterialDescriptorBindings().size());
}

void rhi_scene_renderer_uploads_pika_material_textures_by_slot()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_pika_texture_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        "",
    });
    assert(created.success);

    const std::pmr::vector<uint8_t> ilmPixels{32, 64, 128, 255};
    const std::pmr::vector<uint8_t> faceSdfPixels{128, 128, 128, 255};
    const std::pmr::vector<uint8_t> furNoisePixels{12, 92, 180, 255};
    const auto uploaded = renderer.updatePiKaMaterialTextures({
        arp::samples::stylized::PiKaMaterialTextureSource{
            10,
            "PiKaClothIlm",
            {1, 1},
            ilmPixels,
            false,
            arp::samples::stylized::PiKaMaterialTextureSemantic::Ilm,
        },
        arp::samples::stylized::PiKaMaterialTextureSource{
            10,
            "PiKaFaceSdf",
            {1, 1},
            faceSdfPixels,
            false,
            arp::samples::stylized::PiKaMaterialTextureSemantic::FaceSdf,
        },
        arp::samples::stylized::PiKaMaterialTextureSource{
            13,
            "PiKaFurNoise",
            {1, 1},
            furNoisePixels,
            false,
            arp::samples::stylized::PiKaMaterialTextureSemantic::FurNoise,
        },
    });

    assert(uploaded.success);
    assert(device.stats.textureCreates == 13);
    assert(device.stats.textureUploads == 9);
    assert(device.stats.descriptorSetCreates == 4);
    assert(device.stats.descriptorSetUpdates == 4);
    assert(device.stats.lastDescriptorWriteCount
        == arp::samples::stylized::stylizedSceneMaterialDescriptorBindings().size());
    assert(device.stats.descriptorUpdateNames[2] == std::pmr::string("StylizedSceneMaterialSet10"));
    assert(device.stats.descriptorUpdateNames[3] == std::pmr::string("StylizedSceneMaterialSet13"));
    assert(containsBinding(device.stats.descriptorWriteBindingsByUpdate[2], 17));
    assert(containsBinding(device.stats.descriptorWriteBindingsByUpdate[2], 19));
    assert(containsString(device.stats.descriptorTextureViewNamesByUpdate[2], "PiKaClothIlmView"));
    assert(containsString(device.stats.descriptorTextureViewNamesByUpdate[2], "PiKaFaceSdfView"));
    assert(containsBinding(device.stats.descriptorWriteBindingsByUpdate[3], 24));
    assert(containsString(device.stats.descriptorTextureViewNamesByUpdate[3], "PiKaFurNoiseView"));

    auto mesh = arp::renderer::uploadMeshToRhi(device, twoPikaMaterialTriangleMesh());
    assert(mesh.success);
    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    arp::rhi::Texture frameColor{
        "ProvidedPikaTextureBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };

    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor),
        mesh.mesh,
        scene,
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet::defaultPiKaNprCharacter());

    assert(drawn.success);
    assert(containsString(device.stats.descriptorSetNames, "StylizedSceneMaterialSet10"));
    assert(containsString(device.stats.descriptorSetNames, "StylizedSceneMaterialSet13"));
}

void rhi_scene_renderer_reuses_duplicate_material_texture_sources()
{
    CountingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("rhi_scene_renderer_dedup_material_texture_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        "",
    });
    assert(created.success);

    const std::pmr::vector<uint8_t> sharedPixels{
        64,
        160,
        255,
        255,
        72,
        168,
        255,
        255,
    };
    const auto materialUploaded = renderer.updateMaterialTextures({
        arp::renderer::MaterialTextureSource{
            10,
            "SharedHeroBaseColor",
            {2, 1},
            sharedPixels,
            true,
            arp::renderer::MaterialTextureSemantic::BaseColor,
        },
        arp::renderer::MaterialTextureSource{
            20,
            "SharedHeroBaseColor",
            {2, 1},
            sharedPixels,
            true,
            arp::renderer::MaterialTextureSemantic::BaseColor,
        },
    });

    assert(materialUploaded.success);
    assert(device.stats.textureCreates == 11);
    assert(device.stats.textureUploads == 7);
    assert(device.stats.descriptorSetCreates == 4);
    assert(device.stats.descriptorSetUpdates == 4);
}

void rhi_gui_renderer_records_gui_plan_through_abstract_rhi()
{
    CountingDevice device;
    arp::renderer::GuiRenderer renderer(device);
    const auto created = renderer.create(arp::renderer::GuiRendererDesc{
        writeShaderFixture("rhi_gui_renderer_test_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
    });
    assert(created.success);
    assert(device.stats.shaderCreates == 1);
    assert(device.stats.graphicsPipelineCreates == 1);
    assert(renderer.graphicsPipeline());
    assert(renderer.graphicsPipeline()->alphaBlendEnabled());

    arp::gui::DrawData DrawData;
    DrawData.quads.push_back(arp::gui::DrawQuad{
        1,
        arp::gui::NodeType::Panel,
        {0.0f, 0.0f, 100.0f, 40.0f},
    });
    DrawData.textQuads.push_back(arp::gui::DrawTextQuad{
        2,
        {8.0f, 8.0f, 32.0f, 16.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    });

    arp::rhi::Texture frameColor{
        "ProvidedGuiBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };
    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor), DrawData);

    assert(drawn.success);
    assert(device.stats.textureCreates == 0);
    assert(device.stats.commandListCreates == 1);
    assert(device.stats.submitCalls == 1);
    assert(device.stats.renderPassBegins == 1);
    assert(device.stats.graphicsPipelineBinds == 1);
    assert(device.stats.pushConstantCalls == 2);
    assert(device.stats.drawCalls == 2);
    assert(device.stats.lastDrawVertexCount == 6);
}

void rhi_particle_renderer_records_particle_plan_through_abstract_rhi()
{
    CountingDevice device;
    arp::renderer::ParticleRenderer renderer(device);
    const auto created = renderer.create(arp::renderer::ParticleRendererDesc{
        writeShaderFixture("rhi_particle_renderer_test_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        64,
    });
    assert(created.success);
    assert(device.stats.shaderCreates == 1);
    assert(device.stats.graphicsPipelineCreates == 1);
    assert(renderer.graphicsPipeline());
    assert(renderer.graphicsPipeline()->alphaBlendEnabled());

    std::pmr::vector<arp::renderer::ParticleInstance> particles;
    arp::renderer::ParticleInstance particle;
    particle.position = {0.0f, 1.0f, 2.0f};
    particle.color = {1.0f, 0.6f, 0.2f, 0.8f};
    particle.size = 0.5f;
    particle.lifetimeSeconds = 1.0f;
    particles.push_back(particle);

    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    arp::rhi::Texture frameColor{
        "ProvidedParticleBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };

    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor), particles, scene);

    assert(drawn.success);
    assert(device.stats.bufferCreates == 1);
    assert(device.stats.commandListCreates == 1);
    assert(device.stats.submitCalls == 1);
    assert(device.stats.renderPassBegins == 1);
    assert(device.stats.graphicsPipelineBinds == 1);
    assert(device.stats.vertexBufferBinds == 1);
    assert(device.stats.pushConstantCalls == 1);
    assert(device.stats.drawCalls == 1);
    assert(device.stats.lastDrawVertexCount == 6);
}

void rhi_fxaa_renderer_records_postprocess_through_abstract_rhi()
{
    CountingDevice device;
    arp::renderer::FxaaRenderer renderer(device);
    const auto created = renderer.create(arp::renderer::FxaaRendererDesc{
        writeShaderFixture("rhi_fxaa_renderer_test_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
    });
    assert(created.success);
    assert(device.stats.shaderCreates == 1);
    assert(device.stats.graphicsPipelineCreates == 1);
    assert(device.stats.descriptorSetCreates == 1);
    assert(device.stats.samplerCreates == 1);
    assert(renderer.graphicsPipeline());
    assert(!renderer.graphicsPipeline()->depthTestEnabled());
    assert(renderer.graphicsPipeline()->descriptorSetLayoutCount() == 1);

    arp::rhi::Texture sceneColor{
        "FxaaSceneColor",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment | arp::rhi::TextureUsage::Sampled,
        1,
    };
    arp::rhi::Texture frameColor{
        "ProvidedFxaaBackbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment,
        1,
    };

    const auto drawn = renderer.drawFrame(testFrameContext(&frameColor), sceneColor);

    assert(drawn.success);
    assert(sceneColor.state() == arp::rhi::ResourceState::ShaderRead);
    assert(device.stats.textureCreates == 0);
    assert(device.stats.descriptorSetUpdates == 1);
    assert(device.stats.commandListCreates == 1);
    assert(device.stats.submitCalls == 1);
    assert(device.stats.textureTransitions == 1);
    assert(device.stats.renderPassBegins == 1);
    assert(device.stats.graphicsPipelineBinds == 1);
    assert(device.stats.descriptorSetBinds == 1);
    assert(device.stats.pushConstantCalls == 1);
    assert(device.stats.drawCalls == 1);
    assert(device.stats.lastDrawVertexCount == 3);

    const auto drawnAgain = renderer.drawFrame(testFrameContext(&frameColor), sceneColor);
    assert(drawnAgain.success);
    assert(device.stats.descriptorSetUpdates == 1);
    assert(device.stats.commandListCreates == 2);
    assert(device.stats.submitCalls == 2);
}

} // namespace

int main()
{
    rhi_scene_renderer_records_scene_plan_through_abstract_rhi();
    rhi_scene_renderer_records_pika_runtime_pass_pipelines();
    rhi_scene_renderer_records_gi_compute_when_enabled();
    rhi_scene_renderer_uploads_shadow_params_from_render_settings();
    rhi_scene_renderer_uploads_and_binds_material_textures_by_slot();
    rhi_scene_renderer_uploads_full_material_texture_set_by_slot();
    rhi_scene_renderer_uploads_pika_material_textures_by_slot();
    rhi_scene_renderer_reuses_duplicate_material_texture_sources();
    rhi_gui_renderer_records_gui_plan_through_abstract_rhi();
    rhi_particle_renderer_records_particle_plan_through_abstract_rhi();
    rhi_particle_draw_plan_set_groups_by_visual_family();
    rhi_particle_renderer_selects_correct_pipeline_per_family();
    rhi_fxaa_renderer_records_postprocess_through_abstract_rhi();
    return 0;
}
