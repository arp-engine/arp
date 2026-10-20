#pragma once

#include <cstdint>

namespace arp::rhi_metal {

struct MetalRhiNativeFeatureProbe {
    bool argumentBuffers = false;
    bool argumentBuffersTier2 = false;
    bool placementAliasing = false;
    bool secondaryCommandBuffers = false;
    bool indirectCommandBuffers = false;
    bool timelineSynchronization = false;
    bool nativeSync = false;
    bool memorylessTransientTextures = false;
    bool textureCompressionBc = false;
    bool r32FloatSampledUpload = false;
    bool timestampQueries = false;
    bool pipelineStatisticsQueries = false;
    bool framebufferFetch = false;
    bool framebufferFetchDepth = false;
    bool rg16FloatStorageTexture = false;
    bool rgba16FloatStorageTexture = false;
    bool sceneGeometryRayQuery = false;
    bool sampledDepthTexture = false;
    std::uint32_t maxComputeWorkgroupInvocations = 0u;
    std::uint32_t maxComputeWorkgroupSizeX = 0u;
    std::uint32_t maxComputeWorkgroupSizeY = 0u;
    std::uint32_t maxComputeWorkgroupMemoryBytes = 0u;
    std::uint32_t maxStorageBufferBindingsPerStage = 0u;
    std::uint64_t maxStorageBufferBindingSizeBytes = 0u;
    // Conservative Metal3 family guarantees, published only after supportsFamily.
    std::uint32_t maxTexture2DDimension = 0u, maxTexture3DDimension = 0u,
                  maxTextureArrayLayers = 0u;
    // MetalComputeImages/1: core specification ceilings plus actual native
    // R32Float read/write tier. Non-Apple/unavailable devices keep zero/false.
    std::uint32_t maxTextureBindingsPerStage = 0u;
    std::uint32_t maxSamplerBindingsPerStage = 0u;
    bool r32FloatReadWriteTexture = false;
};

/// Probes the exact native substrates used by the Metal lowering contract.
/// Non-Apple builds return an all-false result.
MetalRhiNativeFeatureProbe probeMetalRhiNativeFeatures();

/// Returns true only when the native statistics counter set and result
/// structure are available. Non-Apple stubs return false.
bool metalRhiNativePipelineStatisticsQueriesAvailable();

} // namespace arp::rhi_metal
