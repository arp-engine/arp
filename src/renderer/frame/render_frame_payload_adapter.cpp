#include "renderer/frame/render_frame_payload_adapter.h"

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

bool sameContractName(const arp::core::pmr::String& lhs, const char* rhs)
{
    return hasText(rhs) && lhs == rhs;
}

bool sameKey(const RenderFramePayloadAdapterKey& lhs, const RenderFramePayloadAdapterKey& rhs)
{
    return lhs.kind == rhs.kind && lhs.contractName == rhs.contractName
        && lhs.contractVersion == rhs.contractVersion;
}

RenderFramePayloadAdapterDiagnostic makeDiagnostic(
    RenderFramePayloadAdapterDiagnosticCode code, const char* message)
{
    return {code, message};
}

RenderFramePayloadAdapterRegistrationResult registrationFailure(
    RenderFramePayloadAdapterDiagnosticCode code, const char* message)
{
    RenderFramePayloadAdapterRegistrationResult result;
    result.success = false;
    result.diagnostics.push_back(makeDiagnostic(code, message));
    return result;
}

RenderFramePayloadAdapterResult adapterFailure(
    RenderFramePayloadAdapterDiagnosticCode code, const char* message)
{
    RenderFramePayloadAdapterResult result;
    result.success = false;
    result.diagnostics.push_back(makeDiagnostic(code, message));
    return result;
}

bool keyMatchesPayload(
    const RenderFramePayloadAdapterKey& key, const CustomWorkloadPayloadRef& payload)
{
    return key.kind == payload.kind && sameContractName(key.contractName, payload.contractName)
        && key.contractVersion == payload.contractVersion;
}

bool keyMatchesPayloadContractIgnoringVersion(
    const RenderFramePayloadAdapterKey& key, const CustomWorkloadPayloadRef& payload)
{
    return key.kind == payload.kind && sameContractName(key.contractName, payload.contractName);
}

} // namespace

RenderFramePayloadAdapterRegistrationResult RenderFramePayloadAdapterRegistry::registerAdapter(
    const IRenderFramePayloadAdapter& adapter)
{
    const auto adapterKey = adapter.key();
    if (adapterKey.kind == CustomWorkloadPayloadKind::None || !hasText(adapterKey.contractName)) {
        return registrationFailure(RenderFramePayloadAdapterDiagnosticCode::InvalidAdapterKey,
            "RenderFramePayloadAdapterRegistry requires adapter kind and contract name.");
    }
    if (adapterKey.contractVersion == 0u) {
        return registrationFailure(RenderFramePayloadAdapterDiagnosticCode::MissingContractVersion,
            "RenderFramePayloadAdapterRegistry requires adapter contract version.");
    }

    for (const auto* registeredAdapter : adapters_) {
        if (registeredAdapter != nullptr && sameKey(registeredAdapter->key(), adapterKey)) {
            return registrationFailure(RenderFramePayloadAdapterDiagnosticCode::DuplicateAdapter,
                "RenderFramePayloadAdapterRegistry rejects duplicate adapter contracts.");
        }
    }

    adapters_.push_back(&adapter);
    return {true, {}};
}

RenderFramePayloadAdapterResult RenderFramePayloadAdapterRegistry::adaptPayload(
    const CustomWorkloadPayloadRef& payload) const
{
    if (!hasText(payload.contractName)) {
        return adapterFailure(RenderFramePayloadAdapterDiagnosticCode::MissingContractName,
            "RenderFramePayloadAdapterRegistry requires an explicit payload contract name.");
    }
    if (payload.contractVersion == 0u) {
        return adapterFailure(RenderFramePayloadAdapterDiagnosticCode::MissingContractVersion,
            "RenderFramePayloadAdapterRegistry requires an explicit payload contract version.");
    }

    bool sawMatchingNameWithDifferentVersion = false;
    for (const auto* adapter : adapters_) {
        if (adapter == nullptr) {
            continue;
        }
        const auto adapterKey = adapter->key();
        if (keyMatchesPayload(adapterKey, payload)) {
            return adapter->adapt(payload);
        }
        if (keyMatchesPayloadContractIgnoringVersion(adapterKey, payload)) {
            sawMatchingNameWithDifferentVersion = true;
        }
    }

    if (sawMatchingNameWithDifferentVersion) {
        return adapterFailure(RenderFramePayloadAdapterDiagnosticCode::VersionMismatch,
            "RenderFramePayloadAdapterRegistry found a payload adapter contract name with a version mismatch.");
    }

    return adapterFailure(RenderFramePayloadAdapterDiagnosticCode::UnknownAdapter,
        "RenderFramePayloadAdapterRegistry found an unknown payload adapter contract.");
}

} // namespace arp::renderer
