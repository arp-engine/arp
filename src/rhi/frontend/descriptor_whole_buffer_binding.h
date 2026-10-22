#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/gpu_descriptors.h"

namespace arp::rhi::semantics {

// DescriptorBindingWrite/1 binds offset zero and the entire resource. Access
// coverage and dynamic arguments cannot author a smaller descriptor range.
// Scalar facts are validation inputs, never an original-device receipt.
class DescriptorWholeBufferBinding final {
public:
    struct Limits {
        std::uint32_t contractVersion = 0u;
        std::uint32_t uniformOffsetAlignment = 0u;
        std::uint32_t storageOffsetAlignment = 0u;
        std::uint64_t maxUniformBindingBytes = 0u;
        std::uint64_t maxStorageBindingBytes = 0u;
    };
    enum class Failure {
        None,
        Version,
        BufferType,
        DynamicRangeUnproven,
        LimitsUnavailable,
        BufferRange,
        BindingSize,
    };

    static constexpr bool preservesRange(std::uint32_t dynamicOffset) noexcept
    {
        return dynamicOffset == 0u;
    }

    // Count/pointer/order remain the responsibility of the layout contract.
    static bool preservesRanges(const std::uint32_t* offsets, std::uint32_t count) noexcept
    {
        if (count && !offsets)
            return false;
        for (std::uint32_t i = 0u; i < count; ++i)
            if (!preservesRange(offsets[i]))
                return false;
        return true;
    }

    static constexpr Failure validate(DescriptorType type,
        std::uint64_t bufferBytes,
        std::uint32_t dynamicOffset,
        const Limits& limits) noexcept
    {
        if (limits.contractVersion != 1u)
            return Failure::Version;
        const bool uniform = type == DescriptorType::UniformBuffer;
        if (!uniform && type != DescriptorType::StorageBuffer
            && type != DescriptorType::ReadOnlyStorageBuffer)
            return Failure::BufferType;
        if (!preservesRange(dynamicOffset))
            return Failure::DynamicRangeUnproven;
        const auto alignment =
            uniform ? limits.uniformOffsetAlignment : limits.storageOffsetAlignment;
        const auto maximum =
            uniform ? limits.maxUniformBindingBytes : limits.maxStorageBindingBytes;
        if (!alignment || !maximum)
            return Failure::LimitsUnavailable;
        if (!core::containsUnsignedRange<std::uint64_t>(
                0u, bufferBytes, dynamicOffset, bufferBytes))
            return Failure::BufferRange;
        return bufferBytes <= maximum ? Failure::None : Failure::BindingSize;
    }

    static constexpr const char* diagnostic(Failure failure) noexcept
    {
        switch (failure) {
        case Failure::None:
            return nullptr;
        case Failure::Version:
            return "RHI-DESCRIPTOR-WHOLE-BUFFER-VERSION";
        case Failure::BufferType:
            return "RHI-DESCRIPTOR-WHOLE-BUFFER-TYPE";
        case Failure::DynamicRangeUnproven:
            return "RHI-DESCRIPTOR-WHOLE-BUFFER-DYNAMIC-RANGE-UNPROVEN";
        case Failure::LimitsUnavailable:
            return "RHI-DESCRIPTOR-WHOLE-BUFFER-LIMITS-UNAVAILABLE";
        case Failure::BufferRange:
            return "RHI-DESCRIPTOR-WHOLE-BUFFER-RANGE";
        case Failure::BindingSize:
            return "RHI-DESCRIPTOR-WHOLE-BUFFER-BINDING-SIZE";
        }
        return "RHI-DESCRIPTOR-WHOLE-BUFFER-FAILURE";
    }
};
} // namespace arp::rhi::semantics
