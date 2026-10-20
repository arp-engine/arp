struct SkyVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
SkyVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
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
