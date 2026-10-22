#pragma once
#include "core/math/math.h"
#include "reflection/reflection_macros.h"
#include "scene/scene.h"

#include <cmath>

namespace arp::scene2d {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ScreenCanvas2DEditorProperties {
    APROP(Serialize, Editor)
    float pixelsPerUnit = 100.0f;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ScreenCanvas2DComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "ScreenCanvas2DComponent"; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(ScreenCanvas2DEditorProperties{ppu_});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<ScreenCanvas2DEditorProperties>())
            return false;
        const auto pixelsPerUnit =
            value.cast<const ScreenCanvas2DEditorProperties&>().pixelsPerUnit;
        if (!std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0.0f)
            return false;
        ppu_ = pixelsPerUnit;
        return true;
    }

    float pixelsPerUnit() const noexcept { return ppu_; }

    // Children use logical pixels. The host applies the active 2D camera at
    // extraction; viewport scaling/black bars remain the renderer's contract.
    AFUNC()

    bool set_pixels_per_unit(float value)
    {
        if (!std::isfinite(value) || value <= 0)
            return false;
        ppu_ = value;
        return true;
    }

    bool applyCamera(core::Vec2 position)
    {
        if (!ppu_ || !gameObject())
            return false;
        scene::Transform transform;
        transform.translation = {position.x, position.y, 0};
        transform.scale = {1 / ppu_, 1 / ppu_, 1};
        return gameObject()->set_world_transform(transform);
    }

private:
    float ppu_ = 100.0f;
};
} // namespace arp::scene2d
