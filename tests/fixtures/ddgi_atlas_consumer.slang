[[vk::binding(0, 0)]] Texture2D<float4> AtlasInput;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> AtlasValues;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint width, height;
    AtlasInput.GetDimensions(width, height);
    if (thread.x < width && thread.y < height)
        AtlasValues[thread.y * width + thread.x] = AtlasInput.Load(int3(thread.xy, 0));
}
