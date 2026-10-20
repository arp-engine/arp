// WoCC Temporal Anti-Aliasing (TAA) resolve shader.
//
// Consumes the per-pixel motion vector (velocity MRT) written by pbr_scene /
// pbr_scene_terrain and the previous frame's resolved color (history) to
// accumulate a sub-pixel-jittered, neighborhood-clipped, temporally-blended
// image. The result replaces the current scene color and feeds the downstream
// post-process chain (bloom -> tonemap -> grade).
//
// Algorithm (standard TAA, PlayDead/Sousa variant):
//   1. Sample current color at the pixel's UV (jittered this frame).
//   2. Reproject: sample history color at (uv - velocity), where velocity is
//      signed `currentUV - previousUV` and zero means no motion. This undoes the
//      per-frame camera/object motion so history aligns with the current pixel.
//   3. Neighborhood clip: compute the 3x3 min/max luma of the CURRENT color
//      and clamp the reprojected history into that AABB (variance-clip variant
//      softens the clamp). This prevents ghosting when history is stale
//      (disocclusion, fast motion).
//   4. Exponential blend: out = lerp(clampedHistory, current, alpha). alpha is
//      the base blend factor, reduced when velocity magnitude is high (fast
//      motion => trust current more, less ghosting) and forced to 1.0 on the
//      first frame / history reset (historyMiss flag in blendParams).
//
// Matrix layout: this resolve shader carries NO matrices - the per-pixel motion
// is already encoded in the velocity buffer by the scene pass. So there are no
// float4x4 push-constant / UBO members and no RowMajor/ColMajor decoration
// concerns (AGENTS.md "Matrix Layout" hard constraint does not apply here).

struct TaaPushConstants
{
    float4 texelSize;
    float4 blendParams;
    float4 inputUvScaleBias;
    float4 validationParams;
};

#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name

// texelSize: xy = (1/width, 1/height) texel size in UV space; zw = (width, height).
// blendParams: x = base blend factor (alpha, ~0.1); y = velocity blend floor
//   (alpha floor for fast-moving pixels, ~0.25); z = velocity scale (how
//   strongly motion reduces history weight); w = historyMiss flag (1.0 = first
//   frame / reset, bypass history blending and output current color).
// inputUvScaleBias: x = Y scale (1.0 identity, -1.0 inverted), y = Y bias
//   (0.0 identity, 1.0 inverted), used to adapt the history/current texture
//   reads to the surface's UV Y convention. zw are the projection depth
//   coefficients A/B for deviceDepth = A + B / viewDepth.

[[vk::push_constant]]
TaaPushConstants gTaa;

// Set 0: input textures + sampler. Bindings follow the engine post-process
// convention (input color @ 0, sampler @ 1) extended for TAA's extra inputs.
// The reflection generator assigns these by name, so the [[vk::binding]]
// attributes MUST match the hardcoded set/binding or the runtime descriptor
// writes target the wrong slots.
[[vk::binding(0, 0)]] Texture2D<float4> SceneColorTexture : register(t0);
[[vk::binding(1, 0)]] Texture2D<float4> HistoryColorTexture : register(t1);
[[vk::binding(2, 0)]] Texture2D<float2> VelocityTexture : register(t2);
[[vk::binding(3, 0)]] ARP_SAMPLED_DEPTH(DepthTexture);
[[vk::binding(4, 0)]] Texture2D<float>  ReactiveTexture : register(t4);
[[vk::binding(5, 0)]] ARP_SAMPLED_DEPTH(HistoryDepthTexture);
[[vk::binding(6, 0)]] Texture2D<float2> HistoryMomentsTexture : register(t6);
[[vk::binding(7, 0)]] SamplerState LinearClampSampler : register(s7);

struct TaaOutput { float4 color : SV_Target0; float2 moments : SV_Target1; };

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    // Fullscreen triangle: one oversized triangle covers the viewport, so the
    // resolve pass never needs a vertex buffer / vertex layout (matches the
    // fxaa/ssao/post fullscreen pattern).
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };
    float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0),
    };

    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = uvs[vertexId];
    return output;
}

float luminance(float3 color)
{
    // Match the Y channel used by rgbToYCoCg(). Temporal moments and clip
    // bounds must measure the same signal or their interval can invert.
    return dot(color, float3(0.25, 0.5, 0.25));
}

float2 sceneUv(float2 uv)
{
    return float2(uv.x, uv.y * gTaa.inputUvScaleBias.x + gTaa.inputUvScaleBias.y);
}

float4 sampleSceneColor(float2 uv)
{
    return SceneColorTexture.SampleLevel(LinearClampSampler, sceneUv(uv), 0.0);
}

float4 sampleHistoryColor(float2 uv)
{
    return HistoryColorTexture.SampleLevel(LinearClampSampler, sceneUv(uv), 0.0);
}

uint2 clampTexel(int2 texel)
{
    const int2 extent = int2(gTaa.texelSize.zw);
    return uint2(clamp(texel, int2(0, 0), extent - int2(1, 1)));
}

uint2 depthTexel(float2 uv)
{
    const uint2 extent = uint2(gTaa.texelSize.zw);
    return min(uint2(sceneUv(uv) * float2(extent)), extent - uint2(1, 1));
}

// Depth attachments use textureLoad rather than the color sampler. This keeps
// color/history filtering intact while satisfying WebGPU's depth binding type.
float sampleDepth(float2 uv) { return DepthTexture.Load(int3(depthTexel(uv), 0)); }
float2 sampleVelocity(float2 uv) { return VelocityTexture.Load(int3(depthTexel(uv), 0)); }
float sampleReactive(float2 uv) { return ReactiveTexture.Load(int3(depthTexel(uv), 0)); }
float sampleHistoryDepthAt(uint2 texel) { return HistoryDepthTexture.Load(int3(texel, 0)); }
// Rg16Float is unfilterable on WebGPU. Moments are only used for variance
// clipping, so fetch the exact texel and keep the filtering sampler reserved
// for the color/velocity/reactive inputs.
float2 sampleHistoryMoments(float2 uv) { return HistoryMomentsTexture.Load(int3(depthTexel(uv), 0)); }
float3 rgbToYCoCg(float3 c) { float y = dot(c, float3(0.25, 0.5, 0.25)); return float3(y, c.r - c.b, c.g - y); }
float3 yCoCgToRgb(float3 c) { return float3(c.x + 0.5 * c.y - c.z, c.x + c.z, c.x - 0.5 * c.y - c.z); }

float linearViewDepth(float deviceDepth)
{
    return abs(gTaa.inputUvScaleBias.w)
        / max(abs(deviceDepth - gTaa.inputUvScaleBias.z), 1e-7);
}

float2 sampleDilatedVelocity(float2 uv)
{
    const int2 center = int2(depthTexel(uv));
    float closestDepth = 2.0;
    float2 closestVelocity = float2(0.0, 0.0);
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const uint2 texel = clampTexel(center + int2(x, y));
            const float candidateDepth = DepthTexture.Load(int3(texel, 0));
            if (candidateDepth < closestDepth) {
                closestDepth = candidateDepth;
                closestVelocity = VelocityTexture.Load(int3(texel, 0));
            }
        }
    }
    return closestVelocity;
}

float sampleDilatedReactive(float2 uv)
{
    const int2 center = int2(depthTexel(uv));
    float reactive = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            reactive = max(reactive,
                ReactiveTexture.Load(int3(clampTexel(center + int2(x, y)), 0)));
        }
    }
    return reactive;
}

bool historyDepthMatches(float2 uv, float2 historyUv)
{
    const float currentDepth = linearViewDepth(sampleDepth(uv));
    const int2 historyCenter = int2(depthTexel(historyUv));
    float closestRelativeDifference = 1e20;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float historyDepth = linearViewDepth(sampleHistoryDepthAt(
                clampTexel(historyCenter + int2(x, y))));
            const float relativeDifference = abs(currentDepth - historyDepth)
                / max(min(currentDepth, historyDepth), 0.001);
            closestRelativeDifference = min(closestRelativeDifference, relativeDifference);
        }
    }
    return closestRelativeDifference <= gTaa.validationParams.x;
}

[shader("fragment")]
TaaOutput fragmentMain(VertexOutput input)
{
    const float2 uv = input.uv;
    const float2 texel = gTaa.texelSize.xy;

    float4 currentColor = sampleSceneColor(uv);
    const bool useNeighborhoodValidation = gTaa.validationParams.w > 0.5;
    float2 velocity = useNeighborhoodValidation
        ? sampleDilatedVelocity(uv) : sampleVelocity(uv);
    TaaOutput output;
    float lumaCurrent = luminance(currentColor.rgb);
    // HistoryMoments stores temporal mean + variance, not raw first/second
    // moments. Direct variance avoids catastrophic cancellation in RG16F.
    output.moments = float2(lumaCurrent, 0.0);

    // History miss (first frame / reset): bypass accumulation, output current.
    if (gTaa.blendParams.w > 0.5) {
        output.color = currentColor;
        return output;
    }

    // velocity is currentUV - previousUV, so history is sampled at uv - velocity.
    float2 historyUv = uv - velocity;
    if (!all(historyUv >= 0.0.xx) || !all(historyUv <= 1.0.xx)) {
        output.color = currentColor;
        return output;
    }
    float4 historyColor = sampleHistoryColor(historyUv);
    float reactive = saturate((useNeighborhoodValidation
        ? sampleDilatedReactive(uv) : sampleReactive(uv)) * gTaa.validationParams.y);
    if (useNeighborhoodValidation && !historyDepthMatches(uv, historyUv)) {
        output.color = currentColor;
        return output;
    }

    // 3x3 neighborhood of the CURRENT color (clamp history into this AABB to
    // reject stale history on disocclusion / fast motion).
    float3 currentYCoCg = rgbToYCoCg(currentColor.rgb);
    float3 colorMin = currentYCoCg;
    float3 colorMax = currentYCoCg;
    float lumaMin = lumaCurrent;
    float lumaMax = lumaCurrent;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            float4 neighbor = sampleSceneColor(uv + float2(x, y) * texel);
            float3 neighborYCoCg = rgbToYCoCg(neighbor.rgb);
            colorMin = min(colorMin, neighborYCoCg);
            colorMax = max(colorMax, neighborYCoCg);
            float lumaNeighbor = luminance(neighbor.rgb);
            lumaMin = min(lumaMin, lumaNeighbor);
            lumaMax = max(lumaMax, lumaNeighbor);
        }
    }

    // Variance-clip soften: expand the clamp AABB slightly by the neighborhood
    // luma spread so the history isn't clipped too aggressively at soft edges
    // (reduces flicker on sub-pixel detail). The 0.5 factor is the standard
    // PlayDead soft-clip weight.
    float lumaSpread = max(lumaMax - lumaMin, 0.0001);
    float3 clipMin = colorMin - float3(lumaSpread * 0.5, 0.0, 0.0);
    float3 clipMax = colorMax + float3(lumaSpread * 0.5, 0.0, 0.0);
    float2 historyMoments = sampleHistoryMoments(historyUv);
    if (gTaa.validationParams.w > 1.5) {
        float sigma = sqrt(max(historyMoments.y, 0.0)) * 1.5;
        float momentClipMin = max(clipMin.x, historyMoments.x - sigma);
        float momentClipMax = min(clipMax.x, historyMoments.x + sigma);
        // Disocclusion can place the temporal interval outside the current
        // neighborhood. Never pass an inverted interval to clamp(); retain
        // the valid neighborhood bounds and let depth/reactive logic reject.
        if (momentClipMin <= momentClipMax) {
            clipMin.x = momentClipMin;
            clipMax.x = momentClipMax;
        }
    }
    float3 clampedHistory = yCoCgToRgb(clamp(rgbToYCoCg(historyColor.rgb), clipMin, clipMax));

    // Blend factor: base alpha, reduced when the pixel moved fast (high
    // velocity magnitude => trust current more to avoid ghosting.
    float motion = length(velocity * gTaa.texelSize.zw);
    float velocityWeight = saturate(motion * gTaa.blendParams.z);
    float alpha = lerp(gTaa.blendParams.x, gTaa.blendParams.y, velocityWeight);
    alpha = max(alpha, reactive);
    alpha = saturate(alpha);

    // Numerically stable exponentially weighted variance update. The previous
    // mean/variance are reprojected with the same velocity as history color.
    float momentDelta = lumaCurrent - historyMoments.x;
    float temporalMean = lerp(historyMoments.x, lumaCurrent, alpha);
    float temporalVariance = (1.0 - alpha)
        * (max(historyMoments.y, 0.0) + alpha * momentDelta * momentDelta);
    output.moments = float2(temporalMean, temporalVariance);

    float3 resolved = lerp(clampedHistory, currentColor.rgb, alpha);
    if (gTaa.validationParams.w > 0.5) {
        float3 neighbors = 0.25 * (sampleSceneColor(uv + float2(texel.x, 0)).rgb
            + sampleSceneColor(uv - float2(texel.x, 0)).rgb
            + sampleSceneColor(uv + float2(0, texel.y)).rgb
            + sampleSceneColor(uv - float2(0, texel.y)).rgb);
        resolved = lerp(resolved, currentColor.rgb + (currentColor.rgb - neighbors),
            gTaa.validationParams.z * (1.0 - reactive));
    }
    output.color = float4(resolved, currentColor.a);
    return output;
}
