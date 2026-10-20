// Cold ABI and later numerical probe. All targets consume row-major CPU bytes
// through the compiler's explicit -matrix-layout-row-major option.
struct MatrixData
{
    float4x4 current;
    float4x4 previous;
    float4x4 palette[3];
};
[[vk::binding(0, 0)]] ConstantBuffer<MatrixData> Matrices;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    float4 point = float4(2.0, 3.0, 5.0, 1.0);
    Output[0] = mul(Matrices.current, point);
    Output[1] = mul(Matrices.previous, point);
    Output[2] = mul(Matrices.palette[2], point);
}
