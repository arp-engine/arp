#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/conformance/original_output.h"
#include "rhi/semantics/fallback_plan.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arp::rhi {

class GpuDevice;

enum class ConformanceScenario {
    Clear,
    Triangle,
    IndexedMesh,
    TextureSampling,
    UniformBuffer,
    StorageBuffer,
    StorageTexture,
    Mrt,
    PostprocessPingPong,
    TransientAliasing,
    Resize,
    SwapchainRecreate
};

enum class ConformancePlatformRuntime {
    WindowsMingwVulkanHiddenWindow,
    BrowserWebGpuCanvas,
    MacOsMetalDrawable,
    IosMetalLifecycle,
    AndroidVulkanLifecycle
};

struct BackendConformanceDesc {
    // Legacy source-compatible claims only. ConformanceEvidenceAdmission/1
    // refuses them as native/output/readiness evidence for every backend.
    Backend backend = Backend::Null;
    bool backendAvailable = false;
    bool hardwareValidationObserved = false;
    bool graphics = false;
    bool compute = false;
    bool sampledTextures = false;
    bool uniformBuffers = false;
    bool storageBuffers = false;
    bool storageTextures = false;
    std::uint32_t mrtColorAttachmentCount = 1;
    bool postprocessPingPong = false;
    bool transientAliasing = false;
    bool allowTransientAliasingDegradation = false;
    bool resize = false;
    bool swapchainRecreate = false;
    bool deterministicVisualHash = false;
    std::uint64_t visualHashSalt = 0;
    bool nativeHandleExposedToUpperLayer = false;
};

struct ConformanceScenarioResult {
    ConformanceScenario scenario = ConformanceScenario::Clear;
    bool success = false;
    FallbackMode mode = FallbackMode::Native;
    bool visualHashValidated = false;
    std::uint64_t deterministicHash = 0;
    bool realBackendExecuted = false;
    bool syntheticHashOnly = true;
    bool resourceCreated = false;
    bool commandEncoded = false;
    bool queueSubmitted = false;
    bool swapchainAcquired = false;
    bool swapchainPresented = false;
    bool resizeHandled = false;
    bool swapchainRecreated = false;
    core::pmr::String diagnostics;
};

struct BackendConformanceReport {
    bool success = false;
    core::pmr::String diagnostics;
    Backend backend = Backend::Null;
    std::uint32_t totalScenarioCount = 0;
    std::uint32_t passedScenarioCount = 0;
    std::uint32_t failedScenarioCount = 0;
    std::uint32_t degradedScenarioCount = 0;
    bool visualHashValidated = false;
    std::uint64_t deterministicVisualHash = 0;
    std::uint64_t parityHash = 0;
    bool backendReadinessPromoted = false;
    bool nativeHandleExposedToUpperLayer = false;
    core::pmr::Vector<ConformanceScenarioResult> scenarios;
};

struct RhiConformanceMatrixDesc {
    core::pmr::Vector<BackendConformanceDesc> backends;
};

struct RhiConformanceMatrixReport {
    bool success = false;
    core::pmr::String diagnostics;
    std::uint32_t backendCount = 0;
    std::uint32_t totalScenarioCount = 0;
    std::uint32_t passedScenarioCount = 0;
    std::uint32_t failedScenarioCount = 0;
    std::uint32_t degradedScenarioCount = 0;
    bool backendParityValidated = false;
    bool deterministicVisualHashValidated = false;
    bool backendReadinessPromotionGated = false;
    std::uint32_t backendReadyCount = 0;
    std::uint32_t hardwareValidationMissingCount = 0;
    std::uint64_t matrixHash = 0;
    bool nativeHandleExposedToUpperLayer = false;
    core::pmr::Vector<BackendConformanceReport> backends;
};

struct ConformanceBackendRuntimeDesc {
    // The former heartbeat facade has no authenticated scenario workload.
    // Its runner refuses before invoking this factory; real workload evidence
    // and admission remain an explicit P4 deliverable.
    Backend backend = Backend::Null;
    bool runtimeAvailable = false;
    core::pmr::String unavailableDiagnostics;
    std::function<arp::core::UniquePtr<GpuDevice>()> createDevice;
    std::uint64_t visualHashSalt = 0;
    bool renderOutputHashCaptured = false;
    std::uint64_t deterministicRenderOutputHash = 0;
};

struct ConformanceBackendExecutionReport {
    bool success = false;
    core::pmr::String diagnostics;
    Backend backend = Backend::Null;
    bool runtimeAvailable = false;
    bool deviceCreated = false;
    bool realExecutionAttempted = false;
    bool sharedScenarioListExecuted = false;
    bool descriptorOnlySynthetic = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool resourceCreationEvidence = false;
    bool commandEncodingEvidence = false;
    bool queueSubmitEvidence = false;
    bool acquirePresentEvidence = false;
    bool resizeLifecycleEvidence = false;
    bool visualHashEvidence = false;
    std::uint64_t deterministicRenderOutputHash = 0;
    bool backendValidated = false;
    std::uint32_t totalScenarioCount = 0;
    std::uint32_t passedScenarioCount = 0;
    std::uint32_t failedScenarioCount = 0;
    std::uint32_t skippedScenarioCount = 0;
    core::pmr::Vector<ConformanceScenarioResult> scenarios;
};

struct RhiConformanceRunnerDesc {
    core::pmr::Vector<ConformanceScenario> scenarios;
    core::pmr::Vector<ConformanceBackendRuntimeDesc> backends;
};

struct RhiConformanceRunnerReport {
    bool success = false;
    core::pmr::String diagnostics;
    bool realExecutionRunnerUsed = false;
    bool sharedScenarioListExecuted = false;
    bool descriptorOnlySyntheticInputRejected = false;
    bool backendValidationGated = false;
    std::uint32_t backendCount = 0;
    std::uint32_t runtimeUnavailableCount = 0;
    std::uint32_t availableBackendExecutionCount = 0;
    std::uint32_t backendValidatedCount = 0;
    std::uint32_t totalScenarioCount = 0;
    std::uint32_t passedScenarioCount = 0;
    std::uint32_t failedScenarioCount = 0;
    std::uint32_t skippedScenarioCount = 0;
    bool nativeHandleExposedToUpperLayer = false;
    core::pmr::Vector<ConformanceBackendExecutionReport> backends;
};

struct BackendPlatformValidationDesc {
    // Caller claims cannot establish Backend/Platform Validated evidence.
    Backend backend = Backend::Null;
    ConformancePlatformRuntime runtime = ConformancePlatformRuntime::WindowsMingwVulkanHiddenWindow;
    bool backendValidated = false;
    bool runtimeEvidenceCaptured = false;
    bool deterministicOutputHashCaptured = false;
    bool diagnosticOnly = false;
    bool platformGap = false;
};

struct BackendPlatformValidationReport {
    bool success = false;
    core::pmr::String diagnostics;
    Backend backend = Backend::Null;
    ConformancePlatformRuntime runtime = ConformancePlatformRuntime::WindowsMingwVulkanHiddenWindow;
    bool backendValidated = false;
    bool runtimeEvidenceCaptured = false;
    bool deterministicOutputHashCaptured = false;
    bool diagnosticOnly = false;
    bool platformGap = false;
    bool runtimeMatchesBackend = false;
    bool platformValidated = false;
};

class RhiConformanceRunner {
public:
    explicit RhiConformanceRunner(RhiConformanceRunnerDesc desc);

    RhiConformanceRunnerReport run() const;

    // The original output path accepts only the issuer-owned sealed source.
    // Legacy runtime factories, flags and hashes are not consulted.
    static OriginalConformanceAuditReport auditOriginal(const OriginalConformanceRun* run) noexcept
    {
        return OriginalConformanceAudit::inspect(run);
    }

private:
    RhiConformanceRunnerDesc desc_;
};

core::pmr::Vector<ConformanceScenario> defaultRhiConformanceScenarios();

std::uint64_t deterministicConformanceScenarioHash(
    ConformanceScenario scenario, std::uint64_t salt);

BackendConformanceReport evaluateBackendConformance(const BackendConformanceDesc& desc);

RhiConformanceMatrixReport evaluateRhiConformanceMatrix(const RhiConformanceMatrixDesc& desc);

bool hasConformanceScenario(const BackendConformanceReport& report, ConformanceScenario scenario);

BackendPlatformValidationReport evaluateBackendPlatformValidation(
    const BackendPlatformValidationDesc& desc);

} // namespace arp::rhi
