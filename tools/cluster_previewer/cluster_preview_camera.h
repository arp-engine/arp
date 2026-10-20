#pragma once

#include "tools/cluster_previewer/cluster_preview_scene.h"

namespace arp::tools::cluster_previewer {

struct CameraVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class OrbitPreviewCamera {
public:
    void frameBounds(const PreviewBounds& bounds);
    void orbit(float deltaX, float deltaY);
    void zoom(float wheelDelta);
    void pan(float deltaX, float deltaY, int viewportWidth, int viewportHeight);

    float yawDegrees() const;
    float pitchDegrees() const;
    float distance() const;
    CameraVec3 target() const;
    CameraVec3 eye() const;

private:
    CameraVec3 orbitCenter_;
    CameraVec3 target_;
    float yawDegrees_ = -35.0f;
    float pitchDegrees_ = 25.0f;
    float distance_ = 5.0f;
    float minDistance_ = 0.25f;
    float maxDistance_ = 5000.0f;
};

} // namespace arp::tools::cluster_previewer
