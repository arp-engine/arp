#include "core/memory/cpu_allocator.h"
#include "platform/android_runtime_contract.h"
#include "renderer/global_illumination.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::platform;

    auto state = AndroidRuntimeState{};
    auto started = applyAndroidLifecycleEvent(state, AndroidLifecycleEvent::Start);
    assert(started.running);
    assert(!started.paused);
    assert(!started.surfaceValid);

    auto surfaceReady = applyAndroidLifecycleEvent(started, AndroidLifecycleEvent::SurfaceCreated);
    assert(surfaceReady.surfaceValid);
    assert(surfaceReady.swapchainRecreateRequired);

    auto paused = applyAndroidLifecycleEvent(surfaceReady, AndroidLifecycleEvent::Pause);
    assert(paused.running);
    assert(paused.paused);
    assert(paused.surfaceValid);

    auto lost = applyAndroidLifecycleEvent(paused, AndroidLifecycleEvent::SurfaceLost);
    assert(!lost.surfaceValid);
    assert(lost.swapchainRecreateRequired);

    auto resumed = applyAndroidLifecycleEvent(lost, AndroidLifecycleEvent::Resume);
    assert(resumed.running);
    assert(!resumed.paused);
    assert(!resumed.surfaceValid);

    auto recreated = applyAndroidLifecycleEvent(resumed, AndroidLifecycleEvent::SurfaceCreated);
    assert(recreated.surfaceValid);
    assert(recreated.swapchainRecreateRequired);

    const auto giDesc =
        arp::renderer::globalIlluminationDescForProfile(arp::renderer::QualityProfile::Medium);
    const auto activeGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            recreated.running && !recreated.paused && recreated.surfaceValid,
            recreated.surfaceValid,
            4u * 1024u * 1024u,
            recreated.memoryWarningPending,
        });
    assert(activeGi.giEnabled);
    assert(activeGi.historyUpdateAllowed);
    assert(activeGi.preserveHistory);
    assert(!activeGi.releaseHistory);
    assert(activeGi.estimatedProbeStateBytes > 0);

    const auto pausedGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            paused.running && !paused.paused && paused.surfaceValid,
            paused.surfaceValid,
            4u * 1024u * 1024u,
            paused.memoryWarningPending,
        });
    assert(!pausedGi.giEnabled);
    assert(!pausedGi.historyUpdateAllowed);
    assert(pausedGi.preserveHistory);
    assert(!pausedGi.releaseHistory);

    const auto surfaceLostGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            lost.running && !lost.paused && lost.surfaceValid,
            lost.surfaceValid,
            4u * 1024u * 1024u,
            lost.memoryWarningPending,
        });
    assert(!surfaceLostGi.giEnabled);
    assert(!surfaceLostGi.historyUpdateAllowed);
    assert(surfaceLostGi.preserveHistory);

    const auto memoryWarning =
        applyAndroidLifecycleEvent(recreated, AndroidLifecycleEvent::MemoryWarning);
    const auto pressuredGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            memoryWarning.running && !memoryWarning.paused && memoryWarning.surfaceValid,
            memoryWarning.surfaceValid,
            64u * 1024u,
            memoryWarning.memoryWarningPending,
        });
    assert(!pressuredGi.giEnabled);
    assert(!pressuredGi.historyUpdateAllowed);
    assert(!pressuredGi.preserveHistory);
    assert(pressuredGi.releaseHistory);
    assert(pressuredGi.fallback == arp::renderer::GiFallbackMode::AmbientCubeAndOcclusion);
    assert(pressuredGi.runtimeStatus == arp::renderer::GiRuntimeStatus::DegradedMemory);
    assert(pressuredGi.diagnosticId == std::pmr::string("GI-MEMORY-PRESSURE"));

    const auto metalDegradedGi = arp::renderer::planGiMobileRuntime(giDesc,
        arp::renderer::GiMobileRuntimeContext{
            true, true, 4u * 1024u * 1024u, false, arp::renderer::DdgiCapabilityMode::Degraded});
    assert(!metalDegradedGi.giEnabled);
    assert(metalDegradedGi.runtimeStatus == arp::renderer::GiRuntimeStatus::DegradedCapability);
    assert(metalDegradedGi.diagnosticId == std::pmr::string("GI-CAPABILITY-DEGRADED"));

    auto rotated = applyAndroidLifecycleEvent(recreated, AndroidLifecycleEvent::OrientationChanged);
    assert(rotated.surfaceValid);
    assert(rotated.swapchainRecreateRequired);

    auto stopped = applyAndroidLifecycleEvent(rotated, AndroidLifecycleEvent::Stop);
    assert(!stopped.running);
    assert(!stopped.surfaceValid);

    const auto log =
        makeAndroidLogMessage(AndroidLogSeverity::Info, "renderer", "Vulkan device ready");
    assert(log.tag == std::pmr::string("ARP.renderer"));
    assert(log.message == std::pmr::string("Vulkan device ready"));
    assert(log.severity == AndroidLogSeverity::Info);

    const auto validAssets = validateAndroidAssetPackage(AndroidAssetPackageDesc{
        "assets/shaders",
        "assets/reflection",
        "assets/textures",
        "assets/pipeline_cache",
    });
    assert(validAssets.success);
    assert(validAssets.diagnostics.empty());

    const auto invalidAssets = validateAndroidAssetPackage(AndroidAssetPackageDesc{
        "assets/shaders",
        "",
        "assets/textures",
        "",
    });
    assert(!invalidAssets.success);
    assert(invalidAssets.diagnostics.find("reflection") != std::pmr::string::npos);
    assert(invalidAssets.diagnostics.find("pipeline_cache") != std::pmr::string::npos);

    return 0;
}
