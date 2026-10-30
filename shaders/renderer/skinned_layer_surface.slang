#include "../core/skinning_geometry.slang"

// LayeredSkinnedSurface/1. Layer ordering and blend/depth/cull state belong
// to the caller. All matrices follow ARP's default Slang matrix ABI.
static const uint kMaxSurfaceJoints = 256;

struct SurfaceSkinningPalette
{
    float4x4 skinMatrices[kMaxSurfaceJoints];
    float4 worldTranslationAndScale;
    float4 worldRotation;
    uint jointCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::binding(0, 0)]] ConstantBuffer<SurfaceSkinningPalette> gSkinning;
[[vk::binding(0, 1)]] Texture2D<float4> gLayerTexture;
[[vk::binding(1, 1)]] SamplerState gLayerSampler;

struct SurfacePushConstants
{
    float4x4 viewProjection;
    float4 tintAndAlpha;
    float4 surfaceParameters;
    float4 lightDirectionAndAmbient;
    float4 lightColorAndIntensity;
};

[[vk::push_constant]] SurfacePushConstants gSurface;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    ArpSkinningBlend blend = arpEmptySkinningBlend();
    [unroll]
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint joint = uint(max(input.joints[lane], 0.0));
        float weight = input.weights[lane];
        if (weight > 0.0 && joint < gSkinning.jointCount && joint < kMaxSurfaceJoints)
        {
            float4x4 matrix = gSkinning.skinMatrices[joint];
            arpAccumulateSkinning(blend, matrix, matrix, false, weight,
                input.position, input.normal, float3(1.0, 0.0, 0.0));
        }
    }
    VertexOutput output;
    float3 worldPosition = arpSkinningWorldPosition(blend.currentPosition, blend.weightSum,
        gSkinning.worldTranslationAndScale, gSkinning.worldRotation);
    output.position = mul(gSurface.viewProjection, float4(worldPosition, 1.0));
    output.normal = arpRotateSkinningDirection(blend.normal, gSkinning.worldRotation);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 color = gLayerTexture.Sample(gLayerSampler, input.uv) * gSurface.tintAndAlpha;
    if (color.a < gSurface.surfaceParameters.x)
        discard;
    float lengthSquared = dot(input.normal, input.normal);
    float3 normal = input.normal * rsqrt(max(lengthSquared, 0.000001));
    float diffuse = max(dot(normal, -gSurface.lightDirectionAndAmbient.xyz), 0.0);
    float3 lighting = gSurface.lightDirectionAndAmbient.w
        + diffuse * gSurface.lightColorAndIntensity.rgb * gSurface.lightColorAndIntensity.w;
    color.rgb *= lerp(lighting, float3(1.0), gSurface.surfaceParameters.y);
    return color;
}
