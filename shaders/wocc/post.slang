// WoCC post-process shader.
//
// Replicates the World of ClaudeCraft TS post chain (src/render/post.ts):
//   RenderPass/N8AO -> UnrealBloom -> OutputPass(ACES + sRGB) -> GradePass
//
// This single Slang module exposes the fragment entry points the WoCC
// FrameGraph chains together as fullscreen passes:
//   - bloomDownsampleFragmentMain : luminance-based high pass (UnrealBloom-style)
//   - bloomBlurFragmentMain        : Kawase dual-filter blur (3 iterations
//                                     produce the soft glow spread)
//   - bloomCompositeFragmentMain  : additive bloom composite
//   - toneMappingFragmentMain     : exposure + ACES (the WoCC scene/sky
//                                   shaders output HDR linear; tonemap lives
//                                   here so bloom sees true HDR values)
//   - gradeFragmentMain           : lift/gamma/gain, saturation, vignette,
//                                   faint animated grain (1:1 with the TS
//                                   GradeShader)
//
// Matrix-layout note: this fullscreen post pass carries no matrices.

struct WoccPostPushConstants
{
    float4 toneMappingExposure;
    float4 bloom;
    float4 gradeLift;
    float4 gradeGain;
    float4 gradeGammaSaturation;
    float4 vignetteGrain;
    float4 timeParams;
};

[[vk::push_constant]]
WoccPostPushConstants gPost;

// Descriptor bindings follow the engine post-process convention (see
// shaders/stylized/postprocess.slang + Get-ShaderBindings in
// compile-shaders.ps1): input color texture @ set 0 binding 0, input sampler
// @ set 0 binding 1, bloom texture @ set 0 binding 2. The reflection generator
// assigns these by name, so the source `[[vk::binding]]` attributes MUST match
// the hardcoded set/binding or the runtime descriptor writes target the wrong
// slots.
[[vk::binding(0, 0)]] Texture2D<float4> PostProcessInputTexture : register(t0);
[[vk::binding(1, 0)]] SamplerState PostProcessInputSampler : register(s1);
[[vk::binding(2, 0)]] Texture2D<float4> BloomTexture : register(t2);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    // Fullscreen triangle: one oversized triangle covers the viewport, so the
    // post pass never needs a vertex buffer / vertex layout (matches the
    // fxaa/ssao/postprocess fullscreen pattern).
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };
    float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0),
    };

    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = uvs[vertexId];
    return output;
}

float4 sampleInput(float2 uv)
{
    return PostProcessInputTexture.Sample(PostProcessInputSampler, uv);
}

// ACES filmic tone mapping - identical to the pbr_scene/sky ACES the WoCC
// scene shaders used to apply in-shader. Centralising it here means the scene
// color target stays HDR linear for the bloom pass, then tonemap + grade run
// in display space (matching the TS OutputPass -> GradePass order).
float3 applyAcesToneMapping(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    float3 x = max(color, float3(0.0, 0.0, 0.0));
    float3 mapped = (x * (a * x + b)) / (x * (c * x + d) + e);
    return saturate(mapped);
}

// UnrealBloom-style luminance-based high pass.
// Three.js UnrealBloomPass:
//   luminance = dot(color, vec3(0.299, 0.587, 0.114))
//   knee      = threshold * 0.5
//   soft      = clamp((luminance - threshold + knee) / (2 * knee), 0, 1)
//   output    = color * soft
[shader("fragment")]
float4 bloomDownsampleFragmentMain(VertexOutput input) : SV_Target
{
    float3 color = sampleInput(input.uv).rgb;
    float luminance = dot(color, float3(0.299, 0.587, 0.114));
    float threshold = gPost.bloom.x;
    float knee = threshold * 0.5;
    float soft = saturate((luminance - threshold + knee) / max(2.0 * knee, 0.0001));
    return float4(color * soft, 1.0);
}

// Kawase dual-filter blur. Each iteration samples 4 taps at offset (±d, ±d)
// and averages them. Three iterations with increasing offset (1 / 2 / 4 texels
// at half-res) approximate a Gaussian kernel matching UnrealBloom's multi-
// resolution pyramid spread.
// gPost.bloom.z carries the pre-computed UV-space offset for this iteration.
[shader("fragment")]
float4 bloomBlurFragmentMain(VertexOutput input) : SV_Target
{
    float offset = gPost.bloom.z;
    float2 d = float2(offset, offset);
    float3 sum = sampleInput(input.uv + d).rgb;
    sum += sampleInput(input.uv - d).rgb;
    sum += sampleInput(input.uv + float2(d.x, -d.y)).rgb;
    sum += sampleInput(input.uv + float2(-d.x, d.y)).rgb;
    return float4(sum * 0.25, 1.0);
}

[shader("fragment")]
float4 bloomCompositeFragmentMain(VertexOutput input) : SV_Target
{
    float4 color = sampleInput(input.uv);
    float3 bloom = BloomTexture.Sample(PostProcessInputSampler, input.uv).rgb;
    return float4(color.rgb + bloom * max(gPost.bloom.y, 0.0), color.a);
}

[shader("fragment")]
float4 toneMappingFragmentMain(VertexOutput input) : SV_Target
{
    float4 color = sampleInput(input.uv);
    float3 mapped = applyAcesToneMapping(color.rgb * max(gPost.toneMappingExposure.x, 0.0));
    return float4(mapped, color.a);
}

// Grade pass - 1:1 replica of src/render/post.ts GradeShader. Runs in display
// space (after tonemap), so lift/gamma/gain and saturation operate on
// tonemapped LDR color, exactly like the TS chain. The animated grain is driven
// by gPost.timeParams.x (the shared frame clock the renderer ticks once per
// frame), matching sharedUniforms.uTime in the TS project.
[shader("fragment")]
float4 gradeFragmentMain(VertexOutput input) : SV_Target
{
    float3 c = sampleInput(input.uv).rgb;

    // Lift / gain / gamma (display-space).
    float3 lift = gPost.gradeLift.rgb;
    float3 gain = gPost.gradeGain.rgb;
    float3 gamma = float3(gPost.gradeGammaSaturation.rgb);
    c = pow(max(float3(0.0, 0.0, 0.0), c * gain + lift), gamma);

    // Saturation (mix toward luminance).
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    float saturation = gPost.gradeGammaSaturation.w;
    c = lerp(float3(l, l, l), c, saturation);

    // Vignette (radial darkening toward the corners).
    float2 d = input.uv - 0.5;
    float vignetteStrength = gPost.vignetteGrain.x;
    float vignetteStart = gPost.vignetteGrain.y;
    float vignetteEnd = gPost.vignetteGrain.z;
    c *= 1.0 - vignetteStrength * smoothstep(vignetteStart, vignetteEnd, dot(d, d) * 2.2);

    // Faint animated grain (same hash + fract formula as the TS GradeShader).
    float grainAmount = gPost.vignetteGrain.w;
    float timeSeconds = gPost.timeParams.x;
    float grain = fract(sin(dot(input.uv * 731.7 + timeSeconds, float2(12.9898, 78.233))) * 43758.5);
    c += (grain - 0.5) * grainAmount;

    return float4(c, 1.0);
}
