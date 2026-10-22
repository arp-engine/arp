#include "core/memory/cpu_allocator.h"
#include "platform/render_system_legacy_rhi_hot_path.h"
#include "rhi/conformance/matrix.h"

#include <cassert>
#include <string>

namespace {

arp::rhi::BackendConformanceDesc makeBackendDesc(arp::rhi::Backend backend)
{
    arp::rhi::BackendConformanceDesc desc;
    desc.backend = backend;
    desc.backendAvailable = true;
    desc.hardwareValidationObserved = true;
    desc.graphics = true;
    desc.compute = true;
    desc.sampledTextures = true;
    desc.uniformBuffers = true;
    desc.storageBuffers = true;
    desc.storageTextures = true;
    desc.mrtColorAttachmentCount = 4;
    desc.postprocessPingPong = true;
    desc.transientAliasing = true;
    desc.resize = true;
    desc.swapchainRecreate = true;
    desc.deterministicVisualHash = true;
    return desc;
}

arp::rhi::RhiConformanceMatrixReport makePassingConformance()
{
    arp::rhi::RhiConformanceMatrixDesc desc;
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::Null));
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::Vulkan));
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::Metal));
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::WebGPU));
    return arp::rhi::evaluateRhiConformanceMatrix(desc);
}

arp::platform::LegacyRhiHotPathQuarantineInput makeReadyInput()
{
    arp::platform::LegacyRhiHotPathQuarantineInput input;
    input.conformance = makePassingConformance();
    input.compatibilityFallbackExplicit = true;
    input.compatibilityFallbackOnly = true;
    input.invokedForProductionFrame = false;
    input.nativeHandleExposedToUpperLayer = false;
    return input;
}

arp::rhi::ConformanceBackendExecutionReport makeRealBackendExecution(arp::rhi::Backend backend)
{
    arp::rhi::ConformanceBackendExecutionReport execution;
    execution.success = true;
    execution.backend = backend;
    execution.runtimeAvailable = true;
    execution.deviceCreated = true;
    execution.realExecutionAttempted = true;
    execution.sharedScenarioListExecuted = true;
    execution.descriptorOnlySynthetic = false;
    execution.nativeHandleExposedToUpperLayer = false;
    execution.resourceCreationEvidence = true;
    execution.commandEncodingEvidence = true;
    execution.queueSubmitEvidence = true;
    execution.acquirePresentEvidence = true;
    execution.resizeLifecycleEvidence = true;
    execution.visualHashEvidence = true;
    execution.deterministicRenderOutputHash = 0x1234abcd;
    execution.backendValidated = true;
    execution.totalScenarioCount = 12;
    execution.passedScenarioCount = 12;
    return execution;
}

arp::rhi::RhiConformanceRunnerReport makeRealBackendReadiness(arp::rhi::Backend backend)
{
    arp::rhi::RhiConformanceRunnerReport readiness;
    readiness.success = true;
    readiness.realExecutionRunnerUsed = true;
    readiness.sharedScenarioListExecuted = true;
    readiness.descriptorOnlySyntheticInputRejected = true;
    readiness.backendValidationGated = true;
    readiness.backendCount = 1;
    readiness.availableBackendExecutionCount = 1;
    readiness.backendValidatedCount = 1;
    readiness.totalScenarioCount = 12;
    readiness.passedScenarioCount = 12;
    readiness.nativeHandleExposedToUpperLayer = false;
    readiness.backends.push_back(makeRealBackendExecution(backend));
    return readiness;
}

arp::platform::LegacyRhiHotPathQuarantineInput makeSelectedMigratedFrameInput(
    arp::rhi::Backend backend)
{
    auto input = makeReadyInput();
    input.realBackendReadiness = makeRealBackendReadiness(backend);
    input.selectedMigratedBackend = backend;
    input.selectedMigratedProductionFrame = true;
    input.invokedForProductionFrame = true;
    return input;
}

} // namespace

int main()
{
    using namespace arp::platform;

    const auto ready = evaluateLegacyRhiHotPathQuarantine(makeReadyInput());
    assert(ready.success);
    assert(ready.retiredFromProductionHotPath);
    assert(ready.backendParityEvidenceAccepted);
    assert(ready.requiredBackendParityAccepted);
    assert(ready.requiredBackendCount == 4u);
    assert(ready.compatibilityFallbackOnly);
    assert(!ready.invokedForProductionFrame);
    assert(!ready.nativeHandleExposedToUpperLayer);
    assert(ready.quarantineLabel == LegacyRhiRenderSystemHotPath::kQuarantineLabel);

    auto missingBackendInput = makeReadyInput();
    missingBackendInput.conformance.backends.pop_back();
    missingBackendInput.conformance.backendCount = 3;
    const auto missingBackend = evaluateLegacyRhiHotPathQuarantine(missingBackendInput);
    assert(!missingBackend.success);
    assert(!missingBackend.requiredBackendParityAccepted);
    assert(
        missingBackend.diagnostics.find("Null, Vulkan, Metal, WebGPU") != std::pmr::string::npos);

    auto productionInvocationInput = makeReadyInput();
    productionInvocationInput.invokedForProductionFrame = true;
    const auto productionInvocation = evaluateLegacyRhiHotPathQuarantine(productionInvocationInput);
    assert(!productionInvocation.success);
    assert(productionInvocation.invokedForProductionFrame);
    assert(productionInvocation.diagnostics.find("production frame") != std::pmr::string::npos);

    const arp::rhi::Backend migratedBackends[] = {
        arp::rhi::Backend::Vulkan,
        arp::rhi::Backend::WebGPU,
    };
    for (const auto backend : migratedBackends) {
        const auto selectedMigratedFrame =
            evaluateLegacyRhiHotPathQuarantine(makeSelectedMigratedFrameInput(backend));
        assert(!selectedMigratedFrame.success);
        assert(selectedMigratedFrame.selectedMigratedBackend == backend);
        assert(selectedMigratedFrame.selectedBackendRealRhiReady);
        assert(selectedMigratedFrame.selectedMigratedProductionFrameProtected);
        assert(selectedMigratedFrame.legacyHotPathRejectedForSelectedMigratedFrame);
        assert(selectedMigratedFrame.diagnostics.find("LegacyRhiRenderSystemHotPath")
            != std::pmr::string::npos);
    }

    auto implicitFallbackInput = makeReadyInput();
    implicitFallbackInput.compatibilityFallbackExplicit = false;
    const auto implicitFallback = evaluateLegacyRhiHotPathQuarantine(implicitFallbackInput);
    assert(!implicitFallback.success);
    assert(!implicitFallback.compatibilityFallbackOnly);
    assert(implicitFallback.diagnostics.find("explicit fallback") != std::pmr::string::npos);

    auto nativeLeakInput = makeReadyInput();
    nativeLeakInput.nativeHandleExposedToUpperLayer = true;
    const auto nativeLeak = evaluateLegacyRhiHotPathQuarantine(nativeLeakInput);
    assert(!nativeLeak.success);
    assert(nativeLeak.nativeHandleExposedToUpperLayer);
    assert(nativeLeak.diagnostics.find("native handle") != std::pmr::string::npos);

    const LegacyRhiRenderSystemHotPath adapter;
    const auto adapterReport = adapter.evaluateQuarantine(makeReadyInput());
    assert(adapterReport.success);
    assert(adapterReport.quarantineLabel == adapter.quarantineLabel());

    return 0;
}
