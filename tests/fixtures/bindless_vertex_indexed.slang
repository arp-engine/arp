[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    bool fault;
    float4 sampled = VertexImages_load(vertex % 2u, int3(0, 0, 0), fault);
    uint2 dimensions = VertexImages_query(vertex % 2u, fault);
    float2 uv = float2((vertex << 1u) & 2u, vertex & 2u);
    float offset = fault ? 0.0 : sampled.r / max(float(dimensions.x), 1.0) * 0.01;
    return float4(uv.x * 2.0 - 1.0 + offset, uv.y * 2.0 - 1.0, 0.0, 1.0);
}
