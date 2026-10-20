#pragma once

// ShadowSystem - directional light shadow frustum computation.
//
// Two APIs live here:
//
// 1. Single-frustum (player-following) - computeShadowLightViewProjection().
//    Used by the current single 512x512 shadow map. The light sits at
//    playerPos + SUN_ANCHOR and looks at playerPos; the orthographic
//    frustum halfExtent/near/far come from ShadowFrustumDesc.
//
// 2. CSM (camera-aligned cascaded shadow maps) - computeCascadeLightViewProjection().
//    Three cascades (near/mid/far) carved out of the camera frustum. Each
//    cascade is an axis-aligned orthographic frustum sized to fit the camera
//    sub-frustum, recentered on the player so texel density is stable. The
//    light direction is taken from the lightDir argument (default preserves
//    the legacy SUN_ANCHOR convention); only the frustum origin scrolls with
//    the player/camera. Pass the runtime keyDirection so shadows align with
//    the shading light direction.
//
// Design (shared):
//   - Pure value type (no heap) - computes VP from player/camera state.
//   - Row-major float4x4 output (matches the renderer's matrix convention;
//     AGENTS.md "Matrix Layout" §2-3: C++ row-major storage, shader default
//     float4x4 -> SPIR-V RowMajor, no transposition on either side).
//   - Deterministic: same input -> same VP, no jittering.
//   - Delegates to core::lookAt() + core::orthographic() + core::multiply()
//     so the shadow path benefits from the same vetted math helpers as the
//     camera path, rather than maintaining a parallel matrix pipeline.
//
// Layering note: this is a renderer-scoped subsystem (shadow map / PCF /
// resolution are render concepts), sibling to global_illumination.h. It
// keeps depending on core::math for the actual matrix algebra - no parallel
// matrix pipeline is introduced here.

#include "core/math.h"

#include <array>
#include <cmath>
#include <cstring>

namespace arp::renderer {

/// Canonical WoCC sun anchor - the offset from the player to the directional
/// light position. The normalized direction is the light view direction.
/// Matches gfx.ts:688-689 (SUN_ANCHOR = (90, 140, 50)).
struct SunAnchor {
    static constexpr float kX = 90.0f;
    static constexpr float kY = 140.0f;
    static constexpr float kZ = 50.0f;
};

/// Shadow frustum parameters - matches the reference (renderer.ts:1095-1100).
struct ShadowFrustumDesc {
    float halfExtent = 95.0f; // ortho left/right/top/bottom ±halfExtent
    float nearPlane = 30.0f;
    float farPlane = 480.0f;
};

/// Cascade count for CSM. Three cascades cover near/mid/far depth ranges
/// with independent texel densities (near = high res, far = low res).
constexpr std::uint32_t kCsmCascadeCount = 3u;

/// One cascade's coverage and resolution. farDepth is the camera-space depth
/// (positive, in meters) at which this cascade ends; the cascade starts at
/// the previous cascade's farDepth (cascade 0 starts at 0). halfExtent is the
/// orthographic half-width/height (meters) the cascade covers in light space.
struct CascadeDesc {
    float farDepth;   // camera-space depth where this cascade ends (m)
    float halfExtent; // ortho half-extent in light space (m)
};

/// CSM configuration - cascade splits + shared near/far for the light frustum.
/// Defaults cover a typical WoCC outdoor scene:
///   cascade 0 (near): 0-30m,  ±30m  (high-res player/interior)
///   cascade 1 (mid):  30-80m, ±80m  (mid-field terrain/structures)
///   cascade 2 (far):  80-300m,±200m (far terrain/horizon)
struct CsmShadowConfig {
    std::array<CascadeDesc, kCsmCascadeCount> cascades = {{
        {30.0f, 30.0f},
        {80.0f, 80.0f},
        {300.0f, 200.0f},
    }};
    float nearPlane = 5.0f;  // light frustum near (m)
    float farPlane = 500.0f; // light frustum far (m)
};

/// Compute the shadow light view-projection matrix (row-major float4x4)
/// for a player-following orthographic frustum.
///
/// The light sits at `playerPos + SUN_ANCHOR` and looks at `playerPos`.
/// The projection is orthographic with the configured half-extent.
///
/// @param playerX   Player world X position.
/// @param playerY   Player world Y position.
/// @param playerZ   Player world Z position.
/// @param desc      Shadow frustum parameters.
/// @param outRowMajor  Output 16-float row-major matrix (HLSL float4x4).
inline void computeShadowLightViewProjection(float playerX,
    float playerY,
    float playerZ,
    const ShadowFrustumDesc& desc,
    float outRowMajor[16],
    const core::Vec3& lightDir = core::Vec3{-90.0f, -140.0f, -50.0f}) noexcept
{
    // lightDir is the light-ray propagation direction (sun -> scene, y < 0).
    // The light position sits opposite to it, "above" the player. The default
    // preserves the legacy SUN_ANCHOR=(90,140,50) convention exactly:
    // normalize(-90,-140,-50) * |SUN_ANCHOR|, negated, == +(90,140,50). Pass
    // the runtime keyDirection here so the shadow frustum follows the shading
    // light direction; otherwise shadows land where the sun doesn't illuminate.
    const float len =
        std::sqrt(lightDir.x * lightDir.x + lightDir.y * lightDir.y + lightDir.z * lightDir.z);
    const float invLen = (len > 1e-8f) ? (1.0f / len) : 0.0f;
    constexpr float kSunAnchorDistance = 173.8292f; // sqrt(90^2 + 140^2 + 50^2)
    const core::Vec3 lightPos{playerX - lightDir.x * invLen * kSunAnchorDistance,
        playerY - lightDir.y * invLen * kSunAnchorDistance,
        playerZ - lightDir.z * invLen * kSunAnchorDistance};
    const core::Vec3 target{playerX, playerY, playerZ};

    // View matrix: left-handed lookAt (light -> target).
    // core::lookAt() produces a left-handed view matrix where the forward
    // basis is +normalize(focus - eye), so view-space z is positive in front
    // of the camera. The orthographic projection below maps that positive
    // z to [0,1].
    const core::Mat4 view = core::lookAt(lightPos, target, core::Vec3{0.0f, 1.0f, 0.0f});

    // Orthographic projection: left-handed, z -> [0,1], w unchanged.
    // halfWidth = halfHeight = halfExtent (square shadow map).
    const core::Mat4 proj =
        core::orthographic(desc.halfExtent, desc.halfExtent, desc.nearPlane, desc.farPlane);

    // VP = proj * view (row-major multiply). C++ stores m[row*4+col]; the
    // shader declares default float4x4 (-> SPIR-V RowMajor decoration, verified
    // by contract tests in tests/contract/shader_tests.cpp), which reads these
    // bytes without transposition. Caster (shadow_depth.slang lightViewProjection)
    // and receiver (pbr_scene.slang lightViewProjection) share the same default
    // declaration and SPIR-V RowMajor decoration, so both project into the same
    // light clip space. See AGENTS.md "Matrix Layout" §2-3.
    //
    // The previous transposed (P*V)^T fill was a workaround from when one shader
    // declared `row_major float4x4` (-> SPIR-V ColMajor); both shaders now use
    // the default declaration, so the transpose must not be applied (it would
    // re-transpose the already-row-major data and break caster/receiver
    // agreement). Verified via spirv-dis: both shadow_depth.spv and
    // pbr_scene.spv decorate lightViewProjection with RowMajor + MatrixStride 16.
    const core::Mat4 vp = core::multiply(proj, view);
    std::memcpy(outRowMajor, vp.m.data(), sizeof(float) * 16);
}

/// Compute the light view-projection matrix for one CSM cascade (row-major
/// float4x4). The cascade is a player-centered orthographic frustum sized to
/// `cascade.halfExtent`, recentered on the player/camera origin so shadow
/// texel density stays stable as the player moves. The light direction is
/// constant (= normalize(SUN_ANCHOR)); only the frustum origin scrolls.
///
/// The light frustum near/far come from `config.nearPlane`/`config.farPlane`
/// (shared across cascades) - they cover the full depth range of the largest
/// cascade so all cascades share the same depth scale. Per-cascade split
/// depth is NOT applied here (it's a receiver-side concern: the PBR shader
/// picks the cascade by view depth and rescales UVs to the atlas sub-region).
///
/// @param playerX        Player world X (cascade recenter origin).
/// @param playerY        Player world Y.
/// @param playerZ        Player world Z.
/// @param cascade        Cascade coverage + half-extent.
/// @param config         CSM shared near/far planes.
/// @param outRowMajor    Output 16-float row-major matrix.
inline void computeCascadeLightViewProjection(float playerX,
    float playerY,
    float playerZ,
    const CascadeDesc& cascade,
    const CsmShadowConfig& config,
    float outRowMajor[16],
    const core::Vec3& lightDir = core::Vec3{-90.0f, -140.0f, -50.0f}) noexcept
{
    // lightDir is the light-ray propagation direction (sun -> scene, y < 0);
    // see computeShadowLightViewProjection for the default/derivation. The
    // cascade frustum is centered on the player so texel density is stable;
    // only halfExtent changes per cascade.
    const float len =
        std::sqrt(lightDir.x * lightDir.x + lightDir.y * lightDir.y + lightDir.z * lightDir.z);
    const float invLen = (len > 1e-8f) ? (1.0f / len) : 0.0f;
    constexpr float kSunAnchorDistance = 173.8292f; // sqrt(90^2 + 140^2 + 50^2)
    const core::Vec3 lightPos{playerX - lightDir.x * invLen * kSunAnchorDistance,
        playerY - lightDir.y * invLen * kSunAnchorDistance,
        playerZ - lightDir.z * invLen * kSunAnchorDistance};
    const core::Vec3 target{playerX, playerY, playerZ};
    const core::Mat4 view = core::lookAt(lightPos, target, core::Vec3{0.0f, 1.0f, 0.0f});
    const core::Mat4 proj = core::orthographic(
        cascade.halfExtent, cascade.halfExtent, config.nearPlane, config.farPlane);

    // Same row-major direct-copy contract as computeShadowLightViewProjection:
    // C++ row-major storage, shader default float4x4 (SPIR-V RowMajor), no
    // transposition. Caster (shadow_depth.slang) and receiver (pbr_scene.slang)
    // must declare the cascade VP with the same default float4x4.
    const core::Mat4 vp = core::multiply(proj, view);
    std::memcpy(outRowMajor, vp.m.data(), sizeof(float) * 16);
}

/// Compute all kCsmCascadeCount cascade light VPs in one call. Convenient for
/// the per-frame UBO fill in wocc_runtime_session. The output array is
/// `kCsmCascadeCount * 16` floats, packed as 3 row-major float4x4 matrices.
inline void computeCsmLightViewProjections(float playerX,
    float playerY,
    float playerZ,
    const CsmShadowConfig& config,
    float outRowMajor[kCsmCascadeCount * 16],
    const core::Vec3& lightDir = core::Vec3{-90.0f, -140.0f, -50.0f}) noexcept
{
    for (std::uint32_t i = 0; i < kCsmCascadeCount; ++i) {
        computeCascadeLightViewProjection(
            playerX, playerY, playerZ, config.cascades[i], config, outRowMajor + i * 16, lightDir);
    }
}

/// Shadow system state - holds the current light VP and shadow parameters.
/// Updated each frame with the player position.
class ShadowSystem {
public:
    ShadowSystem() = default;

    /// Update the shadow frustum to follow the player.
    void update(float playerX, float playerY, float playerZ) noexcept
    {
        computeShadowLightViewProjection(playerX, playerY, playerZ, frustum_, lightVP_);
    }

    /// Enable/disable shadows.
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    bool enabled() const noexcept { return enabled_; }

    /// Set the PCF kernel radius (in texels).
    void setPcfRadius(float radius) noexcept { pcfRadius_ = radius; }

    float pcfRadius() const noexcept { return pcfRadius_; }

    /// Set the shadow strength (0 = no shadow, 1 = full occlusion).
    void setStrength(float strength) noexcept { strength_ = strength; }

    float strength() const noexcept { return strength_; }

    /// Set the shadow map resolution (used for texel size computation).
    void setShadowMapResolution(std::uint32_t resolution) noexcept
    {
        shadowMapResolution_ = resolution;
        if (shadowMapResolution_ > 0u) {
            texelSize_ = 1.0f / static_cast<float>(shadowMapResolution_);
        }
    }

    std::uint32_t shadowMapResolution() const noexcept { return shadowMapResolution_; }

    float texelSize() const noexcept { return texelSize_; }

    /// Get the row-major light VP matrix (16 floats, HLSL float4x4).
    const float* lightViewProjection() const noexcept { return lightVP_; }

    /// Get the frustum description.
    const ShadowFrustumDesc& frustum() const noexcept { return frustum_; }

private:
    ShadowFrustumDesc frustum_;
    float lightVP_[16] = {};
    bool enabled_ = false;
    float pcfRadius_ = 4.0f; // reference: shadow.radius = 4
    float strength_ = 1.0f;  // reference: full strength (intensity 1.0)
    std::uint32_t shadowMapResolution_ = 2048u;
    float texelSize_ = 1.0f / 2048.0f;
};

} // namespace arp::renderer
