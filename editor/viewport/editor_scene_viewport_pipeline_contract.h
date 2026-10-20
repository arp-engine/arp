#pragma once

#include "editor/viewport/editor_scene_viewport_shader_binding_contract.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_pipeline.h"
#include "shader/shader_compiler.h"

#include <string_view>

namespace arp::editor {

enum class EditorSceneViewportPipelineDiagnostic {
    None,
    ReflectionContractUnsupported,
    InvalidModulePath,
    EntryPointMismatch,
    PushConstantMismatch,
    VertexInputMismatch,
    AttachmentContractMismatch,
    RhiPipelineStateInvalid,
};

enum class EditorSceneViewportPipelineLoweringDisposition {
    Native,
    EquivalentLowering,
    ContractValidationOnly,
    DiagnosticFailure,
};

enum class EditorSceneViewportPipelineLoweringDiagnostic {
    None,
    NullValidatorOnly,
    PlanVersionUnsupported,
    OutcomeMismatch,
    ExecutablePayloadUnavailable,
    BudgetInsufficient,
    LifetimeOwnerMismatch,
    PhysicalBindingMissing,
    UploadAlignmentInvalid,
    BackendUnsupported,
};

struct EditorSceneViewportPipelineLoweringResult {
    EditorSceneViewportPipelineLoweringDisposition disposition =
        EditorSceneViewportPipelineLoweringDisposition::DiagnosticFailure;
    EditorSceneViewportPipelineLoweringDiagnostic diagnostic =
        EditorSceneViewportPipelineLoweringDiagnostic::None;

    bool executable() const noexcept
    {
        return diagnostic == EditorSceneViewportPipelineLoweringDiagnostic::None
            && (disposition == EditorSceneViewportPipelineLoweringDisposition::Native
                || disposition
                    == EditorSceneViewportPipelineLoweringDisposition::EquivalentLowering);
    }
};

struct EditorSceneViewportPipelineContractResult {
    EditorSceneViewportPipelineDiagnostic diagnostic = EditorSceneViewportPipelineDiagnostic::None;
    rhi::RenderPipelineDesc pipeline;
    rhi::PipelineStateValidationResult stateValidation;
    rhi::ShaderReflectionDesc shaderReflection;
    rhi::PipelineLayoutDesc pipelineLayout;
    EditorSceneViewportMaterialShaderBindings materialBindings;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportPipelineDiagnostic::None && stateValidation.success;
    }
};

// Builds only L2 pipeline state; artifact and device pipeline creation remain separate gates.
ARP_API EditorSceneViewportPipelineContractResult buildEditorSceneViewportPipelineContract(
    const shader::ShaderReflection& reflection,
    std::string_view modulePath,
    rhi::TextureFormat colorFormat,
    rhi::TextureFormat depthFormat,
    std::uint32_t sampleCount);

ARP_API const char* editorSceneViewportPipelineDiagnosticCode(
    EditorSceneViewportPipelineDiagnostic diagnostic) noexcept;

// Uses the per-device lowering plan and artifact-owned physical binding facts.
// This gates pipeline creation only; it does not claim viewport draw execution.
ARP_API EditorSceneViewportPipelineLoweringResult evaluateEditorSceneViewportPipelineLowering(
    const rhi::ShaderReflectionDesc& reflection,
    const rhi::DeviceCapabilities& capabilities) noexcept;

ARP_API const char* editorSceneViewportPipelineLoweringDiagnosticCode(
    EditorSceneViewportPipelineLoweringDiagnostic diagnostic) noexcept;

} // namespace arp::editor
