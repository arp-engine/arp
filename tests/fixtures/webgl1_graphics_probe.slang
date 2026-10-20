struct FrameConstants {
    float4x4 viewProjection;
    float4 tint;
    float4 values[2];
};

struct DrawConstants {
    float4 offset;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> frameConstants;
[[vk::push_constant]] DrawConstants drawConstants;
[[vk::binding(1, 0)]] Texture2D<float4> colorTexture;
[[vk::binding(2, 0)]] SamplerState colorSampler;

struct VertexInput {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vsMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(frameConstants.viewProjection,
        float4(input.position, 1.0)) + drawConstants.offset;
    output.uv = input.uv;
    output.color = frameConstants.tint * frameConstants.values[0]
        + frameConstants.values[1];
    return output;
}

[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target
{
    return colorTexture.Sample(colorSampler, input.uv) * input.color;
}
