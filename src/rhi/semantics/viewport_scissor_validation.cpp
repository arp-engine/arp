#include "rhi/semantics/viewport_scissor_validation.h"

namespace arp::rhi::semantics {

ValidationResult validateViewportCommand(const rhi::ViewportState& viewport)
{
    // Width / height: zero or negative extent produces no visible pixels and
    // is rejected by every backend's native validation layer anyway; the L2
    // contract rejects it here so the diagnostic carries a stable code that
    // the contract test can assert on regardless of backend.
    const auto* diagnostic = viewportCommandDiagnostic(viewport);
    if (diagnostic == ViewportScissorDiagCode::kWidthLeZero) {
        return {false,
            std::pmr::string(ViewportScissorDiagCode::kWidthLeZero) + "viewport width must be > 0"};
    }
    if (diagnostic == ViewportScissorDiagCode::kHeightLeZero) {
        return {false,
            std::pmr::string(ViewportScissorDiagCode::kHeightLeZero)
                + "viewport height must be > 0"};
    }
    // Depth range: minDepth > maxDepth is a reversed range. Vulkan's
    // vkCmdSetViewport has no native validation for this (the spec only
    // requires minDepth in [0,1] and maxDepth in [0,1]); a reversed range
    // silently produces an inside-out depth test. The L2 contract rejects
    // it explicitly so the failure is observable before draw time.
    if (diagnostic == ViewportScissorDiagCode::kDepthRange) {
        return {false,
            std::pmr::string(ViewportScissorDiagCode::kDepthRange)
                + "viewport minDepth must be <= maxDepth"};
    }
    return {true, ""};
}

ValidationResult validateScissorCommand(const rhi::ScissorState& scissor)
{
    // Zero-size scissor culls all geometry; this is almost always a bug
    // rather than an intent. Reject it at validation time so the failure is
    // observable with a stable diagnostic code.
    if (scissorCommandDiagnostic(scissor)) {
        return {false,
            std::pmr::string(ViewportScissorDiagCode::kScissorZero)
                + "scissor width and height must be > 0"};
    }
    return {true, ""};
}

} // namespace arp::rhi::semantics
