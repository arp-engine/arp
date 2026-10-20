struct FrameConstants {
    float4x4 viewProjection;
    float4 tint;
    float4 values[2];
    float4 unused;
};

struct WebGlPushConstants {
    float4 offset;
    float4 colorAndMix;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> Frame;
[[vk::push_constant]] WebGlPushConstants Push;
[[vk::binding(1, 0)]] Texture2D<float4> TextureA;
[[vk::binding(2, 0)]] SamplerState SamplerA;
[[vk::binding(3, 0)]] Texture2D<float4> TextureB;
[[vk::binding(4, 0)]] SamplerState SamplerB;

struct VertexInput {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")] VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    output.position = mul(Frame.viewProjection, float4(input.position, 1.0)) + Push.offset;
    output.uv = input.uv;
    output.color = input.color * Frame.tint * Frame.values[0] + Frame.values[1];
    return output;
}

    [shader("fragment")] float4 fragmentMain(VertexOutput input)
    : SV_Target0
{
    float4 sampled = lerp(TextureA.Sample(SamplerA, input.uv),
        TextureB.Sample(SamplerB, input.uv),
        Push.colorAndMix.w);
    return sampled * input.color * Frame.tint * float4(Push.colorAndMix.xyz, 1.0);
}
