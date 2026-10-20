[shader("compute")]
[numthreads(2, 1, 1)]
void cleanMain(uint3 tid : SV_DispatchThreadID) {
    uint index = 1u - tid.x;
    bool fault;
    uint2 size = Depths_query(index, fault);
    float loaded = Depths_load(index, int3(0, 0, 0), fault);
    float filtered = Depths_sampleLevel(index, 1u - index, float2(0.5, 0.5), ArpBindlessDepthMip(tid.x), fault);
    Results_store(index, 0u, asuint(loaded), fault);
    Results_store(index, 1u, asuint(filtered), fault);
    Results_store(index, 2u, size.x, fault);
    Results_store(index, 3u, size.y, fault);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void publicCleanMain() {
    bool fault;
    uint2 size = Depths_query(0u, fault);
    float loaded = Depths_load(0u, int3(0, 0, 0), fault);
    float filtered = Depths_sampleLevel(0u, 0u, float2(0.5, 0.5), ArpBindlessDepthMip(1u), fault);
    Results_store(0u, 0u, asuint(loaded), fault);
    Results_store(0u, 1u, asuint(filtered), fault);
    Results_store(0u, 2u, size.x, fault);
    Results_store(0u, 3u, size.y, fault);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void faultMain() {
    bool fault;
    float invalid = Depths_sampleLevel(0u, 0u, float2(0.5, 0.5), ArpBindlessDepthMip(0xffffffffu), fault);
    invalid += Depths_load(0u, int3(0, 0, -1), fault);
    invalid += Depths_load(0u, int3(-1, 0, 0), fault);
    invalid += Depths_sampleLevel(2u, 0u, float2(0.5, 0.5), ArpBindlessDepthMip(0u), fault);
    invalid += Depths_sampleLevel(0u, 2u, float2(0.5, 0.5), ArpBindlessDepthMip(0u), fault);
    Results_store(0u, 0u, asuint(invalid), fault);
}

// Source2 owns one authentic cell. Both non-uniform descriptor indices refer
// to it; this independent entry makes no claim about production mip semantics.
[shader("compute")]
[numthreads(2, 1, 1)]
void nativeSingleCellMain(uint3 tid : SV_DispatchThreadID) {
    uint index = 1u - tid.x;
    bool fault;
    uint2 size = Depths_query(index, fault);
    float loaded = Depths_load(index, int3(0, 0, 0), fault);
    float filtered = Depths_sampleLevel(index, 1u - index, float2(0.5, 0.5), ArpBindlessDepthMip(0u), fault);
    Results_store(index, 0u, asuint(loaded), fault);
    Results_store(index, 1u, asuint(filtered), fault);
    Results_store(index, 2u, size.x, fault);
    Results_store(index, 3u, size.y, fault);
}
