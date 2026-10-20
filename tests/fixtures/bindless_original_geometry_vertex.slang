struct CompletedGraphicsVertex { float4 position : SV_Position; };
[shader("vertex")]
CompletedGraphicsVertex vertexMain(float4 position : POSITION, uint vertexId : SV_VertexID) {
    CompletedGraphicsVertex output;
    output.position = position;
    return output;
}
[shader("fragment")]
float4 fragmentMain(CompletedGraphicsVertex input) : SV_Target0 {
    // Evaluate actual sampling-coordinate gradients before the non-uniform
    // table/sampler dispatch or any fault branch. A 2x2 texture with these
    // unit UV change per pixel selects mip1 without substituting fixed LOD.
    float2 mipUv = input.position.xy;
    float2 mipDx = ddx(mipUv);
    float2 mipDy = ddy(mipUv);
    uint index = 1u - uint(input.position.x) % 2u;
    bool fault;
    float4 texel = Images_load(index, int3(0, 0, 0), fault);
    uint2 size = Images_query(index, fault);
    float4 filtered = Images_sampleLevel(index, 1u - index, float2(0.5, 0.5), 0.0, fault);
    float4 mip = Images_sampleGrad(index, 1u - index, mipUv, mipDx, mipDy, fault);
    return float4(texel.r, filtered.r, mip.r, float(size.x + size.y) * 0.25);
}

[shader("fragment")]
float4 faultFragment(CompletedGraphicsVertex input) : SV_Target0 {
    bool fault;
    return Images_load(2u, int3(0, 0, 0), fault);
}
