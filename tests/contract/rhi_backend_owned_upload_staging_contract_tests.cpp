#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/memory_lifetime_validation.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_command_lowering.h"

#include <cassert>
#include <string>

namespace {

bool has(const arp::rhi::semantics::NativeCommandLoweringDecision& decision,
    arp::rhi::semantics::NativeCommandSemantic semantic)
{
    return arp::rhi::semantics::hasNativeCommandSemantic(decision, semantic);
}

arp::rhi::semantics::NativeCommandLoweringDecision lowerUploadStagingWorkload(
    arp::rhi::Backend backend)
{
    arp::rhi::semantics::CommandWorkloadDesc workload;
    workload.label = "BackendOwnedUploadStaging";
    workload.commandCount = 2;
    workload.usesTransferCopies = true;
    workload.usesUploadStaging = true;

    return arp::rhi::semantics::lowerCommandWorkload(
        arp::rhi::semantics::nativeBackendCapabilityContract(backend),
        workload,
        arp::rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);
}

void assertBackendOwnedUploadStagingSemantics()
{
    const auto nullDecision = lowerUploadStagingWorkload(arp::rhi::Backend::Null);
    assert(nullDecision.success);
    assert(has(nullDecision, arp::rhi::semantics::NativeCommandSemantic::NullBackendOwnedStaging));

    const auto vulkanDecision = lowerUploadStagingWorkload(arp::rhi::Backend::Vulkan);
    assert(vulkanDecision.success);
    assert(has(vulkanDecision,
        arp::rhi::semantics::NativeCommandSemantic::VulkanBackendOwnedStagingBuffer));

    const auto metalDecision = lowerUploadStagingWorkload(arp::rhi::Backend::Metal);
    assert(metalDecision.success);
    assert(has(
        metalDecision, arp::rhi::semantics::NativeCommandSemantic::MetalBackendOwnedStagingBuffer));

    const auto webgpuDecision = lowerUploadStagingWorkload(arp::rhi::Backend::WebGPU);
    assert(webgpuDecision.success);
    assert(has(webgpuDecision,
        arp::rhi::semantics::NativeCommandSemantic::WebGpuBackendOwnedStagingBuffer));

    assert(!vulkanDecision.nativeHandleExposedToUpperLayer);
    assert(!metalDecision.nativeHandleExposedToUpperLayer);
    assert(!webgpuDecision.nativeHandleExposedToUpperLayer);
}

arp::rhi::MemoryLifetimeValidationDesc makeCompleteStagingLifetimeDesc()
{
    arp::rhi::MemoryLifetimeValidationDesc desc;
    desc.frameResources.logicalFrameSlots = 2;
    desc.frameResources.releasedBytes = 1;
    desc.frameResources.backendFallbacksHiddenFromUpperLayer = true;
    desc.resourceDestructionOrderValidated = true;
    desc.descriptorLifetimeValidated = true;
    desc.descriptorArenaResetInvalidatedOldAllocations = true;
    desc.swapchainResizeRecreateValidated = true;
    desc.surfaceResourcesReleasedOnResize = true;
    desc.transientAliasingValidated = true;
    desc.transientAliasesNonOverlapping = true;
    desc.uploadStagingValidated = true;
    desc.readbackStagingValidated = true;
    desc.stagingResourcesBackendOwned = true;
    desc.uploadStagingBounded = true;
    desc.uploadStagingNativeHandlesHidden = true;
    desc.noHotPathUnboundedAllocation = true;
    desc.noBlockingWait = true;
    return desc;
}

void assertMemoryLifetimeRejectsLeakyOrUnboundedStaging()
{
    auto desc = makeCompleteStagingLifetimeDesc();
    auto report = arp::rhi::evaluateMemoryLifetimeValidation(desc);
    assert(report.success);
    assert(report.uploadReadbackStagingValid);

    desc.uploadStagingBounded = false;
    report = arp::rhi::evaluateMemoryLifetimeValidation(desc);
    assert(!report.success);
    assert(!report.uploadReadbackStagingValid);
    assert(report.diagnostics.find("bounded") != std::pmr::string::npos);

    desc = makeCompleteStagingLifetimeDesc();
    desc.uploadStagingNativeHandlesHidden = false;
    report = arp::rhi::evaluateMemoryLifetimeValidation(desc);
    assert(!report.success);
    assert(!report.uploadReadbackStagingValid);
    assert(report.diagnostics.find("native handle") != std::pmr::string::npos);

    desc = makeCompleteStagingLifetimeDesc();
    desc.noHotPathUnboundedAllocation = false;
    report = arp::rhi::evaluateMemoryLifetimeValidation(desc);
    assert(!report.success);
    assert(report.diagnostics.find("unbounded") != std::pmr::string::npos);

    desc = makeCompleteStagingLifetimeDesc();
    desc.nativeHandleExposedToUpperLayer = true;
    report = arp::rhi::evaluateMemoryLifetimeValidation(desc);
    assert(!report.success);
    assert(report.diagnostics.find("native handle") != std::pmr::string::npos);
}

} // namespace

int main()
{
    assertBackendOwnedUploadStagingSemantics();
    assertMemoryLifetimeRejectsLeakyOrUnboundedStaging();
    return 0;
}
