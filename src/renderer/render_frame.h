#pragma once

#include "core/memory/cpu_allocator.h"
#include "gui/gui.h"
#include "renderer/renderer_result.h"
#include "renderer/ui/ui_draw_plan.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_swapchain.h"

#include <cstdint>
#include <string>

namespace arp::render_graph {
class RenderContext;
}

namespace arp::renderer::ui {
struct GlyphAtlasUploadSchedule;
}

namespace arp::renderer {

struct RenderFrameContext {
    rhi::AcquiredFrame frame;
    rhi::RenderTargetDesc colorTarget;
    const rhi::DepthTargetDesc* depthTarget = nullptr;
    rhi::GpuTexture* colorTexture = nullptr;
    rhi::GpuTexture* depthTexture = nullptr;
    rhi::GpuTexture* shadowTexture = nullptr;
};

struct GuiAtlasTextureDesc {
    std::pmr::string resourceId;
    rhi::Extent2D extent;
    arp::core::pmr::Vector<std::uint8_t> rgba8{&arp::core::defaultCpuMemoryResource()};
    bool srgb = true;

    bool valid() const
    {
        const auto expectedBytes = static_cast<std::uint64_t>(extent.width)
            * static_cast<std::uint64_t>(extent.height) * 4u;
        return !resourceId.empty() && extent.width > 0u && extent.height > 0u
            && expectedBytes == rgba8.size();
    }
};

struct GuiRendererDesc {
    std::pmr::string shaderPath;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent;
    GuiAtlasTextureDesc atlasTexture;
    bool hasAtlasTexture = false;
};

struct FxaaRendererDesc {
    std::pmr::string shaderPath;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent;
};

struct TaaRendererDesc {
    std::pmr::string shaderPath;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent;
};

struct SsaoRendererDesc {
    std::pmr::string shaderPath;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent;
};

struct ParticleRendererDesc {
    std::pmr::string shaderPath;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent;
    uint32_t maxParticles = 1024;
    std::pmr::string trailBeamShaderPath;
    std::pmr::string meshParticleShaderPath;
};

struct UiResourceRecoveryTelemetry {
    std::pmr::string reason{&arp::core::defaultCpuMemoryResource()};
    std::uint64_t generation = 0u;
    std::uint64_t retainedBytes = 0u;
    std::uint32_t reconstructedResources = 0u;
    std::uint32_t droppedWork = 0u;
    std::uint64_t latencyMicroseconds = 0u;
    std::pmr::string diagnostics{&arp::core::defaultCpuMemoryResource()};
};

/// Per-frame evidence for the production UI graph section. This is renderer
/// telemetry, not a backend capability claim: it records whether the caller
/// requested the pass, how many lowered commands were consumed, and whether
/// the atlas-backed draw recording completed.
struct UiPassTelemetry {
    bool requested = false;
    bool recorded = false;
    bool atlasReady = false;
    std::uint32_t commandCount = 0u;
    std::pmr::string diagnostics{&arp::core::defaultCpuMemoryResource()};
};

class UiRenderer {
public:
    virtual ~UiRenderer() = default;

    virtual RendererResult create(const GuiRendererDesc& desc) = 0;
    virtual RendererResult drawFrame(
        const RenderFrameContext& frame, const gui::DebugDrawData& DebugDrawData) = 0;
    virtual RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        const gui::DebugDrawData& DebugDrawData) = 0;
    virtual RendererResult recordPass(render_graph::RenderContext& context,
        const RenderFrameContext& frame,
        const arp::core::pmr::Vector<ui::UiDrawCommand>& commands) = 0;
    virtual RendererResult updateAtlasTexture(const GuiAtlasTextureDesc& atlas,
        const arp::core::pmr::Vector<arp::renderer::ui::GlyphAtlasUploadSchedule>& uploads) = 0;
    virtual const rhi::RenderPipeline* graphicsPipeline() const = 0;

    virtual UiResourceRecoveryTelemetry recoveryTelemetry() const { return {}; }

    virtual void beginFrameTelemetry() {}

    virtual UiPassTelemetry passTelemetry() const { return {}; }

    virtual RendererResult onResize(rhi::Extent2D extent)
    {
        (void)extent;
        return RendererResult{false, "UI renderer onResize is not implemented"};
    }

    virtual RendererResult onDeviceLost()
    {
        return RendererResult{false, "UI renderer onDeviceLost is not implemented"};
    }

    virtual RendererResult recoverFromDeviceLost(const GuiRendererDesc& desc)
    {
        (void)desc;
        return RendererResult{false, "UI renderer recoverFromDeviceLost is not implemented"};
    }
};

} // namespace arp::renderer
