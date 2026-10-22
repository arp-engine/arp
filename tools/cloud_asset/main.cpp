#include "assets/cloud/cloud_asset.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "resource/cloud_asset_cooked.h"

#include <charconv>
#include <cstdio>
#include <string_view>

int main(int argc, char** argv)
{
    using namespace arp;
    if (argc != 5 || std::string_view(argv[1]) != "cook") {
        std::fprintf(stderr,
            "Usage: arp_cloud_asset_tool cook <recipe.yaml> <cooked.yaml> <max-document-bytes>\n");
        return 2;
    }
    const std::string_view budgetText = argv[4];
    std::uint64_t budget = 0u;
    const auto parsed =
        std::from_chars(budgetText.data(), budgetText.data() + budgetText.size(), budget);
    if (parsed.ec != std::errc{} || parsed.ptr != budgetText.data() + budgetText.size() || !budget)
        return 2;
    try {
        const auto source = core::readTextFile(argv[2], budget);
        if (!source.success) {
            std::fprintf(stderr, "%s\n", source.diagnostics.c_str());
            return 1;
        }
        assets::cloud::CloudCookDesc recipe;
        auto result = assets::cloud::CloudAssetCooker::parse(source.text, budget, recipe);
        assets::cloud::CloudAsset asset;
        if (result.success)
            result = assets::cloud::CloudAssetCooker::cook(recipe, asset);
        core::pmr::String cooked;
        if (result.success)
            result = resource::CloudAssetCookedCodec::encode(asset, budget, cooked);
        if (!result.success) {
            std::fprintf(stderr, "%s\n", result.diagnostic);
            return 1;
        }
        const core::pmr::Vector<std::uint8_t> bytes(cooked.begin(), cooked.end());
        const auto written = core::io::replaceFileAtomically(core::pmr::String(argv[3]), bytes);
        if (!written.success) {
            std::fprintf(stderr, "%s\n", written.diagnostic.c_str());
            return 1;
        }
        std::printf("CloudPeriodicSimplex/%u cooked %zu cells (%zu document bytes)\n",
            recipe.algorithmVersion,
            asset.cells.size(),
            bytes.size());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
