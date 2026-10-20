// World of ClaudeCraft Procedural Sky Shader
// Renders the interior of a camera-following dome as a vertical gradient
// (zenith → horizon) with a procedural warm sun glow layered toward the
// canonical sun direction. Replicates the reference low-tier sky
// (sky.ts:238-307) without HDRI sampling — the gradient colors and sun
// glow are computed analytically so no external texture is required.
//
// Contract: the dome mesh is an inverted sphere (appendSkyDome) rendered
// with CullMode::Front so the camera sees the interior surface. The vertex
// shader offsets the dome by the full camera position (XYZ) each frame so
// the dome is centered on the camera in view space after the view transform
// V (which maps camera_position → origin and cancels the offset).

// Sky push constants for the procedural sky dome.
//   viewProjection     : camera view-projection matrix (default float4x4 layout)
//   cameraXzDomeRadius : xyz = camera world pos (full XYZ), w = dome radius
//   sunDirSunColorR    : xyz = normalized sun direction (surface-to-sun), w = sun disc intensity
//   sunGlowZenithR     : x = glow power (exponent), y = glow strength, z = (unused), w = zenith.r
//   horizonZenithG     : xyz = horizon color, w = zenith.g
//   zenithBExposure    : x = zenith.b, y = exposure
//
// viewProjection uses the DEFAULT float4x4 layout (Slang -> SPIR-V RowMajor),
// identical to pbr_scene.slang's viewProjection. The C++ side (app.cpp) fills
// this with the TRANSPOSE of the column-major VP (i.e. row-major data); a
// default (RowMajor) declaration reads it without an extra transpose.
// Declaring `row_major` here (SPIR-V ColMajor) would re-transpose the data,
// producing the wrong clip space — the dome would project through a transposed
// VP and the sky would render incorrectly (the same matrix-layout mismatch
// class as the shadow_depth bug). Both the sky shader and the PBR pass consume
// the same row-major VP data, so both must use the default layout. (The
// explanatory comment lives ABOVE the struct so the push-constant size
// reflection, which parses struct members off ';'-terminated statements, does
// not absorb the comment into the first field's type token.)
struct WoccSkyPushConstants
{
    float4x4 viewProjection;
    float4 cameraXzDomeRadius;
    float4 sunDirSunColorR;
    float4 sunGlowZenithR;
    float4 horizonZenithG;
    float4 zenithBExposure;
};

[[vk::push_constant]]
WoccSkyPushConstants gSky;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldDir : TEXCOORD0;  // object-space position = view direction
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    // Offset the dome to follow the camera position (full XYZ). The view
    // projection matrix already includes the camera view transform V which
    // maps camera_position → origin, so V * cameraPos = 0 and the dome is
    // perfectly centered around the camera in view space. Without this the
    // dome center shifts in view space (V * pinned_offset ≠ 0), causing
    // parts of the sphere to fall outside the clip frustum.
    float3 worldPos = input.position + gSky.cameraXzDomeRadius.xyz;
    output.worldDir = input.position;  // un-offset: true view direction
    output.position = mul(gSky.viewProjection, float4(worldPos, 1.0));
    return output;
}

struct FragmentOutput
{
    float4 sceneColor : SV_Target0;
    // Sky has no geometric motion and cannot seed temporal reactivity.  Write
    // neutral values so its attachment contract matches the PBR scene pass.
    float2 velocity : SV_Target1;
    float reactive : SV_Target2;
};

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input)
{
    float3 dir = normalize(input.worldDir);

    // Vertical gradient: zenith (up) → horizon (sideways).
    // heightFactor = 1 at zenith, 0 at horizon. Below-horizon clamps to horizon.
    float heightFactor = saturate(dir.y);
    // Smooth the transition for a softer band near the horizon.
    heightFactor = pow(heightFactor, 0.6);

    float3 zenithColor = float3(gSky.sunGlowZenithR.w, gSky.horizonZenithG.w, gSky.zenithBExposure.x);
    float3 horizonColor = gSky.horizonZenithG.xyz;
    float3 skyColor = lerp(horizonColor, zenithColor, heightFactor);

    // --- Sun ---
    float sunDot = max(dot(dir, gSky.sunDirSunColorR.xyz), 0.0);

    // Sun disc: angular radius, with soft edge.
    // cos(3°) ≈ 0.99863, cos(8°) ≈ 0.99027
    // Wide enough for the 32x20 dome (11° per horizontal segment — worst-case
    // interpolation error ~5.6° means the sun direction must be covered by the
    // soft-fade zone even when it falls exactly between dome vertices).
    const float kSunDiscInner = 0.99863;
    const float kSunDiscOuter = 0.99027;
    float sunDisc = smoothstep(kSunDiscOuter, kSunDiscInner, sunDot);

    // Wide atmospheric glow (scattering around the sun).
    float sunGlow = pow(sunDot, gSky.sunGlowZenithR.x);

    // Warm golden sun color (same as SkyboxComponent::setSunColor).
    float3 sunColor = float3(1.0, 0.80, 0.55);
    float sunIntensity = gSky.sunDirSunColorR.w;
    float glowStrength = gSky.sunGlowZenithR.y;

    // Bright disc + softer atmospheric glow halo.
    float3 sunContrib = sunColor * sunDisc * sunIntensity
                      + sunColor * sunGlow * glowStrength;

    float3 color = skyColor + sunContrib;

    // The sky shader outputs HDR linear color: exposure and ACES tone mapping
    // are applied centrally by the post-process chain (shaders/wocc/post.slang
    // toneMappingFragmentMain), matching the PBR scene pass. zenithBExposure.y
    // (exposure) is consumed by the post tonemap pass, not here.

    FragmentOutput output;
    output.sceneColor = float4(color, 1.0);
    output.velocity = float2(0.0, 0.0);
    output.reactive = 0.0;
    return output;
}
