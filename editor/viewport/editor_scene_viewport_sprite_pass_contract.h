#pragma once
#include <cstdint>

namespace arp::editor {
inline constexpr std::uint32_t kEditorSceneViewportSpritePassContractVersion = 1u;
enum class EditorSceneViewportSpritePassBackend { Null, Vulkan, Metal, WebGPU };
enum class EditorSceneViewportSpritePassBackendDisposition {
    ContractValidationOnly,
    NativeL2MappingDefined,
    EquivalentL2MappingDefined,
    DiagnosticFailure,
};
enum class EditorSceneViewportSpritePassAdmissionDiagnostic {
    None,
    UnsupportedPlanVersion,
    BackendMismatch,
    NullValidatorOnly,
    CapabilityUnavailable,
    ExecutorPayloadUnavailable,
};
enum class EditorSceneViewportSpritePassDiagnostic {
    None,
    UnsupportedContractVersion,
    InvalidBudget,
    DrawBudgetExceeded,
    AtlasBudgetExceeded,
    UploadBudgetExceeded
};

struct EditorSceneViewportSpritePassBudget {
    std::uint32_t maxDrawItems = 0;
    std::uint32_t maxUniqueAtlases = 0;
    std::uint64_t maxTransientUploadBytes = 0;
};

struct EditorSceneViewportSpritePassDemand {
    std::uint32_t contractVersion = kEditorSceneViewportSpritePassContractVersion;
    std::uint32_t drawItems = 0;
    std::uint32_t uniqueAtlases = 0;
    std::uint64_t transientUploadBytes = 0;
};

struct EditorSceneViewportSpritePassValidationResult {
    EditorSceneViewportSpritePassDiagnostic diagnostic =
        EditorSceneViewportSpritePassDiagnostic::None;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportSpritePassDiagnostic::None;
    }
};

struct EditorSceneViewportSpritePassLoweringPlan {
    std::uint32_t contractVersion = kEditorSceneViewportSpritePassContractVersion;
    EditorSceneViewportSpritePassBackend backend = EditorSceneViewportSpritePassBackend::Null;
    EditorSceneViewportSpritePassBackendDisposition disposition =
        EditorSceneViewportSpritePassBackendDisposition::DiagnosticFailure;
};

struct EditorSceneViewportSpritePassAdmissionResult {
    EditorSceneViewportSpritePassAdmissionDiagnostic diagnostic =
        EditorSceneViewportSpritePassAdmissionDiagnostic::ExecutorPayloadUnavailable;

    bool executable() const noexcept
    {
        return diagnostic == EditorSceneViewportSpritePassAdmissionDiagnostic::None;
    }
};

struct EditorSceneViewportSpritePassSemantics final {
    enum class ColorLoad { LoadExistingOpaqueColor };
    enum class DepthLoad { LoadExistingOpaqueDepth };
    enum class DepthAccess { TestLessEqualWithoutWrite };
    enum class Blend { PremultipliedSourceOver };
    enum class Ordering { ExistingSpriteOrderWithStableObjectIdentity };
    enum class Projection { SceneViewportCameraViewProjection };
    enum class Completion { StoreAttachmentsThenPublishColorForSampling };
    static constexpr ColorLoad colorLoad = ColorLoad::LoadExistingOpaqueColor;
    static constexpr DepthLoad depthLoad = DepthLoad::LoadExistingOpaqueDepth;
    static constexpr DepthAccess depthAccess = DepthAccess::TestLessEqualWithoutWrite;
    static constexpr Blend blend = Blend::PremultipliedSourceOver;
    static constexpr Ordering ordering = Ordering::ExistingSpriteOrderWithStableObjectIdentity;
    static constexpr Projection projection = Projection::SceneViewportCameraViewProjection;
    static constexpr Completion completion =
        Completion::StoreAttachmentsThenPublishColorForSampling;
};

inline EditorSceneViewportSpritePassValidationResult validateEditorSceneViewportSpritePass(
    const EditorSceneViewportSpritePassDemand& demand,
    const EditorSceneViewportSpritePassBudget& budget) noexcept
{
    if (demand.contractVersion != kEditorSceneViewportSpritePassContractVersion)
        return {EditorSceneViewportSpritePassDiagnostic::UnsupportedContractVersion};
    if (budget.maxDrawItems == 0 || budget.maxUniqueAtlases == 0
        || budget.maxTransientUploadBytes == 0)
        return {EditorSceneViewportSpritePassDiagnostic::InvalidBudget};
    if (demand.drawItems > budget.maxDrawItems)
        return {EditorSceneViewportSpritePassDiagnostic::DrawBudgetExceeded};
    if (demand.uniqueAtlases > budget.maxUniqueAtlases)
        return {EditorSceneViewportSpritePassDiagnostic::AtlasBudgetExceeded};
    if (demand.transientUploadBytes > budget.maxTransientUploadBytes)
        return {EditorSceneViewportSpritePassDiagnostic::UploadBudgetExceeded};
    return {};
}

inline EditorSceneViewportSpritePassLoweringPlan lowerEditorSceneViewportSpritePass(
    EditorSceneViewportSpritePassBackend backend) noexcept
{
    EditorSceneViewportSpritePassLoweringPlan plan;
    plan.backend = backend;
    switch (backend) {
    case EditorSceneViewportSpritePassBackend::Null:
        plan.disposition = EditorSceneViewportSpritePassBackendDisposition::ContractValidationOnly;
        break;
    case EditorSceneViewportSpritePassBackend::Vulkan:
    case EditorSceneViewportSpritePassBackend::Metal:
        plan.disposition = EditorSceneViewportSpritePassBackendDisposition::NativeL2MappingDefined;
        break;
    case EditorSceneViewportSpritePassBackend::WebGPU:
        plan.disposition =
            EditorSceneViewportSpritePassBackendDisposition::EquivalentL2MappingDefined;
        break;
    }
    return plan;
}

inline EditorSceneViewportSpritePassAdmissionResult admitEditorSceneViewportSpritePass(
    const EditorSceneViewportSpritePassLoweringPlan& plan,
    EditorSceneViewportSpritePassBackend requestedBackend,
    bool capabilityAvailable,
    bool executorPayloadReady) noexcept
{
    if (plan.contractVersion != kEditorSceneViewportSpritePassContractVersion)
        return {EditorSceneViewportSpritePassAdmissionDiagnostic::UnsupportedPlanVersion};
    if (plan.backend != requestedBackend)
        return {EditorSceneViewportSpritePassAdmissionDiagnostic::BackendMismatch};
    switch (plan.disposition) {
    case EditorSceneViewportSpritePassBackendDisposition::ContractValidationOnly:
        return {EditorSceneViewportSpritePassAdmissionDiagnostic::NullValidatorOnly};
    case EditorSceneViewportSpritePassBackendDisposition::NativeL2MappingDefined:
    case EditorSceneViewportSpritePassBackendDisposition::EquivalentL2MappingDefined:
        if (!capabilityAvailable)
            return {EditorSceneViewportSpritePassAdmissionDiagnostic::CapabilityUnavailable};
        if (!executorPayloadReady)
            return {EditorSceneViewportSpritePassAdmissionDiagnostic::ExecutorPayloadUnavailable};
        return {EditorSceneViewportSpritePassAdmissionDiagnostic::None};
    case EditorSceneViewportSpritePassBackendDisposition::DiagnosticFailure:
        return {EditorSceneViewportSpritePassAdmissionDiagnostic::ExecutorPayloadUnavailable};
    }
    return {EditorSceneViewportSpritePassAdmissionDiagnostic::ExecutorPayloadUnavailable};
}

inline const char* editorSceneViewportSpritePassAdmissionDiagnosticCode(
    EditorSceneViewportSpritePassAdmissionDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportSpritePassAdmissionDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-ADMITTED";
    case EditorSceneViewportSpritePassAdmissionDiagnostic::UnsupportedPlanVersion:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-PLAN-VERSION-UNSUPPORTED";
    case EditorSceneViewportSpritePassAdmissionDiagnostic::BackendMismatch:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-PLAN-BACKEND-MISMATCH";
    case EditorSceneViewportSpritePassAdmissionDiagnostic::NullValidatorOnly:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-NULL-VALIDATOR-ONLY";
    case EditorSceneViewportSpritePassAdmissionDiagnostic::CapabilityUnavailable:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-CAPABILITY-UNAVAILABLE";
    case EditorSceneViewportSpritePassAdmissionDiagnostic::ExecutorPayloadUnavailable:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-EXECUTOR-PAYLOAD-UNAVAILABLE";
    }
    return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-ADMISSION-UNKNOWN";
}

inline const char* editorSceneViewportSpritePassDiagnosticCode(
    EditorSceneViewportSpritePassDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportSpritePassDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-OK";
    case EditorSceneViewportSpritePassDiagnostic::UnsupportedContractVersion:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-CONTRACT-VERSION-UNSUPPORTED";
    case EditorSceneViewportSpritePassDiagnostic::InvalidBudget:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-BUDGET-INVALID";
    case EditorSceneViewportSpritePassDiagnostic::DrawBudgetExceeded:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-DRAW-BUDGET-EXCEEDED";
    case EditorSceneViewportSpritePassDiagnostic::AtlasBudgetExceeded:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-ATLAS-BUDGET-EXCEEDED";
    case EditorSceneViewportSpritePassDiagnostic::UploadBudgetExceeded:
        return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-UPLOAD-BUDGET-EXCEEDED";
    }
    return "ARP-EDITOR-VIEWPORT-SPRITE-PASS-DIAGNOSTIC-UNKNOWN";
}
} // namespace arp::editor
