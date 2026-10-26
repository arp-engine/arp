#pragma once

#include "editor/viewport/editor_scene_transform_frame.h"

namespace arp::editor {

// Relative gesture increments, not an absolute world grid. The original input
// accumulator stays unsnapped so sub-step motion can cross a later boundary.
class EditorSceneTransformSnap final {
public:
    bool enabled = false;
    double moveStep = 1.0;
    double rotateStepRadians = 0.2617993877991494;
    double scaleStep = 0.1;

    bool valid() const noexcept
    {
        return std::isfinite(moveStep) && moveStep > 0 && std::isfinite(rotateStepRadians)
            && rotateStepRadians > 0 && std::isfinite(scaleStep) && scaleStep > 0;
    }

    bool same(const EditorSceneTransformSnap& other) const noexcept
    {
        return enabled == other.enabled && moveStep == other.moveStep
            && rotateStepRadians == other.rotateStepRadians && scaleStep == other.scaleStep;
    }

    // Failure leaves destination unchanged. Exact half steps round away from
    // zero; no epsilon changes the supplied floating-point boundary.
    const char* prepare(
        EditorSceneTransformTool tool, double total, double& destination) const noexcept
    {
        if (!valid() || !std::isfinite(total)
            || (tool != EditorSceneTransformTool::Move && tool != EditorSceneTransformTool::Rotate
                && tool != EditorSceneTransformTool::Scale))
            return "EDITOR-TRANSFORM-SNAP-INVALID";
        double candidate = total;
        if (enabled) {
            const double step = tool == EditorSceneTransformTool::Move ? moveStep
                : tool == EditorSceneTransformTool::Rotate             ? rotateStepRadians
                                                                       : scaleStep;
            const double ratio = total / step;
            // Beyond this bound consecutive integer grid indices cannot be
            // represented by double. Refuse rather than silently skip cells.
            constexpr double maximumExactIndex = 9007199254740991.0;
            if (!std::isfinite(ratio) || std::abs(ratio) > maximumExactIndex)
                return "EDITOR-TRANSFORM-SNAP-RANGE";
            candidate = std::round(ratio) * step;
            if (!std::isfinite(candidate))
                return "EDITOR-TRANSFORM-SNAP-RANGE";
        }
        destination = candidate == 0 ? 0 : candidate;
        return nullptr;
    }
};
} // namespace arp::editor
