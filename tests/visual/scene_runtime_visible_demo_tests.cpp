#include "renderer/scene_runtime_frame.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"
#include "samples/scene_runtime_demo/scene_runtime_demo.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

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

void assertDemoUsesCustomGraphPath()
{
    const auto source =
        readText(projectRoot() / "samples" / "scene_runtime_demo" / "scene_runtime_demo.cpp");

    forbidText(source, "SceneRuntimeRenderPipeline");
    requireText(source, "SceneRuntimeDemoCompatibilityPipeline");
    requireText(source, "renderSystem.setRenderPipeline");
    requireText(source, "SceneRuntimePipelinePayload");
    requireText(source, "CustomRenderFrameWorkload");
    requireText(source, "renderSystem.acquireFrame()");
    requireText(source, "renderSystem.renderGraphFrame");
    requireText(source, "renderSystem.presentFrame");
    requireText(source, "custom graph scene-runtime path requested without a ready pipeline");
    requireText(source, "prepareSceneRuntimeCustomGraphFrame");
    requireText(source, "RendererSceneRuntimeFrameRhiPlanner");
    requireText(source, "if (options.validateDirectRhiSubmit)");
    requireText(source, "submitRhiProductionFrame");

    const auto guard = source.find("if (options.validateDirectRhiSubmit)");
    const auto submit = source.find("submitRhiProductionFrame");
    assert(guard != arp::core::pmr::String::npos);
    assert(submit != arp::core::pmr::String::npos);
    assert(guard < submit);
}

} // namespace

int main()
{
    assertDemoUsesCustomGraphPath();

    arp::samples::scene_runtime_demo::SceneRuntimeDemoFrameBuilder builder;
    const auto frame = builder.build(0.25f);

    assert(frame.success);
    assert(frame.diagnostics.empty());
    assert(frame.rendererFrame.statusLabel()
        == arp::renderer::RendererSceneRuntimeStatusLabel::ContractComplete);
    assert(frame.rendererFrame.drawItemCount() == 3u);
    assert(frame.rendererFrame.queueRecords().size() == 3u);
    assert(frame.framePlan.drawPlans.size() == 1u);
    assert(frame.framePlan.drawPlans.front().workKind == arp::renderer::RendererRhiWorkKind::Scene);
    assert(frame.framePlan.drawPlans.front().drawCommandCount == 3u);
    assert(!frame.nativeHandleExposedToUpperLayer);
    return 0;
}
