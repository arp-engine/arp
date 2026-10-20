// WoCC post-FX shader (fog + god rays).
//
// Standalone shader module - separate from post.slang - so the depth-texture
// binding (set 0, binding 3) does not pollute the descriptor layout of the
// existing bloom/tonemap/grade pipelines.
//
// Push-constant struct mirrors WoccPostPushConstants exactly.
//
// UV convention: uv.y=0 is top of screen (sky/zenith), uv.y=1 is bottom
// (ground).  This matches the fullscreen-triangle vertex layout and the
// Y-flipped Vulkan viewport used by the engine.

struct WoccPostFxPushConstants
{
    float4 fogColorDensity;
    float4 fogStartEnd;
    float4 sunScreenPosIntensity;
    float4 godRayDecaySamples;
    float4 inverseViewProjectionY;
    float4 inverseViewProjectionW;
    float4 cameraHeightFogParams;
};

[[vk::push_constant]]
WoccPostFxPushConstants gPost;

[[vk::binding(0, 0)]] Texture2D<float4> PostProcessInputTexture : register(t0);
[[vk::binding(1, 0)]] SamplerState PostProcessInputSampler : register(s1);
[[vk::binding(3, 0)]] Texture2D<float> PostSceneDepthTexture : register(t3);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    float2 uvs[3] = { float2(0.0, 0.0), float2(2.0, 0.0), float2(0.0, 2.0) };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = uvs[vertexId];
    return output;
}

float4 sampleInput(float2 uv)
{
    return PostProcessInputTexture.Sample(PostProcessInputSampler, uv);
}

// Reconstruct view-space Z (world-space meters) from raw Vulkan depth [0,1].
// The engine uses a left-handed perspective projection with depth mapped to [0,1]:
//   depth = f/(f-n) - (n*f)/((f-n)*viewZ)
// Inverting: viewZ = (n*f) / (f - depth*(f-n))
float ReconstructViewZ(float rawDepth, float camNear, float camFar)
{
    float denom = camFar - rawDepth * (camFar - camNear);
    return (camNear * camFar) / max(denom, 0.0001);
}

float ReconstructWorldY(float2 uv, float rawDepth)
{
    float4 clip = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    float worldY = dot(gPost.inverseViewProjectionY, clip);
    float worldW = dot(gPost.inverseViewProjectionW, clip);
    return worldY / max(abs(worldW), 0.0001);
}

// ===========================================================================
//  Fog — natural atmospheric depth fog + horizon haze + sun-direction tint
// ===========================================================================
//
// Three components blended together:
//
//  1. Distance fog (Beer-Lambert exponential)
//     The primary driver.  pure 1-exp(-density*dist) — no broken screen-space
//     height multiplier.  Gives a smooth, gradual onset with no "fog wall."
//
//  2. Sky horizon haze
//     Sky pixels (rawDepth >= 0.995) are blended toward the fog colour with
//     a band centred on the screen-space horizon (uv.y ~ 0.5).  This removes
//     the hard horizon cut-off and creates the "distance fades into sky"
//     effect that real atmosphere produces.
//
//  3. Sun-direction warm tint (Mie scattering approximation)
//     Fog near the projected sun position picks up a warm sunlight colour,
//     simulating forward-scattered Mie radiation.  Subtle (~0.4 lerp factor).
//
// The pass runs in HDR linear space (before tonemap).  Fog colour should be
// specified in HDR linear — values < 1.0 survive ACES without distortion.
[shader("fragment")]
float4 woccFogFragmentMain(VertexOutput input) : SV_Target
{
    float4 color = sampleInput(input.uv);
    float fogDensity = gPost.fogColorDensity.w;
    if (fogDensity <= 0.0) return color;

    float rawDepth = PostSceneDepthTexture.Sample(PostProcessInputSampler, input.uv).r;
    float camNear  = gPost.fogStartEnd.z;
    float camFar   = gPost.fogStartEnd.w;
    float fogNear  = gPost.fogStartEnd.x;
    float3 fogColor = gPost.fogColorDensity.rgb;

    // Sun-direction warm tint (Mie forward-scatter approximation).
    // Fog colour shifts toward warm sunlight near the projected sun position.
    float2 sunUV = gPost.sunScreenPosIntensity.xy;
    float sunDist = length(input.uv - sunUV);
    float sunInfluence = exp(-sunDist * sunDist * 3.0) * 0.25;
    float3 tintedFogColor = lerp(fogColor, float3(1.0, 0.92, 0.75), sunInfluence);

    // --- Sky pixels: horizon haze ----------------------------------------
    // Sky dome is rendered with depthWrite=false, so sky pixels retain the
    // cleared depth of 1.0.  The threshold must be VERY close to 1.0 because
    // Vulkan [0,1] depth is non-linear: with near=0.1, far=1000, depth=0.995
    // corresponds to only ~20m!  Using 0.99999 (~909m) ensures distant terrain
    // up to ~900m is still treated as scene geometry, not sky.
    if (rawDepth >= 0.99999) {
        float fromHorizon = abs(input.uv.y - 0.5);
        float horizonBand = 1.0 - smoothstep(0.0, 0.40, fromHorizon);
        float skyFogFactor = horizonBand * 0.4;
        return float4(lerp(color.rgb, tintedFogColor, skyFogFactor), color.a);
    }

    // --- Scene pixels: distance fog --------------------------------------
    float viewZ = ReconstructViewZ(rawDepth, camNear, camFar);

    // Smooth onset: gentle ramp from 0 at fogNear instead of a hard wall.
    // The exponential itself already provides gradual onset; the max() just
    // shifts the start point so nearby objects (< fogNear) stay clear.
    float dist = max(viewZ - fogNear, 0.0);

    // Pure Beer-Lambert exponential fog.
    float fogFactor = 1.0 - exp(-fogDensity * dist);

    // Altitude modulation: slightly less fog at high altitudes (mountain
    // ridges / sky direction), slightly more in valleys / ground plane.
    // A mild 0.92x-1.0x modifier - the distance term is the primary driver.
    // Driven by cameraHeightFogParams (cameraY, baseHeight, heightFalloff,
    // heightMaxClamp) populated from WoccPostFxCarrier on the CPU side.
    float worldY = ReconstructWorldY(input.uv, rawDepth);
    float cameraY = gPost.cameraHeightFogParams.x;
    float baseHeight = gPost.cameraHeightFogParams.y;
    float heightFalloff = max(gPost.cameraHeightFogParams.z, 0.0001);
    float cameraDensity = exp(-max(cameraY - baseHeight, 0.0) * heightFalloff);
    float surfaceDensity = exp(-max(worldY - baseHeight, 0.0) * heightFalloff);
    float heightDensity = 0.5 * (cameraDensity + surfaceDensity);
    fogFactor = min(saturate(fogFactor * heightDensity), gPost.cameraHeightFogParams.w);

    return float4(lerp(color.rgb, tintedFogColor, fogFactor), color.a);
}

// ===========================================================================
//  God Rays — screen-space radial blur with depth occlusion
// ===========================================================================
//
// Vos / Vlachos radial-blur technique:
//   For each pixel P, march N samples along the ray from P toward the sun
//   position S.  At each sample, read the depth texture to determine whether
//   the point is open sky (light passes through) or occluded by geometry.
//   Accumulate "openness" with exponential decay — samples nearer to P
//   weigh more than samples near S.
//
// Depth occlusion:
//   Sky samples (depth >= 0.995) contribute full illumination.
//   Geometry samples contribute a small fraction (0.12) to simulate light
//   scattering in the air between the camera and the occluder — this prevents
//   a hard binary cut and produces soft shaft edges.
//
// Blend mode: screen blend  (color + contribution * (1 - color))
//   Prevents highlight blowout that pure additive causes on bright skies.
//
// The pass runs in LDR space (after tonemap).
[shader("fragment")]
float4 woccGodRayFragmentMain(VertexOutput input) : SV_Target
{
    float4 color = sampleInput(input.uv);
    float intensity = gPost.sunScreenPosIntensity.z;
    if (intensity <= 0.0) return color;

    float2 sunUV = gPost.sunScreenPosIntensity.xy;

    // Soft fade when the sun is near or past the screen edges.
    // No hard cutoff — rays fade gracefully as the sun leaves the frame.
    float2 fadeLow  = smoothstep(float2(-0.35, -0.35), float2(0.05, 0.05), sunUV);
    float2 fadeHigh = 1.0 - smoothstep(float2(0.95, 0.95), float2(1.35, 1.35), sunUV);
    float screenFade = fadeLow.x * fadeLow.y * fadeHigh.x * fadeHigh.y;
    if (screenFade < 0.001) return color;

    float decay = gPost.godRayDecaySamples.x;
    int numSteps = int(clamp(gPost.godRayDecaySamples.y, 4.0, 32.0));

    // March from the current pixel toward the sun position.
    float2 rayToSun = sunUV - input.uv;
    float rayLength = length(rayToSun);
    float maxDistance = max(gPost.godRayDecaySamples.z, 0.001);
    float2 delta = rayToSun * (min(rayLength, maxDistance) / max(rayLength, 0.0001)) / float(numSteps);
    float2 samplePos = input.uv;

    float illumination = 0.0;
    float totalWeight = 0.0;
    float currentDecay = 1.0;

    // Fixed upper bound (32) with early exit — the compiler unrolls to a
    // bounded loop and the driver can predicate the break.
    for (int i = 0; i < 32; ++i)
    {
        if (i >= numSteps) break;
        samplePos += delta;
        currentDecay *= decay;

        // Skip samples outside the screen.
        if (samplePos.x < 0.0 || samplePos.x > 1.0 ||
            samplePos.y < 0.0 || samplePos.y > 1.0)
            break;

        float sampleDepth = PostSceneDepthTexture.Sample(PostProcessInputSampler, samplePos).r;

        // Depth-based occlusion:
        //   Sky (depth >= 0.995) → full light transmission
        //   Geometry             → reduced (0.12) — air scatter near edges
        float sampleViewZ = ReconstructViewZ(sampleDepth, gPost.fogStartEnd.z, gPost.fogStartEnd.w);
        float lightTransmission = (sampleViewZ >= gPost.fogStartEnd.w * 0.9) ? 1.0 : 0.0;

        illumination += lightTransmission * currentDecay;
        totalWeight  += currentDecay;
    }

    // Normalise so the result is independent of step count.
    if (totalWeight > 0.0)
        illumination /= totalWeight;

    illumination *= intensity * screenFade;

    // Warm light-shaft colour, tinted slightly by the atmospheric fog colour
    // so shafts pick up the ambient haze tint.
    float3 shaftColor = lerp(
        float3(1.0, 0.95, 0.80),    // warm sunlight
        gPost.fogColorDensity.rgb,   // atmospheric fog tint
        0.25
    );

    // Screen blend: natural highlight response, prevents blowout on bright
    // sky pixels.  result = color + contribution * (1 - color)
    float3 contribution = shaftColor * illumination;
    float3 result = color.rgb + contribution * (1.0 - color.rgb);

    return float4(result, color.a);
}
