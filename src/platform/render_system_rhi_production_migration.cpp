#include "platform/render_system_rhi_production_migration.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "rhi/semantics/conformance_evidence_admission.h"

namespace arp::platform {

namespace {

bool rendererCoversRequiredWork(const RenderSystemRhiProductionMigrationInput& input)
{
    return input.rendererFramePlanBuilt && input.rendererRhiPassCount >= 5u
        && input.rendererSceneMovedToRhi && input.rendererGuiMovedToRhi
        && input.rendererPostprocessMovedToRhi && input.rendererComputeMovedToRhi
        && input.rendererTransferMovedToRhi && !input.rendererBackendSpecificBranchingRequired
        && !input.rendererNativeHandleExposedToUpperLayer;
}

bool guiUsesRhi(const RenderSystemRhiProductionMigrationInput& input)
{
    return input.guiRecordedThroughRhiCommandEncoder && !input.guiUsedRemovedModuleCompatibility
        && !input.guiNativeHandleExposedToUpperLayer;
}

bool renderGraphUsesRhi(const RenderSystemRhiProductionMigrationInput& input)
{
    return input.renderGraphRecordedThroughRhiCommandEncoder
        && input.renderGraphBackendConsumableBarrierPlan
        && !input.renderGraphNativeHandleExposedToUpperLayer;
}

} // namespace

RenderSystemRhiProductionMigrationReport RenderSystemRhiProductionMigrationGate::evaluate(
    const RenderSystemRhiProductionMigrationInput& input) const
{
    return evaluateRenderSystemRhiProductionMigration(input);
}

RenderSystemRhiProductionMigrationReport evaluateRenderSystemRhiProductionMigration(
    const RenderSystemRhiProductionMigrationInput& input)
{
    RenderSystemRhiProductionMigrationReport report;
    // The legacy aggregate is publicly constructible and has no authenticated
    // workload/capture evidence. Even forged success/parity/hash flags cannot
    // grant production readiness. Structural intent remains diagnostic only.
    report.diagnostics = rhi::semantics::ConformanceEvidenceAdmission::aggregateDiagnostic();
    report.rendererProductionPathUsesRhiDrawPlans = rendererCoversRequiredWork(input);
    report.guiProductionPathUsesRhiCommandEncoder = guiUsesRhi(input);
    report.renderGraphProductionPathUsesRhiCommandEncoder = renderGraphUsesRhi(input);
    report.removedModuleRouteRejected = !input.removedModuleCompatibilityRouteSelected
        && !input.removedModuleRouteInvokedForProductionFrame;
    report.removedModuleRouteUsedForProductionFrame = input.removedModuleCompatibilityRouteSelected
        || input.removedModuleRouteInvokedForProductionFrame;
    report.nativeHandleExposedToUpperLayer = input.conformance.nativeHandleExposedToUpperLayer
        || input.rendererNativeHandleExposedToUpperLayer || input.guiNativeHandleExposedToUpperLayer
        || input.renderGraphNativeHandleExposedToUpperLayer;

    if (report.rendererProductionPathUsesRhiDrawPlans) {
        ++report.migratedProductionPathCount;
    }
    if (report.guiProductionPathUsesRhiCommandEncoder) {
        ++report.migratedProductionPathCount;
    }
    if (report.renderGraphProductionPathUsesRhiCommandEncoder) {
        ++report.migratedProductionPathCount;
    }

    if (!report.backendParityEvidenceAccepted) {
        arp::core::appendDiagnostic(
            report.diagnostics, "RHI production migration requires backend parity evidence");
    }
    if (!report.visualRegressionEvidenceAccepted) {
        arp::core::appendDiagnostic(
            report.diagnostics, "RHI production migration requires visual regression evidence");
    }
    if (!report.rendererProductionPathUsesRhiDrawPlans) {
        arp::core::appendDiagnostic(
            report.diagnostics, "renderer production path requires RHI draw/frame plans");
    }
    if (!report.guiProductionPathUsesRhiCommandEncoder) {
        arp::core::appendDiagnostic(
            report.diagnostics, "GUI production path requires RHI command encoder recording");
    }
    if (!report.renderGraphProductionPathUsesRhiCommandEncoder) {
        arp::core::appendDiagnostic(report.diagnostics,
            "RenderGraph production path requires RHI command encoder recording");
    }
    if (input.removedModuleCompatibilityRouteSelected) {
        arp::core::appendDiagnostic(report.diagnostics,
            "removed module route must not be selected for the migrated production frame");
    }
    if (input.removedModuleRouteInvokedForProductionFrame) {
        arp::core::appendDiagnostic(report.diagnostics,
            "removed module route must not be invoked for the migrated production frame");
    }
    if (report.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            report.diagnostics, "RHI production migration rejected native handle exposure");
    }

    report.success = report.backendParityEvidenceAccepted && report.visualRegressionEvidenceAccepted
        && report.rendererProductionPathUsesRhiDrawPlans
        && report.guiProductionPathUsesRhiCommandEncoder
        && report.renderGraphProductionPathUsesRhiCommandEncoder
        && report.removedModuleRouteRejected && !report.nativeHandleExposedToUpperLayer
        && report.migratedProductionPathCount == 3u;
    return report;
}

} // namespace arp::platform
