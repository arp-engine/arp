[[vk::binding(0, 0)]] Texture2D<float4> SpriteAtlas;
[[vk::binding(1, 0)]] SamplerState SpriteSampler;

struct VertexInput
{
    float4 origin : TEXCOORD0;
    float4 axisX : TEXCOORD1;
    float4 axisY : TEXCOORD2;
    float4 uv : TEXCOORD3;
    float4 color : TEXCOORD4;
    float4 parameters : TEXCOORD5;
    float2 corner : TEXCOORD6;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float2 corner = input.corner;
    const float4 clipPosition = input.origin + input.axisX * corner.x + input.axisY * corner.y;
    const float2 textureCorner = float2(
        input.parameters.x > 0 ? 1 - corner.x : corner.x,
        input.parameters.y > 0 ? corner.y : 1 - corner.y);

    VertexOutput output;
    output.position = clipPosition;
    output.uv = input.uv.xy + textureCorner * input.uv.zw;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 sampled = SpriteAtlas.Sample(SpriteSampler, input.uv) * input.color;
    return float4(sampled.rgb * sampled.a, sampled.a);
}
