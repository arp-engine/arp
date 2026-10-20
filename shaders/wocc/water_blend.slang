// WoCC Water Blend Shader - fullscreen pass that blends the water render
// target (WaterColor) onto the scene color buffer using alpha blend
// (SrcAlpha, OneMinusSrcAlpha). This is Pass 2 of the dual-pass water
// refraction architecture. No push constants, no vertex buffer (SV_VertexID).

[[vk::binding(0, 0)]]
Texture2D<float4> WaterColorTexture;
[[vk::binding(1, 0)]]
SamplerState BlendSampler;

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    // Fullscreen triangle: 3 vertices covering the entire screen.
    // UV convention matches post.slang/post_fx.slang (uv.y = pos.y*0.5+0.5).
    // The previous 0.5 - pos.y*0.5 flipped the sample vertically, misregistering
    // the water target against the scene.
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(pos, 0.0, 1.0);
    output.uv = float2(pos.x * 0.5 + 0.5, pos.y * 0.5 + 0.5);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // Sample the water render target. The pipeline blend state
    // (SrcAlpha, OneMinusSrcAlpha) blends this onto sceneColor.
    return WaterColorTexture.Sample(BlendSampler, input.uv);
}
