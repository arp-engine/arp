#include "assets/animated_mesh_coordinate_conversion.h"

#include "assets/glb_animated_mesh_importer.h"

#include <algorithm>

namespace arp::assets {

namespace {

animation::Mat4 mirrorMatrixAcrossX(const animation::Mat4& matrix)
{
    animation::Mat4 result = matrix;
    result.m[1] = -matrix.m[1];
    result.m[2] = -matrix.m[2];
    result.m[3] = -matrix.m[3];
    result.m[4] = -matrix.m[4];
    result.m[8] = -matrix.m[8];
    result.m[12] = -matrix.m[12];
    return result;
}

animation::Quat mirrorRotationAcrossX(const animation::Quat& rotation)
{
    // Equivalent to M * R * M for M=diag(-1,1,1), up to quaternion sign.
    return animation::Quat{
        -rotation.x,
        rotation.y,
        rotation.z,
        -rotation.w,
    };
}

} // namespace

void convertAnimatedMeshRightHandedToLeftHanded(AnimatedMeshAsset& asset)
{
    for (auto& vertex : asset.mesh.vertices) {
        vertex.position[0] = -vertex.position[0];
        vertex.normal[0] = -vertex.normal[0];
        vertex.tangent[0] = -vertex.tangent[0];
        vertex.tangent[3] = -vertex.tangent[3];
    }

    for (const auto& submesh : asset.mesh.submeshes) {
        for (uint32_t index = submesh.firstIndex;
            index + 2 < submesh.firstIndex + submesh.indexCount;
            index += 3) {
            std::swap(asset.mesh.indices[index], asset.mesh.indices[index + 1]);
        }
    }

    for (auto& joint : asset.skeleton.joints) {
        joint.inverseBindMatrix = mirrorMatrixAcrossX(joint.inverseBindMatrix);
    }
    for (auto& pose : asset.bindPose) {
        pose.translation.x = -pose.translation.x;
        pose.rotation = mirrorRotationAcrossX(pose.rotation);
    }

    for (auto& clip : asset.clips) {
        for (auto& channel : clip.channels) {
            for (auto& keyframe : channel.keyframes) {
                if (channel.targetPath == "translation") {
                    keyframe.transform.translation.x = -keyframe.transform.translation.x;
                } else if (channel.targetPath == "rotation") {
                    keyframe.transform.rotation =
                        mirrorRotationAcrossX(keyframe.transform.rotation);
                }
            }
        }
    }
}

} // namespace arp::assets
