#pragma once

/// @file weather_system.h
/// @brief P9: Dynamic weather system.
///
/// The weather system drives all visual atmosphere: sky colors, fog density,
/// lighting intensity, surface wetness, and exposure. Three fixed presets
/// (clear, overcast, rain) provide the verification captures required by the
/// P9 plan. Transitions between presets are smoothly interpolated so the
/// scene never pops.
///
/// The GPU receives a compact UBO (WoccWeatherUbo) each frame, consumed by
/// the PBR, terrain, sky, and post-FX shaders. The UBO mirrors the Slang
/// struct `WoccWeather` in pbr_scene.slang.
///
/// Weather presets:
///   0 = Clear     (bright sun, low fog, no wetness)
///   1 = Overcast  (dim sun, grey sky, moderate fog, light wetness)
///   2 = Rain      (very dim sun, dark sky, heavy fog, high wetness)

#include <cmath>
#include <cstdint>

namespace arp::pcg {

/// GPU weather UBO. Mirrors the Slang struct `WoccWeather`.
/// All fields are float4 for std140 alignment.
/// Total size: 4 × float4 = 64 bytes.
struct WoccWeatherUbo {
    // x = weatherMode (0=clear, 1=overcast, 2=rain),
    // y = wetness [0,1], z = cloudCover [0,1], w = fogDensityScale [1,8]
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // x = sunIntensityScale [0,1], y = ambientScale [0.3,1.5],
    // z = exposureAdjust [0.8,1.5], w = reserved
    float lightAdjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    // rgb = sky color tint multiplier (1=neutral), w = horizon haze intensity
    float skyTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // rgb = fog color (linear HDR), a = fog density override (0 = use default)
    float fogTint[4] = {0.82f, 0.86f, 0.92f, 0.0f};
};

/// CPU-side weather state. Updated each frame and uploaded as a UBO.
class WeatherSystem {
public:
    WeatherSystem() = default;

    /// Set the weather preset (0=clear, 1=overcast, 2=rain).
    /// The actual state smoothly interpolates toward the preset.
    void setPreset(int mode)
    {
        if (mode < 0 || mode > 2)
            return;
        targetPreset_ = mode;
    }

    int currentPreset() const { return currentPreset_; }

    int targetPreset() const { return targetPreset_; }

    /// Advance the weather state by dt seconds. Smoothly interpolates
    /// toward the target preset using exponential smoothing.
    void update(float dt)
    {
        // Snap if first update or large dt.
        if (firstUpdate_ || dt > 1.0f) {
            currentPreset_ = targetPreset_;
            applyPreset(currentPreset_,
                wetness_,
                cloudCover_,
                fogDensityScale_,
                sunIntensityScale_,
                ambientScale_,
                exposureAdjust_,
                skyTintR_,
                skyTintG_,
                skyTintB_,
                hazeIntensity_,
                fogR_,
                fogG_,
                fogB_,
                fogDensityOverride_);
            firstUpdate_ = false;
            return;
        }

        // Compute target values.
        float tWet, tCloud, tFog, tSun, tAmb, tExp, tSkyR, tSkyG, tSkyB, tHaze, tFogR, tFogG, tFogB,
            tFogDensity;
        applyPreset(targetPreset_,
            tWet,
            tCloud,
            tFog,
            tSun,
            tAmb,
            tExp,
            tSkyR,
            tSkyG,
            tSkyB,
            tHaze,
            tFogR,
            tFogG,
            tFogB,
            tFogDensity);

        // Exponential smoothing (half-life ~2s for gentle transitions).
        const float rate = 0.35f;
        const float alpha = 1.0f - std::exp(-rate * dt);
        wetness_ += (tWet - wetness_) * alpha;
        cloudCover_ += (tCloud - cloudCover_) * alpha;
        fogDensityScale_ += (tFog - fogDensityScale_) * alpha;
        sunIntensityScale_ += (tSun - sunIntensityScale_) * alpha;
        ambientScale_ += (tAmb - ambientScale_) * alpha;
        exposureAdjust_ += (tExp - exposureAdjust_) * alpha;
        skyTintR_ += (tSkyR - skyTintR_) * alpha;
        skyTintG_ += (tSkyG - skyTintG_) * alpha;
        skyTintB_ += (tSkyB - skyTintB_) * alpha;
        hazeIntensity_ += (tHaze - hazeIntensity_) * alpha;
        fogR_ += (tFogR - fogR_) * alpha;
        fogG_ += (tFogG - fogG_) * alpha;
        fogB_ += (tFogB - fogB_) * alpha;
        fogDensityOverride_ += (tFogDensity - fogDensityOverride_) * alpha;

        // Track current preset when close enough to target.
        if (targetPreset_ != currentPreset_) {
            const float diff = std::abs(wetness_ - tWet) + std::abs(cloudCover_ - tCloud);
            if (diff < 0.02f)
                currentPreset_ = targetPreset_;
        }
    }

    /// Build the GPU UBO from the current weather state.
    WoccWeatherUbo buildUbo() const
    {
        WoccWeatherUbo ubo;
        ubo.params[0] = static_cast<float>(currentPreset_);
        ubo.params[1] = wetness_;
        ubo.params[2] = cloudCover_;
        ubo.params[3] = fogDensityScale_;

        ubo.lightAdjust[0] = sunIntensityScale_;
        ubo.lightAdjust[1] = ambientScale_;
        ubo.lightAdjust[2] = exposureAdjust_;
        ubo.lightAdjust[3] = 0.0f;

        ubo.skyTint[0] = skyTintR_;
        ubo.skyTint[1] = skyTintG_;
        ubo.skyTint[2] = skyTintB_;
        ubo.skyTint[3] = hazeIntensity_;

        ubo.fogTint[0] = fogR_;
        ubo.fogTint[1] = fogG_;
        ubo.fogTint[2] = fogB_;
        ubo.fogTint[3] = fogDensityOverride_;
        return ubo;
    }

    float wetness() const { return wetness_; }

    float fogDensityScale() const { return fogDensityScale_; }

    float sunIntensityScale() const { return sunIntensityScale_; }

    float ambientScale() const { return ambientScale_; }

    float exposureAdjust() const { return exposureAdjust_; }

    float skyTintR() const { return skyTintR_; }

    float skyTintG() const { return skyTintG_; }

    float skyTintB() const { return skyTintB_; }

    float hazeIntensity() const { return hazeIntensity_; }

    float fogR() const { return fogR_; }

    float fogG() const { return fogG_; }

    float fogB() const { return fogB_; }

    float fogDensityOverride() const { return fogDensityOverride_; }

private:
    /// Fill target values for a given preset.
    static void applyPreset(int preset,
        float& wetness,
        float& cloudCover,
        float& fogDensity,
        float& sunIntensity,
        float& ambient,
        float& exposure,
        float& skyR,
        float& skyG,
        float& skyB,
        float& haze,
        float& fogR,
        float& fogG,
        float& fogB,
        float& fogDensityOverride)
    {
        switch (preset) {
        case 1: // Overcast
            wetness = 0.30f;
            cloudCover = 0.85f;
            fogDensity = 2.5f;
            sunIntensity = 0.40f;
            ambient = 1.44f; // 0.45 * 1.44 ≈ 0.65
            exposure = 1.15f;
            skyR = 0.72f;
            skyG = 0.74f;
            skyB = 0.78f; // cool grey
            haze = 1.5f;
            fogR = 0.68f;
            fogG = 0.70f;
            fogB = 0.74f;
            fogDensityOverride = 0.025f;
            break;
        case 2: // Rain
            wetness = 0.80f;
            cloudCover = 1.0f;
            fogDensity = 4.0f;
            sunIntensity = 0.20f;
            ambient = 1.22f; // 0.45 * 1.22 ≈ 0.55
            exposure = 1.25f;
            skyR = 0.50f;
            skyG = 0.52f;
            skyB = 0.56f; // dark grey
            haze = 2.0f;
            fogR = 0.55f;
            fogG = 0.57f;
            fogB = 0.60f;
            fogDensityOverride = 0.04f;
            break;
        default: // Clear (0)
            wetness = 0.0f;
            cloudCover = 0.0f;
            fogDensity = 1.0f;
            sunIntensity = 1.0f;
            ambient = 1.0f;
            exposure = 1.0f;
            skyR = 1.0f;
            skyG = 1.0f;
            skyB = 1.0f; // neutral (use sky defaults)
            haze = 1.0f;
            fogR = 0.82f;
            fogG = 0.86f;
            fogB = 0.92f;
            fogDensityOverride = 0.0f;
            break;
        }
    }

    int currentPreset_ = 0;
    int targetPreset_ = 0;
    bool firstUpdate_ = true;

    // Smoothed state (current values).
    float wetness_ = 0.0f;
    float cloudCover_ = 0.0f;
    float fogDensityScale_ = 1.0f;
    float sunIntensityScale_ = 1.0f;
    float ambientScale_ = 1.0f;
    float exposureAdjust_ = 1.0f;
    float skyTintR_ = 1.0f;
    float skyTintG_ = 1.0f;
    float skyTintB_ = 1.0f;
    float hazeIntensity_ = 1.0f;
    float fogR_ = 0.82f;
    float fogG_ = 0.86f;
    float fogB_ = 0.92f;
    float fogDensityOverride_ = 0.0f;
};

} // namespace arp::pcg
