#include "common/volumetric_fog.slang"
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<FogMedium> FogMedia;
[[vk::binding(2,0)]] ARP_SAMPLED_DEPTH(FogShadow);
[[vk::binding(3,0)]] RWStructuredBuffer<float4> FogScattering;
float fogCascadeVisibility(float3 world, uint cascade, FogConstants p)
{
    float4 clip=mul(p.shadowMatrices[cascade],float4(world,1));
    if(clip.w<=0) return 1;
    float3 ndc=clip.xyz/clip.w;
    float2 uv=ndc.xy*0.5+0.5;
    if(any(uv<0)||any(uv>1)||ndc.z<0||ndc.z>1) return 1;
    float4 rect=p.shadowRects[cascade];
    uv=rect.xy+uv*rect.zw;
    uint width,height;
    FogShadow.GetDimensions(width,height);
    float2 texel=uv*float2(width,height)-0.5;
    int2 base=int2(floor(texel));
    float2 f=frac(texel);
    int2 lower=int2(rect.xy*float2(width,height));
    int2 upper=int2((rect.xy+rect.zw)*float2(width,height))-1;
    float visibility=0;
    // Manual bilinear comparison is identical for sampled depth on all targets.
    // Clamp EACH tap to this cascade, never to an adjacent atlas strip.
    for(uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
        int2 pixel=clamp(base+int2(x,y),lower,upper);
        float lit=ndc.z-p.lightColor.w<=FogShadow.Load(int3(pixel,0)) ? 1.0 : 0.0;
        visibility+=lit*(x==0 ? 1-f.x : f.x)*(y==0 ? 1-f.y : f.y);
    }
    return visibility;
}
float fogVisibility(float3 world, float depth, FogConstants p)
{
    if(p.counts.z==0) return 1;
    float selection=p.shadowSelection.w>0.5 ? length(world-p.shadowSelection.xyz) : depth;
    if(selection>p.cascadeEnds[p.counts.z-1]) return 1;
    uint cascade=0;
    while(cascade+1<p.counts.z && selection>p.cascadeEnds[cascade]) ++cascade;
    float visibility=fogCascadeVisibility(world,cascade,p);
    // Overlap the final tenth of each declared interval, including the far fade.
    // This avoids treating a cascade boundary as an opaque sheet in the medium.
    float begin=cascade==0 ? 0 : p.cascadeEnds[cascade-1];
    float end=p.cascadeEnds[cascade];
    float blend=smoothstep(lerp(begin,end,0.9),end,selection);
    if(blend>0) {
        float next=cascade+1<p.counts.z ? fogCascadeVisibility(world,cascade+1,p) : 1;
        visibility=lerp(visibility,next,blend);
    }
    return visibility;
}
float fogCellVisibility(uint3 id, FogConstants p)
{
    if(p.counts.z==0) return 1;
    float nearDepth=fogSliceDepth(float(id.z),p);
    float farDepth=fogSliceDepth(float(id.z+1),p);
    float visibility=0;
    // Eight strata across XY and physical ray depth estimate canopy coverage BEFORE the
    // low-resolution volume is integrated. A binary center tap aliases thin
    // leaves into large dark cells; post-color blur cannot recover coverage.
    float3 jitter=float3(p.extentJitter.zw,p.cameraPosition.w);
    for(uint z=0;z<2;++z) for(uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
        float3 f=(float3(x,y,z)+jitter)*0.5;
        float2 uv=(float2(id.xy)+f.xy)/float2(p.grid.xy);
        float depth=lerp(nearDepth,farDepth,f.z);
        visibility+=fogVisibility(fogWorldAt(uv,depth,p),depth,p);
    }
    return visibility*0.125;
}
[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID)
{
    FogConstants p=FogParams;
    if(any(id>=p.grid.xyz)) return;
    float2 uv=fogSampleUv(id.xy,p);
    float depth=fogSliceDepth(float(id.z)+p.cameraPosition.w,p);
    float3 world=fogWorldAt(uv,depth,p);
    float3 ray=normalize(fogWorldAt(uv,p.depthTime.y,p)-fogWorldAt(uv,p.depthTime.x,p));
    float phase=fogPhase(dot(ray,-p.lightDirectionIntensity.xyz),p.globalColorAnisotropy.w);
    FogMedium m=FogMedia[fogIndex(id,p.grid.xyz)];
    float3 source=m.coefficients.xyz * p.lightColor.xyz * p.lightDirectionIntensity.w
        * phase * fogCellVisibility(id,p);
    // Integral of a normalized phase against isotropic incident radiance.
    // Directional shadow visibility applies only to the sun source above.
    source += m.coefficients.xyz * p.ambientRadiance.xyz;
    FogScattering[fogIndex(id,p.grid.xyz)]=float4(source,m.coefficients.w);
}
