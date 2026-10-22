#include "rhi/frontend/memory_lifetime_validation.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

namespace arp::rhi {

MemoryLifetimeValidationReport MemoryLifetimeValidator::evaluate(
    const MemoryLifetimeValidationDesc& desc) const
{
    MemoryLifetimeValidationReport report;
    report.nativeHandleExposedToUpperLayer =
        desc.nativeHandleExposedToUpperLayer || desc.frameResources.nativeHandleExposedToUpperLayer;

    report.resourceDestructionOrderValid = desc.resourceDestructionOrderValidated
        && desc.frameResources.logicalFrameSlots == 2
        && desc.frameResources.pendingDestroyCount == 0
        && desc.frameResources.pendingDestroyBytes == 0 && desc.frameResources.releasedBytes > 0
        && desc.frameResources.backendFallbacksHiddenFromUpperLayer;
    report.descriptorLifetimeValid =
        desc.descriptorLifetimeValidated && desc.descriptorArenaResetInvalidatedOldAllocations;
    report.swapchainResizeRecreateValid =
        desc.swapchainResizeRecreateValidated && desc.surfaceResourcesReleasedOnResize;
    report.transientAliasingValid =
        desc.transientAliasingValidated && desc.transientAliasesNonOverlapping;
    report.uploadReadbackStagingValid = desc.uploadStagingValidated && desc.readbackStagingValidated
        && desc.stagingResourcesBackendOwned && desc.uploadStagingBounded
        && desc.uploadStagingNativeHandlesHidden;
    report.hotPathAllocationBounded = desc.noHotPathUnboundedAllocation && desc.noBlockingWait;

    if (!report.resourceDestructionOrderValid) {
        arp::core::appendDiagnostic(report.diagnostics,
            "memory lifetime validation requires deterministic resource destruction order");
    }
    if (!report.descriptorLifetimeValid) {
        arp::core::appendDiagnostic(report.diagnostics,
            "memory lifetime validation requires descriptor lifetime invalidation");
    }
    if (!report.swapchainResizeRecreateValid) {
        arp::core::appendDiagnostic(report.diagnostics,
            "memory lifetime validation requires swapchain resize/recreate release evidence");
    }
    if (!report.transientAliasingValid) {
        arp::core::appendDiagnostic(report.diagnostics,
            "memory lifetime validation requires non-overlapping transient alias evidence");
    }
    if (!report.uploadReadbackStagingValid) {
        arp::core::appendDiagnostic(report.diagnostics,
            "memory lifetime validation requires backend-owned bounded upload/readback staging "
            "with hidden native handles");
    }
    if (!report.hotPathAllocationBounded) {
        arp::core::appendDiagnostic(report.diagnostics,
            "memory lifetime validation rejected hot path unbounded allocation or blocking wait");
    }
    if (report.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            report.diagnostics, "memory lifetime validation rejected native handle exposure");
    }

    report.success = report.resourceDestructionOrderValid && report.descriptorLifetimeValid
        && report.swapchainResizeRecreateValid && report.transientAliasingValid
        && report.uploadReadbackStagingValid && report.hotPathAllocationBounded
        && !report.nativeHandleExposedToUpperLayer;
    return report;
}

MemoryLifetimeValidationReport evaluateMemoryLifetimeValidation(
    const MemoryLifetimeValidationDesc& desc)
{
    return MemoryLifetimeValidator{}.evaluate(desc);
}

} // namespace arp::rhi
