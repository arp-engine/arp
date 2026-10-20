#include "rhi/conformance/matrix.h"

#include "core/diagnostic_builder.h"
#include "rhi/semantics/conformance_evidence_admission.h"

#include <utility>

namespace arp::rhi {

namespace {

// Existing CPU fixture identity hash. This is not a render-output hash.
std::uint64_t hashValue(std::uint64_t hash, std::uint64_t value)
{
    constexpr std::uint64_t prime = 1099511628211ull;
    for (std::uint32_t byte = 0; byte < 8; ++byte) {
        hash ^= static_cast<unsigned char>((value >> (byte * 8)) & 0xffu);
        hash *= prime;
    }
    return hash;
}

bool platformRuntimeMatchesBackend(Backend backend, ConformancePlatformRuntime runtime)
{
    switch (runtime) {
    case ConformancePlatformRuntime::WindowsMingwVulkanHiddenWindow:
    case ConformancePlatformRuntime::AndroidVulkanLifecycle:
        return backend == Backend::Vulkan;
    case ConformancePlatformRuntime::BrowserWebGpuCanvas:
        return backend == Backend::WebGPU;
    case ConformancePlatformRuntime::MacOsMetalDrawable:
    case ConformancePlatformRuntime::IosMetalLifecycle:
        return backend == Backend::Metal;
    }
    return false;
}

} // namespace

core::pmr::Vector<ConformanceScenario> defaultRhiConformanceScenarios()
{
    return {
        ConformanceScenario::Clear,
        ConformanceScenario::Triangle,
        ConformanceScenario::IndexedMesh,
        ConformanceScenario::TextureSampling,
        ConformanceScenario::UniformBuffer,
        ConformanceScenario::StorageBuffer,
        ConformanceScenario::StorageTexture,
        ConformanceScenario::Mrt,
        ConformanceScenario::PostprocessPingPong,
        ConformanceScenario::TransientAliasing,
        ConformanceScenario::Resize,
        ConformanceScenario::SwapchainRecreate,
    };
}

std::uint64_t deterministicConformanceScenarioHash(ConformanceScenario scenario, std::uint64_t salt)
{
    std::uint64_t hash = 1469598103934665603ull;
    hash = hashValue(hash, static_cast<std::uint32_t>(scenario));
    hash = hashValue(hash, salt);
    return hash == 0u ? 1u : hash;
}

BackendConformanceReport evaluateBackendConformance(const BackendConformanceDesc& desc)
{
    BackendConformanceReport report;
    report.backend = desc.backend;
    report.nativeHandleExposedToUpperLayer = desc.nativeHandleExposedToUpperLayer;
    report.diagnostics =
        semantics::ConformanceEvidenceAdmission::descriptorDiagnostic(desc.backend);
    const auto scenarios = defaultRhiConformanceScenarios();
    report.scenarios.reserve(scenarios.size());
    for (const auto scenario : scenarios) {
        ConformanceScenarioResult result;
        result.scenario = scenario;
        result.mode = FallbackMode::DiagnosticFailure;
        result.diagnostics = report.diagnostics;
        report.scenarios.push_back(std::move(result));
        ++report.totalScenarioCount;
        ++report.failedScenarioCount;
    }
    // Descriptor claims and CPU fixture hashes never publish visual evidence,
    // parity or readiness. Future authenticated native evidence uses a new
    // admission contract, not these legacy booleans.
    return report;
}

RhiConformanceMatrixReport evaluateRhiConformanceMatrix(const RhiConformanceMatrixDesc& desc)
{
    RhiConformanceMatrixReport report;
    report.backendCount = static_cast<std::uint32_t>(desc.backends.size());
    report.backendReadinessPromotionGated = true;
    if (desc.backends.empty()) {
        report.diagnostics = "RHI conformance matrix requires at least one backend";
        return report;
    }
    report.backends.reserve(desc.backends.size());
    for (const auto& backendDesc : desc.backends) {
        auto backendReport = evaluateBackendConformance(backendDesc);
        report.totalScenarioCount += backendReport.totalScenarioCount;
        report.failedScenarioCount += backendReport.failedScenarioCount;
        ++report.hardwareValidationMissingCount;
        report.nativeHandleExposedToUpperLayer =
            report.nativeHandleExposedToUpperLayer || backendReport.nativeHandleExposedToUpperLayer;
        core::appendDiagnostic(report.diagnostics, backendReport.diagnostics);
        report.backends.push_back(std::move(backendReport));
    }
    return report;
}

bool hasConformanceScenario(const BackendConformanceReport& report, ConformanceScenario scenario)
{
    for (const auto& result : report.scenarios) {
        if (result.scenario == scenario && result.success) {
            return true;
        }
    }
    return false;
}

BackendPlatformValidationReport evaluateBackendPlatformValidation(
    const BackendPlatformValidationDesc& desc)
{
    BackendPlatformValidationReport report;
    report.backend = desc.backend;
    report.runtime = desc.runtime;
    report.runtimeMatchesBackend = platformRuntimeMatchesBackend(desc.backend, desc.runtime);
    report.diagnosticOnly = true;
    report.platformGap = true;
    report.diagnostics = semantics::ConformanceEvidenceAdmission::platformDiagnostic(desc.backend);
    if (!report.runtimeMatchesBackend) {
        core::appendDiagnostic(report.diagnostics, "Platform Validated runtime/backend mismatch");
    }
    return report;
}

} // namespace arp::rhi
