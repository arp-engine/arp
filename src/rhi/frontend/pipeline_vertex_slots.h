#pragma once
#include "rhi/gpu_pipeline.h"

namespace arp::rhi {
// Positional native layouts preserve the exact L2 stream slots, including gaps.
class VertexStreamSlotLayout final {
public:
    static const char* count(const core::pmr::Vector<VertexBufferLayoutDesc>& source,
        std::uint32_t maxSlots,
        std::uint32_t& count) noexcept
    {
        count = 0;
        for (std::size_t i = 0; i < source.size(); ++i) {
            const auto slot = source[i].slot;
            if (slot >= maxSlots)
                return "RHI-VERTEX-INPUT-SLOT-LIMIT";
            for (std::size_t j = 0; j < i; ++j)
                if (source[j].slot == slot)
                    return "RHI-VERTEX-INPUT-SLOT-DUPLICATE";
            count = std::max(count, slot + 1);
        }
        return nullptr;
    }
};
} // namespace arp::rhi
