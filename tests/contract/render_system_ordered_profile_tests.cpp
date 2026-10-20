#include "platform/render_backend_preference.h"
#include "platform/render_system_ordered_profile.h"

#include <cassert>
#include <cstring>

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
