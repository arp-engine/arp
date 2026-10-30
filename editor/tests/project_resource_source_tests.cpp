#include "3d/authoring_components.h"
#include "3d/components.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/project/project_resource_sources.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/tests/fixtures/project_source_acquisition_fixture.h"
#include "editor/workspace/editor_project_resources.h"
#include "resource/asset_package_closure.h"
#include "scene/camera.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace core::io;
constexpr std::size_t maxBytes = 1024 * 1024;
constexpr std::uint64_t epoch = 17;
constexpr const char* sceneId = "00000000-0000-4000-8000-000000000097";

// The real scene exporter produces this explicit closure. Its file list seeds
// backends and negative cases; normal acquisition uses the production planner.
class Fixture final {
public:
    explicit Fixture(const std::filesystem::path& directory, bool generate = true)
        : root(directory / "workspace")
    {
        std::filesystem::create_directories(root / "scripts");
        project.id = "00000000-0000-4000-8000-000000000094";
        project.name = "Project source resources";
        project.engineContract = "arp.scene-asset/2";
        project.entryScene = sceneId;
        project.editorMetadata = "editor.yaml";
        project.physics2d.emplace();
        // Tiny cold-scene fixture budgets; no simulation or load testing.
        auto& physics = *project.physics2d;
        physics.maxBodies = 8;
        physics.maxColliders = 16;
        physics.maxJoints = 4;
        physics.maxNativeShapes = 32;
        physics.maxEvents = physics.maxQueryResults = 32;
        physics.maxChainPoints = 16;
        ProjectScriptModule module;
        module.id = "gameplay";
        module.resourceId = "00000000-0000-4000-8000-000000000096";
        module.source = "scripts/gameplay.arp";
        module.schemaVersion = 1;
        project.scriptModules.push_back(std::move(module));
        core::pmr::String diagnostic;
        if (generate) {
            const bool prepared = ProjectSceneAuthoring::prepareTemplate(
                root, project, ProjectTemplateKind::Game3D, diagnostic);
            if (!prepared)
                std::fprintf(stderr, "template: %s\n", diagnostic.c_str());
            assert(prepared);
        } else
            project.assetPackages.push_back("assets/main");
        ProjectDiagnostic issue;
        const bool encoded = ProjectManifestCodec::encode(project, yaml, issue);
        if (!encoded)
            std::fprintf(stderr, "manifest: %s %s\n", issue.code, issue.field.c_str());
        assert(encoded);
        if (generate) {
            write("project.arp.yaml", yaml);
            write("editor.yaml",
                "type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: []\n");
            write("scripts/gameplay.arp",
                "\xef\xbb\xbf// Never execute during resource preparation\r\n");
        } else
            assert(bytes(core::pmr::String("project.arp.yaml")) == yaml);
        paths = {"project.arp.yaml", "editor.yaml", "scripts/gameplay.arp"};
        resource::ResourceManager manager;
        const auto factory = ProjectSceneAuthoring::factory(manager, {}, diagnostic);
        const auto codecs = ProjectSceneAuthoring::packageCodecs(factory, diagnostic);
        const auto closure =
            resource::AssetPackageClosureService(codecs).inspect(root / "assets/main");
        assert(closure.success);
        paths.push_back("assets/main/manifest.arpkg.yaml");
        paths.push_back("assets/main/" + closure.package.closurePath);
        for (const auto& entry : closure.package.entries) {
            paths.push_back("assets/main/" + entry.envelopePath);
            envelope = paths.back();
        }
        for (const auto& file : closure.files)
            paths.push_back("assets/main/" + file.path);
        assert(!envelope.empty());
        std::sort(paths.begin(), paths.end());
        assert(std::adjacent_find(paths.begin(), paths.end()) == paths.end());
        assert(native.load(root).success && native.packageProvenanceBound());
    }

    void write(std::string_view relative, std::string_view value) const
    {
        std::ofstream stream(root / std::filesystem::u8path(relative), std::ios::binary);
        stream.write(value.data(), value.size());
        assert(stream.good());
    }

    core::pmr::String bytes(const core::pmr::String& relative) const
    {
        const auto read = core::readBinaryFile(root / std::filesystem::u8path(relative), maxBytes);
        assert(read.success);
        return {reinterpret_cast<const char*>(read.bytes.data()),
            read.bytes.size(),
            &core::defaultCpuMemoryResource()};
    }

    core::SharedPtr<MemoryFileBackend> memory(unsigned mutation = 0) const
    {
        auto result = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{64, 4 * maxBytes, 1, maxBytes, 1024});
        core::pmr::Vector<core::pmr::String> directories{"workspace"};
        for (const auto& relative : paths) {
            auto parent = (std::filesystem::path("workspace") / std::filesystem::u8path(relative))
                              .parent_path();
            while (!parent.empty()) {
                const auto name = core::FilePath::utf8(parent);
                if (std::find(directories.begin(), directories.end(), name) == directories.end())
                    directories.push_back(name);
                parent = parent.parent_path();
            }
        }
        std::sort(directories.begin(), directories.end());
        for (const auto& dir : directories)
            assert(result->registerDirectory(dir) == FileIoStatus::Success);
        for (const auto& relative : paths) {
            auto data = bytes(relative);
            if (mutation == 1 && relative == envelope)
                data = "type: BrokenEnvelope\n";
            if (mutation == 2 && relative == "editor.yaml")
                data = "type: arp.editor-project\ncontractVersion: 99\nterrainPreviews: []\n";
            if (mutation == 3 && relative == "scripts/gameplay.arp")
                data = "\xff";
            if (mutation == 4 && relative == "project.arp.yaml")
                data = "type: arp.project\ncontractVersion: 99\n";
            const auto path = "workspace/" + relative;
            assert(result->registerFile(
                       path, reinterpret_cast<const std::uint8_t*>(data.data()), data.size())
                == FileIoStatus::Success);
        }
        return result;
    }

    std::filesystem::path root;
    ProjectManifest project;
    core::pmr::String yaml;
    core::pmr::String envelope;
    core::pmr::Vector<core::pmr::String> paths;
    EditorProjectResources native;
};

class Harness final {
public:
    explicit Harness(core::SharedPtr<ManagedFileBackend> backend)
    {
        mount = mounts.mount("workspace", std::move(backend));
        assert(mount);
    }

    core::SharedPtr<const ProjectSourceSnapshot> acquire(
        const Fixture& fixture, bool omit = false, bool condition = false)
    {
        ProjectSourceSession session(
            files, {mount, "workspace", epoch}, {64, maxBytes, 4 * maxBytes});
        for (const auto& relative : fixture.paths) {
            if (omit && relative == fixture.envelope)
                continue;
            FileVersionToken expected;
            if (condition) {
                expected.kind = FileVersionKind::ContentSha256;
                const auto digest = core::Sha256::digest(fixture.bytes(relative));
                std::copy(digest.begin(), digest.end(), expected.digest.begin());
            }
            const auto status =
                session.read(relative, maxBytes, expected, condition ? maxBytes : 0);
            if (status != FileIoStatus::Accepted)
                std::fprintf(stderr,
                    "submit %s: %u %s\n",
                    relative.c_str(),
                    unsigned(status),
                    session.diagnostic());
            assert(status == FileIoStatus::Accepted);
            unified_io_fixture::wait(queue, [&] {
                return session.state() != ProjectSourceState::Pending;
            });
            if (session.state() != ProjectSourceState::Ready)
                std::fprintf(stderr, "acquire %s: %s\n", relative.c_str(), session.diagnostic());
            assert(session.state() == ProjectSourceState::Ready);
        }
        const auto snapshot = session.finish(epoch);
        assert(snapshot);
        return snapshot;
    }

    core::SharedPtr<const ProjectSourceSnapshot> acquirePlanned(
        const Fixture& fixture, bool condition)
    {
        const auto snapshot = editor::tests::ProjectSourceAcquisitionFixture::acquire(files,
            queue,
            {mount, "workspace", epoch},
            {64, maxBytes, 4 * maxBytes},
            4 * maxBytes,
            [&](std::string_view relative) {
                FileVersionToken expected;
                if (condition) {
                    expected.kind = FileVersionKind::ContentSha256;
                    const auto digest =
                        core::Sha256::digest(fixture.bytes(core::pmr::String(relative)));
                    std::copy(digest.begin(), digest.end(), expected.digest.begin());
                }
                return expected;
            });
        assert(snapshot->size() == fixture.paths.size());
        return snapshot;
    }

    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{2, 1, maxBytes, 64, 1024}};
    FileService files{queue, mounts};
    core::PoolHandle mount;
};

void check(const Fixture& fixture, Harness& host, bool condition)
{
    const auto snapshot = host.acquirePlanned(fixture, condition);
    const auto reads = host.files.telemetry().submitted;
    EditorProjectResources candidate;
    const auto loaded = candidate.loadSources(snapshot, host.files, epoch);
    if (!loaded.success)
        std::fprintf(stderr, "load: %s\n", loaded.diagnostics.c_str());
    assert(loaded.success && loaded.packageCount == 1 && loaded.physicsWorld2DReady);
    assert(candidate.sourceSnapshot() == snapshot && candidate.packageProvenanceBound());
    assert(ProjectPackageSnapshot::equal(
        candidate.packageBindings(), fixture.native.packageBindings()));
    assert(ProjectInputSnapshot::equal(candidate.inputBindings(), fixture.native.inputBindings()));
    const auto actual = candidate.manager().find<scene::SceneAssetResource>(sceneId);
    const auto expected = fixture.native.manager().find<scene::SceneAssetResource>(sceneId);
    assert(actual.ready() && expected.ready() && actual.get()->yaml() == expected.get()->yaml());
    core::pmr::String diagnostic;
    const auto factory =
        ProjectSceneAuthoring::factory(candidate.manager(), candidate.physicsWorld2D(), diagnostic);
    auto scene = actual.get()->instantiate(
        candidate.manager(), *factory, diagnostic, scene::SceneExecutionMode::Passive);
    assert(scene && scene->objects().size() == 3);
    unsigned cameras = 0, lights = 0, skies = 0;
    for (const auto& object : scene->objects()) {
        cameras += object->getComponent<scene::CameraComponent>() != nullptr;
        lights += object->getComponent<scene3d::LightComponent>() != nullptr;
        skies += object->getComponent<scene3d::SkyComponent>() != nullptr;
    }
    assert(cameras == 1 && lights == 1 && skies == 1);
    ProjectResourceSources sources;
    assert(sources.capture(snapshot, host.files, epoch, 4 * maxBytes, diagnostic));
    FileReadRequest request;
    const auto& locations = candidate.resourceLocations();
    assert(!locations.resolveSource(
        sceneId, "SceneAssetResource", candidate.manager(), sources, host.files, epoch, request));
    assert(request.pathScope == FilePathScope::MountedResource && request.mount == host.mount
        && request.expectedVersion.kind == FileVersionKind::ContentSha256);
    ProjectResourceLocations::Definition definition;
    assert(!locations.identifySource(
        request, candidate.manager(), sources, host.files, epoch, definition));
    assert(definition.resourceId == sceneId && definition.resourceType == "SceneAssetResource");
    std::filesystem::path untouched("sentinel");
    assert(locations.resolve(sceneId, "SceneAssetResource", candidate.manager(), untouched)
        && untouched == "sentinel");
    const auto keep = candidate.retainManager();
    const auto physics = candidate.physicsWorld2D();
    assert(!candidate.loadSources(snapshot, host.files, epoch + 1).success);
    assert(candidate.retainManager() == keep && candidate.physicsWorld2D() == physics
        && actual.ready());
    assert(locations.resolveSource(
        sceneId, "WrongType", candidate.manager(), sources, host.files, epoch, request));
    resource::ResourceManager wrong;
    assert(locations.resolveSource(
        sceneId, "SceneAssetResource", wrong, sources, host.files, epoch, request));
    assert(host.files.telemetry().submitted == reads);
    if (condition) {
        // A new native acquisition has the same mount/epoch but different raw
        // definition bytes. Locations must reject it before returning a request.
        const auto original = fixture.bytes(fixture.envelope);
        fixture.write(fixture.envelope, original + "\n# external edit\n");
        const auto changed = host.acquire(fixture, false, true);
        ProjectResourceSources fresh;
        assert(fresh.capture(changed, host.files, epoch, 4 * maxBytes, diagnostic));
        const auto previous = request.path;
        assert(locations.resolveSource(
            sceneId, "SceneAssetResource", candidate.manager(), fresh, host.files, epoch, request));
        assert(request.path == previous);
        fixture.write(fixture.envelope, original);
    }
}

void negatives(const Fixture& fixture)
{
    Harness good(fixture.memory());
    auto original = good.acquire(fixture);
    ProjectResourceSources sources;
    core::pmr::String diagnostic;
    assert(sources.capture(original, good.files, epoch, 4 * maxBytes, diagnostic));
    auto context = sources.context(4 * maxBytes);
    const auto planned = EditorProjectResourcePlanner::plan(
        "workspace", context, {64, maxBytes, 4 * maxBytes}, 4 * maxBytes);
    assert(planned.success && planned.reads.empty());
    const auto submitted = good.files.telemetry().submitted;
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto bad = context;
        auto budget = ProjectInputBudget{64, maxBytes, 4 * maxBytes};
        if (invalid == 0)
            bad.resources.requirePreloadedSources = false;
        if (invalid == 1)
            bad.origin.mount = {};
        if (invalid == 2)
            budget.maxFiles = 1;
        if (invalid == 3)
            bad.resources.maxDecodedBytes = 1;
        const auto rejected =
            EditorProjectResourcePlanner::plan("workspace", bad, budget, 4 * maxBytes);
        assert(!rejected.success && rejected.reads.empty() && !rejected.diagnostics.empty());
    }
    assert(good.files.telemetry().submitted == submitted);
    EditorProjectResources candidate;
    assert(candidate.loadSources(original, good.files, epoch).success);
    const auto keep = candidate.retainManager();
    const auto physics = candidate.physicsWorld2D();
    const auto bindings = candidate.packageBindings();
    {
        auto missing = good.acquire(fixture, true);
        assert(!candidate.loadSources(missing, good.files, epoch).success);
        assert(candidate.retainManager() == keep && candidate.physicsWorld2D() == physics);
    }
    for (unsigned mutation = 1; mutation <= 4; ++mutation) {
        Harness changed(fixture.memory(mutation));
        auto snapshot = changed.acquire(fixture);
        ProjectResourceSources changedSources;
        assert(changedSources.capture(snapshot, changed.files, epoch, 4 * maxBytes, diagnostic));
        const auto plan = EditorProjectResourcePlanner::plan("workspace",
            changedSources.context(4 * maxBytes),
            {64, maxBytes, 4 * maxBytes},
            4 * maxBytes);
        if (mutation == 3)
            assert(plan.success
                && plan.reads.empty()); // Source text acceptance belongs to loadSources.
        else
            assert(!plan.success && plan.reads.empty());
        assert(!candidate.loadSources(snapshot, changed.files, epoch).success);
        assert(candidate.retainManager() == keep && candidate.physicsWorld2D() == physics
            && ProjectPackageSnapshot::equal(bindings, candidate.packageBindings()));
    }
    for (const auto bytes : {1u, 512u, 4096u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            EditorProjectResources bounded;
            assert(bounded.loadSources(original, good.files, epoch).success);
            const auto owner = bounded.retainManager();
            const auto world = bounded.physicsWorld2D();
            bool rejected = false;
            {
                core::CpuMemoryQuotaScope scope(quota);
                try {
                    rejected = !bounded.loadSources(original, good.files, epoch).success;
                } catch (const std::bad_alloc&) {
                    // Exhaustion may also prevent owning a diagnostic string.
                    rejected = true;
                }
            }
            assert(
                rejected && bounded.retainManager() == owner && bounded.physicsWorld2D() == world);
            assert(bounded.sourceSnapshot() == original
                && ProjectPackageSnapshot::equal(bindings, bounded.packageBindings()));
        }
        assert(quota->liveBytes() == 0);
    }
    assert(good.mounts.unmount(good.mount));
    assert(!candidate.loadSources(original, good.files, epoch).success
        && candidate.retainManager() == keep);
}
} // namespace

int main(int argc, char** argv)
{
    unified_io_fixture::Workspace workspace;
    if (argc == 3 && core::StringView(argv[1]) == "--write-fixtures") {
        Fixture fixture(argv[2]);
        return 0;
    }
#if defined(__EMSCRIPTEN__)
    // Host-exported golden bytes. MEMFS cannot publish directories exclusively;
    // fixture preparation must not weaken that production capability gate.
    Fixture fixture("/project-resource-fixture", false);
#else
    Fixture fixture(workspace.root);
#endif
    const bool http = argc == 3 && core::StringView(argv[1]) == "--http";
    const bool native = argc == 2 && core::StringView(argv[1]) == "--native";
    const bool smoke = argc == 2 && core::StringView(argv[1]) == "--smoke";
    assert(argc == 1 || http || native || smoke);
    const auto run = [&] {
        core::SharedPtr<ManagedFileBackend> backend;
        if (http) {
            HttpFileBackendDesc desc;
            desc.baseUrl = argv[2];
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 2 * maxBytes;
            desc.maxPathBytes = 1024;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 4096;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 2;
            backend = core::makeShared<HttpFileBackend>(desc);
        } else if (native) {
            auto desc = workspace.descriptor();
            desc.maxBytesInFlight = 2 * maxBytes;
            desc.ioChunkBytes = 4096;
            backend = core::makeShared<NativeDirectoryFileBackend>(desc);
        } else
            backend = fixture.memory();
        Harness host(std::move(backend));
        check(fixture, host, native);
    };
    if (!smoke && !http && !native) {
        run();
        negatives(fixture);
    } else {
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Project resources 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS source-backed project resources, closed scene, raw provenance, explicit locations and failure preservation");
}
