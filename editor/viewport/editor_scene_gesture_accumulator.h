#pragma once

#include "core/math/math.h"

#include <cmath>

namespace arp::editor {

// Fixed-size input state. Prepare a copy, apply its total to the immutable
// gesture start pose, then retain the copy only after the command succeeds.
class EditorSceneGestureAccumulator final {
public:
    void reset(core::Vec3 pointer = {}) noexcept
    {
        amount_ = 0;
        pointerX_ = pointer.x;
        pointerY_ = pointer.y;
    }

    bool addLinear(float deltaX, float deltaY, core::Vec3 screenAxis) noexcept
    {
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(screenAxis.x)
            || !std::isfinite(screenAxis.y))
            return false;
        const double lengthSquared =
            double(screenAxis.x) * screenAxis.x + double(screenAxis.y) * screenAxis.y;
        // Hit testing requires an axis at least one screen pixel long.
        if (lengthSquared < 1)
            return false;
        const double next = amount_
            + (double(deltaX) * screenAxis.x + double(deltaY) * screenAxis.y) / lengthSquared;
        if (!std::isfinite(next))
            return false;
        amount_ = next;
        return true;
    }

    bool addRotation(float deltaX, float deltaY, float orientationSign) noexcept
    {
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY)
            || (orientationSign != 1 && orientationSign != -1))
            return false;
        const double nextX = pointerX_ + deltaX;
        const double nextY = pointerY_ + deltaY;
        const double cross = pointerX_ * nextY - pointerY_ * nextX;
        const double dot = pointerX_ * nextX + pointerY_ * nextY;
        // A zero-length radius has no angle. Never guess a rotation axis/sign.
        if (!std::isfinite(cross) || !std::isfinite(dot) || (cross == 0 && dot == 0))
            return false;
        const double next = amount_ + std::atan2(cross, dot) * orientationSign;
        if (!std::isfinite(next))
            return false;
        amount_ = next;
        pointerX_ = nextX;
        pointerY_ = nextY;
        return true;
    }

    double amount() const noexcept { return amount_; }

private:
    double amount_ = 0;
    double pointerX_ = 0;
    double pointerY_ = 0;
};
} // namespace arp::editor
