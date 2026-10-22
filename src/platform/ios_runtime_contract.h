#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <string>

namespace arp::platform {

enum class IosLifecycleEvent {
    Active,
    Inactive,
    Background,
    Foreground,
    LayerCreated,
    LayerLost,
    OrientationChanged,
    MemoryWarning
};

struct IosSafeAreaInsets {
    float top = 0.0f;
    float left = 0.0f;
    float bottom = 0.0f;
    float right = 0.0f;
};

struct IosRuntimeState {
    bool active = false;
    bool backgrounded = false;
    bool layerValid = false;
    bool drawableRecreateRequired = false;
    bool frameSubmissionAllowed = false;
    bool memoryWarningPending = false;
    bool layoutReflowRequired = false;
    IosSafeAreaInsets safeArea;
};

enum class IosTouchPhase { Began, Moved, Ended, Cancelled };

struct IosTouchEvent {
    IosTouchPhase phase = IosTouchPhase::Began;
    int pointerId = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct IosAssetBundleDesc {
    std::pmr::string shaderDirectory;
    std::pmr::string reflectionDirectory;
    std::pmr::string textureDirectory;
    std::pmr::string uiDirectory;
};

struct IosContractResult {
    bool success = false;
    std::pmr::string diagnostics;
};

ARP_API IosRuntimeState applyIosLifecycleEvent(IosRuntimeState state, IosLifecycleEvent event);
ARP_API IosRuntimeState applyIosSafeAreaChange(IosRuntimeState state, IosSafeAreaInsets safeArea);
ARP_API IosTouchEvent mapIosTouchEvent(IosTouchPhase phase, int pointerId, float x, float y);
ARP_API IosContractResult validateIosAssetBundle(const IosAssetBundleDesc& desc);

} // namespace arp::platform
