#pragma once

#include "core/cpu_allocator.h"

#include <filesystem>
#include <string_view>

namespace arp::core {

class FilePath final {
public:
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
