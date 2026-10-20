#ifndef ARP_RENDERER_DDGI_VISIBILITY_SLANG
#define ARP_RENDERER_DDGI_VISIBILITY_SLANG

// Suppress low-confidence Chebyshev visibility without introducing a step at
// the threshold. The previous v^3 branch jumped from about 0.008 to 0.2 and
// could amplify probe interpolation discontinuities.
float ddgiReduceVisibilityLeak(float visibility)
{
    const float threshold = 0.2;
    if (visibility >= threshold) {
        return visibility;
    }
    const float normalized = visibility / threshold;
    return visibility * normalized * normalized;
}

#endif
