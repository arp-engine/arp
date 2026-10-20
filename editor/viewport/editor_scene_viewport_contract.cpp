#include "editor/viewport/editor_scene_viewport_contract.h"

#include <cmath>

namespace arp::editor {
namespace {

bool finiteTransform(const scene::Transform& transform)
{
    const auto finite = [](float value) {
        return std::isfinite(value);
    };
    const auto& t = transform.translation;
    const auto& s = transform.scale;
    const auto& r = transform.rotation;
    return finite(t.x) && finite(t.y) && finite(t.z) && finite(s.x) && finite(s.y) && finite(s.z)
        && finite(r.x) && finite(r.y) && finite(r.z) && finite(r.w)
        && (r.x != 0.0f || r.y != 0.0f || r.z != 0.0f || r.w != 0.0f);
}

bool finiteMatrix(const core::Mat4& matrix)
{
    for (const auto value : matrix.m) {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

} // namespace

EditorSceneViewportValidationResult validateEditorSceneViewportFrame(
    const EditorSceneViewportFrame& frame)
{
    if (frame.contractVersion != kEditorSceneViewportContractVersion)
        return {EditorSceneViewportDiagnostic::UnsupportedContractVersion, 0};
    if (!frame.sceneId.valid())
        return {EditorSceneViewportDiagnostic::MissingSceneIdentity, 0};
    if (frame.stateVersion == 0)
        return {EditorSceneViewportDiagnostic::MissingStateVersion, 0};
    if (!frame.cameraObjectId.valid())
        return {EditorSceneViewportDiagnostic::MissingCamera, 0};
    if (!finiteTransform(frame.cameraWorldTransform))
        return {EditorSceneViewportDiagnostic::InvalidCameraTransform, 0};
    if (!finiteMatrix(frame.cameraViewMatrix) || !finiteMatrix(frame.cameraProjectionMatrix))
        return {EditorSceneViewportDiagnostic::InvalidCameraMatrix, 0};
    if (!std::isfinite(frame.cameraProjectionAspectRatio)
        || frame.cameraProjectionAspectRatio <= 0.0f)
        return {EditorSceneViewportDiagnostic::InvalidCameraAspect, 0};
    if (frame.cameraAspectPolicy != scene::CameraAspectPolicyKind::Fixed
        && frame.cameraAspectPolicy != scene::CameraAspectPolicyKind::MatchViewport)
        return {EditorSceneViewportDiagnostic::InvalidCameraAspect, 0};
    if (frame.targetWidth == 0 || frame.targetHeight == 0)
        return {EditorSceneViewportDiagnostic::InvalidTargetExtent, 0};

    const auto& rect = frame.viewport;
    if (rect.width == 0 || rect.height == 0 || rect.x > frame.targetWidth
        || rect.y > frame.targetHeight || rect.width > frame.targetWidth - rect.x
        || rect.height > frame.targetHeight - rect.y) {
        return {EditorSceneViewportDiagnostic::InvalidViewportRect, 0};
    }
    if (frame.cameraAspectPolicy == scene::CameraAspectPolicyKind::MatchViewport
        && frame.cameraProjectionAspectRatio
            != static_cast<float>(rect.width) / static_cast<float>(rect.height)) {
        return {EditorSceneViewportDiagnostic::CameraViewportAspectMismatch, 0};
    }

    for (std::size_t index = 0; index < frame.drawItems.size(); ++index) {
        const auto& item = frame.drawItems[index];
        if (!item.objectId.valid())
            return {EditorSceneViewportDiagnostic::MissingObjectIdentity, index};
        if (!item.componentId.valid())
            return {EditorSceneViewportDiagnostic::MissingComponentIdentity, index};
        if (!finiteTransform(item.worldTransform))
            return {EditorSceneViewportDiagnostic::InvalidWorldTransform, index};
        if (item.meshResourceId.empty())
            return {EditorSceneViewportDiagnostic::MissingMeshResource, index};
        if (item.materialResourceId.empty())
            return {EditorSceneViewportDiagnostic::MissingMaterialResource, index};
    }

    return {};
}

const char* editorSceneViewportDiagnosticCode(EditorSceneViewportDiagnostic diagnostic)
{
    switch (diagnostic) {
    case EditorSceneViewportDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-OK";
    case EditorSceneViewportDiagnostic::UnsupportedContractVersion:
        return "ARP-EDITOR-VIEWPORT-CONTRACT-VERSION-UNSUPPORTED";
    case EditorSceneViewportDiagnostic::MissingSceneIdentity:
        return "ARP-EDITOR-VIEWPORT-SCENE-ID-MISSING";
    case EditorSceneViewportDiagnostic::MissingStateVersion:
        return "ARP-EDITOR-VIEWPORT-STATE-VERSION-MISSING";
    case EditorSceneViewportDiagnostic::MissingCamera:
        return "ARP-EDITOR-VIEWPORT-CAMERA-MISSING";
    case EditorSceneViewportDiagnostic::InvalidSceneSnapshot:
        return "ARP-EDITOR-VIEWPORT-SCENE-SNAPSHOT-INVALID";
    case EditorSceneViewportDiagnostic::CameraResolutionFailed:
        return "ARP-EDITOR-VIEWPORT-CAMERA-RESOLUTION-FAILED";
    case EditorSceneViewportDiagnostic::InvalidCameraTransform:
        return "ARP-EDITOR-VIEWPORT-CAMERA-TRANSFORM-INVALID";
    case EditorSceneViewportDiagnostic::InvalidCameraMatrix:
        return "ARP-EDITOR-VIEWPORT-CAMERA-MATRIX-INVALID";
    case EditorSceneViewportDiagnostic::InvalidCameraAspect:
        return "ARP-EDITOR-VIEWPORT-CAMERA-ASPECT-INVALID";
    case EditorSceneViewportDiagnostic::CameraViewportAspectMismatch:
        return "ARP-EDITOR-VIEWPORT-CAMERA-ASPECT-VIEWPORT-MISMATCH";
    case EditorSceneViewportDiagnostic::InvalidTargetExtent:
        return "ARP-EDITOR-VIEWPORT-TARGET-EXTENT-INVALID";
    case EditorSceneViewportDiagnostic::InvalidViewportRect:
        return "ARP-EDITOR-VIEWPORT-RECT-INVALID";
    case EditorSceneViewportDiagnostic::UnsupportedTileMapPreview:
        return "ARP-EDITOR-VIEWPORT-TILEMAP-PREVIEW-UNSUPPORTED";
    case EditorSceneViewportDiagnostic::MissingObjectIdentity:
        return "ARP-EDITOR-VIEWPORT-OBJECT-ID-MISSING";
    case EditorSceneViewportDiagnostic::MissingComponentIdentity:
        return "ARP-EDITOR-VIEWPORT-COMPONENT-ID-MISSING";
    case EditorSceneViewportDiagnostic::InvalidWorldTransform:
        return "ARP-EDITOR-VIEWPORT-WORLD-TRANSFORM-INVALID";
    case EditorSceneViewportDiagnostic::MissingMeshResource:
        return "ARP-EDITOR-VIEWPORT-MESH-RESOURCE-MISSING";
    case EditorSceneViewportDiagnostic::MissingMaterialResource:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-RESOURCE-MISSING";
    case EditorSceneViewportDiagnostic::MeshResourceNotReady:
        return "ARP-EDITOR-VIEWPORT-MESH-RESOURCE-NOT-READY";
    case EditorSceneViewportDiagnostic::MeshResourceWrongType:
        return "ARP-EDITOR-VIEWPORT-MESH-RESOURCE-WRONG-TYPE";
    case EditorSceneViewportDiagnostic::InvalidMeshData:
        return "ARP-EDITOR-VIEWPORT-MESH-DATA-INVALID";
    case EditorSceneViewportDiagnostic::MutableMeshGeometryUnsupported:
        return "ARP-EDITOR-VIEWPORT-MESH-GEOMETRY-MUTABLE-UNSUPPORTED";
    case EditorSceneViewportDiagnostic::MaterialResourceNotReady:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-RESOURCE-NOT-READY";
    case EditorSceneViewportDiagnostic::MaterialResourceWrongType:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-RESOURCE-WRONG-TYPE";
    case EditorSceneViewportDiagnostic::MaterialSourceMissing:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-SOURCE-MISSING";
    case EditorSceneViewportDiagnostic::MaterialSourceNotReady:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-SOURCE-NOT-READY";
    case EditorSceneViewportDiagnostic::MaterialSourceWrongType:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-SOURCE-WRONG-TYPE";
    case EditorSceneViewportDiagnostic::MaterialShaderPackageMissing:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-SHADER-PACKAGE-MISSING";
    case EditorSceneViewportDiagnostic::MaterialShaderPackageNotReady:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-SHADER-PACKAGE-NOT-READY";
    case EditorSceneViewportDiagnostic::MaterialShaderPackageWrongType:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-SHADER-PACKAGE-WRONG-TYPE";
    case EditorSceneViewportDiagnostic::ShaderArtifactMetadataMissing:
        return "ARP-EDITOR-VIEWPORT-SHADER-ARTIFACT-METADATA-MISSING";
    case EditorSceneViewportDiagnostic::MaterialContractUnsupported:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-CONTRACT-UNSUPPORTED";
    case EditorSceneViewportDiagnostic::MaterialContractInvalid:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-CONTRACT-INVALID";
    case EditorSceneViewportDiagnostic::MaterialShaderInterfaceMismatch:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-SHADER-INTERFACE-MISMATCH";
    case EditorSceneViewportDiagnostic::MaterialTextureNotReady:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-TEXTURE-NOT-READY";
    case EditorSceneViewportDiagnostic::MaterialTextureWrongType:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-TEXTURE-WRONG-TYPE";
    }
    return "ARP-EDITOR-VIEWPORT-DIAGNOSTIC-UNKNOWN";
}

EditorSceneViewportBackendDisposition editorSceneViewportBackendDisposition(
    EditorSceneViewportBackend backend)
{
    switch (backend) {
    case EditorSceneViewportBackend::Null:
        return EditorSceneViewportBackendDisposition::ContractValidationOnly;
    case EditorSceneViewportBackend::Vulkan:
    case EditorSceneViewportBackend::Metal:
    case EditorSceneViewportBackend::WebGPU:
        return EditorSceneViewportBackendDisposition::DiagnosticFailure;
    }
    return EditorSceneViewportBackendDisposition::DiagnosticFailure;
}

const char* editorSceneViewportBackendDiagnostic(EditorSceneViewportBackend backend)
{
    switch (backend) {
    case EditorSceneViewportBackend::Null:
        return "ARP-EDITOR-VIEWPORT-NULL-VALIDATOR-ONLY";
    case EditorSceneViewportBackend::Vulkan:
    case EditorSceneViewportBackend::Metal:
    case EditorSceneViewportBackend::WebGPU:
        return "ARP-EDITOR-VIEWPORT-EXECUTOR-NOT-IMPLEMENTED";
    }
    return "ARP-EDITOR-VIEWPORT-BACKEND-UNKNOWN";
}

} // namespace arp::editor
