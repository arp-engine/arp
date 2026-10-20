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

[[vk::push_constant]]
GuiPushConstants gGui;

[[vk::binding(0, 1)]]
Texture2D<float4> GuiAtlasTexture;

[[vk::binding(1, 1)]]
SamplerState GuiAtlasSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float textured : TEXCOORD1;
    float sdfMode : TEXCOORD2;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 corners[6] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 1.0),
    };

    float2 pixel = gGui.rectOrigin + corners[vertexId] * gGui.rectSize;
    float2 ndc = float2(
        pixel.x / gGui.screenSize.x * 2.0 - 1.0,
        pixel.y / gGui.screenSize.y * 2.0 - 1.0);

    VertexOutput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = gGui.uvOrigin + corners[vertexId] * gGui.uvSize;
    output.color = gGui.color;
    output.textured = gGui.flags.x;
    output.sdfMode = gGui.flags.y;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    if (input.textured > 0.5) {
        float4 sampled = GuiAtlasTexture.Sample(GuiAtlasSampler, input.uv);
        if (input.sdfMode > 0.5) {
            // SDF mode: the atlas alpha channel stores a normalized signed
            // distance field. FreeType maps zero distance to byte 128.
            // smoothstep takes a HALF-width: using a full screen derivative
            // here softens each edge across two pixels. The Euclidean gradient
            // keeps the one-pixel transition independent of edge orientation.
            float distance = sampled.a;
            float edge = 128.0 / 255.0;
            float pixelDistance = length(float2(ddx(distance), ddy(distance)));
            // One alpha-byte interval is the precision floor of the atlas.
            float smoothing = 0.5 * max(pixelDistance, 1.0 / 255.0);
            float alpha = smoothstep(edge - smoothing, edge + smoothing, distance);
            return float4(input.color.rgb, alpha * input.color.a);
        }
        return sampled * input.color;
    }
    return input.color;
}
