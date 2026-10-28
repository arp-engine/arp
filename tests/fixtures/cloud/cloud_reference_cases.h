#pragma once

#include "core/math/math.h"

#include <array>
#include <string_view>

namespace arp::tests::cloud {

// P0/P3 fixed reference inputs, not a production quality or performance preset.
// All cases use the existing full-resolution, no-history reference algorithm.
struct ReferenceCase final {
    std::string_view name;
    float altitude;
    float viewElevation;
    float coverage;
    core::Vec3 sun;
    bool foreground = false;
    bool orthographic = false;
    float extinction = .002f;
};

class ReferenceCases final {
public:
    static constexpr unsigned width = 32u, height = 24u;
    static constexpr unsigned viewSteps = 256u, lightSteps = 32u;
    static constexpr float tolerance = .002f;
    static constexpr std::array<ReferenceCase, 10u> values{{
        {"clear", 10.f, .6f, 0.f, {1.f, 1.f, -.2f}},
        {"sparse", 10.f, .6f, .65f, {1.f, 1.f, -.2f}},
        {"thick", 10.f, .6f, 1.f, {1.f, 1.f, -.2f}, false, false, .006f},
        {"below", 10.f, .6f, 1.f, {1.f, 1.f, -.2f}},
        {"inside", 1600.f, .6f, 1.f, {1.f, 1.f, -.2f}},
        {"above", 4000.f, -.6f, 1.f, {1.f, 1.f, -.2f}},
        {"horizon", 10.f, 0.f, 1.f, {1.f, 1.f, -.2f}},
        {"backlit", 10.f, .6f, 1.f, {.25f, .6f, 1.f}},
        {"occlusion", 10.f, .6f, 1.f, {1.f, 1.f, -.2f}, true},
        {"orthographic", 1600.f, .6f, 1.f, {1.f, 1.f, -.2f}, true, true},
    }};

    static constexpr const ReferenceCase* find(std::string_view name) noexcept
    {
        for (const auto& value : values)
            if (value.name == name)
                return &value;
        return nullptr;
    }
};

} // namespace arp::tests::cloud
