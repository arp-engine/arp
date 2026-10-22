#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "core/hash/sha256.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace arp::assets {
// TriangleSurfaceCoordinates/1: header, little-endian uint32 index count,
// then indexed float32 xyz corners in primitive order. Attribute packing and
// vertex duplication do not affect the digest. Normals/materials bind separately.
inline std::array<std::uint8_t, 32> triangleSurfaceDigest(const float* positions,
    std::size_t positionCount,
    const std::uint32_t* indices,
    std::size_t indexCount)
{
    if (!positions || !indices || !indexCount || indexCount % 3 || indexCount > UINT32_MAX)
        throw core::DiagnosticError("TRIANGLE-SURFACE-DIGEST-INPUT-INVALID");
    core::Sha256 digest;
    digest.update("TriangleSurfaceCoordinates/1");
    const auto append32 = [&](std::uint32_t value) {
        const char bytes[] = {static_cast<char>(value),
            static_cast<char>(value >> 8),
            static_cast<char>(value >> 16),
            static_cast<char>(value >> 24)};
        digest.update({bytes, sizeof(bytes)});
    };
    append32(static_cast<std::uint32_t>(indexCount));
    for (std::size_t i = 0; i < indexCount; ++i) {
        if (indices[i] >= positionCount)
            throw core::DiagnosticError("TRIANGLE-SURFACE-DIGEST-INDEX-INVALID");
        for (unsigned a = 0; a < 3; ++a) {
            const float value = positions[std::size_t(indices[i]) * 3 + a];
            if (!std::isfinite(value))
                throw core::DiagnosticError("TRIANGLE-SURFACE-DIGEST-POSITION-NONFINITE");
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            append32(bits);
        }
    }
    const auto bytes = digest.finalize();
    std::array<std::uint8_t, 32> result;
    std::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}
} // namespace arp::assets
