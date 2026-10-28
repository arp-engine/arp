#include "renderer/cloud_common.slang"

// CloudAssetSlice/1 diagnostic: explicit linear/repeat samples of a validated
// CloudAsset/1 payload. No generation, conversion, scene shading or history.
struct CloudAssetSliceParameters
{
    uint4 extentLayout;
    float4 coordinates;
    uint4 sampling;
};
[[vk::binding(0,0)]] ConstantBuffer<CloudAssetSliceParameters> SliceParameters;
[[vk::binding(1,0)]] StructuredBuffer<float4> CloudDensityCells;
[[vk::binding(3,0)]] RWStructuredBuffer<float4> CloudAssetSlice;

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    CloudAssetSliceParameters p = SliceParameters;
    if (any(id.xy >= p.extentLayout.xy)) return;
    float3 uvw = float3(p.coordinates.xy + float2(id.xy) * p.coordinates.z,
        p.coordinates.w);
    CloudAssetSlice[id.y * p.extentLayout.x + id.x] = cloudSampleCells(
        CloudDensityCells, uvw, p.extentLayout.z, p.extentLayout.w, p.sampling.x != 0);
}
