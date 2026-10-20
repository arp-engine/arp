[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertex << 1u) & 2u, vertex & 2u);
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}
