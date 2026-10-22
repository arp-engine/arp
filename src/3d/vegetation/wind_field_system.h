#pragma once

/// @file wind_field_system.h
/// @brief P7: Dynamic wind field system.
///
/// The wind field is the single source of truth for all vegetation motion.
/// It is composed of four layers:
///   1. Smooth global direction/speed (slowly varying, time-smoothed)
///   2. Low-frequency gusts (spatial waves that travel across the world)
///   3. Spatial turbulence (noise-based crosswind variation)
///   4. High-frequency leaf flutter (per-instance phase modulation)
///
/// The weather system sets the target wind direction/speed; the WindFieldSystem
/// smoothly interpolates toward it so the forest never instantly switches
/// direction. The GPU receives a compact UBO (WindFieldParameters) each frame;
/// per-instance phase and per-vertex wind weights (COLOR_0) provide the
/// spatial and structural variation.
///
/// Animation LOD is driven by the instance's lodClass field:
///   lodClass 0 (near):  full deformation (bend + gust + flutter)
///   lodClass 1 (mid):   gust + sway, no high-frequency flutter
///   lodClass 2 (far):   low-frequency sway only
///   lodClass 3+ (far):  frozen (no GPU cost)

#include <cmath>
#include <cstdint>

namespace arp::vegetation {

/// GPU wind field UBO. Mirrors the Slang struct `WoccWindField` in
/// pbr_scene.slang. All fields are float4 for std140 alignment.
/// Total size: 4 × float4 = 64 bytes.
struct WindFieldParameters {
    // xyz = normalized wind direction (world space), w = wind speed [0, 10]
    float directionSpeed[4] = {1.0f, 0.0f, 0.0f, 2.0f};
    // x = elapsed time (seconds), y = gust amplitude [0, 1],
    // z = gust frequency, w = turbulence amplitude [0, 1]
    float timeGustTurb[4] = {0.0f, 0.3f, 0.4f, 0.2f};
    // x = flutter frequency, y = flutter amplitude [0, 1],
    // z = interaction radius (0 = disabled), w = interaction strength
    float flutterParams[4] = {8.0f, 0.5f, 0.0f, 0.0f};
    // xyz = interaction center (player position), w = reserved
    float interactionCenter[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// CPU-side wind field state. Updated each frame and uploaded as a UBO.
class WindFieldSystem {
public:
    WindFieldSystem() = default;

    /// Set the target wind direction (will be normalized) and speed.
    /// The actual direction smoothly interpolates toward this target.
    void setTargetWind(float dirX, float dirZ, float speed)
    {
        const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
        if (len > 0.001f) {
            targetDirX_ = dirX / len;
            targetDirZ_ = dirZ / len;
        }
        targetSpeed_ = speed;
    }

    /// Set the player position for interaction field (push grass).
    void setInteractionCenter(float x, float z)
    {
        interactionX_ = x;
        interactionZ_ = z;
        interactionEnabled_ = true;
    }

    void disableInteraction() { interactionEnabled_ = false; }

    /// Advance the wind field by dt seconds. Smoothly interpolates direction
    /// and speed toward targets, advances the time accumulator.
    void update(float dt)
    {
        elapsedTime_ += dt;

        // Smooth direction interpolation (slerp-like for 2D).
        // Use exponential smoothing: current += (target - current) * rate * dt.
        const float smoothRate = 0.5f; // half-life ~1.4s
        const float alpha = 1.0f - std::exp(-smoothRate * dt);
        currentDirX_ += (targetDirX_ - currentDirX_) * alpha;
        currentDirZ_ += (targetDirZ_ - currentDirZ_) * alpha;

        // Re-normalize direction.
        const float len = std::sqrt(currentDirX_ * currentDirX_ + currentDirZ_ * currentDirZ_);
        if (len > 0.001f) {
            currentDirX_ /= len;
            currentDirZ_ /= len;
        }

        // Smooth speed.
        currentSpeed_ += (targetSpeed_ - currentSpeed_) * alpha;
    }

    /// Build the GPU UBO from the current wind state.
    WindFieldParameters buildUbo() const
    {
        WindFieldParameters ubo;
        ubo.directionSpeed[0] = currentDirX_;
        ubo.directionSpeed[1] = 0.0f;
        ubo.directionSpeed[2] = currentDirZ_;
        ubo.directionSpeed[3] = currentSpeed_;

        ubo.timeGustTurb[0] = elapsedTime_;
        ubo.timeGustTurb[1] = gustAmplitude_;
        ubo.timeGustTurb[2] = gustFrequency_;
        ubo.timeGustTurb[3] = turbulenceAmplitude_;

        ubo.flutterParams[0] = flutterFrequency_;
        ubo.flutterParams[1] = flutterAmplitude_;

        if (interactionEnabled_) {
            ubo.flutterParams[2] = interactionRadius_;
            ubo.flutterParams[3] = interactionStrength_;
            ubo.interactionCenter[0] = interactionX_;
            ubo.interactionCenter[2] = interactionZ_;
        } else {
            ubo.flutterParams[2] = 0.0f;
            ubo.flutterParams[3] = 0.0f;
        }
        ubo.interactionCenter[1] = 0.0f;
        ubo.interactionCenter[3] = 0.0f;

        return ubo;
    }

    float elapsedTime() const { return elapsedTime_; }

    float currentDirX() const { return currentDirX_; }

    float currentDirZ() const { return currentDirZ_; }

    float currentSpeed() const { return currentSpeed_; }

    // Tunable parameters (can be driven by weather system).
    float gustAmplitude_ = 0.3f;
    float gustFrequency_ = 0.4f;
    float turbulenceAmplitude_ = 0.2f;
    float flutterFrequency_ = 8.0f;
    float flutterAmplitude_ = 0.5f;
    float interactionRadius_ = 6.0f;
    float interactionStrength_ = 0.8f;

private:
    float elapsedTime_ = 0.0f;
    float currentDirX_ = 1.0f;
    float currentDirZ_ = 0.0f;
    float currentSpeed_ = 2.0f;
    float targetDirX_ = 1.0f;
    float targetDirZ_ = 0.0f;
    float targetSpeed_ = 2.0f;
    bool interactionEnabled_ = false;
    float interactionX_ = 0.0f;
    float interactionZ_ = 0.0f;
};

} // namespace arp::vegetation
