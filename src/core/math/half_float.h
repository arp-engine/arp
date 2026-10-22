#pragma once

#include <cstdint>
#include <cstring>

namespace arp::core {

// IEEE 754 binary16 conversion, independent of image formats and lighting.
enum class HalfFloatRounding { NearestEven, TowardZero };

inline std::uint16_t floatToHalf(float v, HalfFloatRounding rounding)
{
    // Bit-cast to uint32, then fold a float32 into a binary16 per the IEEE 754
    // specification. Handles normals, subnormals, zero, inf and nan. The
    // requested mode is explicit; native image writes need not use nearest-even.
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(float));
    const std::uint32_t sign = (bits >> 16u) & 0x8000u;
    std::uint32_t exponent = (bits >> 23u) & 0xffu;
    std::uint32_t mantissa = bits & 0x7fffffu;

    if (exponent == 255u) {
        // inf or nan -> half inf/nan (preserve quiet/signal bit in mantissa).
        return static_cast<std::uint16_t>(sign | 0x7c00u | (mantissa ? 1u : 0u));
    }

    if (exponent == 0u) {
        // float32 zero / subnormal: too small for half subnormal -> signed zero.
        return static_cast<std::uint16_t>(sign);
    }

    // Re-bias exponent from float32 (bias 127) to half (bias 15).
    std::int32_t halfExponent = static_cast<std::int32_t>(exponent) - 127 + 15;

    if (halfExponent >= 31) {
        return static_cast<std::uint16_t>(
            sign | (rounding == HalfFloatRounding::TowardZero ? 0x7bffu : 0x7c00u));
    }

    if (halfExponent <= 0) {
        // Subnormal half: shift mantissa right to fit the 10-bit half mantissa
        // with the implicit leading 1 made explicit.
        mantissa |= 0x800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - halfExponent);
        if (shift > 24u) {
            return static_cast<std::uint16_t>(sign); // underflow to zero
        }
        std::uint32_t rounded = mantissa >> shift;
        const std::uint32_t rem = mantissa & ((1u << shift) - 1u);
        const std::uint32_t halfWay = 1u << (shift - 1u);
        if (rounding == HalfFloatRounding::NearestEven
            && (rem > halfWay || (rem == halfWay && (rounded & 1u)))) {
            rounded += 1u; // round to nearest even
        }
        // Rounding the largest subnormal can carry into the first normal.
        return static_cast<std::uint16_t>(sign | rounded);
    }

    // Normal half: round mantissa 23->10 bits, nearest-even.
    std::uint32_t halfMantissa = mantissa >> 13u;
    const std::uint32_t rem = mantissa & 0x1fffu;
    const std::uint32_t halfWay = 0x1000u;
    if (rounding == HalfFloatRounding::NearestEven
        && (rem > halfWay || (rem == halfWay && (halfMantissa & 1u)))) {
        halfMantissa += 1u;
        if (halfMantissa == 0x400u) {
            halfMantissa = 0u;
            halfExponent += 1;
            if (halfExponent >= 31) {
                return static_cast<std::uint16_t>(sign | 0x7c00u);
            }
        }
    }
    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint32_t>(halfExponent) << 10u) | halfMantissa);
}

inline std::uint16_t floatToHalf(float v)
{
    return floatToHalf(v, HalfFloatRounding::NearestEven);
}

inline float halfToFloat(std::uint16_t h)
{
    std::uint32_t sign = (static_cast<std::uint32_t>(h) & 0x8000u) << 16u;
    std::uint32_t exponent = (static_cast<std::uint32_t>(h) >> 10u) & 0x1fu;
    std::uint32_t mantissa = static_cast<std::uint32_t>(h) & 0x3ffu;

    if (exponent == 0u) {
        if (mantissa == 0u) {
            const std::uint32_t bits = sign;
            float out;
            std::memcpy(&out, &bits, sizeof(float));
            return out;
        }
        // Subnormal half -> normal float.
        while ((mantissa & 0x400u) == 0u) {
            mantissa <<= 1u;
            exponent -= 1u;
        }
        exponent += 1u;
        mantissa &= 0x3ffu;
    } else if (exponent == 31u) {
        const std::uint32_t bits = sign | 0x7f800000u | (mantissa << 13u);
        float out;
        std::memcpy(&out, &bits, sizeof(float));
        return out;
    }

    const std::uint32_t floatExponent = exponent + (127u - 15u);
    const std::uint32_t bits = sign | (floatExponent << 23u) | (mantissa << 13u);
    float out;
    std::memcpy(&out, &bits, sizeof(float));
    return out;
}

} // namespace arp::core
