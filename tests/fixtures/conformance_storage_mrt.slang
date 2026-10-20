// ConformanceStorageMrtWorkload/1: three independent values, no CPU oracle input.
struct StorageValues
{
    float4 color0;
    float4 color1;
    float4 storage;
};
[[vk::binding(0, 0)]] StructuredBuffer<StorageValues> Input;
[[vk::binding(1, 0)]] Texture2D<float4> StorageInput;
struct FragmentOutput
{
    float4 color0 : SV_Target0;
    float4 color1 : SV_Target1;
};
[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    return float4(p, 0, 1);
}
[shader("fragment")]
FragmentOutput fragmentMain(float4 position : SV_Position)
{
    StorageValues value = Input[0];
    FragmentOutput result;
    result.color0 = value.color0;
    result.color1 = 0.5 * (value.color1 + StorageInput.Load(int3(uint2(position.xy), 0)));
    return result;
}
