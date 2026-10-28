#include "renderer/pipeline/scene_pbr_cloud_composition.h"

#include "renderer/pipeline/detail/scene_cloud_contract.h"

namespace arp::renderer {
const char* ScenePbrCloudComposition::view(
    const cloud::CloudFrameState& frame, ScenePbrView& output) noexcept
{
    const auto& p = frame.parameters;
    ScenePbrView next;
    // jitter.w is the existing GPU ABI for explicit CloudGraphContext::orthographic.
    // Do not infer a projection kind from matrix coefficients or a backend name.
    if (p.jitter.w == 0.f) {
        next = {ScenePbrView::Kind::Perspective, {p.cameraTime.x, p.cameraTime.y, p.cameraTime.z}};
    } else if (p.jitter.w == 1.f) {
        const auto nearClip =
            core::transformHomogeneous(p.inverseViewProjection, {0.f, 0.f, 0.f, 1.f});
        const auto farClip =
            core::transformHomogeneous(p.inverseViewProjection, {0.f, 0.f, 1.f, 1.f});
        if (!std::isfinite(nearClip.w) || !std::isfinite(farClip.w) || nearClip.w == 0.f
            || farClip.w == 0.f)
            return "SCENE-CLOUD-COMPOSITION-VIEW";
        const core::Vec3 nearPoint{
            nearClip.x / nearClip.w, nearClip.y / nearClip.w, nearClip.z / nearClip.w};
        const core::Vec3 farPoint{
            farClip.x / farClip.w, farClip.y / farClip.w, farClip.z / farClip.w};
        next = {ScenePbrView::Kind::Orthographic, nearPoint - farPoint};
    } else
        return "SCENE-CLOUD-COMPOSITION-VIEW";
    core::Vec4 checked;
    if (ScenePbrProducer::viewConstants(next, checked))
        return "SCENE-CLOUD-COMPOSITION-VIEW";
    output = next;
    return nullptr;
}

const char* ScenePbrCloudComposition::validateView(
    const cloud::CloudFrameState& cloudFrame, const ScenePbrFrameConstants& frame) noexcept
{
    ScenePbrView input;
    if (const auto* id = view(cloudFrame, input))
        return id;
    core::Vec4 expected;
    if (ScenePbrProducer::viewConstants(input, expected))
        return "SCENE-CLOUD-COMPOSITION-VIEW";
    const auto& actual = frame.cameraPosition;
    return actual.x == expected.x && actual.y == expected.y && actual.z == expected.z
            && actual.w == expected.w
        ? nullptr
        : "SCENE-CLOUD-COMPOSITION-VIEW";
}

const char* ScenePbrCloudComposition::compose(
    render_graph::FrameGraph& graph, render_graph::TextureHandle output, rhi::Extent2D extent)
try {
    const auto& c = context_;
    if (attempted_)
        return "SCENE-CLOUD-COMPOSITION-SINGLE-USE";
    if (const auto* id = detail::SceneCloudContract::scene(
            graph, c.queue, c.cloud, c.cloudGraph, output, extent))
        return id;
    if (!c.producer.geometry())
        return "SCENE-CLOUD-COMPOSITION-GEOMETRY";
    if (const auto* id = c.producer.geometry()->validateCurrent(c.queue, c.bindings))
        return id;
    if (const auto* id = detail::SceneCloudContract::pending(
            c.queue, c.cloud, c.frame.values().viewProjection, c.frame.values().lighting))
        return id;
    const auto& pending = c.cloud.preparedHistory()->pendingFrame();
    if (const auto* id = validateView(pending, c.frame.values()))
        return id;
    const ScenePbrGraphContext raster{c.cloudGraph.execution,
        c.frame,
        c.cloudGraph.color,
        c.cloudGraph.depth,
        c.pipeline,
        c.sectionDescriptors,
        c.clear};
    if (const auto* id = c.producer.validate(raster))
        return id;
    if (const auto result = c.cloud.validateGraph(c.cloudGraph); !result.success)
        return result.diagnostic;
    attempted_ = true;
    if (const auto* id = c.producer.contribute(raster))
        return id;
    cloud::CloudOutputs outputs;
    const auto result = c.cloud.contributeGraph(c.cloudGraph, outputs);
    return result.success ? nullptr : result.diagnostic;
} catch (const std::bad_alloc&) {
    return "SCENE-CLOUD-COMPOSITION-CPU-BUDGET";
} catch (const std::out_of_range&) {
    return "SCENE-CLOUD-COMPOSITION-HANDLE";
}
} // namespace arp::renderer
