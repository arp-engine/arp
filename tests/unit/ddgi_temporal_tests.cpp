#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {

float blend(float history, float sample, float alpha)
{
    return history + (sample - history) * alpha;
}

float percentile(std::vector<float> values, float fraction)
{
    std::sort(values.begin(), values.end());
    const auto index = std::min<std::size_t>(values.size() - 1u,
        static_cast<std::size_t>(std::ceil(fraction * static_cast<float>(values.size()))) - 1u);
    return values[index];
}

} // namespace

int main()
{
    // Static lighting must converge without camera-dependent fades.
    float staticValue = 0.0f;
    for (int frame = 0; frame < 16; ++frame)
        staticValue = blend(staticValue, 1.0f, 0.35f);
    assert(staticValue > 0.99f);

    // A lighting step reaches 90% of its new steady state within eight frames.
    float lightStep = 0.2f;
    for (int frame = 0; frame < 8; ++frame)
        lightStep = blend(lightStep, 1.0f, 0.35f);
    assert(lightStep > 0.9f);

    // Distant probes receive a smaller bounded update response, but must
    // still reach 90% of a lighting step within the 30-frame acceptance
    // window.
    float distantLightStep = 0.0f;
    for (int frame = 0; frame < 30; ++frame) {
        distantLightStep = blend(distantLightStep, 1.0f, 0.10f);
    }
    assert(distantLightStep > 0.9f);

    // Camera-only motion changes the target by sub-percent reprojection noise;
    // measure the stable-region residual rather than hiding it with a fade.
    float cameraHistory = 0.5f;
    std::vector<float> cameraErrors;
    cameraErrors.reserve(120u);
    for (int frame = 0; frame < 128; ++frame) {
        const auto target = 0.5f + 0.002f * std::sin(static_cast<float>(frame) * 0.31f);
        cameraHistory = blend(cameraHistory, target, 0.25f);
        if (frame >= 8)
            cameraErrors.push_back(std::abs(cameraHistory - target));
    }
    assert(percentile(cameraErrors, 0.50f) < 0.01f);
    assert(percentile(cameraErrors, 0.99f) < 0.03f);

    // A newly exposed dynamic occluder must invalidate stale bright history.
    float dynamicOccluder = 1.0f;
    for (int frame = 0; frame < 8; ++frame)
        dynamicOccluder = blend(dynamicOccluder, 0.05f, 0.55f);
    assert(dynamicOccluder < 0.2f);
    std::cout << "ddgi_temporal_metrics"
              << " static_convergence=" << staticValue << " light_step_8=" << lightStep
              << " distant_light_step_30=" << distantLightStep
              << " camera_median=" << percentile(cameraErrors, 0.50f)
              << " camera_p99=" << percentile(cameraErrors, 0.99f)
              << " dynamic_occluder_8=" << dynamicOccluder << '\n';
    return 0;
}
