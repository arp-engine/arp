// A 3D lattice packed as x = blue + red * edge, y = green.
// Clamp reproduces the OCIO LUT sampler's edge addressing.
float3 loadLutAtlas(Texture2D<float4> atlas, float3 normalizedBgr, int edge)
{
    int3 lattice = clamp(int3(floor(normalizedBgr * float(edge))),
        int3(0), int3(edge - 1));
    return atlas.Load(int3(lattice.x + lattice.z * edge, lattice.y, 0)).rgb;
}
