#pragma once

#include "core/api.h"
#include "scene/component.h"
#include "scene/transform.h"

#include <cmath>
#include <limits>

namespace arp::scene {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct TransformEditorProperties {
    APROP(Editor)
    arp::core::Vec3 position{};
    APROP(Editor)
    // Euler degrees in X=pitch, Y=yaw, Z=roll order for editor controls.
    arp::core::Vec3 rotation{};
    APROP(Editor)
    arp::core::Vec3 scale{1.0f, 1.0f, 1.0f};
};

// Transform is a first-class script component.  The native traversal cache
// remains engine-owned; scripts can only read/write the authored local
// transform and inspect the stable component identity through generated
// reflection methods.
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ARP_API TransformComponent final : public Component {
public:
    const char* scriptTypeName() const override { return "TransformComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    AFUNC()

    Transform local_transform() const { return localTransform_; }
    AFUNC()
    Transform world_transform() const;
    AFUNC()

    void set_local_transform(const Transform& transform) { localTransform_ = transform; }
    AFUNC()

    void set_local_translation(arp::core::Vec3 translation) { setLocalTranslation(translation); }
    AFUNC()

    void set_local_rotation(arp::core::Quat rotation) { setLocalRotation(rotation); }
    AFUNC()

    void set_local_yaw_pitch(float yawDegrees, float pitchDegrees)
    {
        setLocalYawPitch(yawDegrees, pitchDegrees);
    }

    const Transform& localTransform() const { return localTransform_; }

    const Transform& worldTransform() const { return worldTransform_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        TransformEditorProperties properties;
        properties.position = localTransform_.translation;
        properties.scale = localTransform_.scale;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
        if (arp::core::quaternionToYawPitchRollRadians(
                localTransform_.rotation, yaw, pitch, roll)) {
            constexpr float degreesPerRadian = 180.0f / 3.14159265358979f;
            properties.rotation = {
                pitch * degreesPerRadian, yaw * degreesPerRadian, roll * degreesPerRadian};
        } else {
            const auto invalid = std::numeric_limits<float>::quiet_NaN();
            properties.rotation = {invalid, invalid, invalid};
        }
        return arp::reflection::object(properties);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<TransformEditorProperties>())
            return false;
        const auto& properties = value.cast<const TransformEditorProperties&>();
        const auto finite = [](arp::core::Vec3 vector) {
            return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
        };
        if (!finite(properties.position) || !finite(properties.rotation)
            || !finite(properties.scale))
            return false;
        localTransform_.translation = properties.position;
        localTransform_.rotation = arp::core::Quat::fromYawPitchRoll(
            properties.rotation.y, properties.rotation.x, properties.rotation.z);
        localTransform_.scale = properties.scale;
        return true;
    }

    void setLocalTransform(const Transform& transform) { localTransform_ = transform; }

    void setLocalTranslation(arp::core::Vec3 translation)
    {
        localTransform_.translation = translation;
    }

    void setLocalRotation(arp::core::Quat rotation) { localTransform_.rotation = rotation; }

    void setLocalYawPitch(float yawDegrees, float pitchDegrees)
    {
        localTransform_.rotation =
            arp::core::Quat::fromYawPitchRoll(yawDegrees, pitchDegrees, 0.0f);
    }

    void setWorldTransformForTraversal(const Transform& transform) { worldTransform_ = transform; }

private:
    Transform localTransform_;
    Transform worldTransform_;
};

} // namespace arp::scene
