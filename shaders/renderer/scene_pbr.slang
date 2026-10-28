#include "core/leaf_surface.slang"
#include "core/pbr_material.slang"
#include "renderer/pbr_brdf.slang"

// CPU bytes are row-major; every target uses -matrix-layout-row-major.
// cameraPosition.w is 1 for perspective world position, 0 for orthographic to-eye.
struct ScenePbrFrameConstants
{
    float4x4 viewProjection;
    float4 directionIntensity;
    float4 lightColor;
    float4 ambientColor;
    float4 cameraPosition;
};
struct ScenePbrMaterialConstants
{
    float4 baseColor;
    float4 emissive;
    float4 factors;
    float4 flags;
};
struct ScenePbrVertex
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 uv;
    float4 tint;
};
[[vk::binding(0,0)]] ConstantBuffer<ScenePbrFrameConstants> SceneFrame;
[[vk::binding(1,0)]] StructuredBuffer<ScenePbrVertex> SceneVertices;
[[vk::binding(2,0)]] ConstantBuffer<ScenePbrMaterialConstants> SceneMaterial;
[[vk::binding(3,0)]] Texture2D<float4> BaseColorTexture;
[[vk::binding(4,0)]] Texture2D<float4> NormalTexture;
[[vk::binding(5,0)]] Texture2D<float4> MetallicRoughnessTexture;
[[vk::binding(6,0)]] Texture2D<float4> OcclusionTexture;
[[vk::binding(7,0)]] Texture2D<float4> EmissiveTexture;
[[vk::binding(8,0)]] SamplerState MaterialSampler;

struct ScenePbrVaryings
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 tint : TEXCOORD4;
};
[shader("vertex")]
ScenePbrVaryings vertexMain(uint id : SV_VertexID)
{
    ScenePbrVertex vertex = SceneVertices[id];
    ScenePbrVaryings output;
    output.position = mul(SceneFrame.viewProjection, vertex.position);
    output.worldPosition = vertex.position.xyz;
    output.normal = vertex.normal.xyz;
    output.tangent = vertex.tangent;
    output.uv = vertex.uv.xy;
    output.tint = vertex.tint;
    return output;
}
[shader("fragment")]
float4 fragmentMain(ScenePbrVaryings input, bool front : SV_IsFrontFace) : SV_Target0
{
    bool doubleSided = SceneMaterial.flags.x > 0.5;
    if (!front && !doubleSided)
        discard;
    float4 base = BaseColorTexture.SampleLevel(MaterialSampler, input.uv, 0.0);
    float4 mr = MetallicRoughnessTexture.SampleLevel(MaterialSampler, input.uv, 0.0);
    float4 occlusion = OcclusionTexture.SampleLevel(MaterialSampler, input.uv, 0.0);
    float4 emissive = EmissiveTexture.SampleLevel(MaterialSampler, input.uv, 0.0);
    emissive.rgb *= SceneMaterial.emissive.rgb;
    ArpPbrMaterialSurface surface = arpEvaluatePbrMaterial(
        SceneMaterial.baseColor * input.tint, SceneMaterial.factors,
        float4(1.0, 1.0, doubleSided ? 0.0 : 1.0, 0.0), float4(0),
        base, mr, emissive, occlusion);
    if (!surface.covered)
        discard;
    float3 normal = arpPbrMaterialNormal(input.normal, input.tangent, front,
        doubleSided ? 0.0 : 1.0, NormalTexture.SampleLevel(MaterialSampler, input.uv, 0.0).xyz);
    float3 toEye = SceneFrame.cameraPosition.w > 0.5
        ? SceneFrame.cameraPosition.xyz - input.worldPosition : SceneFrame.cameraPosition.xyz;
    float viewLengthSquared = dot(toEye, toEye);
    float3 color = surface.baseColorAlpha.rgb * (1.0 - surface.metallic)
        * SceneFrame.ambientColor.rgb * surface.occlusion + surface.emissive;
    if (viewLengthSquared > 0.0)
    {
        float3 viewDirection = toEye * rsqrt(viewLengthSquared);
        float3 toLight = -SceneFrame.directionIntensity.xyz;
        float nDotL = saturate(dot(normal, toLight));
        float nDotV = saturate(dot(normal, viewDirection));
        if (nDotL > 0.0 && nDotV > 0.0)
        {
            float3 halfVec = normalize(viewDirection + toLight);
            float3 f0 = lerp(float3(0.04), surface.baseColorAlpha.rgb, surface.metallic);
            float3 fresnel = fresnelSchlick(saturate(dot(halfVec, viewDirection)), f0);
            float D = distributionGGX(normal, halfVec, surface.roughness);
            float G = geometrySmith(normal, viewDirection, toLight, surface.roughness);
            float3 specular = D * G * fresnel / max(4.0 * nDotV * nDotL, 0.001);
            float3 diffuse = (1.0 - fresnel) * (1.0 - surface.metallic) * surface.baseColorAlpha.rgb;
            color += (diffuse / 3.14159265 + specular) * nDotL
                * SceneFrame.lightColor.rgb * SceneFrame.directionIntensity.w;
        }
    }
    return float4(color, 1.0);
}
