#pragma once
#include "rhi/frontend/visibility/gpu_visibility_layout.h"
#include "rhi/gpu_pipeline.h"

namespace arp::rhi::visibility {
// Value inspection of Consumer/2's original read-only views. No owner, content
// certificate or execution authority can be created by this projection.
class GpuVisibilityConsumerAliasContract final {
public:
    static constexpr std::uint32_t version = 2u, set = 2u;
    static constexpr std::array<std::size_t, 4u> roles{0u, 4u, 6u, 8u};
    static constexpr std::array<std::uint64_t, 4u> strides{sizeof(GpuVisibilityDrawGroup),
        sizeof(Instance64),
        sizeof(IndirectInstanceBase),
        sizeof(GpuVisibilityParameters)};

    static const char* inspect(std::size_t binding,
        std::size_t source,
        std::size_t expectedSource,
        DescriptorType type,
        ShaderStage stages,
        ResourceAccess access,
        std::uint64_t offset,
        std::uint64_t bytes,
        std::uint64_t expectedBytes,
        const ShaderBufferMemoryRequirement& memory,
        bool emulated) noexcept
    {
        if (binding >= roles.size() || source == SIZE_MAX || source != expectedSource)
            return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-SOURCE";
        if (type
                != (binding == 3u ? DescriptorType::UniformBuffer
                                  : DescriptorType::ReadOnlyStorageBuffer)
            || stages != ShaderStage::Vertex || access != ResourceAccess::ShaderRead)
            return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-ACCESS";
        if (offset || !bytes || bytes != expectedBytes || bytes % strides[binding])
            return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-RANGE";
        if (memory.evidence == ShaderBufferMemoryRequirement::Evidence::None
            || memory.hasRuntimeArray != (binding != 3u) || memory.minimumBytes > bytes
            || memory.sourceMinimumBytes != strides[binding])
            return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-LAYOUT";
        if (binding == 3u) {
            if (bytes != sizeof(GpuVisibilityParameters)
                || memory.sourceOccupiedBytes != sizeof(GpuVisibilityParameters))
                return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-LAYOUT";
        } else if (memory.sourceRuntimeArrayOffsetBytes
            || memory.sourceRuntimeArrayStrideBytes != strides[binding]
            || memory.sourceElementCapacity != bytes / strides[binding]
            || (emulated
                && (memory.targetRuntimeArrayOffsetBytes
                    || memory.targetRuntimeArrayStrideBytes != strides[binding])))
            return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-LAYOUT";
        return nullptr;
    }
};
} // namespace arp::rhi::visibility
