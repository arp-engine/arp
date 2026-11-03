#include "resource/environment_lod_sources.h"

#include "resource/resource_source_io.h"

#include <algorithm>
#include <tuple>

namespace arp::resource {
namespace {
bool sameLimits(const assets::NamedVariantSourceImportLimits& a,
    const assets::NamedVariantSourceImportLimits& b) noexcept
{
    const auto key = [](const auto& v) {
        return std::tie(v.maxSourceBytes,
            v.maxImportBytes,
            v.maxVertices,
            v.maxIndices,
            v.maxPrimitives,
            v.maxVariants,
            v.maxNodeVisits,
            v.maxNodeDepth,
            v.maxJsonDepth,
            v.maxJsonValues,
            v.colorPolicy);
    };
    return key(a) == key(b);
}

std::uint64_t reservedImports(const EnvironmentLodSourceDesc& desc) noexcept
{
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < desc.residentLevels; ++i)
        if (desc.levels[i].sharedLod == UINT32_MAX)
            total += desc.levels[i].limits.maxImportBytes;
    return total;
}

std::uint64_t decodedFootprint(const EnvironmentLodData& data) noexcept
{
    std::uint64_t bytes = 0;
    for (const auto& variant : data.variants)
        bytes += variant.mesh.vertices.size() * sizeof(assets::MeshVertex)
            + variant.mesh.indices.size() * sizeof(std::uint32_t)
            + variant.mesh.submeshes.size() * sizeof(assets::Submesh)
            + variant.windWeights.size() * sizeof(float)
            + variant.materialSlots.size() * sizeof(std::uint32_t);
    for (const auto& image : data.images)
        bytes += image.encodedBytes.size();
    return bytes;
}
} // namespace

const char* EnvironmentLodSourceLoader::validate(const EnvironmentLodSourceDesc& desc)
{
    if (desc.levels.empty() || desc.levels.size() > EnvironmentLodSourceDesc::maxLevels
        || !desc.residentLevels || desc.residentLevels > desc.levels.size()
        || desc.variantNames.empty() || desc.variantNames.size() > UINT32_MAX
        || !desc.maxDecodedBytes || desc.maxDecodedBytes == SIZE_MAX || !desc.maxTotalSourceBytes
        || desc.maxTotalSourceBytes == SIZE_MAX || !desc.maxTotalImportBytes
        || desc.maxTotalImportBytes == UINT64_MAX
        || desc.geometrySpace != assets::NamedVariantGeometrySpace::RootRelative)
        return "ENVIRONMENT-LOD-SOURCE-CONFIG";
    for (std::size_t i = 0; i < desc.variantNames.size(); ++i) {
        const auto& name = desc.variantNames[i];
        if (name.empty() || name.find('\0') != core::pmr::String::npos
            || core::Utf8Decoder::validate(name))
            return "ENVIRONMENT-LOD-VARIANT-NAME";
        for (std::size_t j = 0; j < i; ++j)
            if (name == desc.variantNames[j])
                return "ENVIRONMENT-LOD-VARIANT-NAME";
    }
    std::uint64_t imports = 0;
    std::size_t sourceBytes = 0;
    for (std::size_t i = 0; i < desc.levels.size(); ++i) {
        const auto& level = desc.levels[i];
        if (const auto* invalid = assets::validateNamedVariantSourceImportLimits(level.limits))
            return invalid;
        if (!level.byteCount || level.byteCount > level.limits.maxSourceBytes
            || level.version.kind != core::io::FileVersionKind::ContentSha256
            || !level.version.valid() || desc.variantNames.size() > level.limits.maxVariants)
            return "ENVIRONMENT-LOD-SOURCE-BINDING";
        if (validateResourceReadIntent(level.path, level.byteCount, desc.origin).diagnostic)
            return "ENVIRONMENT-LOD-SOURCE-PATH";
        if (level.sharedLod != UINT32_MAX
            && (level.sharedLod >= i || level.version != desc.levels[level.sharedLod].version
                || level.byteCount != desc.levels[level.sharedLod].byteCount
                || !sameLimits(level.limits, desc.levels[level.sharedLod].limits)))
            return "ENVIRONMENT-LOD-SHARING";
        bool duplicate = false;
        for (std::size_t j = 0; j < i; ++j)
            if (desc.levels[j].path == level.path) {
                duplicate = true;
                if (desc.levels[j].version != level.version
                    || desc.levels[j].byteCount != level.byteCount)
                    return "ENVIRONMENT-LOD-SOURCE-CONFLICT";
            }
        if (i >= desc.residentLevels)
            continue;
        if (!duplicate) {
            if (level.byteCount > desc.maxTotalSourceBytes - sourceBytes)
                return "ENVIRONMENT-LOD-SOURCE-BUDGET";
            sourceBytes += level.byteCount;
        }
        if (level.sharedLod == UINT32_MAX) {
            if (level.limits.maxImportBytes >= desc.maxTotalImportBytes - imports)
                return "ENVIRONMENT-LOD-IMPORT-BUDGET";
            imports += level.limits.maxImportBytes;
        }
    }
    return nullptr;
}

bool EnvironmentLodSourceLoader::collect(const EnvironmentLodSourceDesc& desc,
    core::pmr::Vector<ResourceFileReadIntent>& reads,
    core::pmr::String& diagnostic)
{
    if (const auto* invalid = validate(desc)) {
        diagnostic = invalid;
        return false;
    }
    core::pmr::Vector<ResourceFileReadIntent> candidate;
    candidate.reserve(desc.residentLevels);
    for (std::size_t i = 0; i < desc.residentLevels; ++i) {
        const auto& level = desc.levels[i];
        if (std::any_of(candidate.begin(), candidate.end(), [&](const auto& item) {
                return item.path == level.path;
            }))
            continue;
        if (!appendResourceReadIntent(
                level.path, level.byteCount, candidate, diagnostic, desc.origin))
            return false;
        candidate.back().expectedVersion = level.version;
        candidate.back().maxVersionBytes = level.byteCount;
        candidate.back().role = "environment-glb";
    }
    reads = std::move(candidate);
    diagnostic.clear();
    return true;
}

EnvironmentLodPreparation EnvironmentLodSourceLoader::prepare(
    const EnvironmentLodSourceDesc& desc, const ResourceLoadContext& context)
{
    EnvironmentLodPreparation result;
    try {
        if (const auto* invalid = validate(desc)) {
            result.diagnostic = invalid;
            return result;
        }
        if (!context.maxDecodedBytes) {
            result.diagnostic = "ENVIRONMENT-LOD-DECODE-BUDGET-REQUIRED";
            return result;
        }
        // Child importers own their quotas. Reserve their declared maxima, and
        // charge all remaining preparation overhead to the disjoint allowance.
        auto overhead = core::makeShared<core::CpuMemoryQuota>(
            desc.maxTotalImportBytes - reservedImports(desc));
        core::CpuMemoryQuotaScope scope(overhead);
        core::pmr::Vector<EnvironmentLodData> candidate;
        candidate.reserve(desc.residentLevels);
        auto inputs = context;
        inputs.requirePreloadedSources = true;
        std::uint64_t childPeaks = 0, decoded = 0;
        for (std::uint32_t i = 0; i < desc.residentLevels; ++i) {
            const auto& spec = desc.levels[i];
            auto source =
                readResourceSource(spec.path, inputs, spec.byteCount, false, &desc.origin);
            if (!source.success) {
                result.diagnostic = std::move(source.diagnostics);
                return result;
            }
            const auto digest = core::Sha256::digest(
                {reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()});
            if (source.bytes.size() != spec.byteCount
                || !std::equal(digest.begin(), digest.end(), spec.version.digest.begin())) {
                result.diagnostic = "ENVIRONMENT-LOD-SOURCE-IDENTITY";
                return result;
            }
            EnvironmentLodData data;
            data.lodIndex = data.sourceLod = i;
            if (spec.sharedLod != UINT32_MAX) {
                data.sourceLod = spec.sharedLod;
                candidate.push_back(std::move(data));
                continue;
            }
            auto imported = assets::importNamedVariantsFromGlbBytes(
                {source.bytes.data(), source.bytes.size()}, desc.geometrySpace, spec.limits);
            if (!imported.success) {
                result.diagnostic = std::move(imported.diagnostics);
                return result;
            }
            if (imported.variants.size() != desc.variantNames.size()) {
                result.diagnostic = "ENVIRONMENT-LOD-VARIANT-MAPPING";
                return result;
            }
            data.variants.reserve(desc.variantNames.size());
            for (const auto& name : desc.variantNames) {
                const auto found = std::find_if(
                    imported.variants.begin(), imported.variants.end(), [&](const auto& variant) {
                        return variant.name == name;
                    });
                if (found == imported.variants.end()) {
                    result.diagnostic = "ENVIRONMENT-LOD-VARIANT-MAPPING";
                    return result;
                }
                data.variants.push_back(std::move(*found));
            }
            data.materials = std::move(imported.materials);
            data.textures = std::move(imported.textures);
            data.images = std::move(imported.images);
            childPeaks += imported.importPeakBytes;
            decoded += decodedFootprint(data);
            if (decoded > desc.maxDecodedBytes || decoded > context.maxDecodedBytes) {
                result.diagnostic = "ENVIRONMENT-LOD-DECODE-BUDGET";
                return result;
            }
            candidate.push_back(std::move(data));
        }
        result.levels = std::move(candidate);
        result.decodedBytes = static_cast<std::size_t>(decoded);
        result.importPeakUpperBound = overhead->peakBytes() + childPeaks;
        result.success = true;
    } catch (const std::bad_alloc&) {
        result.diagnostic = "ENVIRONMENT-LOD-IMPORT-BUDGET";
    } catch (...) {
        result.diagnostic = "ENVIRONMENT-LOD-PREPARATION-INVALID";
    }
    return result;
}

void EnvironmentLodSourceLoader::fingerprint(
    const EnvironmentLodSourceDesc& desc, core::StableHash64& hash) noexcept
{
    appendResourceOriginFingerprint(hash, desc.origin);
    hash.appendU64(desc.residentLevels);
    hash.appendU64(desc.levels.size());
    hash.appendU64(desc.variantNames.size());
    hash.appendU64(desc.maxTotalSourceBytes);
    hash.appendU64(desc.maxTotalImportBytes);
    hash.appendU64(desc.maxDecodedBytes);
    for (const auto& name : desc.variantNames) {
        hash.appendU64(name.size());
        hash.appendBytes(name.data(), name.size());
    }
    for (const auto& level : desc.levels) {
        hash.appendU64(level.path.size());
        hash.appendBytes(level.path.data(), level.path.size());
        hash.appendU64(level.byteCount);
        hash.appendBytes(level.version.digest.data(), level.version.digest.size());
        hash.appendU64(level.sharedLod);
        hash.appendU64(static_cast<unsigned>(level.limits.colorPolicy));
        for (const auto budget : {level.limits.maxSourceBytes,
                 level.limits.maxImportBytes,
                 std::uint64_t(level.limits.maxVertices),
                 std::uint64_t(level.limits.maxIndices),
                 std::uint64_t(level.limits.maxPrimitives),
                 std::uint64_t(level.limits.maxVariants),
                 std::uint64_t(level.limits.maxNodeVisits),
                 std::uint64_t(level.limits.maxNodeDepth),
                 std::uint64_t(level.limits.maxJsonDepth),
                 std::uint64_t(level.limits.maxJsonValues)})
            hash.appendU64(budget);
    }
}

const EnvironmentLodData* EnvironmentLodSourceLoader::resolve(
    const core::pmr::Vector<EnvironmentLodData>& levels, std::uint32_t index) noexcept
{
    for (std::size_t guard = 0; guard < levels.size() && index < levels.size(); ++guard) {
        if (levels[index].sourceLod == index)
            return &levels[index];
        index = levels[index].sourceLod;
    }
    return nullptr;
}
} // namespace arp::resource
