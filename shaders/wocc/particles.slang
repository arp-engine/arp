// ParticlePushConstants — push constants for the particle shader.
// Fields: visualFamily (ParticleVisualFamily enum), isTextured (0/1),
// flipbookTilesX/Y (atlas subdivision), billboardMode, depthFadeEnabled
// (0=off, 1=on soft particle depth fade), depthFadeDistance (world-space).
struct ParticlePushConstants
{
    float4x4 viewProjection;
    uint visualFamily;
    uint isTextured;
    uint flipbookTilesX;
    uint flipbookTilesY;
    uint billboardMode;
    uint depthFadeEnabled;
    float depthFadeDistance;
    float pad2;
    float pad3;
};

[[vk::push_constant]]
ParticlePushConstants gParticles;

static const uint ParticleVisualFamilyUnknown = 0;
static const uint ParticleVisualFamilySpriteAlpha = 1;
static const uint ParticleVisualFamilySpriteAdditive = 2;
static const uint ParticleVisualFamilyLightningThin = 3;
static const uint ParticleVisualFamilyStormCloud = 4;
static const uint ParticleVisualFamilyFallbackLegacy = 5;

[[vk::binding(0, 1)]] Texture2D MainTexture;
[[vk::binding(1, 1)]] SamplerState MainSampler;
[[vk::binding(0, 2)]] Texture2D<float> DepthTexture;  // scene depth for soft particle fade

struct VertexInput
{
    float3 worldPosition : POSITION0;
    float2 corner : TEXCOORD0;
    float4 color : COLOR0;
    float4 particleParams : TEXCOORD1;   // (size, normalizedAge, flipbookFrame, rotation)
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 corner : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 particleParams : TEXCOORD2;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    float4 clip = mul(gParticles.viewProjection, float4(input.worldPosition, 1.0));
    bool lightningThin = gParticles.visualFamily == ParticleVisualFamilyLightningThin;
    bool stormCloud = gParticles.visualFamily == ParticleVisualFamilyStormCloud;
    float widthScale = input.particleParams.x * (lightningThin ? 0.0048 : (stormCloud ? 0.040 : 0.048));
    float heightScale = input.particleParams.x * (lightningThin ? 0.135 : (stormCloud ? 0.040 : 0.048));
    // Apply particle rotation to corner offset
    float rotation = input.particleParams.w;
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    float2 rotatedCorner = float2(
        input.corner.x * cosR - input.corner.y * sinR,
        input.corner.x * sinR + input.corner.y * cosR);
    if (lightningThin)
    {
        float seedA = frac(sin(dot(input.worldPosition, float3(12.9898, 78.233, 37.719))) * 43758.5453);
        float seedB = frac(sin(dot(input.worldPosition, float3(39.346, 11.135, 83.155))) * 24634.6345);
        float seedC = frac(sin(dot(input.worldPosition, float3(73.156, 52.235, 19.438))) * 16483.4231);
        float2 stormCenter = float2(
            lerp(-0.92, 0.92, seedA),
            lerp(-0.58, 0.82, seedB));
        float stormSlope = lerp(-0.42, 0.42, seedC);
        float2 stormOffset = float2(
            rotatedCorner.x * input.particleParams.x * 0.020
                + rotatedCorner.y * stormSlope,
            rotatedCorner.y * input.particleParams.x * 0.19);
        clip = float4(stormCenter + stormOffset, 0.0, 1.0);

        float2 baseUV = input.corner * 0.5 + 0.5;
        VertexOutput output;
        output.position = clip;
        output.corner = input.corner;
        output.uv = baseUV;
        output.particleParams = input.particleParams;
        output.color = input.color;
        return output;
    }
    float2 screenOffset;
    if (gParticles.billboardMode == 1u)
    {
        // LocalAligned (cylindrical billboard): upright on Y, screen-space X.
        // The Y offset stays in world space (upright column) while X offset
        // is screen-space (horizontal rotation toward camera).
        float worldHeight = input.particleParams.x * (lightningThin ? 0.62 : (stormCloud ? 0.30 : 0.5));
        float4 worldUp = float4(0.0, rotatedCorner.y * worldHeight, 0.0, 0.0);
        float4 worldPos = float4(input.worldPosition, 1.0) + worldUp;
        float4 worldClip = mul(gParticles.viewProjection, worldPos);
        float2 horizontalOffset = float2(
            rotatedCorner.x * widthScale, 0.0) * max(clip.w, 0.001);
        screenOffset = (worldClip.xy - clip.xy) + horizontalOffset;
    }
    else if (gParticles.billboardMode == 2u)
    {
        // WorldAligned: offset in world space, then project (no camera-facing)
        float worldWidth = input.particleParams.x * (lightningThin ? 0.28 : (stormCloud ? 0.30 : 0.5));
        float worldHeight = input.particleParams.x * (lightningThin ? 0.62 : (stormCloud ? 0.30 : 0.5));
        float4 worldOffset = float4(
            rotatedCorner.x * worldWidth,
            rotatedCorner.y * worldHeight,
            0.0, 0.0);
        float4 worldPos = float4(input.worldPosition, 1.0) + worldOffset;
        float4 worldClip = mul(gParticles.viewProjection, worldPos);
        screenOffset = worldClip.xy - clip.xy;
    }
    else
    {
        // ViewAligned (default): screen-space offset (always faces camera)
        screenOffset = float2(
            rotatedCorner.x * widthScale,
            rotatedCorner.y * heightScale) * max(clip.w, 0.001);
    }
    clip.xy += screenOffset;

    // Flipbook UV: compute tile offset based on current frame
    float2 baseUV = input.corner * 0.5 + 0.5;
    float2 uv = baseUV;
    uint flipbookFrame = uint(input.particleParams.z);
    if (gParticles.flipbookTilesX > 1u || gParticles.flipbookTilesY > 1u)
    {
        uint tileX = flipbookFrame % gParticles.flipbookTilesX;
        uint tileY = flipbookFrame / gParticles.flipbookTilesX;
        float invTilesX = 1.0 / float(gParticles.flipbookTilesX);
        float invTilesY = 1.0 / float(gParticles.flipbookTilesY);
        uv.x = baseUV.x * invTilesX + float(tileX) * invTilesX;
        uv.y = baseUV.y * invTilesY + float(tileY) * invTilesY;
    }

    VertexOutput output;
    output.position = clip;
    output.corner = input.corner;
    output.uv = uv;
    output.particleParams = input.particleParams;
    output.color = input.color;
    return output;
}

float4 sampleMainTexture(float2 uv)
{
    if (gParticles.isTextured != 0)
    {
        return MainTexture.Sample(MainSampler, uv);
    }
    return float4(1.0, 1.0, 1.0, 1.0);
}

float4 computeSpriteAlpha(float4 baseColor, float2 corner, float4 particleParams)
{
    // particleParams: (size, normalizedAge, flipbookFrame, rotation) — unused for sprite alpha shape
    float radiusSq = dot(corner, corner);
    float core = saturate(1.0 - radiusSq);
    float glow = saturate(1.0 - radiusSq * 2.25);
    float alpha = baseColor.a * (core * core * 0.72 + glow * glow * 0.28);
    return float4(baseColor.rgb * alpha, alpha);
}

float4 computeLightningThin(float4 baseColor, float2 corner, float4 particleParams)
{
    float agePhase = particleParams.y * 21.0;
    float tipFade = saturate((corner.y + 1.0) * 4.8) * saturate((1.0 - corner.y) * 2.4);
    float centerWeight = saturate(1.0 - abs(corner.y) * 0.28);
    float electricLength = pow(centerWeight, 0.85) * tipFade;

    float segmentCoord = saturate((corner.y + 1.0) * 0.5) * 9.0;
    float segmentIndex = floor(segmentCoord);
    float segmentT = smoothstep(0.0, 1.0, frac(segmentCoord));
    float segmentSeedA = sin(segmentIndex * 12.9898 + agePhase * 0.11) * 43758.5453;
    float segmentSeedB = sin((segmentIndex + 1.0) * 12.9898 + agePhase * 0.11) * 43758.5453;
    float segmentOffsetA = (frac(segmentSeedA) - 0.5) * 0.48;
    float segmentOffsetB = (frac(segmentSeedB) - 0.5) * 0.48;
    float mainWobble = lerp(segmentOffsetA, segmentOffsetB, segmentT)
        + sin(corner.y * 28.0 + agePhase * 1.15) * 0.035;
    float branchWobbleA = mainWobble * 0.45 + cos(corner.y * 14.0 + agePhase * 0.72) * 0.18;
    float branchWobbleB = mainWobble * -0.35 + sin(corner.y * 18.0 - agePhase * 0.58) * 0.15;

    float mainDistance = abs(corner.x + mainWobble);
    float branchDistanceA = abs(corner.x - mainWobble * 0.58 + branchWobbleA);
    float branchDistanceB = abs(corner.x + mainWobble * 0.35 - branchWobbleB);

    float coreFilament = pow(saturate(1.0 - mainDistance * 8.0), 2.2);
    float branchFilamentA = pow(saturate(1.0 - branchDistanceA * 13.0), 2.1);
    float branchFilamentB = pow(saturate(1.0 - branchDistanceB * 16.0), 1.9);
    float electricFilament = electricLength
        * (coreFilament + branchFilamentA * 0.42 + branchFilamentB * 0.24);

    float electricCore = electricLength * pow(saturate(1.0 - mainDistance * 5.5), 1.45);
    float electricHalo = electricLength * pow(saturate(1.0 - mainDistance * 1.9), 0.95);

    float branchFlash = pow(
        saturate(1.0 - abs(sin(corner.y * 12.0 + agePhase * 1.2) + corner.x * 2.2)),
        5.0);
    float sparkBand = saturate(1.0 - abs(corner.y + 0.84) * 10.0);
    float sparkColumns = pow(
        saturate(1.0 - abs(sin(corner.x * 36.0 + agePhase * 1.7) * 0.8) * 1.18),
        7.0);
    float baseSparks = sparkBand * sparkColumns * (0.18 + branchFlash * 0.22);

    float electricPulse = 0.68 + 0.32 * sin(agePhase * 0.9 + corner.y * 7.5 + dot(corner, corner) * 6.0);
    float electricFlicker = 0.86 + 0.14 * sin(agePhase * 2.3 - corner.y * 11.0);
    float3 coreWhite = float3(0.92, 0.98, 1.0);
    float3 coolFilament = float3(0.18, 0.72, 1.0);
    float3 warmFilament = float3(0.78, 0.42, 0.12);
    float warmMix = branchFlash * 0.18;
    float tintEnergy = saturate(dot(baseColor.rgb, float3(0.2126, 0.7152, 0.0722)));
    float3 filamentColor = lerp(coolFilament, warmFilament, warmMix);
    filamentColor *= 0.9 + tintEnergy * 0.18;

    float branchEnergy = electricLength * (branchFilamentA * 0.52 + branchFilamentB * 0.34) * branchFlash;
    float alpha = baseColor.a * saturate(
        electricFilament * electricPulse * 1.15
        + electricCore * 0.55
        + electricHalo * 0.34
        + branchEnergy
        + baseSparks * 0.65);
    float3 color = filamentColor * alpha * (2.4 + electricFlicker * 0.35);
    color += coreWhite * electricCore * electricPulse * 3.2;
    color += coolFilament * electricHalo * 0.55;
    color += lerp(coolFilament, coreWhite, 0.35) * branchEnergy * 1.5;
    return float4(color, alpha);
}

float4 computeStormCloud(float4 baseColor, float2 corner, float4 particleParams)
{
    // StormCloud: dark tribulation thundercloud (渡劫雷云).
    // Noise DEFINES the shape — no underlying circle.
    // corner: [-1,1], particleParams: (size, age, frame, rotation)

    float age = particleParams.y * 14.0;
    float dist = length(corner);

    // --- 1. Warp uv with age for drifting motion ---
    float2 warp = float2(
        frac(sin(age * 0.07) * 43758.5453) - 0.5,
        frac(sin(age * 0.11) * 24634.6345) - 0.5) * 0.3;
    float2 p = corner + warp;

    // --- 2. Two layers of value noise at different scales ---
    // Layer A: coarse cloud blobs
    float2 ca = floor(p * 2.0);
    float2 fa = frac(p * 2.0);
    float na00 = frac(sin(dot(ca, float2(12.9898, 78.233))) * 43758.5453);
    float na10 = frac(sin(dot(ca + float2(1,0), float2(12.9898, 78.233))) * 43758.5453);
    float na01 = frac(sin(dot(ca + float2(0,1), float2(12.9898, 78.233))) * 43758.5453);
    float na11 = frac(sin(dot(ca + float2(1,1), float2(12.9898, 78.233))) * 43758.5453);
    float2 sa = fa * fa * (3.0 - 2.0 * fa); // smoothstep
    float noiseA = lerp(lerp(na00, na10, sa.x), lerp(na01, na11, sa.x), sa.y);

    // Layer B: fine wispy detail
    float2 cb = floor(p * 5.0);
    float2 fb = frac(p * 5.0);
    float nb00 = frac(sin(dot(cb, float2(39.346, 11.135))) * 24634.6345);
    float nb10 = frac(sin(dot(cb + float2(1,0), float2(39.346, 11.135))) * 24634.6345);
    float nb01 = frac(sin(dot(cb + float2(0,1), float2(39.346, 11.135))) * 24634.6345);
    float nb11 = frac(sin(dot(cb + float2(1,1), float2(39.346, 11.135))) * 24634.6345);
    float2 sb = fb * fb * (3.0 - 2.0 * fb);
    float noiseB = lerp(lerp(nb00, nb10, sb.x), lerp(nb01, nb11, sb.x), sb.y);

    // --- 3. Cloud shape: noise IS the shape ---
    // Band-pass: only a narrow band of noise values = cloud.
    // This creates wispy holes AND thin edges.
    float cloud = noiseA * 0.65 + noiseB * 0.35; // [0, 1]
    float cloudShape = smoothstep(0.45, 0.60, cloud) * (1.0 - smoothstep(0.80, 0.95, cloud));

    // --- 4. Gentle radial fade only at very outer edge ---
    float edgeFade = 1.0 - smoothstep(0.70, 1.0, dist);

    // --- 5. Final density ---
    float density = cloudShape * edgeFade * 0.35;

    // --- 6. Cloud color: very dark gray, almost black ---
    float3 cloudCol = float3(0.04, 0.03, 0.06);

    // Slight brightness where cloud is thicker
    cloudCol += float3(0.06, 0.04, 0.08) * cloudShape * 0.5;

    // --- 7. Lightning bolt ---
    float boltSeed = frac(sin(corner.y * 7.0 + age * 0.15) * 43758.5453);
    float boltPhase = frac(sin(boltSeed * 12.9898 + age * 0.4) * 24634.6345);
    float boltX = (boltPhase - 0.5) * 0.5;
    float bDist = abs(corner.x - boltX);
    float bW = 0.03;
    float boltCore = exp(-bDist * bDist / (bW * bW));
    float boltGlow = exp(-bDist * bDist / (bW * 6.0));
    // Branch
    float brSeed = frac(sin(corner.y * 13.0 + age * 0.25) * 16483.4231);
    float brX = boltX + (brSeed - 0.5) * 0.4;
    float brDist = abs(corner.x - brX);
    float branch = exp(-brDist * brDist / (bW * 3.0)) * 0.3;
    float boltI = boltCore * 5.0 + boltGlow * 0.6 + branch;
    float3 boltCol = float3(0.5, 0.7, 1.0) * boltI + float3(1.0, 1.0, 1.0) * boltCore * 6.0;

    // --- 8. Flash ---
    float flashR = frac(sin(corner.x * 19.0 + corner.y * 37.0 + age * 0.2) * 43758.5453);
    float flash = step(0.96, flashR) * pow(saturate(1.0 - abs(flashR - 0.98) * 50.0), 2.0);

    // --- 9. Compose ---
    float3 col = cloudCol + boltCol + float3(0.5, 0.7, 1.0) * flash * 0.4;
    float alpha = saturate(density + boltI * 0.9 + flash * 2.0);

    return float4(col * max(alpha, 0.001), alpha);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    bool flipbookExplosion = gParticles.visualFamily == ParticleVisualFamilySpriteAdditive
        && gParticles.isTextured != 0u
        && (gParticles.flipbookTilesX > 1u || gParticles.flipbookTilesY > 1u);
    float4 texColor = sampleMainTexture(input.uv);
    float4 baseColor = input.color * texColor;

    // Soft particle depth fade
    if (gParticles.depthFadeEnabled != 0u)
    {
        uint depthW, depthH;
        DepthTexture.GetDimensions(depthW, depthH);
        float2 screenUV = input.position.xy / float2(float(depthW), float(depthH));
        float sceneDepth = DepthTexture.Sample(MainSampler, screenUV);
        float particleDepth = input.position.z / input.position.w;
        float depthDelta = sceneDepth - particleDepth;
        float fadeAlpha = saturate(depthDelta / gParticles.depthFadeDistance);
        baseColor.a *= fadeAlpha;
    }

    if (gParticles.visualFamily == ParticleVisualFamilyLightningThin)
    {
        return computeLightningThin(baseColor, input.corner, input.particleParams);
    }
    if (gParticles.visualFamily == ParticleVisualFamilyStormCloud)
    {
        return computeStormCloud(baseColor, input.corner, input.particleParams);
    }
    if (flipbookExplosion)
    {
        float age = input.particleParams.y;
        float radius = length(input.corner);
        float radial = saturate(1.0 - radius * radius * 0.62);
        float softCore = pow(radial, 1.18);
        float angle = atan2(input.corner.y, input.corner.x);
        float spokeBand = 0.5 + 0.5 * cos(angle * 8.0 + age * 6.5);
        float spokes = pow(saturate(spokeBand), 5.2) * saturate(1.0 - radius * 0.72);
        float shockRing = pow(
            saturate(1.0 - abs(radius - (0.38 + age * 0.08)) * 3.6),
            2.6);
        float texGlow = max(texColor.a, max(texColor.r, texColor.g * 0.78));
        float texLuma = dot(texColor.rgb, float3(0.299, 0.587, 0.114));
        float ember = saturate(texColor.g * 1.05 + texColor.r * 0.44 + spokes * 0.16);
        float energy = max(softCore * 0.44 + spokes * 0.24 + shockRing * 0.18, texGlow * 0.58);
        float alpha = saturate(
            (softCore * 0.12 + spokes * 0.09 + shockRing * 0.07 + texGlow * 0.18)
            * input.color.a);
        float3 fireOuter = float3(1.0, 0.22, 0.05);
        float3 fireInner = float3(1.0, 0.82, 0.26);
        float3 heat = lerp(fireOuter, fireInner, ember);
        float3 plume = heat * (0.18 + energy * 0.20 + texLuma * 0.16);
        float3 ringColor = float3(1.0, 0.55, 0.12) * shockRing * 0.16;
        float3 core = float3(1.0, 0.95, 0.72) * pow(max(energy, softCore), 0.95) * 0.08;
        float3 color = plume + ringColor + core;
        color *= 0.42 + saturate(dot(input.color.rgb, float3(0.299, 0.587, 0.114))) * 0.10;
        return float4(color, alpha);
    }

    // SpriteAlpha / SpriteAdditive / FallbackLegacy / Unknown
    return computeSpriteAlpha(baseColor, input.corner, input.particleParams);
}
