// PbrMaterial/1. Compose after generated MaterialRecords/MaterialImages helpers
// and core/leaf_surface.slang + core/pbr_material.slang. This six-lane record
// is distinct from the old four-float/two-uint test transport of the same size.
// Frame lighting/GI/debug/temporal/geometry state is deliberately external.
struct ArpMaterialGradients
{
    float2 uv;
    float2 dx;
    float2 dy;
};

// Call unconditionally before material/resource indexing, any branch that
// diverges across the quad, or discard. No fixed LOD substitutes for this.
ArpMaterialGradients arpCaptureMaterialGradients(float2 uv)
{
    ArpMaterialGradients gradients;
    gradients.uv = uv;
    gradients.dx = ddx(uv);
    gradients.dy = ddy(uv);
    return gradients;
}

bool arpLoadPbrMaterial(uint bufferIndex, uint materialIndex,
    uint imagesTableId, uint samplersTableId,
    out ArpBindlessRecord_MaterialRecords material)
{
    bool fault;
    material = MaterialRecords_load(bufferIndex, materialIndex, fault);
    if (fault) return false;
    uint flags = material.routing.z;
    bool leaf = (flags & 8u) != 0u;
    uint2 generation = arpBindlessSnapshotGeneration();
    bool valid = material.routing.w == 1u && (flags & ~15u) == 0u
        && material.generation.x == generation.x && material.generation.y == generation.y
        && material.generation.z == imagesTableId && material.generation.w == samplersTableId
        && imagesTableId != 0u && samplersTableId != 0u && imagesTableId != samplersTableId
        && all(isfinite(material.baseColorAlpha)) && all(material.baseColorAlpha >= 0.0)
        && material.baseColorAlpha.a <= 1.0
        && all(isfinite(material.factors)) && all(material.factors >= 0.0)
        && all(material.factors.xyz <= 1.0)
        && all(isfinite(material.coverageLeaf)) && all(material.coverageLeaf >= 0.0)
        && material.coverageLeaf.x <= 1.0 && material.coverageLeaf.z <= 1.0
        && (!leaf || (flags & 3u) == 3u);
    if (!valid) {
        arpBindlessReportFault();
        material = (ArpBindlessRecord_MaterialRecords)0;
    }
    return valid;
}

// All three pass families consume this exact coverage predicate and base
// sample. Their geometry entry must also share the actual deformed position.
bool arpSamplePbrCoverage(ArpBindlessRecord_MaterialRecords material,
    ArpMaterialGradients gradients, out float factoredAlpha, out bool covered)
{
    factoredAlpha = 0.0;
    covered = false;
    bool fault;
    float4 base = MaterialImages_sampleGrad(material.textures.x, material.routing.y,
        gradients.uv, gradients.dx, gradients.dy, fault);
    if (fault) return false;
    factoredAlpha = base.a * material.baseColorAlpha.a;
    covered = (material.routing.z & 1u) == 0u || factoredAlpha >= material.coverageLeaf.x;
    return true;
}

struct ArpBindlessPbrMaterialSample
{
    ArpPbrMaterialSurface surface;
    float3 normal;
    bool receivesShadow;
};

bool arpSamplePbrMaterial(ArpBindlessRecord_MaterialRecords material,
    ArpMaterialGradients gradients, float3 geometryNormal, float4 tangent,
    bool frontFacing, out ArpBindlessPbrMaterialSample output)
{
    output = (ArpBindlessPbrMaterialSample)0;
    bool fault;
    uint samplerIndex = material.routing.y;
    float4 base = MaterialImages_sampleGrad(material.textures.x, samplerIndex,
        gradients.uv, gradients.dx, gradients.dy, fault);
    bool valid = !fault;
    float4 normal = MaterialImages_sampleGrad(material.textures.y, samplerIndex,
        gradients.uv, gradients.dx, gradients.dy, fault);
    valid = valid && !fault;
    float4 mr = MaterialImages_sampleGrad(material.textures.z, samplerIndex,
        gradients.uv, gradients.dx, gradients.dy, fault);
    valid = valid && !fault;
    float4 emissive = MaterialImages_sampleGrad(material.textures.w, samplerIndex,
        gradients.uv, gradients.dx, gradients.dy, fault);
    valid = valid && !fault;
    float4 occlusion = MaterialImages_sampleGrad(material.routing.x, samplerIndex,
        gradients.uv, gradients.dx, gradients.dy, fault);
    valid = valid && !fault;
    if (!valid) return false;
    uint flags = material.routing.z;
    bool leaf = (flags & 8u) != 0u;
    float singleSided = (flags & 2u) == 0u ? 1.0 : 0.0;
    float alphaMode = leaf ? 2.0 + material.coverageLeaf.z : ((flags & 1u) != 0u ? 1.0 : 0.0);
    output.surface = arpEvaluatePbrMaterial(material.baseColorAlpha,
        float4(material.factors.xy, material.coverageLeaf.x, alphaMode),
        float4(material.factors.zw, leaf ? -material.coverageLeaf.y : singleSided, 0.0),
        float4(-material.coverageLeaf.w, 0.0, 0.0, 0.0), base, mr, emissive, occlusion);
    output.normal = arpPbrMaterialNormal(geometryNormal, tangent, frontFacing, singleSided, normal.xyz);
    output.receivesShadow = (flags & 4u) != 0u;
    return true;
}
