// Small cross-target shader used by the WebGPU smoke contract.
// Keep the compute and fullscreen fragment entry points in one artifact so
// reflection validates the same resource declarations for both paths.

[[vk::binding(0, 0)]] RWTexture2D<float4> ComputeSmokeOutput;
[[vk::binding(0, 1)]] Texture2D<float4> ComputeSmokeInput;
[[vk::binding(1, 1)]] SamplerState ComputeSmokeSampler;

[numthreads(8, 8, 1)]
[shader("compute")]
void computeSmokeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    ComputeSmokeOutput.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    uint2 pixel = dispatchThreadId.xy;
    float2 uv = (float2(pixel) + float2(0.5, 0.5))
        / float2(width, height);
    float red = pixel.x < width / 2 ? 0.95 : 0.10;
    float green = pixel.y < height / 2 ? 0.25 : 0.85;
    ComputeSmokeOutput[pixel] = float4(red, green, 0.18 + uv.x * 0.45, 1.0);
}

struct SmokeVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
SmokeVertexOutput computeSmokeVertexMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)
    };
    float2 uvs[3] = {
        float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0)
    };
    SmokeVertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = uvs[vertexId];
    return output;
}

[shader("fragment")]
float4 computeSmokeFragmentMain(SmokeVertexOutput input) : SV_Target
{
    return ComputeSmokeInput.SampleLevel(ComputeSmokeSampler, input.uv, 0.0);
}
