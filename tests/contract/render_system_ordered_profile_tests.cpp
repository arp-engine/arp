#include "platform/render_backend_preference.h"
#include "platform/render_system_ordered_profile.h"
#include "renderer/gui/gui_ordered_unified_contract.h"
#include "rhi/semantics/webgl_context_ownership.h"

#include <cassert>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Ordered platform contracts require assertions
#endif

using namespace arp;

int main()
{
    platform::RenderSystemOrderedProfile p;
    const auto validate = [&] {
        return platform::RenderSystemOrderedProfileContract::validate(rhi::Backend::WebGL, p);
    };
    assert(std::strcmp(validate(), "PLATFORM-ORDERED-PROFILE-VERSION") == 0);
    p.contractVersion = 1;
    assert(std::strcmp(validate(), "PLATFORM-ORDERED-PROFILE-OPT-IN") == 0);
    p.prototypeOptIn = p.execution.prototypeOptIn = p.frames.prototypeOptIn = true;
    assert(std::strcmp(validate(), "PLATFORM-ORDERED-PROFILE-BUDGET") == 0);
    auto& b = p.budgets;
    b.contextMetadataBytes = b.resourceMetadataBytes = b.uploadBytes = 1048576;
    b.logicalBufferBytes = b.logicalTextureBytes = b.deviceMetadataBytes = 1048576;
    b.resourceObjects = 64;
    b.identityFieldBytes = b.surfaceMutations = 64;
    b.pipelines = b.descriptors = {1, "bounded-test", 4};
    p.execution.contractVersion = p.frames.contractVersion = 1;
    p.execution.maxSubmissions = 128;
    p.execution.maxCpuMetadataBytes = 1048576;
    p.execution.requirements = {1, 1, 1280, 1024, 3, 36, 40, 0, 1};
    p.frames.commands = {
        4, true, 516, 262144, 1, {1, true, 540, 128, 262144, 8388608, 4096}, 4, 4, 5, 64};
    p.frames.maxFrameCpuMetadataBytes = 8388608;
    p.frames.frameQuota = core::makeShared<core::CpuMemoryQuota>(8388608);
    p.frames.commandQuota = core::makeShared<core::CpuMemoryQuota>(8388608);
    p.frames.surface = {"#canvas", {1280, 720}, rhi::TextureFormat::Rgba8Unorm, 0};
    assert(!validate());
    p.derivativesOptIn = true;
    assert(std::strcmp(validate(), "PLATFORM-ORDERED-DERIVATIVES-PROFILE-REQUIRED") == 0);
    p.contractVersion = 2;
    p.mainBrowserThreadWithWorkersOptIn = true;
    assert(!validate());
    rhi::semantics::WebGlContextOwnershipRequest context;
    using Context = rhi::semantics::WebGlContextOwnershipContract;
    assert(std::strcmp(Context::executionThreadDiagnostic(context, true, true, true),
               "RHI-WEBGL1-CONTEXT-PTHREAD-PRODUCT-UNAVAILABLE")
        == 0);
    context.contractVersion = 2;
    context.mainBrowserThreadWithWorkersOptIn = true;
    assert(!Context::executionThreadDiagnostic(context, true, true, true));
    assert(Context::executionThreadDiagnostic(context, true, false, true));
    assert(Context::executionThreadDiagnostic(context, true, true, false));
    assert(!Context::executionThreadDiagnostic(context, false, true, true));
    rhi::ScissorState scissor{7, 9, 2, 3};
    assert(
        !renderer::GuiOrderedUnifiedContract::clip({.25f, 1.5f, 10.5f, 4.25f}, {64, 32}, scissor));
    assert(scissor.x == 0 && scissor.y == 1 && scissor.width == 11 && scissor.height == 5);
    assert(renderer::GuiOrderedUnifiedContract::clip({-.25f, 0, 1, 1}, {64, 32}, scissor));
    assert(scissor.x == 0 && scissor.width == 11);
    assert(renderer::GuiOrderedUnifiedContract::clip({0, 0, 65, 1}, {64, 32}, scissor));
    assert(renderer::GuiOrderedUnifiedContract::clip({0, 0, 0, 1}, {64, 32}, scissor));
    assert(renderer::GuiOrderedUnifiedContract::clip(
        {std::numeric_limits<float>::quiet_NaN(), 0, 1, 1}, {64, 32}, scissor));
    assert(renderer::GuiOrderedUnifiedContract::clip(
        {0, 0, std::numeric_limits<float>::infinity(), 1}, {64, 32}, scissor));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    for (auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto* id = platform::RenderSystemOrderedProfileContract::validate(backend, p);
        assert(id);
        assert(std::strcmp(id,
                   backend == rhi::Backend::Null ? "PLATFORM-ORDERED-PROFILE-VALIDATOR-ONLY"
                                                 : "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE")
            == 0);
        assert(!platform::RenderSystemBackendSelection::forBuild(
            platform::BackendPreference::WebGL, backend, true)
                .routeToFactory);
    }
    assert(!platform::RenderSystemBackendSelection::forBuild(
        platform::BackendPreference::WebGL, rhi::Backend::WebGL)
            .routeToFactory);
    assert(platform::RenderSystemBackendSelection::forBuild(
        platform::BackendPreference::WebGL, rhi::Backend::WebGL, true)
            .routeToFactory);
    p.execution.requirements.gpuCompletion = true;
    assert(std::strcmp(validate(), "RHI-ORDERED-GRAPHICS-WORKLOAD-UNSUPPORTED") == 0);
    p.execution.requirements.gpuCompletion = false;
    p.frames.surface.imageCount = 2;
    assert(std::strcmp(validate(), "PLATFORM-ORDERED-PROFILE-SURFACE") == 0);
    p.frames.surface.imageCount = 0;
    p.frames.frameQuota = p.frames.commandQuota;
    assert(std::strcmp(validate(), "PLATFORM-ORDERED-CPU-QUOTA") == 0);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}
