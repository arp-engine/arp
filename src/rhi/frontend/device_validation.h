#pragma once

// Shared L2 validation helpers for RHI device construction.
//
// The Vulkan, Metal and WebGPU `describe*Device()` factory-validation functions
// previously duplicated the same ~5 invariant checks verbatim (only the
// backend-name string differed):
//   - capabilityContract.backend must match the expected backend
//   - memoryBudgetBytes must be non-zero (sanitized telemetry)
//   - allowNativeHandleExposureToUpperLayers must be false
//   - allowPerDrawPipelineCreation must be false
//   - allowPerFrameShaderCompilation must be false
//
// DeviceCapabilitySeed/1: the shared skeleton contains identity metadata only.
// Runtime capability publication belongs to each backend's queried runtime.
//
// This header consolidates both into pure helpers that backends call from their
// describe/capabilities implementations. Backends retain full ownership of
// backend-specific fields (native device properties, runtime-derived swapchain
// counts, capability-contract feature mapping).
//
// Layering: header-only, namespace arp::rhi; backend construction decisions
// belong to semantics/device_construction_lowering.h. No native API dependency,
// no dynamic allocation beyond what the pmr string already incurs.
//
// Status: Contract Complete.

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/device_construction_lowering.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace arp::rhi {

using DeviceConstructionBackendAdmission = semantics::DeviceConstructionBackendAdmission;

// Result of validateSharedDeviceInvariants. When `success` is false,
// `diagnostics` carries the first failing invariant's message and the backend's
// describe function should return immediately. The trailing booleans mirror the
// shared desc fields so the backend can stamp them into its per-backend report
// struct without re-reading the desc.
struct SharedDeviceInvariantReport {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    bool nativeHandleExposedToUpperLayer = false;
    bool perDrawPipelineCreationAllowed = false;
    bool perFrameShaderCompilationAllowed = false;
};

// Validates the invariants common to every RHI device desc. `backendName` is
// the human-readable name used in diagnostics (e.g. "Vulkan"). Returns a report
// the caller stamps into its per-backend DeviceReport; on failure the caller
// returns the report as-is.
//
// The checks, in order, are:
//   1. expectedBackend is a registered construction identity
//   2. capabilityContractBackend == expectedBackend
//   3. memoryBudgetBytes != 0
//   4. !allowNativeHandleExposureToUpperLayers (gated by extensionEnabled)
//   5. !allowPerDrawPipelineCreation (gated by extensionEnabled)
//   6. !allowPerFrameShaderCompilation (gated by extensionEnabled)
inline SharedDeviceInvariantReport validateSharedDeviceInvariants(Backend expectedBackend,
    std::string_view backendName,
    Backend capabilityContractBackend,
    std::uint64_t memoryBudgetBytes,
    bool allowNativeHandleExposureToUpperLayers,
    bool allowPerDrawPipelineCreation,
    bool allowPerFrameShaderCompilation,
    bool extensionEnabled = false)
{
    SharedDeviceInvariantReport report;
    report.success = true;

    if (const auto* diagnostic = DeviceConstructionBackendAdmission::diagnostic(expectedBackend)) {
        report.success = false;
        report.diagnostics = diagnostic;
        RhiErrorHandler::get().report(RhiSeverity::Error, diagnostic);
        return report;
    }

    if (capabilityContractBackend != expectedBackend) {
        report.success = false;
        report.diagnostics = core::pmr::String(backendName, &core::defaultCpuMemoryResource())
            + " RHI device capability contract backend does not match";
        RhiErrorHandler::get().report(RhiSeverity::Error, report.diagnostics.c_str());
        return report;
    }

    if (memoryBudgetBytes == 0) {
        report.success = false;
        report.diagnostics = core::pmr::String(backendName, &core::defaultCpuMemoryResource())
            + " RHI device requires sanitized memory budget telemetry";
        RhiErrorHandler::get().report(RhiSeverity::Error, report.diagnostics.c_str());
        return report;
    }

    if (!extensionEnabled) {
        if (allowNativeHandleExposureToUpperLayers) {
            report.success = false;
            report.diagnostics = core::pmr::String(backendName, &core::defaultCpuMemoryResource())
                + " RHI device must not expose native handles upward";
            RhiErrorHandler::get().report(RhiSeverity::Error, report.diagnostics.c_str());
            return report;
        }

        if (allowPerDrawPipelineCreation) {
            report.success = false;
            report.diagnostics = core::pmr::String(backendName, &core::defaultCpuMemoryResource())
                + " RHI device must not allow per-draw pipeline creation";
            RhiErrorHandler::get().report(RhiSeverity::Error, report.diagnostics.c_str());
            return report;
        }

        if (allowPerFrameShaderCompilation) {
            report.success = false;
            report.diagnostics = core::pmr::String(backendName, &core::defaultCpuMemoryResource())
                + " RHI device must not allow per-frame shader compilation";
            RhiErrorHandler::get().report(RhiSeverity::Error, report.diagnostics.c_str());
            return report;
        }
    }

    report.nativeHandleExposedToUpperLayer = allowNativeHandleExposureToUpperLayers;
    report.perDrawPipelineCreationAllowed = allowPerDrawPipelineCreation;
    report.perFrameShaderCompilationAllowed = allowPerFrameShaderCompilation;
    return report;
}

// Metadata only. An identity or display label cannot establish execution,
// memory intents, queried limits or a surface/frame policy. Backends publish
// their own facts after this neutral seed; Null uses its validator profile.
inline DeviceCapabilities makeSharedDeviceCapabilities(
    Backend backend, std::string_view deviceNameFallback, const std::pmr::string& label)
{
    DeviceCapabilities caps;
    caps.backend = backend;
    caps.deviceName = label.empty() ? deviceNameFallback : std::string_view(label);
    caps.swapchain = {0u, 0u};
    if (const auto* diagnostic = DeviceConstructionBackendAdmission::diagnostic(backend))
        RhiErrorHandler::get().report(RhiSeverity::Error, diagnostic);
    return caps;
}

// Revocation only: this cannot manufacture native facts or grant a feature.
// Preserve the identity and diagnostic reports for callers; their executable
// flags are reconciled by the existing status synchronizer afterwards.
inline void revokeUnavailableNativeDeviceCapabilities(DeviceCapabilities& caps)
{
    caps.core = {};
    caps.computeResources = {};
    caps.explicitFeatures = {};
    caps.features = {};
    caps.memory = {};
    caps.swapchain = {0u, 0u};
    caps.commandRetention = {};
    caps.pushConstantLoweringHasExecutablePayload = false;
    caps.pushConstantLoweringOutcome = PushConstantLoweringOutcome::DiagnosticFailure;
    caps.pushConstantLoweringDiagnosticId = "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE";
    caps.pushConstantLoweringTelemetryLabel = "DeviceCapabilitySeed/1:Unavailable";
    RhiErrorHandler::get().report(RhiSeverity::Error, "RHI-DEVICE-NATIVE-RUNTIME-UNAVAILABLE");
}

} // namespace arp::rhi
