#ifndef ARP_MEDIUM_INTEGRATION_SLANG
#define ARP_MEDIUM_INTEGRATION_SLANG

float arpVolumeSegmentLength(float extinction, float distance)
{
    float opticalDepth = extinction * distance;
    return abs(opticalDepth) < 0.001
        ? distance * (1.0 - opticalDepth * 0.5 + opticalDepth * opticalDepth / 6.0)
        : (1.0 - exp(-opticalDepth)) / extinction;
}

float arpVolumePhase(float cosine, float g)
{
    float d = 1.0 + g * g - 2.0 * g * cosine;
    return (1.0 - g * g) / (12.566370614359172 * d * sqrt(d));
}

#endif
