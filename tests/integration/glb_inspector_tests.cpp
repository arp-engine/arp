#include "assets/glb_inspector.h"

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

} // namespace

int main()
{
    const auto root = findProjectRoot();

    const auto fox =
        arp::assets::inspectGlbFile(root / "artifacts/project-lane/assets/khronos/Fox/Fox.glb");
    assert(fox.success);
    assert(fox.info.validGlb);
    assert(fox.info.version == 2);
    assert(fox.info.meshCount >= 1);
    assert(fox.info.nodeCount >= 1);
    assert(fox.info.animationCount >= 1);
    assert(fox.info.skinCount >= 1);

    const auto man = arp::assets::inspectGlbFile(
        root / "artifacts/project-lane/assets/khronos/CesiumMan/CesiumMan.glb");
    assert(man.success);
    assert(man.info.meshCount >= 1);
    assert(man.info.animationCount >= 1);
    assert(man.info.skinCount >= 1);

    const auto box =
        arp::assets::inspectGlbFile(root / "artifacts/project-lane/assets/khronos/Box/Box.glb");
    assert(box.success);
    assert(box.info.meshCount >= 1);
    assert(box.info.nodeCount >= 1);
    assert(box.info.animationCount == 0);

    const auto missing =
        arp::assets::inspectGlbFile(root / "artifacts/project-lane/assets/missing.glb");
    assert(!missing.success);
    assert(!missing.diagnostics.empty());

    return 0;
}
