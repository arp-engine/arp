#pragma once

#include "assets/cloud/cloud_asset.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/volumetric_cloud/cloud_execution_plan.h"
#include "renderer/volumetric_cloud/cloud_frame_parameters.h"
#include "renderer/volumetric_cloud/cloud_gpu_contract.h"
#include "renderer/volumetric_cloud/detail/cloud_prepared_submission.h"

namespace arp::render_graph {
class OriginalFrameGraphExecution;
}

namespace arp::resource {
class ResourceManager;
}

namespace arp::scene3d {
class Scene3DRenderQueue;
}

namespace arp::renderer::cloud {

class CloudPreparedHistory;
class CloudPreparedResources;
class CloudPreparedPipelines;
class CloudSceneBinding;
struct CloudPreparedFrameBuffers;
struct CloudPreparedResourceCreateContext;
struct CloudPreparedFeatureCreateContext;
struct CloudPreparedFeatureGraphContext;
struct CloudReconstructionResources;

// Explicit finite Prototype frame. Resources, artifacts and frozen descriptors
// belong to the original inventory retained by execution. History selection is
// explicit; this API neither rewrites nor unseals a previous frame's buffers.
struct CloudPreparedGraphContext {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    render_graph::OriginalFrameGraphExecution& execution;
    CloudExecutionPlan plan;
    render_graph::TextureHandle color, depth, output;
    render_graph::BufferHandle constants, density, raw, previous, resolved;
    std::array<std::size_t, kCloudPipelineCount> pipelines{}, descriptors{};
    // Version 2 binds the immutable uniform and sample owners selected by this
    // history controller and attaches its ordinary submission observer.
    CloudPreparedHistory* history = nullptr;
    std::uint64_t historyGeneration = 0u;
    // /3 explicitly selects Reconstruction/1, including a distinct full-size
    // resolved grid and an immutable policy uniform owned by history.
    const CloudReconstructionResources* reconstruction = nullptr;
    render_graph::BufferHandle reconstructionConstants;
};

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
    VolumetricCloudFeature();
    ~VolumetricCloudFeature() override;

    CloudResult create(const CloudCreateContext& context);
    // Explicit original path: all budgets, publications and memory intents
    // come from the caller. No conversion from legacy creation is inferred.
    CloudResult create(const CloudPreparedFeatureCreateContext&, core::pmr::String& diagnostic);
    CloudResult createScene(const std::shared_ptr<const CloudSceneBinding>&,
        const scene3d::Scene3DRenderQueue&,
        const resource::ResourceManager&,
        const CloudPreparedFeatureCreateContext&,
        core::pmr::String& diagnostic);
    CloudResult resize(rhi::Extent2D extent);
    CloudResult resize(const CloudPreparedResourceCreateContext&);
    CloudResult contributeGraph(const CloudGraphContext& context, CloudOutputs& output);
    CloudResult contributeGraph(const CloudPreparedFeatureGraphContext&, CloudOutputs&);
    // Cold preflight may intern existing buffer owners in the same graph, but
    // adds no pass and does not advance the pending generation.
    CloudResult validateGraph(const CloudPreparedFeatureGraphContext&);
    // Cold preparation precedes assembly of the caller's single inventory.
    CloudResult prepareFrame(const CloudGraphContext&, CloudPreparedFrameBuffers&);
    // Cold registry validation precedes the same ordinary frame preparation.
    CloudResult prepareSceneFrame(const scene3d::Scene3DRenderQueue&,
        const resource::ResourceManager&,
        CloudGraphContext,
        CloudPreparedFrameBuffers&);
    const CloudPreparedResources* preparedResources() const noexcept;
    const CloudPreparedPipelines* preparedPipelines() const noexcept;
    const CloudSceneBinding* preparedSceneBinding() const noexcept;
    CloudPreparedHistory* preparedHistory() noexcept;
    render_graph::PassSubmissionToken preparedToken() const noexcept;
    static CloudResult contributePreparedGraph(
        const CloudPreparedGraphContext& context, CloudOutputs& output);

    bool created() const noexcept { return device_ != nullptr; }

    bool frameReady() const noexcept;

    CloudResult resetHistory() noexcept;

    // Only a graph that has never entered execution validation may be cancelled.
    // After execution failure retain the graph, release/drain, then destroy it.
    CloudResult cancelPendingFrame() noexcept;
    void release() noexcept;

    const CloudExecutionPlan& plan() const noexcept { return plan_; }

    const char* validateSubmission(render_graph::PassSubmissionToken token) const noexcept override;
    void onSubmitted(render_graph::PassSubmissionToken token,
        const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override;

private:
    CloudResult prepareFrameInternal(const CloudGraphContext&, CloudPreparedFrameBuffers&);
    CloudResult contributeGraphInternal(
        const CloudPreparedFeatureGraphContext&, CloudOutputs&, bool validateOnly);
    struct PreparedState;
    detail::CloudPreparedSubmission preparedSubmission_;
    core::UniquePtr<PreparedState> prepared_;
    static CloudResult contributePreparedGraph(const CloudPreparedGraphContext&,
        CloudOutputs&,
        render_graph::PassSubmissionObserver*,
        render_graph::PassSubmissionToken,
        bool validateOnly = false);

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
        CloudFrameState frame;
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
