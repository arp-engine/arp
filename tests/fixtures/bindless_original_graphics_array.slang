[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(3, 2)]] StructuredBuffer<uint> SharedValues;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> PassValues;

struct RootGraphicsArrayVertex {
    float4 position : SV_Position;
    nointerpolation uint sum : TEXCOORD0;
};

[shader("vertex")]
RootGraphicsArrayVertex vertexMain(uint vertexId : SV_VertexID) {
    RootGraphicsArrayVertex output;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    output.position = float4(position, 0.0, 1.0);
    output.sum = uint(FrameValues.x) + SharedValues[0];
    return output;
}

float4 shadeRootArray(RootGraphicsArrayVertex input, bool injectFault) {
    // Terrain weight pages need array-layer selection plus correct gradients.
    // Derivatives of actual UV dominate both nonuniform dispatch and discard.
    float2 uv = input.position.xy;
    float2 gradientX = ddx(uv);
    float2 gradientY = ddy(uv);
    uint x = uint(input.position.x);
    uint layer = uint(input.position.y);
    uint index = 1u - x % 2u;
    if (x == 1u && layer == 0u) discard;
    bool fault;
    float4 texel = Images_load(index, int4(0, 0, int(layer), 0), fault);
    bool failed = fault;
    uint3 size = Images_query(index, fault); failed = failed || fault;
    float4 filtered = Images_sampleLevel(index, 1u - index, float2(0.5, 0.5), layer, 0.0, fault);
    failed = failed || fault;
    float4 mip = Images_sampleGrad(index, 1u - index, uv, layer, gradientX, gradientY, fault);
    failed = failed || fault || any(size != uint3(2u, 2u, 2u));
    if (injectFault) {
        float4 rejected = Images_load(index, int4(0, 0, 2, 0), fault);
        failed = failed || !fault || any(rejected != 0.0);
        rejected = Images_load(index, int4(0, 0, int(layer), 2), fault);
        failed = failed || !fault || any(rejected != 0.0);
        rejected = Images_sampleGrad(index, 1u - index, uv, 2u, gradientX, gradientY, fault);
        failed = failed || !fault || any(rejected != 0.0);
        rejected = Images_sampleGrad(2u, 1u - index, uv, layer, gradientX, gradientY, fault);
        failed = failed || !fault || any(rejected != 0.0);
        rejected = Images_sampleGrad(index, 2u, uv, layer, gradientX, gradientY, fault);
        failed = failed || !fault || any(rejected != 0.0);
    }
    uint sum = uint(FrameValues.x) + SharedValues[0];
    PassValues[uint(input.position.y) * 2u + uint(input.position.x)] = input.sum + sum;
    failed = failed || input.sum != 49u || sum != 49u;
    return failed ? float4(1.0, 0.0, 1.0, 1.0) : float4(texel.r, filtered.r, mip.r, 1.0);
}

[shader("fragment")]
float4 cleanFragment(RootGraphicsArrayVertex input) : SV_Target0 {
    return shadeRootArray(input, false);
}

[shader("fragment")]
float4 faultFragment(RootGraphicsArrayVertex input) : SV_Target0 {
    return shadeRootArray(input, true);
}
