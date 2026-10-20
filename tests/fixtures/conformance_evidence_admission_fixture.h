#pragma once

#include "rhi/conformance/matrix.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/conformance_evidence_admission.h"

#include <cassert>
#include <string_view>
#include <utility>

class ConformanceEvidenceAdmissionFixture final {
public:
    static void verify()
    {
        using namespace arp::rhi;
        using Admission = semantics::ConformanceEvidenceAdmission;
        static_assert(Admission::contractVersion == 1u);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (auto backend : {Backend::Null,
                 Backend::Vulkan,
                 Backend::Metal,
                 Backend::WebGPU,
                 Backend::WebGL,
                 static_cast<Backend>(255)}) {
            assert(Admission::descriptorDiagnostic(backend));
            assert(Admission::runtimeDiagnostic(backend));
            assert(Admission::platformDiagnostic(backend));
        }
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
        for (auto backend : {Backend::Null,
                 Backend::Vulkan,
                 Backend::Metal,
                 Backend::WebGPU,
                 Backend::WebGL,
                 static_cast<Backend>(255)}) {
            const auto identityDiagnostic = backend == Backend::WebGL
                ? "RHI-WEBGL1-CONFORMANCE-UNAVAILABLE"
                : backend == static_cast<Backend>(255) ? "RHI-CONFORMANCE-BACKEND-UNKNOWN"
                                                       : nullptr;
            // All rendering support flags are populated. Claimed availability,
            // capture/hardware evidence and even a matching salt cannot turn a
            // descriptor-only report into real GPU output evidence.
            for (unsigned bits = 0; bits != 32u; ++bits) {
                const unsigned flags = bits ^ 7u;
                BackendConformanceDesc input;
                input.backend = backend;
                input.backendAvailable = (flags & 1u) != 0u;
                input.hardwareValidationObserved = (flags & 2u) != 0u;
                input.deterministicVisualHash = (flags & 4u) != 0u;
                input.allowTransientAliasingDegradation = (flags & 8u) != 0u;
                input.nativeHandleExposedToUpperLayer = (flags & 16u) != 0u;
                input.graphics = input.compute = input.sampledTextures = input.uniformBuffers =
                    true;
                input.storageBuffers = input.storageTextures = input.postprocessPingPong = true;
                input.transientAliasing = input.resize = input.swapchainRecreate = true;
                input.mrtColorAttachmentCount = 4u;
                input.visualHashSalt = 123u;
                const auto report = evaluateBackendConformance(input);
                assert(!report.success && !report.backendReadinessPromoted
                    && !report.visualHashValidated);
                assert(report.deterministicVisualHash == 0u && report.parityHash == 0u);
                assert(report.passedScenarioCount == 0u
                    && report.failedScenarioCount == report.totalScenarioCount);
                assert(report.totalScenarioCount == defaultRhiConformanceScenarios().size());
                const auto diagnostic = identityDiagnostic
                    ? identityDiagnostic
                    : "RHI-CONFORMANCE-DESCRIPTOR-EVIDENCE-UNAUTHENTICATED";
                assert(std::string_view(report.diagnostics).find(diagnostic)
                    != std::string_view::npos);
                for (const auto& scenario : report.scenarios) {
                    assert(!scenario.success && scenario.mode == FallbackMode::DiagnosticFailure);
                    assert(!scenario.realBackendExecuted && scenario.syntheticHashOnly
                        && !scenario.visualHashValidated);
                }
                RhiConformanceMatrixDesc matrix;
                matrix.backends.push_back(input);
                const auto aggregate = evaluateRhiConformanceMatrix(matrix);
                assert(!aggregate.success && !aggregate.backendParityValidated
                    && !aggregate.deterministicVisualHashValidated);
                assert(aggregate.backendReadyCount == 0u && aggregate.matrixHash == 0u);
                assert(aggregate.failedScenarioCount == report.totalScenarioCount);
            }

            for (unsigned flags = 0; flags != 16u; ++flags) {
                unsigned factoryCalls = 0u;
                RhiConformanceRunnerDesc input;
                input.scenarios = {ConformanceScenario::Clear, ConformanceScenario::Triangle};
                ConformanceBackendRuntimeDesc runtime;
                runtime.backend = backend;
                runtime.runtimeAvailable = (flags & 1u) != 0u;
                runtime.renderOutputHashCaptured = (flags & 2u) != 0u;
                runtime.deterministicRenderOutputHash = (flags & 4u) != 0u ? 123u : 0u;
                runtime.unavailableDiagnostics = "caller-claimed-success";
                if ((flags & 8u) != 0u) {
                    runtime.createDevice = [&factoryCalls]() -> arp::core::UniquePtr<GpuDevice> {
                        ++factoryCalls;
                        return {};
                    };
                }
                input.backends.push_back(std::move(runtime));
                const auto report = RhiConformanceRunner(std::move(input)).run();
                assert(factoryCalls == 0u);
                assert(!report.success && !report.realExecutionRunnerUsed
                    && !report.sharedScenarioListExecuted);
                assert(report.backendValidatedCount == 0u
                    && report.availableBackendExecutionCount == 0u);
                assert(report.totalScenarioCount == 2u && report.failedScenarioCount == 2u
                    && report.skippedScenarioCount == 0u);
                assert(report.backends.size() == 1u);
                const auto& result = report.backends.front();
                assert(!result.success && !result.backendValidated && !result.deviceCreated
                    && !result.realExecutionAttempted);
                assert(!result.runtimeAvailable && result.descriptorOnlySynthetic);
                assert(!result.resourceCreationEvidence && !result.commandEncodingEvidence
                    && !result.queueSubmitEvidence);
                assert(!result.acquirePresentEvidence && !result.resizeLifecycleEvidence
                    && !result.visualHashEvidence);
                assert(result.deterministicRenderOutputHash == 0u);
                const auto diagnostic = identityDiagnostic
                    ? identityDiagnostic
                    : "RHI-CONFORMANCE-RUNTIME-WORKLOAD-UNIMPLEMENTED";
                assert(std::string_view(result.diagnostics) == diagnostic);
                for (const auto& scenario : result.scenarios) {
                    assert(!scenario.success && scenario.mode == FallbackMode::DiagnosticFailure);
                    assert(!scenario.realBackendExecuted && scenario.syntheticHashOnly);
                    assert(!scenario.visualHashValidated && scenario.deterministicHash == 0u);
                    assert(std::string_view(scenario.diagnostics) == diagnostic);
                }
            }

            for (auto runtime : {ConformancePlatformRuntime::WindowsMingwVulkanHiddenWindow,
                     ConformancePlatformRuntime::BrowserWebGpuCanvas,
                     ConformancePlatformRuntime::MacOsMetalDrawable,
                     ConformancePlatformRuntime::IosMetalLifecycle,
                     ConformancePlatformRuntime::AndroidVulkanLifecycle,
                     static_cast<ConformancePlatformRuntime>(255)}) {
                for (unsigned flags = 0; flags != 32u; ++flags) {
                    BackendPlatformValidationDesc input;
                    input.backend = backend;
                    input.runtime = runtime;
                    input.backendValidated = (flags & 1u) != 0u;
                    input.runtimeEvidenceCaptured = (flags & 2u) != 0u;
                    input.deterministicOutputHashCaptured = (flags & 4u) != 0u;
                    input.diagnosticOnly = (flags & 8u) != 0u;
                    input.platformGap = (flags & 16u) != 0u;
                    const auto report = evaluateBackendPlatformValidation(input);
                    const bool matchingIdentity =
                        (backend == Backend::Vulkan
                            && (runtime
                                    == ConformancePlatformRuntime::WindowsMingwVulkanHiddenWindow
                                || runtime == ConformancePlatformRuntime::AndroidVulkanLifecycle))
                        || (backend == Backend::Metal
                            && (runtime == ConformancePlatformRuntime::MacOsMetalDrawable
                                || runtime == ConformancePlatformRuntime::IosMetalLifecycle))
                        || (backend == Backend::WebGPU
                            && runtime == ConformancePlatformRuntime::BrowserWebGpuCanvas);
                    assert(report.runtimeMatchesBackend == matchingIdentity);
                    assert(
                        !report.success && !report.platformValidated && !report.backendValidated);
                    assert(
                        !report.runtimeEvidenceCaptured && !report.deterministicOutputHashCaptured);
                    assert(report.diagnosticOnly && report.platformGap);
                    const auto diagnostic = identityDiagnostic
                        ? identityDiagnostic
                        : "RHI-CONFORMANCE-PLATFORM-EVIDENCE-UNAUTHENTICATED";
                    assert(std::string_view(report.diagnostics).find(diagnostic)
                        != std::string_view::npos);
                }
            }
        }
        assert(!evaluateRhiConformanceMatrix({}).success);
        assert(!RhiConformanceRunner({}).run().success);
        RhiConformanceRunnerDesc defaults;
        defaults.backends.push_back({});
        const auto defaultReport = RhiConformanceRunner(std::move(defaults)).run();
        assert(defaultReport.failedScenarioCount == defaultRhiConformanceScenarios().size());
        assert(defaultReport.skippedScenarioCount == 0u && !defaultReport.success);
    }
};
