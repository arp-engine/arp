#pragma once
#include "renderer/pipeline/basic_forward_contract.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"
#include "renderer/volumetric_cloud/cloud_prepared_history.h"
#include "renderer/volumetric_cloud/cloud_scene_binding.h"

#include <tuple>

namespace arp::renderer::detail {
class SceneCloudContract final {
private:
    static auto layerFields(const scene3d::CloudLayerDesc& d) noexcept
    {
        return std::tie(d.planetCenter.x,
            d.planetCenter.y,
            d.planetCenter.z,
            d.planetRadius,
            d.baseAltitude,
            d.thickness,
            d.extinction,
            d.scatteringAlbedo.x,
            d.scatteringAlbedo.y,
            d.scatteringAlbedo.z,
            d.anisotropy,
            d.windVelocity.x,
            d.windVelocity.y,
            d.windVelocity.z,
            d.weatherPeriod,
            d.shapePeriod,
            d.detailPeriod,
            d.coverage,
            d.erosion);
    }

public:
    static const char* scene(render_graph::FrameGraph& graph,
        const scene3d::Scene3DRenderQueue& queue,
        const cloud::VolumetricCloudFeature& cloud,
        const cloud::CloudPreparedFeatureGraphContext& cloudGraph,
        render_graph::TextureHandle output,
        rhi::Extent2D extent)
    {
        if (!queue.hasCloud())
            return "SCENE-CLOUD-COMPOSITION-CLOUD-ABSENT";
        const auto* sceneBinding = cloud.preparedSceneBinding();
        if (!sceneBinding || queue.cloud().densityResource != sceneBinding->authoredReference())
            return "SCENE-CLOUD-COMPOSITION-SCENE-BINDING";
        if (queue.hasFog() || !queue.fogVolumes().empty() || queue.animationBindingCount()
            || queue.lightCount())
            return "SCENE-CLOUD-COMPOSITION-UNSUPPORTED-SCENE";
        if (&graph != &cloudGraph.execution.graph() || output.id != cloudGraph.output.id)
            return "SCENE-CLOUD-COMPOSITION-GRAPH";
        if (graph.texture(output).desc().extent != rhi::Extent3D{extent.width, extent.height, 1u})
            return "SCENE-CLOUD-COMPOSITION-EXTENT";
        return nullptr;
    }

    static const char* pending(const scene3d::Scene3DRenderQueue& queue,
        cloud::VolumetricCloudFeature& cloud,
        const core::Mat4& vp,
        const BasicForwardLightingConstants& light)
    {
        const auto* history = cloud.preparedHistory();
        if (!history || !history->constants())
            return "SCENE-CLOUD-COMPOSITION-PENDING-FRAME";
        const auto& pending = history->pendingFrame();
        if (layerFields(queue.cloud().layer) != layerFields(pending.layer))
            return "SCENE-CLOUD-COMPOSITION-LAYER";
        if (vp.m != pending.rasterViewProjection.m)
            return "SCENE-CLOUD-COMPOSITION-CAMERA";
        const auto& p = pending.parameters;
        const auto incoming =
            core::normalize(core::Vec3{-p.sunDirection.x, -p.sunDirection.y, -p.sunDirection.z});
        if (light.directionIntensity.w != 1.f || light.directionIntensity.x != incoming.x
            || light.directionIntensity.y != incoming.y || light.directionIntensity.z != incoming.z
            || light.lightColor.x != p.sunRadiance.x || light.lightColor.y != p.sunRadiance.y
            || light.lightColor.z != p.sunRadiance.z || light.ambientColor.x != p.ambientRadiance.x
            || light.ambientColor.y != p.ambientRadiance.y
            || light.ambientColor.z != p.ambientRadiance.z)
            return "SCENE-CLOUD-COMPOSITION-LIGHT";
        return nullptr;
    }
};
} // namespace arp::renderer::detail
