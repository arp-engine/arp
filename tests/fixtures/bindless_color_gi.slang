[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(0, 2)]] TextureCube<float4> Environment;
[[vk::binding(1, 2)]] Texture2D<float4> BrdfLut;
[[vk::binding(2, 2)]] SamplerState LinearSampler;
[[vk::binding(3, 2)]] Texture2DArray<float4> Irradiance;

void evaluateGi(uint index, bool injectFault, uint base = 0u) {
    const float3 directions[6] = {
        float3(1,0,0), float3(-1,0,0), float3(0,1,0),
        float3(0,-1,0), float3(0,0,1), float3(0,0,-1)
    };
    bool fault;
    for(uint face=0u;face<6u;++face) for(uint mip=0u;mip<2u;++mip) {
        float value=FrameValues.x+Environment.SampleLevel(LinearSampler,directions[face],float(mip)).x;
        Results_store(index,base+face*2u+mip,asuint(value),fault);
    }
    for(uint mip=0u;mip<2u;++mip) {
        float value=FrameValues.x+BrdfLut.SampleLevel(LinearSampler,float2(0.25,0.25),float(mip)).x;
        Results_store(index,base+12u+mip,asuint(value),fault);
    }
    for(uint layer=0u;layer<2u;++layer) for(uint mip=0u;mip<2u;++mip) {
        float value=FrameValues.x+Irradiance.SampleLevel(LinearSampler,float3(0.25,0.25,float(layer)),float(mip)).x;
        Results_store(index,base+14u+layer*2u+mip,asuint(value),fault);
    }
    if(injectFault) Results_store(2u,0u,0xffffffffu,fault);
}
[shader("compute")][numthreads(1,1,1)]
void cleanMain(uint3 tid : SV_DispatchThreadID) { evaluateGi(tid.x,false); }
[shader("compute")][numthreads(1,1,1)]
void faultMain(uint3 tid : SV_DispatchThreadID) { evaluateGi(tid.x,true); }

struct GiVertex {
    float4 position : SV_Position;
    nointerpolation float environment : TEXCOORD0;
};
[shader("vertex")]
GiVertex vertexMain(uint vertex : SV_VertexID) {
    GiVertex output;
    float2 position = float2(float((vertex << 1u) & 2u), float(vertex & 2u));
    output.position = float4(position * 2.0 - 1.0, 0.0, 1.0);
    output.environment = FrameValues.x + Environment.SampleLevel(LinearSampler,float3(1,0,0),0.0).x;
    return output;
}
float4 shadeGi(GiVertex input, bool injectFault) {
    // Each pixel owns its nineteen words; no ordering between invocations.
    uint base = (uint(input.position.y) * 2u + uint(input.position.x)) * 19u;
    evaluateGi(0u, injectFault, base);
    bool fault;
    Results_store(0u, base + 18u, asuint(input.environment), fault);
    return (uint(input.position.x) & 1u) == 0u
        ? float4(1,0,0,1) : float4(0,1,0,1);
}
[shader("fragment")]
float4 cleanFragment(GiVertex input) : SV_Target0 { return shadeGi(input, false); }
[shader("fragment")]
float4 faultFragment(GiVertex input) : SV_Target0 { return shadeGi(input, true); }
