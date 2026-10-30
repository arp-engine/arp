#pragma once

#include "core/math/math.h"
#include "editor/viewport/editor_scene_viewport_contract.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_device.h"

#include <array>
#include <cstdint>

namespace arp::editor {

struct EditorSceneViewportDrawItemSubmission {
    const renderer::MeshGpuBuffer* mesh = nullptr;
    const rhi::DescriptorSet* materialSet = nullptr;
    std::uint32_t materialSetIndex = 0;
    core::Mat4 viewProjection = core::Mat4::identity();
    core::Mat4 model = core::Mat4::identity();
    core::Mat4 normalTransform = core::Mat4::identity();
    std::array<float, 4> cameraPosition{0.0f, 0.0f, 1.0f, 1.0f};
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> emissiveMetallic{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> roughnessOcclusionAlphaCutoffMode{1.0f, 1.0f, 0.5f, 0.0f};
};

struct EditorSceneViewportDrawBudget {
    std::uint32_t maxDrawCalls = 0;
};

// Shared 256-byte row-major shader payload for native and ordered executors.
struct EditorSceneViewportPbrPush {
    core::Mat4 viewProjection, model, normalTransform;
    std::array<float, 4> cameraPosition, baseColorFactor, emissiveMetallic;
    std::array<float, 4> roughnessOcclusionAlphaCutoffMode;
};

static_assert(sizeof(EditorSceneViewportPbrPush) == 256);

inline EditorSceneViewportPbrPush buildEditorSceneViewportPbrPush(
    const EditorSceneViewportDrawItemSubmission& item)
{
    return {item.viewProjection,
        item.model,
        item.normalTransform,
        item.cameraPosition,
        item.baseColorFactor,
        item.emissiveMetallic,
        item.roughnessOcclusionAlphaCutoffMode};
}

struct EditorSceneViewportDrawExecutionResult {
    core::UniquePtr<rhi::CommandBuffer> commandBuffer;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    std::uint32_t drawCount = 0;

    bool success() const noexcept { return commandBuffer != nullptr && diagnostics.empty(); }
};

ARP_API EditorSceneViewportDrawExecutionResult recordEditorSceneViewportPass(rhi::GpuDevice& device,
    rhi::RenderPipeline& pipeline,
    rhi::GpuTexture& colorTarget,
    rhi::GpuTexture& depthTarget,
    const EditorSceneViewportRect& viewport,
    const core::pmr::Vector<EditorSceneViewportDrawItemSubmission>& items,
    EditorSceneViewportDrawBudget budget,
    bool preserveAttachments = false);

} // namespace arp::editor
