#include "rhi/backends/webgl/webgl_runtime.h"
#include "rhi/frontend/webgl_execution_telemetry.h"
#include "rhi/semantics/webgl_context_recovery.h"
#include "rhi/semantics/webgl_cpu_retirement.h"
#include "rhi/semantics/webgl_surface_lifecycle.h"

#include <cassert>
#include <cstring>

using namespace arp;
using namespace arp::rhi::semantics;

int main()
{
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)})
        assert((WebGlExecutionTelemetry::backendDiagnostic(backend) == nullptr)
            == (backend == rhi::Backend::WebGL));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    WebGlExecutionTelemetry ledger;
    ledger.submission(WebGlSubmissionOutcome::Accepted, 4);
    ledger.submission(WebGlSubmissionOutcome::Failed, 1);
    ledger.submission(WebGlSubmissionOutcome::Lost, 0);
    WebGlMemoryCounters memory;
    memory.objectCount = 7;
    memory.cpuUploadBytes = 128;
    ledger.observe(memory);
    memory.objectCount = 0;
    auto report = ledger.report(memory);
    assert(report.complete && !report.gpuCompletionKnown && report.execution.submitAttempts == 3);
    assert(report.execution.accepted == 1 && report.execution.failed == 1
        && report.execution.lost == 1);
    assert(report.execution.acceptedOperations == 5 && report.peak.objectCount == 7
        && report.live.objectCount == 0);
    ledger.add(&WebGlExecutionCounters::nativeCalls, UINT64_MAX);
    assert(ledger.report(memory).complete);
    ledger.add(&WebGlExecutionCounters::nativeCalls);
    report = ledger.report(memory);
    assert(!report.complete && report.execution.nativeCalls == UINT64_MAX
        && std::strcmp(report.diagnostic, "RHI-WEBGL1-EXECUTION-TELEMETRY-OVERFLOW") == 0);
    ledger.add(&WebGlExecutionCounters::draws);
    assert(!ledger.report({}).complete && ledger.report({}).execution.draws == 1);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(WebGlCpuRetirementContract::validate(rhi::Backend::WebGL, {}));
    assert(WebGlCpuRetirementContract::validate(rhi::Backend::WebGL, {1, false}));
    assert(WebGlCpuRetirementContract::validate(rhi::Backend::WebGL, {2, true}));
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)})
        assert((WebGlCpuRetirementContract::validate(backend, {1, true}) == nullptr)
            == (backend == rhi::Backend::WebGL));
    WebGlSurfaceLifecycleRequest surface;
    assert(WebGlSurfaceLifecycleContract::validate(rhi::Backend::WebGL, surface));
    surface = {1, true, 16};
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)})
        assert((WebGlSurfaceLifecycleContract::validate(backend, surface) == nullptr)
            == (backend == rhi::Backend::WebGL));
    surface.prototypeOptIn = false;
    assert(WebGlSurfaceLifecycleContract::validate(rhi::Backend::WebGL, surface));
    surface = {1, true, 0};
    assert(WebGlSurfaceLifecycleContract::validate(rhi::Backend::WebGL, surface));
    surface = {2, true, 16};
    assert(WebGlSurfaceLifecycleContract::validate(rhi::Backend::WebGL, surface));
    WebGlContextRecoveryRequest recovery;
    assert(WebGlContextRecoveryContract::validate(rhi::Backend::WebGL, recovery));
    recovery = {1, true, 2};
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)})
        assert((WebGlContextRecoveryContract::validate(backend, recovery) == nullptr)
            == (backend == rhi::Backend::WebGL));
    recovery.prototypeOptIn = false;
    assert(WebGlContextRecoveryContract::validate(rhi::Backend::WebGL, recovery));
    recovery.prototypeOptIn = true;
    recovery.maxRestorations = 0;
    assert(WebGlContextRecoveryContract::validate(rhi::Backend::WebGL, recovery));
    recovery.maxRestorations = UINT32_MAX;
    assert(!WebGlContextRecoveryContract::validate(rhi::Backend::WebGL, recovery));
    recovery.contractVersion = 2;
    assert(WebGlContextRecoveryContract::validate(rhi::Backend::WebGL, recovery));
    WebGlContextOwnershipRequest request;
    assert(WebGlContextOwnershipContract::validate(request));
    request.contractVersion = 1;
    request.canvasSelector = "#canvas";
    request.context.contractVersion = 1;
    request.colorSpace = WebGlCanvasColorSpace::SRgb;
    // Explicit contract fixture budgets, not implementation defaults.
    request.maxCpuMetadataBytes = 256 * 1024;
    request.maxIdentityFieldBytes = 4096;
    assert(!WebGlContextOwnershipContract::validate(request));
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)}) {
        const auto* route = WebGlContextOwnershipContract::validateForBackend(backend, request);
        assert((route == nullptr) == (backend == rhi::Backend::WebGL));
    }
    for (const auto selector : {std::string_view{}, std::string_view{"#canvas\0other", 13}}) {
        auto invalid = request;
        invalid.canvasSelector = selector;
        assert(std::strcmp(WebGlContextOwnershipContract::validate(invalid),
                   "RHI-WEBGL1-CONTEXT-CANVAS-SELECTOR")
            == 0);
    }
    auto invalid = request;
    invalid.colorSpace = WebGlCanvasColorSpace::Unspecified;
    assert(WebGlContextOwnershipContract::validate(invalid));
    invalid = request;
    invalid.maxIdentityFieldBytes = UINT32_MAX;
    assert(WebGlContextOwnershipContract::validate(invalid));
    invalid = request;
    invalid.extensionOptIn.extensions.derivatives = true;
    assert(std::strcmp(WebGlContextOwnershipContract::validate(invalid),
               "RHI-WEBGL1-EXTENSION-OPT-IN-REQUIRED")
        == 0);
    invalid.extensionOptIn.contractVersion = 1;
    assert(!WebGlContextOwnershipContract::validate(invalid));
    const char* diagnostic = nullptr;
    assert(!rhi::webgl::WebGlRuntime::create(request, {}, diagnostic));
    assert(std::strcmp(diagnostic, "RHI-WEBGL1-CONTEXT-CPU-QUOTA") == 0);
    auto quota = core::makeShared<core::CpuMemoryQuota>(request.maxCpuMetadataBytes);
    const auto live = quota->liveBytes();
#ifndef __EMSCRIPTEN__
    assert(!rhi::webgl::WebGlRuntime::create(request, quota, diagnostic));
    assert(std::strcmp(diagnostic, "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE") == 0);
    assert(quota->liveBytes() == live);
#endif
}
