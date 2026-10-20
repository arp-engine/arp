#pragma once

#include "core/cpu_allocator.h"
#include "rhi/frontend/frame_resources.h"

#include <string>

namespace arp::rhi {

struct MemoryLifetimeValidationDesc {
    FrameResourceRecyclerStats frameResources;
    bool resourceDestructionOrderValidated = false;
    bool descriptorLifetimeValidated = false;
    bool descriptorArenaResetInvalidatedOldAllocations = false;
    bool swapchainResizeRecreateValidated = false;
    bool surfaceResourcesReleasedOnResize = false;
    bool transientAliasingValidated = false;
    bool transientAliasesNonOverlapping = false;
    bool uploadStagingValidated = false;
    bool readbackStagingValidated = false;
    bool stagingResourcesBackendOwned = false;
    bool uploadStagingBounded = false;
    bool uploadStagingNativeHandlesHidden = true;
    bool noHotPathUnboundedAllocation = false;
    bool noBlockingWait = false;
    bool nativeHandleExposedToUpperLayer = false;
};

struct MemoryLifetimeValidationReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool resourceDestructionOrderValid = false;
    bool descriptorLifetimeValid = false;
    bool swapchainResizeRecreateValid = false;
    bool transientAliasingValid = false;
    bool uploadReadbackStagingValid = false;
    bool hotPathAllocationBounded = false;
    bool nativeHandleExposedToUpperLayer = false;
};

class MemoryLifetimeValidator {
public:
    MemoryLifetimeValidationReport evaluate(const MemoryLifetimeValidationDesc& desc) const;
};

MemoryLifetimeValidationReport evaluateMemoryLifetimeValidation(
    const MemoryLifetimeValidationDesc& desc);

} // namespace arp::rhi
