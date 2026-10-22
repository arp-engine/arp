// TerrainMaterial/1. Compose after generated TerrainImages/TerrainArrays/
// TerrainSamplers/TerrainRecords/TerrainPages/TerrainFoam and core/terrain_surface.
// No fixed-LOD path. Every projection receives the original continuous world
// gradients captured before indexing, page lookup, sparse selection or discard.
struct ArpTerrainGradients
{
    float3 position;
    float3 dx;
    float3 dy;
};

ArpTerrainGradients arpCaptureTerrainGradients(float3 worldPosition)
{
    ArpTerrainGradients result;
    result.position = worldPosition;
    result.dx = ddx(worldPosition);
    result.dy = ddy(worldPosition);
    return result;
}

bool arpLoadTerrainMaterial(uint bufferIndex, uint materialIndex,
    uint imageTableId, uint arrayTableId, uint samplerTableId,
    out ArpBindlessRecord_TerrainRecords material)
{
    bool fault;
    material = TerrainRecords_load(bufferIndex, materialIndex, fault);
    if (fault) return false;
    uint2 generation = arpBindlessSnapshotGeneration();
    bool valid = all(material.generation.xy == generation)
        && any(material.generation.zw != uint2(0u))
        && all(material.domains == uint4(imageTableId, arrayTableId, samplerTableId, 1u))
        && imageTableId != 0u && arrayTableId != 0u && samplerTableId != 0u
        && imageTableId != arrayTableId && imageTableId != samplerTableId && arrayTableId != samplerTableId
        && all(isfinite(material.texParams)) && all(material.texParams > 0.0)
        && all(isfinite(material.weightParams)) && material.weightParams.x > 0.0 && material.weightParams.z > 0.0
        && all(isfinite(material.layerRough012)) && all(material.layerRough012 >= 0.0) && all(material.layerRough012 <= 1.0)
        && all(isfinite(material.layerRough345)) && all(material.layerRough345.xy >= 0.0) && all(material.layerRough345.xy <= 1.0)
        && all(isfinite(material.brushParams)) && material.brushParams.y >= 0.0 && material.brushParams.w >= 0.0
        && all(isfinite(material.brushColor)) && material.brushColor.a >= 0.0;
    if (!valid) {
        arpBindlessReportFault();
        material = (ArpBindlessRecord_TerrainRecords)0;
    }
    return valid;
}

// Same first-containing-page rule and inclusive bounds as the authored path.
// pageCount and maxPageVisits come from the frozen workload, never capacity
// guesses. covered=false distinguishes a valid hole from a resource fault.
bool arpSampleTerrainWeightPages(ArpBindlessRecord_TerrainRecords material,
    uint pagesBuffer, uint pageCount, uint maxPageVisits, ArpTerrainGradients gradients,
    out float4 pageA, out float4 pageB, out bool covered)
{
    pageA = 0.0;
    pageB = 0.0;
    covered = false;
    bool fault;
    uint actualCount = TerrainPages_query(pagesBuffer, fault);
    if (fault) return false;
    if (pageCount > actualCount || pageCount > maxPageVisits
        || !all(isfinite(gradients.position)) || !all(isfinite(gradients.dx)) || !all(isfinite(gradients.dy))) {
        arpBindlessReportFault();
        return false;
    }
    for (uint index = 0u; index < pageCount; ++index) {
        ArpBindlessRecord_TerrainPages page = TerrainPages_load(pagesBuffer, index, fault);
        if (fault) return false;
        float4 bounds = page.worldBounds;
        if (!all(isfinite(bounds)) || bounds.y <= bounds.x || bounds.w <= bounds.z
            || !all(page.routing.yz == material.generation.zw) || page.routing.w != 0u) {
            arpBindlessReportFault();
            return false;
        }
        if (gradients.position.x < bounds.x || gradients.position.x > bounds.y
            || gradients.position.z < bounds.z || gradients.position.z > bounds.w) continue;
        float2 span = bounds.yw - bounds.xz;
        if (!all(isfinite(span))) { arpBindlessReportFault(); return false; }
        float2 uv = (gradients.position.xz - bounds.xz) / span;
        float2 dx = gradients.dx.xz / span, dy = gradients.dy.xz / span;
        pageA = TerrainArrays_sampleGrad(material.routing.x, material.routing.w,
            uv, page.routing.x, dx, dy, fault);
        bool valid = !fault;
        pageB = TerrainArrays_sampleGrad(material.routing.y, material.routing.w,
            uv, page.routing.x, dx, dy, fault);
        if (!valid || fault) { pageA = 0.0; pageB = 0.0; return false; }
        // Coverage and surface composition share the same sampled-weight
        // domain. Invalid weights must not leave a depth/shadow silhouette.
        if (!arpTerrainWeightPagesValid(pageA, pageB)) {
            arpBindlessReportFault(); pageA = 0.0; pageB = 0.0; return false;
        }
        covered = true;
        return true;
    }
    return true;
}

// Explicit frame/history inputs; no foam alias is hidden in the material record.
bool arpTerrainShoreWetness(uint foamBuffer, float4 originExtent, float4 params,
    float3 worldPosition, out float wetness)
{
    wetness = 0.0;
    if (!all(isfinite(originExtent)) || !all(isfinite(params))) {
        arpBindlessReportFault(); return false;
    }
    float2 uv = (worldPosition.xz - originExtent.xy) * originExtent.zw;
    if (params.z <= 0.5 || any(uv < 0.0) || any(uv > 1.0)) return true;
    if (params.w < 0.0 || params.w >= 4294967040.0) {
        arpBindlessReportFault(); return false;
    }
    uint resolution = max(uint(params.w + 0.5), 1u);
    bool fault;
    uint count = TerrainFoam_query(foamBuffer, fault);
    if (fault) return false;
    if (resolution > count / resolution) { arpBindlessReportFault(); return false; }
    uint2 texel = min(uint2(uv * float(resolution)), uint2(resolution - 1u));
    uint packed = TerrainFoam_load(foamBuffer, texel.y * resolution + texel.x, fault);
    if (fault) return false;
    float swash = float((packed >> 8u) & 255u) / 255.0;
    float residual = float((packed >> 16u) & 255u) / 255.0;
    float nearWater = 1.0 - smoothstep(0.35, 2.2, abs(worldPosition.y - params.x));
    wetness = saturate(max(residual, swash * 0.42) * nearWater * params.y);
    return true;
}

uint4 arpTerrainLayer(ArpBindlessRecord_TerrainRecords material, uint layer)
{
    if (layer == 0u) return material.layer0;
    if (layer == 1u) return material.layer1;
    if (layer == 2u) return material.layer2;
    if (layer == 3u) return material.layer3;
    if (layer == 4u) return material.layer4;
    return material.layer5;
}

struct ArpBindlessTerrainSurface
{
    float3 baseColor;
    float3 normal;
    float3 emissive;
    float roughness;
    float occlusion;
};

bool arpSampleTerrainSurface(ArpBindlessRecord_TerrainRecords material,
    ArpTerrainGradients gradients, float3 inputNormal, float4 inputTangent,
    float4 pageA, float4 pageB, float rain, float shoreWetness,
    out ArpBindlessTerrainSurface output)
{
    output = (ArpBindlessTerrainSurface)0;
    if (!all(isfinite(gradients.position)) || !all(isfinite(gradients.dx))
        || !all(isfinite(gradients.dy)) || !all(isfinite(inputNormal)) || !all(isfinite(inputTangent))
        || !arpTerrainWeightPagesValid(pageA, pageB) || !isfinite(rain) || rain < 0.0 || rain > 1.0
        || !isfinite(shoreWetness) || shoreWetness < 0.0 || shoreWetness > 1.0) {
        arpBindlessReportFault(); return false;
    }
    float3 geometryNormal = arpTerrainNormalize(inputNormal, float3(0.0, 1.0, 0.0));
    ArpTerrainWeights weights = arpTerrainBlendWeights(pageA, pageB,
        gradients.position.y, material.weightParams.y, material.weightParams.z);
    float3 an = abs(geometryNormal);
    float wallW = clamp(1.0 - an.y * 1.45, 0.0, 1.0);
    float axisW = an.x / max(1e-4, an.x + an.z);
    float2 tuv = gradients.position.xz * material.texParams.x;
    float2 dx = gradients.dx.xz * material.texParams.x, dy = gradients.dy.xz * material.texParams.x;
    float angle = arpTerrainCellAngle(gradients.position.xz);
    float2 rotated = arpTerrainRotate(tuv, angle);
    float2 rotatedDx = arpTerrainRotate(dx, angle), rotatedDy = arpTerrainRotate(dy, angle);
    float2 wallXY = gradients.position.xy * material.texParams.w;
    float2 wallZY = gradients.position.zy * material.texParams.w;
    float2 wallXYDx = gradients.dx.xy * material.texParams.w, wallXYDy = gradients.dy.xy * material.texParams.w;
    float2 wallZYDx = gradients.dx.zy * material.texParams.w, wallZYDy = gradients.dy.zy * material.texParams.w;
    float3 alb = 0.0;
    float2 detailNormal = 0.0;
    float roughBase = 0.0, roughTexture = 0.0, heightDetail = 0.0;
    uint samplerIndex = material.routing.w;
    bool failed = false, fault;
    for (uint layer = 0u; layer < 6u; ++layer) {
        float weight = arpTerrainLayerWeight(weights, layer);
        float base = layer < 4u ? material.layerRough012[layer] : material.layerRough345[layer - 4u];
        roughBase += base * weight;
        // Preserve the current authored sparse-layer threshold, including its
        // base roughness contribution when a texture set is skipped.
        if (weight <= 0.003) continue;
        uint4 maps = arpTerrainLayer(material, layer);
        float scale = layer == 0u ? 1.0 : layer == 1u ? 0.9 : layer == 2u ? 0.8
            : layer == 3u ? 0.7 : layer == 4u ? 0.6 : 0.5;
        bool stochastic = layer == 1u || layer == 3u;
        float2 uv = (stochastic ? rotated : tuv) * scale;
        float2 uvDx = (stochastic ? rotatedDx : dx) * scale;
        float2 uvDy = (stochastic ? rotatedDy : dy) * scale;
        float3 color = TerrainImages_sampleGrad(maps.x, samplerIndex, uv, uvDx, uvDy, fault).rgb;
        failed = failed || fault;
        if (layer == 0u) {
            float3 coarse = TerrainImages_sampleGrad(maps.x, samplerIndex,
                uv * 0.31, uvDx * 0.31, uvDy * 0.31, fault).rgb;
            failed = failed || fault;
            color = lerp(color, coarse, 0.42);
        }
        if (layer == 4u && wallW > 0.01) {
            float3 a = TerrainImages_sampleGrad(maps.x, samplerIndex, wallXY, wallXYDx, wallXYDy, fault).rgb;
            failed = failed || fault;
            float3 b = TerrainImages_sampleGrad(maps.x, samplerIndex, wallZY, wallZYDx, wallZYDy, fault).rgb;
            failed = failed || fault;
            color = lerp(color, lerp(a, b, axisW), wallW);
        }
        alb += color * weight;
        float3 normal = TerrainImages_sampleGrad(maps.y, samplerIndex, uv, uvDx, uvDy, fault).xyz * 2.0 - 1.0;
        failed = failed || fault;
        float normalStrength = layer == 0u ? 0.65 : layer == 1u ? 0.7 : layer == 2u ? 0.6
            : layer == 3u ? 0.8 : layer == 4u ? 0.9 * (1.0 - wallW) : 0.5;
        detailNormal += normal.xy * weight * normalStrength;
        float rough = TerrainImages_sampleGrad(maps.z, samplerIndex, uv, uvDx, uvDy, fault).r;
        failed = failed || fault;
        float height = TerrainImages_sampleGrad(maps.w, samplerIndex, uv, uvDx, uvDy, fault).r;
        failed = failed || fault;
        roughTexture += rough * weight;
        heightDetail += height * weight;
    }
    float effectiveWetness = max(saturate(weights.wetness + rain * (1.0 - weights.wetness)), shoreWetness);
    alb *= lerp(1.0, 0.65, effectiveWetness * 0.6);
    float macro = TerrainImages_sampleGrad(material.routing.z, samplerIndex,
        gradients.position.xz * material.texParams.y, gradients.dx.xz * material.texParams.y,
        gradients.dy.xz * material.texParams.y, fault).r;
    failed = failed || fault;
    alb *= lerp(0.92, 1.08, macro);
    float macro2 = TerrainImages_sampleGrad(material.routing.z, samplerIndex,
        gradients.position.xz * material.texParams.z + 0.37, gradients.dx.xz * material.texParams.z,
        gradients.dy.xz * material.texParams.z, fault).r;
    failed = failed || fault;
    alb = lerp(alb, alb * float3(1.07, 1.03, 0.86), (macro2 - 0.5) * 0.5 * weights.first.x);
    float3 tangent = arpTerrainNormalize(inputTangent.xyz, float3(1.0, 0.0, 0.0));
    tangent = arpTerrainNormalize(tangent - geometryNormal * dot(geometryNormal, tangent),
        abs(geometryNormal.y) < 0.999 ? normalize(cross(float3(0.0, 1.0, 0.0), geometryNormal)) : float3(1.0, 0.0, 0.0));
    float3 bitangent = arpTerrainNormalize(cross(geometryNormal, tangent) * inputTangent.w, float3(0.0, 0.0, 1.0));
    float3 shadingNormal = arpTerrainNormalize(geometryNormal + tangent * detailNormal.x + bitangent * detailNormal.y, geometryNormal);
    if (weights.last.x * wallW > 0.01) {
        uint normalIndex = material.layer4.y;
        float3 rNx = TerrainImages_sampleGrad(normalIndex, samplerIndex, wallZY, wallZYDx, wallZYDy, fault).xyz * 2.0 - 1.0;
        failed = failed || fault;
        float3 rNz = TerrainImages_sampleGrad(normalIndex, samplerIndex, wallXY, wallXYDx, wallXYDy, fault).xyz * 2.0 - 1.0;
        failed = failed || fault;
        float3 perturb = lerp(float3(rNz.x, rNz.y, 0.0), float3(0.0, rNx.y, rNx.x), axisW);
        shadingNormal = arpTerrainNormalize(shadingNormal + perturb * (weights.last.x * wallW * 0.8), shadingNormal);
    }
    if (failed) return false;
    output.baseColor = alb;
    output.normal = shadingNormal;
    output.roughness = arpTerrainRoughness(roughBase, roughTexture, effectiveWetness, shoreWetness);
    output.occlusion = lerp(0.78, 1.0, heightDetail);
    if (material.brushParams.w > 0.5) {
        float d = distance(gradients.position.xz, material.brushParams.xz);
        float radius = material.brushParams.y, width = max(0.28, radius * 0.055);
        float ring = smoothstep(radius - width, radius, d) * (1.0 - smoothstep(radius, radius + width, d));
        output.emissive = material.brushColor.rgb * ring * material.brushColor.a;
    }
    return true;
}
