#include "resource/environment_asset_resources.h"

#include "assets/glb_static_mesh_importer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>

namespace arp::resource {

namespace {

bool filesByteIdentical(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    std::error_code ec;
    const auto lhsSize = std::filesystem::file_size(lhs, ec);
    if (ec)
        return false;
    const auto rhsSize = std::filesystem::file_size(rhs, ec);
    if (ec || lhsSize != rhsSize)
        return false;

    std::ifstream a(lhs, std::ios::binary);
    std::ifstream b(rhs, std::ios::binary);
    if (!a || !b)
        return false;
    std::array<char, 64 * 1024> aBuffer{};
    std::array<char, 64 * 1024> bBuffer{};
    while (a && b) {
        a.read(aBuffer.data(), static_cast<std::streamsize>(aBuffer.size()));
        b.read(bBuffer.data(), static_cast<std::streamsize>(bBuffer.size()));
        const auto aCount = a.gcount();
        const auto bCount = b.gcount();
        if (aCount != bCount)
            return false;
        if (aCount == 0)
            break;
        if (!std::equal(aBuffer.begin(), aBuffer.begin() + aCount, bBuffer.begin())) {
            return false;
        }
    }
    return true;
}

} // namespace

// ===========================================================================
// TerrainStampAsset
// ===========================================================================

ResourceLoadResult TerrainStampAsset::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    // Read the float32 raw height file.
    std::ifstream file(rawHeightPath_, std::ios::binary);
    if (!file.good()) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "failed to open terrain stamp raw file: " + rawHeightPath_.string();
        return result;
    }

    const std::size_t expectedSamples = static_cast<std::size_t>(widthSamples_) * heightSamples_;
    heights_.resize(expectedSamples);

    file.read(reinterpret_cast<char*>(heights_.data()),
        static_cast<std::streamsize>(expectedSamples * sizeof(float)));
    if (!file.good()) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "failed to read terrain stamp raw data: " + rawHeightPath_.string();
        return result;
    }

    // Compute the actual elevation range from the loaded data and update
    // the member fields. The constructor receives estimated values from the
    // scene loader; the actual range comes from the raw file. This is critical
    // for the landform vertical remap in TerrainLandformStack, which maps
    // [minElevation, maxElevation] -> [remapMin, remapMax].
    float actualMin = 1e30f, actualMax = -1e30f;
    for (float h : heights_) {
        if (!std::isfinite(h)) {
            ResourceLoadResult result;
            result.success = false;
            result.diagnostics =
                "terrain stamp contains non-finite samples: " + rawHeightPath_.string();
            return result;
        }
        if (h < actualMin)
            actualMin = h;
        if (h > actualMax)
            actualMax = h;
    }
    minElevationMeters_ = actualMin;
    maxElevationMeters_ = actualMax;

    // Admit metadata via the base class (registers the resource with the
    // ResourceManager's metadata graph) -- same pattern as MeshAssetResource.
    return ResourceBase::load(manager, context);
}

float TerrainStampAsset::sampleNormalized(float u, float v) const
{
    if (heights_.empty())
        return minElevationMeters_;
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float fx = u * static_cast<float>(widthSamples_ - 1u);
    const float fy = v * static_cast<float>(heightSamples_ - 1u);
    const std::uint32_t x0 = static_cast<std::uint32_t>(fx);
    const std::uint32_t y0 = static_cast<std::uint32_t>(fy);
    const std::uint32_t x1 = std::min(x0 + 1u, widthSamples_ - 1u);
    const std::uint32_t y1 = std::min(y0 + 1u, heightSamples_ - 1u);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float h00 = heights_[y0 * widthSamples_ + x0];
    const float h10 = heights_[y0 * widthSamples_ + x1];
    const float h01 = heights_[y1 * widthSamples_ + x0];
    const float h11 = heights_[y1 * widthSamples_ + x1];
    const float h0 = h00 * (1.0f - tx) + h10 * tx;
    const float h1 = h01 * (1.0f - tx) + h11 * tx;
    return h0 * (1.0f - ty) + h1 * ty;
}

float TerrainStampAsset::sampleWorld(float x, float z) const
{
    if (horizontalSizeMeters_ <= 0.0f)
        return minElevationMeters_;
    const float u = x / horizontalSizeMeters_;
    const float v = z / horizontalSizeMeters_;
    return sampleNormalized(u, v);
}

// ===========================================================================
// TerrainSurfaceAsset
// ===========================================================================

ResourceLoadResult TerrainSurfaceAsset::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    // Validate that required map files exist (albedo, normal, roughness, height).
    // AO is optional.
    auto checkExists = [](const std::filesystem::path& p, const char* label) -> std::string {
        if (p.empty())
            return std::string();
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || std::filesystem::file_size(p, ec) == 0) {
            return std::string("missing or empty surface map: ") + label + " -> " + p.string();
        }
        return std::string();
    };

    std::string err;
    if (!(err = checkExists(mapPaths_.albedo, "albedo")).empty()
        || !(err = checkExists(mapPaths_.normalGl, "normal_gl")).empty()
        || !(err = checkExists(mapPaths_.roughness, "roughness")).empty()
        || !(err = checkExists(mapPaths_.height, "height")).empty()) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = err;
        return result;
    }

    // Admit metadata via the base class.
    return ResourceBase::load(manager, context);
}

// ===========================================================================
// NamedMeshVariantSet
// ===========================================================================

ResourceLoadResult NamedMeshVariantSet::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    auto importResult = assets::importNamedVariantsFromGlbFile(glbPath_);
    if (!importResult.success) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "GLB named variant import failed: "
            + std::string(importResult.diagnostics.c_str(), importResult.diagnostics.size());
        return result;
    }

    variants_ = std::move(importResult.variants);
    materials_ = std::move(importResult.materials);

    // Validate variant count.
    if (expectedVariantCount_ > 0 && variants_.size() != expectedVariantCount_) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "variant count mismatch: expected "
            + std::to_string(expectedVariantCount_) + " got " + std::to_string(variants_.size());
        return result;
    }

    // Admit metadata via the base class.
    return ResourceBase::load(manager, context);
}

const assets::NamedVariant* NamedMeshVariantSet::findVariant(const std::string& name) const
{
    auto it =
        std::find_if(variants_.begin(), variants_.end(), [&name](const assets::NamedVariant& v) {
            return std::string(v.name.c_str(), v.name.size()) == name;
        });
    return it != variants_.end() ? &(*it) : nullptr;
}

std::vector<std::string> NamedMeshVariantSet::variantNames() const
{
    std::vector<std::string> names;
    names.reserve(variants_.size());
    for (const auto& v : variants_) {
        names.emplace_back(v.name.c_str(), v.name.size());
    }
    return names;
}

// ===========================================================================
// PlantAssetSet
// ===========================================================================

ResourceLoadResult PlantAssetSet::load(ResourceManager& manager, const ResourceLoadContext& context)
{
    std::cerr << "[PlantAssetSet::load] begin assetSetId=" << assetSetId_
              << " dir=" << runtimeDir_.string() << '\n';
    // Load each LOD GLB file.
    const std::uint32_t lodLimit = std::min(initialLodCount_, maxLodCount_);
    for (std::uint32_t lod = 0; lod < lodLimit; ++lod) {
        std::ostringstream ss;
        ss << "lod" << lod << ".glb";
        auto glbPath = runtimeDir_ / ss.str();
        std::cerr << "[PlantAssetSet::load] lod=" << lod << " path=" << glbPath.string() << '\n';
        if (!std::filesystem::exists(glbPath)) {
            // LOD2 is sometimes optional for the lowest-detail set; but
            // the contract requires all 3 LODs. If missing, stop loading
            // and report what we have.
            std::cerr << "[PlantAssetSet::load] lod missing, stop at lod=" << lod << '\n';
            break;
        }

        // Preserve the complete three-LOD file contract while sharing an
        // imported representation when authored LOD payloads are identical.
        std::uint32_t sourceLod = lod;
        for (std::uint32_t previous = 0; previous < lod; ++previous) {
            std::ostringstream previousName;
            previousName << "lod" << previous << ".glb";
            if (filesByteIdentical(glbPath, runtimeDir_ / previousName.str())) {
                sourceLod = previous;
                break;
            }
        }
        if (sourceLod != lod) {
            LodData lodData;
            lodData.lodIndex = lod;
            lodData.sourceLod = sourceLod;
            lods_.push_back(std::move(lodData));
            std::cerr << "[PlantAssetSet::load] shared identical payload lod=" << lod
                      << " sourceLod=" << sourceLod << '\n';
            continue;
        }

        std::cerr << "[PlantAssetSet::load] import begin lod=" << lod << '\n';
        auto importResult = assets::importNamedVariantsFromGlbFile(
            glbPath, assets::NamedVariantGeometrySpace::RootRelative);
        std::cerr << "[PlantAssetSet::load] import end lod=" << lod
                  << " success=" << importResult.success
                  << " variants=" << importResult.variants.size()
                  << " materials=" << importResult.materials.size() << '\n';
        if (!importResult.success) {
            ResourceLoadResult result;
            result.success = false;
            result.diagnostics = "PlantAssetSet LOD" + std::to_string(lod) + " import failed: "
                + std::string(importResult.diagnostics.c_str(), importResult.diagnostics.size());
            return result;
        }

        LodData lodData;
        lodData.lodIndex = lod;
        lodData.sourceLod = lod;
        std::cerr << "[PlantAssetSet::load] move variants/materials lod=" << lod << '\n';
        lodData.variants = std::move(importResult.variants);
        lodData.materials = std::move(importResult.materials);
        lodData.textures = std::move(importResult.textures);
        lodData.images = std::move(importResult.images);
        lods_.push_back(std::move(lodData));
        std::cerr << "[PlantAssetSet::load] stored lod=" << lod << " lodCount=" << lods_.size()
                  << '\n';
    }

    if (lods_.empty()) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "PlantAssetSet: no LOD files found in " + runtimeDir_.string();
        return result;
    }

    // Validate variant count from LOD0.
    if (expectedVariants_ > 0 && !lods_.empty() && lods_[0].variants.size() != expectedVariants_) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "PlantAssetSet variant count mismatch: expected "
            + std::to_string(expectedVariants_) + " got "
            + std::to_string(lods_[0].variants.size());
        return result;
    }

    std::cerr << "[PlantAssetSet::load] admit metadata assetSetId=" << assetSetId_ << '\n';
    // Admit metadata via the base class.
    return ResourceBase::load(manager, context);
}

ResourceLoadResult PlantAssetSet::loadLod(std::uint32_t lodIndex)
{
    if (lodIndex >= maxLodCount_) {
        ResourceLoadResult result;
        result.diagnostics =
            "PlantAssetSet requested LOD exceeds contract: " + std::to_string(lodIndex);
        return result;
    }
    if (lodIndex < lods_.size()) {
        ResourceLoadResult result;
        result.success = true;
        result.state = ResourceState::Ready;
        result.resourceId = reference().id;
        result.resourceType = reference().type;
        return result;
    }
    for (std::uint32_t lod = static_cast<std::uint32_t>(lods_.size()); lod <= lodIndex; ++lod) {
        const auto glbPath = runtimeDir_ / ("lod" + std::to_string(lod) + ".glb");
        if (!std::filesystem::exists(glbPath)) {
            ResourceLoadResult result;
            result.diagnostics = "PlantAssetSet LOD file unavailable: " + glbPath.string();
            return result;
        }
        std::uint32_t sourceLod = lod;
        for (std::uint32_t previous = 0; previous < lod; ++previous) {
            if (filesByteIdentical(
                    glbPath, runtimeDir_ / ("lod" + std::to_string(previous) + ".glb"))) {
                sourceLod = previous;
                break;
            }
        }
        LodData data;
        data.lodIndex = lod;
        data.sourceLod = sourceLod;
        if (sourceLod == lod) {
            auto imported = assets::importNamedVariantsFromGlbFile(
                glbPath, assets::NamedVariantGeometrySpace::RootRelative);
            if (!imported.success) {
                ResourceLoadResult result;
                result.diagnostics = "PlantAssetSet incremental LOD" + std::to_string(lod)
                    + " import failed: "
                    + std::string(imported.diagnostics.c_str(), imported.diagnostics.size());
                return result;
            }
            if (expectedVariants_ > 0u && imported.variants.size() != expectedVariants_) {
                ResourceLoadResult result;
                result.diagnostics = "PlantAssetSet incremental variant count mismatch";
                return result;
            }
            data.variants = std::move(imported.variants);
            data.materials = std::move(imported.materials);
            data.textures = std::move(imported.textures);
            data.images = std::move(imported.images);
        }
        lods_.push_back(std::move(data));
        std::cerr << "[PlantAssetSet::loadLod] assetSetId=" << assetSetId_ << " lod=" << lod
                  << " sourceLod=" << sourceLod << '\n';
    }
    ResourceLoadResult result;
    result.success = true;
    result.state = ResourceState::Ready;
    result.resourceId = reference().id;
    result.resourceType = reference().type;
    return result;
}

const assets::NamedVariant* PlantAssetSet::findVariant(
    const std::string& variantName, std::uint32_t lodIndex) const
{
    const auto* resolved = resolvedLod(lodIndex);
    if (resolved == nullptr)
        return nullptr;
    const auto& variants = resolved->variants;
    auto it = std::find_if(
        variants.begin(), variants.end(), [&variantName](const assets::NamedVariant& v) {
            return std::string(v.name.c_str(), v.name.size()) == variantName;
        });
    return it != variants.end() ? &(*it) : nullptr;
}

std::vector<std::string> PlantAssetSet::variantNames() const
{
    if (lods_.empty())
        return {};
    std::vector<std::string> names;
    names.reserve(lods_[0].variants.size());
    for (const auto& v : lods_[0].variants) {
        names.emplace_back(v.name.c_str(), v.name.size());
    }
    return names;
}

std::size_t PlantAssetSet::variantCount() const
{
    return lods_.empty() ? 0u : lods_[0].variants.size();
}

// ===========================================================================
// RockAssetSet
// ===========================================================================

ResourceLoadResult RockAssetSet::load(ResourceManager& manager, const ResourceLoadContext& context)
{
    std::cerr << "[RockAssetSet::load] begin assetSetId=" << assetSetId_
              << " dir=" << runtimeDir_.string() << '\n';
    const std::uint32_t lodLimit = std::min(initialLodCount_, maxLodCount_);
    for (std::uint32_t lod = 0; lod < lodLimit; ++lod) {
        std::ostringstream ss;
        ss << "lod" << lod << ".glb";
        auto glbPath = runtimeDir_ / ss.str();
        std::cerr << "[RockAssetSet::load] lod=" << lod << " path=" << glbPath.string() << '\n';
        if (!std::filesystem::exists(glbPath)) {
            std::cerr << "[RockAssetSet::load] lod missing, stop at lod=" << lod << '\n';
            break;
        }

        std::cerr << "[RockAssetSet::load] import begin lod=" << lod << '\n';
        auto importResult = assets::importNamedVariantsFromGlbFile(
            glbPath, assets::NamedVariantGeometrySpace::RootRelative);
        std::cerr << "[RockAssetSet::load] import end lod=" << lod
                  << " success=" << importResult.success
                  << " variants=" << importResult.variants.size()
                  << " materials=" << importResult.materials.size() << '\n';
        if (!importResult.success) {
            ResourceLoadResult result;
            result.success = false;
            result.diagnostics = "RockAssetSet LOD" + std::to_string(lod) + " import failed: "
                + std::string(importResult.diagnostics.c_str(), importResult.diagnostics.size());
            return result;
        }

        LodData lodData;
        lodData.lodIndex = lod;
        std::cerr << "[RockAssetSet::load] move variants/materials lod=" << lod << '\n';
        lodData.variants = std::move(importResult.variants);
        lodData.materials = std::move(importResult.materials);
        lods_.push_back(std::move(lodData));
        std::cerr << "[RockAssetSet::load] stored lod=" << lod << " lodCount=" << lods_.size()
                  << '\n';
    }

    if (lods_.empty()) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "RockAssetSet: no LOD files found in " + runtimeDir_.string();
        return result;
    }

    if (expectedVariants_ > 0 && !lods_.empty() && lods_[0].variants.size() != expectedVariants_) {
        ResourceLoadResult result;
        result.success = false;
        result.diagnostics = "RockAssetSet variant count mismatch: expected "
            + std::to_string(expectedVariants_) + " got "
            + std::to_string(lods_[0].variants.size());
        return result;
    }

    std::cerr << "[RockAssetSet::load] admit metadata assetSetId=" << assetSetId_ << '\n';
    // Admit metadata via the base class.
    return ResourceBase::load(manager, context);
}

ResourceLoadResult RockAssetSet::loadLod(std::uint32_t lodIndex)
{
    if (lodIndex >= maxLodCount_) {
        ResourceLoadResult result;
        result.diagnostics =
            "RockAssetSet requested LOD exceeds contract: " + std::to_string(lodIndex);
        return result;
    }
    if (lodIndex < lods_.size()) {
        ResourceLoadResult result;
        result.success = true;
        result.state = ResourceState::Ready;
        result.resourceId = reference().id;
        result.resourceType = reference().type;
        return result;
    }
    for (std::uint32_t lod = static_cast<std::uint32_t>(lods_.size()); lod <= lodIndex; ++lod) {
        const auto glbPath = runtimeDir_ / ("lod" + std::to_string(lod) + ".glb");
        if (!std::filesystem::exists(glbPath)) {
            ResourceLoadResult result;
            result.diagnostics = "RockAssetSet LOD file unavailable: " + glbPath.string();
            return result;
        }
        auto imported = assets::importNamedVariantsFromGlbFile(
            glbPath, assets::NamedVariantGeometrySpace::RootRelative);
        if (!imported.success) {
            ResourceLoadResult result;
            result.diagnostics = "RockAssetSet incremental LOD" + std::to_string(lod)
                + " import failed: "
                + std::string(imported.diagnostics.c_str(), imported.diagnostics.size());
            return result;
        }
        if (expectedVariants_ > 0u && imported.variants.size() != expectedVariants_) {
            ResourceLoadResult result;
            result.diagnostics = "RockAssetSet incremental variant count mismatch";
            return result;
        }
        LodData data;
        data.lodIndex = lod;
        data.variants = std::move(imported.variants);
        data.materials = std::move(imported.materials);
        lods_.push_back(std::move(data));
        std::cerr << "[RockAssetSet::loadLod] assetSetId=" << assetSetId_ << " lod=" << lod << '\n';
    }
    ResourceLoadResult result;
    result.success = true;
    result.state = ResourceState::Ready;
    result.resourceId = reference().id;
    result.resourceType = reference().type;
    return result;
}

const assets::NamedVariant* RockAssetSet::findVariant(
    const std::string& variantName, std::uint32_t lodIndex) const
{
    if (lodIndex >= lods_.size())
        return nullptr;
    const auto& variants = lods_[lodIndex].variants;
    auto it = std::find_if(
        variants.begin(), variants.end(), [&variantName](const assets::NamedVariant& v) {
            return std::string(v.name.c_str(), v.name.size()) == variantName;
        });
    return it != variants.end() ? &(*it) : nullptr;
}

std::vector<std::string> RockAssetSet::variantNames() const
{
    if (lods_.empty())
        return {};
    std::vector<std::string> names;
    names.reserve(lods_[0].variants.size());
    for (const auto& v : lods_[0].variants) {
        names.emplace_back(v.name.c_str(), v.name.size());
    }
    return names;
}

std::size_t RockAssetSet::variantCount() const
{
    return lods_.empty() ? 0u : lods_[0].variants.size();
}

} // namespace arp::resource
