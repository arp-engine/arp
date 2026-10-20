static const float kEditorViewportMinimumRoughness = 0.045;
static const float kEditorViewportAmbientIntensity = 0.16;
static const float3 kEditorViewportKeyLightDirection = normalize(float3(-0.41, -0.73, -0.55));
static const float3 kEditorViewportKeyLightColor = float3(3.2, 3.05, 2.8);

// EditorViewportPbrPushConstants mirrors the row-major CPU payload. Keep all
// explanatory comments outside this struct for reflected size parsing.
struct EditorViewportPbrPushConstants
{
    float4x4 viewProjection;
    float4x4 model;
    float4x4 normalTransform;
    float4 cameraPosition;
    float4 baseColorFactor;
    float4 emissiveMetallic;
    float4 roughnessOcclusionAlphaCutoffMode;
};

[[vk::push_constant]]
EditorViewportPbrPushConstants gEditorViewport;

[[vk::binding(0, 1)]]
Texture2D<float4> BaseColorTexture;
[[vk::binding(1, 1)]]
Texture2D<float4> NormalTexture;
[[vk::binding(2, 1)]]
Texture2D<float4> MetallicRoughnessTexture;
[[vk::binding(3, 1)]]
Texture2D<float4> OcclusionTexture;
[[vk::binding(4, 1)]]
Texture2D<float4> EmissiveTexture;
[[vk::binding(5, 1)]]
SamplerState LinearSampler;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
};

struct EditorViewportFragmentInput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
};

[shader("vertex")]
EditorViewportFragmentInput vertexMain(VertexInput input)
{
    EditorViewportFragmentInput output;
    float4 worldPosition = mul(gEditorViewport.model, float4(input.position, 1.0));
    output.position = mul(gEditorViewport.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul((float3x3)gEditorViewport.normalTransform, input.normal));
    output.uv = input.uv;
    output.worldTangent = float4(
        normalize(mul((float3x3)gEditorViewport.model, input.tangent.xyz)),
        input.tangent.w);
    return output;
}

float distributionGgx(float normalHalf, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = normalHalf * normalHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(3.14159265 * denominator * denominator, 0.000001);
}

float geometrySchlick(float normalDirection, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    return normalDirection / max(normalDirection * (1.0 - k) + k, 0.000001);
}

float3 fresnelSchlick(float viewHalf, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - viewHalf, 5.0);
}

[shader("fragment")]
float4 fragmentMain(EditorViewportFragmentInput input) : SV_Target
{
    float4 baseSample = BaseColorTexture.Sample(LinearSampler, input.uv);
    float4 materialSample = MetallicRoughnessTexture.Sample(LinearSampler, input.uv);
    float4 normalSample = NormalTexture.Sample(LinearSampler, input.uv);
    float4 occlusionSample = OcclusionTexture.Sample(LinearSampler, input.uv);
    float4 emissiveSample = EmissiveTexture.Sample(LinearSampler, input.uv);

    float alpha = baseSample.a * gEditorViewport.baseColorFactor.a;
    float alphaMode = gEditorViewport.roughnessOcclusionAlphaCutoffMode.w;
    if (alphaMode > 0.5 && alphaMode < 1.5
        && alpha < gEditorViewport.roughnessOcclusionAlphaCutoffMode.z)
    {
        discard;
    }

    float3 baseColor = baseSample.rgb * gEditorViewport.baseColorFactor.rgb;
    float3 tangent = normalize(input.worldTangent.xyz);
    float3 geometricNormal = normalize(input.worldNormal);
    float3 bitangent = normalize(cross(geometricNormal, tangent)) * input.worldTangent.w;
    float3 tangentNormal = normalize(normalSample.xyz * 2.0 - 1.0);
    float3 normal = normalize(
        tangent * tangentNormal.x + bitangent * tangentNormal.y + geometricNormal * tangentNormal.z);

    float metallic = saturate(gEditorViewport.emissiveMetallic.w * materialSample.b);
    float roughness = clamp(
        gEditorViewport.roughnessOcclusionAlphaCutoffMode.x * materialSample.g,
        kEditorViewportMinimumRoughness,
        1.0);
    float occlusion = lerp(
        1.0,
        occlusionSample.r,
        saturate(gEditorViewport.roughnessOcclusionAlphaCutoffMode.y));

    float3 viewDirection = normalize(gEditorViewport.cameraPosition.xyz - input.worldPosition);
    float3 lightDirection = -kEditorViewportKeyLightDirection;
    float3 halfDirection = normalize(viewDirection + lightDirection);
    float normalLight = saturate(dot(normal, lightDirection));
    float normalView = max(saturate(dot(normal, viewDirection)), 0.0001);
    float normalHalf = saturate(dot(normal, halfDirection));
    float viewHalf = saturate(dot(viewDirection, halfDirection));
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 fresnel = fresnelSchlick(viewHalf, f0);
    float distribution = distributionGgx(normalHalf, roughness);
    float geometry = geometrySchlick(normalView, roughness)
        * geometrySchlick(max(normalLight, 0.0001), roughness);
    float3 specular = distribution * geometry * fresnel
        / max(4.0 * normalView * max(normalLight, 0.0001), 0.0001);
    float3 diffuseWeight = (1.0 - fresnel) * (1.0 - metallic);
    float3 directLight = (diffuseWeight * baseColor / 3.14159265 + specular)
        * kEditorViewportKeyLightColor * normalLight;
    float3 ambient = baseColor * kEditorViewportAmbientIntensity * occlusion;
    float3 emissive = emissiveSample.rgb * gEditorViewport.emissiveMetallic.rgb;
    float3 color = directLight + ambient + emissive;
    return float4(color, alpha);
}
