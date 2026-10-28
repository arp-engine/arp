#pragma once

// Simplex noise — a gradient noise free of the directional artifacts that
// plague value-noise (hash2).  The simplex grid (equilateral triangles in 2D,
// tetrahedra in 3D) produces isotropic, organic-looking output.
//
// This is a self-contained header-only implementation.  The permutation and
// gradient tables are the canonical ones from the reference implementation;
// the algorithm matches the standard 2D/3D simplex formulation.
//
// FBM (fractional Brownian motion) stacks multiple octaves of simplex noise
// at decreasing amplitude and increasing frequency for natural-looking detail.
//
// All functions are deterministic given a seed — same seed, same output.

#include <array>
#include <cmath>
#include <cstdint>

namespace arp::pcg {

namespace simplex_detail {

// Permutation table (shuffled 0..255, canonical simplex reference).
inline constexpr std::array<std::uint8_t, 512> perm = [] {
    std::array<std::uint8_t, 512> p{};
    constexpr std::uint8_t base[256] = {
        151,
        160,
        137,
        91,
        90,
        15,
        131,
        13,
        201,
        95,
        96,
        53,
        194,
        233,
        7,
        225,
        140,
        36,
        103,
        30,
        69,
        142,
        8,
        99,
        37,
        240,
        21,
        10,
        23,
        190,
        6,
        148,
        247,
        120,
        234,
        75,
        0,
        26,
        197,
        62,
        94,
        252,
        219,
        203,
        117,
        35,
        11,
        32,
        57,
        177,
        33,
        88,
        237,
        149,
        56,
        87,
        174,
        20,
        125,
        136,
        171,
        168,
        68,
        175,
        74,
        165,
        71,
        134,
        139,
        48,
        27,
        166,
        77,
        146,
        158,
        231,
        83,
        111,
        229,
        122,
        60,
        211,
        133,
        230,
        220,
        105,
        92,
        41,
        55,
        46,
        245,
        40,
        244,
        102,
        143,
        54,
        65,
        25,
        63,
        161,
        1,
        216,
        80,
        73,
        209,
        76,
        132,
        187,
        208,
        89,
        18,
        169,
        200,
        196,
        135,
        130,
        116,
        188,
        159,
        86,
        164,
        100,
        109,
        198,
        173,
        186,
        3,
        64,
        52,
        217,
        226,
        250,
        124,
        123,
        5,
        202,
        38,
        147,
        118,
        126,
        255,
        82,
        85,
        212,
        207,
        206,
        59,
        227,
        47,
        16,
        58,
        17,
        182,
        189,
        28,
        42,
        223,
        183,
        170,
        213,
        119,
        248,
        152,
        2,
        44,
        154,
        163,
        70,
        221,
        153,
        101,
        155,
        167,
        43,
        172,
        9,
        129,
        22,
        39,
        253,
        19,
        98,
        108,
        110,
        79,
        113,
        224,
        232,
        178,
        185,
        112,
        104,
        218,
        246,
        97,
        228,
        251,
        34,
        242,
        193,
        238,
        210,
        144,
        12,
        191,
        179,
        162,
        241,
        81,
        51,
        145,
        235,
        249,
        14,
        239,
        107,
        49,
        192,
        214,
        31,
        181,
        199,
        106,
        157,
        184,
        84,
        204,
        176,
        115,
        121,
        50,
        45,
        127,
        4,
        150,
        254,
        138,
        236,
        205,
        93,
        222,
        114,
        67,
        29,
        24,
        72,
        243,
        141,
        128,
        195,
        78,
        66,
        215,
        61,
        156,
        180,
    };
    for (int i = 0; i < 256; ++i)
        p[i] = p[i + 256] = base[i];
    return p;
}();

// 2D gradient vectors (normalised).
inline constexpr std::array<std::array<float, 2>, 12> grad2 = {{
    {1.0f, 1.0f},
    {-1.0f, 1.0f},
    {1.0f, -1.0f},
    {-1.0f, -1.0f},
    {1.0f, 0.0f},
    {-1.0f, 0.0f},
    {0.0f, 1.0f},
    {0.0f, -1.0f},
    {0.707106769f, 0.707106769f},
    {-0.707106769f, 0.707106769f},
    {0.707106769f, -0.707106769f},
    {-0.707106769f, -0.707106769f},
}};

// 3D gradient vectors.
inline constexpr std::array<std::array<float, 3>, 12> grad3 = {{
    {1.0f, 1.0f, 0.0f},
    {-1.0f, 1.0f, 0.0f},
    {1.0f, -1.0f, 0.0f},
    {-1.0f, -1.0f, 0.0f},
    {1.0f, 0.0f, 1.0f},
    {-1.0f, 0.0f, 1.0f},
    {1.0f, 0.0f, -1.0f},
    {-1.0f, 0.0f, -1.0f},
    {0.0f, 1.0f, 1.0f},
    {0.0f, -1.0f, 1.0f},
    {0.0f, 1.0f, -1.0f},
    {0.0f, -1.0f, -1.0f},
}};

inline float dot2(const std::array<float, 2>& g, float x, float y) noexcept
{
    return g[0] * x + g[1] * y;
}

inline float dot3(const std::array<float, 3>& g, float x, float y, float z) noexcept
{
    return g[0] * x + g[1] * y + g[2] * z;
}

} // namespace simplex_detail

/// 2D Simplex noise → [0, 1] range (centered at ~0.5).
/// `x`, `z` in any units; `seed` deterministically shuffles the permutation.
inline float simplex2D(float x, float z, std::uint32_t seed = 0u) noexcept
{
    using namespace simplex_detail;

    // Skew into simplex space.
    constexpr float F2 = 0.366025403784439f; // (sqrt(3)-1)/2
    constexpr float G2 = 0.211324865405187f; // (3-sqrt(3))/6

    float s = (x + z) * F2;
    int i = static_cast<int>(std::floor(x + s));
    int j = static_cast<int>(std::floor(z + s));

    float t = static_cast<float>(i + j) * G2;
    float X0 = static_cast<float>(i) - t;
    float Z0 = static_cast<float>(j) - t;
    float x0 = x - X0;
    float z0 = z - Z0;

    // Determine which simplex we're in.
    int i1, j1;
    if (x0 > z0) {
        i1 = 1;
        j1 = 0;
    } else {
        i1 = 0;
        j1 = 1;
    }

    float x1 = x0 - static_cast<float>(i1) + G2;
    float z1 = z0 - static_cast<float>(j1) + G2;
    float x2 = x0 - 1.0f + 2.0f * G2;
    float z2 = z0 - 1.0f + 2.0f * G2;

    // Hash corners with seed offset.
    auto idx = [seed](int ii, int jj) -> int {
        std::uint32_t h = seed;
        h = (h * 1103515245u + static_cast<std::uint32_t>(ii)) & 0x7fffffff;
        h = (h * 1103515245u + static_cast<std::uint32_t>(jj)) & 0x7fffffff;
        return static_cast<int>(perm[h % 256u]);
    };

    int gi0 = idx(i, j) % 12;
    int gi1 = idx(i + i1, j + j1) % 12;
    int gi2 = idx(i + 1, j + 1) % 12;

    // Contributions from the three corners.
    float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f;

    float t0 = 0.5f - x0 * x0 - z0 * z0;
    if (t0 > 0.0f) {
        t0 *= t0;
        n0 = t0 * t0 * dot2(grad2[gi0], x0, z0);
    }

    float t1 = 0.5f - x1 * x1 - z1 * z1;
    if (t1 > 0.0f) {
        t1 *= t1;
        n1 = t1 * t1 * dot2(grad2[gi1], x1, z1);
    }

    float t2 = 0.5f - x2 * x2 - z2 * z2;
    if (t2 > 0.0f) {
        t2 *= t2;
        n2 = t2 * t2 * dot2(grad2[gi2], x2, z2);
    }

    // Scale to [0, 1].  Theoretical range is ~[-1, +1]; 70 * sum brings it
    // very close to [0, 1] in practice.
    return (70.0f * (n0 + n1 + n2) + 1.0f) * 0.5f;
}

namespace simplex_detail {
struct LegacySimplex3DHash {
    std::uint32_t seed;

    int operator()(int ii, int jj, int kk) const noexcept
    {
        std::uint32_t h = seed;
        h = (h * 1103515245u + static_cast<std::uint32_t>(ii)) & 0x7fffffff;
        h = (h * 1103515245u + static_cast<std::uint32_t>(jj)) & 0x7fffffff;
        h = (h * 1103515245u + static_cast<std::uint32_t>(kk)) & 0x7fffffff;
        return static_cast<int>(perm[h % 256u]);
    }
};
} // namespace simplex_detail

// A policy owns corner hashing and isolates arithmetic instantiations. Strict
// callers must compile their entire instance in the declared precision domain.
template <class CornerHash> class Simplex3D final {
public:
    static float sample(float x, float y, float z, const CornerHash& hash) noexcept
    {
        using namespace simplex_detail;

        // Skew constants for 3D.
        constexpr float F3 = 1.0f / 3.0f;
        constexpr float G3 = 1.0f / 6.0f;

        float s = (x + y + z) * F3;
        int i = static_cast<int>(std::floor(x + s));
        int j = static_cast<int>(std::floor(y + s));
        int k = static_cast<int>(std::floor(z + s));

        float t = static_cast<float>(i + j + k) * G3;
        float X0 = static_cast<float>(i) - t;
        float Y0 = static_cast<float>(j) - t;
        float Z0 = static_cast<float>(k) - t;
        float x0 = x - X0;
        float y0 = y - Y0;
        float z0 = z - Z0;

        // Determine simplex traversal order.
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) {
                i1 = 1;
                j1 = 0;
                k1 = 0;
                i2 = 1;
                j2 = 1;
                k2 = 0;
            } else if (x0 >= z0) {
                i1 = 1;
                j1 = 0;
                k1 = 0;
                i2 = 1;
                j2 = 0;
                k2 = 1;
            } else {
                i1 = 0;
                j1 = 0;
                k1 = 1;
                i2 = 1;
                j2 = 0;
                k2 = 1;
            }
        } else {
            if (y0 < z0) {
                i1 = 0;
                j1 = 0;
                k1 = 1;
                i2 = 0;
                j2 = 1;
                k2 = 1;
            } else if (x0 < z0) {
                i1 = 0;
                j1 = 1;
                k1 = 0;
                i2 = 0;
                j2 = 1;
                k2 = 1;
            } else {
                i1 = 0;
                j1 = 1;
                k1 = 0;
                i2 = 1;
                j2 = 1;
                k2 = 0;
            }
        }

        float x1 = x0 - static_cast<float>(i1) + G3;
        float y1 = y0 - static_cast<float>(j1) + G3;
        float z1 = z0 - static_cast<float>(k1) + G3;
        float x2 = x0 - static_cast<float>(i2) + 2.0f * G3;
        float y2 = y0 - static_cast<float>(j2) + 2.0f * G3;
        float z2 = z0 - static_cast<float>(k2) + 2.0f * G3;
        float x3 = x0 - 1.0f + 3.0f * G3;
        float y3 = y0 - 1.0f + 3.0f * G3;
        float z3 = z0 - 1.0f + 3.0f * G3;

        const auto& idx3 = hash;

        int gi0 = idx3(i, j, k) % 12;
        int gi1 = idx3(i + i1, j + j1, k + k1) % 12;
        int gi2 = idx3(i + i2, j + j2, k + k2) % 12;
        int gi3 = idx3(i + 1, j + 1, k + 1) % 12;

        float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f, n3 = 0.0f;

        float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
        if (t0 > 0.0f) {
            t0 *= t0;
            n0 = t0 * t0 * dot3(grad3[gi0], x0, y0, z0);
        }

        float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
        if (t1 > 0.0f) {
            t1 *= t1;
            n1 = t1 * t1 * dot3(grad3[gi1], x1, y1, z1);
        }

        float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
        if (t2 > 0.0f) {
            t2 *= t2;
            n2 = t2 * t2 * dot3(grad3[gi2], x2, y2, z2);
        }

        float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
        if (t3 > 0.0f) {
            t3 *= t3;
            n3 = t3 * t3 * dot3(grad3[gi3], x3, y3, z3);
        }

        return (32.0f * (n0 + n1 + n2 + n3) + 1.0f) * 0.5f;
    }

private:
    static float dot3(const std::array<float, 3>& g, float x, float y, float z) noexcept
    {
        return g[0] * x + g[1] * y + g[2] * z;
    }
};

/// 3D simplex retains its legacy seed and floating-point policy.
inline float simplex3D(float x, float y, float z, std::uint32_t seed = 0u) noexcept
{
    return Simplex3D<simplex_detail::LegacySimplex3DHash>::sample(x, y, z, {seed});
}

/// Fractional Brownian motion — stacks multiple octaves of simplex2D.
/// `octaves`: number of detail layers (1-8 recommended).
/// `lacunarity`: frequency multiplier per octave (typically 2.0).
/// `gain`: amplitude multiplier per octave (typically 0.5).
/// Returns [0, 1].
inline float fbmSimplex(float x,
    float z,
    std::uint32_t seed,
    int octaves = 4,
    float lacunarity = 2.0f,
    float gain = 0.5f) noexcept
{
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude
            * simplex2D(x * frequency, z * frequency, seed + static_cast<std::uint32_t>(i));
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

} // namespace arp::pcg
