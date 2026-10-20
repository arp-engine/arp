#pragma once

// Per-frame camera history for Temporal Anti-Aliasing (TAA).
//
// TAA resolve (shaders/wocc/taa.slang) reprojects the previous frame's color
// into the current frame's pixel positions using a per-pixel velocity buffer.
// The velocity buffer is produced by pbr_scene's MRT output, which needs the
// previous frame's view-projection (prevViewProjection) to compute
// prevClipPosition per vertex. This header holds that previous-frame VP so the
// game loop can feed it to the scene pass + TAA resolve without the engine
// owning camera state.
//
// Engine / game boundary: the downstream game (e.g. World of ClaudeCraft)
// owns the camera. Each frame the game:
//   1. Builds both the jittered raster VP and the canonical unjittered VP.
//   2. Pushes the jittered VP into the scene pass and supplies the current and
//      previous unjittered VPs through the temporal-camera UBO.
//   3. Calls advanceTaaCameraHistory() at end-of-frame to roll the buffer
//      forward for next frame.
//
// Matrix convention: row-major core::Mat4 (m[row*4+col] = M[row][col]),
// clip = M . v. Stored row-major, matching AGENTS.md "Matrix Layout" hard
// constraint #1 and the default float4x4 SPIR-V RowMajor decoration consumed
// by the shaders. No transposition on the C++ side.

#include "core/math.h"

namespace arp::renderer {

struct TaaCameraHistory {
    // Previous frame's canonical unjittered view-projection. TAA history is
    // stored on a fixed texel grid, so projection jitter must not be encoded
    // as scene motion or the resolved image follows the jitter phase.
    core::Mat4 prevViewProjection = core::Mat4::identity();

    // False on the first frame (or after a camera cut / history reset). The
    // caller MUST treat the first frame as a history-miss: the velocity buffer
    // is zeroed (prevClipPosition == curClipPosition) and the TAA resolve must
    // bypass history blending (output = current color, no accumulation) to
    // avoid smearing a stale history across a cut.
    bool initialized = false;
};

// Roll the camera history forward at end-of-frame.
//
// After the current frame's scene pass + TAA resolve have consumed the
// previous-frame VP, the current-frame VP becomes the previous-frame VP for
// the next frame. This also flips `initialized` to true so subsequent frames
// know the history is valid for temporal accumulation.
//
// @param history              The history to advance (mutated in place).
// @param currentViewProjection  This frame's canonical unjittered VP.
inline void advanceTaaCameraHistory(
    TaaCameraHistory& history, const core::Mat4& currentViewProjection)
{
    history.prevViewProjection = currentViewProjection;
    history.initialized = true;
}

// Reset the camera history after a camera cut or when the TAA history texture
// is invalidated (resize, device loss, explicit reset). The next frame will be
// treated as a history-miss and the resolve pass will bypass accumulation.
inline void resetTaaCameraHistory(TaaCameraHistory& history)
{
    history.prevViewProjection = core::Mat4::identity();
    history.initialized = false;
}

} // namespace arp::renderer
