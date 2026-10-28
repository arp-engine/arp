#pragma once
#include "renderer/pipeline/scene_opaque_producer.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"

namespace arp::renderer {
// Borrowed cold context: all owners must outlive graph preparation/execution.
// There is one original execution and one cloud Feature. No alternate journal.
struct SceneOpaqueCloudCompositionContext {
    const SceneOpaqueProducer& producer;
    const SceneOpaqueFrame& frame;
    const SceneOpaqueGeometry& geometry;
    const scene3d::Scene3DRenderQueue& queue;
    core::ArrayView<SceneOpaqueMeshBinding> bindings;
    cloud::VolumetricCloudFeature& cloud;
    cloud::CloudPreparedFeatureGraphContext cloudGraph;
    std::size_t opaquePipeline = SIZE_MAX, opaqueDescriptors = SIZE_MAX;
    rhi::Color clear;
};

class ARP_API SceneOpaqueCloudComposition final {
public:
    explicit SceneOpaqueCloudComposition(SceneOpaqueCloudCompositionContext context)
        : context_(context)
    {}

    SceneOpaqueCloudComposition(const SceneOpaqueCloudComposition&) = delete;
    SceneOpaqueCloudComposition& operator=(const SceneOpaqueCloudComposition&) = delete;
    const char* compose(
        render_graph::FrameGraph&, render_graph::TextureHandle output, rhi::Extent2D);

    const auto& queue() const noexcept { return context_.queue; }

private:
    SceneOpaqueCloudCompositionContext context_;
    bool attempted_ = false;
};
} // namespace arp::renderer
