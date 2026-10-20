struct SpritePushConstants
{
    float4 projection;
};
[[vk::push_constant]] SpritePushConstants gSprite;
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
};
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint vertexId : SV_VertexID)
{
    float2 corners[6] = {float2(0,0),float2(1,0),float2(1,1),float2(0,0),float2(1,1),float2(0,1)};
    float2 c=corners[vertexId];
    float2 world=input.origin.xy+input.axisX.xy*c.x+input.axisY.xy*c.y;
    VertexOutput output;
    output.position=float4(world*gSprite.projection.xy+gSprite.projection.zw,0,1);
    float2 textureCorner=float2(input.parameters.x>0 ? 1-c.x:c.x,input.parameters.y>0 ? c.y:1-c.y);
    output.uv=input.uv.xy+textureCorner*input.uv.zw;
    output.color=input.color;
    return output;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 sampled=SpriteAtlas.Sample(SpriteSampler,input.uv)*input.color;
    return float4(sampled.rgb*sampled.a,sampled.a);
}

struct ExpandedVertexInput
{
    float2 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
};

[shader("vertex")]
VertexOutput vertexMainExpanded(ExpandedVertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * gSprite.projection.xy + gSprite.projection.zw, 0, 1);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
