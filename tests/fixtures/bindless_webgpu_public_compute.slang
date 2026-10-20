[[vk::binding(2, 0)]] RWStructuredBuffer<uint> Result;

[shader("compute")][numthreads(2, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID) {
    bool fault;
    uint loaded = Counters_load(tid.x, 0u, fault);
    bool loadFault = fault;
    uint elementCount = Counters_query(tid.x, fault);
    bool queryFault = fault;
    uint inputValue = Inputs_load(tid.x, 0u, fault);
    bool inputLoadFault = fault;
    uint inputCount = Inputs_query(tid.x, fault);
    bool inputQueryFault = fault;
    float4 imageValue = Images_load(tid.x, int3(0, 0, 0), fault);
    bool imageLoadFault = fault;
    uint2 imageSize = Images_query(tid.x, fault);
    bool imageQueryFault = fault;
    uint2 outputSize = Outputs_query(tid.x, fault);
    bool outputQueryFault = fault;
    float outputValue = Outputs_load(tid.x, uint2(0u, 0u), fault);
    bool outputLoadFault = fault;
    Outputs_store(tid.x, uint2(0u, 0u), float(tid.x) + 0.5, fault);
    uint previous = Counters_atomicAdd(tid.x, 0u, 1u, fault);
    bool atomicFault = fault;
    Counters_store(tid.x, 0u, previous + 1u, fault);
    float4 filteredValue = Images_sampleLevel(tid.x, 1u - tid.x,
        float2(tid.x == 0u ? 0.5 : 0.25, 0.5), 0.0, fault);
    bool sampleFault = fault;
    uint3 filteredChannels = uint3(filteredValue.rgb * 255.0 + 0.5);
    uint filteredPacked = filteredChannels.x | (filteredChannels.y << 8u)
        | (filteredChannels.z << 16u);
    float4 uniformValue = Globals_load(tid.x, fault);
    bool uniformFault = fault;
    Result[tid.x * 11u] = loadFault || queryFault || atomicFault || fault
        ? 0xffffffffu : loaded;
    Result[tid.x * 11u + 1u] = queryFault || atomicFault || fault
        ? 0xffffffffu : previous;
    Result[tid.x * 11u + 2u] = atomicFault || fault ? 0xffffffffu : elementCount;
    Result[tid.x * 11u + 3u] = inputLoadFault || inputQueryFault || fault
        ? 0xffffffffu : inputValue;
    Result[tid.x * 11u + 4u] = inputQueryFault || fault ? 0xffffffffu : inputCount;
    Result[tid.x * 11u + 5u] = imageLoadFault || imageQueryFault || fault
        ? 0xffffffffu : uint(imageValue.x * 255.0 + 0.5);
    Result[tid.x * 11u + 6u] = imageQueryFault || fault
        ? 0xffffffffu : imageSize.x;
    Result[tid.x * 11u + 7u] = outputQueryFault || fault ? 0xffffffffu : outputSize.x;
    Result[tid.x * 11u + 8u] = outputQueryFault || outputLoadFault || fault
        ? 0xffffffffu : asuint(outputValue);
    Result[tid.x * 11u + 9u] = sampleFault || fault ? 0xffffffffu : filteredPacked;
    Result[tid.x * 11u + 10u] = uniformFault || fault
        ? 0xffffffffu : asuint(uniformValue.x);
    ArpBindlessIndirectCommand physical;
    uint4 mapping;
    bool encoded = arpBindlessEncodeIndirectInstanceBase(3u, 2u, 7u, -4,
        1u + tid.x * 4u, 10u, physical, mapping);
    uint firstLogical, lastLogical, invalidLogical;
    bool firstValid = arpBindlessResolveIndirectInstance(mapping, 0u, firstLogical);
    bool lastValid = arpBindlessResolveIndirectInstance(mapping, 1u, lastLogical);
    bool invalidValid = arpBindlessResolveIndirectInstance(mapping, 2u, invalidLogical);
    uint base = 22u + tid.x * 11u;
    Result[base] = encoded && firstValid && lastValid && !invalidValid && invalidLogical == 0u
        ? physical.indexCount : 0xffffffffu;
    Result[base + 1u] = physical.instanceCount;
    Result[base + 2u] = physical.firstIndex;
    Result[base + 3u] = asuint(physical.vertexOffset);
    Result[base + 4u] = physical.firstInstance;
    Result[base + 5u] = mapping.x;
    Result[base + 6u] = mapping.y;
    Result[base + 7u] = mapping.z;
    Result[base + 8u] = mapping.w;
    Result[base + 9u] = firstLogical;
    Result[base + 10u] = lastLogical;
}

[shader("compute")][numthreads(2, 1, 1)]
void indirectFaultMain(uint3 tid : SV_DispatchThreadID) {
    ArpBindlessIndirectCommand physical;
    uint4 mapping;
    bool encoded = arpBindlessEncodeIndirectInstanceBase(3u, 2u, 7u, -4,
        tid.x == 0u ? 0xffffffffu - 1u : 1u, tid.x == 0u ? 0xffffffffu : 0u,
        physical, mapping);
    uint logicalInstance;
    bool resolved = arpBindlessResolveIndirectInstance(mapping, 0u, logicalInstance);
    uint base = tid.x * 11u;
    Result[base] = physical.indexCount;
    Result[base + 1u] = physical.instanceCount;
    Result[base + 2u] = physical.firstIndex;
    Result[base + 3u] = asuint(physical.vertexOffset);
    Result[base + 4u] = physical.firstInstance;
    Result[base + 5u] = mapping.x;
    Result[base + 6u] = mapping.y;
    Result[base + 7u] = mapping.z;
    Result[base + 8u] = mapping.w;
    Result[base + 9u] = uint(encoded);
    Result[base + 10u] = uint(resolved) | logicalInstance;
}
