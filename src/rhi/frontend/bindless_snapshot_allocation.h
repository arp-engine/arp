#pragma once
#include "rhi/gpu_bindless.h"

#include <array>

namespace arp::rhi {
// SnapshotAllocation/1 is Compute-only; /2 also admits Vertex/Fragment snapshots.
// The immutable package fixes logical
// bytes; a cold artifact projection may enlarge only its resident envelope.
// Queries and creation share these exact buffer descriptors. No native handle,
// resource/content identity, shader layout or execution capability is changed.
class BindlessSnapshotAllocationContract final {
public:
    static std::array<BufferDesc, 3u> buffers(const BindlessShaderArtifact& a)
    {
        return {{{"BindlessSnapshotMetadata",
                     std::uint64_t(a.snapshotMetadata.wordCount) * sizeof(std::uint32_t),
                     ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
                     MemoryUsage::CpuToGpu,
                     ResourceState::ShaderRead},
            {"BindlessFaultLatch",
                sizeof(std::uint32_t),
                ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
                MemoryUsage::GpuOnly,
                ResourceState::ShaderWrite},
            {"BindlessFaultReadback",
                sizeof(std::uint32_t),
                ResourceUsage::CopyDst,
                MemoryUsage::GpuToCpu,
                ResourceState::CopyDst}}};
    }

    static constexpr const char* validate(
        const BindlessShaderArtifact& a, std::uint32_t version) noexcept
    {
        if (version != 1u && version != 2u)
            return "RHI-BINDLESS-SNAPSHOT-ALLOCATION-VERSION";
        const auto stages = a.snapshotMetadata.stages;
        const auto graphics = ShaderStage::Vertex | ShaderStage::Fragment;
        const bool validStages = stages == ShaderStage::Compute
            || (version == 2u && stages != ShaderStage::None && (stages & graphics) == stages);
        if (a.snapshotMetadata.contractVersion != 1u || !a.snapshotMetadata.wordCount
            || !validStages || a.faultLatch.contractVersion != 1u || a.faultLatch.stages != stages
            || a.snapshotBytes
                < (std::uint64_t(a.snapshotMetadata.wordCount) + 2u) * sizeof(std::uint32_t))
            return "RHI-BINDLESS-SNAPSHOT-ALLOCATION-SCHEMA";
        return nullptr;
    }
};
} // namespace arp::rhi
