#include "2d/scene_2d_render_queue.h"
#include "3d/scene_3d_render_queue.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/pipeline/scene_runtime_render_pipeline.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/scene_runtime_frame.h"
#include "rhi/backends/null/null_backend.h"
#include "scene/camera.h"
#include "scene/scene_traversal_snapshot.h"
#include "ui/ui.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

arp::core::pmr::String readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

void requireText(const arp::core::pmr::String& text, const char* token)
{
    assert(text.find(token) != arp::core::pmr::String::npos);
}

void forbidText(const arp::core::pmr::String& text, const char* token)
{
    assert(text.find(token) == arp::core::pmr::String::npos);
}

arp::scene::SceneTraversalSnapshot makeSnapshot()
{
    arp::core::pmr::Vector<arp::scene::SceneTraversalNode> nodes;
    arp::scene::SceneTraversalNode node;
    node.objectId = arp::scene::GameObjectId::fromValue(1);
    node.active = true;
    node.activeInHierarchy = true;
    nodes.push_back(node);
    return arp::scene::SceneTraversalSnapshot::success(std::move(nodes));
}

arp::core::pmr::Vector<arp::scene::ResolvedCamera> makeCameras()
{
    arp::core::pmr::Vector<arp::scene::ResolvedCamera> cameras;
    arp::scene::ResolvedCamera camera;
    camera.objectId = arp::scene::GameObjectId::fromValue(2);
    camera.componentId = arp::scene::ComponentId::fromValue(3);
    camera.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameras.push_back(camera);
    return cameras;
}

arp::scene3d::Scene3DRenderQueue make3DQueue()
{
    arp::scene3d::Scene3DRenderQueue queue;
    arp::scene3d::Scene3DRenderItem item;
    item.kind = arp::scene3d::Scene3DRenderItemKind::Mesh;
    item.objectId = arp::scene::GameObjectId::fromValue(4);
    item.mesh = arp::renderer::MeshHandle{5};
    item.material = arp::renderer::MaterialHandle{6};
    item.worldBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    queue.addItem(item);
    return queue;
}

arp::scene2d::Scene2DRenderQueue make2DQueue()
{
    arp::scene2d::Scene2DRenderQueue queue;
    arp::scene2d::Scene2DRenderItem item;
    item.kind = arp::scene2d::Scene2DRenderItemKind::Sprite;
    item.objectId = arp::scene::GameObjectId::fromValue(7);
    item.sprite = arp::scene2d::SpriteHandle{8};
    item.material = arp::renderer::MaterialHandle{9};
    item.texture = arp::renderer::TextureHandle{10};
    item.worldBounds =
        arp::scene2d::Bounds2D::explicitBounds({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f});
    queue.addSprite(item);
    return queue;
}

arp::ui::RenderQueue makeUiQueue()
{
    arp::ui::RenderQueue queue;
    arp::ui::UIRenderItem item;
    item.widgetId = arp::ui::WidgetId::fromValue(11);
    item.rect = arp::ui::LayoutRect::fromOriginSize(4.0f, 4.0f, 16.0f, 8.0f);
    item.clipRect = item.rect;
    item.material = arp::renderer::MaterialHandle{12};
    item.texture = arp::renderer::TextureHandle{13};
    queue.addItem(item);
    return queue;
}

arp::renderer::RendererSceneRuntimeFrame makeFrame(
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
    const arp::scene3d::Scene3DRenderQueue& queue3D,
    const arp::scene2d::Scene2DRenderQueue& queue2D,
    const arp::ui::RenderQueue& uiQueue)
{
    arp::renderer::RendererSceneRuntimeFrameInput input;
    input.snapshot = &snapshot;
    input.resolvedCameras = &cameras;
    input.scene3DQueue = &queue3D;
    input.scene2DQueue = &queue2D;
    input.uiQueue = &uiQueue;
    input.oneCompatibleWorldUiPassIntent = true;

    arp::renderer::RendererSceneRuntimeFrameBuilder builder({3, 8, 8, 8});
    const auto result = builder.build(input);
    assert(result.ok());
    return result.frame();
}

struct ScenePayloadFixture {
    arp::scene::SceneTraversalSnapshot snapshot;
    arp::core::pmr::Vector<arp::scene::ResolvedCamera> cameras;
    arp::scene3d::Scene3DRenderQueue queue3D;
    arp::scene2d::Scene2DRenderQueue queue2D;
    arp::ui::RenderQueue uiQueue;
    arp::renderer::RendererSceneRuntimeFrame frame;
    arp::renderer::SceneRuntimePipelinePayload payload;

    ScenePayloadFixture()
        : snapshot(makeSnapshot()), cameras(makeCameras()), queue3D(make3DQueue()),
          queue2D(make2DQueue()), uiQueue(makeUiQueue()),
          frame(makeFrame(snapshot, cameras, queue3D, queue2D, uiQueue)), payload({
                                                                              &snapshot,
                                                                              &cameras,
                                                                              &queue3D,
                                                                              &queue2D,
                                                                              &uiQueue,
                                                                              &frame,
                                                                          })
    {}
};

arp::renderer::RenderFrameRequest makeRequest(
    const arp::renderer::SceneRuntimePipelinePayload& payload)
{
    arp::renderer::RenderFrameRequest request;
    request.view.extent = {320, 180};
    request.view.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    request.view.frameIndex = 1u;
    request.pipelinePayload = &payload;
    request.pipelinePayloadContractName = arp::renderer::kSceneRuntimePipelinePayloadContract;
    request.pipelinePayloadContractVersion = arp::renderer::kSceneRuntimePipelinePayloadVersion;
    return request;
}

arp::render_graph::TextureHandle createBackbuffer(
    arp::render_graph::FrameGraph& graph, arp::rhi::GpuDevice& device)
{
    return graph.importTexture(device.createTexture({
        "SceneRuntimeBackbuffer",
        {320u, 180u, 1u},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Present,
        1u,
    }));
}

arp::renderer::RenderPipelineGraphCompositionContext makeCompositionContext(
    const arp::renderer::RenderFrameRequest& request)
{
    arp::rhi::AcquiredFrame acquired;
    acquired.success = true;
    acquired.frameSerial = 9u;
    acquired.frameIndex = 1u;

    return {
        acquired,
        request,
        true,
    };
}

void scene_runtime_pipeline_composes_one_world_ui_pass()
{
    ScenePayloadFixture fixture;

    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);

    arp::renderer::SceneRuntimeRenderPipeline pipeline;
    const auto created = pipeline.create({
        *device,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureFormat::Unknown,
        {320, 180},
    });
    assert(created.success);
    assert(pipeline.desc().name == arp::core::pmr::String("SceneRuntimeRenderPipeline"));

    const auto composed = pipeline.composeGraph(
        graph, backbuffer, makeCompositionContext(makeRequest(fixture.payload)), {});
    assert(composed.success);

    const auto& record = pipeline.lastSemanticRecord();
    assert(record.physicalPassCount == 1u);
    assert(std::strcmp(record.passName, "SceneRuntimeWorldUiPass") == 0);
    assert(record.queueCount == 3u);
    assert(record.queues[0].kind == arp::renderer::RendererSceneRuntimeQueueKind::Scene3D);
    assert(record.queues[1].kind == arp::renderer::RendererSceneRuntimeQueueKind::Scene2D);
    assert(record.queues[2].kind == arp::renderer::RendererSceneRuntimeQueueKind::UI);
    assert(record.queues[0].order < record.queues[1].order);
    assert(record.queues[1].order < record.queues[2].order);

    graph.execute();
    assert(!graph.executionTrace().empty());
    assert(graph.executionTrace()[0] == arp::core::pmr::String("SceneRuntimeWorldUiPass"));
}

void scene_runtime_pipeline_rejects_invalid_payload_before_casting()
{
    ScenePayloadFixture fixture;
    auto request = makeRequest(fixture.payload);
    request.pipelinePayloadContractName = "renderer/WrongPayload";

    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);

    arp::renderer::SceneRuntimeRenderPipeline pipeline;
    const auto created = pipeline.create({
        *device,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureFormat::Unknown,
        {320, 180},
    });
    assert(created.success);

    const auto composed =
        pipeline.composeGraph(graph, backbuffer, makeCompositionContext(request), {});
    assert(!composed.success);
    assert(composed.diagnostics.find("contract name mismatch") != arp::core::pmr::String::npos);
}

void scene_runtime_pipeline_rejects_implicit_multi_pass_payload()
{
    ScenePayloadFixture fixture;
    fixture.payload.requiresMultiplePhysicalPasses = true;

    auto device = arp::rhi::createNullDevice();
    assert(device);
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);

    arp::renderer::SceneRuntimeRenderPipeline pipeline;
    const auto created = pipeline.create({
        *device,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureFormat::Unknown,
        {320, 180},
    });
    assert(created.success);

    const auto composed = pipeline.composeGraph(
        graph, backbuffer, makeCompositionContext(makeRequest(fixture.payload)), {});
    assert(!composed.success);
    assert(composed.diagnostics.find("multi-pass") != arp::core::pmr::String::npos);
    assert(composed.diagnostics.find("explicit") != arp::core::pmr::String::npos);
}

void scene_runtime_pipeline_source_stays_backend_neutral_and_bounded()
{
    const auto root = projectRoot();
    const auto header =
        readText(root / "src" / "renderer" / "pipeline" / "scene_runtime_render_pipeline.h");
    const auto source =
        readText(root / "src" / "renderer" / "pipeline" / "scene_runtime_render_pipeline.cpp");
    const auto combined = header + source;

    requireText(combined, "SceneRuntimeWorldUiPass");
    requireText(combined, "validateSceneRuntimePipelinePayload");
    requireText(combined, "kSceneRuntimeRenderPipelineMaxQueues");
    forbidText(combined, "vulkan");
    forbidText(combined, "Vulkan");
    forbidText(combined, "Metal");
    forbidText(combined, "WebGPU");
    forbidText(combined, "wgpu");
    forbidText(combined, "std::vector");
    forbidText(combined, "std::pmr::vector");
    forbidText(combined, "std::make_unique");
    forbidText(combined, "new ");
}

} // namespace

int main()
{
    scene_runtime_pipeline_composes_one_world_ui_pass();
    scene_runtime_pipeline_rejects_invalid_payload_before_casting();
    scene_runtime_pipeline_rejects_implicit_multi_pass_payload();
    scene_runtime_pipeline_source_stays_backend_neutral_and_bounded();
    return 0;
}
