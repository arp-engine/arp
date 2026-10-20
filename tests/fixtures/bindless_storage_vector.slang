// Distinct float lanes and per-image indexing expose scalar R32 substitution.
// Store -> load visibility is a separate command/state contract; these entries
// are dispatched with a declared storage dependency by execution validation.
[shader("compute")]
[numthreads(1, 1, 1)]
void storeMain(uint3 id : SV_DispatchThreadID) {
    bool fault;
    uint index = id.x;
    uint2 size = Pyramids_query(index, fault);
    if (size.x != 0u && size.y != 0u)
        Pyramids_store(index, uint2(0, 0), float4(0.75, 0.125, float(index) + 0.25, 1.0), fault);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void readMain(uint3 id : SV_DispatchThreadID) {
    bool fault;
    float4 value = Sources_load(id.x, int3(0, 0, 0), fault);
    Results_store(id.x, 0u, asuint(value.x), fault);
    Results_store(id.x, 1u, asuint(value.y), fault);
    Results_store(id.x, 2u, asuint(value.z), fault);
    Results_store(id.x, 3u, asuint(value.w), fault);
}
