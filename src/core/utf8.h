#pragma once

#include "core/cpu_allocator.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace arp::core {

struct Utf8DecodeResult {
    bool success = false;
    arp::core::pmr::Vector<std::uint32_t> codepoints{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

inline bool isUtf8ContinuationByte(unsigned char value) noexcept
{
    return (value & 0xC0u) == 0x80u;
}

// Unicode scalar UTF-8 decoder shared by allocating decode and bounded hot
// validation. Visitor receives only canonical scalar values; diagnostics are
// stable static text and no string/container is allocated by this algorithm.
class Utf8Decoder final {
public:
    template <class Emit>
    static const char* visit(std::string_view textValue, Emit&& emit) noexcept(
        std::is_nothrow_invocable_v<Emit&, std::uint32_t>)
    {
        for (std::size_t index = 0; index < textValue.size();) {
            const auto lead = static_cast<unsigned char>(textValue[index]);
            std::uint32_t codepoint = 0u;
            std::size_t sequenceLength = 0u;

            if ((lead & 0x80u) == 0u) {
                codepoint = lead;
                sequenceLength = 1u;
            } else if ((lead & 0xE0u) == 0xC0u) {
                codepoint = lead & 0x1Fu;
                sequenceLength = 2u;
            } else if ((lead & 0xF0u) == 0xE0u) {
                codepoint = lead & 0x0Fu;
                sequenceLength = 3u;
            } else if ((lead & 0xF8u) == 0xF0u) {
                codepoint = lead & 0x07u;
                sequenceLength = 4u;
            } else {
                return "Invalid UTF-8 text.";
            }

            if (sequenceLength > textValue.size() - index) {
                return "Truncated UTF-8 text.";
            }

            for (std::size_t offset = 1; offset < sequenceLength; ++offset) {
                const auto continuation = static_cast<unsigned char>(textValue[index + offset]);
                if (!isUtf8ContinuationByte(continuation)) {
                    return "Invalid UTF-8 continuation byte.";
                }
                codepoint = (codepoint << 6u) | (continuation & 0x3Fu);
            }

            if ((sequenceLength == 2u && codepoint < 0x80u)
                || (sequenceLength == 3u && codepoint < 0x800u)
                || (sequenceLength == 4u && codepoint < 0x10000u) || codepoint > 0x10FFFFu
                || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
                return "Non-canonical UTF-8 text.";
            }

            emit(codepoint);
            index += sequenceLength;
        }

        return nullptr;
    }

    static const char* validate(std::string_view text) noexcept
    {
        return visit(text, [](std::uint32_t) noexcept {});
    }
};

inline Utf8DecodeResult decodeUtf8(const arp::core::pmr::String& textValue)
{
    Utf8DecodeResult result;
    const auto* diagnostic = Utf8Decoder::visit(
        std::string_view(textValue.data(), textValue.size()), [&](std::uint32_t codepoint) {
            result.codepoints.push_back(codepoint);
        });
    if (diagnostic)
        result.diagnostics = diagnostic;
    else
        result.success = true;
    return result;
}

inline bool appendUtf8Codepoint(
    arp::core::pmr::String& target, std::uint32_t codepoint, arp::core::pmr::String& diagnostics)
{
    auto text = [](const char* value) {
        return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
    };

    if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) {
        diagnostics = text("Invalid Unicode scalar value.");
        return false;
    }
    if (codepoint <= 0x7Fu) {
        target.push_back(static_cast<char>(codepoint));
        return true;
    }
    if (codepoint <= 0x7FFu) {
        target.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
        target.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        return true;
    }
    if (codepoint <= 0xFFFFu) {
        target.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
        target.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        target.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        return true;
    }
    if (codepoint <= 0x10FFFFu) {
        target.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
        target.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
        target.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        target.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        return true;
    }

    diagnostics = text("Invalid Unicode codepoint.");
    return false;
}

} // namespace arp::core
