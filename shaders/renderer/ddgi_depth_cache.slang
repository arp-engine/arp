#ifndef ARP_DDGI_DEPTH_CACHE_SLANG
#define ARP_DDGI_DEPTH_CACHE_SLANG

// DdgiCacheDepthPrecision/2: binary16 has 10 fraction bits, minimum normal
// exponent -14. Native narrowing/storage may round to nearest or toward zero.
// Discount that representable interval, not a tuned world-depth bias.
float ddgiEncodeCacheDepth(float depth)
{
    return f16tof32(f32tof16(depth));
}

float ddgiCacheDepthDistance(float storedDepth, float sceneDepth)
{
    float exponent = max(floor(log2(max(abs(storedDepth), exp2(-14.0)))), -14.0);
    float ulp = exp2(exponent - 10.0);
    // For nonnegative [0,1] depth, nearest rounding can originate half an ULP
    // below/above the stored value; truncation can originate up to one ULP
    // above it. An f16 conversion round trip does not guarantee nearest mode
    // on every target. Keep the asymmetric union of both source intervals.
    float lower = storedDepth - 0.5 * ulp;
    float upper = storedDepth + ulp;
    return max(0.0, max(lower - sceneDepth, sceneDepth - upper));
}

#endif
