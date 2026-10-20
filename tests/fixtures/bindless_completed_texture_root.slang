void processTexture(uint index) {
    bool fault;
    float4 texel = Images_load(index, int3(0, 0, 0), fault);
    uint2 size = Images_query(index, fault);
    float4 filtered = Images_sampleLevel(index, 1u - index, float2(0.5, 0.5), 0.0, fault);
    float4 mip = Images_sampleLevel(index, 1u - index, float2(0.5, 0.5), 1.0, fault);
    Results_store(index, 0u, asuint(texel.r), fault);
    Results_store(index, 1u, size.x, fault);
    Results_store(index, 2u, asuint(filtered.r), fault);
    Results_store(index, 3u, size.y, fault);
    Results_store(index, 4u, asuint(filtered.a), fault);
    Results_store(index, 5u, asuint(mip.r), fault);
}

[shader("compute")]
[numthreads(2, 1, 1)]
void cleanMain(uint3 tid : SV_DispatchThreadID) {
    processTexture(1u - tid.x);
}

[shader("compute")]
[numthreads(4, 1, 1)]
void faultMain(uint3 tid : SV_DispatchThreadID) {
    if (tid.x < 2u) processTexture(1u - tid.x);
    else {
        bool fault;
        Images_load(2u, int3(0, 0, 0), fault);
        Images_query(2u, fault);
        Images_sampleLevel(0u, 2u, float2(0.5, 0.5), 0.0, fault);
        Images_sampleLevel(2u, 0u, float2(0.5, 0.5), 0.0, fault);
        Results_store(2u, 0u, 0xffffffffu, fault);
    }
}
