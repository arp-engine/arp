#pragma once
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/resource_texture_validation.h"

namespace arp::rhi {
// ImmutableOriginalTextureUploadStaging /1. Cold initialization is distinct
// from texture upload or completion. The closed backend factory must actually
// write and flush every packed byte before sealing/adopting its original buffer.
// Vulkan queue submission performs the host-to-device domain operation for
// prior flushed host writes; no invented HostWrite GPU state is introduced.
struct OriginalTextureUploadStagingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSources = 0u, maxMipMappings = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxLogicalStagingBytes = 0u, maxNativeStagingBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u, maxCpuResourceBytes = 0u, maxCpuLeaseBytes = 0u;
};

class OriginalTextureUploadStagingContract final {
public:
    // Source, two native payload owners and runtime, plus existing original
    // packing and adopted buffer closure. Conservative independently authored
    // bounds never replace canonical inventory identity or original quotas.
    static constexpr std::uint32_t retainedOwnerBound = 4u
        + OriginalCommandTextureUploadPackingSource::retainedOwnerCount
        + OriginalCommandBufferLease::retainedOwnerCount;

    static constexpr const char* validateIntent(
        const OriginalTextureUploadStagingBudget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u)
            return "RHI-ORIGINAL-TEXTURE-STAGING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-TEXTURE-STAGING-OPT-IN";
        if (b.maxSources != 1u || !b.maxMipMappings || b.maxRetainedOwners < retainedOwnerBound
            || !b.maxLogicalStagingBytes || !b.maxNativeStagingBytes || !b.maxCpuMetadataBytes
            || !b.maxCpuResourceBytes || !b.maxCpuLeaseBytes)
            return "RHI-ORIGINAL-TEXTURE-STAGING-BUDGET";
        return nullptr;
    }

    static const char* validateBuffer(const GpuBuffer& buffer,
        std::uint64_t packedBytes,
        const OriginalTextureUploadStagingBudget& b) noexcept
    {
        const auto& d = buffer.desc();
        if (d.memory != MemoryUsage::CpuToGpu || d.usage != ResourceUsage::CopySrc
            || d.initialState != ResourceState::CopySrc || d.sizeBytes != packedBytes
            || !buffer.hostWritesSealed() || !buffer.gpuWritesSealed())
            return "RHI-ORIGINAL-TEXTURE-STAGING-BUFFER-PROFILE";
        if (!packedBytes || packedBytes > b.maxLogicalStagingBytes
            || !buffer.allocation().estimatedSizeBytes
            || buffer.allocation().estimatedSizeBytes > b.maxNativeStagingBytes)
            return "RHI-ORIGINAL-TEXTURE-STAGING-BYTE-CAPACITY";
        return nullptr;
    }

    static const char* validateDestination(
        const TextureDesc& d, std::uint32_t version = 1u) noexcept
    {
        // Whole RGBA8 2D/array uploads also map the six faces of one cube.
        // Other formats/dimensions require their own tested native copy mapping.
        if (version != 1u && version != 2u)
            return "RHI-ORIGINAL-TEXTURE-STAGING-VERSION";
        if ((d.format != TextureFormat::Rgba8Unorm
                && (version != 2u || d.format != TextureFormat::Rgba16Float))
            || d.extent.depth != 1u
            || (d.cubeCompatible
                && (d.arrayLayerCount != 6u || !d.extent.width
                    || d.extent.width != d.extent.height))
            || d.sampleCount != 1u || d.initialState != ResourceState::Undefined)
            return "RHI-ORIGINAL-TEXTURE-STAGING-DESTINATION-PROFILE";
        return nullptr;
    }

    static const char* validateExecutionBudget(const OriginalCommandAdmissionBudget& b,
        std::uint64_t mips,
        std::uint64_t nativeBytes) noexcept
    {
        // One command payload; one merged dependency call, one copy per mip,
        // and command end. Queue/fence capacity uses the independent /7 seal.
        if (!b.maxNativePayloadObjects || mips > UINT64_MAX - 2u
            || b.maxTypedNativeCommandCalls < mips + 2u || nativeBytes > b.maxNativeStagingBytes)
            return "RHI-ORIGINAL-TEXTURE-UPLOAD-NATIVE-CAPACITY";
        return nullptr;
    }
};
} // namespace arp::rhi
