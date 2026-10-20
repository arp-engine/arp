#include "assets/animated_mesh_coordinate_conversion.h"
#include "assets/glb_animated_mesh_importer.h"

#include <cmath>
#include <iostream>
#include <utility>

namespace {

int failures = 0;

void expect(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(const float left, const float right)
{
    return std::fabs(left - right) <= 0.00001f;
}

arp::assets::AnimatedMeshAsset makeAsset()
{
    arp::assets::AnimatedMeshAsset asset;
    arp::assets::MeshVertex vertex;
    vertex.position[0] = 2.0f;
    vertex.position[1] = 3.0f;
    vertex.position[2] = 4.0f;
    vertex.normal[0] = 0.25f;
    vertex.normal[1] = 0.5f;
    vertex.normal[2] = 0.75f;
    vertex.tangent[0] = 0.2f;
    vertex.tangent[1] = 0.4f;
    vertex.tangent[2] = 0.8f;
    vertex.tangent[3] = 1.0f;
    asset.mesh.vertices.assign(3u, vertex);
    asset.mesh.indices = {0u, 1u, 2u};
    asset.mesh.submeshes.push_back({0u, 3u, 0u});

    auto inverseBind = animation::Mat4::identity();
    inverseBind.m[1] = 1.0f;
    inverseBind.m[2] = 2.0f;
    inverseBind.m[4] = 3.0f;
    inverseBind.m[8] = 4.0f;
    inverseBind.m[12] = 5.0f;
    asset.skeleton.joints.push_back({"RightHand", -1, inverseBind});

    animation::LocalTransform bind;
    bind.translation = {6.0f, 7.0f, 8.0f};
    bind.rotation = {0.1f, 0.2f, 0.3f, 0.9f};
    asset.bindPose.push_back(bind);

    animation::AnimationClip clip;
    clip.name = "pose";
    clip.durationSeconds = 1.0f;
    animation::AnimationChannel translation;
    translation.jointName = "RightHand";
    translation.targetPath = "translation";
    translation.keyframes.push_back({0.0f, {{9.0f, 10.0f, 11.0f}}});
    animation::AnimationChannel rotation;
    rotation.jointName = "RightHand";
    rotation.targetPath = "rotation";
    animation::JointTransform rotationTransform;
    rotationTransform.rotation = {0.15f, 0.25f, 0.35f, 0.85f};
    rotation.keyframes.push_back({0.0f, rotationTransform});
    clip.channels.push_back(std::move(translation));
    clip.channels.push_back(std::move(rotation));
    asset.clips.push_back(std::move(clip));
    return asset;
}

void validatesCompleteAnimatedMeshConversion()
{
    auto asset = makeAsset();
    arp::assets::convertAnimatedMeshRightHandedToLeftHanded(asset);

    const auto& vertex = asset.mesh.vertices.front();
    expect(near(vertex.position[0], -2.0f) && near(vertex.position[1], 3.0f)
            && near(vertex.position[2], 4.0f),
        "mesh position must reflect across X only");
    expect(near(vertex.normal[0], -0.25f) && near(vertex.normal[1], 0.5f),
        "mesh normal must reflect across X");
    expect(near(vertex.tangent[0], -0.2f) && near(vertex.tangent[3], -1.0f),
        "tangent direction and handedness must reflect across X");
    expect(
        asset.mesh.indices[0] == 1u && asset.mesh.indices[1] == 0u && asset.mesh.indices[2] == 2u,
        "triangle winding must reverse after reflection");

    const auto& inverseBind = asset.skeleton.joints.front().inverseBindMatrix.m;
    expect(near(inverseBind[1], -1.0f) && near(inverseBind[2], -2.0f) && near(inverseBind[4], -3.0f)
            && near(inverseBind[8], -4.0f) && near(inverseBind[12], -5.0f),
        "inverse bind matrix must be reflected on both sides");
    const auto& bind = asset.bindPose.front();
    expect(near(bind.translation.x, -6.0f), "bind translation must reflect across X");
    expect(near(bind.rotation.x, -0.1f) && near(bind.rotation.y, 0.2f)
            && near(bind.rotation.z, 0.3f) && near(bind.rotation.w, -0.9f),
        "bind rotation must reflect across X");

    const auto& translation = asset.clips.front().channels[0].keyframes.front().transform;
    expect(near(translation.translation.x, -9.0f) && near(translation.translation.y, 10.0f)
            && near(translation.translation.z, 11.0f),
        "animation translation must reflect across X");
    const auto& rotation = asset.clips.front().channels[1].keyframes.front().transform.rotation;
    expect(near(rotation.x, -0.15f) && near(rotation.y, 0.25f) && near(rotation.z, 0.35f)
            && near(rotation.w, -0.85f),
        "animation rotation must reflect across X");
}

void validatesConversionIsAnInvolution()
{
    auto asset = makeAsset();
    arp::assets::convertAnimatedMeshRightHandedToLeftHanded(asset);
    arp::assets::convertAnimatedMeshRightHandedToLeftHanded(asset);

    expect(near(asset.mesh.vertices.front().position[0], 2.0f),
        "two handedness conversions must restore mesh positions");
    expect(asset.mesh.indices[0] == 0u && asset.mesh.indices[1] == 1u,
        "two handedness conversions must restore winding");
    expect(near(asset.bindPose.front().translation.x, 6.0f),
        "two handedness conversions must restore bind pose");
    expect(near(asset.clips.front().channels[0].keyframes.front().transform.translation.x, 9.0f),
        "two handedness conversions must restore animation channels");
}

} // namespace

int main()
{
    validatesCompleteAnimatedMeshConversion();
    validatesConversionIsAnInvolution();
    if (failures == 0) {
        std::cout << "Animated mesh coordinate conversion contracts passed\n";
    }
    return failures == 0 ? 0 : 1;
}
