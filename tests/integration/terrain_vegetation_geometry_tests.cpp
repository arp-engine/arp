#include "resource/internal_asset_package.h"
#include "resource/vegetation_catalog_geometry.h"
#include "tests/fixtures/terrain_vegetation_geometry.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using Preparation = renderer::TerrainVegetationGeometryPreparation;
using Catalog = resource::VegetationCatalogResourceData;

void check(const renderer::TerrainVegetationGeometrySnapshot& snapshot,
    const std::shared_ptr<const renderer::TerrainVegetationSnapshot>& source)
{
    assert(snapshot.instances->source == source);
    std::uint32_t previous = 0u;
    for (const auto& prototype : snapshot.prototypes) {
        assert(prototype.prototypeId > previous);
        previous = prototype.prototypeId;
        const auto index = prototype.prototypeId - 1u;
        assert(prototype.geometry.get() == &source->catalog->templates[index]);
        assert(prototype.asset.get() == &source->catalog->assets[index]);
        assert(!prototype.geometry.owner_before(source->catalog)
            && !source->catalog.owner_before(prototype.geometry));
        assert(!prototype.asset.owner_before(source->catalog)
            && !source->catalog.owner_before(prototype.asset));
        assert(prototype.sections.size() == prototype.geometry->sections.size());
        for (std::size_t i = 0u; i < prototype.sections.size(); ++i) {
            const auto& section = prototype.sections[i];
            const auto& original = prototype.geometry->sections[i];
            assert(section.firstIndex == original.firstIndex
                && section.indexCount == original.indexCount
                && section.materialSlot == original.materialSlot);
            assert(section.materialBindingIndex < prototype.asset->materials.size());
            assert(prototype.asset->materials[section.materialBindingIndex].slot
                == section.materialSlot);
        }
    }
}

void validationContracts()
{
    auto source = tests::terrainVegetationGeometrySourceFixture();
    auto desc = tests::terrainVegetationGeometryDescFixture();
    Preparation owner;
    assert(owner.prepare(source, desc).success);
    const auto previous = owner.snapshot();
    assert(previous->prototypes.size() == 2u); // Three tile/prototype groups share two geometries.
    check(*previous, source);
    const auto reject = [&](auto change, const char* diagnostic) {
        auto changed = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
        auto catalog = core::makeShared<Catalog>(*source->catalog);
        changed->catalog = catalog;
        change(*catalog);
        const auto result = owner.prepare(changed, desc);
        assert(!result.success && result.diagnostics == diagnostic && owner.snapshot() == previous);
    };
    reject(
        [](auto& c) {
            c.templates.back().indices[5] = 4u;
        },
        "VEGETATION-GEOMETRY-INDEX-RANGE");
    reject(
        [](auto& c) {
            c.templates.back().sections.back().firstIndex = 2u;
        },
        "VEGETATION-GEOMETRY-SECTION-PARTITION");
    reject(
        [](auto& c) {
            c.templates.back().sections.back().indexCount = 2u;
        },
        "VEGETATION-GEOMETRY-SECTION-PARTITION");
    reject(
        [](auto& c) {
            c.templates.back().sections.back().indexCount = UINT32_MAX;
        },
        "VEGETATION-GEOMETRY-SECTION-PARTITION");
    reject(
        [](auto& c) {
            c.templates.back().sections.pop_back();
            c.assets.back().materials.pop_back();
        },
        "VEGETATION-GEOMETRY-SECTION-COVERAGE");
    reject(
        [](auto& c) {
            c.templates.back().vertices.clear();
        },
        "VEGETATION-GEOMETRY-COUNTS");
    reject(
        [](auto& c) {
            c.templates.back().indices.pop_back();
        },
        "VEGETATION-GEOMETRY-COUNTS");
    reject(
        [](auto& c) {
            c.templates.back().sections.clear();
        },
        "VEGETATION-GEOMETRY-COUNTS");
    for (unsigned field = 0u; field < 15u; ++field) {
        reject(
            [&](auto& c) {
                auto& v = c.templates.back().vertices.back();
                const float nan = std::numeric_limits<float>::quiet_NaN();
                if (field < 3u)
                    v.position[field] = nan;
                else if (field < 6u)
                    v.normal[field - 3u] = nan;
                else if (field < 8u)
                    v.uv[field - 6u] = nan;
                else if (field < 12u)
                    v.tangent[field - 8u] = nan;
                else if (field == 12u)
                    v.primaryBend = nan;
                else if (field == 13u)
                    v.flutter = nan;
                else
                    v.stiffness = nan;
            },
            "VEGETATION-GEOMETRY-VERTEX-NONFINITE");
    }
    for (unsigned field = 0u; field < 3u; ++field)
        for (float invalid : {-0.01f, 1.01f})
            reject(
                [&](auto& c) {
                    auto& v = c.templates.back().vertices.back();
                    if (field == 0u)
                        v.primaryBend = invalid;
                    else if (field == 1u)
                        v.flutter = invalid;
                    else
                        v.stiffness = invalid;
                },
                "VEGETATION-GEOMETRY-WIND-RANGE");
    reject(
        [](auto& c) {
            c.assets.back().materials.clear();
        },
        "VEGETATION-GEOMETRY-MATERIAL-BINDINGS");
    reject(
        [](auto& c) {
            c.assets.back().materials.back().slot = 7u;
        },
        "VEGETATION-GEOMETRY-MATERIAL-BINDINGS");
    reject(
        [](auto& c) {
            std::swap(c.assets.back().materials[0], c.assets.back().materials[1]);
        },
        "VEGETATION-GEOMETRY-MATERIAL-BINDINGS");
    reject(
        [](auto& c) {
            c.assets.back().materials.back().resourceId = "unresolved-by-name";
        },
        "VEGETATION-GEOMETRY-MATERIAL-BINDINGS");
    reject(
        [](auto& c) {
            c.assets.back().materials.back().slot = UINT32_MAX;
        },
        "VEGETATION-GEOMETRY-MATERIAL-BINDINGS");
    reject(
        [](auto& c) {
            c.assets.back().materials.pop_back();
        },
        "VEGETATION-GEOMETRY-MATERIAL-MISSING");
    reject(
        [](auto& c) {
            c.assets.back().materials.back().slot = 24u;
        },
        "VEGETATION-GEOMETRY-MATERIAL-UNUSED");
    reject(
        [](auto& c) {
            c.contractVersion = 2u;
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    reject(
        [](auto& c) {
            c.templates.back().id = 1u;
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    reject(
        [](auto& c) {
            c.assets.back().templateId = 1u;
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    reject(
        [](auto& c) {
            c.assets.back().templateCategory = vegetation::TemplateCategory::TreeMaturePine;
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    reject(
        [](auto& c) {
            c.templateIdsByAssetId.begin()->second = 99u;
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    reject(
        [](auto& c) {
            c.templateIdsByAssetId.emplace("extra", 2u);
        },
        "VEGETATION-GEOMETRY-CATALOG-BUDGET");
    reject(
        [](auto& c) {
            c.templateIdsByAssetId.erase(c.assets.back().id);
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    reject(
        [](auto& c) {
            c.assets.back().id = c.assets.front().id;
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    reject(
        [](auto& c) {
            c.assets.back().id.append(1u, '\0');
        },
        "VEGETATION-GEOMETRY-CATALOG-INDEX");
    const auto budgets = [&](auto change, const char* diagnostic) {
        auto reduced = desc;
        change(reduced);
        const auto result = owner.prepare(source, reduced);
        assert(!result.success && result.diagnostics == diagnostic && owner.snapshot() == previous);
    };
    budgets(
        [](auto& d) {
            d.contractVersion = 2u;
        },
        "VEGETATION-GEOMETRY-BUDGET-INVALID");
    budgets(
        [](auto& d) {
            d.maxTiles = 2u;
        },
        "VEGETATION-DRAW-BUDGET");
    budgets(
        [](auto& d) {
            d.maxInstances = 3u;
        },
        "VEGETATION-DRAW-BUDGET");
    budgets(
        [](auto& d) {
            d.maxCatalogEntries = 1u;
        },
        "VEGETATION-GEOMETRY-CATALOG-BUDGET");
    budgets(
        [](auto& d) {
            d.maxPrototypes = 1u;
        },
        "VEGETATION-GEOMETRY-PROTOTYPE-BUDGET");
    budgets(
        [](auto& d) {
            --d.maxVertices;
        },
        "VEGETATION-GEOMETRY-PAYLOAD-BUDGET");
    budgets(
        [](auto& d) {
            --d.maxIndices;
        },
        "VEGETATION-GEOMETRY-PAYLOAD-BUDGET");
    budgets(
        [](auto& d) {
            --d.maxSections;
        },
        "VEGETATION-GEOMETRY-PAYLOAD-BUDGET");
    budgets(
        [](auto& d) {
            d.maxCpuBytes = 1u;
        },
        "VEGETATION-GEOMETRY-CPU-BUDGET");
    assert(!owner.prepare(nullptr, desc).success && owner.snapshot() == previous);
    auto next = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    auto catalog = core::makeShared<Catalog>(*source->catalog);
    // Explicit sections may share a slot, and slots may share a material ID.
    catalog->templates.back().sections.back().materialSlot = 7u;
    catalog->assets.back().materials.pop_back();
    next->catalog = catalog;
    ++next->catalogResourceGeneration;
    assert(owner.prepare(next, desc).success && owner.snapshot() != previous);
    check(*owner.snapshot(), next);
    catalog = core::makeShared<Catalog>(*source->catalog);
    catalog->assets.back().materials.back().resourceId = tests::vegetationGeometryMaterialId;
    next = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    next->catalog = catalog;
    assert(owner.prepare(next, desc).success);
    check(*owner.snapshot(), next);
}

void lookupAndOwnershipContracts()
{
    auto source = tests::terrainVegetationGeometrySourceFixture();
    auto catalog = core::makeShared<Catalog>(*source->catalog);
    catalog->templateIdsByAssetId.erase(catalog->assets.front().id);
    catalog->assets.front().id.assign(65536u, 'a');
    catalog->templateIdsByAssetId.emplace(catalog->assets.front().id, 1u);
    source->catalog = catalog;
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(catalog->templateIdFor(catalog->assets.front().id) == 1u);
        assert(catalog->findTemplate(catalog->assets.front().id) == &catalog->templates.front());
        assert(catalog->templateIdFor("missing") == UINT16_MAX);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == before);
    Preparation owner;
    auto desc = tests::terrainVegetationGeometryDescFixture();
    desc.maxCpuBytes = 16384u; // Metadata quota smaller than the pre-existing asset ID.
    assert(owner.prepare(source, desc).success);
    auto snapshot = owner.snapshot();
    check(*snapshot, source);
    auto geometryAlias = snapshot->prototypes.back().geometry;
    auto assetAlias = snapshot->prototypes.back().asset;
    std::weak_ptr<const Catalog> weak = catalog;
    owner.clear();
    snapshot.reset();
    source.reset();
    catalog.reset();
    assert(!weak.expired() && geometryAlias->vertices.size() == 4u);
    geometryAlias.reset();
    assert(!weak.expired() && assetAlias->materials.back().slot == 23u);
    assetAlias.reset();
    assert(weak.expired());
}

void packageSmoke(const std::filesystem::path& package)
{
    resource::ResourceManager resources;
    const auto loaded = resource::InternalAssetPackageReader().load(package, resources);
    if (!loaded.success)
        std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    const auto terrain =
        resources.find<resource::TerrainAssetResource>(tests::vegetationDrawTerrainId);
    assert(terrain.ready() && terrain.get()->hasVegetation());
    const auto catalogHandle =
        resources.find<resource::VegetationCatalogResource>(tests::vegetationDrawCatalogId);
    assert(catalogHandle.ready() && catalogHandle.get()->data);
    const auto& catalog = *catalogHandle.get()->data;
    auto desc = tests::terrainVegetationGeometryDescFixture();
    desc.maxTiles = std::size_t(terrain.get()->tileCountX()) * terrain.get()->tileCountZ();
    desc.maxInstances = 0u;
    for (std::uint32_t z = 0; z < terrain.get()->tileCountZ(); ++z)
        for (std::uint32_t x = 0; x < terrain.get()->tileCountX(); ++x)
            desc.maxInstances += terrain.get()->vegetationFor({int(x), int(z)})->size();
    desc.maxCatalogEntries = desc.maxPrototypes = catalog.assets.size();
    desc.maxVertices = desc.maxIndices = desc.maxSections = 0u;
    for (const auto& geometry : catalog.templates) {
        desc.maxVertices += geometry.vertices.size();
        desc.maxIndices += geometry.indices.size();
        desc.maxSections += geometry.sections.size();
    }
    renderer::TerrainVegetationBinding binding;
    assert(binding
            .bind(resources,
                tests::vegetationDrawTerrainId,
                terrain.generation(),
                {100, 5, -200},
                {desc.maxTiles, desc.maxInstances})
            .success);
    Preparation owner;
    const auto prepared = owner.prepare(binding.snapshot(), desc);
    if (!prepared.success)
        std::fprintf(stderr, "%s\n", prepared.diagnostics.c_str());
    assert(prepared.success && !owner.snapshot()->prototypes.empty());
    check(*owner.snapshot(), binding.snapshot());
    auto geometry = owner.snapshot()->prototypes.front().geometry;
    auto asset = owner.snapshot()->prototypes.front().asset;
    std::weak_ptr<const Catalog> weak = binding.snapshot()->catalog;
    assert(resources.unload(tests::vegetationDrawCatalogId).success);
    owner.clear();
    binding.clear();
    assert(!weak.expired() && !geometry->vertices.empty() && !asset->materials.empty());
    geometry.reset();
    asset.reset();
    assert(weak.expired());
    std::puts(
        "Fresh-process cooked world -> exact geometry/section/material-ID mapping PASS; no GPU draws");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2)
        packageSmoke(argv[1]);
    else {
        validationContracts();
        lookupAndOwnershipContracts();
        std::puts("Terrain vegetation geometry mapping contracts PASS (CPU only)");
    }
}
