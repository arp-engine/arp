#include "3d/authoring_components.h"
#include "3d/components.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/workspace/editor_shell.h"
#include "resource/resource_contract.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;

class ExportFixture final {
public:
    ExportFixture()
    {
        root = std::filesystem::temp_directory_path()
            / ("arp-scene-export-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        ProjectDiagnostic diagnostic;
        assert(ProjectManifestCodec::decode(
            tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"),
            manifest,
            diagnostic));
        manifest.entryScene = resource::deriveResourceUuid("scene-export-test", "entry");
        // Explicit small cold world exercises 2D value schemas without borrowing
        // the live project's physics state on a worker.
        manifest.physics2d = physics2d::WorldConfig{{0, -9.8f}, 4, 4, 2, 4, 8, 4, 4, 4, 1.f / 60.f};
    }

    ~ExportFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    static void done(EditorShell& shell, EditorCommandPayload command)
    {
        assert(shell.submitCommand(std::move(command)).status == EditorResponseStatus::Completed);
    }

    void create(EditorShell& shell, std::string_view name)
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
                .ok());
        done(shell,
            CreateProjectCommand{
                core::FilePath::utf8(root / name), manifest, false, ProjectTemplateKind::Game3D});
    }

    static SceneExportResult finish(EditorShell& shell, std::uint64_t id)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            shell.pollTasks();
            const auto response = shell.submitCommand(GetSceneExportTaskQuery{id});
            assert(response.hasSnapshot);
            if (response.snapshot.sceneExportTask.status != SceneExportStatus::Pending)
                return response.snapshot.sceneExportTask;
            std::this_thread::yield();
        }
        assert(false && "export timeout");
        return {};
    }

    static void reject(EditorShell& shell, EditorCommandPayload command, EditorDiagnosticCode code)
    {
        const auto version = shell.stateVersion();
        const auto response = shell.submitCommand(std::move(command));
        assert(response.status != EditorResponseStatus::Completed && response.diagnostic == code);
        assert(version == shell.stateVersion());
    }

    std::filesystem::path root;
    ProjectManifest manifest;
};

void transaction(ExportFixture& fixture)
{
    using F = ExportFixture;
    EditorShell shell;
    fixture.create(shell, "transaction");
    const auto root = shell.projectRoot();
    const auto manifest = core::readTextFile(root / "project.arp.yaml");
    const auto source = core::readTextFile(root / "assets/main/scene.yaml");
    assert(manifest.success && source.success);
    F::reject(
        shell, ExportSceneCommand{"assets/conflict"}, EditorDiagnosticCode::ProjectOperationFailed);
    assert(!std::filesystem::exists(root / "assets/conflict"));
    F::done(shell, DuplicateSceneCommand{});
    F::done(shell, CreateSceneObjectCommand("submitted object"));
    F::done(shell, AddSceneComponentCommand("RigidBody2DComponent"));
    F::done(shell, AddSceneComponentCommand("Collider2DComponent"));
    const auto id = shell.sceneDocument().runtimeSceneId();
    auto* manager = &shell.projectResources();
    const auto world = shell.sceneDocument().physicsWorld2DProvider()();
    const auto physicsBefore = world->telemetry();
    const auto accepted = shell.submitCommand(ExportSceneCommand{"assets/exported"});
    assert(accepted.hasSnapshot && accepted.status == EditorResponseStatus::Completed
        && accepted.snapshot.sceneExportPending
        && accepted.snapshot.sceneExportTask.resourceId == id);
    const auto taskId = accepted.snapshot.sceneExportTask.taskId;
    // Completion polling is the only publication of task state. The draft may
    // change immediately, even if a fast worker already finished its filesystem job.
    F::reject(shell, ExportSceneCommand{"assets/other"}, EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell, NewSceneCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell, DuplicateSceneCommand{}, EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell, CloseProjectCommand{true}, EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell,
        OpenProjectCommand{core::FilePath::utf8(root), true},
        EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell, LoadSceneCommand("x.yaml"), EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell, SaveSceneCommand("x.yaml"), EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell, SaveProjectCommand{}, EditorDiagnosticCode::WorkspaceBusy);
    F::reject(shell, RenameProjectCommand{"blocked"}, EditorDiagnosticCode::WorkspaceBusy);
    F::done(shell, CreateSceneObjectCommand("later edit"));
    const auto result = F::finish(shell, taskId);
    assert(result.status == SceneExportStatus::ExportedUnregistered && result.diagnostic.empty()
        && result.contentHash == accepted.snapshot.sceneExportTask.contentHash
        && result.authoringHash == accepted.snapshot.sceneExportTask.authoringHash
        && result.documentEpoch == accepted.snapshot.sceneExportTask.documentEpoch);
    const auto bytes = core::readTextFile(root / "assets/exported/scene.yaml");
    assert(bytes.success && core::Sha256::hexDigest(bytes.text) == result.contentHash
        && bytes.text.find("submitted object") != core::pmr::String::npos
        && bytes.text.find("later edit") == core::pmr::String::npos);
    assert(shell.sceneDirty()
        && shell.submitCommand(GetEditorStateQuery{}).snapshot.scenePath.empty()
        && &shell.projectResources() == manager && !shell.projectResources().recordSnapshot(id)
        && shell.projectManifest()->entryScene == fixture.manifest.entryScene
        && shell.projectManifest()->assetPackages.size() == 1);
    assert(core::readTextFile(root / "project.arp.yaml").text == manifest.text
        && core::readTextFile(root / "assets/main/scene.yaml").text == source.text
        && world->telemetry().bodies == physicsBefore.bodies
        && world->telemetry().colliders == physicsBefore.colliders);
    // A separate candidate can consume the package; this does not publish it
    // into the live project and does not change the worker's unregistered result.
    auto candidate = *shell.projectManifest();
    candidate.assetPackages.push_back("assets/exported");
    candidate.entryScene = id;
    core::pmr::String yaml;
    ProjectDiagnostic diagnostic;
    assert(ProjectManifestCodec::encode(candidate, yaml, diagnostic));
    EditorProjectResources loaded;
    assert(loaded.loadManifest(root, yaml).success);
    const auto asset = loaded.manager().find<scene::SceneAssetResource>(id);
    assert(asset.ready());
    std::filesystem::path sourceRoot;
    assert(loaded.packageRootForResource(id, sourceRoot)
        && sourceRoot == std::filesystem::canonical(root / "assets/exported"));
    assert(loaded.packageRootForResource(fixture.manifest.entryScene, sourceRoot)
        && sourceRoot == std::filesystem::canonical(root / "assets/main"));
    assert(!loaded.packageRootForResource(
        resource::deriveResourceUuid("missing", "scene"), sourceRoot));
    core::pmr::String explanation;
    const auto factory =
        ProjectSceneAuthoring::factory(loaded.manager(), loaded.physicsWorld2D(), explanation);
    auto restored = asset.get()->instantiate(
        loaded.manager(), *factory, explanation, scene::SceneExecutionMode::Passive);
    assert(restored && restored->findObjectByName("submitted object")
        && !restored->findObjectByName("later edit"));
    // Rejected starts retain the prior completed task and never replace output.
    for (const char* path : {"assets/exported", "../outside", "missing/output", "project.arp.yaml"})
        F::reject(shell, ExportSceneCommand{path}, EditorDiagnosticCode::ProjectOperationFailed);
    assert(shell.submitCommand(GetSceneExportTaskQuery{taskId}).snapshot.sceneExportTask.status
        == SceneExportStatus::ExportedUnregistered);
    assert(core::readTextFile(root / "assets/exported/scene.yaml").text == bytes.text);
    // A staging collision exercises a real asynchronous failure. The unrelated
    // directory must survive, with neither published output nor partial package.
    const auto blocker = root / "assets/blocked.preparation.arp-staging";
    std::filesystem::create_directory(blocker);
    std::ofstream(blocker / "owner.txt") << "other writer";
    const auto failing = shell.submitCommand(ExportSceneCommand{"assets/blocked"});
    assert(failing.hasSnapshot);
    const auto failed = F::finish(shell, failing.snapshot.sceneExportTask.taskId);
    assert(failed.status == SceneExportStatus::Failed && !failed.diagnostic.empty()
        && !std::filesystem::exists(root / "assets/blocked")
        && core::readTextFile(blocker / "owner.txt").text == "other writer");
    // Unknown components are not silently omitted.
    shell.sceneDocument()
        .scene()
        .createObject("unsupported")
        .addComponent<scene3d::TerrainAuthoringComponent>();
    F::reject(shell,
        ExportSceneCommand{"assets/unsupported"},
        EditorDiagnosticCode::ProjectOperationFailed);
    assert(!std::filesystem::exists(root / "assets/unsupported"));
    std::puts(
        "PASS scene export transaction: immutable runtime bytes, independent cold owners, gates, no registration/save, candidate load and exclusive output");
}

void snapshotFailures(ExportFixture& fixture)
{
    resource::ResourceManager resources;
    core::pmr::String diagnostic;
    const auto factory = ProjectSceneAuthoring::factory(resources, {}, diagnostic);
    assert(factory);
    scene::Scene scene(
        "snapshot", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
    const auto id = resource::deriveResourceUuid("export-test", "snapshot");
    ProjectSceneAuthoring::Snapshot value;
    value.document = "unchanged";
    assert(!ProjectSceneAuthoring::snapshot(scene, *factory, "bad", value, diagnostic)
        && value.document == "unchanged");
    assert(ProjectSceneAuthoring::snapshot(scene, *factory, id, value, diagnostic));
    const auto output = fixture.root / "bad-snapshot";
    auto bad = value;
    bad.document += "# tampered\n";
    assert(!ProjectSceneAuthoring::exportSnapshot(bad, *factory, output, diagnostic));
    bad = value;
    bad.document = "type: [broken\n";
    bad.contentHash = core::Sha256::hexDigest(bad.document);
    assert(!ProjectSceneAuthoring::exportSnapshot(bad, *factory, output, diagnostic));
    bad.document = core::pmr::String(ProjectSceneAuthoring::maxSceneBytes + 1, 'x');
    assert(!ProjectSceneAuthoring::exportSnapshot(bad, *factory, output, diagnostic));
    using Y = core::YamlContract;
    auto document = Y::parseSingleDocument(value.document);
    document["resourceId"] = resource::deriveResourceUuid("export-test", "other");
    bad.document = Y::serialize(document);
    bad.contentHash = core::Sha256::hexDigest(bad.document);
    assert(!ProjectSceneAuthoring::exportSnapshot(bad, *factory, output, diagnostic));
    assert(!std::filesystem::exists(output));
    auto& mesh = scene.createObject("mesh").addComponent<scene3d::MeshComponent>();
    assert(ProjectSceneAuthoring::snapshot(scene, *factory, id, value, diagnostic));
    const auto plain = Y::parseSingleDocument(value.document);
    auto rejectProfile = [&](Y::Node root, std::string_view expected) {
        auto invalid = value;
        invalid.document = Y::serialize(root);
        invalid.contentHash = core::Sha256::hexDigest(invalid.document);
        assert(!ProjectSceneAuthoring::exportSnapshot(invalid, *factory, output, diagnostic));
        assert(diagnostic.find(expected) != core::pmr::String::npos);
        assert(!std::filesystem::exists(output)
            && !std::filesystem::exists(std::filesystem::path(
                output.native() + std::filesystem::path(".preparation.arp-staging").native())));
    };
    auto dynamic = plain;
    dynamic["objects"][0]["components"][0]["configuration"] = Y::Node::mapping();
    rejectProfile(dynamic, "CONFIGURATION-INSPECTION-UNSUPPORTED");
    auto unknown = plain;
    unknown["objects"][0]["components"][0]["type"] = "unknown.component/1";
    rejectProfile(unknown, "COMPONENT-INSPECTION-CONTRACT-REQUIRED");
    auto dependency = plain;
    dependency["objects"][0]["components"][0]["fields"]["resourceId"] =
        resource::deriveResourceUuid("export-test", "mesh");
    rejectProfile(dependency, "DEPENDENCY-BINDINGS");
    (void)mesh;
    for (unsigned i = 0; i <= ProjectSceneAuthoring::maxObjects; ++i)
        scene.createObject("object");
    assert(!ProjectSceneAuthoring::snapshot(scene, *factory, id, value, diagnostic));
    assert(diagnostic == "EDITOR-SCENE-EXPORT-BUDGET");
    std::puts(
        "PASS export snapshot rejection: explicit UUID, byte/hash/schema/object budgets, no output on failure");
}

void lifetime(ExportFixture& fixture)
{
    using F = ExportFixture;
    {
        EditorShell shell;
        fixture.create(shell, "lifetime");
    }
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        const auto output = fixture.root / "lifetime/assets/ordinary";
        {
            EditorShell shell;
            assert(shell
                    .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                        fixture.root / "lifetime")
                    .ok());
            F::done(shell, DuplicateSceneCommand{});
            const auto start = shell.submitCommand(ExportSceneCommand{"assets/ordinary"});
            assert(start.hasSnapshot);
            F::done(shell, CreateSceneObjectCommand("edit during output"));
            if (cycle % 2 == 0) {
                const auto result = F::finish(shell, start.snapshot.sceneExportTask.taskId);
                assert(result.status == SceneExportStatus::ExportedUnregistered);
                F::reject(shell,
                    ExportSceneCommand{"assets/ordinary"},
                    EditorDiagnosticCode::ProjectOperationFailed);
                F::done(shell, CloseProjectCommand{true});
            }
            // Odd cycles destroy with work pending; destructor must drain with
            // no live Scene/ResourceManager dependency and preserve the output.
        }
        assert(std::filesystem::is_regular_file(output / "scene.yaml"));
        std::error_code error;
        assert(core::FilePath::contains(fixture.root, std::filesystem::canonical(output)));
        std::filesystem::remove_all(output, error);
        assert(!error);
        const auto stats = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = stats;
        if (stats.liveBytes != baseline.liveBytes)
            std::fprintf(stderr,
                "export cycle %u live=%llu baseline=%llu\n",
                cycle,
                static_cast<unsigned long long>(stats.liveBytes),
                static_cast<unsigned long long>(baseline.liveBytes));
        assert(stats.liveBytes == baseline.liveBytes
            && stats.allocationCount - stats.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS scene export CPU smoke: 20 warmed export/edit/failure/drain/destroy cycles; core live bytes/count delta = 0");
}
} // namespace

int main(int argc, char** argv)
{
    ExportFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        lifetime(fixture);
        return 0;
    }
    assert(argc == 1);
    snapshotFailures(fixture);
    transaction(fixture);
}
