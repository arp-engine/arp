void processTexture(uint index) {
    bool fault;
    uint3 size = Images_query(index, fault);
    for (uint layer = 0u; layer < 2u; ++layer) {
        float4 texel = Images_load(index, int4(0, 0, int(layer), 0), fault);
        float4 filtered = Images_sampleLevel(index, 1u - index, float2(0.5, 0.5), layer, 0.0, fault);
        float4 mip = Images_sampleLevel(index, 1u - index, float2(0.5, 0.5), layer, 1.0, fault);
        float4 mipLoaded = Images_load(index, int4(0, 0, int(layer), 1), fault);
        uint base = layer * 8u;
        Results_store(index, base, asuint(texel.r), fault);
        Results_store(index, base + 1u, size.x, fault);
        Results_store(index, base + 2u, asuint(filtered.r), fault);
        Results_store(index, base + 3u, size.y, fault);
        Results_store(index, base + 4u, asuint(filtered.a), fault);
        Results_store(index, base + 5u, asuint(mip.r), fault);
        Results_store(index, base + 6u, size.z, fault);
        Results_store(index, base + 7u, asuint(mipLoaded.r), fault);
    }
}

[shader("compute")]
[numthreads(2, 1, 1)]
void cleanMain(uint3 tid : SV_DispatchThreadID) { processTexture(1u - tid.x); }

[shader("compute")]
[numthreads(4, 1, 1)]
void faultMain(uint3 tid : SV_DispatchThreadID) {
    if (tid.x < 2u) processTexture(1u - tid.x);
    else {
        bool fault;
        Images_load(0u, int4(0, 0, 2, 0), fault);
        Images_load(0u, int4(0, 0, 0, 2), fault);
        Images_load(0u, int4(0, 0, -1, 0), fault);
        Images_load(2u, int4(0, 0, 0, 0), fault);
        Images_sampleLevel(0u, 0u, float2(0.5, 0.5), 2u, 0.0, fault);
        Images_sampleLevel(0u, 2u, float2(0.5, 0.5), 0u, 0.0, fault);
        Results_store(2u, 0u, 0xffffffffu, fault);
    }
}
