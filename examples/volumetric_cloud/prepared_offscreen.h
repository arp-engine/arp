#pragma once

#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"

namespace arp::cloud_demo {

// Finite Prototype caller of the same original FrameGraph path. This owns no
// backend payload and grants neither swapchain nor timestamp recording.
struct PreparedOffscreenRequest {
    rhi::GpuDevice& device;
    const assets::cloud::CloudAsset& asset;
    renderer::cloud::CloudRenderConfig config;
    rhi::Extent2D extent;
    scene3d::CloudLayerDesc layer;
    core::pmr::String camera, capture;
    std::uint32_t frames = 0u, captureFrame = 0u;
    bool reconstruction = false;
};

int runPreparedOffscreen(const PreparedOffscreenRequest&);

} // namespace arp::cloud_demo
