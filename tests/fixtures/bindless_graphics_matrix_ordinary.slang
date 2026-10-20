// CPU row-memory ABI: current/previous instance transforms and a finite palette.
struct MatrixData
{
    float4x4 current;
    float4x4 previous;
    float4x4 palette[3];
};
[[vk::binding(0, 0)]] ConstantBuffer<MatrixData> Matrices;
struct RevisionData
{
    float4 value;
};
[[vk::binding(1, 0)]] ConstantBuffer<RevisionData> Revision;

struct MatrixVertex
{
    float4 position : SV_Position;
    nointerpolation float4 transformed : TEXCOORD0;
};

[shader("vertex")]
MatrixVertex vertexMain(uint vertexId : SV_VertexID)
{
    MatrixVertex result;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    result.position = float4(position, 0.0, 1.0);
    float4 point = float4(2.0, 3.0, 5.0, 1.0);
    float4 skinned = mul(Matrices.palette[0], point) * 0.25
        + mul(Matrices.palette[2], point) * 0.75;
    result.transformed = mul(Matrices.current, skinned)
        + mul(Matrices.previous, point) + Revision.value;
    return result;
}

float4 matrixFragment(MatrixVertex input, bool injectFault)
{
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    float4 point = float4(float(pixel + 1u), 3.0, 5.0, 1.0);
    float4 transformed = mul(Matrices.current, mul(Matrices.previous, point))
        + mul(Matrices.palette[1], point) + Revision.value;
    bool fault;
    for (uint lane = 0u; lane < 4u; ++lane) {
        Results_store(0u, pixel * 8u + lane, asuint(input.transformed[lane]), fault);
        Results_store(0u, pixel * 8u + 4u + lane, asuint(transformed[lane]), fault);
    }
    if (injectFault) {
        Results_store(1u, 0u, 0xffffffffu, fault);
        Results_store(0u, 32u, 0xffffffffu, fault);
    }
    return float4(0.25, 0.5, 0.75, 1.0);
}

[shader("fragment")]
float4 cleanFragment(MatrixVertex input) : SV_Target0
{
    return matrixFragment(input, false);
}

[shader("fragment")]
float4 faultFragment(MatrixVertex input) : SV_Target0
{
    return matrixFragment(input, true);
}
