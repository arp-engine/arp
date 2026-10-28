#pragma once

#include "core/diagnostics/diagnostic_error.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace arp::renderer::detail {
// Private positive outward arithmetic for renderer surface error proofs. The returned
// value is bit-identical to nextafter(x,+infinity), without the library call.
// This contract does not expose floating exception flags.
struct ProjectionRounding final {
    static double upward(double value)
    {
        if constexpr (sizeof(double) != sizeof(std::uint64_t)
            || !std::numeric_limits<double>::is_iec559
            || std::numeric_limits<double>::digits != 53) {
            throw core::DiagnosticError("ORTHO-ERROR-BINARY64-REQUIRED");
        } else {
            if (!std::isfinite(value) || value < 0)
                throw core::DiagnosticError("ORTHO-ERROR-ARITHMETIC-OVERFLOW");
            std::uint64_t bits = 0;
            // Both signs of zero advance to the smallest positive subnormal.
            if (value != 0)
                std::memcpy(&bits, &value, sizeof(bits));
            ++bits;
            std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value))
                throw core::DiagnosticError("ORTHO-ERROR-ARITHMETIC-OVERFLOW");
            return value;
        }
    }
};
} // namespace arp::renderer::detail
