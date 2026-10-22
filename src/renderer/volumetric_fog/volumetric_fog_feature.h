#pragma once

#include "renderer/render_graph/render_graph.h"
#include "renderer/volumetric_fog/fog_contract.h"

namespace arp::renderer::fog {

struct FogCreateContext {
    rhi::GpuDevice& device;
    rhi::Extent2D extent;
    FogConfig config;
    core::pmr::String shaderDirectory;
    std::uint64_t cameraId = 1u;
};

enum class FogShadowCascadeSelection : std::uint32_t { ViewDepth, DistanceFromOrigin };

struct FogShadowInput {
    render_graph::TextureHandle depth;
    std::uint32_t cascadeCount = 0u;
    // Raster VP used by the actual shadow caster; sampling conversion is internal.
    std::array<core::Mat4, 4> matrices{};
    std::array<core::Vec4, 4> uvRects{};
    std::array<float, 4> cascadeEnds{};
    float depthBias = 0.001f;
    FogShadowCascadeSelection selection = FogShadowCascadeSelection::ViewDepth;
    core::Vec3 selectionOrigin{};
};

struct FogGraphContext {
    render_graph::FrameGraph& graph;
    const scene3d::Scene3DRenderQueue& scene;
    render_graph::TextureHandle color;
    render_graph::TextureHandle depth;
    core::Mat4 view;
    // Raster projection used for the input depth (RenderTargetCoordinates/2).
    core::Mat4 projection;
    // Raster m[2]/m[6] minus canonical projection m[2]/m[6]. History compares
    // canonical projections while reconstruction keeps the exact raster camera.
    core::Vec2 projectionJitter{};
    core::Vec3 cameraPosition;
    scene3d::Scene3DLightBinding primaryLight;
    FogShadowInput shadow;
    std::uint64_t cameraId = 1u;
    std::uint64_t sceneRevision = 0u;
    float nearPlane = 0.1f;
    float time = 0.0f;
    bool cameraCut = false;
    // FogIsotropicAmbient/1: mean incident linear radiance; zero disables.
    // Isotropic single scattering, without sky occlusion or multiple scattering.
    core::Vec3 ambientRadiance{};
};

struct FogOutputs {
    render_graph::TextureHandle color;
    render_graph::BufferHandle integrated;
    render_graph::BufferHandle constants;
    bool historyUsed = false;
    std::uint64_t gpuBytes = 0u;
};

// One instance owns one camera's bounded frame/history resources. It must outlive
// graphs using it. A discarded, unsubmitted graph requires cancelPendingFrame().
class ARP_API VolumetricFogFeature final : public render_graph::PassSubmissionObserver {
public:
    ~VolumetricFogFeature() override { release(); }

    FogResult create(const FogCreateContext& context);
    FogResult resize(rhi::Extent2D extent);
    FogResult contributeGraph(const FogGraphContext& context, FogOutputs& outputs);

    void resetHistory() noexcept { historyValid_ = false; }

    void cancelPendingFrame() noexcept;
    void release() noexcept;

    bool created() const noexcept { return device_ != nullptr; }

    // Nonblocking admission for the same slot selection used by contributeGraph.
    bool frameReady() const noexcept;

    const FogGrid& grid() const noexcept { return grid_; }

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

    struct FrameSlot {
        std::shared_ptr<rhi::GpuBuffer> constants, volumes, medium, scattering, history, integrated;
        std::array<core::UniquePtr<rhi::DescriptorSet>, 5> sets;
        std::array<core::UniquePtr<rhi::GpuTextureView>, 4> views;
        std::shared_ptr<const rhi::GpuFence> completion;
        FogConstants parameters{};
        core::pmr::Vector<FogGpuVolume> authoredVolumes;
        core::Mat4 projection;
        core::Mat4 historyProjection;
        std::uint64_t sceneRevision = 0u;
        float time = 0.0f;
    };

    FogResult createPipelines();
    FogResult createSlots();
    bool idle() const noexcept;
    void recordCompute(render_graph::RenderContext& context,
        std::uint32_t pass,
        std::uint32_t slot,
        std::uint32_t z);
    void bindBuffer(rhi::DescriptorSet& set,
        std::uint32_t binding,
        rhi::DescriptorType type,
        const rhi::GpuBuffer& buffer);
    void bindTexture(FrameSlot& slot,
        std::uint32_t viewSlot,
        rhi::DescriptorSet& set,
        std::uint32_t binding,
        rhi::GpuTexture& texture,
        bool depth);

    rhi::GpuDevice* device_ = nullptr;
    FogConfig config_;
    rhi::Extent2D extent_;
    FogGrid grid_;
    core::pmr::String shaderDirectory_;
    std::uint64_t cameraId_ = 0u;
    std::array<Pipeline, 5> pipelines_;
    core::UniquePtr<rhi::DescriptorArena> arena_;
    core::pmr::Vector<FrameSlot> slots_;
    core::pmr::Vector<FogGpuVolume> volumeUpload_;
    std::uint32_t previousSlot_ = 0u, pendingSlot_ = 0u, pendingPrevious_ = 0u;
    std::uint64_t generation_ = 0u;
    bool historyValid_ = false, pending_ = false;
};

} // namespace arp::renderer::fog
