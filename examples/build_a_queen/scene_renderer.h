#pragma once

#include "assets/asset_pipeline.h"
#include "assets/glb_animated_mesh_importer.h"
#include "core/memory/cpu_allocator.h"
#include "game.h"
#include "renderer/renderer_common.h"
#include "scene_content.h"

#include <array>
#include <cstdint>

namespace arp::build_a_queen {

class HudRenderer;

class SceneRenderer final : public SceneContent {
public:
    static constexpr std::uint32_t kFrameSlots = 3;

    explicit SceneRenderer(rhi::GpuDevice& device);
    ~SceneRenderer();

    renderer::RendererResult create(const core::pmr::String& shaderPath,
        const core::pmr::String& characterDirectory,
        rhi::TextureFormat colorFormat,
        rhi::Extent2D extent);
    renderer::RendererResult resize(rhi::Extent2D extent);
    renderer::RendererResult draw(rhi::GpuTexture& target,
        const rhi::AcquiredFrame& acquiredFrame,
        const Snapshot& state,
        const LevelDesc& level,
        const Pickup* pickups,
        const bool* resolvedPickups,
        std::uint32_t pickupCount,
        HudRenderer& hud,
        std::uint64_t frameIndex);

    bool readyForFrame(std::uint64_t frameIndex) const;

    std::uint32_t lastVertexCount() const { return lastVertexCount_; }

private:
    struct FrameSlot {
        FrameSlot();

        core::UniquePtr<rhi::GpuBuffer> vertices;
        core::UniquePtr<rhi::GpuBuffer> playerAppearanceBuffer;
        core::pmr::Vector<Vertex> playerAppearanceVertices;
        core::UniquePtr<rhi::GpuBuffer> targetAppearanceBuffer;
        core::pmr::Vector<Vertex> targetAppearanceVertices;
        std::array<core::UniquePtr<rhi::GpuBuffer>, 3> skinningPalettes;
        std::array<core::UniquePtr<rhi::DescriptorSet>, 3> skinningSets;
        core::UniquePtr<rhi::GpuTimelineSemaphore> completion;
        core::UniquePtr<rhi::CommandBuffer> commands;
        std::uint64_t submittedValue = 0;
        bool pending = false;
    };

    bool drawAnimatedCharacter(rhi::RenderPassEncoder& pass,
        AnimatedCharacter& character,
        std::size_t characterIndex,
        std::size_t paletteIndex,
        float x,
        float z,
        float scaleMultiplier,
        float yawRadians,
        const char* clip,
        float timeSeconds,
        const core::Mat4& viewProjection,
        const core::Mat4& lightViewProjection,
        bool shadowPass,
        FrameSlot& slot,
        rhi::GpuBuffer* appearanceBuffer = nullptr);
    rhi::GpuDevice& device_;
    rhi::TextureFormat colorFormat_ = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent_{};
    rhi::ShaderPackageDesc shaderPackage_;
    rhi::ShaderPackageDesc skinnedShaderPackage_;
    rhi::ShaderPackageDesc skyShaderPackage_;
    rhi::ShaderPackageDesc shadowShaderPackage_;
    rhi::ShaderPackageDesc skinnedShadowShaderPackage_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    core::UniquePtr<rhi::PipelineLayout> skinnedLayout_;
    core::UniquePtr<rhi::PipelineLayout> skyLayout_;
    core::UniquePtr<rhi::PipelineLayout> shadowLayout_;
    core::UniquePtr<rhi::PipelineLayout> skinnedShadowLayout_;
    core::UniquePtr<rhi::RenderPipeline> pipeline_;
    core::UniquePtr<rhi::RenderPipeline> skinnedPipeline_;
    core::UniquePtr<rhi::RenderPipeline> skyPipeline_;
    core::UniquePtr<rhi::RenderPipeline> shadowPipeline_;
    core::UniquePtr<rhi::RenderPipeline> skinnedShadowPipeline_;
    core::UniquePtr<rhi::DescriptorArena> shadowDescriptorArena_;
    core::UniquePtr<rhi::DescriptorSet> shadowDescriptorSet_;
    core::UniquePtr<rhi::GpuTextureView> shadowView_;
    core::UniquePtr<rhi::GpuSampler> shadowSampler_;
    core::UniquePtr<rhi::GpuTexture> shadowDepth_;
    core::Mat4 lightViewProjection_{};
    core::UniquePtr<rhi::DescriptorArena> skinningDescriptorArena_;
    core::UniquePtr<rhi::GpuTexture> depth_;
    core::pmr::Vector<FrameSlot> slots_{&core::defaultCpuMemoryResource()};
    std::uint32_t lastVertexCount_ = 0;
};

} // namespace arp::build_a_queen
