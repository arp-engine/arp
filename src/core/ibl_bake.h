#pragma once

// IblBake - reusable CPU image-based-lighting convolution utilities.
//
// Bakes directional environment lighting into the three textures a PBR IBL
// pipeline samples at runtime:
//   - Irradiance cube        : cosine-weighted hemisphere convolution of the
//                              environment radiance -> diffuse ambient.
//   - Prefiltered specular   : per-roughness-mip GGX importance-sampled
//                              convolution -> glossy reflections.
//   - BRDF integration LUT   : 2D (NdotV, roughness) -> (scale, bias) lookup
//                              that completes the split-sum specular term.
//
// This header is RHI-agnostic and allocator-aware: it operates on core::Vec3
// directions and emits packed Rgba16Float (IEEE 754 half) texels, so any game
// or sample can reuse it and upload the result through its own RHI. Per
// AGENTS.md the algorithm lives in core (reusable + composable); the RHI
// upload glue stays in the caller.
//
// The environment is supplied as a RadianceFn (a callable returning the HDR
// linear radiance for a given world-space direction). Callers replicate their
// sky shader's fragment formula on the CPU so the baked IBL matches the
// visible sky - this is the explicit contract between shader and bake, not a
// guessed fallback.
//
// Cube face convention: standard GL/Vulkan TextureCube order, face 0..5 =
// +X, -X, +Y, -Y, +Z, -Z. cubeFaceDirection(face, u, v) maps a [0,1] texel
// coordinate to the world direction a TextureCube sampler resolves it to, so
// the baker and the GPU sampler agree on orientation. This convention is
// asserted by the contract test.

#include "core/cpu_allocator.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>

namespace arp::core::ibl {

// A directional radiance source. Given a normalized world-space direction,
// returns the HDR linear radiance (RGB, may exceed 1.0).
using RadianceFn = std::function<core::Vec3(core::Vec3 dir)>;

// ---------------------------------------------------------------------------
// fp16 (IEEE 754 half) pack/unpack.
//
// Rgba16Float textures store IEEE 754 binary16 half-precision values. core has
// no existing half-float utility, so this is the single canonical home for it.
// ---------------------------------------------------------------------------
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

// Pack a Vec3 + alpha into 4 consecutive half-float (uint16) values.
inline void packRgba16f(core::Vec3 rgb, float a, std::uint16_t out[4])
{
    out[0] = floatToHalf(rgb.x);
    out[1] = floatToHalf(rgb.y);
    out[2] = floatToHalf(rgb.z);
    out[3] = floatToHalf(a);
}

// ---------------------------------------------------------------------------
// Cube face convention.
//
// Standard GL/Vulkan TextureCube face order: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z,
// 5=-Z. For each face, (u,v) in [0,1]^2 maps to a normalized world direction
// using the GL_TEXTURE_CUBE_MAP positive/negative face basis vectors. This is
// the contract a TextureCube sampler resolves texel coordinates against, so a
// radiance sampled at cubeFaceDirection(face, u, v) during bake is what the
// GPU returns when sampling IrradianceCube/PrefilteredSpecularCube in that
// direction at runtime.
// ---------------------------------------------------------------------------
inline core::Vec3 cubeFaceDirection(std::uint32_t face, float u, float v)
{
    // Map [0,1] -> [-1,1] and clip just inside the boundary to avoid degenerate
    // directions at the cube edges (the exact edge value does not matter for
    // convolution because texels are point samples, not edge integrals).
    const float a = u * 2.0f - 1.0f;
    const float b = v * 2.0f - 1.0f;
    switch (face) {
    case 0u:
        return normalize(core::Vec3{1.0f, -b, -a}); // +X
    case 1u:
        return normalize(core::Vec3{-1.0f, -b, a}); // -X
    case 2u:
        return normalize(core::Vec3{a, 1.0f, b}); // +Y
    case 3u:
        return normalize(core::Vec3{a, -1.0f, -b}); // -Y
    case 4u:
        return normalize(core::Vec3{a, -b, 1.0f}); // +Z
    default:
        return normalize(core::Vec3{-a, -b, -1.0f}); // -Z
    }
}

// ---------------------------------------------------------------------------
// Diffuse irradiance cube.
//
// irradiance(n) = (1/pi) * integral over the upper hemisphere of
//                 radiance(omega) * max(n . omega, 0) domega.
//
// Approximated by stratified Monte Carlo over a cosine-weighted hemisphere.
// Output: face-major [6][res*res] Rgba16Float, each texel = 4 half values
// (rgb = irradiance, a = 1.0). Resolution is the per-face edge length.
// ---------------------------------------------------------------------------
inline std::pmr::vector<std::uint16_t> bakeIrradianceCube(
    const RadianceFn& radiance, std::uint32_t resolution, std::pmr::memory_resource* alloc)
{
    const std::uint32_t texelsPerFace = resolution * resolution;
    std::pmr::vector<std::uint16_t> out(alloc);
    out.resize(static_cast<std::size_t>(6u) * texelsPerFace * 4u, 0);

    // Cosine-weighted hemisphere sampling: build N stratified sample
    // directions for each normal. Stratification reduces variance enough that
    // 256 samples gives a smooth result at 16x16.
    constexpr std::uint32_t kSampleCount = 256u;
    const std::uint32_t sqrtSamples = 16u; // 16*16 = 256

    for (std::uint32_t face = 0u; face < 6u; ++face) {
        for (std::uint32_t ty = 0u; ty < resolution; ++ty) {
            for (std::uint32_t tx = 0u; tx < resolution; ++tx) {
                const float u = (static_cast<float>(tx) + 0.5f) / static_cast<float>(resolution);
                const float v = (static_cast<float>(ty) + 0.5f) / static_cast<float>(resolution);
                const core::Vec3 n = cubeFaceDirection(face, u, v);

                // Build an orthonormal basis around n (tangent/bitangent).
                const core::Vec3 up = std::abs(n.y) < 0.999f ? core::Vec3{0.0f, 1.0f, 0.0f}
                                                             : core::Vec3{1.0f, 0.0f, 0.0f};
                const core::Vec3 tangent = normalize(core::cross(up, n));
                const core::Vec3 bitangent = core::cross(n, tangent);

                core::Vec3 sum{0.0f, 0.0f, 0.0f};
                for (std::uint32_t sy = 0u; sy < sqrtSamples; ++sy) {
                    for (std::uint32_t sx = 0u; sx < sqrtSamples; ++sx) {
                        const float ru =
                            (static_cast<float>(sx) + 0.5f) / static_cast<float>(sqrtSamples);
                        const float rv =
                            (static_cast<float>(sy) + 0.5f) / static_cast<float>(sqrtSamples);
                        // Cosine-weighted hemisphere sample in tangent space.
                        // phi = 2*pi*ru, cosTheta = sqrt(rv), sinTheta = sqrt(1-rv).
                        const float phi = 2.0f * 3.14159265358979323846f * ru;
                        const float cosTheta = std::sqrt(rv);
                        const float sinTheta = std::sqrt(1.0f - rv);
                        const float localX = std::cos(phi) * sinTheta;
                        const float localY = sinTheta * std::sin(phi);
                        const float localZ = cosTheta;
                        // Tangent -> world.
                        const core::Vec3 dir = normalize(core::Vec3{
                            tangent.x * localX + bitangent.x * localY + n.x * localZ,
                            tangent.y * localX + bitangent.y * localY + n.y * localZ,
                            tangent.z * localX + bitangent.z * localY + n.z * localZ,
                        });
                        const core::Vec3 r = radiance(dir);
                        // Cosine weight is already folded into the sample
                        // distribution (PDF = cosTheta/pi), so the Monte Carlo
                        // weight is 1/N (sum then divide by N -> divide by pi
                        // via the PDF normalization).
                        sum = sum + r;
                    }
                }
                // Cosine-weighted importance sampling: the sample PDF is
                // cosTheta/pi, and the integrand is L(omega)*cosTheta. The
                // Monte Carlo estimate is (1/N) * sum(L*cosTheta / pdf) =
                // (1/N) * sum(L*cosTheta / (cosTheta/pi)) = (1/N) * sum(L*pi).
                // The irradiance then divides by pi (the Lambert 1/pi factor),
                // which cancels the pi in the estimator, leaving (1/N) * sum(L).
                // So the weight is simply 1/N - the cosine-weighted samples
                // already encode the cosine, and the 1/pi Lambert factor cancels
                // against the pdf's pi.
                const float inv = 1.0f / static_cast<float>(kSampleCount);
                core::Vec3 irradiance = scale(sum, inv);

                std::uint16_t packed[4];
                packRgba16f(irradiance, 1.0f, packed);
                const std::size_t base = (static_cast<std::size_t>(face) * texelsPerFace
                                             + static_cast<std::size_t>(ty) * resolution + tx)
                    * 4u;
                out[base + 0u] = packed[0];
                out[base + 1u] = packed[1];
                out[base + 2u] = packed[2];
                out[base + 3u] = packed[3];
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// GGX helpers used by the prefiltered specular and BRDF LUT bakes.
// ---------------------------------------------------------------------------
inline core::Vec3 importanceSampleGgx(core::Vec3 n, float roughness, float u1, float u2)
{
    // Importance-sample the GGX normal distribution for the split-sum
    // prefilter. The prefilter evaluates the common V=N=R approximation, so
    // sampling the NDF around the reflection direction is the intended
    // distribution here; this is not a view-dependent VNDF sampler.
    const float alpha = roughness * roughness;
    const float a2 = alpha * alpha;

    // Build a basis around n.
    const core::Vec3 up =
        std::abs(n.y) < 0.999f ? core::Vec3{0.0f, 1.0f, 0.0f} : core::Vec3{1.0f, 0.0f, 0.0f};
    const core::Vec3 tangent = normalize(core::cross(up, n));
    const core::Vec3 bitangent = core::cross(n, tangent);

    // Sample the microfacet normal in tangent space.
    const float phi = 2.0f * 3.14159265358979323846f * u1;
    const float cosThetaSq = (1.0f - u2) / (1.0f + (a2 - 1.0f) * u2);
    const float cosTheta = std::sqrt(std::max(0.0f, cosThetaSq));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosThetaSq));
    const float localX = sinTheta * std::cos(phi);
    const float localY = sinTheta * std::sin(phi);
    const float localZ = cosTheta;
    const core::Vec3 h = normalize(core::Vec3{
        tangent.x * localX + bitangent.x * localY + n.x * localZ,
        tangent.y * localX + bitangent.y * localY + n.y * localZ,
        tangent.z * localX + bitangent.z * localY + n.z * localZ,
    });
    return h;
}

inline float geometrySmith(float nDotV, float nDotL, float roughness)
{
    // Smith joint masking-shadowing with the GGX lambda.
    const float a = roughness * roughness;
    const auto g1 = [a](float nDotX) {
        const float a2 = a * a;
        return 2.0f * nDotX / (nDotX + std::sqrt(a2 + (1.0f - a2) * nDotX * nDotX));
    };
    return g1(nDotV) * g1(nDotL);
}

// ---------------------------------------------------------------------------
// Prefiltered specular cube.
//
// For each mip (roughness increases with mip level), convolve the environment
// with the GGX BRDF using importance sampling. The result is the prefiltered
// radiance that, combined with the BRDF LUT at runtime, yields the specular
// IBL term (split-sum approximation, Karis 2013).
//
// Output storage holds all mips and faces contiguously; `mips` gives per-mip
// spans so the caller can upload each mip level separately.
// ---------------------------------------------------------------------------
struct PrefilteredCubeData {
    std::uint32_t baseResolution = 0;
    std::uint32_t mipCount = 0;

    // Each mip is described by a (offset, count) pair into `storage` rather
    // than a std::span, because the project targets C++17 (std::span is C++20).
    // The caller reads storage.data() + offset for count half-float texels.
    struct MipRange {
        std::size_t texelOffset = 0;
        std::size_t texelCount = 0;
    };

    std::pmr::vector<MipRange> mips;         // one range per mip
    std::pmr::vector<std::uint16_t> storage; // backing texels (4 per RGBA texel)
};

// Capture expensive radiance once; convolution queries the bounded CPU cube.
class RadianceCube final {
public:
    RadianceCube(
        const RadianceFn& radiance, std::uint32_t resolution, std::pmr::memory_resource* allocator)
        : resolution_(resolution), pixels_(allocator)
    {
        if (!resolution || resolution > 256u)
            throw std::invalid_argument("IBL-CAPTURE-RESOLUTION-INVALID");
        pixels_.reserve(6u * resolution * resolution);
        for (unsigned face = 0; face < 6; ++face)
            for (unsigned y = 0; y < resolution; ++y)
                for (unsigned x = 0; x < resolution; ++x)
                    pixels_.push_back(radiance(
                        cubeFaceDirection(face, (x + .5f) / resolution, (y + .5f) / resolution)));
    }

    core::Vec3 sample(core::Vec3 d) const noexcept
    {
        const float ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
        unsigned face;
        float u, v, major;
        if (ax >= ay && ax >= az) {
            face = d.x >= 0 ? 0 : 1;
            major = ax;
            u = d.x >= 0 ? -d.z : d.z;
            v = -d.y;
        } else if (ay >= az) {
            face = d.y >= 0 ? 2 : 3;
            major = ay;
            u = d.x;
            v = d.y >= 0 ? d.z : -d.z;
        } else {
            face = d.z >= 0 ? 4 : 5;
            major = az;
            u = d.z >= 0 ? d.x : -d.x;
            v = -d.y;
        }
        const float px = (u / std::max(major, 1e-12f) * .5f + .5f) * resolution_ - .5f;
        const float py = (v / std::max(major, 1e-12f) * .5f + .5f) * resolution_ - .5f;
        const int x = int(std::floor(px)), y = int(std::floor(py));
        const float fx = px - x, fy = py - y;
        core::Vec3 out{};
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const auto ix = std::clamp(x + dx, 0, int(resolution_) - 1),
                           iy = std::clamp(y + dy, 0, int(resolution_) - 1);
                out = out
                    + scale(pixels_[(face * resolution_ + iy) * resolution_ + ix],
                        (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy));
            }
        return out;
    }

private:
    std::uint32_t resolution_;
    core::pmr::Vector<core::Vec3> pixels_;
};

inline PrefilteredCubeData bakePrefilteredSpecularCube(const RadianceFn& radiance,
    std::uint32_t baseResolution,
    std::uint32_t mipCount,
    std::pmr::memory_resource* alloc)
{
    PrefilteredCubeData out;
    out.baseResolution = baseResolution;
    out.mipCount = mipCount;
    out.mips = std::pmr::vector<PrefilteredCubeData::MipRange>(alloc);
    out.storage = std::pmr::vector<std::uint16_t>(alloc);

    constexpr std::uint32_t kSampleCount = 128u;
    const float invPi = 1.0f / 3.14159265358979323846f;

    for (std::uint32_t mip = 0u; mip < mipCount; ++mip) {
        const std::uint32_t res = std::max<std::uint32_t>(baseResolution >> mip, 1u);
        const std::uint32_t texelsPerFace = res * res;
        const std::size_t mipBaseTexel = out.storage.size();
        out.storage.resize(mipBaseTexel + static_cast<std::size_t>(6u) * texelsPerFace * 4u, 0);

        // Roughness mapped linearly across the mip chain (mip 0 = mirror,
        // last mip = fully diffuse-glossy).
        const float roughness =
            (mipCount <= 1u) ? 0.0f : static_cast<float>(mip) / static_cast<float>(mipCount - 1u);

        for (std::uint32_t face = 0u; face < 6u; ++face) {
            for (std::uint32_t ty = 0u; ty < res; ++ty) {
                for (std::uint32_t tx = 0u; tx < res; ++tx) {
                    const float u = (static_cast<float>(tx) + 0.5f) / static_cast<float>(res);
                    const float v = (static_cast<float>(ty) + 0.5f) / static_cast<float>(res);
                    const core::Vec3 n = cubeFaceDirection(face, u, v);
                    // For the prefiltered cube, the sample direction IS the
                    // reflection vector R = n (treat the surface as a perfect
                    // mirror so the lobe is centred on n; roughness spreads it
                    // via the VNDF sample).
                    const core::Vec3 r = n;

                    core::Vec3 sum{0.0f, 0.0f, 0.0f};
                    float weightSum = 0.0f;
                    for (std::uint32_t s = 0u; s < kSampleCount; ++s) {
                        // Low-discrepancy sample indices (Hammersley-ish via
                        // van der Corput + index/total).
                        const float u1 =
                            (static_cast<float>(s) + 0.5f) / static_cast<float>(kSampleCount);
                        // van der Corput radical inverse base 2.
                        std::uint32_t bits = s;
                        bits = (bits << 16u) | (bits >> 16u);
                        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                        const float u2 =
                            static_cast<float>(bits) * 2.3283064365386963e-10f; // 1/2^32

                        const core::Vec3 h = importanceSampleGgx(n, roughness, u1, u2);
                        // Split-sum environment prefiltering uses V=N=R. A
                        // sampled half vector therefore produces a distinct
                        // incoming direction as roughness grows. Constructing
                        // V by reflecting N around H and reflecting it again
                        // collapses every sample back to N.
                        const core::Vec3 view = r;
                        const core::Vec3 sampleDir =
                            normalize(scale(h, 2.0f * dot(view, h)) - view);
                        const float nDotL = std::max(dot(n, sampleDir), 0.0f);
                        if (nDotL <= 0.0f) {
                            continue;
                        }
                        const core::Vec3 ld = radiance(sampleDir);
                        // The GGX NDF is already represented by the importance
                        // sampling distribution. Cosine weighting produces the
                        // standard radiance prefilter used by split-sum IBL.
                        const float w = nDotL;
                        sum = sum + scale(ld, w);
                        weightSum += w;
                    }
                    core::Vec3 prefiltered = (weightSum > 0.0f) ? scale(sum, 1.0f / weightSum)
                                                                : core::Vec3{0.0f, 0.0f, 0.0f};
                    (void)invPi;

                    std::uint16_t packed[4];
                    packRgba16f(prefiltered, 1.0f, packed);
                    const std::size_t base = mipBaseTexel
                        + (static_cast<std::size_t>(face) * texelsPerFace
                              + static_cast<std::size_t>(ty) * res + tx)
                            * 4u;
                    out.storage[base + 0u] = packed[0];
                    out.storage[base + 1u] = packed[1];
                    out.storage[base + 2u] = packed[2];
                    out.storage[base + 3u] = packed[3];
                }
            }
        }
        const std::size_t mipTexelCount = static_cast<std::size_t>(6u) * texelsPerFace * 4u;
        out.mips.push_back(PrefilteredCubeData::MipRange{mipBaseTexel, mipTexelCount});
    }
    return out;
}

// ---------------------------------------------------------------------------
// BRDF integration LUT.
//
// 2D lookup (NdotV, roughness) -> (scale, bias) for the split-sum specular
// term: f_spec = F0 * scale + F90 * bias, where F0 = fresnel at 0 and
// F90 = fresnel at 90. Standard Epic Games integration (Karis 2013).
//
// Output: [res*res] Rgba16Float, row-major (y = roughness, x = NdotV).
// ---------------------------------------------------------------------------
inline std::pmr::vector<std::uint16_t> bakeBrdfIntegrationLut(
    std::uint32_t resolution, std::pmr::memory_resource* alloc)
{
    std::pmr::vector<std::uint16_t> out(alloc);
    out.resize(static_cast<std::size_t>(resolution) * resolution * 4u, 0);

    // Standard Epic Games split-sum BRDF integration (Karis 2013, "Real Shading
    // in UE4"). For each (NdotV, roughness) cell, integrate the BRDF over the
    // GGX distribution with importance sampling; store the F0 coefficient
    // (scale) and the F90 coefficient (bias). At runtime the specular IBL term
    // is reconstructed as F0 * scale + F90 * bias.
    constexpr std::uint32_t kSampleCount = 1024u;

    for (std::uint32_t y = 0u; y < resolution; ++y) {
        const float roughness = static_cast<float>(y) / static_cast<float>(resolution - 1u);
        for (std::uint32_t x = 0u; x < resolution; ++x) {
            const float nDotV0 = (static_cast<float>(x) + 0.5f) / static_cast<float>(resolution);
            // Surface setup: n = +Z, v in the XZ plane.
            const core::Vec3 n{0.0f, 0.0f, 1.0f};
            const float cosTheta = std::min(nDotV0, 0.999f);
            const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
            const core::Vec3 v{sinTheta, 0.0f, cosTheta};

            float f0Scale = 0.0f;
            float f90Bias = 0.0f;
            for (std::uint32_t s = 0u; s < kSampleCount; ++s) {
                // Hammersley low-discrepancy sequence.
                const float u1 = static_cast<float>(s) / static_cast<float>(kSampleCount);
                std::uint32_t bits = s;
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                const float u2 = static_cast<float>(bits) * 2.3283064365386963e-10f;

                // GGX NDF importance sampling (NOT VNDF): sample a
                // microfacet normal m around n.
                const float a = roughness * roughness;
                const float a2 = a * a;
                const float phi = 2.0f * 3.14159265358979323846f * u1;
                const float cosThetaSq = (1.0f - u2) / (1.0f + (a2 - 1.0f) * u2);
                const float cosThetaH = std::sqrt(std::max(0.0f, cosThetaSq));
                const float sinThetaH = std::sqrt(std::max(0.0f, 1.0f - cosThetaSq));
                core::Vec3 h{
                    sinThetaH * std::cos(phi),
                    sinThetaH * std::sin(phi),
                    cosThetaH,
                };
                h = normalize(h);

                const float vDotH = std::max(dot(v, h), 0.0f);
                const float nDotH = std::max(dot(n, h), 0.0f);
                if (vDotH <= 0.0f || nDotH <= 0.0f) {
                    continue;
                }
                const core::Vec3 l = normalize(scale(h, 2.0f * vDotH) - v);
                const float nDotL = std::max(dot(n, l), 0.0f);
                const float nDotV = std::max(dot(n, v), 0.0f);
                if (nDotL <= 0.0f || nDotV <= 0.0f) {
                    continue;
                }

                // Geometry (Smith GGX joint masking-shadowing).
                const float g = geometrySmith(nDotV, nDotL, roughness);
                // With NDF (half-vector) importance sampling, the per-sample
                // Monte Carlo weight for the split-sum BRDF integration is
                // G * vDotH / (nDotV * nDotH). Derivation: BRDF = D*G*F /
                // (4*nDotV*nDotL); the L-direction pdf (derived from the
                // half-vector pdf) = D*nDotH/(4*vDotH); MC weight = BRDF*nDotL
                // / pdf = [D*G*F/(4*nDotV*nDotL)*nDotL] / [D*nDotH/(4*vDotH)]
                // = G*F*vDotH / (nDotV*nDotH).
                const float w = g * vDotH / (nDotV * nDotH);
                // Schlick fresnel: F = F0 + (1 - F0) * (1 - vDotH)^5.
                // Decompose into F0 coefficient (1 - f) and F90 coefficient (f).
                const float f = std::pow(1.0f - vDotH, 5.0f);
                f0Scale += w * (1.0f - f);
                f90Bias += w * f;
            }
            const float scale = f0Scale / static_cast<float>(kSampleCount);
            const float bias = f90Bias / static_cast<float>(kSampleCount);

            std::uint16_t packed[4];
            packRgba16f(core::Vec3{scale, bias, 0.0f}, 1.0f, packed);
            const std::size_t base = (static_cast<std::size_t>(y) * resolution + x) * 4u;
            out[base + 0u] = packed[0];
            out[base + 1u] = packed[1];
            out[base + 2u] = packed[2];
            out[base + 3u] = packed[3];
        }
    }
    return out;
}

} // namespace arp::core::ibl
