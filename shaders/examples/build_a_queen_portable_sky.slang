struct SkyVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct VertexInput
{
    float2 corner : TEXCOORD0;
};
[shader("vertex")]
SkyVertexOutput vertexMain(VertexInput input)
{
    const float2 uv = input.corner;
    SkyVertexOutput output;
    output.position = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    output.uv = uv;
    return output;
}
#include "queen_sky_color.slang"
[shader("fragment")]
float4 fragmentMain(SkyVertexOutput input) : SV_Target
{
    return queenSkyColor(input.uv);
}
