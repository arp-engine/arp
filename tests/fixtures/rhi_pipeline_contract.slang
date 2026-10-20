struct FrameConstants
{
    float4 value;
};

struct RhiPipelinePushConstants
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

[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> frameConstants;
[[vk::binding(0, 1)]] Texture2D<float4> materialTexture;
[[vk::push_constant]] RhiPipelinePushConstants pc;

groupshared uint computeScratch;

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = frameConstants.value
        + pc.data0;
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target
{
    return materialTexture.Load(int3(0, 0, 0))
        + pc.data0;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void csMain(uint3 dispatchId : SV_DispatchThreadID)
{
    computeScratch = asuint(pc.data0.x);
    GroupMemoryBarrierWithGroupSync();
}
