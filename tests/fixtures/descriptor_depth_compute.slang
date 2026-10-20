#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(Inputs)[3];
[[vk::binding(0, 1)]] RWStructuredBuffer<float4> Output;

[shader("compute")]
[numthreads(5, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    float depth = Inputs[thread.x % 3].Load(int3(0, 0, 0));
    Output[thread.y * 30 + thread.x] = float4(depth, depth, depth, 1);
}
