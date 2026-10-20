// DdgiSceneMaterials/2 (also accepts /1 sRGB). Read-only uint words avoid descriptor arrays and keep
// the same material ABI on SPIR-V, MSL and WGSL.
bool ddgiUsesSceneMaterial(uint material) { return (material & 0xc0000000u) == 0x40000000u; }

int ddgiMaterialAddress(int coord, uint extent, uint wrap)
{
    int n = int(extent);
    if (wrap == 33071u) return clamp(coord, 0, n - 1);
    if (wrap == 33648u) {
        int p = ((coord % (2*n)) + 2*n) % (2*n);
        return p < n ? p : 2*n - 1 - p;
    }
    return ((coord % n) + n) % n;
}

float4 ddgiMaterialTexel(uint record, int2 coord)
{
    uint width = DdgiSceneMaterials[record+5u];
    uint height = DdgiSceneMaterials[record+6u];
    int x = ddgiMaterialAddress(coord.x, width, DdgiSceneMaterials[record+9u]);
    int y = ddgiMaterialAddress(coord.y, height, DdgiSceneMaterials[record+10u]);
    uint index = DdgiSceneMaterials[1u] + DdgiSceneMaterials[record+4u] + uint(y)*width + uint(x);
    uint packed = DdgiSceneMaterials[index];
    float4 rgba = float4(packed & 255u, (packed >> 8u) & 255u,
        (packed >> 16u) & 255u, (packed >> 24u) & 255u) / 255.0;
    float3 srgb = rgba.rgb;
    if (DdgiSceneMaterials[3u] == 0u) rgba.rgb = lerp(pow((srgb+0.055)/1.055, 2.4.xxx), srgb/12.92,
        srgb <= 0.04045.xxx);
    return rgba;
}

float4 ddgiSceneMaterialColor(uint material, float2 uv)
{
    uint slot = material & 0x3fffffffu;
    if (slot >= DdgiSceneMaterials[0u]) return 0.0.xxxx;
    uint record = 4u + slot * 12u;
    float4 factor = asfloat(uint4(DdgiSceneMaterials[record], DdgiSceneMaterials[record+1u],
        DdgiSceneMaterials[record+2u], DdgiSceneMaterials[record+3u]));
    uint width = DdgiSceneMaterials[record+5u];
    uint height = DdgiSceneMaterials[record+6u];
    if (width == 0u || height == 0u) return factor;
    float2 p = uv * float2(width,height) - 0.5;
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    float4 color = lerp(lerp(ddgiMaterialTexel(record,cell),ddgiMaterialTexel(record,cell+int2(1,0)),f.x),
        lerp(ddgiMaterialTexel(record,cell+int2(0,1)),ddgiMaterialTexel(record,cell+int2(1,1)),f.x),f.y);
    return color*factor;
}

bool ddgiSceneMaterialVisible(uint material, float alpha)
{
    uint record = 4u + (material & 0x3fffffffu) * 12u;
    uint mode = DdgiSceneMaterials[record+7u];
    // Blended surfaces are excluded from the opaque GI occluder domain.
    return mode == 0u || (mode == 1u && alpha >= asfloat(DdgiSceneMaterials[record+8u]));
}
