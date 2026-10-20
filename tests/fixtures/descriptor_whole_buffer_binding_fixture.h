#pragma once

#include "rhi/frontend/descriptor_whole_buffer_binding.h"

#include <array>
#include <cassert>
#include <limits>
#include <string_view>

namespace arp::test {
class DescriptorWholeBufferBindingFixture final {
public:
    static void verifyContract()
    {
        using Policy = rhi::semantics::DescriptorWholeBufferBinding;
        using Failure = Policy::Failure;
        using Type = rhi::DescriptorType;
        Policy::Limits limits{1u, 256u, 128u, 1024u, 2048u};
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto type :
            {Type::UniformBuffer, Type::StorageBuffer, Type::ReadOnlyStorageBuffer}) {
            const auto maximum = type == Type::UniformBuffer ? limits.maxUniformBindingBytes
                                                             : limits.maxStorageBindingBytes;
            const auto alignment = type == Type::UniformBuffer ? limits.uniformOffsetAlignment
                                                               : limits.storageOffsetAlignment;
            assert(Policy::validate(type, maximum, 0u, limits) == Failure::None);
            assert(Policy::validate(type, maximum + 1u, 0u, limits) == Failure::BindingSize);
            assert(Policy::validate(type, 0u, 0u, limits) == Failure::BufferRange);
            for (const auto offset : {1u, alignment, UINT32_MAX}) {
                assert(Policy::validate(type, maximum, offset, limits)
                    == Failure::DynamicRangeUnproven);
                // Regression: a small remaining tail cannot legalize the full binding.
                assert(Policy::validate(type, maximum + alignment, offset, limits)
                    == Failure::DynamicRangeUnproven);
            }
            auto unavailable = limits;
            unavailable.uniformOffsetAlignment = unavailable.storageOffsetAlignment = 0u;
            assert(Policy::validate(type, 1u, 0u, unavailable) == Failure::LimitsUnavailable);
            unavailable = limits;
            unavailable.maxUniformBindingBytes = unavailable.maxStorageBindingBytes = 0u;
            assert(Policy::validate(type, 1u, 0u, unavailable) == Failure::LimitsUnavailable);
            for (const auto version : {0u, 2u, UINT32_MAX}) {
                auto wrong = limits;
                wrong.contractVersion = version;
                assert(Policy::validate(type, 1u, 0u, wrong) == Failure::Version);
            }
        }
        for (const auto type : {Type::SampledTexture,
                 Type::StorageTexture,
                 Type::Sampler,
                 Type::InputAttachment,
                 static_cast<Type>(255)})
            assert(Policy::validate(type, 64u, 0u, limits) == Failure::BufferType);
        limits.maxStorageBindingBytes = std::numeric_limits<std::uint64_t>::max();
        assert(Policy::validate(Type::StorageBuffer, limits.maxStorageBindingBytes, 0u, limits)
            == Failure::None);
        assert(
            Policy::validate(Type::StorageBuffer, limits.maxStorageBindingBytes, UINT32_MAX, limits)
            == Failure::DynamicRangeUnproven);
        std::array<std::uint32_t, 3> offsets{};
        assert(Policy::preservesRanges(nullptr, 0u));
        assert(!Policy::preservesRanges(nullptr, 1u));
        assert(Policy::preservesRanges(offsets.data(), offsets.size()));
        offsets[1] = 256u;
        assert(!Policy::preservesRanges(offsets.data(), offsets.size()));
        assert(std::string_view(Policy::diagnostic(Failure::DynamicRangeUnproven))
            == "RHI-DESCRIPTOR-WHOLE-BUFFER-DYNAMIC-RANGE-UNPROVEN");
        assert(!Policy::diagnostic(Failure::None));
        assert(core::defaultCpuAllocator().stats().allocationCount == before);
    }
};
} // namespace arp::test
