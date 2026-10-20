[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(0, 2)]] TextureCube<float4> Environment;
[[vk::binding(1, 2)]] Texture2D<float4> BrdfLut;
[[vk::binding(2, 2)]] SamplerState LinearSampler;
[[vk::binding(3, 2)]] TextureCube<float4> Zenith;

struct GiVertex {
    float4 position : SV_Position;
    nointerpolation float2 environment : TEXCOORD0;
};
[shader("vertex")]
GiVertex vertexMain(uint vertex : SV_VertexID) {
    GiVertex output;
    float2 position = float2(float((vertex << 1u) & 2u), float(vertex & 2u));
    output.position = float4(position * 2.0 - 1.0, 0.0, 1.0);
    output.environment = FrameValues.x + float2(
        Environment.SampleLevel(LinearSampler,float3(1,0,0),2.5).x,
        Zenith.SampleLevel(LinearSampler,float3(1,0,0),2.5).x);
    return output;
}
float4 shadeGi(GiVertex input, bool injectFault) {
    uint base = (uint(input.position.y) * 2u + uint(input.position.x)) * 106u;
    const float3 directions[6] = {
        float3(1,0,0), float3(-1,0,0), float3(0,1,0),
        float3(0,-1,0), float3(0,0,1), float3(0,0,-1)
    };
    bool fault;
    for(uint face=0u;face<6u;++face) for(uint probe=0u;probe<8u;++probe) {
        float lod=probe==7u ? 2.5 : float(probe);
        float horizon=Environment.SampleLevel(LinearSampler,directions[face],lod).x;
        float zenith=Zenith.SampleLevel(LinearSampler,directions[face],lod).x;
        Results_store(0u,base+face*8u+probe,asuint(FrameValues.x+horizon),fault);
        Results_store(0u,base+48u+face*8u+probe,asuint(FrameValues.x+zenith),fault);
    }
    const float2 texels[4] = {float2(7,15),float2(64,64),float2(120,96),float2(127,0)};
    for(uint probe=0u;probe<4u;++probe) {
        float2 lut=BrdfLut.SampleLevel(LinearSampler,(texels[probe]+0.5)/128.0,0.0).xy;
        Results_store(0u,base+96u+probe*2u,asuint(FrameValues.x+lut.x),fault);
        Results_store(0u,base+97u+probe*2u,asuint(FrameValues.x+lut.y),fault);
    }
    Results_store(0u,base+104u,asuint(input.environment.x),fault);
    Results_store(0u,base+105u,asuint(input.environment.y),fault);
    if(injectFault) Results_store(2u,0u,0xffffffffu,fault);
    return (uint(input.position.x) & 1u) == 0u ? float4(1,0,0,1) : float4(0,1,0,1);
}
[shader("fragment")]
float4 cleanFragment(GiVertex input) : SV_Target0 { return shadeGi(input, false); }
[shader("fragment")]
float4 faultFragment(GiVertex input) : SV_Target0 { return shadeGi(input, true); }
