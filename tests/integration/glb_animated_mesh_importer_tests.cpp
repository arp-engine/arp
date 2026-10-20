#include "assets/glb_animated_mesh_importer.h"

#include <cassert>
#include <filesystem>

namespace {

std::filesystem::path findProjectRoot()
{
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "artifacts" / "project-lane" / "assets")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}

void assertAnimatedImport(const std::filesystem::path& path)
{
    const auto imported = arp::assets::importAnimatedMeshFromGlbFile(path);
    assert(imported.success);
    assert(imported.asset.mesh.validForIndexedDraw());
    assert(imported.asset.skinnedVertices.size() == imported.asset.mesh.vertices.size());
    assert(imported.asset.skeleton.valid());
    assert(imported.asset.skeleton.jointCount() > 0);
    assert(!imported.asset.clips.empty());
    assert(imported.asset.clips[0].durationSeconds > 0.0f);
    assert(!imported.asset.clips[0].channels.empty());
    assert(!imported.asset.materials.empty());
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    assertAnimatedImport(root / "artifacts/project-lane/assets/khronos/CesiumMan/CesiumMan.glb");
    const auto foxPath = root / "artifacts/project-lane/assets/khronos/Fox/Fox.glb";
    assertAnimatedImport(foxPath);

    const auto fox = arp::assets::importAnimatedMeshFromGlbFile(foxPath);
    assert(fox.success);
    assert(fox.asset.materials.size() == 1);
    assert(fox.asset.materials[0].name == "fox_material");
    assert(fox.asset.materials[0].baseColorTexture == 0);
    assert(fox.asset.textures.size() == 1);
    assert(fox.asset.textures[0].sourceImage == 0);
    assert(fox.asset.images.size() == 1);
    assert(fox.asset.images[0].mimeType == "image/png");
    assert(!fox.asset.images[0].encodedBytes.empty());

    const auto staticOnly = arp::assets::importAnimatedMeshFromGlbFile(
        root / "artifacts/project-lane/assets/khronos/Box/Box.glb");
    assert(!staticOnly.success);
    assert(!staticOnly.diagnostics.empty());

    return 0;
}
