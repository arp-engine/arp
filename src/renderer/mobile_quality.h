#pragma once

#include "core/cpu_allocator.h"
#include "renderer/render_resolution_policy.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::renderer {

enum class QualityProfile { Low, Medium, High, Desktop };

struct QualityProfileDesc {
    QualityProfile profile = QualityProfile::Medium;
    float resolutionScale = 1.0f;
    uint32_t maxRenderTargets = 3;
    uint32_t shadowResolution = 1024;
    uint32_t maxLights = 32;
    uint32_t vegetationDensity = 50;
    bool depthPrepassEnabled = true;
    bool characterPassEnabled = true;
    bool lightweightPostProcessEnabled = true;
    bool expensivePostProcessEnabled = false;
    bool dynamicResolutionAllowed = true;
};

struct MobileFrameShape {
    QualityProfile profile = QualityProfile::Medium;
    std::pmr::vector<std::pmr::string> passes;
};

inline QualityProfileDesc qualityProfileDesc(QualityProfile profile)
{
    switch (profile) {
    case QualityProfile::Low:
        return QualityProfileDesc{profile, 0.7f, 2, 512, 12, 25, false, true, true, false, true};
    case QualityProfile::Medium:
        return QualityProfileDesc{profile, 0.85f, 3, 1024, 24, 50, true, true, true, false, true};
    case QualityProfile::High:
        return QualityProfileDesc{profile, 1.0f, 4, 2048, 48, 75, true, true, true, false, true};
    case QualityProfile::Desktop:
        return QualityProfileDesc{profile, 1.0f, 6, 4096, 128, 100, true, true, true, true, true};
    }
    return QualityProfileDesc{};
}

inline MobileFrameShape buildMobileFrameShape(const QualityProfileDesc& profile)
{
    MobileFrameShape shape;
    shape.profile = profile.profile;

    if (profile.depthPrepassEnabled) {
        shape.passes.push_back("DepthPrepass");
    }
    shape.passes.push_back("ShadowBudgeted");
    shape.passes.push_back("OpaqueScene");
    if (profile.characterPassEnabled) {
        shape.passes.push_back("CharacterMaterial");
    }
    shape.passes.push_back("TransparentVfxLimited");
    if (profile.lightweightPostProcessEnabled) {
        shape.passes.push_back("LightweightPostProcess");
    }
    if (profile.expensivePostProcessEnabled) {
        shape.passes.push_back("HeavyPostProcess");
    }
    shape.passes.push_back("Gui");
    return shape;
}

inline bool containsFramePass(const MobileFrameShape& shape, const std::pmr::string& passName)
{
    return std::find(shape.passes.begin(), shape.passes.end(), passName) != shape.passes.end();
}

inline QualityProfileDesc applyDynamicResolution(
    QualityProfileDesc profile, float frameTimeMs, float targetFrameTimeMs)
{
    if (!profile.dynamicResolutionAllowed || frameTimeMs <= targetFrameTimeMs) {
        return profile;
    }

    RenderResolutionPolicy policy;
    policy.dynamic = true;
    policy.minimum = 0.5f;
    policy.targetGpuMs = targetFrameTimeMs;
    policy.hysteresis = 0.0f;
    policy.step = profile.resolutionScale * 0.1f;
    profile.resolutionScale = policy.resolve(profile.resolutionScale, frameTimeMs);
    return profile;
}

} // namespace arp::renderer
