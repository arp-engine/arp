#pragma once

#include "animation/animation.h"
#include "animation/skeleton.h"
#include "assets/asset_pipeline.h"
#include "core/api.h"
#include "core/cpu_allocator.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace arp::assets {

struct SkinnedVertex {
    std::array<uint16_t, 4> joints{};
    std::array<float, 4> weights{};
};

struct AnimatedMeshAsset {
    MeshAsset mesh;
    std::pmr::vector<SkinnedVertex> skinnedVertices;
    animation::Skeleton skeleton;
    std::pmr::vector<animation::LocalTransform> bindPose;
    std::pmr::vector<animation::AnimationClip> clips;
    std::pmr::vector<ImportedMaterialAsset> materials;
    std::pmr::vector<ImportedTextureAsset> textures;
    std::pmr::vector<ImportedImageAsset> images;
};

struct AnimatedMeshImportResult {
    bool success = false;
    std::pmr::string diagnostics;
    AnimatedMeshAsset asset;
};

ARP_API AnimatedMeshImportResult importAnimatedMeshFromGlbFile(const std::filesystem::path& path);

} // namespace arp::assets
