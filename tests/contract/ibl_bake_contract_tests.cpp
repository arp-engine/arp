// Contract test for the renderer-owned IBL convolution utilities.
//
// These are pure CPU math assertions (no GPU) that lock the contract between
// the IBL baker and the runtime TextureCube sampler:
//   - fp16 round-trip correctness
//   - cube face direction convention and cross-face edge continuity
//   - irradiance convolution energy conservation + directionality
//   - BRDF LUT endpoints (mirror vs rough)
//   - prefiltered cube mip chain structure and roughness response
//
// Per AGENTS.md each stage keeps a fast unit test; this is the IBL bake stage's.

#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/gi/ibl_bake.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool approx(float a, float b, float tol = 0.02f)
{
    return std::fabs(a - b) <= tol;
}

bool vec3Approx(arp::core::Vec3 a, arp::core::Vec3 b, float tol = 0.02f)
{
    return approx(a.x, b.x, tol) && approx(a.y, b.y, tol) && approx(a.z, b.z, tol);
}

// Constant radiance source: returns `c` for every direction.
arp::core::ibl::RadianceFn constantRadiance(arp::core::Vec3 c)
{
    return [c](arp::core::Vec3) {
        return c;
    };
}

void testFloatToHalfRoundTrip()
{
    const float values[] = {0.0f, 0.15f, 0.31f, 0.53f, 0.78f, 1.0f, 0.98f, 0.012f, 1.12f};
    for (float v : values) {
        const std::uint16_t h = arp::core::ibl::floatToHalf(v);
        const float back = arp::core::ibl::halfToFloat(h);
        assert(approx(back, v, 0.005f));
    }
    // Round-trip 1.0 exactly (half 0x3c00).
    assert(arp::core::ibl::floatToHalf(1.0f) == 0x3c00u);
    assert(arp::core::ibl::halfToFloat(0x3c00u) == 1.0f);
    // Zero stays zero with either sign.
    assert(arp::core::ibl::floatToHalf(0.0f) == 0x0000u);
    using arp::core::ibl::HalfFloatRounding;
    // Native float storage conversion can use toward-zero rounding. Keep its
    // exact representation separate from the default nearest-even encoder.
    assert(arp::core::ibl::floatToHalf(.01125f) == 8643u);
    assert(arp::core::ibl::floatToHalf(.01125f, HalfFloatRounding::TowardZero) == 8642u);
    assert(
        arp::core::ibl::floatToHalf(-.01125f, HalfFloatRounding::TowardZero) == (0x8000u | 8642u));
    assert(arp::core::ibl::floatToHalf(65536.f, HalfFloatRounding::TowardZero) == 0x7bffu);
    assert(arp::core::ibl::floatToHalf(-0.f, HalfFloatRounding::TowardZero) == 0x8000u);
    // Both encoders must preserve every finite binary16 representation,
    // including the transition from the largest subnormal to the first normal.
    for (unsigned bits = 0u; bits < 0x7c00u; ++bits) {
        const auto value = arp::core::ibl::halfToFloat(static_cast<std::uint16_t>(bits));
        assert(arp::core::ibl::floatToHalf(value) == bits);
        assert(arp::core::ibl::floatToHalf(value, HalfFloatRounding::TowardZero) == bits);
    }
    const auto belowNormal = std::nextafter(arp::core::ibl::halfToFloat(0x400u), 0.f);
    assert(arp::core::ibl::floatToHalf(belowNormal) == 0x400u);
    assert(arp::core::ibl::floatToHalf(belowNormal, HalfFloatRounding::TowardZero) == 0x3ffu);
    std::printf("[ok] fp16 round-trip (floatToHalf/halfToFloat)\n");
}

void testCubeFaceDirection()
{
    // Each face's center should resolve to its axis direction.
    assert(vec3Approx(arp::core::ibl::cubeFaceDirection(0u, 0.5f, 0.5f), {1.0f, 0.0f, 0.0f}));
    assert(vec3Approx(arp::core::ibl::cubeFaceDirection(1u, 0.5f, 0.5f), {-1.0f, 0.0f, 0.0f}));
    assert(vec3Approx(arp::core::ibl::cubeFaceDirection(2u, 0.5f, 0.5f), {0.0f, 1.0f, 0.0f}));
    assert(vec3Approx(arp::core::ibl::cubeFaceDirection(3u, 0.5f, 0.5f), {0.0f, -1.0f, 0.0f}));
    assert(vec3Approx(arp::core::ibl::cubeFaceDirection(4u, 0.5f, 0.5f), {0.0f, 0.0f, 1.0f}));
    assert(vec3Approx(arp::core::ibl::cubeFaceDirection(5u, 0.5f, 0.5f), {0.0f, 0.0f, -1.0f}));

    // Vulkan/GL cube lookup maps positive world Y to the top row of each
    // side face. Lock both orientation and shared-edge continuity because
    // face-center checks cannot detect a vertically flipped bake.
    const auto positiveXTop = arp::core::ibl::cubeFaceDirection(0u, 0.5f, 0.0f);
    const auto positiveXBottom = arp::core::ibl::cubeFaceDirection(0u, 0.5f, 1.0f);
    assert(positiveXTop.y > 0.0f);
    assert(positiveXBottom.y < 0.0f);
    assert(vec3Approx(positiveXTop, arp::core::ibl::cubeFaceDirection(2u, 1.0f, 0.5f)));
    assert(vec3Approx(positiveXBottom, arp::core::ibl::cubeFaceDirection(3u, 1.0f, 0.5f)));
    assert(vec3Approx(arp::core::ibl::cubeFaceDirection(4u, 0.5f, 0.0f),
        arp::core::ibl::cubeFaceDirection(2u, 0.5f, 1.0f)));
    std::printf("[ok] cube face direction convention + shared-edge continuity\n");
}

void testIrradianceConstantConservation()
{
    // A constant radiance field (1,1,1) convolved over a cosine-weighted
    // hemisphere yields approximately (1,1,1) everywhere: the PDF
    // (cosTheta/pi) integrates to 1, so irradiance(n) = radiance = (1,1,1).
    auto* alloc = &arp::core::defaultCpuMemoryResource();
    const auto cube = arp::core::ibl::bakeIrradianceCube(
        constantRadiance(arp::core::Vec3{1.0f, 1.0f, 1.0f}), 8u, alloc);
    // Sample face 0 (+X), texel near the centre.
    const std::size_t base = (0u * 8u * 8u + 4u * 8u + 4u) * 4u;
    const float r = arp::core::ibl::halfToFloat(cube[base + 0u]);
    const float g = arp::core::ibl::halfToFloat(cube[base + 1u]);
    const float b = arp::core::ibl::halfToFloat(cube[base + 2u]);
    assert(approx(r, 1.0f, 0.08f));
    assert(approx(g, 1.0f, 0.08f));
    assert(approx(b, 1.0f, 0.08f));
    std::printf("[ok] irradiance constant-radiance conservation (~1.0)\n");
}

void testIrradianceDirectionality()
{
    // Radiance only below the horizon (dir.y < 0): an upward-facing normal
    // (the +Y face centre) should see almost no light, while a downward-facing
    // normal (the -Y face centre) should be bright. This proves the bake has
    // real directional separation, not a flat fill.
    auto* alloc = &arp::core::defaultCpuMemoryResource();
    auto downOnly = [](arp::core::Vec3 dir) -> arp::core::Vec3 {
        return dir.y < 0.0f ? arp::core::Vec3{1.0f, 1.0f, 1.0f} : arp::core::Vec3{0.0f, 0.0f, 0.0f};
    };
    const auto cube = arp::core::ibl::bakeIrradianceCube(downOnly, 8u, alloc);
    // +Y face centre (normal up).
    std::size_t upBase = (2u * 8u * 8u + 4u * 8u + 4u) * 4u;
    const float upR = arp::core::ibl::halfToFloat(cube[upBase + 0u]);
    // -Y face centre (normal down).
    std::size_t downBase = (3u * 8u * 8u + 4u * 8u + 4u) * 4u;
    const float downR = arp::core::ibl::halfToFloat(cube[downBase + 0u]);
    assert(upR < 0.15f);  // upward normal sees little of the below-horizon light
    assert(downR > 0.5f); // downward normal sees most of it
    std::printf("[ok] irradiance directionality (up-dark / down-bright)\n");
}

void testBrdfLutEndpoints()
{
    // At roughness ~ 0 (top row), the surface is a perfect mirror: scale ~ 1,
    // bias ~ 0 (F0 dominates). At roughness ~ 1 (bottom row), energy bleeds
    // from the F0 term so scale < 1.
    auto* alloc = &arp::core::defaultCpuMemoryResource();
    const auto lut = arp::core::ibl::bakeBrdfIntegrationLut(16u, alloc);
    const std::uint32_t res = 16u;
    // Top row (roughness 0), pick a mid NdotV.
    const std::size_t topBase = (0u * res + 8u) * 4u;
    const float topScale = arp::core::ibl::halfToFloat(lut[topBase + 0u]);
    const float topBias = arp::core::ibl::halfToFloat(lut[topBase + 1u]);
    assert(topScale > 0.9f);
    assert(topBias < 0.1f);
    // Bottom row (roughness ~ 1).
    const std::size_t botBase = ((res - 1u) * res + 8u) * 4u;
    const float botScale = arp::core::ibl::halfToFloat(lut[botBase + 0u]);
    assert(botScale < topScale);
    std::printf("[ok] BRDF LUT endpoints (mirror scale~1/bias~0, rough scale<1)\n");
}

void testPrefilteredCubeStructure()
{
    // Constant radiance -> each mip is constant (uniform field is roughness-
    // invariant), and the mip resolutions halve down the chain.
    auto* alloc = &arp::core::defaultCpuMemoryResource();
    const auto pc = arp::core::ibl::bakePrefilteredSpecularCube(
        constantRadiance(arp::core::Vec3{1.0f, 1.0f, 1.0f}), 8u, 3u, alloc);
    assert(pc.mipCount == 3u);
    assert(pc.mips.size() == 3u);
    // mip 0 = 8x8x6, mip 1 = 4x4x6, mip 2 = 2x2x6 faces (4 half values/texel).
    assert(pc.mips[0].texelCount == static_cast<std::size_t>(6u * 8u * 8u * 4u));
    assert(pc.mips[1].texelCount == static_cast<std::size_t>(6u * 4u * 4u * 4u));
    assert(pc.mips[2].texelCount == static_cast<std::size_t>(6u * 2u * 2u * 4u));
    // Every mip's first texel should be ~1.0 (constant field, normalized by weights).
    for (std::uint32_t m = 0u; m < 3u; ++m) {
        const float v = arp::core::ibl::halfToFloat(pc.storage[pc.mips[m].texelOffset]);
        assert(approx(v, 1.0f, 0.15f));
    }
    std::printf("[ok] prefiltered cube mip chain structure + constant-field invariance\n");
}

void testPrefilteredCubeRoughnessResponse()
{
    // Keep every mip at 1x1 so all three samples use the exact +Z face center.
    // A narrow directional lobe must lose peak energy as roughness widens the
    // GGX convolution. The former double-reflection bug sampled +Z for every
    // ray and therefore returned 1.0 at every mip.
    auto sharpPositiveZ = [](arp::core::Vec3 dir) -> arp::core::Vec3 {
        const float lobe = std::pow(std::max(dir.z, 0.0f), 64.0f);
        return {lobe, lobe, lobe};
    };
    auto* alloc = &arp::core::defaultCpuMemoryResource();
    const auto cube = arp::core::ibl::bakePrefilteredSpecularCube(sharpPositiveZ, 1u, 3u, alloc);

    const auto faceValue = [&cube](std::uint32_t mip, std::uint32_t face) {
        const std::size_t faceOffset = static_cast<std::size_t>(face) * 4u;
        return arp::core::ibl::halfToFloat(cube.storage[cube.mips[mip].texelOffset + faceOffset]);
    };
    const float mirrorPeak = faceValue(0u, 4u);
    const float mediumPeak = faceValue(1u, 4u);
    const float roughPeak = faceValue(2u, 4u);
    assert(mirrorPeak > 0.95f);
    assert(mediumPeak < mirrorPeak * 0.90f);
    assert(roughPeak < mediumPeak * 0.90f);
    std::printf("[ok] prefiltered cube widens a directional lobe across roughness mips\n");
}

} // namespace

int main()
{
    testFloatToHalfRoundTrip();
    testCubeFaceDirection();
    testIrradianceConstantConservation();
    testIrradianceDirectionality();
    testBrdfLutEndpoints();
    testPrefilteredCubeStructure();
    testPrefilteredCubeRoughnessResponse();
    std::printf("all ibl_bake contract tests passed\n");
    return 0;
}
