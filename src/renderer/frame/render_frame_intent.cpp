#include "renderer/frame/render_frame_intent.h"

#include <limits>
#include <utility>

namespace arp::renderer {

namespace {

const std::array<RenderFrameStage, kRenderFrameStageCount> kStageOrder{
    RenderFrameStage::ShadowDepth,
    RenderFrameStage::WorldOpaque,
    RenderFrameStage::WorldTransparent,
    RenderFrameStage::StylizedAux,
    RenderFrameStage::Postprocess,
    RenderFrameStage::UI,
    RenderFrameStage::Debug,
};

bool hasText(const arp::core::pmr::String& text)
{
    return !text.empty();
}

void addDiagnostic(RenderFrameIntentValidationResult& result,
    RenderFrameIntentDiagnosticCode code,
    const char* message)
{
    result.success = false;
    result.diagnostics.push_back({code, message});
}

template <class T>
void validateBoundedCount(RenderFrameIntentValidationResult& result,
    const arp::core::pmr::Vector<T>& records,
    std::size_t capacity,
    const char* label)
{
    if (records.size() <= capacity) {
        return;
    }

    arp::core::pmr::String message{"RenderFrameIntent "};
    message += label;
    message += " exceeds its explicit bounded capacity.";
    result.success = false;
    result.diagnostics.push_back({
        RenderFrameIntentDiagnosticCode::CapacityExceeded,
        std::move(message),
    });
}

bool attachmentContractExists(
    const RenderFrameIntent& intent, const arp::core::pmr::String& attachmentId)
{
    for (const auto& attachment : intent.attachmentRequirements) {
        if (attachment.id == attachmentId) {
            return attachment.explicitContract && attachment.format != rhi::TextureFormat::Unknown
                && attachment.extent.width != 0u && attachment.extent.height != 0u
                && attachment.initialState != rhi::ResourceState::Undefined
                && attachment.finalState != rhi::ResourceState::Undefined;
        }
    }
    return false;
}

void validateAttachmentRequirements(
    RenderFrameIntentValidationResult& result, const RenderFrameIntent& intent)
{
    for (const auto& attachment : intent.attachmentRequirements) {
        if (!hasText(attachment.id) || !attachment.explicitContract) {
            addDiagnostic(result,
                RenderFrameIntentDiagnosticCode::MissingAttachmentContract,
                "RenderFrameIntent attachment contract requires an explicit id and contract marker.");
            continue;
        }
        if (attachment.format == rhi::TextureFormat::Unknown || attachment.extent.width == 0u
            || attachment.extent.height == 0u
            || attachment.initialState == rhi::ResourceState::Undefined
            || attachment.finalState == rhi::ResourceState::Undefined) {
            addDiagnostic(result,
                RenderFrameIntentDiagnosticCode::MissingAttachmentContract,
                "RenderFrameIntent attachment contract requires explicit extent, format, and resource states.");
        }
    }
}

void validatePasses(RenderFrameIntentValidationResult& result, const RenderFrameIntent& intent)
{
    arp::core::pmr::Set<arp::core::pmr::String> seenPassIds;

    for (const auto& pass : intent.passes) {
        if (!isRenderFrameStageSupported(pass.stage)) {
            addDiagnostic(result,
                RenderFrameIntentDiagnosticCode::InvalidStage,
                "RenderFrameIntent pass uses an invalid stage.");
        }

        if (!hasText(pass.id)) {
            addDiagnostic(result,
                RenderFrameIntentDiagnosticCode::MissingPassId,
                "RenderFrameIntent pass requires an explicit pass id.");
        } else if (!seenPassIds.insert(pass.id).second) {
            addDiagnostic(result,
                RenderFrameIntentDiagnosticCode::DuplicatePassId,
                "RenderFrameIntent contains a duplicate pass id.");
        }

        for (const auto& attachmentUse : pass.attachmentUses) {
            if (!hasText(attachmentUse.attachmentId)
                || attachmentUse.state == rhi::ResourceState::Undefined
                || !attachmentContractExists(intent, attachmentUse.attachmentId)) {
                addDiagnostic(result,
                    RenderFrameIntentDiagnosticCode::MissingAttachmentContract,
                    "RenderFrameIntent pass references a missing attachment contract.");
            }
        }
    }
}

void validateMultiPassMetadata(
    RenderFrameIntentValidationResult& result, const RenderFrameIntent& intent)
{
    if (!intent.requiresMultiplePhysicalPasses) {
        return;
    }

    if (intent.hasExplicitAttachmentContract || intent.hasExplicitPostprocessContract
        || intent.hasExplicitOffscreenContract) {
        return;
    }

    addDiagnostic(result,
        RenderFrameIntentDiagnosticCode::UnsupportedMultiPassIntent,
        "RenderFrameIntent rejects multi-pass intent without explicit attachment, postprocess, or offscreen metadata.");
}

} // namespace

const std::array<RenderFrameStage, kRenderFrameStageCount>& renderFrameStageOrder()
{
    return kStageOrder;
}

bool isRenderFrameStageSupported(RenderFrameStage stage)
{
    for (const auto orderedStage : kStageOrder) {
        if (orderedStage == stage) {
            return true;
        }
    }
    return false;
}

std::size_t renderFrameStageSortKey(RenderFrameStage stage)
{
    for (std::size_t i = 0; i < kStageOrder.size(); ++i) {
        if (kStageOrder[i] == stage) {
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

const char* renderFrameStageName(RenderFrameStage stage)
{
    switch (stage) {
    case RenderFrameStage::ShadowDepth:
        return "ShadowDepth";
    case RenderFrameStage::WorldOpaque:
        return "WorldOpaque";
    case RenderFrameStage::WorldTransparent:
        return "WorldTransparent";
    case RenderFrameStage::StylizedAux:
        return "StylizedAux";
    case RenderFrameStage::Postprocess:
        return "Postprocess";
    case RenderFrameStage::UI:
        return "UI";
    case RenderFrameStage::Debug:
        return "Debug";
    case RenderFrameStage::Invalid:
        return "Invalid";
    }
    return "Invalid";
}

RenderFrameIntentValidationResult RenderFrameIntentValidator::validate(
    const RenderFrameIntent& intent) const
{
    RenderFrameIntentValidationResult result;
    result.success = true;

    if (!hasText(intent.contractName) || intent.contractVersion == 0u) {
        addDiagnostic(result,
            RenderFrameIntentDiagnosticCode::MissingContract,
            "RenderFrameIntent requires an explicit contract name and version.");
    }

    validateBoundedCount(
        result, intent.sceneRequirements, intent.budget.maxSceneRequirements, "scene requirements");
    validateBoundedCount(
        result, intent.queueRequirements, intent.budget.maxQueueRequirements, "queue requirements");
    validateBoundedCount(result,
        intent.postprocessRequirements,
        intent.budget.maxPostprocessRequirements,
        "postprocess requirements");
    validateBoundedCount(
        result, intent.uiRequirements, intent.budget.maxUiRequirements, "UI requirements");
    validateBoundedCount(result,
        intent.attachmentRequirements,
        intent.budget.maxAttachmentRequirements,
        "attachment requirements");
    validateBoundedCount(result,
        intent.preparedSubmitRequirements,
        intent.budget.maxPreparedSubmitRequirements,
        "prepared-submit requirements");
    validateBoundedCount(result, intent.passes, intent.budget.maxPasses, "passes");

    validateAttachmentRequirements(result, intent);
    validatePasses(result, intent);
    validateMultiPassMetadata(result, intent);

    return result;
}

} // namespace arp::renderer
