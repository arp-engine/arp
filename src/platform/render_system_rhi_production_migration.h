#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/conformance/matrix.h"

#include <cstdint>
#include <string>

namespace arp::platform {

struct RenderSystemRhiProductionMigrationInput {
    rhi::RhiConformanceMatrixReport conformance;
    bool rendererFramePlanBuilt = false;
    std::uint32_t rendererRhiPassCount = 0;
    bool rendererSceneMovedToRhi = false;
    bool rendererGuiMovedToRhi = false;
    bool rendererPostprocessMovedToRhi = false;
    bool rendererComputeMovedToRhi = false;
    bool rendererTransferMovedToRhi = false;
    bool rendererBackendSpecificBranchingRequired = false;
    bool rendererNativeHandleExposedToUpperLayer = false;
    bool guiRecordedThroughRhiCommandEncoder = false;
    bool guiUsedRemovedModuleCompatibility = false;
    bool guiNativeHandleExposedToUpperLayer = false;
    bool renderGraphRecordedThroughRhiCommandEncoder = false;
    bool renderGraphBackendConsumableBarrierPlan = false;
    bool renderGraphNativeHandleExposedToUpperLayer = false;
    bool removedModuleCompatibilityRouteSelected = false;
    bool removedModuleRouteInvokedForProductionFrame = false;
};

struct RenderSystemRhiProductionMigrationReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool backendParityEvidenceAccepted = false;
    bool visualRegressionEvidenceAccepted = false;
    bool rendererProductionPathUsesRhiDrawPlans = false;
    bool guiProductionPathUsesRhiCommandEncoder = false;
    bool renderGraphProductionPathUsesRhiCommandEncoder = false;
    bool removedModuleRouteRejected = false;
    bool removedModuleRouteUsedForProductionFrame = false;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t migratedProductionPathCount = 0;
};

class ARP_API RenderSystemRhiProductionMigrationGate {
public:
    RenderSystemRhiProductionMigrationReport evaluate(
        const RenderSystemRhiProductionMigrationInput& input) const;
};

ARP_API RenderSystemRhiProductionMigrationReport evaluateRenderSystemRhiProductionMigration(
    const RenderSystemRhiProductionMigrationInput& input);

} // namespace arp::platform
