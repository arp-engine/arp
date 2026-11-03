#include "3d/components.h"
#include "core/hash/sha256.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/project/project_resource_sources.h"
#include "editor/tests/fixtures/project_source_acquisition_fixture.h"
#include "editor/tests/fixtures/terrain_preview_source_fixture.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_terrain_preview.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace core::io;
using Fixture = editor::tests::TerrainPreviewSourceFixture;
constexpr std::uint64_t epoch = 19;
constexpr ProjectInputBudget budget{64, Fixture::maxFileBytes, Fixture::maxTotalBytes};

#if defined(_WIN32)
void legacyReadCycle(unified_io_fixture::Workspace& workspace, const Fixture& fixture)
{
    using Yaml = core::YamlContract;
    auto config = Fixture::preview();
    config["contractVersion"] = "editor.terrain-preview/v1";
    config["vegetation"] = Yaml::Node::mapping();
    config["vegetation"]["input"] = "legacy-source/v2";
    config["vegetation"]["resourceId"] = arp::tests::VegetationCatalogSourceFixture::id;
    config["vegetation"]["catalog"] = "source.yaml";
    workspace.write("workspace/preview.yaml", Yaml::serialize(config));
    const auto runtime = arp::tests::VegetationCatalogSourceFixture::document(true);
    workspace.write("workspace/runtime/catalog.yaml", runtime);
    const auto load = [&](bool expected, const char* expectedDiagnostic = nullptr) {
        resource::ResourceManager candidate;
        core::pmr::String diagnostic;
        const bool loaded =
            loadEditorTerrainPreview(fixture.root, "preview.yaml", candidate, diagnostic);
        if (loaded != expected)
            std::fprintf(stderr, "legacy terrain preview: %s\n", diagnostic.c_str());
        assert(loaded == expected);
        if (expectedDiagnostic)
            assert(diagnostic == expectedDiagnostic);
        if (loaded) {
            const auto preview = candidate.find<EditorTerrainPreviewMaterial>(Fixture::previewId);
            const auto vegetation = candidate.find<resource::VegetationCatalogResource>(
                arp::tests::VegetationCatalogSourceFixture::id);
            assert(preview.ready() && preview.get()->tiles.size() == 2 && vegetation.ready());
        } else
            assert(!diagnostic.empty());
    };
    load(true);
    // Admission fits the original total budget, but the runtime YAML read must
    // reject above its separate 1 MiB limit before attempting model imports.
    workspace.write(
        "workspace/runtime/catalog.yaml", core::pmr::String(Fixture::maxFileBytes + 1, ' '));
    load(false, "EDITOR-TERRAIN-PREVIEW-READ-BUDGET");
    workspace.write("workspace/runtime/catalog.yaml", "fields: [\n");
    load(false);
    workspace.write("workspace/runtime/catalog.yaml", runtime);
    load(true);
}

void legacyReadContract(
    unified_io_fixture::Workspace& workspace, const Fixture& fixture, bool smoke)
{
    legacyReadCycle(workspace, fixture);
    const auto before = core::defaultCpuAllocator().stats();
    const unsigned cycles = smoke ? 20u : 1u;
    for (unsigned i = 0; i < cycles; ++i)
        legacyReadCycle(workspace, fixture);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    const auto runtime = arp::tests::VegetationCatalogSourceFixture::document(true);
    std::printf("Legacy runtime YAML %zu bytes SHA256=%s; two tiles and vegetation ready\n",
        runtime.size(),
        core::Sha256::hexDigest(runtime).c_str());
    std::printf("Legacy runtime read %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        cycles,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
#endif

// Acquires exactly the production planner's dependency set. Fixed acquisition is
// reserved for malformed/missing input tests, so a local file cannot fill a gap.
class Harness final {
public:
    explicit Harness(core::SharedPtr<ManagedFileBackend> backend)
    {
        mount = mounts.mount("terrain", std::move(backend));
        assert(mount);
    }

    core::SharedPtr<const ProjectSourceSnapshot> acquire()
    {
        return editor::tests::ProjectSourceAcquisitionFixture::acquire(
            files, queue, {mount, "workspace", epoch}, budget, Fixture::maxTotalBytes);
    }

    core::SharedPtr<const ProjectSourceSnapshot> fixed(
        const Fixture& fixture, core::StringView omit = {})
    {
        ProjectSourceSession session(files, {mount, "workspace", epoch}, budget);
        for (const auto& path : fixture.paths) {
            if (path == omit)
                continue;
            assert(session.read(path, budget.maxFileBytes) == FileIoStatus::Accepted);
            unified_io_fixture::wait(queue, [&] {
                return session.state() != ProjectSourceState::Pending;
            });
            assert(session.state() == ProjectSourceState::Ready);
        }
        const auto result = session.finish(epoch);
        assert(result);
        return result;
    }

    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{2, 1, Fixture::maxFileBytes, 64, 1024}};
    FileService files{queue, mounts};
    core::PoolHandle mount;
};

core::SharedPtr<MemoryFileBackend> memory(const Fixture& fixture, unsigned mutation = 0)
{
    auto result = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{64, Fixture::maxTotalBytes, 1, Fixture::maxFileBytes, 1024});
    core::pmr::Vector<core::pmr::String> directories{"workspace"};
    for (const auto& file : fixture.files) {
        auto parent = (std::filesystem::path("workspace") / file.path).parent_path();
        while (!parent.empty()) {
            const auto name = core::FilePath::utf8(parent);
            if (std::find(directories.begin(), directories.end(), name) == directories.end())
                directories.push_back(name);
            parent = parent.parent_path();
        }
    }
    std::sort(directories.begin(), directories.end());
    for (const auto& directory : directories)
        assert(result->registerDirectory(directory) == FileIoStatus::Success);
    for (const auto& file : fixture.files) {
        auto bytes = file.bytes;
        if (file.path == "preview.yaml" && mutation) {
            auto config = Fixture::preview();
            auto& vegetation = config["vegetation"];
            if (mutation == 1)
                vegetation["sourceLimits"]["mesh"]["maxNodeVisits"] = 0;
            if (mutation == 2)
                vegetation["resourceId"] = Fixture::previewId;
            if (mutation == 3)
                vegetation["input"] = "legacy-source/v2";
            if (mutation == 4)
                vegetation["sourceLimits"]["mesh"]["colorPolicy"] = "guess";
            if (mutation == 5)
                vegetation["catalog"] = "../outside.yaml";
            if (mutation == 6)
                config["maxDecodedBytes"] = 1;
            if (mutation == 7)
                config["maxPreviewBytes"] = 1;
            if (mutation == 8)
                vegetation["sourceLimits"]["unknownBudget"] = 100;
            if (mutation == 9)
                vegetation["sourceLimits"]["maxCatalogBytes"] = 1;
            if (mutation == 10)
                vegetation["sourceLimits"]["mesh"]["maxVertices"] = 1;
            const auto yaml = Fixture::Yaml::serialize(config);
            bytes.assign(yaml.begin(), yaml.end());
        }
        if ((mutation == 11 && file.path == "source.yaml")
            || (mutation == 12 && file.path == "runtime/catalog.yaml")
            || (mutation == 15 && file.path == "terrain/terrain.asset.yaml"))
            bytes = {0xff};
        if (mutation == 13 && file.path == "runtime/models/plant.glb")
            bytes.front() ^= 0xff;
        if (mutation == 14 && file.path == "map.png")
            bytes.resize(2);
        assert(result->registerFile("workspace/" + file.path, bytes.data(), bytes.size())
            == FileIoStatus::Success);
    }
    return result;
}

void compare(const Fixture& fixture, Harness& host)
{
    const auto snapshot = host.acquire();
    if (snapshot->size() != fixture.paths.size()) {
        std::fprintf(stderr,
            "terrain input count: acquired=%zu expected=%zu\n",
            snapshot->size(),
            fixture.paths.size());
        for (std::size_t i = 0; i < snapshot->size(); ++i)
            std::fprintf(stderr, "acquired: %s\n", snapshot->entryAt(i)->relative.c_str());
        for (const auto& path : fixture.paths)
            std::fprintf(stderr, "expected: %s\n", path.c_str());
    }
    assert(snapshot->size() == fixture.paths.size());
    const auto requests = host.files.telemetry().submitted;
    EditorProjectResources candidate;
    const auto loaded = candidate.loadSources(snapshot, host.files, epoch);
    if (!loaded.success)
        std::fprintf(stderr, "terrain sources: %s\n", loaded.diagnostics.c_str());
    assert(loaded.success && candidate.sourceSnapshot() == snapshot);
    assert(ProjectInputSnapshot::equal(candidate.inputBindings(), fixture.native.inputBindings()));
    assert(host.files.telemetry().submitted == requests);
    const auto& actual = candidate.manager();
    const auto& expected = fixture.native.manager();
    const auto terrain = actual.find<resource::TerrainAssetResource>(Fixture::terrainId);
    assert(terrain.ready() && terrain.get()->tileCountX() == 2);
    const auto* preview = dynamic_cast<const EditorTerrainPreviewMaterial*>(
        actual.find<resource::MaterialResource>(Fixture::previewId).get());
    assert(preview && preview->tiles.size() == 2 && preview->vegetationInstanceCount == 1);
    const auto catalog = actual.find<resource::VegetationCatalogResource>(
        arp::tests::VegetationCatalogSourceFixture::id);
    const auto baseline = expected.find<resource::VegetationCatalogResource>(
        arp::tests::VegetationCatalogSourceFixture::id);
    assert(catalog.ready() && baseline.ready());
    const auto& a = *catalog.get()->data;
    const auto& b = *baseline.get()->data;
    assert(a.assets.size() == 2 && a.templates.size() == b.templates.size());
    for (std::size_t i = 0; i < a.templates.size(); ++i) {
        assert(
            a.assets[i].id == b.assets[i].id && a.assets[i].templateId == b.assets[i].templateId);
        assert(a.templates[i].indices == b.templates[i].indices);
        const auto& av = a.templates[i].vertices;
        const auto& bv = b.templates[i].vertices;
        assert(av.size() == bv.size());
        for (std::size_t j = 0; j < av.size(); ++j)
            assert(std::equal(
                std::begin(av[j].position), std::end(av[j].position), std::begin(bv[j].position)));
    }
    for (const auto& tile : preview->tiles) {
        const auto material = actual.find<resource::MaterialResource>(tile.materialId);
        assert(material.ready());
        for (const auto& id : {material.get()->baseColorTextureId,
                 material.get()->normalTextureId,
                 material.get()->metallicRoughnessTextureId,
                 material.get()->occlusionTextureId,
                 material.get()->emissiveTextureId}) {
            const auto image = actual.find<resource::TextureResource>(id);
            const auto reference = expected.find<resource::TextureResource>(id);
            assert(image.ready() && reference.ready());
            assert(image.get()->decodedWidth == reference.get()->decodedWidth
                && image.get()->decodedHeight == reference.get()->decodedHeight
                && image.get()->decodedSrgb == reference.get()->decodedSrgb
                && image.get()->decodedRgba8 == reference.get()->decodedRgba8);
            const auto& rgba = image.get()->decodedRgba8;
            if (id == material.get()->baseColorTextureId)
                assert(rgba[0] == 70 && rgba[1] == 160 && rgba[2] == 70);
            if (id == material.get()->normalTextureId)
                // The shared baker explicitly maps +Z texture V to -cross(N,T).
                assert(rgba[0] == 128 && rgba[1] == 127 && rgba[2] == 255);
            if (id == material.get()->occlusionTextureId)
                assert(rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255);
        }
    }
    for (const auto& path : fixture.paths) {
        const auto* receipt = snapshot->find(path);
        assert(receipt && receipt->binding.mount == host.mount);
        assert(receipt->receipt.bytes
            == std::find_if(fixture.files.begin(),
                fixture.files.end(),
                [&](const auto& file) {
                    return file.path == path;
                })
                ->bytes);
    }
}

void negatives(const Fixture& fixture)
{
    const auto workingDirectory = std::filesystem::current_path();
    // Make the mounted logical root resolve to real golden files as well. A
    // broken preloaded-source gate would now be observable as false success.
    std::filesystem::current_path(fixture.root.parent_path());
    Harness good(memory(fixture));
    const auto original = good.acquire();
    EditorProjectResources candidate;
    assert(candidate.loadSources(original, good.files, epoch).success);
    const auto owner = candidate.retainManager();
    const auto unchanged = [&] {
        assert(candidate.retainManager() == owner && candidate.sourceSnapshot() == original);
        assert(
            ProjectInputSnapshot::equal(candidate.inputBindings(), fixture.native.inputBindings()));
    };
    // The real fixture still exists on disk. Missing acquired dependencies must
    // fail without reopening it, including either catalog or the shared GLB.
    for (const auto* missing :
        {"source.yaml", "runtime/catalog.yaml", "runtime/models/plant.glb", "map.png"}) {
        const auto incomplete = good.fixed(fixture, missing);
        assert(!candidate.loadSources(incomplete, good.files, epoch).success);
        unchanged();
    }
    for (unsigned mutation = 1; mutation <= 15; ++mutation) {
        Harness bad(memory(fixture, mutation));
        const auto snapshot = bad.fixed(fixture);
        ProjectResourceSources sources;
        core::pmr::String diagnostic;
        assert(sources.capture(snapshot, bad.files, epoch, Fixture::maxTotalBytes, diagnostic));
        if (mutation == 1 || mutation == 3 || mutation == 4 || mutation == 5 || mutation == 8
            || mutation == 9) {
            const auto before = bad.files.telemetry().submitted;
            const auto plan = planEditorTerrainPreviewSources("workspace",
                "preview.yaml",
                sources.context(Fixture::maxTotalBytes),
                {Fixture::maxFileBytes,
                    64,
                    64,
                    Fixture::maxFileBytes,
                    Fixture::maxTotalBytes,
                    Fixture::maxTotalBytes});
            assert(!plan.success && plan.reads.empty() && !plan.diagnostics.empty());
            assert(bad.files.telemetry().submitted == before);
        }
        const auto failed = candidate.loadSources(snapshot, bad.files, epoch);
        assert(!failed.success && !failed.diagnostics.empty());
        unchanged();
    }
    assert(!candidate.loadSources(original, good.files, epoch + 1).success);
    unchanged();
    assert(good.mounts.unmount(good.mount));
    assert(!candidate.loadSources(original, good.files, epoch).success);
    unchanged();
    const auto replacement = good.mounts.mount("terrain", memory(fixture));
    assert(replacement && !(replacement == good.mount));
    assert(!candidate.loadSources(original, good.files, epoch).success);
    unchanged();
    std::filesystem::current_path(workingDirectory);
}

void writeScene(const Fixture& fixture)
{
    EditorSceneDocument document(false);
    core::pmr::String diagnostic;
    assert(fixture.native.bindSceneServices(document, diagnostic));
    document.scene().setName("Mounted terrain scene");
    assert(document.addComponent("CameraComponent", document.createObject("Source Camera")));
    const auto object = document.createObject("Source Terrain");
    auto* terrain =
        dynamic_cast<scene3d::TerrainComponent*>(document.addComponent("TerrainComponent", object));
    assert(terrain);
    terrain->set_resource_id(core::pmr::String(Fixture::terrainId));
    terrain->set_material_resource_id(core::pmr::String(Fixture::previewId));
    assert(document.select(object));
    core::pmr::String yaml;
    assert(EditorSceneAuthoringCodec::encode(document, yaml, diagnostic));
    std::ofstream output(fixture.root / "scene.yaml", std::ios::binary);
    output.write(yaml.data(), yaml.size());
    assert(output.good());
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    if (argc == 3
        && (core::StringView(argv[1]) == "--write-fixtures"
            || core::StringView(argv[1]) == "--write-scene-fixture")) {
        Fixture::generate(argv[2]);
        Fixture fixture(argv[2]);
        if (core::StringView(argv[1]) == "--write-scene-fixture")
            writeScene(fixture);
        return 0;
    }
    const bool http = argc == 4 && core::StringView(argv[1]) == "--http";
    const bool native = argc == 2 && core::StringView(argv[1]) == "--native";
    const bool smoke = argc == 2 && core::StringView(argv[1]) == "--smoke";
    const bool failureSmoke = argc == 2 && core::StringView(argv[1]) == "--failure-smoke";
    const bool legacyRead = argc == 2 && core::StringView(argv[1]) == "--legacy-read";
    const bool legacyReadSmoke = argc == 2 && core::StringView(argv[1]) == "--legacy-read-smoke";
    assert(argc == 1 || http || native || smoke || failureSmoke || legacyRead || legacyReadSmoke);
#if defined(__EMSCRIPTEN__)
    Fixture fixture("/terrain-preview-source-fixture");
#else
    const auto directory = http ? std::filesystem::path(argv[3]) : workspace.root;
    if (!http)
        Fixture::generate(directory);
    Fixture fixture(directory);
#endif
#if defined(_WIN32)
    if (legacyRead || legacyReadSmoke) {
        legacyReadContract(workspace, fixture, legacyReadSmoke);
        return 0;
    }
#else
    assert(!legacyRead && !legacyReadSmoke);
#endif
    const auto run = [&] {
        if (failureSmoke) {
            negatives(fixture);
            return;
        }
        core::SharedPtr<ManagedFileBackend> backend;
        if (http) {
            HttpFileBackendDesc desc;
            desc.baseUrl = argv[2];
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 2 * Fixture::maxFileBytes;
            desc.maxPathBytes = 1024;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 4096;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 2;
            backend = core::makeShared<HttpFileBackend>(desc);
        } else if (native) {
            auto desc = workspace.descriptor();
            desc.maxBytesInFlight = 2 * Fixture::maxFileBytes;
            desc.ioChunkBytes = 4096;
            backend = core::makeShared<NativeDirectoryFileBackend>(desc);
        } else
            backend = memory(fixture);
        Harness host(std::move(backend));
        compare(fixture, host);
    };
    if (!smoke && !http && !native && !failureSmoke) {
        run();
        negatives(fixture);
    } else {
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Terrain sources 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS mounted terrain preview with two tiles, catalog provenance, baked textures and atomic failure preservation");
}
