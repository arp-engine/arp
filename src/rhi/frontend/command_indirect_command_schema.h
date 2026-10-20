#pragma once
#include "core/unsigned_element_span.h"
#include "rhi/frontend/command_indirect_commands.h"

#include <array>

namespace arp::rhi {
// Native-independent indexed-record format. All consumers share these exact
// field offsets and signed two's-complement decoding, including unaligned input.
class IndexedIndirectCommandSchema final {
public:
    static constexpr std::uint32_t version = 1u;
    static constexpr std::size_t byteCount = sizeof(DrawIndexedIndirectCommand);
    static constexpr std::size_t wordCount = byteCount / sizeof(std::uint32_t);
    using Words = std::array<std::uint32_t, wordCount>;
    using Order = core::UnsignedElementSpan::ByteOrder;

    static Words words(const DrawIndexedIndirectCommand& draw) noexcept
    {
        return {draw.indexCount,
            draw.instanceCount,
            draw.firstIndex,
            static_cast<std::uint32_t>(draw.vertexOffset),
            draw.firstInstance};
    }

    static const char* decode(const void* source,
        std::size_t bytes,
        Order order,
        DrawIndexedIndirectCommand& output) noexcept
    {
        output = {};
        if (bytes != byteCount)
            return "RHI-INDIRECT-ARGUMENT-SCHEMA";
        const core::UnsignedElementSpan input(
            source, bytes, core::UnsignedElementSpan::Width::Uint32, order);
        if (const auto* id = input.diagnostic())
            return id;
        Words values{};
        for (std::size_t i = 0u; i < values.size(); ++i)
            if (!input.read(i, values[i]))
                return "RHI-INDIRECT-ARGUMENT-SCHEMA";
        const auto signedValue =
            std::int64_t{values[3]} - (values[3] > INT32_MAX ? (std::int64_t{1} << 32u) : 0);
        output = {
            values[0], values[1], values[2], static_cast<std::int32_t>(signedValue), values[4]};
        return nullptr;
    }

    static bool encodeLittleEndian(
        const Words& values, void* destination, std::size_t bytes) noexcept
    {
        if (!destination || bytes != byteCount)
            return false;
        for (std::size_t i = 0u; i < values.size(); ++i)
            if (!core::UnsignedElementSpan::writeLittleEndian(values[i],
                    core::UnsignedElementSpan::Width::Uint32,
                    static_cast<std::uint8_t*>(destination) + i * sizeof(std::uint32_t),
                    sizeof(std::uint32_t)))
                return false;
        return true;
    }
};
} // namespace arp::rhi
