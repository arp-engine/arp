// Scene ABI probe: current/previous transforms and a finite skinning palette.
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

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    float4 point = float4(2.0, 3.0, 5.0, 1.0);
    float4 transformed = mul(Matrices.current, point) + mul(Matrices.previous, point)
        + mul(Matrices.palette[2], point) + Revision.value;
    bool fault;
    Results_store(0u, 0u, asuint(transformed.x), fault);
}
