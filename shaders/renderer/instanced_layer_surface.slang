// InstancedLayerSurface/1: shared local skeletal pose rows and independent
// affine instances. Input/storage rows are explicit float4 vectors. Only the
// push matrix uses the standard ARP row-major compilation contract.
[[vk::binding(0, 0)]] StructuredBuffer<float4> gPoseRows;
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
    float4 world0 : TEXCOORD3;
    float4 world1 : TEXCOORD4;
    float4 world2 : TEXCOORD5;
    float4 normal0 : TEXCOORD6;
    float4 normal1 : TEXCOORD7;
    float4 normal2 : TEXCOORD8;
    float4 tint : TEXCOORD9;
    float2 poseRowsAndJoints : TEXCOORD10;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 tint : TEXCOORD2;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    float3 localPosition = 0, localNormal = 0;
    float sum = 0;
    [unroll]
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint joint = uint(max(input.joints[lane], 0));
        float weight = input.weights[lane];
        if (weight > 0 && joint < uint(input.poseRowsAndJoints.y))
        {
            uint start = uint(input.poseRowsAndJoints.x) + joint * 6;
            float4 p = float4(input.position, 1);
            float4 n = float4(input.normal, 0);
            localPosition += float3(dot(gPoseRows[start], p), dot(gPoseRows[start+1], p), dot(gPoseRows[start+2], p)) * weight;
            localNormal += float3(dot(gPoseRows[start+3], n), dot(gPoseRows[start+4], n), dot(gPoseRows[start+5], n)) * weight;
            sum += weight;
        }
    }
    float4 p = float4(localPosition / sum, 1);
    float3 worldPosition = float3(dot(input.world0, p), dot(input.world1, p), dot(input.world2, p));
    float4 n = float4(localNormal, 0);
    VertexOutput output;
    output.position = mul(gSurface.viewProjection, float4(worldPosition, 1));
    output.normal = float3(dot(input.normal0, n), dot(input.normal1, n), dot(input.normal2, n));
    output.uv = input.uv;
    output.tint = input.tint;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 color = gLayerTexture.Sample(gLayerSampler, input.uv) * gSurface.tintAndAlpha * input.tint;
    if (color.a < gSurface.surfaceParameters.x)
        discard;
    float3 normal = input.normal * rsqrt(max(dot(input.normal, input.normal), 0.000001));
    float diffuse = max(dot(normal, -gSurface.lightDirectionAndAmbient.xyz), 0);
    float3 lighting = float3(gSurface.surfaceParameters.zw, gSurface.lightDirectionAndAmbient.w)
        + diffuse * gSurface.lightColorAndIntensity.rgb * gSurface.lightColorAndIntensity.w;
    color.rgb *= lerp(saturate(lighting), float3(1), gSurface.surfaceParameters.y);
    return color;
}
