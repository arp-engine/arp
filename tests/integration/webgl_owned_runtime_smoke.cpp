#include "rhi/backends/webgl/webgl_runtime.h"

#include <emscripten.h>
#include <emscripten/html5.h>

using namespace arp;
using namespace arp::rhi;

namespace {
core::UniquePtr<webgl::WebGlRuntime> runtime;
std::shared_ptr<core::CpuMemoryQuota> quota;
core::LifetimeEpoch::Ticket originalEpoch;
double deadline = 0;

EM_JS(void,
    report,
    (int success,
        const char* diagnostic,
        int foreignRejected,
        int lostRejected,
        int epochInvalidated,
        int released),
    {
        // clang-format off
        fetch('/result', {method:'POST', headers:{'Content-Type':'application/json'},
            body:JSON.stringify({type:'WebGlOwnedContextSmoke',contractVersion:1,success:!!success,
                diagnostic:UTF8ToString(diagnostic),webGpuUnavailable:navigator.gpu===undefined,
                foreignCurrentRejected:!!foreignRejected,contextLostRejected:!!lostRejected,
                originalEpochInvalidated:!!epochInvalidated,quotaReleased:!!released,
                subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
                productionBackendValidated:false,gpuCompletionClaimed:false})});
        // clang-format on
    });

EM_JS(int, loseContext, (), {
    // clang-format off
        const gl=GL.currentContext && GL.currentContext.GLctx;
        const extension=gl && gl.getExtension('WEBGL_lose_context');
        if(!extension) return 0;
        extension.loseContext();
        return 1;
    // clang-format on
});

EM_JS(void, renameCanvas, (), {
    // clang-format off
        Module.canvas.id = 'retired-original-canvas';
    // clang-format on
});

void awaitLoss(void*)
{
    const auto* diagnostic = runtime->currentDiagnostic();
    if (!originalEpoch.isCurrent()) {
        const bool lost = diagnostic && std::string_view(diagnostic) == "RHI-WEBGL1-CONTEXT-LOST";
        renameCanvas();
        runtime.reset();
        // Weak observation tickets retain their allocator control block until
        // the observer releases them; invalidation is not allocation release.
        originalEpoch = {};
        report(
            lost, lost ? "" : "WEBGL-OWNED-LOSS-DIAGNOSTIC", 1, lost, 1, quota->liveBytes() == 0);
        return;
    }
    if (emscripten_get_now() >= deadline) {
        report(0, "WEBGL-OWNED-LOSS-CALLBACK-TIMEOUT", 1, 0, 0, 0);
        runtime.reset();
        return;
    }
    emscripten_async_call(awaitLoss, nullptr, 10);
}
} // namespace

int main()
{
    semantics::WebGlContextOwnershipRequest request;
    request.contractVersion = 1;
    request.canvasSelector = "#canvas";
    request.context.contractVersion = 1;
    request.colorSpace = semantics::WebGlCanvasColorSpace::SRgb;
    request.maxCpuMetadataBytes = 256 * 1024;
    request.maxIdentityFieldBytes = 4096;
    quota = core::makeShared<core::CpuMemoryQuota>(request.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    auto missing = request;
    missing.canvasSelector = "#does-not-exist";
    if (webgl::WebGlRuntime::create(missing, quota, diagnostic) || !diagnostic
        || std::string_view(diagnostic) != "RHI-WEBGL1-CONTEXT-CREATE" || quota->liveBytes()) {
        report(0, "WEBGL-OWNED-COLD-FAILURE-RETIREMENT", 0, 0, 0, 0);
        return 1;
    }
    auto bounded = request;
    bounded.maxCpuMetadataBytes = 1;
    auto exhausted = core::makeShared<core::CpuMemoryQuota>(bounded.maxCpuMetadataBytes);
    if (webgl::WebGlRuntime::create(bounded, exhausted, diagnostic) || !diagnostic
        || std::string_view(diagnostic) != "RHI-WEBGL1-CONTEXT-METADATA-ALLOCATION"
        || exhausted->liveBytes()) {
        report(0, "WEBGL-OWNED-METADATA-FAILURE-RETIREMENT", 0, 0, 0, 0);
        return 1;
    }
    runtime = webgl::WebGlRuntime::create(request, quota, diagnostic);
    if (!runtime) {
        report(0, diagnostic, 0, 0, 0, quota->liveBytes() == 0);
        return 1;
    }
    originalEpoch = runtime->epoch();
    if (webgl::WebGlRuntime::create(request, quota, diagnostic) || !diagnostic
        || std::string_view(diagnostic) != "RHI-WEBGL1-CONTEXT-OWNER-ALREADY-ACTIVE") {
        report(0, "WEBGL-OWNED-DUPLICATE-OWNER", 0, 0, 0, 0);
        return 1;
    }
    const auto originalContext = emscripten_webgl_get_current_context();
    EmscriptenWebGLContextAttributes foreignAttributes{};
    emscripten_webgl_init_context_attributes(&foreignAttributes);
    foreignAttributes.majorVersion = 1;
    foreignAttributes.enableExtensionsByDefault = false;
    const auto foreign = emscripten_webgl_create_context("#other", &foreignAttributes);
    if (!foreign || emscripten_webgl_make_context_current(foreign) != EMSCRIPTEN_RESULT_SUCCESS
        || std::string_view(runtime->currentDiagnostic()) != "RHI-WEBGL1-CONTEXT-FOREIGN-CURRENT") {
        report(0, "WEBGL-OWNED-FOREIGN-CONTEXT", 0, 0, 0, 0);
        return 1;
    }
    emscripten_webgl_destroy_context(foreign);
    if (emscripten_webgl_make_context_current(originalContext) != EMSCRIPTEN_RESULT_SUCCESS
        || runtime->currentDiagnostic() || !runtime->profile().valid()
        || !originalEpoch.isCurrent()) {
        report(0, "WEBGL-OWNED-CONTEXT-READMISSION", 1, 0, 0, 0);
        return 1;
    }
    if (!loseContext()) {
        report(0, "WEBGL-OWNED-LOSS-TEST-EXTENSION", 1, 0, 0, 0);
        return 1;
    }
    deadline = emscripten_get_now() + 10000;
    emscripten_async_call(awaitLoss, nullptr, 0);
}
