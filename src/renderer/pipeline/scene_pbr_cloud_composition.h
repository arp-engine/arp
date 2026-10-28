#pragma once
#include "renderer/pipeline/scene_pbr_producer.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"

namespace arp::renderer {
struct ScenePbrCloudCompositionContext {
    const ScenePbrProducer& producer;
    const ScenePbrFrame& frame;
    const scene3d::Scene3DRenderQueue& queue;
    core::ArrayView<SceneOpaqueMeshBinding> bindings;
    cloud::VolumetricCloudFeature& cloud;
    cloud::CloudPreparedFeatureGraphContext cloudGraph;
    std::size_t pipeline = SIZE_MAX;
    core::ArrayView<std::size_t> sectionDescriptors;
    rhi::Color clear;
};

class ARP_API ScenePbrCloudComposition final {
public:
    static const char* view(const cloud::CloudFrameState&, ScenePbrView&) noexcept;
    static const char* validateView(
        const cloud::CloudFrameState&, const ScenePbrFrameConstants&) noexcept;

    explicit ScenePbrCloudComposition(ScenePbrCloudCompositionContext context) : context_(context)
    {}

    ScenePbrCloudComposition(const ScenePbrCloudComposition&) = delete;
    ScenePbrCloudComposition& operator=(const ScenePbrCloudComposition&) = delete;
    const char* compose(
        render_graph::FrameGraph&, render_graph::TextureHandle output, rhi::Extent2D);

    const auto& queue() const noexcept { return context_.queue; }

    std::size_t rasterPassCount() const noexcept { return context_.sectionDescriptors.size(); }

private:
    ScenePbrCloudCompositionContext context_;
    bool attempted_ = false;
};
} // namespace arp::renderer
