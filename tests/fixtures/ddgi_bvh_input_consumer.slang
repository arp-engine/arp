[[vk::binding(0, 0)]] StructuredBuffer<float4> Leaves;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Output;
[[vk::binding(2, 0)]] StructuredBuffer<float4> Nodes;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    Output[0] = Leaves[0];
    Output[1] = Leaves[1];
    Output[2] = Nodes[0];
    Output[3] = Nodes[1];
}
