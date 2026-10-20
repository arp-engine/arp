#pragma once

#include "rhi/gpu_pipeline.h"

#include <cstdint>

namespace arp::rhi::semantics {

inline bool viewportStatesEqual(const ViewportState& a, const ViewportState& b) noexcept
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height
        && a.minDepth == b.minDepth && a.maxDepth == b.maxDepth;
}

inline bool scissorStatesEqual(const ScissorState& a, const ScissorState& b) noexcept
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// Stable diagnostic code prefixes shared by all four backends for the
// dynamic viewport/scissor commands. Every validator failure MUST include
// exactly one prefix; tests assert the same code string regardless of
// backend. The codes mirror the PushConstantDiagCode convention.
//
// Note: [VS-E017] is emitted by validateRenderPipelineState (gpu_pipeline.h)
// when a descriptor mixes dynamicViewportScissor with hasViewport/hasScissor;
// it is declared here so the contract test can reference a single source of
// truth. The value-level codes (E001-E007) are emitted by the validators
// below and by each backend's pass-open / pipeline-bound gate.
namespace ViewportScissorDiagCode {
inline constexpr const char* kPassNotOpen = "[VS-E001] ";
inline constexpr const char* kNoPipeline = "[VS-E002] ";
inline constexpr const char* kWidthLeZero = "[VS-E003] ";
inline constexpr const char* kHeightLeZero = "[VS-E004] ";
inline constexpr const char* kDepthRange = "[VS-E005] ";
inline constexpr const char* kScissorZero = "[VS-E006] ";
inline constexpr const char* kNoExecPayload = "[VS-E007] ";
inline constexpr const char* kConflictStatic = "[VS-E017] ";
} // namespace ViewportScissorDiagCode

/// Validate a setViewport command's value semantics.
///
/// This is the L1 shared validator - every backend MUST call it before
/// lowering the command. The viewport value profile requires:
///
///   - width  > 0.0f  (zero/negative width is rejected; the scissor-style
///                     zero check does not apply to floats, but a viewport
///                     with zero extent produces no visible pixels)
///   - height > 0.0f
///   - minDepth <= maxDepth  (reversed depth range is an explicit opt-out
///                             of the default [0,1] range and not supported
///                             by the L2 dynamic-viewport contract; backends
///                             that need inverse-depth must do so via the
///                             pipeline's static viewport with hasViewport)
///
/// @return ValidationResult with success=true and empty diagnostics on pass,
///         or success=false with a stable diagnostic string on failure.
ValidationResult validateViewportCommand(const rhi::ViewportState& viewport);

// Allocation-free classification for bounded recording contracts. The public
// validator below preserves its existing diagnostic text and semantics.
inline const char* viewportCommandDiagnostic(const rhi::ViewportState& viewport) noexcept
{
    if (viewport.width <= 0.0f)
        return ViewportScissorDiagCode::kWidthLeZero;
    if (viewport.height <= 0.0f)
        return ViewportScissorDiagCode::kHeightLeZero;
    if (viewport.minDepth > viewport.maxDepth)
        return ViewportScissorDiagCode::kDepthRange;
    return nullptr;
}

/// Validate a setScissor command's value semantics.
///
/// The scissor value profile requires:
///
///   - width  > 0  (zero-size scissor culls all geometry; this is almost
///                  always a bug, not an intent, so the L2 contract rejects
///                  it at validation time rather than silently producing
///                  an empty draw)
///   - height > 0
///
/// x/y offsets are not range-checked here; the backend's native validation
/// layer (Vulkan VUID-vkCmdSetScissor-03052, etc.) rejects out-of-bounds
/// rects at draw time. The L1 validator only checks the value invariants
/// the contract can express without knowing the attachment extent.
///
/// @return ValidationResult with success=true and empty diagnostics on pass,
///         or success=false with a stable diagnostic string on failure.
ValidationResult validateScissorCommand(const rhi::ScissorState& scissor);

inline const char* scissorCommandDiagnostic(const rhi::ScissorState& scissor) noexcept
{
    return !scissor.width || !scissor.height ? ViewportScissorDiagCode::kScissorZero : nullptr;
}

} // namespace arp::rhi::semantics
