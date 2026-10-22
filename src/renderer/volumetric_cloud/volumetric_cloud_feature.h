#pragma once

#include "assets/cloud/cloud_asset.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/volumetric_cloud/cloud_execution_plan.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"

namespace arp::renderer::cloud {

struct CloudCreateContext {
    rhi::GpuDevice& device;
    rhi::Extent2D extent;
    CloudRenderConfig config;
    const assets::cloud::CloudAsset& asset;
    core::pmr::String shaderDirectory;
    std::uint64_t cameraId = 1u;
};

// Matrices are the raster matrices that produced opaque depth, in world metres.
// Sun direction points from the sample toward the light. Radiances are linear.
struct CloudGraphContext {
    render_graph::FrameGraph& graph;
    render_graph::TextureHandle color, depth;
    scene3d::CloudLayerDesc layer;
    core::Mat4 view, projection;
    core::Vec3 cameraPosition;
    core::Vec3 sunDirection, sunRadiance, ambientRadiance;
    core::Vec2 projectionJitter{};
    std::uint64_t cameraId = 1u, sceneRevision = 0u;
    float time = 0.0f;
    bool orthographic = false, cameraCut = false;
};

struct CloudOutputs {
    render_graph::TextureHandle color;
    render_graph::BufferHandle samples, constants;
    bool historyUsed = false;
    std::uint64_t gpuBytes = 0u;
};

// One feature per identified camera. Discarded graphs must be cancelled before
// reuse. Feature/device must outlive graphs and completion fences using it.
class ARP_API VolumetricCloudFeature final : public render_graph::PassSubmissionObserver {
public:
    ~VolumetricCloudFeature() override { release(); }

    CloudResult create(const CloudCreateContext& context);
    CloudResult resize(rhi::Extent2D extent);
    CloudResult contributeGraph(const CloudGraphContext& context, CloudOutputs& output);

    bool created() const noexcept { return device_ != nullptr; }

    bool frameReady() const noexcept;

    void resetHistory() noexcept
    {
        historyValid_ = false;
        samplingFrame_ = 0u;
    }

    // Only a graph that has never entered execution validation may be cancelled.
    // After execution failure retain the graph, release/drain, then destroy it.
    CloudResult cancelPendingFrame() noexcept;
    void release() noexcept;

    const CloudExecutionPlan& plan() const noexcept { return plan_; }

    const char* validateSubmission(render_graph::PassSubmissionToken token) const noexcept override;
    void onSubmitted(render_graph::PassSubmissionToken token,
        const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override;

private:
    struct Pipeline {
        rhi::ShaderPackageDesc package;
        rhi::DescriptorLayoutDesc bindings;
        core::UniquePtr<rhi::PipelineLayout> layout;
        core::UniquePtr<rhi::ComputePipeline> compute;
        core::UniquePtr<rhi::RenderPipeline> graphics;
    };

    struct Slot {
        std::shared_ptr<rhi::GpuBuffer> constants, raw, resolved;
        std::shared_ptr<rhi::GpuTexture> color;
        std::array<core::UniquePtr<rhi::DescriptorSet>, kCloudPipelineCount> sets;
        std::array<core::UniquePtr<rhi::GpuTextureView>, 2> views;
        std::array<rhi::GpuResourceId, 2> viewedTextures{};
        std::shared_ptr<const rhi::GpuFence> completion;
        CloudGpuConstants parameters{};
        core::Mat4 viewProjection, canonicalProjection;
        core::Vec3 planetCenter;
        std::uint64_t sceneRevision = 0u;
    };

    CloudResult createPipelines();
    CloudResult createSlots();
    bool idle() const noexcept;
    void recordCompute(
        render_graph::RenderContext& context, std::uint32_t pass, std::uint32_t slot);
    void bindTexture(Slot& slot,
        std::uint32_t view,
        rhi::DescriptorSet& set,
        std::uint32_t binding,
        rhi::GpuTexture& texture,
        bool depth);
    rhi::GpuDevice* device_ = nullptr;
    CloudRenderConfig config_;
    CloudExecutionPlan plan_;
    assets::cloud::CloudCookDesc assetDesc_;
    std::uint64_t assetBytes_ = 0u, cameraId_ = 0u, generation_ = 0u;
    core::pmr::String shaderDirectory_;
    std::shared_ptr<rhi::GpuBuffer> asset_;
    std::array<Pipeline, kCloudPipelineCount> pipelines_;
    core::UniquePtr<rhi::DescriptorArena> arena_;
    core::pmr::Vector<Slot> slots_;
    std::uint32_t previousSlot_ = 0u, pendingSlot_ = 0u, pendingPrevious_ = 0u;
    std::uint32_t samplingFrame_ = 0u;
    bool pending_ = false, historyValid_ = false;
    mutable bool executionValidated_ = false;
};

} // namespace arp::renderer::cloud
