#pragma once

#include "core/cpu_allocator.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_result.h"
#include "renderer/visibility/depth_pyramid_contract.h"
#include "rhi/gpu_device.h"

#include <array>
#include <cstdint>
#include <memory>

namespace arp::renderer::visibility {

struct DepthPyramidCreateContext {
    rhi::GpuDevice& device;
    DepthPyramidExtent extent;
    core::pmr::String shaderPath;
};

struct DepthPyramidGraphContext {
    render_graph::FrameGraph& graph;
    render_graph::TextureHandle sourceDepth;
    std::uint32_t frameIndex = 0u;
};

struct DepthPyramidGraphOutputs {
    render_graph::TextureHandle texture;
    std::uint32_t mipLevelCount = 0u;
};

// Builds a max-depth mip chain for non-reversed [0, 1] depth. The resulting
// texture is persistent; graph contribution only borrows it for the frame.
class DepthPyramidRenderer final {
public:
    static constexpr std::uint32_t kFrameSlots = 2u;
    static constexpr std::uint32_t kMaxMipLevels = 32u;
    static constexpr std::uint32_t kWorkgroupSize = 8u;

    DepthPyramidRenderer() = default;

    ~DepthPyramidRenderer() { release(); }

    DepthPyramidRenderer(const DepthPyramidRenderer&) = delete;
    DepthPyramidRenderer& operator=(const DepthPyramidRenderer&) = delete;

    RendererResult create(const DepthPyramidCreateContext& context);
    RendererResult resize(DepthPyramidExtent extent);
    RendererResult contributeGraph(
        const DepthPyramidGraphContext& context, DepthPyramidGraphOutputs& outputs);
    void release() noexcept;

    bool created() const noexcept { return device_ != nullptr; }

    DepthPyramidExtent extent() const noexcept { return extent_; }

    std::uint32_t mipLevelCount() const noexcept { return mipLevelCount_; }

    std::shared_ptr<rhi::GpuTexture> texture() const { return texture_; }

    // Full mip-chain sampled view. Visibility consumers select an explicit
    // mip with Texture.Load; exposing a single engine-owned view keeps that
    // ABI portable across Vulkan, WebGPU, and Metal.
    const rhi::GpuTextureView* sampledView() const noexcept { return sampledTextureView_.get(); }

private:
    RendererResult createExecutionResources(const core::pmr::String& shaderPath);
    RendererResult createPyramidTexture();
    void recordCopyPass(render_graph::RenderContext& context,
        rhi::GpuTexture& sourceDepth,
        std::uint32_t frameSlot);
    void recordReducePass(render_graph::RenderContext& context,
        std::uint32_t sourceMip,
        std::uint32_t destinationMip,
        std::uint32_t frameSlot);

    rhi::GpuDevice* device_ = nullptr;
    DepthPyramidExtent extent_;
    std::uint32_t mipLevelCount_ = 0u;
    rhi::ShaderPackageDesc shaderPackage_;
    core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    core::UniquePtr<rhi::ComputePipeline> copyPipeline_;
    core::UniquePtr<rhi::ComputePipeline> reducePipeline_;
    core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    std::shared_ptr<rhi::GpuTexture> texture_;
    core::UniquePtr<rhi::GpuTextureView> sampledTextureView_;
    // The copy shader shares the reduction layout but does not read
    // PyramidSource. Keep its required sampled binding disjoint from the mip
    // being written so WebGPU never sees read/write aliasing in one pass.
    std::shared_ptr<rhi::GpuTexture> copyDummySampledTexture_;
    core::UniquePtr<rhi::GpuTextureView> copyDummySampledView_;
    std::array<core::UniquePtr<rhi::GpuTextureView>, kMaxMipLevels> sampledViews_;
    std::array<core::UniquePtr<rhi::GpuTextureView>, kMaxMipLevels> storageViews_;
    std::array<core::UniquePtr<rhi::GpuTextureView>, kFrameSlots> sourceDepthViews_;
    std::array<std::array<core::UniquePtr<rhi::DescriptorSet>, kMaxMipLevels>, kFrameSlots>
        descriptorSets_;
};

} // namespace arp::renderer::visibility
