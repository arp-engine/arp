#pragma once

#include <algorithm>
#include <cstddef>

namespace arp::ui {

struct ScrollbarThumb {
    float y = 0.0f;
    float height = 0.0f;
    std::size_t maxOffset = 0u;
    bool visible = false;
};

class ScrollbarGeometry final {
public:
    static ScrollbarThumb thumb(float trackY,
        float trackHeight,
        std::size_t itemCount,
        std::size_t visibleCount,
        std::size_t offset,
        float minThumbHeight)
    {
        ScrollbarThumb result;
        if (visibleCount == 0u || itemCount <= visibleCount || trackHeight <= 0.0f)
            return result;
        result.visible = true;
        result.maxOffset = itemCount - visibleCount;
        result.height = std::min(trackHeight,
            std::max(minThumbHeight,
                trackHeight * static_cast<float>(visibleCount) / static_cast<float>(itemCount)));
        const float fraction = static_cast<float>(std::min(offset, result.maxOffset))
            / static_cast<float>(result.maxOffset);
        result.y = trackY + (trackHeight - result.height) * fraction;
        return result;
    }
};

} // namespace arp::ui
