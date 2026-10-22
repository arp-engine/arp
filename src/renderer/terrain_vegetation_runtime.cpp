#include "renderer/terrain_vegetation_runtime.h"

#include "core/math/math.h"
#include "resource/vegetation_catalog_resource.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::renderer {
namespace {

core::pmr::String diagnostic(const char* message)
{
    return core::pmr::String(message, &core::defaultCpuMemoryResource());
}

auto findPage(core::pmr::Vector<TerrainVegetationPage>& pages, terrain::TerrainTileCoord tile)
{
    return std::find_if(pages.begin(), pages.end(), [tile](const TerrainVegetationPage& page) {
        return page.tile == tile;
    });
}

} // namespace

bool decodeTerrainVegetationRecords(const std::uint8_t* bytes,
    std::size_t byteCount,
    core::pmr::Vector<resource::TerrainStaticVegetationInstance>& out,
    core::pmr::String& diagnostics)
{
    out.clear();
    if (!bytes || byteCount == 0 || byteCount % 48 != 0) {
        diagnostics = "VEGETATION-RECORD-LAYOUT-INVALID";
        return false;
    }
    const auto word = [](const std::uint8_t* p) {
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16)
            | (std::uint32_t(p[3]) << 24);
    };
    const auto scalar = [&](const std::uint8_t* p) {
        const auto bits = word(p);
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    };
    out.reserve(byteCount / 48);
    for (std::size_t offset = 0; offset < byteCount; offset += 48) {
        const auto* p = bytes + offset;
        resource::TerrainStaticVegetationInstance r;
        r.prototypeId = word(p);
        r.position = {scalar(p + 4), scalar(p + 8), scalar(p + 12)};
        r.rotation = {scalar(p + 16), scalar(p + 20), scalar(p + 24), scalar(p + 28)};
        r.scale = {scalar(p + 32), scalar(p + 36), scalar(p + 40)};
        r.variationSeed = word(p + 44);
        bool valid = r.prototypeId != 0;
        for (std::size_t k = 4; k < 44; k += 4)
            valid = valid && std::isfinite(scalar(p + k));
        float norm = 0;
        for (auto c : r.rotation)
            norm += c * c;
        if (!valid || r.scale.x <= 0 || r.scale.y <= 0 || r.scale.z <= 0
            || std::abs(norm - 1) > 1e-4f) {
            out.clear();
            diagnostics = "VEGETATION-RECORD-TRANSFORM-INVALID";
            return false;
        }
        out.push_back(r);
    }
    diagnostics.clear();
    return true;
}

bool buildTerrainVegetationGpuInstances(const TerrainVegetationPrototypePage& page,
    core::pmr::Vector<TerrainVegetationGpuInstance>& outInstances,
    core::pmr::String& diagnostics)
{
    outInstances.clear();
    diagnostics.clear();
    if (page.prototypeId == 0u) {
        diagnostics =
            diagnostic("Terrain vegetation GPU page requires a non-zero catalog prototype ID");
        return false;
    }
    outInstances.reserve(page.instances.size());
    for (const auto& instance : page.instances) {
        if (instance.prototypeId != page.prototypeId || !std::isfinite(instance.position.x)
            || !std::isfinite(instance.position.y) || !std::isfinite(instance.position.z)
            || !std::isfinite(instance.scale.x) || !std::isfinite(instance.scale.y)
            || !std::isfinite(instance.scale.z)) {
            diagnostics = diagnostic(
                "Terrain vegetation GPU page contains an invalid or cross-prototype instance");
            outInstances.clear();
            return false;
        }
        TerrainVegetationGpuInstance gpu;
        gpu.prototypeId = instance.prototypeId;
        gpu.position[0] = instance.position.x;
        gpu.position[1] = instance.position.y;
        gpu.position[2] = instance.position.z;
        for (std::size_t index = 0u; index < instance.rotation.size(); ++index)
            gpu.rotation[index] = instance.rotation[index];
        gpu.scale[0] = instance.scale.x;
        gpu.scale[1] = instance.scale.y;
        gpu.scale[2] = instance.scale.z;
        gpu.variationSeed = instance.variationSeed;
        outInstances.push_back(gpu);
    }
    return true;
}

bool lowerTerrainVegetationInstancesForCull(const TerrainVegetationPrototypePage& page,
    core::pmr::Vector<vegetation::InstanceDataGpu>& outInstances,
    core::pmr::String& diagnostics)
{
    outInstances.clear();
    diagnostics.clear();
    if (page.prototypeId == 0u) {
        diagnostics =
            diagnostic("Terrain vegetation cull lowering requires a non-zero catalog prototype ID");
        return false;
    }
    constexpr std::uint64_t kExactFloatIntegerLimit = std::uint64_t{1}
        << std::numeric_limits<float>::digits;
    if (page.instances.size() > kExactFloatIntegerLimit) {
        diagnostics = diagnostic(
            "Terrain vegetation cull lowering exceeds the established float instance-ID range");
        return false;
    }
    outInstances.reserve(page.instances.size());
    for (std::size_t index = 0u; index < page.instances.size(); ++index) {
        const auto& source = page.instances[index];
        if (source.prototypeId != page.prototypeId || source.scale.x != source.scale.z) {
            diagnostics =
                diagnostic("Terrain vegetation instance cannot be represented by the cull ABI");
            outInstances.clear();
            return false;
        }
        float yaw = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
        const core::Quat rotation{
            source.rotation[0], source.rotation[1], source.rotation[2], source.rotation[3]};
        if (!core::quaternionToYawPitchRollRadians(rotation, yaw, pitch, roll)) {
            diagnostics =
                diagnostic("Terrain vegetation instance has an invalid quaternion rotation");
            outInstances.clear();
            return false;
        }
        vegetation::InstanceDataGpu lowered;
        lowered.positionScale[0] = source.position.x;
        lowered.positionScale[1] = source.position.y;
        lowered.positionScale[2] = source.position.z;
        lowered.positionScale[3] = source.scale.x;
        lowered.yawPhaseIdLod[0] = yaw;
        lowered.yawPhaseIdLod[1] = static_cast<float>(source.variationSeed) / 4294967296.0f;
        lowered.yawPhaseIdLod[2] = static_cast<float>(index);
        lowered.yawPhaseIdLod[3] = 0.0f;
        lowered.extraTransform[0] = pitch;
        lowered.extraTransform[1] = roll;
        lowered.extraTransform[2] = source.scale.y == source.scale.x ? 0.0f : source.scale.y;
        lowered.extraTransform[3] = 0.0f;
        vegetation::setInstanceMetadata(lowered, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
        outInstances.push_back(lowered);
    }
    return true;
}

core::pmr::Vector<TerrainVegetationPrototypePage> groupTerrainVegetationInstances(
    core::ArrayView<vegetation::StaticVegetationInstance> instances)
{
    core::pmr::Vector<TerrainVegetationPrototypePage> prototypes{&core::defaultCpuMemoryResource()};
    for (const auto& instance : instances) {
        auto bucket =
            std::find_if(prototypes.begin(), prototypes.end(), [&](const auto& prototype) {
                return prototype.prototypeId == instance.prototypeId;
            });
        if (bucket == prototypes.end()) {
            TerrainVegetationPrototypePage prototype;
            prototype.prototypeId = instance.prototypeId;
            prototypes.push_back(std::move(prototype));
            bucket = std::prev(prototypes.end());
        }
        bucket->instances.push_back(instance);
    }
    std::sort(prototypes.begin(), prototypes.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.prototypeId < rhs.prototypeId;
    });
    return prototypes;
}

TerrainVegetationPage TerrainVegetationRuntime::buildPage(
    const resource::TerrainAssetResource& terrain, terrain::TerrainTileCoord tile)
{
    TerrainVegetationPage page;
    page.tile = tile;
    page.terrainGeneration = terrain.generation();
    page.terrainResourceId = terrain.reference().id;
    page.vegetationCatalogResourceId = terrain.vegetationCatalogResourceId();
    if (const auto* instances = terrain.vegetationFor(tile))
        page.prototypes = groupTerrainVegetationInstances(*instances);
    return page;
}

bool pageMatchesCatalog(const TerrainVegetationPage& page,
    const resource::VegetationCatalogResource& catalog,
    core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (catalog.reference().id != page.vegetationCatalogResourceId || catalog.data == nullptr) {
        diagnostics = diagnostic(
            "Terrain vegetation page requires its declared ready VegetationCatalogResource");
        return false;
    }
    for (const auto& prototype : page.prototypes) {
        if (prototype.prototypeId == 0u
            || prototype.prototypeId > std::numeric_limits<std::uint16_t>::max()
            || catalog.data->findAssetByTemplateId(
                   static_cast<std::uint16_t>(prototype.prototypeId))
                == nullptr) {
            diagnostics =
                diagnostic("Terrain vegetation page references an unknown catalog prototype");
            return false;
        }
    }
    return true;
}

TerrainVegetationRuntime::Result TerrainVegetationRuntime::activate(
    const resource::TerrainAssetResource& terrain,
    const resource::VegetationCatalogResource& catalog,
    terrain::TerrainTileCoord tile,
    std::uint64_t submissionSerial,
    UploadPageFn upload,
    void* context)
{
    Result result;
    if (upload == nullptr) {
        result.diagnostics = diagnostic(
            "Terrain vegetation page activation requires an explicit backend upload callback");
        return result;
    }
    if (terrain.vegetationFor(tile) == nullptr) {
        result.diagnostics =
            diagnostic("Terrain vegetation page is unavailable for the requested tile");
        return result;
    }

    auto existing = findPage(activePages, tile);
    if (existing != activePages.end() && existing->terrainGeneration == terrain.generation()
        && existing->terrainResourceId == terrain.reference().id) {
        result.success = true;
        result.unchanged = true;
        return result;
    }
    if (existing != activePages.end() && !retiredPages_.canRetire()) {
        result.diagnostics = diagnostic("Terrain vegetation retirement capacity is exhausted");
        return result;
    }

    TerrainVegetationPage candidate = buildPage(terrain, tile);
    if (!pageMatchesCatalog(candidate, catalog, result.diagnostics)) {
        return result;
    }
    if (!upload(context, candidate, result.diagnostics)) {
        if (result.diagnostics.empty()) {
            result.diagnostics = diagnostic("Terrain vegetation backend rejected the asset page");
        }
        return result;
    }
    if (existing == activePages.end()) {
        activePages.push_back(std::move(candidate));
    } else {
        TerrainVegetationPage retired = std::move(*existing);
        *existing = std::move(candidate);
        if (!retiredPages_.retire(submissionSerial, std::move(retired))) {
            result.diagnostics = diagnostic("Terrain vegetation retirement capacity is exhausted");
            return result;
        }
    }
    result.success = true;
    return result;
}

bool TerrainVegetationRuntime::deactivate(
    terrain::TerrainTileCoord tile, std::uint64_t submissionSerial)
{
    auto existing = findPage(activePages, tile);
    if (existing == activePages.end())
        return false;
    if (!retiredPages_.canRetire())
        return false;
    TerrainVegetationPage retired = std::move(*existing);
    activePages.erase(existing);
    return retiredPages_.retire(submissionSerial, std::move(retired));
}

const TerrainVegetationPage* TerrainVegetationRuntime::page(
    terrain::TerrainTileCoord tile) const noexcept
{
    const auto found = std::find_if(
        activePages.begin(), activePages.end(), [tile](const TerrainVegetationPage& page) {
            return page.tile == tile;
        });
    return found == activePages.end() ? nullptr : &*found;
}

} // namespace arp::renderer
