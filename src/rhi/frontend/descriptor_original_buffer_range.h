#pragma once
#include "core/api.h"
#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/descriptor_lowering.h"

namespace arp::rhi {
class GpuBuffer;
template <class Resource> class OriginalCommandResourceLease;
using OriginalCommandBufferLease = OriginalCommandResourceLease<GpuBuffer>;
class GpuDevice;
struct DescriptorBindingWrite;

// Explicit cold publication of an authored finite buffer view. No dynamic
// offset, shader footprint, command or completion authority is issued here.
struct OriginalBufferRangeDescriptorRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxNativeWrites = 0u;
};

class ARP_API OriginalBufferRangeDescriptorContract final {
public:
    struct Decision {
        bool validatorOnly = false, publicationAvailable = false;
        bool shaderExecutionAvailable = false;
        const char* diagnostic = "RHI-BUFFER-RANGE-DESCRIPTOR-BACKEND";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::DescriptorLowering::OriginalBufferRangeDescriptor_lowering<Decision>(
            backend);
    }

    static constexpr const char* validateIntent(
        const OriginalBufferRangeDescriptorRequest& r) noexcept
    {
        if (r.contractVersion != 1u)
            return "RHI-BUFFER-RANGE-DESCRIPTOR-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-BUFFER-RANGE-DESCRIPTOR-OPT-IN";
        return r.maxNativeWrites == 1u ? nullptr : "RHI-BUFFER-RANGE-DESCRIPTOR-BUDGET";
    }

    static const char* validateSource(GpuDevice&,
        const DescriptorBindingWrite&,
        const std::shared_ptr<const OriginalCommandBufferLease>&,
        std::uint64_t minimumBytes) noexcept;
};
} // namespace arp::rhi
