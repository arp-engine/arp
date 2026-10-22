// FogSpatialAmbient/1 Prototype: explicit resource contract.
#define ARP_FOG_SPATIAL_AMBIENT 1
#include "common/volumetric_fog.slang"
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<FogMedium> FogMedia;
[[vk::binding(2,0)]] ARP_SAMPLED_DEPTH(FogShadow);
[[vk::binding(3,0)]] RWStructuredBuffer<FogScatteringRecord> FogScattering;
#include "renderer/ddgi_sampling.slang"
[[vk::binding(4,0)]] Texture2D<float4> FogProbeIrradiance;
[[vk::binding(5,0)]] Texture2D<float4> FogProbeMoments;
[[vk::binding(6,0)]] SamplerState FogProbeSampler;
[[vk::binding(7,0)]] StructuredBuffer<DdgiSamplingProbeState> FogProbeStates;
[[vk::binding(8,0)]] ConstantBuffer<DdgiSamplingConstants> FogProbeConstants;

// FogSpatialAmbient/1 Prototype: six-axis quadrature of normalized cosine
// irradiance approximates spherical mean radiance. A volume has no surface
// normal, so surface bias and normal-wrap weights must not be used here.
float3 fogSpatialAmbient(float3 world, float3 boundaryRadiance)
{
    DdgiSamplingConstants c = FogProbeConstants;
    float3 grid = (world-c.originNormalBias.xyz)/c.spacingViewBias.xyz;
    int3 base = int3(floor(grid));
    float3 f = frac(grid);
    float3 radiance = 0;
    float weightSum = 0;
    float coverage = 0;
    for(uint z=0;z<2;++z) for(uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
        int3 cell = base+int3(x,y,z);
        if(any(cell<0)||any(uint3(cell)>=c.probeCount.xyz)) continue;
        uint3 logical = uint3(cell);
        DdgiSamplingProbeState state = FogProbeStates[ddgiProbeStateIndex(logical,c)];
        if(state.classification!=1u || state.confidence<=0) continue;
        float3 probePosition = c.originNormalBias.xyz+float3(logical)*c.spacingViewBias.xyz+state.offset;
        float3 delta = world-probePosition;
        float distance = length(delta);
        float visibility = 1;
        if(distance>0.0001) {
            float2 moments = FogProbeMoments.SampleLevel(FogProbeSampler,
                ddgiProbeAtlasUv(logical,delta/distance,c,14u,16u),0).xy;
            visibility = ddgiChebyshevVisibility(moments.x,moments.y,distance);
        }
        float trilinear = (x==0 ? 1-f.x:f.x)*(y==0 ? 1-f.y:f.y)*(z==0 ? 1-f.z:f.z);
        coverage += trilinear*state.confidence;
        float weight = trilinear*state.confidence*visibility;
        float3 mean = 0;
        for(uint axis=0;axis<3;++axis) for(uint side=0;side<2;++side) {
            float3 direction = 0;
            direction[axis] = side==0 ? -1.0:1.0;
            mean += FogProbeIrradiance.SampleLevel(FogProbeSampler,
                ddgiProbeAtlasUv(logical,direction,c,6u,8u),0).rgb/6.0;
        }
        radiance += mean*weight;
        weightSum += weight;
    }
    // The declared exterior/invalid-probe boundary condition is the existing
    // mean sky, with continuous trilinear coverage at the probe volume edges.
    return weightSum>0.0001 ? lerp(boundaryRadiance,radiance/weightSum,saturate(coverage)) : boundaryRadiance;
}

#include "renderer/fog_scatter_common.slang"

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID) { fogScatterCell(id); }
