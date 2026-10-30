#pragma once

#include "core/memory/cpu_allocator.h"

#include <filesystem>
#include <string_view>

namespace arp::core {

class FilePath final {
public:
    // Lexical host-root admission only. Native IO verifies the actual object.
    static bool isAbsoluteHostPath(std::string_view value) noexcept
    {
#if defined(_WIN32)
        const auto separator = [](char c) {
            return c == '/' || c == '\\';
        };
        const bool drive = value.size() >= 3
            && ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z'))
            && value[1] == ':' && separator(value[2]);
        const bool unc =
            value.size() > 2 && separator(value[0]) && separator(value[1]) && !separator(value[2]);
        return drive || unc;
#else
        return !value.empty() && value.front() == '/';
#endif
    }

    static bool isSafeRelativePath(std::string_view value, std::size_t maxBytes = 512u)
    {
        if (value.empty() || value.size() > maxBytes || value.front() == '/' || value.back() == '/'
            || value.find('\\') != std::string_view::npos
            || value.find(':') != std::string_view::npos)
            return false;
        std::size_t segmentStart = 0;
        while (segmentStart < value.size()) {
            const auto separator = value.find('/', segmentStart);
            const auto segmentEnd = separator == std::string_view::npos ? value.size() : separator;
            const auto segmentLength = segmentEnd - segmentStart;
            if (segmentLength == 0u || (segmentLength == 1u && value[segmentStart] == '.')
                || (segmentLength == 2u && value[segmentStart] == '.'
                    && value[segmentStart + 1u] == '.'))
                return false;
            if (separator == std::string_view::npos)
                break;
            segmentStart = separator + 1u;
        }
        return true;
    }

    // Lexical containment only. Resolve symlinks/canonicalize both paths at the
    // I/O boundary before using this predicate as a project-root gate.
    static bool contains(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        const auto relative = path.lexically_relative(root);
        if (relative.empty())
            return path == root;
        return *relative.begin() != ".." && !relative.is_absolute();
    }

    static pmr::String utf8(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return pmr::String(
            reinterpret_cast<const char*>(value.data()), value.size(), &defaultCpuMemoryResource());
    }
};

} // namespace arp::core
