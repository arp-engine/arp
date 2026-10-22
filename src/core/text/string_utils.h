#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace arp::core {

// ---------------------------------------------------------------------------
// Non-allocating helpers (operate on std::string_view)
// ---------------------------------------------------------------------------

inline char toLowerAscii(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

inline bool asciiEqualsIgnoreCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (toLowerAscii(left[index]) != toLowerAscii(right[index])) {
            return false;
        }
    }
    return true;
}

inline bool isAsciiWhitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f'
        || value == '\v';
}

inline std::string_view trimAsciiOws(std::string_view text) noexcept
{
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

inline std::string_view trimAsciiWhitespace(std::string_view text) noexcept
{
    std::size_t begin = 0;
    while (begin < text.size() && isAsciiWhitespace(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && isAsciiWhitespace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

inline bool containsCommaSeparatedAsciiToken(std::string_view text, std::string_view token) noexcept
{
    if (token.empty()) {
        return false;
    }
    while (!text.empty()) {
        const auto comma = text.find(',');
        const auto part = trimAsciiOws(text.substr(0, comma));
        if (asciiEqualsIgnoreCase(part, token)) {
            return true;
        }
        if (comma == std::string_view::npos) {
            return false;
        }
        text.remove_prefix(comma + 1u);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Non-allocating predicate helpers (operate on std::string_view)
// ---------------------------------------------------------------------------

inline bool contains(std::string_view haystack, std::string_view needle) noexcept
{
    return haystack.find(needle) != std::string_view::npos;
}

inline bool startsWith(std::string_view str, std::string_view prefix) noexcept
{
    if (str.size() < prefix.size()) {
        return false;
    }
    return str.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(std::string_view str, std::string_view suffix) noexcept
{
    if (str.size() < suffix.size()) {
        return false;
    }
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline int indexOf(std::string_view haystack, std::string_view needle) noexcept
{
    auto pos = haystack.find(needle);
    return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
}

// ---------------------------------------------------------------------------
// Allocating helpers (operate on pmr::String / pmr::Vector)
// These are reused by ArpScript reflection (str methods) and any other
// caller needing PMR-allocated string manipulation.
// ---------------------------------------------------------------------------

// Returns a PMR vector of PMR strings, splitting `text` at each occurrence of
// `sep`. An empty `sep` is treated as a single separator on every character.
// Leading/trailing separators yield empty parts (Python semantics).
inline pmr::Vector<pmr::String> splitPmr(
    std::string_view text, std::string_view sep, std::pmr::memory_resource* res)
{
    pmr::Vector<pmr::String> parts{res};
    std::pmr::polymorphic_allocator<char> alloc{res};
    if (sep.empty()) {
        // Split per character
        for (char c : text) {
            pmr::String part(alloc);
            part.push_back(c);
            parts.push_back(std::move(part));
        }
        return parts;
    }
    if (text.empty()) {
        parts.push_back(pmr::String(alloc));
        return parts;
    }
    std::size_t start = 0;
    for (;;) {
        auto pos = text.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.push_back(pmr::String(text.substr(start), alloc));
            break;
        }
        parts.push_back(pmr::String(text.substr(start, pos - start), alloc));
        start = pos + sep.size();
    }
    return parts;
}

inline pmr::String concatPmr(
    std::initializer_list<std::string_view> parts, std::pmr::memory_resource* res)
{
    std::size_t total = 0u;
    for (const auto part : parts)
        total += part.size();
    pmr::String result(res);
    result.reserve(total);
    for (const auto part : parts)
        result.append(part.data(), part.size());
    return result;
}

// Joins `parts` into a single PMR string using `sep` between elements.
inline pmr::String joinPmr(
    const pmr::Vector<pmr::String>& parts, std::string_view sep, std::pmr::memory_resource* res)
{
    std::pmr::polymorphic_allocator<char> alloc{res};
    if (parts.empty()) {
        return pmr::String(alloc);
    }
    std::size_t total = sep.size() * (parts.size() - 1);
    for (const auto& p : parts) {
        total += p.size();
    }
    pmr::String result(alloc);
    result.reserve(total);
    result.append(parts[0]);
    for (std::size_t i = 1; i < parts.size(); ++i) {
        result.append(sep);
        result.append(parts[i]);
    }
    return result;
}

// Returns a copy of `text` with all occurrences of `from` replaced by `to`.
inline pmr::String replaceAllPmr(std::string_view text,
    std::string_view from,
    std::string_view to,
    std::pmr::memory_resource* res)
{
    std::pmr::polymorphic_allocator<char> alloc{res};
    pmr::String result(alloc);
    if (from.empty()) {
        result.append(text);
        return result;
    }
    std::size_t start = 0;
    for (;;) {
        auto pos = text.find(from, start);
        if (pos == std::string_view::npos) {
            result.append(text.substr(start));
            break;
        }
        result.append(text.substr(start, pos - start));
        result.append(to);
        start = pos + from.size();
    }
    return result;
}

// Returns a copy of `text` with leading/trailing whitespace removed.
inline pmr::String stripPmr(std::string_view text, std::pmr::memory_resource* res)
{
    std::pmr::polymorphic_allocator<char> alloc{res};
    auto trimmed = trimAsciiWhitespace(text);
    return pmr::String(trimmed, alloc);
}

// Returns a copy of `text` with all ASCII uppercase letters converted to
// lowercase.
inline pmr::String toUpperPmr(std::string_view text, std::pmr::memory_resource* res)
{
    std::pmr::polymorphic_allocator<char> alloc{res};
    pmr::String result(text, alloc);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return result;
}

// Returns a copy of `text` with all ASCII lowercase letters converted to
// uppercase.
inline pmr::String toLowerPmr(std::string_view text, std::pmr::memory_resource* res)
{
    std::pmr::polymorphic_allocator<char> alloc{res};
    pmr::String result(text, alloc);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

} // namespace arp::core
