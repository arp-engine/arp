#pragma once

#include "renderer/gui/gui_draw_plan.h"

#include <cmath>

namespace arp::renderer {

// Unified UI / 2 quantizes only the declared clip, never the layout geometry.
class GuiOrderedUnifiedContract final {
public:
    static const char* clip(const arp::ui::LayoutRect& rect,
        const rhi::Extent2D& extent,
        rhi::ScissorState& output) noexcept
    {
        if (!rect.valid() || !std::isfinite(rect.x) || !std::isfinite(rect.y)
            || !std::isfinite(rect.width) || !std::isfinite(rect.height) || rect.x < 0 || rect.y < 0
            || rect.width <= 0 || rect.height <= 0 || double(rect.x) + rect.width > extent.width
            || double(rect.y) + rect.height > extent.height || extent.width > INT32_MAX
            || extent.height > INT32_MAX)
            return "GUI-ORDERED-CLIP-RANGE";
        const auto left = static_cast<std::int32_t>(std::floor(double(rect.x)));
        const auto top = static_cast<std::int32_t>(std::floor(double(rect.y)));
        const auto right = static_cast<std::uint32_t>(std::ceil(double(rect.x) + rect.width));
        const auto bottom = static_cast<std::uint32_t>(std::ceil(double(rect.y) + rect.height));
        output = {left, top, right - std::uint32_t(left), bottom - std::uint32_t(top)};
        return nullptr;
    }
};
} // namespace arp::renderer
