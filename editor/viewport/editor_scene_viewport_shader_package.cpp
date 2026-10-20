#include "editor/viewport/editor_scene_viewport_shader_package.h"

#include "renderer/shader_package_reflection.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

namespace arp::editor {
namespace {

bool hasSuffix(const std::string_view path, const std::string_view suffix) noexcept
{
    return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
}

bool hasExactGraphicsEntryPoints(const shader::ShaderReflection& reflection)
{
    bool vertex = false;
    bool fragment = false;
    for (const auto& entry : reflection.entryPoints) {
        if (entry.name == "vertexMain" && entry.stage == shader::ShaderStage::Vertex)
            vertex = true;
        else if (entry.name == "fragmentMain" && entry.stage == shader::ShaderStage::Fragment)
            fragment = true;
        else
            return false;
    }
    return vertex && fragment && reflection.entryPoints.size() == 2u;
}

} // namespace

EditorSceneViewportShaderPackageResult loadEditorSceneViewportShaderPackage(
    const EditorSceneViewportShaderArtifacts& artifacts,
    const rhi::TextureFormat colorFormat,
    const rhi::TextureFormat depthFormat,
    const std::uint32_t sampleCount)
{
    EditorSceneViewportShaderPackageResult result;
    if (artifacts.sourcePath.empty() || artifacts.spirvPath.empty() || artifacts.mslPath.empty()
        || artifacts.wgslPath.empty() || artifacts.reflectionPath.empty()) {
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::ArtifactPathMissing;
        return result;
    }
    if (!hasSuffix(artifacts.sourcePath, ".slang") || !hasSuffix(artifacts.spirvPath, ".spv")
        || !hasSuffix(artifacts.mslPath, ".metal") || !hasSuffix(artifacts.wgslPath, ".wgsl")
        || !hasSuffix(artifacts.reflectionPath, ".reflection.yaml")) {
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::ArtifactPathInvalid;
        return result;
    }

    const auto loaded =
        shader::ShaderReflectionCache::loadFromFile(core::pmr::String(artifacts.reflectionPath));
    if (!loaded.success) {
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::ReflectionLoadFailed;
        return result;
    }
    const auto identity =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(loaded.module,
            core::pmr::String(artifacts.sourcePath),
            core::pmr::String(artifacts.spirvPath),
            core::pmr::String(artifacts.mslPath),
            core::pmr::String(artifacts.wgslPath));
    if (!identity.success) {
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::ArtifactIdentityInvalid;
        return result;
    }
    if (!hasExactGraphicsEntryPoints(loaded.module.reflection)) {
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::EntryPointMismatch;
        return result;
    }
    result.shaderReflection = loaded.module.reflection;

    result.pipelineContract = buildEditorSceneViewportPipelineContract(
        loaded.module.reflection, artifacts.sourcePath, colorFormat, depthFormat, sampleCount);
    if (!result.pipelineContract.valid()) {
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::PipelineContractInvalid;
        return result;
    }

    auto& package = result.package;
    package.label = "ARP Editor Scene Viewport PBR";
    package.slangModulePath = artifacts.sourcePath;
    package.artifacts.spirvPath = artifacts.spirvPath;
    package.artifacts.mslPath = artifacts.mslPath;
    package.artifacts.wgslPath = artifacts.wgslPath;
    package.artifacts.reflectionPath = artifacts.reflectionPath;
    package.reflection = result.pipelineContract.shaderReflection;
    package.entrypoints.push_back(
        {package.slangModulePath, "vertexMain", rhi::ShaderStage::Vertex});
    package.entrypoints.push_back(
        {package.slangModulePath, "fragmentMain", rhi::ShaderStage::Fragment});
    if (!renderer::hydrateShaderPackageReflectionFromArtifact(package)) {
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::PackageHydrationFailed;
        return result;
    }
    if (!rhi::validateShaderPackage(package).success) {
        package.generatedReflectionHydrated = false;
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::PackageInvalid;
        return result;
    }
    if (package.reflection.sourceSha256 != loaded.module.reflection.sourceSha256
        || package.reflection.reflectionContractVersion
            != loaded.module.reflection.reflectionContractVersion) {
        package.generatedReflectionHydrated = false;
        result.diagnostic = EditorSceneViewportShaderPackageDiagnostic::PackageHydrationFailed;
    }
    return result;
}

EditorSceneViewportDevicePipelineResult createEditorSceneViewportDevicePipeline(
    rhi::GpuDevice& device, const EditorSceneViewportShaderPackageResult& package)
{
    EditorSceneViewportDevicePipelineResult result;
    if (!package.valid()) {
        result.diagnostic = EditorSceneViewportDevicePipelineDiagnostic::ShaderPackageInvalid;
        return result;
    }
    const auto capabilities = device.capabilities();
    if (device.backend() != capabilities.backend) {
        result.diagnostic = EditorSceneViewportDevicePipelineDiagnostic::BackendLoweringRejected;
        result.lowering.disposition =
            EditorSceneViewportPipelineLoweringDisposition::DiagnosticFailure;
        result.lowering.diagnostic = EditorSceneViewportPipelineLoweringDiagnostic::OutcomeMismatch;
        result.backendDiagnostics = "device backend and capability report disagree";
        return result;
    }
    result.lowering =
        evaluateEditorSceneViewportPipelineLowering(package.package.reflection, capabilities);
    if (!result.lowering.executable()) {
        result.diagnostic = EditorSceneViewportDevicePipelineDiagnostic::BackendLoweringRejected;
        result.backendDiagnostics =
            editorSceneViewportPipelineLoweringDiagnosticCode(result.lowering.diagnostic);
        return result;
    }

    result.resources.package = package.package;
    result.resources.layout = device.createPipelineLayout(package.pipelineContract.pipelineLayout);
    if (!result.resources.layout) {
        result.diagnostic = EditorSceneViewportDevicePipelineDiagnostic::PipelineLayoutCreateFailed;
        result.backendDiagnostics = "RHI pipeline layout creation returned no resource";
        return result;
    }

    auto pipelineDesc = package.pipelineContract.pipeline;
    pipelineDesc.layout = result.resources.layout.get();
    pipelineDesc.shaderPackage = &result.resources.package;
    const auto validation = device.validateRenderPipeline(pipelineDesc);
    if (!validation.success) {
        result.diagnostic = EditorSceneViewportDevicePipelineDiagnostic::PipelineValidationFailed;
        result.backendDiagnostics = validation.diagnostics;
        return result;
    }
    result.resources.pipeline = device.createRenderPipeline(pipelineDesc);
    if (!result.resources.pipeline) {
        result.diagnostic = EditorSceneViewportDevicePipelineDiagnostic::RenderPipelineCreateFailed;
        result.backendDiagnostics = "RHI render pipeline creation returned no resource";
    }
    return result;
}

const char* editorSceneViewportShaderPackageDiagnosticCode(
    const EditorSceneViewportShaderPackageDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportShaderPackageDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-OK";
    case EditorSceneViewportShaderPackageDiagnostic::ArtifactPathMissing:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-PATH-MISSING";
    case EditorSceneViewportShaderPackageDiagnostic::ArtifactPathInvalid:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-PATH-INVALID";
    case EditorSceneViewportShaderPackageDiagnostic::ReflectionLoadFailed:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-REFLECTION-LOAD-FAILED";
    case EditorSceneViewportShaderPackageDiagnostic::ArtifactIdentityInvalid:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-IDENTITY-INVALID";
    case EditorSceneViewportShaderPackageDiagnostic::EntryPointMismatch:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-ENTRYPOINT-MISMATCH";
    case EditorSceneViewportShaderPackageDiagnostic::PipelineContractInvalid:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-PIPELINE-INVALID";
    case EditorSceneViewportShaderPackageDiagnostic::PackageHydrationFailed:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-HYDRATION-FAILED";
    case EditorSceneViewportShaderPackageDiagnostic::PackageInvalid:
        return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-RHI-INVALID";
    }
    return "ARP-EDITOR-VIEWPORT-SHADER-PACKAGE-DIAGNOSTIC-UNKNOWN";
}

const char* editorSceneViewportDevicePipelineDiagnosticCode(
    const EditorSceneViewportDevicePipelineDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportDevicePipelineDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-DEVICE-PIPELINE-OK";
    case EditorSceneViewportDevicePipelineDiagnostic::ShaderPackageInvalid:
        return "ARP-EDITOR-VIEWPORT-DEVICE-PIPELINE-PACKAGE-INVALID";
    case EditorSceneViewportDevicePipelineDiagnostic::BackendLoweringRejected:
        return "ARP-EDITOR-VIEWPORT-DEVICE-PIPELINE-LOWERING-REJECTED";
    case EditorSceneViewportDevicePipelineDiagnostic::PipelineLayoutCreateFailed:
        return "ARP-EDITOR-VIEWPORT-DEVICE-PIPELINE-LAYOUT-FAILED";
    case EditorSceneViewportDevicePipelineDiagnostic::PipelineValidationFailed:
        return "ARP-EDITOR-VIEWPORT-DEVICE-PIPELINE-VALIDATION-FAILED";
    case EditorSceneViewportDevicePipelineDiagnostic::RenderPipelineCreateFailed:
        return "ARP-EDITOR-VIEWPORT-DEVICE-PIPELINE-CREATE-FAILED";
    }
    return "ARP-EDITOR-VIEWPORT-DEVICE-PIPELINE-DIAGNOSTIC-UNKNOWN";
}

} // namespace arp::editor
