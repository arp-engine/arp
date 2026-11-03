#include "core/io/file_io.h"
#include "resource/asset_package_closure.h"
#include "resource/asset_package_payload.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_cooked.h"
#include "scene/scene_asset_package.h"
#include "scene/scene_asset_resource.h"
#include "scene/scene_component_persistence.h"
#include "tests/fixtures/cooked_vegetation_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace resource;

AssetPackageOutputBudget budget()
{
    return {1, 4, 65536, 4 * 65536, 16, 8 * 65536, 4 * 1024 * 1024};
}

InternalAssetPackageDesc package()
{
    InternalAssetPackageDesc result;
    result.name = "PreparedOutput/1";
    result.status = "Contract Complete";
    for (unsigned i = 0; i < 2; ++i) {
        ResourceEnvelopeBuildDesc desc;
        desc.resourceId =
            i ? "00000000-0000-4000-8000-000000000092" : "00000000-0000-4000-8000-000000000091";
        desc.resourceType = "OutputProbeResource";
        desc.status = "Contract Complete";
        if (i)
            desc.dependencies.push_back({result.entries.front().resourceId, "input"});
        const auto built =
            buildResourceEnvelopeFromYaml(desc, "type: OutputProbeResource\nfields: {value: 7}\n");
        assert(built.success);
        InternalAssetPackageEntry entry;
        entry.resourceId = desc.resourceId;
        entry.resourceType = desc.resourceType;
        entry.status = desc.status;
        entry.dependencies = desc.dependencies;
        entry.envelopePath = i ? "resources/b.arpres.yaml" : "resources/a.arpres.yaml";
        entry.envelopeYaml = built.envelopeYaml;
        entry.contentHash = built.contentHash;
        result.entries.push_back(std::move(entry));
    }
    return result;
}

void check(bool value, const AssetPackageOutputResult& result)
{
    if (!value)
        std::fprintf(stderr,
            "%s %s\n",
            result.diagnosticId ? result.diagnosticId : "no diagnostic",
            result.diagnostics.c_str());
    assert(value);
}

void contract(const InternalAssetPackageDesc& input)
{
    InternalAssetPackageWriter writer;
    auto prepared = writer.prepare(input, budget());
    check(prepared.success && prepared.files.size() == 3, prepared);
    assert(prepared.files.front().role == AssetPackageOutputRole::Manifest);
    for (std::size_t i = 1; i < prepared.files.size(); ++i)
        assert(prepared.files[i - 1].path < prepared.files[i].path
            && prepared.files[i].role == AssetPackageOutputRole::Envelope);

    // The original producer is the byte oracle, not another manifest builder.
    unified_io_fixture::Workspace workspace;
    const auto originalRoot = workspace.root / "original";
    const auto original = writer.write(input, originalRoot);
    assert(original.success);
    for (const auto& file : prepared.files) {
        const auto bytes = core::readBinaryFile(
            originalRoot / std::filesystem::path(file.path), budget().maxMetadataFileBytes);
        assert(bytes.success && bytes.bytes == file.bytes);
    }

    const auto reject = [&](const InternalAssetPackageDesc& desc, AssetPackageOutputBudget limits) {
        const auto failed = writer.prepare(desc, limits);
        check(!failed.success && failed.files.empty() && failed.diagnosticId, failed);
    };
    auto limits = budget();
    limits.contractVersion = 2;
    reject(input, limits);
    limits = budget();
    limits.maxResources = 1;
    reject(input, limits);
    limits = budget();
    limits.maxMetadataFileBytes = 1;
    reject(input, limits);
    limits = budget();
    limits.maxMetadataTotalBytes = input.entries.front().envelopeYaml.size();
    reject(input, limits);
    limits = budget();
    limits.maxOutputFiles = 2;
    reject(input, limits);
    limits = budget();
    limits.maxOutputBytes = 1;
    reject(input, limits);
    limits = budget();
    limits.maxPreparationBytes = 1;
    reject(input, limits);
    for (const auto path : {"../escape", "manifest.arpkg.yaml", "resources/a.arpres.yaml/child"}) {
        auto bad = input;
        bad.entries.back().envelopePath = path;
        reject(bad, budget());
    }
    auto bad = input;
    bad.entries.front().contentHash ^= 1;
    reject(bad, budget());
    bad = input;
    bad.entries.back().resourceId = bad.entries.front().resourceId;
    reject(bad, budget());
    bad = input;
    bad.entries.back().envelopePath = bad.entries.front().envelopePath;
    reject(bad, budget());
    bad = input;
    bad.entries.front().envelopeYaml += "\n---\nextra: document\n";
    reject(bad, budget());
    bad = input;
    bad.name = "name\nignored: scalar injection";
    reject(bad, budget());
    bad = input;
    bad.closurePath = "resources";
    reject(bad, budget());

    // An inner preparation budget must not bypass a smaller caller quota.
    const auto small = core::makeShared<core::CpuMemoryQuota>(32768);
    {
        core::CpuMemoryQuotaScope caller(small);
        reject(input, budget());
    }
    assert(small->liveBytes() == 0);

    // Output bytes remain independently owned, including an individual file
    // moved out after both the caller scope and enclosing result have retired.
    const auto callerQuota =
        core::makeShared<core::CpuMemoryQuota>(budget().maxPreparationBytes + 65536);
    {
        AssetPackageOutputFile retained;
        {
            core::CpuMemoryQuotaScope caller(callerQuota);
            auto result = writer.prepare(input, budget());
            check(result.success, result);
            retained = std::move(result.files.front());
        }
        assert(!retained.bytes.empty() && callerQuota->liveBytes() >= budget().maxPreparationBytes);
        // Destruction retires the allocation. Assigning an empty PMR string
        // may retain its capacity and therefore correctly retain the quota.
    }
    assert(callerQuota->liveBytes() == 0);
    std::puts(
        "PASS metadata bytes match original writer; budgets, paths, identities, YAML and quota ownership reject atomically");
}

void ordinary(const InternalAssetPackageDesc& input)
{
    InternalAssetPackageWriter writer;
    {
        const auto warm = writer.prepare(input, budget());
        check(warm.success, warm);
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        {
            const auto value = writer.prepare(input, budget());
            check(value.success, value);
        }
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::printf("Metadata preparation ordinary20: core bytes=%llu live=%llu delta=0\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount));
}

class SceneOutputFixture final {
public:
    SceneOutputFixture()
        : factory(core::makeShared<scene::SceneComponentFactory>()),
          codecs(core::makeShared<AssetPackageCodecRegistry>(2))
    {
        assert(scene::registerScenePersistence(*factory));
        assert(scene::registerSceneAssetPackageCodecs(*codecs, factory, diagnostic));
        assert(codecs->freeze(diagnostic));
        scene::Scene authored(
            "prepared source", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
        authored.createObject("actor");
        assert(scene::SceneDocumentResource::encode(
            authored, *factory, "arp.scene-asset", id, document, diagnostic));
        ResourceEnvelopeBuildDesc desc;
        desc.resourceId = id;
        desc.resourceType = "SceneAssetResource";
        desc.status = "Contract Complete";
        const auto envelope = buildResourceEnvelopeFromYaml(desc,
            "type: arp.scene-package\ncontractVersion: 1\ndocumentPath: scene.yaml\n"
            "maxDocumentBytes: 65536\nmaxObjects: 64\n");
        assert(envelope.success);
        package.name = "PreparedScene/1";
        package.status = "Contract Complete";
        package.closurePath = "closure.yaml";
        InternalAssetPackageEntry entry;
        entry.resourceId = id;
        entry.resourceType = desc.resourceType;
        entry.status = desc.status;
        entry.contentHash = envelope.contentHash;
        entry.envelopePath = "resources/scene.arpres.yaml";
        entry.envelopeYaml = envelope.envelopeYaml;
        package.entries.push_back(std::move(entry));
    }

    ResourceSourceSet sources(const std::filesystem::path& root) const
    {
        ResourceSourceSet result;
        ResourceSourceBuffer file;
        file.path = core::FilePath::utf8(root / "scene.yaml");
        file.pathScope = core::io::FilePathScope::HostFilesystem;
        file.bytes.assign(document.begin(), document.end());
        assert(result.add(std::move(file)));
        return result;
    }

    AssetPackageSourceContext context(const ResourceSourceSet& inputs) const
    {
        AssetPackageSourceContext result;
        result.resources.sources = &inputs;
        result.resources.requirePreloadedSources = true;
        result.resources.maxDecodedBytes = 65536;
        return result;
    }

    static constexpr const char* id = "00000000-0000-4000-8000-000000000093";
    AssetPackageClosureBudget payloadBudget{4, 4, 65536, 65536, 65536};
    core::SharedPtr<scene::SceneComponentFactory> factory;
    core::SharedPtr<AssetPackageCodecRegistry> codecs;
    core::pmr::String document, diagnostic;
    InternalAssetPackageDesc package;
};

void sourceContract(SceneOutputFixture& fixture, bool smoke)
{
    const std::filesystem::path logicalRoot("acquired-package-output");
    assert(!std::filesystem::exists(logicalRoot));
    AssetPackageClosureService producer(fixture.codecs);
    const auto run = [&] {
        auto inputs = fixture.sources(logicalRoot);
        auto context = fixture.context(inputs);
        auto result = producer.prepare(
            fixture.package, logicalRoot, context, fixture.payloadBudget, budget());
        check(result.success && result.files.size() == 4, result);
        inputs.clear();
        // The exact output can be loaded after all input buffers retire.
        ResourceSourceSet retained;
        for (const auto& file : result.files) {
            ResourceSourceBuffer source;
            source.path = core::FilePath::utf8(logicalRoot / std::filesystem::path(file.path));
            source.pathScope = core::io::FilePathScope::HostFilesystem;
            source.bytes = file.bytes;
            assert(retained.add(std::move(source)));
        }
        auto verifiedContext = fixture.context(retained);
        ResourceManager loaded;
        const auto verified = InternalAssetPackageReader().loadSources(
            logicalRoot, loaded, verifiedContext, fixture.codecs);
        assert(verified.success && loaded.find<scene::SceneAssetResource>(fixture.id).ready());
    };
    run();
    if (smoke) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Source output ordinary20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        return;
    }
    auto inputs = fixture.sources(logicalRoot);
    const auto context = fixture.context(inputs);
    unified_io_fixture::Workspace workspace;
    const auto originalRoot = workspace.root / "source-original";
    std::filesystem::create_directory(originalRoot);
    workspace.write("source-original/scene.yaml", fixture.document);
    const auto original =
        producer.prepareIndex(fixture.package, originalRoot, fixture.payloadBudget);
    assert(original.success);
    const auto prepared =
        producer.prepare(fixture.package, logicalRoot, context, fixture.payloadBudget, budget());
    check(prepared.success, prepared);
    for (const auto& file : prepared.files) {
        const auto originalBytes = core::readBinaryFile(
            originalRoot / std::filesystem::path(file.path), budget().maxOutputBytes);
        assert(originalBytes.success && originalBytes.bytes == file.bytes);
    }
    const auto reject = [&](AssetPackageSourceContext source,
                            InternalAssetPackageDesc package,
                            AssetPackageClosureBudget payload,
                            AssetPackageOutputBudget output) {
        const auto failed = producer.prepare(package, logicalRoot, source, payload, output);
        check(!failed.success && failed.files.empty() && failed.diagnosticId, failed);
    };
    auto invalidContext = context;
    invalidContext.resources.requirePreloadedSources = false;
    reject(invalidContext, fixture.package, fixture.payloadBudget, budget());
    invalidContext = context;
    invalidContext.origin.pathScope = core::io::FilePathScope::MountedResource;
    invalidContext.origin.mount = {&fixture, 1, 2};
    reject(invalidContext, fixture.package, fixture.payloadBudget, budget());
    // Same scope/owner/slot, a different source generation must still fail.
    ResourceSourceBuffer staleFile = *inputs.sourceAt(0);
    staleFile.pathScope = core::io::FilePathScope::MountedResource;
    staleFile.mount = {&fixture, 1, 1};
    ResourceSourceSet stale;
    assert(stale.add(std::move(staleFile)));
    invalidContext.resources.sources = &stale;
    reject(invalidContext, fixture.package, fixture.payloadBudget, budget());
    unsigned callbackCount = 0;
    invalidContext = context;
    invalidContext.resources.commitGate = {[](void* data) noexcept {
                                               ++*static_cast<unsigned*>(data);
                                               return true;
                                           },
        &callbackCount};
    reject(invalidContext, fixture.package, fixture.payloadBudget, budget());
    assert(callbackCount == 0);
    auto missing = context;
    ResourceSourceSet empty;
    missing.resources.sources = &empty;
    reject(missing, fixture.package, fixture.payloadBudget, budget());
    auto payload = fixture.payloadBudget;
    payload.maxTotalFileBytes = 1;
    reject(context, fixture.package, payload, budget());
    for (const auto field : {&AssetPackageClosureBudget::maxFiles,
             &AssetPackageClosureBudget::maxFileBytes,
             &AssetPackageClosureBudget::maxTotalFileBytes,
             &AssetPackageClosureBudget::maxDecodedBytes}) {
        payload = fixture.payloadBudget;
        payload.*field = SIZE_MAX;
        reject(context, fixture.package, payload, budget());
    }
    auto output = budget();
    output.maxOutputFiles = 3;
    reject(context, fixture.package, fixture.payloadBudget, output);
    auto bad = fixture.package;
    bad.closurePath = "resources/scene.arpres.yaml/child";
    reject(context, bad, fixture.payloadBudget, budget());
    bad = fixture.package;
    bad.entries.front().contentHash ^= 1;
    reject(context, bad, fixture.payloadBudget, budget());
    bad = fixture.package;
    auto envelope = core::YamlContract::parseSingleDocument(bad.entries.front().envelopeYaml);
    envelope["version"] = 99;
    bad.entries.front().envelopeYaml = core::YamlContract::serialize(envelope);
    reject(context, bad, fixture.payloadBudget, budget());
    ResourceSourceSet emptyPayload;
    auto emptyFile = *inputs.sourceAt(0);
    emptyFile.bytes.clear();
    assert(emptyPayload.add(std::move(emptyFile)));
    invalidContext = context;
    invalidContext.resources.sources = &emptyPayload;
    reject(invalidContext, fixture.package, fixture.payloadBudget, budget());
    output = budget();
    output.maxPreparationBytes = 1;
    reject(context, fixture.package, fixture.payloadBudget, output);
    assert(!std::filesystem::exists(logicalRoot));
    std::puts(
        "PASS complete source-only Scene package equals Native producer; missing/generation/budget/collision failures publish no files");
}

#if !defined(__EMSCRIPTEN__)
// Original domain producers supply the oracle, including a declared zero-byte
// vegetation block. The prepared result must independently decode all three types.
void terrainSourceContract(bool smoke)
{
    using F = tests::CookedVegetationFixture;
    unified_io_fixture::Workspace workspace;
    const auto nativeRoot = workspace.root / "terrain-original";
    std::filesystem::create_directory(nativeRoot);
    const ResourceReference catalogRef{
        F::catalogId, "VegetationCatalogResource", {}, "Contract Complete", {}};
    const auto cooked = VegetationCatalogCookedCodec().write(
        catalogRef, F::catalog(), nativeRoot / "catalog", F::geometryBytes);
    assert(cooked.success);
    TerrainAssetBuildDesc terrain;
    terrain.version = TerrainAssetVersion::V2;
    terrain.hasSurfaceWeights = false;
    terrain.resourceId = "00000000-0000-4000-8000-000000000094";
    terrain.vegetationCatalogResourceId = F::catalogId;
    terrain.bounds = {0, 2, 0, 2};
    terrain.tileCountX = terrain.tileCountZ = 1;
    terrain.samplesPerEdge = 2;
    terrain.tileWorldSize = 2;
    TerrainAssetBuildTile tile;
    tile.heights = {1, 3, 5, 7};
    terrain.tiles.push_back(std::move(tile));
    assert(TerrainAssetWriter().write(terrain, nativeRoot).success);
    // Rebind the explicit fixture metadata so the legitimate empty payload sorts
    // last, independently of the producer's content-addressed filename ordering.
    using Y = core::YamlContract;
    const auto manifestBytes = core::readTextFile(nativeRoot / "terrain.asset.yaml", 65536);
    assert(manifestBytes.success);
    auto terrainManifest = Y::parseSingleDocument(manifestBytes.text);
    auto& vegetationBlock = terrainManifest["tiles"][0]["vegetation"];
    std::filesystem::rename(nativeRoot / Y::text(vegetationBlock, "path").c_str(),
        nativeRoot / "zz-empty-vegetation.bin");
    vegetationBlock["path"] = "zz-empty-vegetation.bin";
    workspace.write("terrain-original/terrain.asset.yaml", Y::serialize(terrainManifest));

    InternalAssetPackageDesc package;
    package.name = "PreparedTerrain/1";
    package.status = "Contract Complete";
    package.closurePath = "closure.yaml";
    const auto append = [&](const char* id,
                            const char* type,
                            const char* path,
                            core::StringView payload,
                            core::pmr::Vector<ResourceDependency> dependencies) {
        ResourceEnvelopeBuildDesc desc;
        desc.resourceId = id;
        desc.resourceType = type;
        desc.status = "Contract Complete";
        desc.dependencies = dependencies;
        const auto built = buildResourceEnvelopeFromYaml(desc, payload);
        assert(built.success);
        InternalAssetPackageEntry entry;
        entry.resourceId = id;
        entry.resourceType = type;
        entry.status = desc.status;
        entry.envelopePath = path;
        entry.dependencies = std::move(dependencies);
        entry.envelopeYaml = built.envelopeYaml;
        entry.contentHash = built.contentHash;
        package.entries.push_back(std::move(entry));
    };
    append(terrain.resourceId.c_str(),
        "TerrainAssetResource",
        "resources/terrain.yaml",
        "type: TerrainAssetResource\nfields: {manifestPath: terrain.asset.yaml}\n",
        {{F::catalogId, "terrain/vegetation-catalog"}});
    append(F::catalogId,
        "VegetationCatalogResource",
        "resources/catalog.yaml",
        "type: VegetationCatalogResource\nfields: {manifestPath: catalog/catalog.cooked.yaml}\n",
        {{F::materialId, "material"}});
    registerPrefabAssetReflection();
    const auto material = reflection::serialize_to_yaml(reflection::object(MaterialResource{}));
    assert(material.success);
    append(F::materialId, "MaterialResource", "resources/material.yaml", material.yaml, {});
    AssetPackageClosureService producer;
    AssetPackageClosureBudget limits{4, 8, 65536, 65536, 65536};
    auto original = producer.prepareIndex(package, nativeRoot, limits);
    assert(original.success);
    const std::filesystem::path logicalRoot("acquired-terrain-output");
    ResourceSourceSet inputs;
    std::size_t totalBytes = 0;
    bool hasEmpty = false;
    for (const auto& file : original.files) {
        const auto bytes = core::readBinaryFile(nativeRoot / file.path.c_str(), 65536);
        assert(bytes.success);
        totalBytes += bytes.bytes.size();
        hasEmpty |= bytes.bytes.empty();
        ResourceSourceBuffer source;
        source.path = core::FilePath::utf8(logicalRoot / file.path.c_str());
        source.pathScope = core::io::FilePathScope::HostFilesystem;
        source.bytes = bytes.bytes;
        assert(inputs.add(std::move(source)));
    }
    assert(hasEmpty && original.files.back().byteCount == 0);
    // Sorted last is an empty block: the logical remaining byte budget is zero.
    limits.maxTotalFileBytes = totalBytes;
    original = producer.prepareIndex(package, nativeRoot, limits);
    assert(original.success);
    AssetPackageSourceContext context;
    context.resources = {{}, &inputs, true, 65536, {}};
    const auto run = [&] {
        const auto result = producer.prepare(package, logicalRoot, context, limits, budget());
        check(result.success && result.files.size() == original.files.size() + 5, result);
        for (const auto& file : result.files) {
            const auto bytes =
                core::readBinaryFile(nativeRoot / file.path.c_str(), budget().maxOutputBytes);
            assert(bytes.success && bytes.bytes == file.bytes);
        }
    };
    run();
    if (smoke) {
        // Ledger inspection identified shared type metadata and replaceable
        // generated schemas retaining distinct quota owners. Warm both registry
        // lifetimes, then verify every ordinary cycle against the stable baseline.
        run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i) {
            run();
            const auto after = core::defaultCpuAllocator().stats();
            assert(before.liveBytes == after.liveBytes
                && before.allocationCount - before.deallocationCount
                    == after.allocationCount - after.deallocationCount);
        }
        std::printf("PASS terrain source output ordinary20: core bytes=%llu live=%llu delta=0\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount));
    } else {
        --limits.maxTotalFileBytes;
        const auto failed = producer.prepare(package, logicalRoot, context, limits, budget());
        check(!failed.success && failed.files.empty(), failed);
        std::puts(
            "PASS terrain/catalog/material output matches original; real empty block and exact/insufficient total budgets");
    }
    assert(!std::filesystem::exists(logicalRoot));
}
#endif
} // namespace

int main(int argc, char** argv)
{
#if !defined(__EMSCRIPTEN__)
    if (argc == 2
        && (std::string_view(argv[1]) == "--terrain"
            || std::string_view(argv[1]) == "--terrain-smoke")) {
        terrainSourceContract(std::string_view(argv[1]) == "--terrain-smoke");
        return 0;
    }
#endif
    if (argc == 2
        && (std::string_view(argv[1]) == "--sources"
            || std::string_view(argv[1]) == "--source-smoke")) {
        SceneOutputFixture fixture;
        sourceContract(fixture, std::string_view(argv[1]) == "--source-smoke");
        return 0;
    }
    const auto input = package();
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        ordinary(input);
    else
        contract(input);
}
