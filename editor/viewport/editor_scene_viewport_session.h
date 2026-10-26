#pragma once

#include "editor/viewport/editor_scene_viewport_composite.h"
#include "editor/viewport/editor_scene_viewport_draw_executor.h"
#include "editor/viewport/editor_scene_viewport_environment.h"
#include "editor/viewport/editor_scene_viewport_material_payload.h"
#include "editor/viewport/editor_scene_viewport_mesh_cache.h"
#include "editor/viewport/editor_scene_viewport_shader_package.h"
#include "editor/viewport/editor_scene_viewport_sprite_renderer.h"
#include "editor/viewport/editor_scene_viewport_texture_cache.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_shell.h"

#include <cstdint>

namespace arp::editor {

struct EditorSceneViewportSessionConfig {
    EditorSceneViewportMeshCacheBudget meshCacheBudget;
    EditorSceneViewportTextureCacheBudget textureCacheBudget;
    std::uint32_t maxSnapshotObjects = 0u;
    std::uint32_t maxDrawCalls = 0u;
    std::uint32_t budgetVersion = 0u;
};

inline constexpr EditorSceneViewportSessionConfig kEditorSceneViewportSmokeSessionConfig{
    {2u, 1024u * 1024u},
    {2u, 1024u, 4096u},
    1024u,
    2u,
    0u,
};

inline constexpr std::uint32_t kEditorSceneViewportInteractiveBudgetVersion = 1u;
inline constexpr std::uint64_t kEditorSceneViewportInteractiveGeometryBudgetBytes =
    256u * 1024u * 1024u;
inline constexpr std::uint64_t kEditorSceneViewportInteractiveTextureBudgetBytes =
    256u * 1024u * 1024u;
inline constexpr std::uint64_t kEditorSceneViewportInteractiveUploadBudgetBytes =
    16u * 1024u * 1024u;
inline constexpr EditorSceneViewportSessionConfig kEditorSceneViewportInteractiveSessionConfig{
    {EditorSceneAuthoringCodec::maxComponents, kEditorSceneViewportInteractiveGeometryBudgetBytes},
    {EditorSceneAuthoringCodec::maxComponents * kEditorSceneViewportMaterialTextureSlotCount,
        kEditorSceneViewportInteractiveTextureBudgetBytes,
        kEditorSceneViewportInteractiveUploadBudgetBytes},
    EditorSceneAuthoringCodec::maxObjects,
    EditorSceneAuthoringCodec::maxComponents,
    kEditorSceneViewportInteractiveBudgetVersion,
};

class EditorSceneViewportSession final {
public:
    explicit EditorSceneViewportSession(EditorSceneViewportSessionConfig config);
    EditorSceneViewportSession(const EditorSceneViewportSession&) = delete;
    EditorSceneViewportSession& operator=(const EditorSceneViewportSession&) = delete;

    bool initialize(EditorShell& shell,
        rhi::GpuDevice& device,
        const EditorSceneViewportShaderArtifacts& artifacts,
        const EditorSceneViewportShaderArtifacts& spriteArtifacts,
        const EditorSceneViewportShaderArtifacts& environmentArtifacts,
        rhi::Extent2D extent,
        core::pmr::String& diagnostics);
    bool renderIfDirty(EditorShell& shell, rhi::Extent2D extent, core::pmr::String& diagnostics);

    bool valid() const noexcept;

    std::size_t lastSceneDrawCount() const noexcept { return lastSceneDrawCount_; }

    rhi::Extent2D extent() const noexcept { return composite_.extent(); }

    renderer::GuiExternalTextureSetBinding guiBinding() noexcept { return composite_.guiBinding(); }

private:
    // Declared before consumers so it dies last, including on early returns.
    // Sprite renderer destruction unregisters from this exact manager.
    core::SharedPtr<resource::ResourceManager> renderedResourceOwner_;

    struct MaterialDescriptorOwner {
        core::UniquePtr<rhi::DescriptorArena> arena;
        core::UniquePtr<rhi::DescriptorSet> set;
    };

    bool render(EditorShell& shell, rhi::Extent2D extent, core::pmr::String& diagnostics);

    EditorSceneViewportSessionConfig config_;
    EditorSceneViewportShaderArtifacts spriteArtifacts_;
    rhi::GpuDevice* device_ = nullptr;
    EditorSceneViewportDevicePipelineResult devicePipeline_;
    shader::ShaderReflection shaderReflection_;
    EditorSceneViewportMeshCache meshCache_;
    EditorSceneViewportTextureCache textureCache_;
    core::UniquePtr<EditorSceneViewportSpriteRenderer> spriteRenderer_;
    EditorSceneViewportComposite composite_;
    EditorSceneViewportEnvironment environment_;
    core::pmr::String renderedSceneDiagnostics_{&core::defaultCpuMemoryResource()};
    bool renderEnvironment(EditorShell& shell,
        bool gridPass,
        const EditorSceneViewportSky& sky,
        core::pmr::String& diagnostics);
    bool finishEnvironment(
        EditorShell& shell, const EditorSceneViewportSky& sky, core::pmr::String& diagnostics);
    core::pmr::Vector<MaterialDescriptorOwner> materialDescriptors_{
        &core::defaultCpuMemoryResource()};
    std::uint64_t renderedStateVersion_ = 0u;
    bool hasRendered_ = false;
    std::size_t lastSceneDrawCount_ = 0;
};

} // namespace arp::editor
