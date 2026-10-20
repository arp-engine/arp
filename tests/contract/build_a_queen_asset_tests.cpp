#include "animation/skinned_mesh_player.h"
#include "assets/glb_animated_mesh_importer.h"
#include "assets/glb_static_mesh_importer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <utility>

namespace {

bool hasMaterial(const arp::assets::StaticMeshImportResult& imported, std::string_view name)
{
    for (const auto& material : imported.materials)
        if (material.name == name)
            return true;
    return false;
}

void verifyCharacter(
    const std::filesystem::path& path, std::array<std::string_view, 5> requiredMaterials)
{
    auto imported = arp::assets::importStaticMeshFromGlbFile(path);
    assert(imported.success);
    assert(imported.mesh.validForIndexedDraw());
    assert(imported.mesh.recomputeBounds());
    assert(imported.mesh.bounds.max[1] > imported.mesh.bounds.min[1]);
    for (const auto& submesh : imported.mesh.submeshes)
        assert(submesh.materialSlot < imported.materials.size());
    for (const auto name : requiredMaterials)
        assert(hasMaterial(imported, name));
}

arp::assets::AnimatedMeshAsset importAnimated(const std::filesystem::path& path)
{
    auto imported = arp::assets::importAnimatedMeshFromGlbFile(path);
    assert(imported.success);
    assert(imported.asset.mesh.validForIndexedDraw());
    assert(imported.asset.skeleton.valid());
    assert(imported.asset.skeleton.jointCount() == 62u);
    assert(imported.asset.skinnedVertices.size() == imported.asset.mesh.vertices.size());
    assert(imported.asset.clips.size() == 24u);
    assert(imported.asset.materials.size() > 1u);
    return std::move(imported.asset);
}

void verifyRunClip(arp::assets::AnimatedMeshAsset& asset)
{
    animation::SkinnedMeshPlayer player(asset);
    assert(player.valid());
    const std::pmr::string run("Run");
    const auto clip = std::find_if(asset.clips.begin(), asset.clips.end(), [](const auto& item) {
        return item.name == "Run";
    });
    assert(clip != asset.clips.end());
    assert(clip->durationSeconds > 0.0f);
    assert(player.update(run, 0.0f));
    const auto initial = player.skinMatrices();
    assert(initial.size() == 62u);
    assert(player.update(run, 0.25f));
    const auto& moving = player.skinMatrices();
    bool changed = false;
    for (std::size_t joint = 0; joint < moving.size(); ++joint)
        for (std::size_t element = 0; element < moving[joint].m.size(); ++element)
            changed =
                changed || std::abs(initial[joint].m[element] - moving[joint].m[element]) > 0.0001f;
    assert(changed);

    const auto expectedLoopPose = moving;
    assert(player.updateLooping(run, clip->durationSeconds + 0.25f));
    const auto& repeated = player.skinMatrices();
    for (std::size_t joint = 0; joint < repeated.size(); ++joint)
        for (std::size_t element = 0; element < repeated[joint].m.size(); ++element)
            assert(std::abs(expectedLoopPose[joint].m[element] - repeated[joint].m[element])
                < 0.0001f);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root(argv[1]);
    verifyCharacter(root / "Casual.glb", {"Skin", "White", "Grey", "Hair_Brown", "Orange"});
    verifyCharacter(root / "Formal.glb", {"Skin", "Brown", "Red", "LimeGreen", "Gold"});
    auto casual = importAnimated(root / "Casual.glb");
    auto formal = importAnimated(root / "Formal.glb");
    verifyRunClip(casual);
    bool differentOutfitGeometry = casual.mesh.vertices.size() != formal.mesh.vertices.size()
        || casual.mesh.indices.size() != formal.mesh.indices.size();
    if (!differentOutfitGeometry) {
        for (std::size_t index = 0; index < casual.mesh.vertices.size(); ++index) {
            for (std::size_t axis = 0; axis < 3u; ++axis)
                differentOutfitGeometry = differentOutfitGeometry
                    || std::abs(casual.mesh.vertices[index].position[axis]
                           - formal.mesh.vertices[index].position[axis])
                        > 0.0001f;
        }
    }
    assert(differentOutfitGeometry);
    return 0;
}
