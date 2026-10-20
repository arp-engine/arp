[[vk::binding(7, 0)]] Texture2D<float4> Inputs[3] : register(t7);
[[vk::binding(2, 0)]] Texture2D<float4> Scalar : register(t2);
[[vk::binding(0, 1)]] RWStructuredBuffer<float4> Output;

[shader("compute")]
[numthreads(5, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint element = thread.x % 3;
    Output[thread.y * 30 + thread.x] = float4(
        Inputs[element].Load(int3(0, 0, 0)).rgb,
        Scalar.Load(int3(0, 0, 0)).r);
}
