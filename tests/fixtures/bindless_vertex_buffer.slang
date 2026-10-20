[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position {
    bool fault;
    uint value = Inputs_load(vertex % 2u, 0u, fault);
    float2 uv = float2((vertex << 1u) & 2u, vertex & 2u);
    return float4(uv * 2.0 - 1.0, fault ? 0.0 : float(value) * 0.001, 1.0);
}
