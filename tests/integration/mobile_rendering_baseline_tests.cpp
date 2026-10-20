#include "core/cpu_allocator.h"
#include "renderer/mobile_quality.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::renderer;

    const auto low = qualityProfileDesc(QualityProfile::Low);
    assert(low.resolutionScale == 0.7f);
    assert(low.shadowResolution <= 1024);
    assert(low.maxLights <= 16);
    assert(!low.depthPrepassEnabled);
    assert(!low.expensivePostProcessEnabled);

    const auto high = qualityProfileDesc(QualityProfile::High);
    assert(high.resolutionScale >= low.resolutionScale);
    assert(high.shadowResolution > low.shadowResolution);
    assert(high.maxLights > low.maxLights);

    const auto desktop = qualityProfileDesc(QualityProfile::Desktop);
    assert(desktop.profile == QualityProfile::Desktop);
    assert(desktop.dynamicResolutionAllowed);
    assert(desktop.maxRenderTargets >= high.maxRenderTargets);

    const auto lowShape = buildMobileFrameShape(low);
    assert(lowShape.passes.front() == std::pmr::string("ShadowBudgeted"));
    assert(!containsFramePass(lowShape, "DepthPrepass"));
    assert(!containsFramePass(lowShape, "HeavyPostProcess"));
    assert(containsFramePass(lowShape, "Gui"));

    const auto highShape = buildMobileFrameShape(high);
    assert(containsFramePass(highShape, "DepthPrepass"));
    assert(containsFramePass(highShape, "CharacterMaterial"));
    assert(containsFramePass(highShape, "LightweightPostProcess"));

    const auto scaled = applyDynamicResolution(low, 25.0f, 33.3f);
    assert(scaled.resolutionScale == low.resolutionScale);
    const auto reduced = applyDynamicResolution(low, 45.0f, 33.3f);
    assert(reduced.resolutionScale < low.resolutionScale);

    return 0;
}
