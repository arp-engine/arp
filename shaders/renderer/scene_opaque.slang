// SceneOpaqueProducer/1. CPU row-major bytes; all targets explicitly compile
// with -matrix-layout-row-major. Geometry was deterministically expanded cold.
struct SceneOpaqueFrameConstants
{
    float4x4 viewProjection;
    float4 directionIntensity;
    float4 lightColor;
    float4 ambientColor;
};
struct SceneOpaqueVertex
{
    float4 position;
    float4 normal;
    float4 albedo;
};
[[vk::binding(0,0)]] ConstantBuffer<SceneOpaqueFrameConstants> SceneFrame;
[[vk::binding(1,0)]] StructuredBuffer<SceneOpaqueVertex> SceneVertices;

struct SceneOpaqueVaryings
{
    float4 position : SV_Position;
    float4 normal : TEXCOORD0;
    float4 albedo : TEXCOORD1;
};
[shader("vertex")]
SceneOpaqueVaryings vertexMain(uint id : SV_VertexID)
{
    SceneOpaqueVertex vertex = SceneVertices[id];
    SceneOpaqueVaryings output;
    output.position = mul(SceneFrame.viewProjection, vertex.position);
    output.normal = vertex.normal;
    output.albedo = vertex.albedo;
    return output;
}
[shader("fragment")]
float4 fragmentMain(SceneOpaqueVaryings input, bool front : SV_IsFrontFace) : SV_Target0
{
    if (!front && input.normal.w < 0.5)
        discard;
    float cosine = dot(normalize(input.normal.xyz), -SceneFrame.directionIntensity.xyz);
    float lambert = input.normal.w > 0.5 ? abs(cosine) : max(cosine, 0.0);
    float3 irradiance = SceneFrame.ambientColor.rgb
        + SceneFrame.lightColor.rgb * SceneFrame.directionIntensity.w * lambert;
    return float4(input.albedo.rgb * irradiance, 1.0);
}
