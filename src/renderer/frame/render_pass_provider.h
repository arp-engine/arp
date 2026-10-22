#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/frame/render_frame_intent.h"

#include <cstddef>

namespace arp::renderer {

struct RenderPassDeclaration {
    arp::core::pmr::String id;
    RenderFrameStage stage = RenderFrameStage::Invalid;
    std::size_t declaredOrder = 0;
    arp::core::pmr::String providerId;
    arp::core::pmr::String capabilityLabel;
    arp::core::pmr::Vector<RenderFrameAttachmentUse> attachmentUses;
};

struct RenderPassQueueSemantic {
    RenderFrameQueueKind kind = RenderFrameQueueKind::Scene3D;
    std::size_t order = 0;
    std::size_t drawItemCount = 0;
};

struct RenderPassProviderSemanticRecord {
    arp::core::pmr::String passName;
    std::size_t physicalPassCount = 0;
    arp::core::pmr::Vector<RenderPassQueueSemantic> queues;
};

enum class RenderPassProviderDiagnosticCode {
    None,
    MissingProviderId,
    DuplicateProviderId,
    UnsupportedStage,
    MissingCapabilityLabel,
    MissingPassId,
    MissingAttachmentState,
    ProviderFailed,
    MultiPassMetadataMissing,
    IncompatibleSceneRuntimeIntent,
};

struct RenderPassProviderDiagnostic {
    RenderPassProviderDiagnosticCode code = RenderPassProviderDiagnosticCode::None;
    arp::core::pmr::String message;
};

struct RenderPassProviderRegistrationResult {
    bool success = false;
    arp::core::pmr::Vector<RenderPassProviderDiagnostic> diagnostics;
};

struct RenderPassProviderResult {
    bool success = false;
    arp::core::pmr::Vector<RenderPassDeclaration> passes;
    arp::core::pmr::Vector<RenderPassProviderSemanticRecord> semanticRecords;
    arp::core::pmr::Vector<RenderPassProviderDiagnostic> diagnostics;
};

class IRenderPassProvider {
public:
    virtual ~IRenderPassProvider() = default;
    virtual const char* providerId() const = 0;
    virtual RenderFrameStage stage() const = 0;
    virtual const char* capabilityLabel() const = 0;

    virtual bool supports(const RenderFrameIntent& intent) const
    {
        return intent.contractVersion != 0u;
    }

    virtual RenderPassProviderResult provide(const RenderFrameIntent& intent) const = 0;
};

class ARP_API RenderPassProviderRegistry final {
public:
    RenderPassProviderRegistrationResult registerProvider(const IRenderPassProvider& provider);

    RenderPassProviderResult composePasses(const RenderFrameIntent& intent) const;

private:
    arp::core::pmr::Vector<const IRenderPassProvider*> providers_;
};

} // namespace arp::renderer
