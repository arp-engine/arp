#include "assets/glb_animated_mesh_importer.h"
#include "assets/image_decoder.h"
#include "core/memory/cpu_allocator.h"

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

void assertDecodedRgba8(const arp::assets::ImageDecodeResult& decoded)
{
    assert(decoded.success);
    assert(decoded.image.width > 0);
    assert(decoded.image.height > 0);
    assert(decoded.image.rgba8.size() == decoded.image.width * decoded.image.height * 4u);
}

void assertDecodedThroughImageDecodeAllocator(
    const std::pmr::vector<uint8_t>& encodedBytes, const std::pmr::string& mimeType)
{
    const auto before = arp::core::defaultCpuAllocator().stats().byIntent(
        arp::core::CpuAllocationIntent::ImageDecode);
    const auto decoded = arp::assets::decodeImageToRgba8(encodedBytes, mimeType, true);
    assertDecodedRgba8(decoded);
    const auto after = arp::core::defaultCpuAllocator().stats().byIntent(
        arp::core::CpuAllocationIntent::ImageDecode);
    assert(after.allocationCount > before.allocationCount);
    assert(after.deallocationCount > before.deallocationCount);
    assert(after.liveBytes == before.liveBytes);
}

} // namespace

int main()
{
    const auto root = findProjectRoot();

    const auto fox = arp::assets::importAnimatedMeshFromGlbFile(
        root / "artifacts/project-lane/assets/khronos/Fox/Fox.glb");
    assert(fox.success);
    assert(fox.asset.images.size() == 1);
    assertDecodedThroughImageDecodeAllocator(
        fox.asset.images[0].encodedBytes, fox.asset.images[0].mimeType);

    const auto cesiumMan = arp::assets::importAnimatedMeshFromGlbFile(
        root / "artifacts/project-lane/assets/khronos/CesiumMan/CesiumMan.glb");
    assert(cesiumMan.success);
    assert(cesiumMan.asset.images.size() == 1);
    assertDecodedThroughImageDecodeAllocator(
        cesiumMan.asset.images[0].encodedBytes, cesiumMan.asset.images[0].mimeType);

    const std::pmr::vector<uint8_t> invalidImage{1, 2, 3, 4};
    const auto invalid = arp::assets::decodeImageToRgba8(invalidImage, "image/png", true);
    assert(!invalid.success);
    assert(!invalid.diagnostics.empty());

    return 0;
}
