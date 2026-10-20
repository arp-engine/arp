#include "core/cpu_allocator.h"
#include "platform/ios_runtime_contract.h"
#include "renderer/global_illumination.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::platform;

    auto state = IosRuntimeState{};
    auto active = applyIosLifecycleEvent(state, IosLifecycleEvent::Active);
    assert(active.active);
    assert(!active.backgrounded);

    auto surfaced = applyIosLifecycleEvent(active, IosLifecycleEvent::LayerCreated);
    assert(surfaced.layerValid);
    assert(surfaced.drawableRecreateRequired);

    const auto giDesc =
        arp::renderer::globalIlluminationDescForProfile(arp::renderer::QualityProfile::High);
    const auto activeGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            surfaced.frameSubmissionAllowed,
            surfaced.layerValid,
            8u * 1024u * 1024u,
            surfaced.memoryWarningPending,
        });
    assert(activeGi.giEnabled);
    assert(activeGi.historyUpdateAllowed);
    assert(activeGi.preserveHistory);
    assert(!activeGi.releaseHistory);

    auto inactive = applyIosLifecycleEvent(surfaced, IosLifecycleEvent::Inactive);
    assert(!inactive.active);
    assert(!inactive.backgrounded);
    assert(inactive.layerValid);

    const auto inactiveGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            inactive.frameSubmissionAllowed,
            inactive.layerValid,
            8u * 1024u * 1024u,
            inactive.memoryWarningPending,
        });
    assert(!inactiveGi.giEnabled);
    assert(!inactiveGi.historyUpdateAllowed);
    assert(inactiveGi.preserveHistory);
    assert(!inactiveGi.releaseHistory);

    auto backgrounded = applyIosLifecycleEvent(inactive, IosLifecycleEvent::Background);
    assert(!backgrounded.active);
    assert(backgrounded.backgrounded);
    assert(backgrounded.frameSubmissionAllowed == false);

    const auto backgroundGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            backgrounded.frameSubmissionAllowed,
            backgrounded.layerValid,
            8u * 1024u * 1024u,
            backgrounded.memoryWarningPending,
        });
    assert(!backgroundGi.giEnabled);
    assert(!backgroundGi.historyUpdateAllowed);
    assert(backgroundGi.preserveHistory);

    auto warned = applyIosLifecycleEvent(backgrounded, IosLifecycleEvent::MemoryWarning);
    assert(warned.memoryWarningPending);

    const auto pressuredGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            warned.frameSubmissionAllowed,
            warned.layerValid,
            96u * 1024u,
            warned.memoryWarningPending,
        });
    assert(!pressuredGi.giEnabled);
    assert(!pressuredGi.historyUpdateAllowed);
    assert(!pressuredGi.preserveHistory);
    assert(pressuredGi.releaseHistory);
    assert(pressuredGi.runtimeStatus == arp::renderer::GiRuntimeStatus::DegradedMemory);
    assert(pressuredGi.diagnosticId == std::pmr::string("GI-MEMORY-PRESSURE"));

    const auto unsupportedGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{true,
            true,
            8u * 1024u * 1024u,
            false,
            arp::renderer::DdgiCapabilityMode::DiagnosticFailure});
    assert(!unsupportedGi.giEnabled);
    assert(unsupportedGi.runtimeStatus == arp::renderer::GiRuntimeStatus::DiagnosticFailure);
    assert(unsupportedGi.diagnosticId == std::pmr::string("GI-CAPABILITY-DIAGNOSTIC-FAILURE"));

    auto foreground = applyIosLifecycleEvent(warned, IosLifecycleEvent::Foreground);
    assert(!foreground.backgrounded);
    assert(!foreground.active);

    auto safeArea = applyIosSafeAreaChange(foreground, IosSafeAreaInsets{20.0f, 0.0f, 34.0f, 0.0f});
    assert(safeArea.safeArea.top == 20.0f);
    assert(safeArea.safeArea.bottom == 34.0f);
    assert(safeArea.layoutReflowRequired);

    const auto touch = mapIosTouchEvent(IosTouchPhase::Moved, 2, 120.0f, 240.0f);
    assert(touch.pointerId == 2);
    assert(touch.x == 120.0f);
    assert(touch.y == 240.0f);
    assert(touch.phase == IosTouchPhase::Moved);

    const auto validBundle = validateIosAssetBundle(IosAssetBundleDesc{
        "App.bundle/shaders",
        "App.bundle/reflection",
        "App.bundle/textures",
        "App.bundle/ui",
    });
    assert(validBundle.success);

    const auto invalidBundle = validateIosAssetBundle(IosAssetBundleDesc{
        "",
        "App.bundle/reflection",
        "",
        "App.bundle/ui",
    });
    assert(!invalidBundle.success);
    assert(invalidBundle.diagnostics.find("shaders") != std::pmr::string::npos);
    assert(invalidBundle.diagnostics.find("textures") != std::pmr::string::npos);

    return 0;
}
