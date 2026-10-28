#pragma once

#include "rhi/gpu_resource_types.h"
#include "rhi/semantics/resource_lowering.h"

namespace arp::rhi {
// Cold allocation requirement, not a command/content/completion authority.
// Existing CpuToGpu mapped-write + flush, seals and journals remain mandatory.
// Allocation bytes use the original allocator ledger and owners, with no retry
// in a different memory class. Failure can be retried only after a new explicit
// caller request (or changed capability/budget); no per-frame work is introduced.
class HostVisibleDeviceLocalBufferContract final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-HOST-DEVICE-BUFFER-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, bool capability) noexcept
    {
        return semantics::ResourceLowering::HostVisibleDeviceLocalBuffer_lowering<Decision>(
            backend, capability);
    }

    static const char* validate(const BufferDesc& desc, Backend backend, bool capability) noexcept
    {
        if (!desc.hostVisibleDeviceLocal.requested())
            return nullptr;
        if (desc.hostVisibleDeviceLocal.contractVersion != 1u)
            return "RHI-HOST-DEVICE-BUFFER-VERSION";
        if (!desc.hostVisibleDeviceLocal.prototypeOptIn)
            return "RHI-HOST-DEVICE-BUFFER-OPT-IN";
        if (desc.memory != MemoryUsage::CpuToGpu || !desc.sizeBytes)
            return "RHI-HOST-DEVICE-BUFFER-MEMORY-INTENT";
        const auto decision = lowering(backend, capability);
        return decision.validatorOnly ? nullptr : decision.diagnostic;
    }
};
} // namespace arp::rhi
