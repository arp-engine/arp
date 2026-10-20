struct BasicForwardFrameConstants
{
    float4x4 viewProjection;
};

struct BasicForwardPushConstants
{
    float4 baseColor;
    float4 directionIntensity;
    float4 lightColor;
    float4 ambientColor;
    float4 flags;
};

[[vk::binding(0, 0)]] ConstantBuffer<BasicForwardFrameConstants> BasicFrame;
[[vk::binding(1, 0)]] Texture2D<float4> BasicColor;
[[vk::binding(2, 0)]] SamplerState BasicSampler;
[[vk::push_constant]] BasicForwardPushConstants BasicMaterial;

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(BasicFrame.viewProjection, float4(input.position, 1.0));
    output.normal = input.normal;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 albedo = BasicMaterial.baseColor;
    if (BasicMaterial.flags.x > 0.5)
        albedo *= BasicColor.Sample(BasicSampler, input.uv);
    float lambert = 0;
    if (BasicMaterial.flags.z > 0.5)
    {
        float cosine = dot(normalize(input.normal), -BasicMaterial.directionIntensity.xyz);
        lambert = BasicMaterial.flags.y > 0.5 ? abs(cosine) : max(cosine, 0);
    }
    float3 irradiance = BasicMaterial.ambientColor.rgb
        + BasicMaterial.lightColor.rgb * BasicMaterial.directionIntensity.w * lambert;
    float alpha = BasicMaterial.flags.w > 0.5 ? albedo.a : 1.0;
    return float4(albedo.rgb * irradiance * alpha, alpha);
}
