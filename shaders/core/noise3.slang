#ifndef ARP_NOISE3_SLANG
#define ARP_NOISE3_SLANG
// Integer lattice hash; wrapping uint arithmetic is part of Noise3/1.
float arpHash3(int3 cell, uint seed)
{
    uint h = seed;
    h = (h ^ (uint(cell.x) * 374761393u)) * 668265263u;
    h = (h ^ (uint(cell.y) * 1274126177u)) * 461845907u;
    h = (h ^ (uint(cell.z) * 2246822519u)) * 3266489917u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return float(h >> 8) / 16777216.0;
}
float arpNoise3(float3 p, uint seed)
{
    int3 cell = int3(floor(p));
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(arpHash3(cell, seed), arpHash3(cell + int3(1,0,0), seed), f.x);
    float b = lerp(arpHash3(cell + int3(0,1,0), seed), arpHash3(cell + int3(1,1,0), seed), f.x);
    float c = lerp(arpHash3(cell + int3(0,0,1), seed), arpHash3(cell + int3(1,0,1), seed), f.x);
    float d = lerp(arpHash3(cell + int3(0,1,1), seed), arpHash3(cell + int3(1,1,1), seed), f.x);
    return lerp(lerp(a,b,f.y), lerp(c,d,f.y), f.z);
}
#endif
