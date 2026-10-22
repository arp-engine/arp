#pragma once

#include "editor/viewport/editor_scene_viewport_camera.h"
#include "editor/viewport/editor_scene_viewport_draw_executor.h"
#include "editor/viewport/editor_scene_viewport_shader_package.h"

namespace arp::editor {

// EditorViewportEnvironment/2: world-space rays and explicit clip-depth rows.
// No cross-language matrix type is used; all eleven members are 16-byte vectors.
struct EditorSceneViewportEnvironmentPush {
    core::Vec4 camera;
    core::Vec4 rayOrigin;
    core::Vec4 rayHorizontal;
    core::Vec4 rayVertical;
    core::Vec4 depthNumerator;
    core::Vec4 depthDenominator;
    core::Vec4 grid;
    core::Vec4 zenith;
    core::Vec4 horizon;
    core::Vec4 sunDirection;
    core::Vec4 sunColor;
};

static_assert(sizeof(EditorSceneViewportEnvironmentPush) == 176u);

ARP_API EditorSceneViewportEnvironmentPush buildEditorSceneViewportEnvironmentPush(
    const EditorSceneViewportCamera& camera,
    float aspect,
    bool gridPass,
    const EditorSceneViewportSky& sky = {}) noexcept;

// Uses generated reflection and the existing four-backend push lowering gate.
ARP_API bool validateEditorSceneViewportEnvironmentReflection(
    const shader::ShaderReflection& reflection) noexcept;
ARP_API bool editorSceneViewportEnvironmentExecutable(const rhi::DeviceCapabilities& capabilities,
    const rhi::ShaderReflectionDesc& reflection) noexcept;

// Owns one immutable pipeline. Two fullscreen draws per dirty viewport, no assets.
class EditorSceneViewportEnvironment final {
public:
    bool initialize(rhi::GpuDevice& device,
        const EditorSceneViewportShaderArtifacts& artifacts,
        core::pmr::String& diagnostics);
    EditorSceneViewportDrawExecutionResult record(rhi::GpuDevice& device,
        rhi::GpuTexture& color,
        rhi::GpuTexture& depth,
        const EditorSceneViewportCamera& camera,
        bool gridPass,
        const EditorSceneViewportSky& sky = {});

    bool valid() const noexcept { return resources_.pipeline != nullptr; }

private:
    rhi::GpuDevice* device_ = nullptr;
    EditorSceneViewportDevicePipelineResources resources_;
};

} // namespace arp::editor
