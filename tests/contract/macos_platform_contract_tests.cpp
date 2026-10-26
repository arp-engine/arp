#include "platform/render_system.h"
#include "platform/window.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    const bool stagingPolicy = argc == 2 && !std::strcmp(argv[1], "--staging-policy");
    assert(argc == 1 || stagingPolicy);
#if defined(__APPLE__)
    using namespace arp::platform;
    using namespace arp::rhi;

    assert(BackendPreference::Metal != BackendPreference::Vulkan);
    WindowDesc desc;
    desc.title = "arp-macos-platform-contract";
    desc.width = 64;
    desc.height = 64;
    desc.hidden = true;
    desc.gpuSurface = true; // The Apple window source attaches a CAMetalLayer for GPU surfaces.

    WindowSystem platform;
    const auto initialized = platform.initialize();
    if (!initialized.success) {
        return stagingPolicy ? 77 : 0; // Headless CI may not expose a video driver.
    }
    auto window = platform.createWindow(desc);
    if (!window.valid()) {
        return stagingPolicy ? 77 : 0;
    }
    const auto token = window.surfaceToken();
    assert(token != nullptr);

    SwapchainDesc swapchain;
    swapchain.surfaceName = "macos-contract";
    swapchain.extent = {64u, 64u};
    swapchain.format = TextureFormat::Bgra8Unorm;
    swapchain.platformSurface = token;
    assert(swapchain.platformSurface == token);
    if (stagingPolicy) {
        constexpr semantics::UploadStagingBudgetV1 workload{
            1u, 1024u * 1024u, 1024u * 1024u, 3u, 3u * 1024u * 1024u, 256u};
        for (unsigned mutation = 0u; mutation < 6u; ++mutation) {
            auto candidate = workload;
            if (mutation == 0u)
                candidate.maxRequestBytes /= 2u; // Not represented by the page limit.
            if (mutation == 1u)
                candidate.maxRequestBytes = candidate.pageSizeBytes = 16u * 1024u * 1024u + 1u;
            if (mutation == 2u)
                candidate.maxPageCount = 17u;
            if (mutation == 3u)
                candidate.alignmentBytes /= 2u;
            if (mutation == 4u)
                candidate.alignmentBytes *= 2u;
            candidate.totalCapacityBytes = candidate.pageSizeBytes * candidate.maxPageCount;
            if (mutation == 5u)
                --candidate.totalCapacityBytes;
            auto rejected = createDefaultRenderSystem(platform,
                window,
                "MetalStagingReductionRejected",
                PresentationMode::Auto,
                &candidate);
            assert(!rejected.valid() && !rejected.gpuDevice() && !rejected.diagnostics().empty());
            if (mutation < 5u)
                assert(rejected.diagnostics() == "RHI-UPLOAD-STAGING-BUDGET-OVERRIDE-OUT-OF-SCOPE");
        }
        auto accepted = createDefaultRenderSystem(
            platform, window, "MetalStagingReductionAccepted", PresentationMode::Auto, &workload);
        if (!accepted.valid())
            std::fprintf(stderr, "%s\n", accepted.diagnostics().c_str());
        assert(accepted.valid() && accepted.gpuDevice());
        const auto ledger = accepted.gpuDevice()->physicalBudgetLedger();
        const auto& staging = ledger.category(semantics::PhysicalBudgetCategory::UploadStaging);
        assert(staging.observed() && staging.capacity == workload.totalCapacityBytes
            && staging.current <= staging.peak && staging.peak <= staging.capacity);
        std::printf(
            "METAL_PLATFORM_STAGING_REDUCTION_PASS rejected=6 capacity=%llu current=%llu peak=%llu\n",
            static_cast<unsigned long long>(staging.capacity),
            static_cast<unsigned long long>(staging.current),
            static_cast<unsigned long long>(staging.peak));
    }
#else
    if (stagingPolicy)
        return 77;
#endif
    return 0;
}
