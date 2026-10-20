float queenShadowTap(float2 uv, float2 offset, float depth, float2 texel)
{
    const float storedDepth = gQueenShadowMap.Sample(gQueenShadowSampler, uv + offset * texel);
    return depth - 0.0025 <= storedDepth ? 1.0 : 0.0;
}

float4 queenSurface(float3 surfaceNormal, float3 worldPosition, float4 lightPosition, float4 surfaceColor, float2 texel, float enableShadow, float flipShadowY)
{
    float3 normal = normalize(surfaceNormal);
    const float3 lightDirection = normalize(float3(0.42, 0.78, -0.46));
    const float3 viewDirection = normalize(float3(-worldPosition.x, 3.5, -7.5 - worldPosition.z));
    if (dot(normal, viewDirection) < 0.0)
        normal = -normal;
    const float diffuse = max(dot(normal, lightDirection), 0.0);
    const float hemi = saturate(normal.y * 0.5 + 0.5);
    const float3 ambient = lerp(float3(0.22, 0.25, 0.34), float3(0.60, 0.55, 0.48), hemi);
    const float3 key = float3(0.88, 0.76, 0.62) * diffuse * 0.78;
    const float3 halfVector = normalize(lightDirection + viewDirection);
    const float specular = pow(max(dot(normal, halfVector), 0.0), 28.0) * 0.16;
    const float3 shadowNdc = lightPosition.xyz / max(lightPosition.w, 0.0001);
    float2 shadowUv = shadowNdc.xy * 0.5 + 0.5;
    shadowUv.y = lerp(shadowUv.y, 1.0 - shadowUv.y, flipShadowY);
    float visibility = 1.0;
    if (all(shadowUv >= 0.0) && all(shadowUv <= 1.0) && shadowNdc.z >= 0.0 && shadowNdc.z <= 1.0)
    {
        const float litSamples =
            queenShadowTap(shadowUv, float2(-1, -1), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2( 0, -1), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2( 1, -1), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2(-1,  0), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2( 0,  0), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2( 1,  0), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2(-1,  1), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2( 0,  1), shadowNdc.z, texel) +
            queenShadowTap(shadowUv, float2( 1,  1), shadowNdc.z, texel);
        visibility = lerp(1.0, lerp(0.42, 1.0, litSamples / 9.0), enableShadow);
    }
    return float4(surfaceColor.rgb * (ambient + key * visibility) + specular * visibility, surfaceColor.a);
}
