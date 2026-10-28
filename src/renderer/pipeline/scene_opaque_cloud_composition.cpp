#include "renderer/pipeline/scene_opaque_cloud_composition.h"

#include "renderer/pipeline/detail/scene_cloud_contract.h"

namespace arp::renderer {
const char* SceneOpaqueCloudComposition::compose(
    render_graph::FrameGraph& graph, render_graph::TextureHandle output, rhi::Extent2D extent)
try {
    const auto& c = context_;
    const auto& cg = c.cloudGraph;
    if (attempted_)
        return "SCENE-CLOUD-COMPOSITION-SINGLE-USE";
    if (const auto* id =
            detail::SceneCloudContract::scene(graph, c.queue, c.cloud, cg, output, extent))
        return id;
    if (const auto* id = c.geometry.validateCurrent(c.queue, c.bindings))
        return id;
    if (c.producer.geometry().get() != &c.geometry)
        return "SCENE-CLOUD-COMPOSITION-GEOMETRY";
    if (const auto* id = detail::SceneCloudContract::pending(
            c.queue, c.cloud, c.frame.values().viewProjection, c.frame.values().lighting))
        return id;
    const SceneOpaqueGraphContext opaque{
        cg.execution, c.frame, cg.color, cg.depth, c.opaquePipeline, c.opaqueDescriptors, c.clear};
    if (const auto* id = c.producer.validate(opaque))
        return id;
    if (const auto result = c.cloud.validateGraph(cg); !result.success)
        return result.diagnostic;
    // From this point any allocation failure consumes the context: discard the
    // graph and cancel the unsubmitted Feature frame; never execute a prefix.
    attempted_ = true;
    if (const auto* id = c.producer.contribute(opaque))
        return id;
    cloud::CloudOutputs result;
    const auto contribution = c.cloud.contributeGraph(cg, result);
    return contribution.success ? nullptr : contribution.diagnostic;
} catch (const std::bad_alloc&) {
    return "SCENE-CLOUD-COMPOSITION-CPU-BUDGET";
} catch (const std::out_of_range&) {
    return "SCENE-CLOUD-COMPOSITION-HANDLE";
}
} // namespace arp::renderer
