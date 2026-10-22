#include "3d/camera_parameters.h"
#include "core/memory/cpu_allocator.h"
#include "gui/gui.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderers.h"
#include "samples/stylized/stylized_scene_renderer.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

struct RecordingStats {
    std::uint32_t bufferCreates = 0;
    std::uint32_t textureCreates = 0;
    std::uint32_t shaderCreates = 0;
    std::uint32_t graphicsPipelineCreates = 0;
    std::uint32_t samplerCreates = 0;
    std::uint32_t descriptorSetCreates = 0;
    std::uint32_t descriptorSetUpdates = 0;
    std::uint32_t commandListCreates = 0;
    std::uint32_t submitCalls = 0;
    std::uint32_t renderPassBegins = 0;
    std::uint32_t graphicsPipelineBinds = 0;
    std::uint32_t descriptorSetBinds = 0;
    std::uint32_t vertexBufferBinds = 0;
    std::uint32_t indexBufferBinds = 0;
    std::uint32_t pushConstantCalls = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t drawIndexedCalls = 0;
    std::uint32_t bufferUploads = 0;
};

class RecordingCommandList final : public arp::rhi::CommandList {
public:
    explicit RecordingCommandList(RecordingStats& stats) : stats_(stats) {}

    void begin(const std::pmr::string&) override {}

    void transition(
        arp::rhi::Texture& texture, arp::rhi::ResourceState, arp::rhi::ResourceState after) override
    {
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

    void bindGraphicsPipeline(arp::rhi::GraphicsPipeline&) override
    {
        ++stats_.graphicsPipelineBinds;
    }

    void bindComputePipeline(arp::rhi::ComputePipeline&) override {}

    void bindDescriptorSet(arp::rhi::DescriptorSet&) override { ++stats_.descriptorSetBinds; }

    void bindVertexBuffer(arp::rhi::Buffer&) override { ++stats_.vertexBufferBinds; }

    void bindIndexBuffer(arp::rhi::Buffer&) override { ++stats_.indexBufferBinds; }

    void pushConstants(const void*, std::uint64_t) override { ++stats_.pushConstantCalls; }

    void draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override
    {
        ++stats_.drawCalls;
    }

    void drawIndexed(
        std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override
    {
        ++stats_.drawIndexedCalls;
    }

    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}

    void end() override {}

private:
    RecordingStats& stats_;
};

class RecordingDevice final : public arp::rhi::Device {
public:
    RecordingStats stats;

    arp::rhi::Backend backend() const override { return arp::rhi::Backend::Null; }

    std::unique_ptr<arp::rhi::Texture> createTexture(const arp::rhi::TextureDesc& desc) override
    {
        ++stats.textureCreates;
        return std::make_unique<arp::rhi::Texture>(desc.name,
            desc.extent,
            desc.initialState,
            desc.format,
            desc.usage,
            desc.mipCount,
            desc.dimension,
            desc.arrayLayers,
            desc.sampleCount,
            desc.clearColor,
            desc.clearDepth);
    }

    std::unique_ptr<arp::rhi::Buffer> createBuffer(const arp::rhi::BufferDesc& desc) override
    {
        ++stats.bufferCreates;
        return std::make_unique<arp::rhi::Buffer>(desc.name,
            desc.sizeBytes,
            desc.initialState,
            desc.usage,
            desc.memoryLocation,
            desc.initialData ? desc.initialDataSizeBytes : 0u);
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
        return std::make_unique<arp::rhi::GraphicsPipeline>(desc.name,
            desc.vertexLayout,
            desc.colorFormat,
            desc.viewportExtent,
            desc.pushConstantSizeBytes,
            desc.alphaBlendEnabled,
            desc.depthTestEnabled,
            desc.depthFormat,
            static_cast<std::uint32_t>(desc.descriptorSets.size()),
            desc.cullMode,
            desc.depthWriteEnabled,
            desc.frontFace,
            desc.pipelineLayout);
    }

    std::unique_ptr<arp::rhi::ComputePipeline> createComputePipeline(
        const arp::rhi::ComputePipelineDesc& desc) override
    {
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
        return arp::rhi::Device::updateDescriptorSet(descriptorSet, writes);
    }

    arp::rhi::ResourceValidationResult uploadBuffer(const arp::rhi::BufferUploadDesc& desc) override
    {
        ++stats.bufferUploads;
        return arp::rhi::Device::uploadBuffer(desc);
    }

    std::unique_ptr<arp::rhi::CommandList> createCommandList() override
    {
        ++stats.commandListCreates;
        return std::make_unique<RecordingCommandList>(stats);
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

arp::renderer::RenderFrameContext graphFrameContext(arp::rhi::Texture& color,
    arp::rhi::Texture* depth = nullptr,
    const arp::rhi::DepthTargetView* depthTarget = nullptr)
{
    return arp::renderer::RenderFrameContext{
        arp::rhi::AcquiredFrame{true, "", 0u, 11u},
        arp::rhi::RenderTargetView{
            color.name(),
            color.extent(),
            color.format(),
            arp::rhi::ResourceState::ColorAttachment,
            0u,
        },
        depthTarget,
        &color,
        depth,
    };
}

arp::render_graph::TextureHandle importBackbuffer(arp::render_graph::FrameGraph& graph)
{
    return graph.importTexture(std::make_unique<arp::rhi::Texture>("GraphBackbuffer",
        arp::rhi::Extent2D{640u, 360u},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment | arp::rhi::TextureUsage::Sampled,
        1u));
}

void assertGraphOwnedSubmissionOnly(
    const RecordingStats& stats, std::uint32_t commandListsBefore, std::uint32_t submitsBefore)
{
    assert(stats.commandListCreates == commandListsBefore + 1u);
    assert(stats.submitCalls == submitsBefore + 1u);
}

void gui_graph_pass_records_without_internal_command_list_or_submit()
{
    RecordingDevice device;
    arp::renderer::GuiRenderer renderer(device);
    const auto created = renderer.create(arp::renderer::GuiRendererDesc{
        writeShaderFixture("renderer_graph_pass_gui_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640u, 360u},
    });
    assert(created.success);

    arp::gui::DrawData DrawData;
    DrawData.quads.push_back(arp::gui::DrawQuad{
        1u,
        arp::gui::NodeType::Panel,
        {0.0f, 0.0f, 100.0f, 40.0f},
    });

    arp::render_graph::FrameGraph graph(device);
    const auto backbuffer = importBackbuffer(graph);
    const auto commandListsBefore = device.stats.commandListCreates;
    const auto submitsBefore = device.stats.submitCalls;
    graph.addPass("Gui")
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store)
        .execute([&](arp::render_graph::RenderContext& context) {
            const auto recorded = renderer.recordPass(
                context, graphFrameContext(graph.texture(backbuffer)), DrawData);
            assert(recorded.success);
        });

    graph.execute();
    assertGraphOwnedSubmissionOnly(device.stats, commandListsBefore, submitsBefore);
    assert(device.stats.renderPassBegins == 1u);
    assert(device.stats.drawCalls == 1u);
}

void particle_graph_pass_records_without_internal_command_list_or_submit()
{
    RecordingDevice device;
    arp::renderer::ParticleRenderer renderer(device);
    const auto created = renderer.create(arp::renderer::ParticleRendererDesc{
        writeShaderFixture("renderer_graph_pass_particle_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640u, 360u},
        16u,
    });
    assert(created.success);

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

    arp::render_graph::FrameGraph graph(device);
    const auto backbuffer = importBackbuffer(graph);
    const auto commandListsBefore = device.stats.commandListCreates;
    const auto submitsBefore = device.stats.submitCalls;
    const auto buffersBefore = device.stats.bufferCreates;
    graph.addPass("Particles")
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store)
        .execute([&](arp::render_graph::RenderContext& context) {
            const auto recorded = renderer.recordPass(
                context, graphFrameContext(graph.texture(backbuffer)), particles, scene);
            assert(recorded.success);
        });

    graph.execute();
    assertGraphOwnedSubmissionOnly(device.stats, commandListsBefore, submitsBefore);
    assert(device.stats.bufferCreates == buffersBefore);
    assert(device.stats.bufferUploads == 1u);
    assert(device.stats.vertexBufferBinds == 1u);
    assert(device.stats.drawCalls == 1u);
}

void fxaa_graph_pass_records_without_internal_command_list_or_submit()
{
    RecordingDevice device;
    arp::renderer::FxaaRenderer renderer(device);
    const auto created = renderer.create(arp::renderer::FxaaRendererDesc{
        writeShaderFixture("renderer_graph_pass_fxaa_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640u, 360u},
    });
    assert(created.success);

    auto sceneColor = device.createTexture(arp::rhi::TextureDesc{
        "FxaaSceneColor",
        {640u, 360u},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment | arp::rhi::TextureUsage::Sampled,
        1u,
    });
    assert(sceneColor);

    arp::render_graph::FrameGraph graph(device);
    const auto backbuffer = importBackbuffer(graph);
    const auto commandListsBefore = device.stats.commandListCreates;
    const auto submitsBefore = device.stats.submitCalls;
    graph.addPass("Fxaa")
        .read(backbuffer, arp::rhi::ResourceState::ShaderRead)
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store)
        .execute([&](arp::render_graph::RenderContext& context) {
            const auto recorded = renderer.recordPass(
                context, graphFrameContext(graph.texture(backbuffer)), *sceneColor);
            assert(recorded.success);
        });

    graph.execute();
    assertGraphOwnedSubmissionOnly(device.stats, commandListsBefore, submitsBefore);
    assert(device.stats.descriptorSetUpdates == 1u);
    assert(device.stats.descriptorSetBinds == 1u);
    assert(device.stats.drawCalls == 1u);
}

void scene_graph_pass_records_without_internal_command_list_or_submit()
{
    RecordingDevice device;
    arp::samples::stylized::StylizedSceneRenderer renderer(device);
    const auto created = renderer.create(arp::samples::stylized::StylizedSceneRendererDesc{
        writeShaderFixture("renderer_graph_pass_scene_shader.bin"),
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640u, 360u},
        "",
    });
    assert(created.success);

    auto mesh = arp::renderer::uploadMeshToRhi(device, arp::renderer::MeshAsset::debugTriangle());
    assert(mesh.success);

    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    auto depth = device.createTexture(arp::rhi::TextureDesc{
        "GraphDepth",
        {640u, 360u},
        arp::rhi::ResourceState::DepthAttachment,
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::DepthAttachment,
        1u,
    });
    assert(depth);
    arp::rhi::DepthTargetView depthTarget{
        "GraphDepth",
        {640u, 360u},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::ResourceState::DepthAttachment,
    };

    arp::render_graph::FrameGraph graph(device);
    const auto backbuffer = importBackbuffer(graph);
    const auto commandListsBefore = device.stats.commandListCreates;
    const auto submitsBefore = device.stats.submitCalls;
    graph.addPass("MainScene")
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store)
        .execute([&](arp::render_graph::RenderContext& context) {
            const auto recorded = renderer.recordPass(context,
                graphFrameContext(graph.texture(backbuffer), depth.get(), &depthTarget),
                mesh.mesh,
                scene,
                arp::samples::stylized::SceneStylizedMaterial::defaults(),
                arp::samples::stylized::CharacterMaterialSet::defaultToonCharacter());
            assert(recorded.success);
        });

    graph.execute();
    assertGraphOwnedSubmissionOnly(device.stats, commandListsBefore, submitsBefore);
    assert(device.stats.renderPassBegins == 1u);
    assert(device.stats.indexBufferBinds == 1u);
    assert(device.stats.drawIndexedCalls == 1u);
}

} // namespace

int main()
{
    gui_graph_pass_records_without_internal_command_list_or_submit();
    particle_graph_pass_records_without_internal_command_list_or_submit();
    fxaa_graph_pass_records_without_internal_command_list_or_submit();
    scene_graph_pass_records_without_internal_command_list_or_submit();
    return 0;
}
