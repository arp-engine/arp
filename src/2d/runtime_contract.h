#pragma once

#include "core/math/math.h"
#include "rhi/gpu_common.h"

#include <cmath>
#include <cstdint>

namespace arp::scene2d {

inline constexpr std::uint32_t kRuntime2DContractVersion = 1;
enum class Execution2D { ValidatorOnly, Native, Equivalent, DiagnosticFailure };

// Availability comes from an installed executable, never the backend name.
struct Executor2DEvidence {
    bool installed = false;
    bool shaderInterfaceValidated = false;
    bool resourceBindingsValidated = false;
    bool nativePayloadAvailable = false;
};

struct Execution2DDecision {
    Execution2D mode = Execution2D::DiagnosticFailure;
    const char* diagnostic = "ARP-2D-EXECUTOR-UNAVAILABLE";

    bool executable() const
    {
        return mode == Execution2D::Native || mode == Execution2D::Equivalent;
    }
};

inline Execution2DDecision lowerExecution2D(rhi::Backend backend, Executor2DEvidence evidence)
{
    switch (backend) {
    case rhi::Backend::Null:
        return {Execution2D::ValidatorOnly, "ARP-2D-NULL-VALIDATOR-ONLY"};
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
    case rhi::Backend::WebGPU:
        if (evidence.installed && evidence.shaderInterfaceValidated
            && evidence.resourceBindingsValidated && evidence.nativePayloadAvailable)
            return {Execution2D::Native, "ARP-2D-NATIVE"};
        return {};
    case rhi::Backend::WebGL:
    default:
        return {Execution2D::DiagnosticFailure, "ARP-2D-BACKEND-UNSUPPORTED"};
    }
}

// XY world, +Y up, positive Z rotation counterclockwise, screen +Y down.
// Atlas input is straight RGBA; shader output/blend is premultiplied linear.
class Viewport2DMapping final {
public:
    bool configure(std::uint32_t width, std::uint32_t height, core::Vec2 logical, bool integerScale)
    {
        valid_ = false;
        if (!width || !height || !std::isfinite(logical.x) || !std::isfinite(logical.y)
            || logical.x <= 0 || logical.y <= 0)
            return false;
        const float fit = std::min(float(width) / logical.x, float(height) / logical.y);
        // An integer scale cannot fit a canvas larger than the physical surface.
        const float scale = integerScale ? std::floor(fit) : fit;
        if (scale <= 0)
            return false;
        scale_ = scale;
        logical_ = logical;
        origin_ = {(width - logical.x * scale) * .5f, (height - logical.y * scale) * .5f};
        valid_ = true;
        return true;
    }

    bool screenToWorld(
        core::Vec2 screen, core::Vec2 camera, float pixelsPerUnit, core::Vec2& result) const
    {
        if (!valid_ || !std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0
            || !std::isfinite(screen.x) || !std::isfinite(screen.y))
            return false;
        const auto x = (screen.x - origin_.x) / scale_;
        const auto y = (screen.y - origin_.y) / scale_;
        if (x < 0 || y < 0 || x >= logical_.x || y >= logical_.y)
            return false;
        result = {camera.x + (x - logical_.x * .5f) / pixelsPerUnit,
            camera.y - (y - logical_.y * .5f) / pixelsPerUnit};
        return true;
    }

    core::Vec2 worldToScreen(core::Vec2 world, core::Vec2 camera, float pixelsPerUnit) const
    {
        return {origin_.x + (logical_.x * .5f + (world.x - camera.x) * pixelsPerUnit) * scale_,
            origin_.y + (logical_.y * .5f - (world.y - camera.y) * pixelsPerUnit) * scale_};
    }

    core::Vec2 origin() const { return origin_; }

    float scale() const { return scale_; }

    bool valid() const { return valid_; }

private:
    core::Vec2 origin_{}, logical_{};
    float scale_ = 0;
    bool valid_ = false;
};

struct SpriteOrder {
    std::int32_t layer = 0, order = 0;
    bool ySort = false;
    float worldY = 0;
    std::uint64_t objectId = 0;
};

// Y sorting is a layer policy, applied consistently to all objects in a layer.
inline bool spriteOrderLess(const SpriteOrder& a, const SpriteOrder& b)
{
    if (a.layer != b.layer)
        return a.layer < b.layer;
    if (a.order != b.order)
        return a.order < b.order;
    if (a.ySort != b.ySort)
        return a.ySort < b.ySort;
    if (a.ySort && a.worldY != b.worldY)
        return a.worldY > b.worldY;
    return a.objectId < b.objectId;
}
} // namespace arp::scene2d
