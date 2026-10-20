#pragma once
#include "2d/runtime_contract.h"

namespace arp::scene2d {
class Camera2D final {
public:
    bool follow(core::Vec2 target, double seconds, double response)
    {
        if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(seconds)
            || seconds < 0 || !std::isfinite(response) || response < 0)
            return false;
        const auto weight = float(-std::expm1(-response * seconds));
        position_ = position_ + core::scale(target - position_, weight);
        return true;
    }

    bool setPosition(core::Vec2 position)
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y))
            return false;
        position_ = position;
        return true;
    }

    bool alignedPosition(float pixelsPerUnit, float scale, core::Vec2& position) const
    {
        const float density = pixelsPerUnit * scale;
        if (!std::isfinite(density) || density <= 0)
            return false;
        position = {std::round(position_.x * density) / density,
            std::round(position_.y * density) / density};
        return true;
    }

    core::Vec2 position() const { return position_; }

private:
    core::Vec2 position_{};
};
} // namespace arp::scene2d
