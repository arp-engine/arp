#pragma once

#include "3d/terrain/terrain_surface.h"
#include "core/api.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "scene/camera.h"
#include "scene/ids.h"

#include <cstdint>

namespace arp::editor {

inline constexpr const char* kEditorSceneViewportContract = "editor/SceneViewportFrame";
inline constexpr std::uint32_t kEditorSceneViewportContractVersion = 4u;

struct EditorSceneViewportRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

enum class EditorSceneViewportGeometry { Mesh, TerrainTile };

struct EditorSceneViewportDrawItem {
    scene::GameObjectId objectId;
    scene::ComponentId componentId;
    scene::Transform worldTransform;
    core::pmr::String meshResourceId;
    core::pmr::String materialResourceId;
    EditorSceneViewportGeometry geometry = EditorSceneViewportGeometry::Mesh;
    terrain::TerrainTileCoord tile;
};

struct EditorSceneViewportSky {
    bool present = false;
    core::Vec3 zenith{0.13f, 0.31f, 0.53f};
    core::Vec3 horizon{0.49f, 0.61f, 0.72f};
    core::Vec3 sunDirection{-0.4f, 0.65f, -0.6f};
    core::Vec3 sunColor{0.8f, 0.65f, 0.4f};
    float domeRadius = 560.0f;
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
    EditorSceneViewportSky sky;
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
    InvalidSkyParameters,
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
    TerrainResourceInvalid,
    TerrainSurfacePreviewUnsupported,
    TerrainTransformUnsupported,
    GeometryBudgetExceeded,
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
