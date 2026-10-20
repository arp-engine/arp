#pragma once

#include "assets/asset_pipeline.h"

#include <filesystem>
#include <string>

namespace arp::tools::cluster_previewer {

struct PreviewTextureAsset {
    bool valid = false;
    uint32_t width = 0;
    uint32_t height = 0;
    bool srgb = true;
    std::pmr::string sourceLabel;
    std::pmr::string diagnostics;
    std::pmr::vector<uint8_t> rgba8;
};

struct PreviewMeshLoadResult {
    bool success = false;
    bool usedFallback = false;
    std::pmr::string sourceLabel;
    std::pmr::string diagnostics;
    arp::assets::MeshAsset mesh;
    PreviewTextureAsset baseColorTexture;
};

std::filesystem::path findProjectRootFromCurrentPath();
std::filesystem::path defaultDownloadedPreviewMeshPath();

arp::assets::MeshAsset makeProceduralPreviewMesh();

PreviewMeshLoadResult loadPreviewMesh(const std::filesystem::path& requestedPath = {});

} // namespace arp::tools::cluster_previewer
