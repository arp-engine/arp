#include "common/volumetric_fog.slang"
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<FogMedium> FogMedia;
[[vk::binding(2,0)]] ARP_SAMPLED_DEPTH(FogShadow);
[[vk::binding(3,0)]] RWStructuredBuffer<FogScatteringRecord> FogScattering;
#include "renderer/fog_scatter_common.slang"

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID) { fogScatterCell(id); }
