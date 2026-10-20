// Independent direct-bound IBL sampler reference. Snapshot marker stores are
// receipt-only; no indexed result participates in any sampled value below.
[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> DirectResults;
[[vk::binding(0, 2)]] TextureCube<float4> Environment;
[[vk::binding(1, 2)]] Texture2D<float4> BrdfLut;
[[vk::binding(2, 2)]] SamplerState LinearSampler;
[[vk::binding(3, 2)]] TextureCube<float4> Zenith;
struct ReferenceVertex {
    float4 position : SV_Position;
    nointerpolation float2 cubeValues : TEXCOORD0;
};
[shader("vertex")]
ReferenceVertex vertexMain(uint vertex : SV_VertexID) {
    ReferenceVertex output;
    float2 uv=float2(float((vertex<<1u)&2u),float(vertex&2u));
    output.position=float4(uv*2.0-1.0,0.0,1.0);
    output.cubeValues=FrameValues.x+float2(
        Environment.SampleLevel(LinearSampler,float3(1,0,0),2.5).x,
        Zenith.SampleLevel(LinearSampler,float3(1,0,0),2.5).x);
    return output;
}
float4 referenceIbl(ReferenceVertex input) {
    uint pixel=uint(input.position.y)*2u+uint(input.position.x);
    uint cursor=pixel*106u;
    const float3 directions[6]={float3(1,0,0),float3(-1,0,0),float3(0,1,0),
        float3(0,-1,0),float3(0,0,1),float3(0,0,-1)};
    for(uint cube=0u;cube<2u;++cube) for(uint face=0u;face<6u;++face) {
        for(uint level=0u;level<8u;++level) {
            float lod=level<7u?float(level):2.5;
            float sampled;
            if(cube==0u) sampled=Environment.SampleLevel(LinearSampler,directions[face],lod).x;
            else sampled=Zenith.SampleLevel(LinearSampler,directions[face],lod).x;
            DirectResults[cursor++]=asuint(sampled+FrameValues.x);
        }
    }
    const uint2 coords[4]={uint2(7,15),uint2(64,64),uint2(120,96),uint2(127,0)};
    for(uint probe=0u;probe<4u;++probe) {
        float4 coefficients=BrdfLut.SampleLevel(LinearSampler,(float2(coords[probe])+0.5)/128.0,0.0);
        DirectResults[cursor++]=asuint(FrameValues.x+coefficients.x);
        DirectResults[cursor++]=asuint(FrameValues.x+coefficients.y);
    }
    DirectResults[cursor++]=asuint(input.cubeValues.x);
    DirectResults[cursor]=asuint(input.cubeValues.y);
    bool receiptFault;
    Results_store(0u,pixel*106u,0u,receiptFault);
    return (uint(input.position.x)&1u)==0u?float4(1,0,0,1):float4(0,1,0,1);
}
[shader("fragment")]
float4 cleanFragment(ReferenceVertex input):SV_Target0 { return referenceIbl(input); }
[shader("fragment")]
float4 faultFragment(ReferenceVertex input):SV_Target0 { return referenceIbl(input); }
