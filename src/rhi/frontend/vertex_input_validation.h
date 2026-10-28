#pragma once

#include "rhi/gpu_pipeline.h"

#include <algorithm>

namespace arp::rhi {
struct VertexInputStreamInspection {
    std::uint32_t slot, strideBytes;
    VertexInputRate rate;
    std::size_t firstAttribute, attributeCount;
    std::uint64_t fetchBytes;
};

class VertexInputValidation final {
public:
    static constexpr std::uint64_t formatBytes(VertexFormat format) noexcept
    {
        constexpr auto scalarBytes = 32u / 8u;
        switch (format) {
        case VertexFormat::Float32x1:
            return scalarBytes;
        case VertexFormat::Float32x2:
            return 2u * scalarBytes;
        case VertexFormat::Float32x3:
            return 3u * scalarBytes;
        case VertexFormat::Float32x4:
            return 4u * scalarBytes;
        }
        return 0u;
    }

    template <class Stream>
    static const char* inspectStream(const VertexBufferLayoutDesc& input, Stream& output) noexcept
    {
        if (!input.strideBytes || input.attributes.empty())
            return "RHI-VERTEX-INPUT-LAYOUT-INVALID";
        if (input.inputRate != VertexInputRate::Vertex
            && input.inputRate != VertexInputRate::Instance)
            return "RHI-VERTEX-INPUT-RATE-UNKNOWN";
        std::uint64_t footprint = 0u;
        for (const auto& attribute : input.attributes) {
            const auto bytes = formatBytes(attribute.format);
            if (!bytes)
                return "RHI-VERTEX-INPUT-FORMAT-UNKNOWN";
            footprint = std::max(footprint, std::uint64_t{attribute.offsetBytes} + bytes);
        }
        output = {
            input.slot, input.strideBytes, input.inputRate, 0u, input.attributes.size(), footprint};
        return nullptr;
    }
};
} // namespace arp::rhi
