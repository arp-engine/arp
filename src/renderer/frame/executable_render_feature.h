#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/gpu_device.h"

#include <cstdint>

namespace arp::renderer {

enum class RenderFeatureStage : std::uint8_t {
    Surface,
    Shadow,
    ExactDepth,
    LightingUpload,
    LightingCull,
    Opaque,
    Transparent,
    AmbientOcclusion,
    Reflection,
    Temporal,
    PostProcess,
    Ui,
    Present,
};

struct RenderFeatureResult {
    bool success = false;
    core::pmr::String diagnosticId;
    core::pmr::String message;
};

struct RenderFeatureCreateContext {
    rhi::GpuDevice& device;
    rhi::Extent2D extent;
};

struct RenderFeatureGraphContext {
    render_graph::FrameGraph& graph;
    render_graph::TextureHandle backbuffer;
    std::uint32_t frameIndex = 0u;
};

class ARP_API IExecutableRenderFeature {
public:
    virtual ~IExecutableRenderFeature() = default;
    virtual const char* id() const noexcept = 0;
    virtual RenderFeatureStage stage() const noexcept = 0;
    virtual RenderFeatureResult create(const RenderFeatureCreateContext& context) = 0;
    virtual RenderFeatureResult resize(rhi::Extent2D extent) = 0;
    virtual RenderFeatureResult contributeGraph(const RenderFeatureGraphContext& context) = 0;
    virtual void release() noexcept = 0;
};

struct ExecutableRenderFeatureSetDesc {
    std::uint32_t maxFeatureCount = 32u;
};

class ARP_API ExecutableRenderFeatureSet final {
public:
    explicit ExecutableRenderFeatureSet(ExecutableRenderFeatureSetDesc desc = {});

    ~ExecutableRenderFeatureSet() { release(); }

    ExecutableRenderFeatureSet(const ExecutableRenderFeatureSet&) = delete;
    ExecutableRenderFeatureSet& operator=(const ExecutableRenderFeatureSet&) = delete;

    RenderFeatureResult registerFeature(IExecutableRenderFeature& feature);
    RenderFeatureResult create(const RenderFeatureCreateContext& context);
    RenderFeatureResult resize(rhi::Extent2D extent);
    RenderFeatureResult contributeGraph(const RenderFeatureGraphContext& context);
    void release() noexcept;

    bool created() const noexcept { return created_; }

    std::uint32_t size() const noexcept;

private:
    ExecutableRenderFeatureSetDesc desc_;
    core::pmr::Vector<IExecutableRenderFeature*> features_;
    std::uint32_t createdFeatureCount_ = 0u;
    bool created_ = false;
};

} // namespace arp::renderer
