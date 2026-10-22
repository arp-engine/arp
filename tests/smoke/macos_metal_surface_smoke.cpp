#include "core/memory/cpu_allocator.h"
#include "platform/render_system.h"
#include "platform/window.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {

struct Report {
    bool initialized = false;
    bool lowDensityWindow = false;
    bool highDensityWindow = false;
    bool renderSystemCreated = false;
    bool backendMetal = false;
    bool surfaceBacked = false;
    bool baseFrames = false;
    bool resize = false;
    bool minimizeRestore = false;
    bool focus = false;
    bool surfaceLoss = false;
    bool destroyRecreate = false;
    bool presentReuseRejected = false;
    bool explicitVulkanRejected = false;
    bool explicitWebGpuRejected = false;
    bool emptySurfaceRejected = false;
    bool missingSurfaceRejected = false;
    bool retina1xObserved = false;
    bool retina2xObserved = false;
    std::uint32_t acquireCount = 0;
    std::uint32_t presentCount = 0;
    std::uint32_t notReadyCount = 0;
    std::uint32_t resizeCount = 0;
    std::uint32_t focusLostEvents = 0;
    std::uint32_t focusGainedEvents = 0;
    int lowDensityPixelWidth = 0;
    int lowDensityPixelHeight = 0;
    int highDensityPixelWidth = 0;
    int highDensityPixelHeight = 0;
    float lowDensityScale = 0.0f;
    float highDensityScale = 0.0f;
    std::pmr::string diagnosticId;
};

void setFailure(Report& report, const char* diagnostic)
{
    if (report.diagnosticId.empty()) {
        report.diagnosticId = diagnostic;
    }
}

void drainEvents(Report& report,
    arp::platform::WindowSystem& platform,
    const arp::platform::PlatformWindow& window)
{
    arp::platform::WindowEventBatch batch;
    platform.pumpEvents(batch);
    for (const auto& event : batch.windowEvents) {
        if (event.windowId != window.id())
            continue;
        if (event.type == arp::platform::WindowEventType::FocusLost) {
            ++report.focusLostEvents;
        } else if (event.type == arp::platform::WindowEventType::FocusGained) {
            ++report.focusGainedEvents;
        }
    }
}

bool runFrames(arp::platform::RenderSystem& renderSystem,
    arp::platform::WindowSystem& platform,
    const arp::platform::PlatformWindow& window,
    Report& report,
    const std::uint32_t frameCount)
{
    constexpr std::uint32_t kMaxNotReadyEventWaits = 120u;
    std::uint32_t completedFrames = 0u;
    std::uint32_t notReadyEventWaits = 0u;
    while (completedFrames < frameCount) {
        drainEvents(report, platform, window);
        const auto frame = renderSystem.acquireFrame();
        if (!frame.success) {
            if (frame.status == arp::rhi::AcquireStatus::NotReady
                && notReadyEventWaits < kMaxNotReadyEventWaits) {
                ++report.notReadyCount;
                ++notReadyEventWaits;
                arp::platform::WindowEventBatch batch;
                platform.waitForEvents(16, batch);
                continue;
            }
            std::cerr << "surface acquire failed: status=" << static_cast<int>(frame.status)
                      << " diagnostic=" << frame.diagnostics.c_str() << '\n';
            setFailure(report, "RHI-METAL-SURFACE-ACQUIRE-FAILED");
            return false;
        }
        ++report.acquireCount;
        const auto presented = renderSystem.presentFrame(frame);
        if (!presented.success) {
            setFailure(report, "RHI-METAL-SURFACE-PRESENT-FAILED");
            return false;
        }
        ++report.presentCount;
        ++completedFrames;
    }
    drainEvents(report, platform, window);
    return true;
}

bool runWindowScenario(
    arp::platform::WindowSystem& platform, const bool highDensity, Report& report)
{
    auto window = platform.createWindow({
        highDensity ? "arp-macos-metal-surface-2x" : "arp-macos-metal-surface-1x",
        128,
        72,
        false,
        true,
        false,
        highDensity,
    });
    if (!window.valid()) {
        setFailure(report, "RHI-METAL-WINDOW-CREATE-FAILED");
        return false;
    }
    // CTest launches the bundle without Finder activation. Explicitly flush the
    // newly-created Cocoa window before its CAMetalLayer is asked for a drawable.
    window.show();
    window.raise();
    window.sync();
    drainEvents(report, platform, window);
    const auto pixelWidth = window.pixelWidth();
    const auto pixelHeight = window.pixelHeight();
    const auto scale = window.pixelDensity();
    if (highDensity) {
        report.highDensityWindow = true;
        report.highDensityPixelWidth = pixelWidth;
        report.highDensityPixelHeight = pixelHeight;
        report.highDensityScale = scale;
        report.retina2xObserved = scale >= 1.8f;
    } else {
        report.lowDensityWindow = true;
        report.lowDensityPixelWidth = pixelWidth;
        report.lowDensityPixelHeight = pixelHeight;
        report.lowDensityScale = scale;
        report.retina1xObserved = scale <= 1.2f;
    }

    auto renderSystem =
        arp::platform::createDefaultRenderSystem(platform, window, "arp-macos-metal-surface-smoke");
    if (!renderSystem.valid()) {
        setFailure(report, "RHI-METAL-RENDER-SYSTEM-CREATE-FAILED");
        return false;
    }
    report.renderSystemCreated = true;
    report.backendMetal = renderSystem.backend() == arp::rhi::Backend::Metal;
    report.surfaceBacked = renderSystem.capabilities().surfaceBacked;
    if (!report.backendMetal || !report.surfaceBacked) {
        setFailure(report, "RHI-METAL-SURFACE-BACKEND-MISMATCH");
        return false;
    }

    if (!runFrames(renderSystem, platform, window, report, 120u)) {
        return false;
    }
    report.baseFrames = true;

    for (const arp::rhi::Extent2D extent : {
             arp::rhi::Extent2D{640u, 360u},
             arp::rhi::Extent2D{1u, 1u},
             arp::rhi::Extent2D{128u, 72u},
         }) {
        window.resize(static_cast<int>(extent.width), static_cast<int>(extent.height));
        const auto resized = renderSystem.resize({
            static_cast<std::uint32_t>(window.pixelWidth()),
            static_cast<std::uint32_t>(window.pixelHeight()),
        });
        if (!resized.success || renderSystem.extent().width == 0u
            || renderSystem.extent().height == 0u) {
            setFailure(report, "RHI-METAL-RESIZE-FAILED");
            return false;
        }
        ++report.resizeCount;
        if (!runFrames(renderSystem, platform, window, report, 8u)) {
            return false;
        }
    }
    report.resize = report.resizeCount >= 3u;

    window.minimize();
    window.sync();
    drainEvents(report, platform, window);
    const auto paused = renderSystem.swapchain()->setSurfaceAvailable(false);
    const auto pausedFrame = renderSystem.acquireFrame();
    window.restore();
    window.show();
    window.raise();
    window.sync();
    drainEvents(report, platform, window);
    const auto resumed = renderSystem.swapchain()->setSurfaceAvailable(true);
    const bool pausedCorrectly =
        paused.success && !paused.acquireAllowed && !pausedFrame.success && pausedFrame.surfaceLost;
    const bool resumedCorrectly = resumed.success && resumed.acquireAllowed
        && runFrames(renderSystem, platform, window, report, 8u);
    report.minimizeRestore = pausedCorrectly && resumedCorrectly;
    if (!report.minimizeRestore) {
        setFailure(report, "RHI-METAL-MINIMIZE-RESTORE-FAILED");
        return false;
    }

    window.hide();
    window.sync();
    drainEvents(report, platform, window);
    window.show();
    window.raise();
    window.sync();
    drainEvents(report, platform, window);
    report.focus = report.focusLostEvents > 0u && report.focusGainedEvents > 0u;
    if (!report.focus) {
        setFailure(report, "RHI-METAL-FOCUS-TRANSITION-NOT-OBSERVED");
        return false;
    }

    const auto lost = renderSystem.swapchain()->setSurfaceAvailable(false);
    const auto lostFrame = renderSystem.acquireFrame();
    const auto restored = renderSystem.swapchain()->setSurfaceAvailable(true);
    const bool lossCorrectly = lost.success && lost.releaseSurfaceResources && !lost.acquireAllowed
        && !lostFrame.success && lostFrame.surfaceLost && restored.success
        && restored.acquireAllowed;
    report.surfaceLoss = lossCorrectly;
    if (!lossCorrectly || !runFrames(renderSystem, platform, window, report, 8u)) {
        setFailure(report, "RHI-METAL-SURFACE-LOSS-RECOVERY-FAILED");
        return false;
    }

    const auto reuseFrame = renderSystem.acquireFrame();
    if (!reuseFrame.success) {
        setFailure(report, "RHI-METAL-PRESENT-REUSE-SETUP-FAILED");
        return false;
    }
    const auto firstPresent = renderSystem.presentFrame(reuseFrame);
    const auto secondPresent = renderSystem.presentFrame(reuseFrame);
    report.presentReuseRejected = firstPresent.success && !secondPresent.success;
    if (!report.presentReuseRejected) {
        setFailure(report, "RHI-METAL-PRESENT-REUSE-NOT-REJECTED");
        return false;
    }
    return true;
}

void writeReport(const Report& report, const bool success)
{
    const auto emit = [&](std::ostream& output) {
        output
            << "{\"success\":" << (success ? "true" : "false") << ",\"runtime\":true"
            << ",\"lowDensityScale\":" << report.lowDensityScale
            << ",\"highDensityScale\":" << report.highDensityScale << ",\"lowDensityPixelExtent\":["
            << report.lowDensityPixelWidth << "," << report.lowDensityPixelHeight << "]"
            << ",\"highDensityPixelExtent\":[" << report.highDensityPixelWidth << ","
            << report.highDensityPixelHeight << "]"
            << ",\"acquireCount\":" << report.acquireCount
            << ",\"presentCount\":" << report.presentCount
            << ",\"notReadyCount\":" << report.notReadyCount
            << ",\"resizeCount\":" << report.resizeCount
            << ",\"focusLostEvents\":" << report.focusLostEvents
            << ",\"focusGainedEvents\":" << report.focusGainedEvents
            << ",\"retina1xObserved\":" << (report.retina1xObserved ? "true" : "false")
            << ",\"retina2xObserved\":" << (report.retina2xObserved ? "true" : "false")
            << ",\"baseFrames\":" << (report.baseFrames ? "true" : "false")
            << ",\"resize\":" << (report.resize ? "true" : "false")
            << ",\"minimizeRestore\":" << (report.minimizeRestore ? "true" : "false")
            << ",\"focus\":" << (report.focus ? "true" : "false")
            << ",\"surfaceLoss\":" << (report.surfaceLoss ? "true" : "false")
            << ",\"destroyRecreate\":" << (report.destroyRecreate ? "true" : "false")
            << ",\"presentReuseRejected\":" << (report.presentReuseRejected ? "true" : "false")
            << ",\"explicitVulkanRejected\":" << (report.explicitVulkanRejected ? "true" : "false")
            << ",\"explicitWebGpuRejected\":" << (report.explicitWebGpuRejected ? "true" : "false")
            << ",\"emptySurfaceRejected\":" << (report.emptySurfaceRejected ? "true" : "false")
            << ",\"missingSurfaceRejected\":" << (report.missingSurfaceRejected ? "true" : "false")
            << ",\"diagnosticId\":\"" << report.diagnosticId.c_str() << "\"}\n";
    };
    emit(std::cout);
    if (const char* path = std::getenv("ARP_MACOS_SURFACE_REPORT")) {
        std::ofstream file(path);
        if (file) {
            emit(file);
        }
    }
}

} // namespace

int main()
{
#if !defined(__APPLE__)
    return 0;
#else
    Report report;
    arp::platform::WindowSystem platform;
    const auto initialized = platform.initialize();
    if (!initialized.success) {
        report.diagnosticId = "RHI-METAL-VIDEO-INIT-NOT-RUN";
        writeReport(report, false);
        return 2;
    }
    report.initialized = true;

    const bool lowDensity = runWindowScenario(platform, false, report);
    const bool highDensity = runWindowScenario(platform, true, report);

    // Recreate the complete window/system pair after the first pair has gone
    // out of scope. This checks ownership teardown order independently from
    // the detailed lifecycle sequence above.
    {
        auto window = platform.createWindow(
            {"arp-macos-metal-surface-recreate", 128, 72, false, true, false, true});
        auto renderSystem = window.valid()
            ? arp::platform::createDefaultRenderSystem(
                  platform, window, "arp-macos-metal-surface-recreate")
            : arp::platform::RenderSystem{};
        report.destroyRecreate = window.valid() && renderSystem.valid()
            && runFrames(renderSystem, platform, window, report, 8u);
    }

    auto negativeWindow = platform.createWindow(
        {"arp-macos-metal-surface-negative", 64, 64, true, false, false, false});
    if (negativeWindow.valid()) {
        arp::platform::RenderSystemDesc desc;
        desc.surface.windowSystem = &platform;
        desc.surface.window = &negativeWindow;
        desc.applicationName = "arp-macos-metal-negative";
        desc.backendPreference = arp::platform::BackendPreference::Vulkan;
        const auto vulkan = arp::platform::createRenderSystem(desc);
        report.explicitVulkanRejected = !vulkan.valid() && !vulkan.diagnostics().empty();
        desc.backendPreference = arp::platform::BackendPreference::WebGPU;
        const auto webGpu = arp::platform::createRenderSystem(desc);
        report.explicitWebGpuRejected = !webGpu.valid() && !webGpu.diagnostics().empty();
        desc.backendPreference = arp::platform::BackendPreference::Default;
        const auto emptySurface = arp::platform::createRenderSystem(desc);
        report.emptySurfaceRejected = !emptySurface.valid()
            && emptySurface.diagnostics() == "RHI-METAL-SURFACE-TOKEN-MISSING";
    }
    arp::platform::RenderSystemDesc missingSurface;
    missingSurface.backendPreference = arp::platform::BackendPreference::Default;
    const auto missing = arp::platform::createRenderSystem(missingSurface);
    report.missingSurfaceRejected =
        !missing.valid() && missing.diagnostics() == "RHI-PLATFORM-SURFACE-MISSING";

    const bool lifecycleComplete = lowDensity && highDensity && report.destroyRecreate
        && report.explicitVulkanRejected && report.explicitWebGpuRejected
        && report.emptySurfaceRejected && report.missingSurfaceRejected;
    const bool retinaCoverageComplete = report.retina1xObserved && report.retina2xObserved;
    const bool success = lifecycleComplete && retinaCoverageComplete;
    if (!lifecycleComplete && report.diagnosticId.empty()) {
        report.diagnosticId = "RHI-METAL-SURFACE-LIFECYCLE-INCOMPLETE";
    }
    if (lifecycleComplete && !retinaCoverageComplete) {
        report.diagnosticId = "RHI-METAL-RETINA-DENSITY-NOT-AVAILABLE";
    }
    writeReport(report, success);
    return success ? 0 : lifecycleComplete ? 125 : 1;
#endif
}
