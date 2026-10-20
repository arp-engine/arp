[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(3, 2)]] StructuredBuffer<uint> SharedValues;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> PassValues;

struct CompletedGraphicsVertex {
    float4 position : SV_Position;
    nointerpolation uint sum : TEXCOORD0;
};

[shader("vertex")]
CompletedGraphicsVertex vertexMain(uint vertexId : SV_VertexID) {
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    CompletedGraphicsVertex output;
    output.position = float4(position, 0.0, 1.0);
    output.sum = uint(FrameValues.x) + SharedValues[0];
    return output;
}

float4 completedGraphicsColor(CompletedGraphicsVertex input, bool injectFault) {
    // Evaluate actual sampling-coordinate gradients before the non-uniform
    // table/sampler dispatch or any fault branch. A 2x2 texture with these
    // unit UV change per pixel selects mip1 without substituting fixed LOD.
    float2 mipUv = input.position.xy;
    float2 mipDx = ddx(mipUv);
    float2 mipDy = ddy(mipUv);
    uint index = 1u - uint(input.position.x) % 2u;
    if (injectFault && input.position.x >= 1.0) index = 2u;
    bool fault;
    float4 texel = Images_load(index, int3(0, 0, 0), fault);
    uint2 size = Images_query(index, fault);
    float4 filtered = Images_sampleLevel(index, 1u - index, float2(0.5, 0.5), 0.0, fault);
    float4 mip = Images_sampleGrad(index, 1u - index, mipUv, mipDx, mipDy, fault);
    uint sum = uint(FrameValues.x) + SharedValues[0];
    PassValues[uint(input.position.y) * 2u + uint(input.position.x)] = input.sum + sum;
    if (input.sum != 49u || sum != 49u) return float4(1.0, 0.0, 1.0, 1.0);
    return float4(texel.r, filtered.r, mip.r, float(size.x + size.y) * 0.25);
}

[shader("fragment")]
float4 fragmentMain(CompletedGraphicsVertex input) : SV_Target0 {
    return completedGraphicsColor(input, false);
}

[shader("fragment")]
float4 faultFragment(CompletedGraphicsVertex input) : SV_Target0 {
    return completedGraphicsColor(input, true);
}
