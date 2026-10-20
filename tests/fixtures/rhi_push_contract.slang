struct RhiPushContractPushConstants
{
    float4 data0;
    float4 data1;
    float4 data2;
    float4 data3;
    float4 data4;
    float4 data5;
    float4 data6;
    float4 data7;
};

[[vk::push_constant]] RhiPushContractPushConstants pc;
groupshared float4 computeScratch;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = pc.data0;
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target
{
    return pc.data0;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void csMain(uint3 dispatchId : SV_DispatchThreadID)
{
    computeScratch = pc.data0;
    GroupMemoryBarrierWithGroupSync();
}
