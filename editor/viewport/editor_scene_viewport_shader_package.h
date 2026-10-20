#pragma once

#include "editor/viewport/editor_scene_viewport_pipeline_contract.h"
#include "rhi/gpu_device.h"
#include "shader/shader_compiler.h"

#include <cstdint>
#include <string_view>

namespace arp::editor {

struct EditorSceneViewportShaderArtifacts {
    std::string_view sourcePath;
    std::string_view spirvPath;
    std::string_view mslPath;
    std::string_view wgslPath;
    std::string_view reflectionPath;
};

enum class EditorSceneViewportShaderPackageDiagnostic {
    None,
    ArtifactPathMissing,
    ArtifactPathInvalid,
    ReflectionLoadFailed,
    ArtifactIdentityInvalid,
    EntryPointMismatch,
    PipelineContractInvalid,
    PackageHydrationFailed,
    PackageInvalid,
};

struct EditorSceneViewportShaderPackageResult {
    EditorSceneViewportShaderPackageDiagnostic diagnostic =
        EditorSceneViewportShaderPackageDiagnostic::None;
    rhi::ShaderPackageDesc package;
    shader::ShaderReflection shaderReflection;
    EditorSceneViewportPipelineContractResult pipelineContract;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportShaderPackageDiagnostic::None
            && package.generatedReflectionHydrated && pipelineContract.valid();
    }
};

enum class EditorSceneViewportDevicePipelineDiagnostic {
    None,
    ShaderPackageInvalid,
    BackendLoweringRejected,
    PipelineLayoutCreateFailed,
    PipelineValidationFailed,
    RenderPipelineCreateFailed,
};

// Declaration order keeps the native pipeline destroyed before its layout and
// package. The owning device must outlive this value.
struct EditorSceneViewportDevicePipelineResources {
    rhi::ShaderPackageDesc package;
    core::UniquePtr<rhi::PipelineLayout> layout;
    core::UniquePtr<rhi::RenderPipeline> pipeline;
};

struct EditorSceneViewportDevicePipelineResult {
    EditorSceneViewportDevicePipelineDiagnostic diagnostic =
        EditorSceneViewportDevicePipelineDiagnostic::None;
    EditorSceneViewportPipelineLoweringResult lowering;
    core::pmr::String backendDiagnostics{&core::defaultCpuMemoryResource()};
    EditorSceneViewportDevicePipelineResources resources;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportDevicePipelineDiagnostic::None && resources.layout
            && resources.pipeline;
    }
};

// Paths are caller-owned artifact identities; none are inferred from the source path.
ARP_API EditorSceneViewportShaderPackageResult loadEditorSceneViewportShaderPackage(
    const EditorSceneViewportShaderArtifacts& artifacts,
    rhi::TextureFormat colorFormat,
    rhi::TextureFormat depthFormat,
    std::uint32_t sampleCount);

ARP_API EditorSceneViewportDevicePipelineResult createEditorSceneViewportDevicePipeline(
    rhi::GpuDevice& device, const EditorSceneViewportShaderPackageResult& package);

ARP_API const char* editorSceneViewportShaderPackageDiagnosticCode(
    EditorSceneViewportShaderPackageDiagnostic diagnostic) noexcept;

ARP_API const char* editorSceneViewportDevicePipelineDiagnosticCode(
    EditorSceneViewportDevicePipelineDiagnostic diagnostic) noexcept;

} // namespace arp::editor
