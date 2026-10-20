#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/math.h"
#include "scene/camera.h"
#include "scene/ids.h"

#include <cstdint>

namespace arp::editor {

inline constexpr const char* kEditorSceneViewportContract = "editor/SceneViewportFrame";
inline constexpr std::uint32_t kEditorSceneViewportContractVersion = 2u;

struct EditorSceneViewportRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct EditorSceneViewportDrawItem {
    scene::GameObjectId objectId;
    scene::ComponentId componentId;
    scene::Transform worldTransform;
    core::pmr::String meshResourceId;
    core::pmr::String materialResourceId;
};

struct EditorSceneViewportFrame {
    std::uint32_t contractVersion = kEditorSceneViewportContractVersion;
    scene::SceneId sceneId;
    std::uint64_t stateVersion = 0;
    scene::GameObjectId cameraObjectId;
    scene::Transform cameraWorldTransform;
    core::Mat4 cameraViewMatrix = core::Mat4::identity();
    core::Mat4 cameraProjectionMatrix = core::Mat4::identity();
    scene::CameraAspectPolicyKind cameraAspectPolicy = scene::CameraAspectPolicyKind::Fixed;
    float cameraProjectionAspectRatio = 1.0f;
    std::uint32_t targetWidth = 0;
    std::uint32_t targetHeight = 0;
    EditorSceneViewportRect viewport;
    core::pmr::Vector<EditorSceneViewportDrawItem> drawItems;
};

enum class EditorSceneViewportDiagnostic {
    None,
    UnsupportedContractVersion,
    MissingSceneIdentity,
    MissingStateVersion,
    MissingCamera,
    InvalidSceneSnapshot,
    CameraResolutionFailed,
    InvalidCameraTransform,
    InvalidCameraMatrix,
    InvalidCameraAspect,
    CameraViewportAspectMismatch,
    InvalidTargetExtent,
    InvalidViewportRect,
    UnsupportedTileMapPreview,
    MissingObjectIdentity,
    MissingComponentIdentity,
    InvalidWorldTransform,
    MissingMeshResource,
    MissingMaterialResource,
    MeshResourceNotReady,
    MeshResourceWrongType,
    InvalidMeshData,
    MutableMeshGeometryUnsupported,
    MaterialResourceNotReady,
    MaterialResourceWrongType,
    MaterialSourceMissing,
    MaterialSourceNotReady,
    MaterialSourceWrongType,
    MaterialShaderPackageMissing,
    MaterialShaderPackageNotReady,
    MaterialShaderPackageWrongType,
    ShaderArtifactMetadataMissing,
    MaterialContractUnsupported,
    MaterialContractInvalid,
    MaterialShaderInterfaceMismatch,
    MaterialTextureNotReady,
    MaterialTextureWrongType,
};

struct EditorSceneViewportValidationResult {
    EditorSceneViewportDiagnostic diagnostic = EditorSceneViewportDiagnostic::None;
    std::size_t drawItemIndex = 0;

    bool valid() const { return diagnostic == EditorSceneViewportDiagnostic::None; }
};

enum class EditorSceneViewportBackend {
    Null,
    Vulkan,
    Metal,
    WebGPU,
};

enum class EditorSceneViewportBackendDisposition {
    ContractValidationOnly,
    DiagnosticFailure,
};

ARP_API EditorSceneViewportValidationResult validateEditorSceneViewportFrame(
    const EditorSceneViewportFrame& frame);

ARP_API const char* editorSceneViewportDiagnosticCode(EditorSceneViewportDiagnostic diagnostic);

ARP_API EditorSceneViewportBackendDisposition editorSceneViewportBackendDisposition(
    EditorSceneViewportBackend backend);

ARP_API const char* editorSceneViewportBackendDiagnostic(EditorSceneViewportBackend backend);

} // namespace arp::editor
