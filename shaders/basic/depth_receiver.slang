struct BasicForwardFrameConstants
{
    float4x4 viewProjection;
};

struct BasicDepthPushConstants
{
    float4 coordinateMap;
};

[[vk::binding(0, 0)]] ConstantBuffer<BasicForwardFrameConstants> BasicFrame;
[[vk::binding(1, 0)]] Texture2D<float4> BasicDepth;
[[vk::binding(2, 0)]] SamplerState BasicSampler;
[[vk::push_constant]] BasicDepthPushConstants BasicDepthMapping;

struct VertexInput
{
    float3 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 depthUv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(BasicFrame.viewProjection, float4(input.position, 1.0));
    output.depthUv = (output.position.xy / output.position.w)
        * BasicDepthMapping.coordinateMap.xy + BasicDepthMapping.coordinateMap.zw;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float depth = BasicDepth.Sample(BasicSampler, input.depthUv).r;
    return float4(depth, depth, depth, 1);
}
