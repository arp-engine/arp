#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"

namespace arp::rhi {

// HeapPlacementAdmission/1. Allocation-free L2 input checks. Logical
// requirements are validator facts and never native allocation metadata.
class HeapPlacementContract final {
public:
    // The mapping kind does not authorize execution. Native runtime,
    // capability, exact requirements and ownership still require admission.
    enum class Disposition { ValidatorOnly, Native, DiagnosticFailure };

    static constexpr Disposition lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return Disposition::ValidatorOnly;
        case Backend::Vulkan:
        case Backend::Metal:
            return Disposition::Native;
        case Backend::WebGPU:
        case Backend::WebGL:
            return Disposition::DiagnosticFailure;
        }
        return Disposition::DiagnosticFailure;
    }

    static constexpr const char* heap(
        std::uint64_t size, std::uint64_t alignment, MemoryUsage usage) noexcept
    {
        if (!size)
            return "RHI-HEAP-SIZE-ZERO";
        if (!core::isPowerOfTwoUnsigned(alignment))
            return "RHI-HEAP-ALIGNMENT-INVALID";
        if (usage != MemoryUsage::GpuOnly && usage != MemoryUsage::Transient)
            return "RHI-HEAP-MEMORY-INTENT";
        return nullptr;
    }

    static const char* texture(const TextureDesc& desc, std::uint64_t& logicalBytes) noexcept
    {
        logicalBytes = 0u;
        const auto allowed = TextureUsage::Sampled | TextureUsage::Storage
            | TextureUsage::RenderAttachment | TextureUsage::CopySrc | TextureUsage::CopyDst;
        if ((desc.memory != MemoryUsage::GpuOnly && desc.memory != MemoryUsage::Transient)
            || desc.initialState != ResourceState::Undefined || desc.usage == TextureUsage::None
            || (static_cast<std::uint32_t>(desc.usage) & ~static_cast<std::uint32_t>(allowed)))
            return "RHI-HEAP-TEXTURE-INTENT";
        if (!core::isPowerOfTwoUnsigned(desc.sampleCount) || desc.sampleCount > 64u
            || (desc.extent.depth > 1u && (desc.arrayLayerCount != 1u || desc.cubeCompatible))
            || (desc.cubeCompatible
                && (desc.extent.depth != 1u || desc.extent.width != desc.extent.height
                    || desc.arrayLayerCount % 6u))
            || (desc.sampleCount > 1u
                && (desc.extent.depth != 1u || desc.mipLevelCount != 1u || desc.cubeCompatible
                    || isBlockCompressed(desc.format))))
            return "RHI-HEAP-TEXTURE-SHAPE";
        std::uint64_t bytes = 0u;
        if (const auto* id = semantics::TextureUploadDestinationValidation::footprint(desc, bytes))
            return id;
        if (!core::canMultiplyUnsigned(bytes, std::uint64_t{desc.sampleCount}))
            return "RHI-HEAP-TEXTURE-BYTE-OVERFLOW";
        logicalBytes = bytes * desc.sampleCount;
        return nullptr;
    }

    static constexpr const char* placement(std::uint64_t heapSize,
        std::uint64_t heapAlignment,
        std::uint64_t offset,
        const TexturePlacementRequirements& requirements) noexcept
    {
        if (!heapSize || !core::isPowerOfTwoUnsigned(heapAlignment) || !requirements.sizeBytes
            || !core::isPowerOfTwoUnsigned(requirements.alignmentBytes))
            return "RHI-HEAP-REQUIREMENTS-INVALID";
        if (offset % heapAlignment || offset % requirements.alignmentBytes)
            return "RHI-HEAP-OFFSET-ALIGNMENT";
        if (!core::containsUnsignedRange(
                std::uint64_t{0}, heapSize, offset, requirements.sizeBytes))
            return "RHI-HEAP-RANGE";
        return nullptr;
    }

    static TexturePlacementRequirements validatorRequirements(const TextureDesc& desc) noexcept
    {
        std::uint64_t bytes = 0u;
        if (texture(desc, bytes))
            return {};
        // The existing L2 estimate defines validator alignment only. Its
        // unchecked size is not consumed: the checked footprint above is.
        return {bytes, estimateTexturePlacementRequirements(desc).alignmentBytes};
    }
};

} // namespace arp::rhi
