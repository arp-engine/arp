#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

#include <string>

namespace arp::platform {

enum class AndroidLifecycleEvent {
    Start,
    Pause,
    Resume,
    Stop,
    SurfaceCreated,
    SurfaceLost,
    OrientationChanged,
    MemoryWarning
};

struct AndroidRuntimeState {
    bool running = false;
    bool paused = false;
    bool surfaceValid = false;
    bool swapchainRecreateRequired = false;
    bool memoryWarningPending = false;
};

enum class AndroidLogSeverity { Debug, Info, Warning, Error };

struct AndroidLogMessage {
    AndroidLogSeverity severity = AndroidLogSeverity::Info;
    std::pmr::string tag;
    std::pmr::string message;
};

struct AndroidAssetPackageDesc {
    std::pmr::string shaderDirectory;
    std::pmr::string reflectionDirectory;
    std::pmr::string textureDirectory;
    std::pmr::string pipelineCacheDirectory;
};

struct AndroidContractResult {
    bool success = false;
    std::pmr::string diagnostics;
};

ARP_API AndroidRuntimeState applyAndroidLifecycleEvent(
    AndroidRuntimeState state, AndroidLifecycleEvent event);

ARP_API AndroidLogMessage makeAndroidLogMessage(AndroidLogSeverity severity,
    const std::pmr::string& subsystem,
    const std::pmr::string& message);

ARP_API AndroidContractResult validateAndroidAssetPackage(const AndroidAssetPackageDesc& desc);

} // namespace arp::platform
