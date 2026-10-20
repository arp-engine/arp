#include "platform/android_runtime_contract.h"

#include "core/cpu_allocator.h"

#include <vector>

namespace arp::platform {

AndroidRuntimeState applyAndroidLifecycleEvent(
    AndroidRuntimeState state, AndroidLifecycleEvent event)
{
    state.swapchainRecreateRequired = false;

    switch (event) {
    case AndroidLifecycleEvent::Start:
        state.running = true;
        state.paused = false;
        state.memoryWarningPending = false;
        break;
    case AndroidLifecycleEvent::Pause:
        state.paused = true;
        break;
    case AndroidLifecycleEvent::Resume:
        state.running = true;
        state.paused = false;
        break;
    case AndroidLifecycleEvent::Stop:
        state.running = false;
        state.paused = false;
        state.surfaceValid = false;
        state.swapchainRecreateRequired = true;
        break;
    case AndroidLifecycleEvent::SurfaceCreated:
        state.surfaceValid = true;
        state.swapchainRecreateRequired = true;
        break;
    case AndroidLifecycleEvent::SurfaceLost:
        state.surfaceValid = false;
        state.swapchainRecreateRequired = true;
        break;
    case AndroidLifecycleEvent::OrientationChanged:
        state.swapchainRecreateRequired = state.surfaceValid;
        break;
    case AndroidLifecycleEvent::MemoryWarning:
        state.memoryWarningPending = true;
        break;
    }

    return state;
}

AndroidLogMessage makeAndroidLogMessage(
    AndroidLogSeverity severity, const std::pmr::string& subsystem, const std::pmr::string& message)
{
    return AndroidLogMessage{
        severity,
        "ARP." + subsystem,
        message,
    };
}

AndroidContractResult validateAndroidAssetPackage(const AndroidAssetPackageDesc& desc)
{
    std::pmr::vector<std::pmr::string> missing;
    if (desc.shaderDirectory.empty()) {
        missing.push_back("shaders");
    }
    if (desc.reflectionDirectory.empty()) {
        missing.push_back("reflection");
    }
    if (desc.textureDirectory.empty()) {
        missing.push_back("textures");
    }
    if (desc.pipelineCacheDirectory.empty()) {
        missing.push_back("pipeline_cache");
    }

    if (missing.empty()) {
        return AndroidContractResult{true, ""};
    }

    std::pmr::string diagnostics = "missing Android asset package entries:";
    for (const auto& entry : missing) {
        diagnostics += " ";
        diagnostics += entry;
    }
    return AndroidContractResult{false, diagnostics};
}

} // namespace arp::platform
