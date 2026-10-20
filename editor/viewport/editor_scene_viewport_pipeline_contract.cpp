#include "editor/viewport/editor_scene_viewport_pipeline_contract.h"

#include "assets/asset_pipeline.h"
#include "editor/viewport/editor_scene_viewport_shader_binding_contract.h"
#include "shader/shader_pipeline_reflection_validator.h"

#include <array>
#include <cstddef>

namespace arp::editor {
namespace {

struct VertexInputContract {
    const char* name;
    const char* semantic;
    shader::VertexAttributeFormat reflectedFormat;
    rhi::VertexFormat pipelineFormat;
    std::uint32_t location;
    std::uint32_t offset;
};

constexpr std::array<VertexInputContract, 4> kVertexInputs{{
    {"position",
        "POSITION0",
        shader::VertexAttributeFormat::Float3,
        rhi::VertexFormat::Float32x3,
        0u,
        offsetof(assets::MeshVertex, position)},
    {"normal",
        "NORMAL0",
        shader::VertexAttributeFormat::Float3,
        rhi::VertexFormat::Float32x3,
        1u,
        offsetof(assets::MeshVertex, normal)},
    {"uv",
        "TEXCOORD0",
        shader::VertexAttributeFormat::Float2,
        rhi::VertexFormat::Float32x2,
        2u,
        offsetof(assets::MeshVertex, uv)},
    {"tangent",
        "TANGENT0",
        shader::VertexAttributeFormat::Float4,
        rhi::VertexFormat::Float32x4,
        3u,
        offsetof(assets::MeshVertex, tangent)},
}};

static_assert(sizeof(assets::MeshVertex) == 48u);

bool hasEntryPoint(
    const shader::ShaderReflection& reflection, const char* name, shader::ShaderStage stage)
{
    for (const auto& entry : reflection.entryPoints) {
        if (entry.name == name && entry.stage == stage)
            return true;
    }
    return false;
}

bool vertexInputMatches(const shader::ShaderReflection& reflection)
{
    if (reflection.vertexAttributes.size() != kVertexInputs.size())
        return false;
    for (const auto& expected : kVertexInputs) {
        const shader::ShaderVertexAttribute* found = nullptr;
        for (const auto& attribute : reflection.vertexAttributes) {
            if (attribute.location != expected.location)
                continue;
            if (found != nullptr)
                return false;
            found = &attribute;
        }
        if (found == nullptr || found->name != expected.name || found->semantic != expected.semantic
            || found->format != expected.reflectedFormat) {
            return false;
        }
    }
    return true;
}

} // namespace

EditorSceneViewportPipelineContractResult buildEditorSceneViewportPipelineContract(
    const shader::ShaderReflection& reflection,
    const std::string_view modulePath,
    const rhi::TextureFormat colorFormat,
    const rhi::TextureFormat depthFormat,
    const std::uint32_t sampleCount)
{
    EditorSceneViewportPipelineContractResult result;
    if (reflection.reflectionContractVersion != shader::kShaderReflectionContractVersion) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::ReflectionContractUnsupported;
        return result;
    }
    if (modulePath.empty()) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::InvalidModulePath;
        return result;
    }
    if (!hasEntryPoint(reflection, "vertexMain", shader::ShaderStage::Vertex)
        || !hasEntryPoint(reflection, "fragmentMain", shader::ShaderStage::Fragment)) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::EntryPointMismatch;
        return result;
    }
    const auto pushStages =
        rhi::parseShaderReflectionPushConstantStages(reflection.pushConstantStages);
    const auto pushLayout =
        shader::ShaderPipelineReflectionValidator::validatePushConstantMembers(reflection);
    if (reflection.pushConstantSizeBytes != 256u
        || reflection.pushConstantContractVersion != "AtomicFullBlockPush/1" || !pushStages
        || *pushStages != (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment)
        || reflection.pushConstantMembers.size() != 7u || !pushLayout.success) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::PushConstantMismatch;
        return result;
    }
    if (!vertexInputMatches(reflection)) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::VertexInputMismatch;
        return result;
    }
    if (colorFormat != rhi::TextureFormat::Rgba16Float
        || depthFormat != rhi::TextureFormat::D32Float || sampleCount != 1u) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::AttachmentContractMismatch;
        return result;
    }
    const auto bindingContract = inspectEditorSceneViewportShaderBindings(reflection);
    if (!bindingContract.valid()) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::ReflectionContractUnsupported;
        return result;
    }
    result.materialBindings = bindingContract.bindings;
    result.shaderReflection = bindingContract.rhiReflection;
    result.pipelineLayout =
        rhi::buildPipelineLayoutDescFromShaderReflection(result.shaderReflection);
    const auto reflectedPushStages =
        rhi::parseShaderReflectionPushConstantStages(result.shaderReflection.pushConstantStages);
    if (!reflectedPushStages || result.pipelineLayout.pushConstantRanges.size() != 1u) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::ReflectionContractUnsupported;
        return result;
    }
    result.pipelineLayout.pushConstantRanges[0].stages = *reflectedPushStages;
    if (!rhi::validatePipelineLayoutAgainstShaderReflection(
            result.pipelineLayout, result.shaderReflection)
            .success) {
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::ReflectionContractUnsupported;
        return result;
    }

    auto& pipeline = result.pipeline;
    pipeline.label = "ARP Editor Scene Viewport PBR";
    pipeline.vertexShader.modulePath = modulePath;
    pipeline.vertexShader.entryPoint = "vertexMain";
    pipeline.vertexShader.stage = rhi::ShaderStage::Vertex;
    pipeline.fragmentShader.modulePath = modulePath;
    pipeline.fragmentShader.entryPoint = "fragmentMain";
    pipeline.fragmentShader.stage = rhi::ShaderStage::Fragment;
    pipeline.colorFormat = colorFormat;
    pipeline.depthFormat = depthFormat;
    auto& color = pipeline.colorTargets.emplace_back();
    color.format = colorFormat;
    color.blendEnabled = false;
    color.colorBlend = {rhi::BlendFactor::One, rhi::BlendFactor::Zero, rhi::BlendOp::Add};
    color.alphaBlend = {rhi::BlendFactor::One, rhi::BlendFactor::Zero, rhi::BlendOp::Add};
    color.writeMask = rhi::ColorWriteMask::All;
    color.resolveTargetRequired = false;

    pipeline.depthStencil.format = depthFormat;
    pipeline.depthStencil.depthTestEnabled = true;
    pipeline.depthStencil.depthWriteEnabled = true;
    pipeline.depthStencil.depthCompare = rhi::CompareOp::LessEqual;
    pipeline.depthStencil.stencilEnabled = false;
    pipeline.depthStencil.front = {
        rhi::StencilOp::Keep, rhi::StencilOp::Keep, rhi::StencilOp::Keep, rhi::CompareOp::Always};
    pipeline.depthStencil.back = pipeline.depthStencil.front;
    pipeline.depthStencil.stencilReadMask = 0xffu;
    pipeline.depthStencil.stencilWriteMask = 0xffu;
    pipeline.depthStencil.depthBiasEnabled = false;
    pipeline.depthStencil.depthBiasConstantFactor = 0.0f;
    pipeline.depthStencil.depthBiasClamp = 0.0f;
    pipeline.depthStencil.depthBiasSlopeFactor = 0.0f;

    pipeline.cullMode = rhi::CullMode::None;
    pipeline.frontFace = rhi::FrontFace::CounterClockwise;
    pipeline.topology = rhi::PrimitiveTopology::TriangleList;
    pipeline.multisample.sampleCount = sampleCount;
    pipeline.multisample.sampleMask = 0xffffffffffffffffull;
    pipeline.multisample.alphaToCoverageEnabled = false;
    pipeline.hasViewport = false;
    pipeline.hasScissor = false;
    pipeline.dynamicViewportScissor = true;
    pipeline.subpassIndex = 0u;

    auto& vertexBuffer = pipeline.vertexBuffers.emplace_back();
    vertexBuffer.slot = 0u;
    vertexBuffer.strideBytes = sizeof(assets::MeshVertex);
    vertexBuffer.inputRate = rhi::VertexInputRate::Vertex;
    for (const auto& input : kVertexInputs)
        vertexBuffer.attributes.push_back({input.location, input.offset, input.pipelineFormat});

    result.stateValidation = rhi::validateRenderPipelineState(pipeline);
    if (!result.stateValidation.success)
        result.diagnostic = EditorSceneViewportPipelineDiagnostic::RhiPipelineStateInvalid;
    return result;
}

EditorSceneViewportPipelineLoweringResult evaluateEditorSceneViewportPipelineLowering(
    const rhi::ShaderReflectionDesc& reflection,
    const rhi::DeviceCapabilities& capabilities) noexcept
{
    using Disposition = EditorSceneViewportPipelineLoweringDisposition;
    using Diagnostic = EditorSceneViewportPipelineLoweringDiagnostic;
    EditorSceneViewportPipelineLoweringResult result;
    if (capabilities.backend == rhi::Backend::Null) {
        result.disposition = Disposition::ContractValidationOnly;
        result.diagnostic = Diagnostic::NullValidatorOnly;
        return result;
    }
    if (capabilities.pushConstantLoweringContractVersion != 1u) {
        result.diagnostic = Diagnostic::PlanVersionUnsupported;
        return result;
    }

    rhi::PushConstantLoweringOutcome expectedOutcome =
        rhi::PushConstantLoweringOutcome::DiagnosticFailure;
    rhi::PushConstantLifetimeOwner expectedOwner = rhi::PushConstantLifetimeOwner::Unknown;
    switch (capabilities.backend) {
    case rhi::Backend::Null:
        result.disposition = Disposition::ContractValidationOnly;
        result.diagnostic = Diagnostic::NullValidatorOnly;
        return result;
    case rhi::Backend::Vulkan:
        expectedOutcome = rhi::PushConstantLoweringOutcome::Native;
        expectedOwner = rhi::PushConstantLifetimeOwner::NativeCommandBuffer;
        result.disposition = Disposition::Native;
        break;
    case rhi::Backend::Metal:
        expectedOutcome = rhi::PushConstantLoweringOutcome::Native;
        expectedOwner = rhi::PushConstantLifetimeOwner::NativeRenderEncoder;
        result.disposition = Disposition::Native;
        break;
    case rhi::Backend::WebGPU:
        expectedOutcome = rhi::PushConstantLoweringOutcome::EquivalentLowering;
        expectedOwner = rhi::PushConstantLifetimeOwner::FrameUploadRing;
        result.disposition = Disposition::EquivalentLowering;
        break;
    case rhi::Backend::WebGL:
    default:
        result.diagnostic = Diagnostic::BackendUnsupported;
        return result;
    }
    if (capabilities.pushConstantLoweringOutcome != expectedOutcome) {
        result.disposition = Disposition::DiagnosticFailure;
        result.diagnostic = Diagnostic::OutcomeMismatch;
        return result;
    }
    if (!capabilities.pushConstantLoweringHasExecutablePayload) {
        result.disposition = Disposition::DiagnosticFailure;
        result.diagnostic = Diagnostic::ExecutablePayloadUnavailable;
        return result;
    }
    if (reflection.pushConstantByteCount == 0u
        || capabilities.pushConstantLoweringBoundedFrameBudgetBytes
            < reflection.pushConstantByteCount) {
        result.disposition = Disposition::DiagnosticFailure;
        result.diagnostic = Diagnostic::BudgetInsufficient;
        return result;
    }
    if (capabilities.pushConstantLoweringLifetimeOwner != expectedOwner) {
        result.disposition = Disposition::DiagnosticFailure;
        result.diagnostic = Diagnostic::LifetimeOwnerMismatch;
        return result;
    }
    if (capabilities.backend == rhi::Backend::Metal
        && (reflection.pushConstantMetalBufferIndex == UINT32_MAX
            || reflection.pushConstantMetalBufferIndex >= 31u)) {
        result.disposition = Disposition::DiagnosticFailure;
        result.diagnostic = Diagnostic::PhysicalBindingMissing;
        return result;
    }
    if (capabilities.backend == rhi::Backend::WebGPU) {
        const auto alignment = capabilities.pushConstantLoweringAlignmentBytes;
        if (reflection.pushConstantWebGpuBindingGroup == UINT32_MAX
            || reflection.pushConstantWebGpuBindingIndex == UINT32_MAX) {
            result.disposition = Disposition::DiagnosticFailure;
            result.diagnostic = Diagnostic::PhysicalBindingMissing;
            return result;
        }
        if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
            result.disposition = Disposition::DiagnosticFailure;
            result.diagnostic = Diagnostic::UploadAlignmentInvalid;
            return result;
        }
    }
    return result;
}

const char* editorSceneViewportPipelineDiagnosticCode(
    EditorSceneViewportPipelineDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportPipelineDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-OK";
    case EditorSceneViewportPipelineDiagnostic::ReflectionContractUnsupported:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-REFLECTION-UNSUPPORTED";
    case EditorSceneViewportPipelineDiagnostic::InvalidModulePath:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-MODULE-PATH-INVALID";
    case EditorSceneViewportPipelineDiagnostic::EntryPointMismatch:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-ENTRYPOINT-MISMATCH";
    case EditorSceneViewportPipelineDiagnostic::PushConstantMismatch:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-PUSH-CONSTANT-MISMATCH";
    case EditorSceneViewportPipelineDiagnostic::VertexInputMismatch:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-VERTEX-INPUT-MISMATCH";
    case EditorSceneViewportPipelineDiagnostic::AttachmentContractMismatch:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-ATTACHMENT-MISMATCH";
    case EditorSceneViewportPipelineDiagnostic::RhiPipelineStateInvalid:
        return "ARP-EDITOR-VIEWPORT-PIPELINE-RHI-STATE-INVALID";
    }
    return "ARP-EDITOR-VIEWPORT-PIPELINE-DIAGNOSTIC-UNKNOWN";
}

const char* editorSceneViewportPipelineLoweringDiagnosticCode(
    const EditorSceneViewportPipelineLoweringDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportPipelineLoweringDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-LOWERING-OK";
    case EditorSceneViewportPipelineLoweringDiagnostic::NullValidatorOnly:
        return "ARP-EDITOR-VIEWPORT-NULL-VALIDATOR-ONLY";
    case EditorSceneViewportPipelineLoweringDiagnostic::PlanVersionUnsupported:
        return "ARP-EDITOR-VIEWPORT-LOWERING-PLAN-VERSION";
    case EditorSceneViewportPipelineLoweringDiagnostic::OutcomeMismatch:
        return "ARP-EDITOR-VIEWPORT-LOWERING-OUTCOME-MISMATCH";
    case EditorSceneViewportPipelineLoweringDiagnostic::ExecutablePayloadUnavailable:
        return "ARP-EDITOR-VIEWPORT-LOWERING-PAYLOAD-UNAVAILABLE";
    case EditorSceneViewportPipelineLoweringDiagnostic::BudgetInsufficient:
        return "ARP-EDITOR-VIEWPORT-LOWERING-BUDGET-INSUFFICIENT";
    case EditorSceneViewportPipelineLoweringDiagnostic::LifetimeOwnerMismatch:
        return "ARP-EDITOR-VIEWPORT-LOWERING-LIFETIME-OWNER-MISMATCH";
    case EditorSceneViewportPipelineLoweringDiagnostic::PhysicalBindingMissing:
        return "ARP-EDITOR-VIEWPORT-LOWERING-PHYSICAL-BINDING-MISSING";
    case EditorSceneViewportPipelineLoweringDiagnostic::UploadAlignmentInvalid:
        return "ARP-EDITOR-VIEWPORT-LOWERING-ALIGNMENT-INVALID";
    case EditorSceneViewportPipelineLoweringDiagnostic::BackendUnsupported:
        return "ARP-EDITOR-VIEWPORT-LOWERING-BACKEND-UNSUPPORTED";
    }
    return "ARP-EDITOR-VIEWPORT-LOWERING-DIAGNOSTIC-UNKNOWN";
}

} // namespace arp::editor
