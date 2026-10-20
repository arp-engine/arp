[[vk::binding(0, 0)]] RWTexture2D<float4> AtlasOutput;
struct AtlasFillData { float4 value; };
[[vk::binding(1, 0)]] ConstantBuffer<AtlasFillData> AtlasFill;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint width, height;
    AtlasOutput.GetDimensions(width, height);
    if (thread.x < width && thread.y < height)
        AtlasOutput[thread.xy] = AtlasFill.value;
}
