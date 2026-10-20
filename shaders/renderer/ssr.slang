// WoCC Screen-Space Reflection (SSR) shader.
//
// Two-pass architecture (per plan: compute trace + graphics composite):
//   1. ssrTraceComputeMain (compute, [numthreads(8,8,1)]):
//      Per-pixel linear ray march in view space against the scene depth
//      buffer, with binary refinement. Writes reflection color + hit mask
//      (RGBA16F: RGB = reflected color sampled from SceneColorTexture,
//      A = hit confidence [0,1]) to SsrReflectionOut storage texture.
//   2. ssrCompositeFragmentMain (fragment, fullscreen triangle):
//      Reads SsrReflectionInput (trace result) + SceneColorTexture, blends
//      reflection over scene color by intensity + edge fade.
//
// Matrix layout contract (AGENTS.md section 3): this shader does NOT declare
// any float4x4 in push constants. The inverse view-projection is packed as
// two float4 rows (Y and W) to reconstruct world position from clip-space
// depth, mirroring post_fx.slang's pattern. This deliberately avoids the
// float4x4 RowMajor decoration contract test - a reflection pass does not
// need the full matrix, only world-position reconstruction. These two rows
// are ALSO used to analytically solve for the water surface depth at each
// pixel (the water plane is horizontal at a known world Y), enabling SSR to
// trace reflections from the water surface rather than from the underwater
// terrain (the water pass has depthWrite=false, so SceneDepthTexture carries
// terrain depth for water pixels).
//
// Push-constant struct body MUST contain NO inline // comments (AGENTS.md
// section 4: shader_compiler.cpp's reflectedPushConstantSize parser does not
// skip // comments). Explanatory comments live ABOVE the struct.

struct SsrPushConstants
{
    float4 invViewProjectionY;
    float4 invViewProjectionW;
    float4 projectionParams;
    float4 ssrParams;
    float4 waterParams;
    float4 inputUvScaleBias;
    float4 surfaceToKeyLightDirectionView;
};

[[vk::push_constant]]
SsrPushConstants gSsr;

#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name

// Set 0 bindings (shared layout for both passes; trace uses 0/1/2/3,
// composite uses 0/2/4 - unused bindings are still declared because the
// requires every layout binding to have a valid descriptor written).
[[vk::binding(0, 0)]] Texture2D<float4> SceneColorTexture;
[[vk::binding(1, 0)]] ARP_SAMPLED_DEPTH(SceneDepthTexture);
[[vk::binding(2, 0)]] SamplerState      LinearSampler;
[[vk::binding(3, 0)]] RWTexture2D<float4> SsrReflectionOut;
[[vk::binding(4, 0)]] Texture2D<float4> SsrReflectionInput;

// D32 scene depth is fetched as integer texels so the
// shader does not pair texture_depth_2d with the filtering color sampler.
float SampleSceneDepth(float2 uv)
{
    uint width, height;
    SceneDepthTexture.GetDimensions(width, height);
    int2 pixel = int2(clamp(
        uv * float2(float(width), float(height)),
        float2(0.0, 0.0),
        float2(float(width - 1u), float(height - 1u))));
    return SceneDepthTexture.Load(int3(pixel, 0)).r;
}

// ===========================================================================
//  Shared helpers
// ===========================================================================

float2 screenNdc(float2 uv)
{
    float2 transformedUv = uv;
    transformedUv.y = transformedUv.y * gSsr.inputUvScaleBias.x
        + gSsr.inputUvScaleBias.y;
    return transformedUv * 2.0 - 1.0;
}

float2 screenUv(float2 screenXY)
{
    float2 uv = screenXY * 0.5 + 0.5;
    uv.y = uv.y * gSsr.inputUvScaleBias.x + gSsr.inputUvScaleBias.y;
    return uv;
}

float3 reconstructViewPosition(float2 uv, float viewZ)
{
    float2 ndc = screenNdc(uv);
    float scaleX = abs(gSsr.waterParams.z) > 1e-6
        ? gSsr.waterParams.z : 1.0;
    float scaleY = abs(gSsr.waterParams.w) > 1e-6
        ? gSsr.waterParams.w : -1.0;
    return float3(ndc.x * viewZ / scaleX,
                  ndc.y * viewZ / scaleY,
                  viewZ);
}

float2 projectViewPosition(float3 viewPosition)
{
    float scaleX = abs(gSsr.waterParams.z) > 1e-6
        ? gSsr.waterParams.z : 1.0;
    float scaleY = abs(gSsr.waterParams.w) > 1e-6
        ? gSsr.waterParams.w : -1.0;
    float2 ndc = float2(viewPosition.x * scaleX,
                        viewPosition.y * scaleY)
        / max(viewPosition.z, 0.0001);
    return screenUv(ndc);
}

// Reconstruct view-space Z (world-space meters) from raw engine depth [0,1].
// Mirrors post_fx.slang's ReconstructViewZ (left-handed perspective, [0,1]
// depth): depth = f/(f-n) - (n*f)/((f-n)*viewZ), inverted to
// viewZ = (n*f) / (f - depth*(f-n)).
float ReconstructViewZ(float rawDepth, float camNear, float camFar)
{
    float denom = camFar - rawDepth * (camFar - camNear);
    return (camNear * camFar) / max(denom, 0.0001);
}

// Reconstruct world-space position from UV + raw depth + inverse VP rows.
// Uses the packed invViewProjectionY / invViewProjectionW rows to avoid a
// full float4x4 in push constants (AGENTS.md matrix-layout contract).
float3 ReconstructWorldPos(float2 uv, float rawDepth)
{
    float4 clip = float4(screenNdc(uv), rawDepth, 1.0);
    float worldY = dot(gSsr.invViewProjectionY, clip);
    float worldW = dot(gSsr.invViewProjectionW, clip);
    // invViewProjectionY/W carry only two rows; reconstructing full XYZ
    // requires all four rows. For SSR we only need view-space Z (from
    // ReconstructViewZ) and the reflection direction (view-space), so this
    // helper is reserved for future world-space extensions. The trace pass
    // below works entirely in view space.
    return float3(0.0, worldY / max(abs(worldW), 0.0001), 0.0);
}

// Analytically solve for the raw engine depth [0,1] where the camera ray
// through `uv` pierces the horizontal water plane (world Y = waterPlaneY).
// This lets SSR trace reflections from the water surface itself, which is
// required because the water pass has depthWrite=false: SceneDepthTexture
// holds underwater terrain depth for water pixels, so a naive trace would
// start from the terrain surface (wrong origin, wrong normal) instead of
// the water surface.
//
// For clip = (ndc.x, ndc.y, d, 1), the reconstructed world Y is a linear
// rational function of d:
//   worldY(d) = dot(invVP_Y, clip) / dot(invVP_W, clip)
//             = (num + numD*d) / (den + denD*d)
// where num/den are evaluated at d=0 (the xy-only part) and numD/denD are
// the .z coefficients (the depth-weighted part). Setting worldY = waterPlaneY
// and solving the single linear equation yields:
//   d = (waterPlaneY * den - num) / (numD - waterPlaneY * denD)
//
// Returns -1.0 when the camera ray is parallel to the water plane (denom ~0,
// i.e. the ray never intersects, like at the horizon) or when the solution
// lies outside [0,1] (the water plane is behind the camera or beyond the far
// plane for this pixel). Callers must guard against -1.0.
float SolveWaterRawDepth(float2 uv, float waterPlaneY)
{
    float2 ndc = screenNdc(uv);
    float4 clip0 = float4(ndc.x, ndc.y, 0.0, 1.0);
    float num  = dot(gSsr.invViewProjectionY, clip0);
    float numD = gSsr.invViewProjectionY.z;
    float den  = dot(gSsr.invViewProjectionW, clip0);
    float denD = gSsr.invViewProjectionW.z;
    float denom = numD - waterPlaneY * denD;
    if (abs(denom) < 1e-6) return -1.0;
    float d = (waterPlaneY * den - num) / denom;
    return d;
}

// ===========================================================================
//  Pass 1: SSR trace (compute)
// ===========================================================================
//
// Algorithm: linear ray march + binary refine.
//   1. Reconstruct view-space position P from pixel UV + depth. For water
//      pixels (water plane closer than scene depth), the depth is the
//      analytical water-surface depth from SolveWaterRawDepth, NOT the
//      SceneDepthTexture value (which is underwater terrain depth because
//      the water pass has depthWrite=false).
//   2. March the canonical surface-to-key-light direction. This is the same
//      lighting fact used by the PBR, water, and shadow consumers; it is
//      supplied by the C++ render boundary after the world-to-view transform.
//   3. March the reflected ray in view space, projecting each step to screen
//      UV and comparing the ray's view Z against the sampled scene depth.
//   4. On a hit (ray Z behind scene depth within a thickness tolerance),
//      binary-refine the hit position for sub-pixel accuracy.
//   5. Sample SceneColorTexture at the refined hit UV, write RGB + hit
//      confidence A to SsrReflectionOut.
//
// Edge fade: reflections fade near screen borders (where the reflected ray
// leaves the visible region) to avoid hard cutoffs.

[shader("compute")]
[numthreads(8, 8, 1)]
void ssrTraceComputeMain(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    uint width, height;
    SceneColorTexture.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height) return;

    float2 invRes = float2(1.0 / float(width), 1.0 / float(height));
    float2 uv = (float2(pixel) + 0.5) * invRes;

    float camNear = gSsr.projectionParams.x;
    float camFar  = gSsr.projectionParams.y;
    float rayStep = gSsr.projectionParams.z;
    float maxSteps = gSsr.projectionParams.w;
    float thickness   = gSsr.ssrParams.x;
    float roughCutoff = gSsr.ssrParams.y;
    float intensity   = gSsr.ssrParams.z;
    float edgeFade    = gSsr.ssrParams.w;

    float rawDepth = SampleSceneDepth(uv);

    // --- Water surface analytical classification ---
    // The water pass has depthWrite=false, so for water pixels SceneDepthTexture
    // holds the UNDERWATER terrain depth, not the water surface. To trace
    // reflections from the actual water surface, analytically solve for the
    // depth where the camera ray pierces the horizontal water plane (world Y =
    // waterPlaneY) using the packed inverse-VP Y/W rows. A pixel is water when
    // the water surface is closer to the camera than the scene geometry
    // (waterRawDepth < rawDepth) and the intersection lies in [0,1].
    float waterPlaneY = gSsr.waterParams.x;
    float waterEnabled = gSsr.waterParams.y;
    float waterRawDepth = (waterEnabled > 0.5)
        ? SolveWaterRawDepth(uv, waterPlaneY) : -1.0;
    bool isWater = (waterRawDepth >= 0.0 && waterRawDepth < 1.0
                    && waterRawDepth < rawDepth);

    // This trace only has a reliable surface model for water. Applying the
    // same screen-space reflection to terrain without a material roughness or
    // normal buffer reflects the terrain back onto itself and creates large
    // false triangular patches. Keep SSR scoped to the surface contract it
    // can actually validate.
    if (!isWater) {
        SsrReflectionOut[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Water pixels that show sky behind them (rawDepth ~= 1) still need to
    // trace - the reflected ray may hit above-water geometry, and a miss
    // degrades gracefully to the water shader's ambient Fresnel fallback.
    if (rawDepth >= 0.99999 && !isWater) {
        SsrReflectionOut[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Trace origin depth: water surface (analytical) for water pixels, scene
    // depth for everything else.
    float traceDepth = isWater ? waterRawDepth : rawDepth;
    float viewZ = ReconstructViewZ(traceDepth, camNear, camFar);

    // Reconstruct the perspective-correct view-space origin. Treating screen
    // NDC directly as view-space x/y makes fixed world-direction rays diverge
    // with depth and produces large triangular false hits on WebGPU.
    float3 Pview = reconstructViewPosition(uv, viewZ);

    // SSR projection follows the one canonical scene-light direction. Do not
    // derive this from the camera vector: that makes the projected image rotate
    // with the camera instead of remaining anchored to the authored light.
    float3 R = normalize(gSsr.surfaceToKeyLightDirectionView.xyz);
    if (dot(R, R) <= 1e-6) {
        SsrReflectionOut[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Ray march: step from Pview along R, project each step to screen UV,
    // compare ray viewZ against sampled scene depth.
    float3 rayPos = Pview;
    float2 rayUV = uv;
    float hitConfidence = 0.0;
    float2 hitUV = uv;
    bool hit = false;

    // Linear march.
    for (int i = 0; i < 64; ++i) {
        if (i >= int(maxSteps)) break;
        rayPos += R * rayStep;
        // Project the view-space ray with the same perspective scales used to
        // reconstruct Pview. The old affine mapping treated view x/y as NDC
        // and caused false triangular reflection hits.
        float2 sampleUV = projectViewPosition(rayPos);
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sampleDepth = SampleSceneDepth(sampleUV);
        if (sampleDepth >= 0.99999) continue;  // sky - no occluder

        float sampleViewZ = ReconstructViewZ(sampleDepth, camNear, camFar);
        float rayZ = rayPos.z;
        float depthDiff = sampleViewZ - rayZ;

        // Hit only after the ray crosses behind the sampled surface. View Z
        // grows away from the camera, so rayZ > sampleViewZ means the ray is
        // behind the geometry. The thickness window suppresses sub-pixel
        // misses without accepting the long in-front segment.
        if (depthDiff < 0.0 && -depthDiff < thickness) {
            hit = true;
            hitUV = sampleUV;
            // Binary refine between previous position and current.
            float3 lo = rayPos - R * rayStep;
            float3 hi = rayPos;
            for (int r = 0; r < 8; ++r) {
                float3 mid = (lo + hi) * 0.5;
                float2 midUV = projectViewPosition(mid);
                if (midUV.x < 0.0 || midUV.x > 1.0 ||
                    midUV.y < 0.0 || midUV.y > 1.0) break;
                float midDepth = SampleSceneDepth(midUV);
                if (midDepth >= 0.99999) { lo = mid; continue; }
                float midViewZ = ReconstructViewZ(midDepth, camNear, camFar);
                float midDiff = midViewZ - mid.z;
                if (midDiff < 0.0 && -midDiff < thickness) {
                    hi = mid;
                    hitUV = midUV;
                } else {
                    lo = mid;
                }
            }
            hitConfidence = 1.0;
            break;
        }
    }

    if (!hit) {
        SsrReflectionOut[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Edge fade: reduce confidence near screen borders.
    float2 borderDist = min(hitUV, 1.0 - hitUV);
    float borderFade = smoothstep(0.0, edgeFade, min(borderDist.x, borderDist.y));
    hitConfidence *= borderFade;

    // Sample reflection color from scene color at the hit UV.
    float3 reflectionColor = SceneColorTexture.SampleLevel(LinearSampler, hitUV, 0).rgb;

    SsrReflectionOut[pixel] = float4(reflectionColor * intensity, hitConfidence);
}

// ===========================================================================
//  Pass 2: SSR composite (fullscreen fragment)
// ===========================================================================
//
// Reads the trace result (SsrReflectionInput) and blends it over the scene
// color (SceneColorTexture) by hit confidence. Runs in HDR linear space
// (before tonemap), so reflections contribute physically to the HDR buffer.

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float2 fullscreenUv(float2 position)
{
    float2 uv = position * 0.5 + 0.5;
    uv.y = uv.y * gSsr.inputUvScaleBias.x + gSsr.inputUvScaleBias.y;
    return uv;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    // Fullscreen triangle: one oversized triangle covers the viewport, so the
    // post pass never needs a vertex buffer / vertex layout (matches the
    // fxaa/ssao/postprocess fullscreen pattern).
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = fullscreenUv(positions[vertexId]);
    return output;
}

[shader("fragment")]
float4 ssrCompositeFragmentMain(VertexOutput input) : SV_Target
{
    float4 sceneColor = SceneColorTexture.Sample(LinearSampler, input.uv);
    float4 reflection = SsrReflectionInput.Sample(LinearSampler, input.uv);

    // Blend reflection over scene color by hit confidence (reflection.a).
    // reflection.rgb is already pre-multiplied by intensity in the trace pass.
    float3 result = lerp(sceneColor.rgb, reflection.rgb, reflection.a);

    return float4(result, sceneColor.a);
}
