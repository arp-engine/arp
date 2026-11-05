// Shared surface math. Callers explicitly author the texture and vertex ABI.
VertexOutput surfaceVertex(VertexInput input)
{
    ArpSkinningBlend blend = arpEmptySkinningBlend();
    [unroll]
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint joint = uint(max(input.joints[lane], 0.0));
        float weight = input.weights[lane];
        if (weight > 0.0 && joint < gSkinning.jointCount && joint < kMaxSurfaceJoints)
        {
            float4x4 matrix = gSkinning.skinMatrices[joint];
            float3 previousNormal = blend.normal;
            arpAccumulateSkinning(blend, matrix, matrix, false, weight,
                input.position, input.normal, float3(1.0, 0.0, 0.0));
            // Optional CPU-validated inverse-transpose matrices for full
            // affine instances. Default zero preserves existing palettes.
            uint normalIndex = gSkinning.pad0 + joint;
            if (gSkinning.pad0 != 0 && normalIndex < kMaxSurfaceJoints)
            {
                blend.normal = previousNormal
                    + mul((float3x3)gSkinning.skinMatrices[normalIndex], input.normal) * weight;
            }
        }
    }
    VertexOutput output;
    float3 worldPosition = arpSkinningWorldPosition(blend.currentPosition, blend.weightSum,
        gSkinning.worldTranslationAndScale, gSkinning.worldRotation);
    output.position = mul(gSurface.viewProjection, float4(worldPosition, 1.0));
    output.normal = arpRotateSkinningDirection(blend.normal, gSkinning.worldRotation);
    output.uv = input.uv;
    assignSurfaceLayer(output, input);
    uint materialFlags = uint(gSurface.surfaceParameters.y);
    if ((materialFlags & 2u) != 0u)
    {
        float3 right = normalize(gSurface.viewProjection[0].xyz);
        float3 up = -normalize(gSurface.viewProjection[1].xyz);
        float3 forward = normalize(cross(right, up));
        float3 n = output.normal * rsqrt(max(dot(output.normal, output.normal), 0.000001));
        float3 viewNormal = float3(dot(n, right), dot(n, up), dot(n, forward));
        float3 reflection = reflect(float3(0.0, 0.0, -1.0), viewNormal);
        float denominator = 2.0 * sqrt(dot(reflection.xy, reflection.xy)
            + (reflection.z + 1.0) * (reflection.z + 1.0));
        output.uv = reflection.xy / max(denominator, 0.000001) + 0.5;
    }
    uint uvMatrix = materialFlags >> 8u;
    if ((materialFlags & 4u) != 0u && uvMatrix < kMaxSurfaceJoints)
        output.uv = mul(gSkinning.skinMatrices[uvMatrix], float4(output.uv, 0.0, 1.0)).xy;
    return output;
}

float4 surfaceFragment(VertexOutput input)
{
    float4 color = sampleSurfaceLayer(input) * gSurface.tintAndAlpha;
    if (color.a < gSurface.surfaceParameters.x)
        discard;
    float lengthSquared = dot(input.normal, input.normal);
    float3 normal = input.normal * rsqrt(max(lengthSquared, 0.000001));
    float diffuse = max(dot(normal, -gSurface.lightDirectionAndAmbient.xyz), 0.0);
    float3 ambient = float3(gSurface.surfaceParameters.zw, gSurface.lightDirectionAndAmbient.w);
    float3 lighting = ambient
        + diffuse * gSurface.lightColorAndIntensity.rgb * gSurface.lightColorAndIntensity.w;
    float unshaded = gSurface.surfaceParameters.y <= 1.0 ? gSurface.surfaceParameters.y
        : ((uint(gSurface.surfaceParameters.y) & 1u) != 0u ? 1.0 : 0.0);
    color.rgb *= lerp(saturate(lighting), float3(1.0), unshaded);
    return color;
}
