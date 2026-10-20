#pragma once

#include "core/base64.h"

#include <algorithm>

namespace arp::core {

inline bool isFlatArtifactFilename(StringView name) noexcept
{
    return !name.empty() && name != "." && name != ".."
        && std::all_of(name.begin(), name.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                   || c == '_' || c == '-' || c == '.';
           });
}

inline bool isRelativeArtifactPath(StringView path, std::size_t maxDepth) noexcept
{
    std::size_t depth = 0u;
    while (!path.empty()) {
        const auto slash = path.find('/');
        if (++depth > maxDepth || !isFlatArtifactFilename(path.substr(0u, slash)))
            return false;
        if (slash == path.npos)
            return true;
        path.remove_prefix(slash + 1u);
    }
    return false;
}

// Artifact metadata records portable '/' or '\\' paths regardless of host.
inline StringView artifactFilename(StringView path) noexcept
{
    const auto at = path.find_last_of("/\\");
    return at == path.npos ? path : path.substr(at + 1u);
}

} // namespace arp::core
