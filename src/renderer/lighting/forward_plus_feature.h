#pragma once

#include "renderer/lighting/forward_plus.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/gpu_device.h"

#include <array>

namespace arp::renderer::lighting {

struct ForwardPlusFeatureCreateContext {
    rhi::GpuDevice& device;
    rhi::Extent2D extent;
    ForwardPlusConfig config;
    core::pmr::String shaderPath;
};

struct ForwardPlusFeatureGraphContext {
    render_graph::FrameGraph& graph;
    render_graph::TextureHandle exactOpaqueDepth;
    const RenderLightPlan& lightPlan;
    ForwardPlusCullView view;
    std::uint32_t frameIndex = 0u;
};

struct ForwardPlusFeatureOutputs {
    render_graph::BufferHandle directionalLights;
    render_graph::BufferHandle localLights;
    render_graph::BufferHandle opaqueHeaders;
    render_graph::BufferHandle opaqueIndices;
    render_graph::BufferHandle conservativeHeaders;
    render_graph::BufferHandle conservativeIndices;
    render_graph::BufferHandle overflowTelemetry;
};

struct ForwardPlusFeatureResult {
    bool success = false;
    ForwardPlusDiagnostic diagnostic;
};

enum class ForwardPlusLightList : std::uint8_t {
    OpaqueDepthBounded,
    Conservative,
};

struct ForwardPlusShadingBindings {
    static constexpr std::uint32_t kConstants = 6u;
    static constexpr std::uint32_t kDirectionalLights = 7u;
    static constexpr std::uint32_t kLocalLights = 8u;
    static constexpr std::uint32_t kTileHeaders = 9u;
    static constexpr std::uint32_t kTileIndices = 10u;
};

ARP_API void appendForwardPlusShadingBindings(
    rhi::DescriptorLayoutDesc& layout, rhi::ShaderStage stages = rhi::ShaderStage::Fragment);

class ARP_API ForwardPlusFeature final {
public:
    ForwardPlusFeatureResult create(const ForwardPlusFeatureCreateContext& context);
    ForwardPlusFeatureResult resize(rhi::Extent2D extent);
    ForwardPlusFeatureResult contributeGraph(
        const ForwardPlusFeatureGraphContext& context, ForwardPlusFeatureOutputs& outputs);
    ForwardPlusFeatureResult writeShadingDescriptors(
        rhi::DescriptorSet& descriptorSet, ForwardPlusLightList list, std::uint32_t frameIndex);
    void release() noexcept;

    bool created() const noexcept { return device_ != nullptr; }

    const ForwardPlusConfig& config() const noexcept { return config_; }

    rhi::Extent2D extent() const noexcept { return extent_; }

private:
    static constexpr std::uint32_t kFrameSlots = 2u;

    struct FrameResources {
        // FrameGraph stores buffers in a frame-local vector. Resource IDs
        // remain stable if later graph declarations relocate that vector.
        rhi::GpuResourceId directionalLights;
        rhi::GpuResourceId localLights;
        rhi::GpuResourceId opaqueHeaders;
        rhi::GpuResourceId opaqueIndices;
        rhi::GpuResourceId conservativeHeaders;
        rhi::GpuResourceId conservativeIndices;
        rhi::GpuResourceId depth;
    };

    ForwardPlusFeatureResult createExecutionResources(const core::pmr::String& shaderPath);
    void recordLightUpload(render_graph::RenderContext& context,
        render_graph::FrameGraph& graph,
        const ForwardPlusFeatureOutputs& outputs,
        std::uint32_t frameSlot,
        std::uint64_t directionalBytes,
        std::uint64_t localBytes);
    void dispatchCull(render_graph::RenderContext& context,
        render_graph::FrameGraph& graph,
        render_graph::TextureHandle depth,
        const ForwardPlusFeatureOutputs& outputs,
        const RenderLightPlan& plan,
        const ForwardPlusCullView& view,
        std::uint32_t frameIndex);

    rhi::GpuDevice* device_ = nullptr;
    rhi::Extent2D extent_;
    ForwardPlusConfig config_;
    rhi::ShaderPackageDesc shaderPackage_;
    core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    core::UniquePtr<rhi::ComputePipeline> pipeline_;
    core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    std::array<core::UniquePtr<rhi::DescriptorSet>, kFrameSlots> cullSets_;
    std::array<core::UniquePtr<rhi::GpuBuffer>, kFrameSlots> cullConstantsBuffers_;
    std::array<core::UniquePtr<rhi::GpuBuffer>, kFrameSlots> shadingConstantsBuffers_;
    std::array<core::UniquePtr<rhi::GpuBuffer>, kFrameSlots> directionalUploadBuffers_;
    std::array<core::UniquePtr<rhi::GpuBuffer>, kFrameSlots> localUploadBuffers_;
    std::array<ForwardPlusShadingConstants, kFrameSlots> shadingConstants_{};
    std::array<core::UniquePtr<rhi::GpuTextureView>, kFrameSlots> depthViews_;
    std::array<FrameResources, kFrameSlots> frameResources_;
    core::pmr::Vector<std::uint16_t> conservativeTileCounts_;
};

} // namespace arp::renderer::lighting
