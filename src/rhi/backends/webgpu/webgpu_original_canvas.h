#pragma once

#include "core/lifetime_epoch.h"
#include "rhi/gpu_resource_types.h"
#include "rhi/gpu_swapchain.h"

namespace arp::rhi_webgpu {
class WebGpuRhiSwapchain;
class WebGpuConformanceClearExecution;
class WebGpuConformanceClearFrame;

// CanvasTexture/1: the wrapper and normalized native state use the exact
// configured surface usage. CopyDst is not advertised by this surface contract.
class WebGpuCanvasTextureContract final {
public:
    static rhi::TextureDesc describe(const rhi::SwapchainDesc& surface)
    {
        rhi::TextureDesc value;
        value.label = "WebGpuSwapchainCanvas";
        value.extent = {surface.extent.width, surface.extent.height, 1};
        value.format = surface.format;
        value.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        value.memory = rhi::MemoryUsage::GpuOnly;
        value.initialState = rhi::ResourceState::Undefined;
        value.mipLevelCount = value.arrayLayerCount = value.sampleCount = 1;
        value.cubeCompatible = false;
        return value;
    }
};

// Issued only by the actual swapchain after getCurrentTexture ownership transfer.
// Neither an ordinary texture nor a caller's serial can construct this identity.
class WebGpuOriginalCanvas final {
    friend struct WebGpuNativeTexturePayload;
    friend class WebGpuRhiSwapchain;
    friend class WebGpuConformanceClearExecution;
    friend class WebGpuConformanceClearFrame;

    class Key {
        friend class WebGpuRhiSwapchain;
        Key() = default;
    };

public:
    WebGpuOriginalCanvas(Key,
        core::LifetimeEpoch::Ticket surface,
        rhi::GpuResourceId image,
        std::uint32_t slot,
        std::uint64_t serial) noexcept
        : surface_(std::move(surface)), image_(image), slot_(slot), serial_(serial)
    {}

    WebGpuOriginalCanvas(const WebGpuOriginalCanvas&) = delete;
    WebGpuOriginalCanvas& operator=(const WebGpuOriginalCanvas&) = delete;

private:
    const core::LifetimeEpoch::Ticket surface_;
    const rhi::GpuResourceId image_;
    const std::uint32_t slot_;
    const std::uint64_t serial_;
    core::LifetimeEpoch::Ticket issuer_;
    bool claimed_ = false, submitted_ = false, observed_ = false, presented_ = false,
         discarded_ = false, retirementSafe_ = false;
};
} // namespace arp::rhi_webgpu
