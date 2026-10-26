#pragma once

#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace arp::editor {

// Editor popup placement uses the resolved logical canvas, never display/DPI
// guesses. The result describes whole selectable rows inside that rectangle.
class EditorEnumPopupLayout final {
public:
    static constexpr std::uint32_t inspectorMaxRows = 6;

    struct Result {
        ui::LayoutRect rect;
        std::uint32_t rows = 0;
        const char* diagnostic = "EDITOR-ENUM-POPUP-LAYOUT-INVALID";

        bool ok() const noexcept { return rows != 0; }
    };

    static Result prepare(ui::LayoutRect bounds,
        ui::LayoutRect anchor,
        float preferredX,
        float preferredWidth,
        float rowHeight,
        std::size_t optionCount,
        std::uint32_t maxRows) noexcept
    {
        const auto valid = [](ui::LayoutRect rect) {
            return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width)
                && std::isfinite(rect.height) && rect.width > 0 && rect.height > 0
                && std::isfinite(rect.x + rect.width) && std::isfinite(rect.y + rect.height);
        };
        if (!valid(bounds) || !valid(anchor) || !std::isfinite(preferredX)
            || !std::isfinite(preferredWidth) || preferredWidth <= 0 || !std::isfinite(rowHeight)
            || rowHeight <= 0 || !optionCount || !maxRows)
            return {};
        const double bottom = double(bounds.y) + bounds.height;
        const double anchorBottom = double(anchor.y) + anchor.height;
        if (anchor.y < bounds.y || anchorBottom > bottom || anchor.x < bounds.x
            || double(anchor.x) + anchor.width > double(bounds.x) + bounds.width)
            return {};
        const auto desiredRows = std::min(optionCount, std::size_t(maxRows));
        const double desiredHeight = desiredRows * double(rowHeight);
        const double below = bottom - anchorBottom;
        const double above = double(anchor.y) - bounds.y;
        const bool upward = below < desiredHeight && above > below;
        const double available = upward ? above : below;
        const auto rows = static_cast<std::uint32_t>(
            std::min(double(desiredRows), std::floor(available / rowHeight)));
        if (!rows)
            return {{}, 0, "EDITOR-ENUM-POPUP-NO-WHOLE-ROW"};
        const float height = rows * rowHeight;
        const float width = std::min(preferredWidth, bounds.width);
        const float x = std::clamp(preferredX, bounds.x, bounds.x + bounds.width - width);
        const float y = upward ? anchor.y - height : static_cast<float>(anchorBottom);
        const auto rect = ui::LayoutRect::fromOriginSize(x, y, width, height);
        if (!valid(rect) || rect.x < bounds.x || rect.y < bounds.y
            || double(rect.x) + rect.width > double(bounds.x) + bounds.width
            || double(rect.y) + rect.height > bottom)
            return {};
        return {rect, rows, ""};
    }
};
} // namespace arp::editor
