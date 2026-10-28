// Ordered Unified UI / 2. flags.y: straight=0, premultiplied=1, SDF=2.
struct GuiPushConstants
{
    float2 screenSize;
    float2 rectOrigin;
    float2 rectSize;
    float2 uvOrigin;
    float2 uvSize;
    float2 flags;
    float4 color;
};

[[vk::push_constant]] GuiPushConstants gGui;
[[vk::binding(0, 0)]] Texture2D<float4> GuiAtlasTexture;
[[vk::binding(1, 0)]] SamplerState GuiAtlasSampler;

struct VertexInput
{
    float2 corner : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float textured : TEXCOORD1;
    float sampleMode : TEXCOORD2;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    float2 pixel = gGui.rectOrigin + input.corner * gGui.rectSize;
    VertexOutput output;
    output.position = float4(pixel / gGui.screenSize * 2.0 - 1.0, 0.0, 1.0);
    output.uv = gGui.uvOrigin + input.corner * gGui.uvSize;
    output.color = gGui.color;
    output.textured = gGui.flags.x;
    output.sampleMode = gGui.flags.y;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 sampled = GuiAtlasTexture.Sample(GuiAtlasSampler, input.uv);
    // Evaluate derivatives before control flow so helper invocations participate.
    float gradient = fwidth(sampled.a);
    float alpha = sampled.a * input.color.a;
    float3 rgb = sampled.rgb * input.color.rgb * input.color.a;
    if (input.sampleMode < 0.5)
        rgb *= sampled.a;
    if (input.sampleMode > 1.5) {
        float smoothing = 0.5 * max(gradient, 1.0 / 255.0);
        float coverage = smoothstep(128.0 / 255.0 - smoothing, 128.0 / 255.0 + smoothing, sampled.a);
        alpha = coverage * input.color.a;
        rgb = input.color.rgb * alpha;
    }
    if (input.textured < 0.5) {
        alpha = input.color.a;
        rgb = input.color.rgb * alpha;
    }
    return float4(rgb, alpha);
}
