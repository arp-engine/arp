// VertexColoredSurface/1: static textured surfaces with authored vertex RGBA,
// directional lighting and an explicit fade outside rectangular world bounds.
// Every matrix uses the standard ARP Slang row-major compilation contract.
struct ColoredSurfacePushConstants
{
    float4x4 viewProjection;
    float4 worldBounds; // min X/Z, max X/Z
    float4 directionToLightAndAlpha;
    float4 diffuseAndBorderWidth;
    float4 ambientAndMinimumBorder;
};
[[vk::push_constant]] ColoredSurfacePushConstants gColoredSurface;
[[vk::binding(0, 0)]] Texture2D<float4> SurfaceTexture;
[[vk::binding(1, 0)]] SamplerState SurfaceSampler;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 worldXZ : TEXCOORD2;
};
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(gColoredSurface.viewProjection, float4(input.position, 1));
    output.uv = input.uv;
    float diffuse = max(dot(normalize(input.normal), gColoredSurface.directionToLightAndAlpha.xyz), 0);
    output.color = float4(input.color.rgb * saturate(gColoredSurface.ambientAndMinimumBorder.rgb
        + gColoredSurface.diffuseAndBorderWidth.rgb * diffuse),
        input.color.a * gColoredSurface.directionToLightAndAlpha.w);
    output.worldXZ = input.position.xz;
    return output;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float2 distances = min(input.worldXZ - gColoredSurface.worldBounds.xy,
        gColoredSurface.worldBounds.zw - input.worldXZ);
    float fade = saturate(min(distances.x, distances.y) / gColoredSurface.diffuseAndBorderWidth.w + 1);
    fade = lerp(gColoredSurface.ambientAndMinimumBorder.w, 1, fade);
    float4 color = SurfaceTexture.Sample(SurfaceSampler, input.uv) * input.color;
    color.rgb *= fade;
    return color;
}
