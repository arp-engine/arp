#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/frame/render_frame_intent.h"
#include "renderer/pipeline/custom_render_frame_workload.h"

#include <cstdint>

namespace arp::renderer {

struct RenderFramePayloadAdapterKey {
    CustomWorkloadPayloadKind kind = CustomWorkloadPayloadKind::None;
    arp::core::pmr::String contractName;
    std::uint32_t contractVersion = 0;
};

enum class RenderFramePayloadAdapterDiagnosticCode {
    None,
    MissingContractName,
    MissingContractVersion,
    InvalidAdapterKey,
    DuplicateAdapter,
    UnknownAdapter,
    VersionMismatch,
    PayloadValidationFailed,
};

struct RenderFramePayloadAdapterDiagnostic {
    RenderFramePayloadAdapterDiagnosticCode code = RenderFramePayloadAdapterDiagnosticCode::None;
    arp::core::pmr::String message;
};

struct RenderFramePayloadAdapterRegistrationResult {
    bool success = false;
    arp::core::pmr::Vector<RenderFramePayloadAdapterDiagnostic> diagnostics;
};

struct RenderFramePayloadAdapterResult {
    bool success = false;
    RenderFrameIntent intent;
    arp::core::pmr::Vector<RenderFramePayloadAdapterDiagnostic> diagnostics;
};

class IRenderFramePayloadAdapter {
public:
    virtual ~IRenderFramePayloadAdapter() = default;
    virtual RenderFramePayloadAdapterKey key() const = 0;
    virtual RenderFramePayloadAdapterResult adapt(
        const CustomWorkloadPayloadRef& payload) const = 0;
};

class ARP_API RenderFramePayloadAdapterRegistry final {
public:
    RenderFramePayloadAdapterRegistrationResult registerAdapter(
        const IRenderFramePayloadAdapter& adapter);

    RenderFramePayloadAdapterResult adaptPayload(const CustomWorkloadPayloadRef& payload) const;

private:
    arp::core::pmr::Vector<const IRenderFramePayloadAdapter*> adapters_;
};

} // namespace arp::renderer
