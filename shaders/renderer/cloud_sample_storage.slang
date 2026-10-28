#ifndef ARP_CLOUD_SAMPLE_STORAGE
#define ARP_CLOUD_SAMPLE_STORAGE
#include "renderer/cloud_common.slang"

// CloudSample/2 stores six unquantized float32 lanes. Scalar members avoid
// float3/float4 alignment differences between SPIR-V, MSL and WGSL.
#if defined(ARP_CLOUD_COMPACT_HISTORY)
struct CloudGpuCompactSample {
    float red;
    float green;
    float blue;
    float transmittance;
    float distance;
    float opaqueDistance;
};
#define CloudStoredSample CloudGpuCompactSample
CloudGpuSample cloudLoadStored(CloudStoredSample value)
{
    CloudGpuSample result;
    result.radianceTransmittance = float4(value.red, value.green, value.blue, value.transmittance);
    result.metadata = float4(value.distance, value.opaqueDistance, 0, 0);
    return result;
}
CloudStoredSample cloudStoreSample(CloudGpuSample value)
{
    CloudStoredSample result;
    result.red = value.radianceTransmittance.x;
    result.green = value.radianceTransmittance.y;
    result.blue = value.radianceTransmittance.z;
    result.transmittance = value.radianceTransmittance.w;
    result.distance = value.metadata.x;
    result.opaqueDistance = value.metadata.y;
    return result;
}
#else
#define CloudStoredSample CloudGpuSample
CloudGpuSample cloudLoadStored(CloudStoredSample value) { return value; }
CloudStoredSample cloudStoreSample(CloudGpuSample value) { return value; }
#endif
#endif
