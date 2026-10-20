// FoliageGeometry/1. Authored heightReference must be positive and finite.
// PBR grass uses 0.5 metres; dedicated canopy uses 2.0 metres. A material's
// color/depth/shadow family supplies the same profile and current wind field.
// This preserves the existing WindField64 ABI and all authored wave semantics.
struct ArpFoliageWindField
{
    float4 directionSpeed;
    float4 timeGustTurb;
    float4 flutterParams;
    float4 interactionCenter;
};

float3 arpDeformFoliagePosition(float3 worldPos, float3 localPos,
    float3 windWeight, float2 instancePhase, float3 instanceWorldPos,
    ArpFoliageWindField wind, float heightReference)
{
    float primaryBend = windWeight.x;
    float flutterWeight = windWeight.y;
    float stiffness = windWeight.z;
    float lodClass = instancePhase.y;
    float phase = instancePhase.x;
    if (lodClass >= 3.0 || stiffness == 1.0) return worldPos;
    float time = wind.timeGustTurb.x + phase;
    float windSpeed = max(wind.directionSpeed.w, 3.0);
    float3 configuredDir = wind.directionSpeed.xyz;
    float3 windDir = length(configuredDir.xz) > 0.001
        ? normalize(configuredDir) : float3(0.8, 0.0, 0.6);
    float heightFrac = saturate(abs(localPos.y) / heightReference);
    float flex = 1.0 - stiffness;
    float3 crossDir = float3(-windDir.z, 0.0, windDir.x);
    float spatialPhase = dot(instanceWorldPos.xz, windDir.xz) * 0.70;
    float clumpWave = sin(phase * 6.2831853 + spatialPhase
        + wind.timeGustTurb.x * 1.65);
    float clumpAmplitude = primaryBend * flex * heightFrac * (0.050 + windSpeed * 0.040);
    float3 displacement = windDir * (clumpWave * clumpAmplitude);
    displacement += crossDir * (cos(phase * 6.2831853 + spatialPhase * 1.37
        + wind.timeGustTurb.x * 1.18) * clumpAmplitude * 0.28);
    if (lodClass <= 1.0) {
        float gustAmp = max(wind.timeGustTurb.y, 0.25);
        float gustFreq = wind.timeGustTurb.z;
        float gustWave = sin(dot(worldPos.xz, windDir.xz) * gustFreq + time * gustFreq * 2.0);
        float gustStrength = gustAmp * heightFrac * windSpeed * 0.075 * flex;
        displacement += windDir * gustWave * gustStrength;
        displacement += crossDir * gustWave * gustStrength * (0.45 + 0.25 * primaryBend);
    }
    if (lodClass <= 0.0) {
        float turbNoise = sin(worldPos.x * 1.3 + time * 1.7) * cos(worldPos.z * 1.1 + time * 1.3);
        displacement += crossDir * turbNoise * wind.timeGustTurb.w * heightFrac
            * windSpeed * 0.08 * flex;
    }
    if (lodClass <= 0.0 && flutterWeight > 0.001) {
        float flutterWave = sin(time * max(wind.flutterParams.x, 5.0) + phase * 6.28318);
        displacement += crossDir * flutterWave * flutterWeight * max(wind.flutterParams.y, 0.35)
            * heightFrac * 0.12 * flex;
        displacement.y += abs(flutterWave) * flutterWeight * heightFrac * 0.018 * flex;
    }
    float interactRadius = wind.flutterParams.z;
    if (interactRadius > 0.001) {
        float3 toCenter = worldPos - wind.interactionCenter.xyz;
        float distanceToCenter = length(toCenter.xz);
        if (distanceToCenter < interactRadius) {
            float falloff = 1.0 - saturate(distanceToCenter / interactRadius);
            float3 pushDir = float3(toCenter.x, 0.0, toCenter.z);
            float pushAmount = falloff * falloff * wind.flutterParams.w * heightFrac * 0.3 * flex;
            displacement += pushDir / max(length(pushDir.xz), 0.001) * pushAmount;
        }
    }
    return worldPos + displacement;
}

// Coverage compares factored alpha with the same cutoff in every pass. Sample
// gradients must already exist before indexing/discard in an Emulated shader.
bool arpFoliageAlphaCovered(float textureAlpha, float alphaFactor, float cutoff)
{
    return textureAlpha * alphaFactor >= cutoff;
}
