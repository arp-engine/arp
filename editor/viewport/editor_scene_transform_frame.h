#pragma once

#include "scene/transform.h"

#include <array>
#include <cmath>
#include <limits>

namespace arp::editor {

enum class EditorSceneTransformSpace : unsigned char { World, Local };
enum class EditorSceneTransformTool : unsigned char { Select, Move, Rotate, Scale };

// Editor tool geometry and numeric intent share one basis. The Scene domain
// remains the owner of TRS composition and local/world conversion.
class EditorSceneTransformFrame final {
public:
    static constexpr float axisLength = 1.0f;
    static constexpr float ringRadius = 1.25f;
    static constexpr unsigned ringSegments = 48u;
    static constexpr float minimumParentScale = 0.000001f;
    static constexpr float axisTolerance = 64 * std::numeric_limits<float>::epsilon();
    // Existing editor numeric command limits, shared by batch preparation.
    static constexpr float maximumTranslation = 100000.0f;
    static constexpr float maximumScale = 100000.0f;

    std::array<core::Vec3, 3> axes{};

    static bool build(core::Quat worldRotation,
        EditorSceneTransformSpace space,
        EditorSceneTransformFrame& destination) noexcept;

    core::Vec3 planeU(unsigned axis) const noexcept { return axes[(axis + 1) % 3]; }

    core::Vec3 planeV(unsigned axis) const noexcept { return axes[(axis + 2) % 3]; }

    // Returns a stable diagnostic on rejection, preserving destination. Scale
    // never discards an unrepresentable world-axis shear or guesses a local axis.
    static const char* prepare(EditorSceneTransformTool tool,
        EditorSceneTransformSpace space,
        unsigned axis,
        double amount,
        const scene::Transform& startLocal,
        const scene::Transform& parentWorld,
        scene::Transform& destination) noexcept;

    static bool same(const scene::Transform& a, const scene::Transform& b) noexcept;

    static bool validAuthoring(const scene::Transform& value) noexcept;
    static bool canonicalize(const scene::Transform& value, scene::Transform& destination) noexcept;
    static bool alignedAxis(
        core::Quat rotation, core::Vec3 worldAxis, unsigned& destination) noexcept;

    // An explicit world-space unit axis lets group tools reuse single-object
    // operations. Pivot orbit/stretch is the batch domain's separate policy.
    static const char* prepareWorld(EditorSceneTransformTool tool,
        core::Vec3 worldAxis,
        double amount,
        const scene::Transform& startLocal,
        const scene::Transform& parentWorld,
        scene::Transform& destination) noexcept;
};
} // namespace arp::editor
