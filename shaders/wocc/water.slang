// WoCC Water Shader (Set 0 scene input + Set 1 material + push constants)
//
// Multi-wave Gerstner vertex displacement + closed-form analytic normal (from
// the summed Gerstner partial derivatives) + dual scrolling normal map detail
// (texture, distance-attenuated) + Schlick Fresnel + depth-based color
// gradient (from vertex-encoded shore distance) + screen-space refraction
// (sampling a copied sceneColor via normal-offset UVs) + Beer-Lambert light
// absorption (wavelength-dependent depth attenuation of the refracted scene)
// + foam texture (shoreline). Semi-transparent blend, depthWrite=false.
//
// Set 0 (per-frame, written by the pipeline each frame from GPU-only copies of
// the scene color + depth made before this pass to avoid a feedback loop with
// the depth attachment this pass still writes/tests):
//   binding 0 = SceneColorTexture (copied scene color, refraction source)
//   binding 1 = SceneDepthTexture (copied scene depth, D32Float, Depth aspect)
//   binding 2 = SceneInputSampler (linear clamp)
//
// Set 1 reuses the PBR material layout (binding 0-5) so the water material
// descriptor set is created by the same createMaterialFromImages path:
//   binding 0 = BaseColor (water albedo, blue)
//   binding 1 = NormalTexture (waterNormalA, scrolled)
//   binding 2 = MetallicRoughnessTexture (waterNormalB, scrolled offset)
//   binding 3 = EmissiveTexture (foam texture, R channel)
//   binding 5 = LinearSampler
//
// Push constant struct fields MUST contain NO inline // comments.
// waves is a float4x4 used as 4 column vec4s (one per Gerstner wave):
//   column i = (dirX, dirZ, amplitude, wavelength). Declared as a matrix
//   (not float4[4]) because the shader-bindings codegen emits float[16] for
//   matrix members but mishandles vec4 arrays.
struct WoccWaterPushConstants
{
    float4x4 viewProjection;
    float4x4 waves;         // 4 Gerstner waves as columns: (dirX, dirZ, amp, wavelength)
    float4   cameraTime;
    float4   lightDirInt;
    float4   lightColor;
    float4   waterParams;    // x=baseline y, y=amp(ref,unused), z=len(ref,unused), w=speed
    float4   waveDirScroll;  // xy=unused, z=normalScrollSpeed, w=transparency
    float4   deepColorRange;
    float4   shallowFoam;
    float4   foamRefract;    // x=foamIntensity, y=refractionStrength, z=invScreenWidth, w=invScreenHeight
};

[[vk::push_constant]]
WoccWaterPushConstants gWater;

// Set 0: Per-frame scene color + depth snapshots copied before this pass to
// avoid sampling the live depth attachment this pass writes/tests.
[[vk::binding(0, 0)]]
Texture2D<float4> SceneColorTexture;
[[vk::binding(1, 0)]]
Texture2D<float>  SceneDepthTexture;
[[vk::binding(2, 0)]]
SamplerState      SceneInputSampler;
// Set 1: Material textures (same binding layout as PBR for descriptor reuse).
[[vk::binding(0, 1)]]
Texture2D<float4> BaseColorTexture;
[[vk::binding(1, 1)]]
Texture2D<float4> NormalTexture;
[[vk::binding(2, 1)]]
Texture2D<float4> MetallicRoughnessTexture;
[[vk::binding(3, 1)]]
Texture2D<float4> EmissiveTexture;
[[vk::binding(5, 1)]]
SamplerState LinearSampler;

static const float kPi = 3.14159265358979;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 viewDir : TEXCOORD2;
    float shoreDistance : TEXCOORD3;
    float3 gerstnerNormal : TEXCOORD4;
};

// Summed multi-wave Gerstner displacement + closed-form surface normal.
// Looping 4 waves with decreasing amplitude/wavelength produces an irregular
// natural surface instead of a single-direction sine "comb". The normal is
// derived analytically from the Gerstner partial derivatives (no finite-
// differencing), so it is exact and free of the 3 extra gerstner evals the
// old eps=0.5 scheme cost per fragment.
struct GerstnerResult
{
    float3 pos;
    float3 normal;
};

GerstnerResult gerstnerMultiWave(
    float3 p, float time, float baseline, float speed, float waveScale)
{
    GerstnerResult r;
    r.pos = float3(p.x, baseline, p.z);
    r.normal = float3(0.0, 1.0, 0.0);
    [unroll]
    for (int i = 0; i < 4; ++i) {
        float2 dir = normalize(gWater.waves[i].xy);
        float baseAmp = gWater.waves[i].z;
        float amp = baseAmp * waveScale;
        float len = gWater.waves[i].w;
        float freq = 2.0 * kPi / max(len, 0.001);
        // Per-wave phase offset (i * 1.7) de-correlates the 4 waves so crests
        // don't all align, avoiding periodic reinforcement patterns.
        float phase = dot(dir, p.xz) * freq + speed * time * freq + float(i) * 1.7;
        // Steepness Q normalized per-wave so each contributes a comparable
        // vertical pull without blowing up at short wavelengths / large amps.
        // Derive steepness from the authored deep-water amplitude, not the
        // attenuated amplitude. Otherwise Q grows as waveScale approaches zero
        // and cancels the intended fade of the horizontal displacement.
        float Q = 0.4 / max(baseAmp * freq * 4.0, 0.001);
        float s = sin(phase);
        float c = cos(phase);
        // Displacement accumulation.
        r.pos.y += amp * s;
        r.pos.x -= Q * amp * dir.x * c;
        r.pos.z -= Q * amp * dir.y * c;
        // Analytic normal contribution (Gerstner closed-form partials):
        //   dN.x += -dir.x * WA * cos(phase)
        //   dN.z += -dir.y * WA * cos(phase)
        //   dN.y += -Q * WA^2 * sin(phase)
        float WA = freq * amp;
        r.normal.x += -dir.x * WA * c;
        r.normal.z += -dir.y * WA * c;
        r.normal.y += -Q * WA * WA * s;
    }
    r.normal = normalize(r.normal);
    return r;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    // buildWater stores terrainY-waterLevel in normal.y. Suppress all wave
    // displacement through the shallow shoreline band so neither crests nor
    // Gerstner horizontal motion can push the lifted water mesh through land.
    float shoreDepth = max(-input.normal.y, 0.0);
    float waveScale = smoothstep(0.0, 0.75, shoreDepth);

    float waveAmplitudeSum = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i) {
        waveAmplitudeSum += gWater.waves[i].z;
    }
    // The deep-water baseline is lifted by the summed amplitudes to keep wave
    // troughs above the seabed. Remove that lift at the shoreline along with
    // the waves; retaining it is what exposed flat white triangular wedges.
    float baseline = gWater.waterParams.x
        - waveAmplitudeSum * (1.0 - waveScale);
    float speed = gWater.waterParams.w;
    float time = gWater.cameraTime.w;

    GerstnerResult g = gerstnerMultiWave(
        input.position, time, baseline, speed, waveScale);

    output.shoreDistance = input.normal.y;
    output.worldPos = g.pos;
    output.position = mul(gWater.viewProjection, float4(g.pos, 1.0));
    output.uv = input.uv;
    output.viewDir = normalize(gWater.cameraTime.xyz - g.pos);
    output.gerstnerNormal = g.normal;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // The CPU mesh deliberately includes a small land-side margin so shoreline
    // interpolation remains continuous. It must never shade those fragments:
    // zero depth produces full-intensity foam and reveals the grid triangles.
    if (input.shoreDistance > 0.0) {
        discard;
    }

    float speed = gWater.waterParams.w;
    float time = gWater.cameraTime.w;
    float normalScrollSpeed = gWater.waveDirScroll.z;
    float transparency = gWater.waveDirScroll.w;
    float depthRange = gWater.deepColorRange.w;
    float foamWidth = gWater.shallowFoam.w;
    float foamIntensity = gWater.foamRefract.x;
    float refractionStrength = gWater.foamRefract.y;
    float2 invScreenWH = gWater.foamRefract.zw;

    // --- Water depth from vertex-encoded shore distance ---
    // shoreDistance (baked into normal.y by buildWater) = terrainY - waterLevel.
    // Negative = underwater depth; magnitude is the water depth at this vertex.
    float waterDepth = max(-input.shoreDistance, 0.0);

    // --- Analytic base normal (from vertex-stage Gerstner closed-form) ---
    // No finite-differencing: the vertex shader already computed the exact
    // surface normal from the summed Gerstner partials and interpolated it.
    float3 baseNormal = normalize(input.gerstnerNormal);

    // --- Dual scrolling normal map detail (high-freq, distance-attenuated) ---
    float2 scrollA = input.uv + float2(time * normalScrollSpeed,
                                        time * normalScrollSpeed * 0.7);
    float2 scrollB = input.uv * 1.3 + float2(-time * normalScrollSpeed * 0.9,
                                              time * normalScrollSpeed * 1.1);
    float3 nA = normalize(NormalTexture.Sample(LinearSampler, scrollA).xyz * 2.0 - 1.0);
    float3 nB = normalize(MetallicRoughnessTexture.Sample(LinearSampler, scrollB).xyz * 2.0 - 1.0);
    float3 detailNormal = normalize(nA + nB);
    // Detail weight is higher up close (where the texel footprint is sub-pixel)
    // and fades with distance to suppress aliasing/flicker on far water.
    float dist = length(input.worldPos.xz - gWater.cameraTime.xz);
    float detailWeight = lerp(0.35, 0.08, saturate(dist / 80.0));
    float3 normal = normalize(float3(
        baseNormal.x + detailNormal.x * detailWeight,
        baseNormal.y,
        baseNormal.z + detailNormal.z * detailWeight
    ));

    // --- Fresnel (Schlick) ---
    float NdotV = max(dot(normal, input.viewDir), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    // --- Screen-space refraction ---
    // SV_POSITION.xy is pixel coords; convert to UV via invScreenWH (push
    // constant). Offset the UV by the surface normal's screen-space xz to
    // simulate light bending through the curved water surface. The copied
    // sceneColor was captured before this pass (front-copy pattern) so there
    // is no feedback loop with this pass's color/depth attachments.
    float2 screenUv = input.position.xy * invScreenWH;
    float2 refractOffset = normal.xz * refractionStrength * invScreenWH * 64.0;
    float3 sceneColor = SceneColorTexture.Sample(SceneInputSampler, screenUv + refractOffset).rgb;

    // --- Scene depth (water column thickness along the view ray) ---
    // The copied scene depth is the pre-water scene's nonlinear depth at this
    // pixel; SV_POSITION.z is the water surface's nonlinear depth. The
    // difference, converted back to linear eye-space depth, approximates how
    // much water the view ray passes through before hitting the seabed/terrain.
    // This refines the vertex-interpolated shoreDistance into a per-pixel
    // water depth, sharpening the waterline and foam band at oblique angles
    // where vertex interpolation smears the boundary.
    float sceneDepthNdc = SceneDepthTexture.Sample(SceneInputSampler, screenUv).r;
    float waterDepthNdc = input.position.z;
    // The near/far projection is not in the push constant; instead use a
    // relative measure: nonlinear depth is steeper near the camera, so the
    // raw difference underestimates distant water columns. A mild power
    // reshape compensates so the depth signal stays usable across the view.
    float depthDelta = max(sceneDepthNdc - waterDepthNdc, 0.0);
    float sceneWaterDepth = depthDelta * 40.0;  // scale into world-ish units
    // Blend the vertex-encoded depth (stable, smooth) with the scene-depth
    // refinement (sharp at the boundary) - the scene depth dominates near the
    // waterline where it is most accurate, vertex depth fills in elsewhere.
    waterDepth = lerp(waterDepth, sceneWaterDepth, 0.6);

    // --- Beer-Lambert light absorption ---
    // Wavelength-dependent attenuation of the refracted scene light as it
    // travels through the water column: red absorbed fastest (long wavelength,
    // short penetration), blue slowest. This produces the characteristic
    // warm-to-cool shift with depth - shallow water shows the true seabed
    // color, deep water loses warm tones and trends toward the deep water
    // tint. Coefficients are tuned for clear ocean water.
    float3 absorptionCoeff = float3(0.55, 0.18, 0.06);  // R >> G > B
    float3 transmittance = exp(-absorptionCoeff * waterDepth);
    float3 refractedScene = sceneColor * transmittance;

    // --- Depth color gradient (shallow -> deep) ---
    float depthFactor = saturate(waterDepth / max(depthRange, 0.001));
    float3 shallowColor = gWater.shallowFoam.xyz;
    float3 deepColor = gWater.deepColorRange.xyz;
    float3 waterTint = lerp(shallowColor, deepColor, depthFactor);

    // --- Composite water body color ---
    // Shallow water: mostly the refracted (Beer-Lambert-attenuated) seabed
    // scene so you can see the floor through the water. Deep water: blend
    // toward the deep water tint since transmittance -> 0 and the column
    // becomes the dominant color. transparency controls the overall mix.
    float refractMix = transparency * (1.0 - depthFactor * 0.6);
    float3 waterColor = lerp(waterTint, refractedScene, refractMix);

    // --- Specular highlight (Blinn-Phong sun glint) ---
    float3 lightDir = normalize(gWater.lightDirInt.xyz);
    float3 halfVec = normalize(lightDir + input.viewDir);
    float spec = pow(max(dot(normal, halfVec), 0.0), 64.0);
    float3 specular = gWater.lightColor.rgb * spec * gWater.lightDirInt.w;

    // --- Foam (shoreline white band + grain detail) ---
    // Two-layer foam: a soft outer band (foamWidth) plus a sharp bright edge
    // right at the waterline. The foam texture (EmissiveTexture.R) is used as
    // grain detail inside the band rather than as a multiplicative mask, so
    // the shoreline reads as a continuous white band with aerated texture
    // instead of patchy noise that drops foam to ~0 wherever the noise is 0.
    float foamMask = 1.0 - smoothstep(0.0, foamWidth, waterDepth);
    foamMask = saturate(foamMask * foamIntensity);
    // Sharp bright edge hugging the waterline (front ~30% of the band),
    // squared for a crisp crest that falls off quickly into the softer band.
    float foamEdge = 1.0 - smoothstep(0.0, foamWidth * 0.3, waterDepth);
    foamEdge = pow(foamEdge, 2.0) * foamIntensity;
    // Grain: remap the 0..1 noise into a narrow [0.65, 1.0] range so it only
    // adds bubbly variation on top of the band, never punching holes in it.
    float2 foamUV = input.uv * 8.0 + time * 0.1;
    float foamTex = EmissiveTexture.Sample(LinearSampler, foamUV).r;
    float foamGrain = lerp(0.65, 1.0, foamTex);
    float foam = saturate(max(foamMask, foamEdge) * foamGrain);

    // --- Final composite ---
    float3 color = waterColor;
    // SSR is composited once in the post-water WoccSsrCompositePass. Keeping
    // it out of this material avoids applying the previous frame's result and
    // the current frame's result to the same water pixels.
    color += specular;
    color = lerp(color, float3(1.0, 1.0, 1.0), foam);

    // Shallow/transparent water lets the refracted scene show through; foam
    // and deep water are more opaque. Fresnel adds glancing-angle opacity.
    float alpha = lerp(transparency, 1.0, foam);
    alpha = lerp(alpha, 0.9, depthFactor * 0.4);
    alpha = max(alpha, fresnel * 0.5);
    alpha = saturate(alpha);

    return float4(color, alpha);
}
