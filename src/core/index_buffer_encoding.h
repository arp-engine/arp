#pragma once

#include "core/array_view.h"
#include "core/cpu_allocator.h"

#include <algorithm>
#include <cstring>

namespace arp::core {
enum class IndexElementWidth : std::uint8_t { Uint16 = 2, Uint32 = 4 };

// Cold import/cook conversion only. Index order and values are exact; no
// partitioning, truncation or implicit change of element width is permitted.
class IndexBufferEncoding final {
public:
    static const char* encode(ArrayView<std::uint32_t> indices,
        std::uint64_t vertexCount,
        IndexElementWidth width,
        std::uint64_t byteBudget,
        pmr::Vector<std::uint8_t>& output)
    {
        output.clear();
        if (indices.empty() || !vertexCount
            || (width != IndexElementWidth::Uint16 && width != IndexElementWidth::Uint32))
            return "CORE-INDEX-ENCODING-CONFIG";
        const auto elementBytes = static_cast<std::uint8_t>(width);
        if (indices.size() > byteBudget / elementBytes || indices.size() > SIZE_MAX / elementBytes)
            return "CORE-INDEX-ENCODING-BUDGET";
        for (const auto index : indices) {
            if (index >= vertexCount)
                return "CORE-INDEX-ENCODING-VERTEX-RANGE";
            if (width == IndexElementWidth::Uint16 && index > UINT16_MAX)
                return "CORE-INDEX-ENCODING-UINT16-RANGE";
        }
        try {
            output.resize(indices.size() * elementBytes);
            if (width == IndexElementWidth::Uint32)
                std::memcpy(output.data(), indices.data(), output.size());
            else
                for (std::size_t i = 0; i < indices.size(); ++i) {
                    const auto encoded = static_cast<std::uint16_t>(indices[i]);
                    std::memcpy(output.data() + i * elementBytes, &encoded, elementBytes);
                }
            return nullptr;
        } catch (const std::bad_alloc&) {
            output.clear();
            return "CORE-INDEX-ENCODING-ALLOCATION";
        }
    }
};
} // namespace arp::core
