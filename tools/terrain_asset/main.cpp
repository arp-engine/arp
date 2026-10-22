#include "resource/internal_asset_package.h"
#include "resource/terrain_asset_build_service.h"
#include "resource/vegetation_catalog_build_service.h"

#include <cstdio>
#include <string_view>

int main(int argc, char** argv)
{
    using namespace arp;
    resource::TerrainAssetIoResult result;
    if (argc == 4 && std::string_view(argv[1]) == "build") {
        result = resource::TerrainAssetBuildService().build(argv[2], argv[3]);
    } else if (argc == 4 && std::string_view(argv[1]) == "cook-catalog") {
        const auto cooked = resource::VegetationCatalogBuildService().build(argv[2], argv[3]);
        result = {cooked.success, cooked.diagnostics};
    } else if (argc == 4 && std::string_view(argv[1]) == "package") {
        result = resource::TerrainAssetBuildService().packageClosed(argv[2], argv[3]);
    } else if (argc >= 5 && std::string_view(argv[1]) == "migrate-v1") {
        resource::ResourceManager dependencies;
        for (int i = 5; i < argc; ++i) {
            const auto loaded = resource::InternalAssetPackageReader().load(argv[i], dependencies);
            if (!loaded.success) {
                std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
                return 1;
            }
        }
        resource::ResourceReference reference;
        reference.id = argv[4];
        reference.type = "TerrainAssetResource";
        reference.status = "Contract Complete";
        result =
            resource::TerrainAssetWriter().migrateV1(dependencies, reference, argv[2], argv[3]);
    } else {
        std::fprintf(stderr,
            "Usage: arp_terrain_asset_tool build <authoring.yaml> <output-package>\n"
            "       arp_terrain_asset_tool cook-catalog <catalog-authoring.yaml> <output-package>\n"
            "       arp_terrain_asset_tool package <prepared-closed-package> <output-package>\n"
            "       arp_terrain_asset_tool migrate-v1 <manifest> <output-directory> <resource-id> [dependency-package ...]\n");
        return 2;
    }
    if (!result.diagnostics.empty())
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    return result.success ? 0 : 1;
}
