struct SsaoPushConstants
{
    float2 inverseScreenSize;
    float radiusPixels;
    float intensity;
    float depthBias;
    float depthFalloff;
    float2 inputUvScaleBias;
    float2 cameraNearFar;  // {near, far} in view-space units (meters)
};

#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name

[[vk::push_constant]]
SsaoPushConstants gSsao;

[[vk::binding(0, 1)]] ARP_SAMPLED_DEPTH(SceneDepthTexture);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
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

float2 sceneInputUv(float2 uv)
{
    return float2(uv.x, uv.y * gSsao.inputUvScaleBias.x + gSsao.inputUvScaleBias.y);
}

float sampleSceneDepth(float2 uv)
{
    uint width = 0;
    uint height = 0;
    SceneDepthTexture.GetDimensions(width, height);
    float2 clampedUv = saturate(sceneInputUv(uv));
    uint2 pixel = min(
        uint2(clampedUv * float2(width, height)),
        uint2(max(width, 1u) - 1u, max(height, 1u) - 1u));
    return SceneDepthTexture.Load(int3(pixel, 0));
}

// Reconstruct view-space Z (world-space meters) from raw Vulkan depth [0,1].
// The engine uses a left-handed perspective projection with depth mapped to [0,1]:
//   depth = f/(f-n) - (n*f)/((f-n)*viewZ)
// Inverting: viewZ = (n*f) / (f - depth*(f-n))
//
// This is essential: the raw depth buffer is highly non-linear (perspective
// z/w), so adjacent pixels' raw depth deltas are crushed toward 0 near the far
// plane. Comparing raw depths makes AO vanish (the original bug). Working in
// view-space meters restores uniform sensitivity across the depth range.
float reconstructViewZ(float rawDepth, float camNear, float camFar)
{
    float denom = camFar - rawDepth * (camFar - camNear);
    return (camNear * camFar) / max(denom, 0.0001);
}

float computeOcclusion(float2 uv, float centerDepth)
{
    // Skip the sky / far plane: raw depth at the far plane is ~1.0.
    if (centerDepth >= 0.9999) {
        return 0.0;
    }

    const float camNear = gSsao.cameraNearFar.x;
    const float camFar  = gSsao.cameraNearFar.y;
    const float centerViewZ = reconstructViewZ(centerDepth, camNear, camFar);

    // Screen-space sample radius (pixels -> UV).
    float2 texelRadius = gSsao.inverseScreenSize * gSsao.radiusPixels;
    float2 offsets[8] = {
        float2(1.0, 0.0),
        float2(-1.0, 0.0),
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(0.7071, 0.7071),
        float2(-0.7071, 0.7071),
        float2(0.7071, -0.7071),
        float2(-0.7071, -0.7071),
    };

    // depthBias / depthFalloff are now in view-space units (meters and 1/m):
    //   - depthBias: how many meters a neighbour must be *behind* the centre
    //     before it starts occluding (rejects micro height noise).
    //   - depthFalloff: 1/m falloff for both the occluder strength and the
    //     range weight, so AO is localized to a ~1/depthFalloff meter shell.
    float occlusion = 0.0;
    [unroll]
    for (int i = 0; i < 8; ++i) {
        float sampleDepth = sampleSceneDepth(uv + offsets[i] * texelRadius);
        // Skip sky samples so they neither contribute nor cancel occlusion.
        if (sampleDepth >= 0.9999) {
            continue;
        }
        float sampleViewZ = reconstructViewZ(sampleDepth, camNear, camFar);
        // Positive delta means the neighbour is closer to the camera than the
        // centre and can therefore occlude it. Farther samples never darken it.
        float depthDelta = centerViewZ - sampleViewZ;
        float nearbyOccluder = saturate((depthDelta - gSsao.depthBias) * gSsao.depthFalloff);
        float rangeWeight = saturate(1.0 - abs(depthDelta) * gSsao.depthFalloff);
        occlusion += nearbyOccluder * rangeWeight;
    }

    return saturate(occlusion * 0.125 * gSsao.intensity);
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Target
{
    float centerDepth = sampleSceneDepth(input.uv);
    float occlusion = computeOcclusion(input.uv, centerDepth);
    return 1.0 - occlusion;
}
