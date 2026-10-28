#pragma once

#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"

namespace arp::cloud_demo {

// Finite Prototype caller of the same original FrameGraph path. This owns no
// backend payload or swapchain authority. Timing is an independent explicit
// opt-in to the existing original query and FrameGraph contracts.
struct PreparedOffscreenRequest {
    rhi::GpuDevice& device;
    const assets::cloud::CloudAsset& asset;
    renderer::cloud::CloudRenderConfig config;
    rhi::Extent2D extent;
    scene3d::CloudLayerDesc layer;
    core::pmr::String camera, capture;
    std::uint32_t frames = 0u, captureFrame = 0u;
    bool reconstruction = false;
    bool timings = false;
    std::uint32_t warmupFrames = 0u;
    bool hostDeviceBuffers = false;
    bool compactHistory = false;
    std::shared_ptr<const renderer::cloud::CloudSceneBinding> sceneBinding;
    const scene3d::Scene3DRenderQueue* sceneQueue = nullptr;
    const resource::ResourceManager* sceneResources = nullptr;
};

int runPreparedOffscreen(const PreparedOffscreenRequest&);

} // namespace arp::cloud_demo
