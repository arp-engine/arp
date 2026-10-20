struct FxaaPushConstants
{
    float2 inverseScreenSize;
    float edgeThreshold;
    float subpixelQuality;
    float2 inputUvScaleBias;
};

[[vk::push_constant]]
FxaaPushConstants gFxaa;

[[vk::binding(0, 1)]] Texture2D<float4> SceneColorTexture : register(t0);
[[vk::binding(1, 1)]] SamplerState SceneColorSampler : register(s1);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float luminance(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

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

float2 sceneColorUv(float2 uv)
{
    return float2(uv.x, uv.y * gFxaa.inputUvScaleBias.x + gFxaa.inputUvScaleBias.y);
}

float4 sampleSceneColor(float2 uv)
{
    return SceneColorTexture.SampleLevel(SceneColorSampler, sceneColorUv(uv), 0.0);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float2 texel = gFxaa.inverseScreenSize;
    float4 center = sampleSceneColor(input.uv);
    float3 north = sampleSceneColor(input.uv + float2(0.0, -texel.y)).rgb;
    float3 south = sampleSceneColor(input.uv + float2(0.0, texel.y)).rgb;
    float3 east = sampleSceneColor(input.uv + float2(texel.x, 0.0)).rgb;
    float3 west = sampleSceneColor(input.uv + float2(-texel.x, 0.0)).rgb;

    float lumaCenter = luminance(center.rgb);
    float lumaMin = min(lumaCenter, min(min(luminance(north), luminance(south)), min(luminance(east), luminance(west))));
    float lumaMax = max(lumaCenter, max(max(luminance(north), luminance(south)), max(luminance(east), luminance(west))));
    float edgeRange = lumaMax - lumaMin;
    if (edgeRange < gFxaa.edgeThreshold) {
        return center;
    }

    float horizontal = abs(luminance(west) - lumaCenter) + abs(luminance(east) - lumaCenter);
    float vertical = abs(luminance(north) - lumaCenter) + abs(luminance(south) - lumaCenter);
    float2 direction = horizontal >= vertical ? float2(0.0, texel.y) : float2(texel.x, 0.0);
    float3 blended = (
        sampleSceneColor(input.uv - direction).rgb
        + sampleSceneColor(input.uv + direction).rgb
        + center.rgb * 2.0) * 0.25;
    return float4(lerp(center.rgb, blended, saturate(gFxaa.subpixelQuality)), center.a);
}
