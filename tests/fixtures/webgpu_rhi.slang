struct WebGpuRhiPushConstants
{
    float4 data0;
    float4 data1;
    float4 data2;
    float4 data3;
};
[[vk::push_constant]] WebGpuRhiPushConstants pc;
struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[shader("vertex")]
VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = pc.data0;
    output.color = float4(1, 1, 1, 1);
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target
{
    return input.color * pc.data0;
}

groupshared float4 computeScratch;

[shader("compute")]
[numthreads(1, 1, 1)]
void csMain(uint3 dispatchId : SV_DispatchThreadID)
{
    computeScratch = pc.data0;
    GroupMemoryBarrierWithGroupSync();
}
