// Same 224-byte shadow-shaped ABI as ordinary_graphics_matrix_push, with an
// authenticated bindless result write for subsequent public command execution.
struct MatrixPushConstants
{
    float4x4 lightViewProjection[3];
    float4 depthBiasAndFlags;
    uint cascadeIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};
[[vk::push_constant]] MatrixPushConstants Push;

struct MatrixVertex
{
    float4 position : SV_Position;
    nointerpolation float4 value : TEXCOORD0;
};

[shader("vertex")]
MatrixVertex vertexMain(uint vertex : SV_VertexID)
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    MatrixVertex output;
    output.position = mul(Push.lightViewProjection[min(Push.cascadeIndex, 2u)], float4(p, 0, 1));
    output.value = Push.depthBiasAndFlags;
    return output;
}

[shader("fragment")]
float4 fragmentMain(MatrixVertex input) : SV_Target0
{
    float4 value = mul(Push.lightViewProjection[min(Push.cascadeIndex, 2u)], input.value) + Push.depthBiasAndFlags;
    bool fault;
    if (uint(input.position.x) == 0u && uint(input.position.y) == 0u)
    {
        Results_store(Push.pad0, 0u, asuint(value.x), fault);
        Results_store(Push.pad0, 1u, asuint(value.y), fault);
        Results_store(Push.pad0, 2u, asuint(value.z), fault);
        Results_store(Push.pad0, 3u, asuint(value.w), fault);
    }
    return value;
}
