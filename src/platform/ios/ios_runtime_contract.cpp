#include "platform/ios_runtime_contract.h"

#include "core/memory/cpu_allocator.h"

#include <vector>

namespace arp::platform {

namespace {

void refreshFrameSubmissionAllowed(IosRuntimeState& state)
{
    state.frameSubmissionAllowed = state.active && !state.backgrounded && state.layerValid;
}

} // namespace

IosRuntimeState applyIosLifecycleEvent(IosRuntimeState state, IosLifecycleEvent event)
{
    state.drawableRecreateRequired = false;
    state.layoutReflowRequired = false;

    switch (event) {
    case IosLifecycleEvent::Active:
        state.active = true;
        state.backgrounded = false;
        break;
    case IosLifecycleEvent::Inactive:
        state.active = false;
        break;
    case IosLifecycleEvent::Background:
        state.active = false;
        state.backgrounded = true;
        break;
    case IosLifecycleEvent::Foreground:
        state.backgrounded = false;
        break;
    case IosLifecycleEvent::LayerCreated:
        state.layerValid = true;
        state.drawableRecreateRequired = true;
        break;
    case IosLifecycleEvent::LayerLost:
        state.layerValid = false;
        state.drawableRecreateRequired = true;
        break;
    case IosLifecycleEvent::OrientationChanged:
        state.drawableRecreateRequired = state.layerValid;
        state.layoutReflowRequired = true;
        break;
    case IosLifecycleEvent::MemoryWarning:
        state.memoryWarningPending = true;
        break;
    }

    refreshFrameSubmissionAllowed(state);
    return state;
}

IosRuntimeState applyIosSafeAreaChange(IosRuntimeState state, IosSafeAreaInsets safeArea)
{
    state.safeArea = safeArea;
    state.layoutReflowRequired = true;
    refreshFrameSubmissionAllowed(state);
    return state;
}

IosTouchEvent mapIosTouchEvent(IosTouchPhase phase, int pointerId, float x, float y)
{
    return IosTouchEvent{
        phase,
        pointerId,
        x,
        y,
    };
}

IosContractResult validateIosAssetBundle(const IosAssetBundleDesc& desc)
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
    if (desc.uiDirectory.empty()) {
        missing.push_back("ui");
    }

    if (missing.empty()) {
        return IosContractResult{true, ""};
    }

    std::pmr::string diagnostics = "missing iOS asset bundle entries:";
    for (const auto& entry : missing) {
        diagnostics += " ";
        diagnostics += entry;
    }
    return IosContractResult{false, diagnostics};
}

} // namespace arp::platform
