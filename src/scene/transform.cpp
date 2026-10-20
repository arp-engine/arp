#include "scene/transform.h"

#include <cmath>

namespace arp::scene {
namespace {

float divideOrDefault(float numerator, float denominator, float defaultValue)
{
    return std::fabs(denominator) > 0.000001f ? numerator / denominator : defaultValue;
}

} // namespace

Transform Transform::identity()
{
    return Transform{};
}

Transform Transform::fromTranslation(arp::core::Vec3 value)
{
    Transform transform;
    transform.translation = value;
    return transform;
}

Transform composeTransforms(const Transform& parentWorld, const Transform& local)
{
    Transform result;
    // TRS composition: world = parentWorld * local.
    //   position: parent translation + rotate(parentRotation, localTranslation * parentScale)
    //   rotation: parentRotation * localRotation  (Hamilton product, normalized)
    //   scale:    parentScale * localScale
    const auto scaledLocalTranslation = arp::core::Vec3{
        local.translation.x * parentWorld.scale.x,
        local.translation.y * parentWorld.scale.y,
        local.translation.z * parentWorld.scale.z,
    };
    const auto rotatedTranslation = arp::core::rotate(parentWorld.rotation, scaledLocalTranslation);
    result.translation = {
        parentWorld.translation.x + rotatedTranslation.x,
        parentWorld.translation.y + rotatedTranslation.y,
        parentWorld.translation.z + rotatedTranslation.z,
    };
    result.scale = {
        parentWorld.scale.x * local.scale.x,
        parentWorld.scale.y * local.scale.y,
        parentWorld.scale.z * local.scale.z,
    };
    result.rotation = arp::core::normalize(parentWorld.rotation * local.rotation);
    return result;
}

Transform deriveLocalTransform(const Transform& parentWorld, const Transform& desiredWorld)
{
    Transform result;
    // Inverse of composeTransforms: local = parentWorld^-1 * desiredWorld.
    //   translation: rotate(conj(parentRotation), worldTranslation - parentTranslation) / parentScale
    //   rotation:    conj(parentRotation) * worldRotation
    //   scale:       worldScale / parentScale
    const auto deltaTranslation = arp::core::Vec3{
        desiredWorld.translation.x - parentWorld.translation.x,
        desiredWorld.translation.y - parentWorld.translation.y,
        desiredWorld.translation.z - parentWorld.translation.z,
    };
    const auto unrotatedTranslation =
        arp::core::rotate(arp::core::conjugate(parentWorld.rotation), deltaTranslation);
    result.translation = {
        divideOrDefault(unrotatedTranslation.x, parentWorld.scale.x, 0.0f),
        divideOrDefault(unrotatedTranslation.y, parentWorld.scale.y, 0.0f),
        divideOrDefault(unrotatedTranslation.z, parentWorld.scale.z, 0.0f),
    };
    result.scale = {
        divideOrDefault(desiredWorld.scale.x, parentWorld.scale.x, 1.0f),
        divideOrDefault(desiredWorld.scale.y, parentWorld.scale.y, 1.0f),
        divideOrDefault(desiredWorld.scale.z, parentWorld.scale.z, 1.0f),
    };
    result.rotation =
        arp::core::normalize(arp::core::conjugate(parentWorld.rotation) * desiredWorld.rotation);
    return result;
}

} // namespace arp::scene
