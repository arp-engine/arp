#include "core/memory/cpu_allocator.h"
#include "platform/render_system_rhi_production_migration.h"

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

arp::rhi::RhiConformanceMatrixReport makeClaimedConformance()
{
    arp::rhi::RhiConformanceMatrixDesc desc;
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::Null));
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::Vulkan));
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::Metal));
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::WebGPU));
    desc.backends.push_back(makeBackendDesc(arp::rhi::Backend::WebGL));
    return arp::rhi::evaluateRhiConformanceMatrix(desc);
}

arp::platform::RenderSystemRhiProductionMigrationInput makeClaimedReadyInput()
{
    arp::platform::RenderSystemRhiProductionMigrationInput input;
    input.conformance = makeClaimedConformance();
    input.rendererFramePlanBuilt = true;
    input.rendererRhiPassCount = 5;
    input.rendererSceneMovedToRhi = true;
    input.rendererGuiMovedToRhi = true;
    input.rendererPostprocessMovedToRhi = true;
    input.rendererComputeMovedToRhi = true;
    input.rendererTransferMovedToRhi = true;
    input.rendererBackendSpecificBranchingRequired = false;
    input.rendererNativeHandleExposedToUpperLayer = false;
    input.guiRecordedThroughRhiCommandEncoder = true;
    input.guiUsedRemovedModuleCompatibility = false;
    input.guiNativeHandleExposedToUpperLayer = false;
    input.renderGraphRecordedThroughRhiCommandEncoder = true;
    input.renderGraphBackendConsumableBarrierPlan = true;
    input.renderGraphNativeHandleExposedToUpperLayer = false;
    input.removedModuleCompatibilityRouteSelected = false;
    input.removedModuleRouteInvokedForProductionFrame = false;
    return input;
}

} // namespace

int main()
{
    using namespace arp::platform;

    const auto ready = evaluateRenderSystemRhiProductionMigration(makeClaimedReadyInput());
    assert(!ready.success);
    assert(!ready.backendParityEvidenceAccepted);
    assert(!ready.visualRegressionEvidenceAccepted);
    assert(ready.rendererProductionPathUsesRhiDrawPlans);
    assert(ready.guiProductionPathUsesRhiCommandEncoder);
    assert(ready.renderGraphProductionPathUsesRhiCommandEncoder);
    assert(ready.removedModuleRouteRejected);
    assert(!ready.removedModuleRouteUsedForProductionFrame);
    assert(!ready.nativeHandleExposedToUpperLayer);
    assert(ready.migratedProductionPathCount == 3u);

    auto forgedInput = makeClaimedReadyInput();
    forgedInput.conformance.success = true;
    forgedInput.conformance.backendParityValidated = true;
    forgedInput.conformance.deterministicVisualHashValidated = true;
    forgedInput.conformance.backendReadyCount = 5u;
    const auto forged = evaluateRenderSystemRhiProductionMigration(forgedInput);
    assert(!forged.success && !forged.backendParityEvidenceAccepted
        && !forged.visualRegressionEvidenceAccepted);
    assert(forged.diagnostics.find("RHI-CONFORMANCE-AGGREGATE-EVIDENCE-UNAUTHENTICATED")
        != std::pmr::string::npos);

    auto missingParityInput = makeClaimedReadyInput();
    missingParityInput.conformance.backendParityValidated = false;
    const auto missingParity = evaluateRenderSystemRhiProductionMigration(missingParityInput);
    assert(!missingParity.success);
    assert(!missingParity.backendParityEvidenceAccepted);
    assert(missingParity.diagnostics.find("backend parity") != std::pmr::string::npos);

    auto missingGuiInput = makeClaimedReadyInput();
    missingGuiInput.guiRecordedThroughRhiCommandEncoder = false;
    const auto missingGui = evaluateRenderSystemRhiProductionMigration(missingGuiInput);
    assert(!missingGui.success);
    assert(!missingGui.guiProductionPathUsesRhiCommandEncoder);
    assert(missingGui.diagnostics.find("GUI") != std::pmr::string::npos);

    auto removedRouteInput = makeClaimedReadyInput();
    removedRouteInput.removedModuleRouteInvokedForProductionFrame = true;
    const auto removedRoute = evaluateRenderSystemRhiProductionMigration(removedRouteInput);
    assert(!removedRoute.success);
    assert(removedRoute.removedModuleRouteUsedForProductionFrame);
    assert(removedRoute.diagnostics.find("removed module route") != std::pmr::string::npos);

    auto nativeLeakInput = makeClaimedReadyInput();
    nativeLeakInput.rendererNativeHandleExposedToUpperLayer = true;
    const auto nativeLeak = evaluateRenderSystemRhiProductionMigration(nativeLeakInput);
    assert(!nativeLeak.success);
    assert(nativeLeak.nativeHandleExposedToUpperLayer);
    assert(nativeLeak.diagnostics.find("native handle") != std::pmr::string::npos);

    return 0;
}
