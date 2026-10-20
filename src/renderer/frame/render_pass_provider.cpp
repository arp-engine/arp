#include "renderer/frame/render_pass_provider.h"

#include <algorithm>
#include <cstring>

namespace arp::renderer {

namespace {

bool hasText(const char* text)
{
    return text != nullptr && text[0] != '\0';
}

bool hasText(const arp::core::pmr::String& text)
{
    return !text.empty();
}

RenderPassProviderDiagnostic makeDiagnostic(
    RenderPassProviderDiagnosticCode code, const char* message)
{
    return {code, message};
}

RenderPassProviderRegistrationResult registrationFailure(
    RenderPassProviderDiagnosticCode code, const char* message)
{
    RenderPassProviderRegistrationResult result;
    result.success = false;
    result.diagnostics.push_back(makeDiagnostic(code, message));
    return result;
}

void addDiagnostic(
    RenderPassProviderResult& result, RenderPassProviderDiagnosticCode code, const char* message)
{
    result.success = false;
    result.diagnostics.push_back(makeDiagnostic(code, message));
}

bool hasExplicitAttachmentState(const RenderFrameAttachmentUse& use)
{
    return hasText(use.attachmentId) && use.state != rhi::ResourceState::Undefined;
}

void validatePassDeclaration(RenderPassProviderResult& result, const RenderPassDeclaration& pass)
{
    if (!hasText(pass.id)) {
        addDiagnostic(result,
            RenderPassProviderDiagnosticCode::MissingPassId,
            "RenderPassProvider pass declaration requires an explicit pass id.");
    }
    if (!isRenderFrameStageSupported(pass.stage)) {
        addDiagnostic(result,
            RenderPassProviderDiagnosticCode::UnsupportedStage,
            "RenderPassProvider pass declaration uses an unsupported stage.");
    }
    if (!hasText(pass.providerId)) {
        addDiagnostic(result,
            RenderPassProviderDiagnosticCode::MissingProviderId,
            "RenderPassProvider pass declaration requires an explicit provider id.");
    }
    if (!hasText(pass.capabilityLabel)) {
        addDiagnostic(result,
            RenderPassProviderDiagnosticCode::MissingCapabilityLabel,
            "RenderPassProvider pass declaration requires an explicit capability label.");
    }
    if (pass.attachmentUses.empty()) {
        addDiagnostic(result,
            RenderPassProviderDiagnosticCode::MissingAttachmentState,
            "RenderPassProvider pass declaration requires explicit read/write attachment state.");
    }
    for (const auto& attachmentUse : pass.attachmentUses) {
        if (!hasExplicitAttachmentState(attachmentUse)) {
            addDiagnostic(result,
                RenderPassProviderDiagnosticCode::MissingAttachmentState,
                "RenderPassProvider pass declaration is missing explicit read/write attachment state.");
        }
    }
}

bool passComesBefore(const RenderPassDeclaration& lhs, const RenderPassDeclaration& rhs)
{
    const auto lhsStage = renderFrameStageSortKey(lhs.stage);
    const auto rhsStage = renderFrameStageSortKey(rhs.stage);
    if (lhsStage != rhsStage) {
        return lhsStage < rhsStage;
    }
    if (lhs.declaredOrder != rhs.declaredOrder) {
        return lhs.declaredOrder < rhs.declaredOrder;
    }
    return lhs.providerId < rhs.providerId;
}

} // namespace

RenderPassProviderRegistrationResult RenderPassProviderRegistry::registerProvider(
    const IRenderPassProvider& provider)
{
    if (!hasText(provider.providerId())) {
        return registrationFailure(RenderPassProviderDiagnosticCode::MissingProviderId,
            "RenderPassProviderRegistry requires an explicit provider id.");
    }
    if (!isRenderFrameStageSupported(provider.stage())) {
        return registrationFailure(RenderPassProviderDiagnosticCode::UnsupportedStage,
            "RenderPassProviderRegistry rejects providers with unsupported stages.");
    }
    if (!hasText(provider.capabilityLabel())) {
        return registrationFailure(RenderPassProviderDiagnosticCode::MissingCapabilityLabel,
            "RenderPassProviderRegistry requires an explicit capability label.");
    }

    for (const auto* registeredProvider : providers_) {
        if (registeredProvider != nullptr && hasText(registeredProvider->providerId())
            && std::strcmp(registeredProvider->providerId(), provider.providerId()) == 0) {
            return registrationFailure(RenderPassProviderDiagnosticCode::DuplicateProviderId,
                "RenderPassProviderRegistry rejects duplicate provider ids.");
        }
    }

    providers_.push_back(&provider);
    return {true, {}};
}

RenderPassProviderResult RenderPassProviderRegistry::composePasses(
    const RenderFrameIntent& intent) const
{
    RenderPassProviderResult result;
    result.success = true;

    for (const auto* provider : providers_) {
        if (provider == nullptr) {
            continue;
        }
        if (!provider->supports(intent)) {
            continue;
        }

        auto providerResult = provider->provide(intent);
        if (!providerResult.success) {
            result.success = false;
            for (const auto& diagnostic : providerResult.diagnostics) {
                result.diagnostics.push_back(diagnostic);
            }
            continue;
        }

        for (const auto& pass : providerResult.passes) {
            validatePassDeclaration(result, pass);
            result.passes.push_back(pass);
        }
        for (const auto& semantic : providerResult.semanticRecords) {
            result.semanticRecords.push_back(semantic);
        }
    }

    if (result.success) {
        std::sort(result.passes.begin(), result.passes.end(), passComesBefore);
    }
    return result;
}

} // namespace arp::renderer
