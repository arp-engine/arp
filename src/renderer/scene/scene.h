#pragma once

#include "assets/asset_pipeline.h"
#include "core/cpu_allocator.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace arp::renderer {

using assets::MeshAsset;
using assets::MeshVertex;
using assets::Submesh;

struct SceneInstance {
    uint32_t instanceId = 0;
    uint32_t meshIndex = 0;
    core::Vec3 boundsCenter;
    float boundsRadius = 1.0f;
};

struct SceneDrawItem {
    uint32_t instanceId = 0;
    uint32_t meshIndex = 0;
};

struct SceneDrawList {
    std::pmr::vector<SceneDrawItem> items;
};

inline bool sphereVisibleToCamera(const core::Camera& camera, const SceneInstance& instance)
{
    const auto localX = instance.boundsCenter.x - camera.position.x;
    const auto localY = instance.boundsCenter.y - camera.position.y;
    const auto localZ = instance.boundsCenter.z - camera.position.z;
    const auto radius = instance.boundsRadius;

    if (localZ + radius < camera.nearPlane || localZ - radius > camera.farPlane) {
        return false;
    }

    const auto halfFovRadians = camera.verticalFovDegrees * 0.5f * 3.1415926535f / 180.0f;
    const auto halfHeight = std::tan(halfFovRadians) * std::max(localZ, camera.nearPlane);
    const auto halfWidth = halfHeight * camera.aspectRatio;
    return std::fabs(localX) <= halfWidth + radius && std::fabs(localY) <= halfHeight + radius;
}

inline SceneDrawList buildSceneDrawList(
    const core::Camera& camera, const std::pmr::vector<SceneInstance>& instances)
{
    SceneDrawList drawList;
    for (const auto& instance : instances) {
        if (sphereVisibleToCamera(camera, instance)) {
            drawList.items.push_back(SceneDrawItem{
                instance.instanceId,
                instance.meshIndex,
            });
        }
    }
    return drawList;
}

struct SceneConstants {
    float viewProjection[16]{
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    float cameraPosition[3]{};
    float cameraNear = 0.1f;
    float cameraFar = 1000.0f;
    float exposure = 1.0f;
    float timeOfDay = 12.0f;
    float directionalLightDirection[3]{-0.35f, -0.72f, 0.58f};
    float directionalLightColor[3]{1.0f, 0.94f, 0.82f};
    float directionalLightIntensity = 1.0f;
    float fogStart = 120.0f;
    float fogEnd = 480.0f;
    float localLightPreviewColor[3]{};
    float localLightPreviewIntensity = 0.0f;
    float contactShadowPreviewIntensity = 0.0f;

    static SceneConstants fromCamera(const core::Camera& camera)
    {
        SceneConstants constants;
        constants.cameraNear = camera.nearPlane;
        constants.cameraFar = camera.farPlane;
        constants.cameraPosition[0] = camera.position.x;
        constants.cameraPosition[1] = camera.position.y;
        constants.cameraPosition[2] = camera.position.z;
        return constants;
    }

    void setViewProjection(const core::Mat4& matrix)
    {
        std::copy(matrix.m.begin(), matrix.m.end(), viewProjection);
    }

    void setCameraPosition(core::Vec3 position)
    {
        cameraPosition[0] = position.x;
        cameraPosition[1] = position.y;
        cameraPosition[2] = position.z;
    }
};

} // namespace arp::renderer
