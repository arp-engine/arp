#pragma once

#include "core/utf8.h"

#include <algorithm>

namespace arp::core {

// Byte offsets, not grapheme clusters. Callers editing Unicode text must keep
// their cursor on scalar boundaries; shaping remains the UI renderer's job.
class Utf8Cursor final {
public:
    struct Scalar {
        std::uint32_t value = 0u;
        std::size_t byteLength = 1u;
        bool valid = false;
    };

    static Scalar at(std::string_view text, std::size_t offset)
    {
        if (offset >= text.size())
            return {};
        const auto first = static_cast<unsigned char>(text[offset]);
        const std::size_t length = first < 0x80u ? 1u
            : (first & 0xe0u) == 0xc0u           ? 2u
            : (first & 0xf0u) == 0xe0u           ? 3u
            : (first & 0xf8u) == 0xf0u           ? 4u
                                                 : 1u;
        Scalar result{0xfffdu, 1u, false};
        if (length > text.size() - offset)
            return result;
        const char* diagnostic =
            Utf8Decoder::visit(text.substr(offset, length), [&](std::uint32_t value) {
                result.value = value;
            });
        if (!diagnostic) {
            result.byteLength = length;
            result.valid = true;
        }
        return result;
    }

    static std::size_t previous(std::string_view text, std::size_t cursor)
    {
        cursor = std::min(cursor, text.size());
        if (cursor == 0u)
            return 0u;
        --cursor;
        while (cursor > 0u && isUtf8ContinuationByte(static_cast<unsigned char>(text[cursor])))
            --cursor;
        return cursor;
    }

    static std::size_t next(std::string_view text, std::size_t cursor)
    {
        if (cursor >= text.size())
            return text.size();
        ++cursor;
        while (cursor < text.size()
            && isUtf8ContinuationByte(static_cast<unsigned char>(text[cursor])))
            ++cursor;
        return cursor;
    }
};

} // namespace arp::core
