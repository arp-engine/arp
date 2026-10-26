#include "core/encoding/yaml_contract.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_recovery_preparation.h"
#include "editor/workspace/editor_shell.h"
#include "resource/resource_contract.h"

namespace {
using namespace arp;
using namespace arp::editor;
using tests::RecoveryFixture;
using Preparation = EditorRecoveryPreparation;
using Yaml = core::YamlContract;

void directoryRecovery()
{
    RecoveryFixture fixture;
    auto snapshot = fixture.snapshot();
    core::pmr::String diagnostic;
    const auto initial = RecoveryFixture::encode(snapshot);
    auto candidate = Preparation::prepare(snapshot, fixture.root, diagnostic);
    if (!candidate)
        std::fprintf(stderr, "prepare: %s\n", diagnostic.c_str());
    assert(candidate && candidate->document && candidate->conflicts.size() == 2
        && candidate->conflicts[0].kind == Preparation::ConflictKind::Missing
        && candidate->conflicts[1].kind == Preparation::ConflictKind::Missing
        && candidate->scripts.size() == 1 && candidate->scripts[0].id == 0
        && candidate->scripts[0].undo.empty() && candidate->scripts[0].redo.empty()
        && candidate->scripts[0].dirty());
    assert(RecoveryFixture::encode(candidate->submitted) == initial);
    assert(!std::filesystem::exists(fixture.root / "rules.arp")
        && !std::filesystem::exists(fixture.root / "authoring.arp.yaml"));
    std::weak_ptr<resource::ResourceManager> frozenManager = candidate->resources.retainManager();
    auto frozenDocument = std::move(candidate->document);
    candidate.reset();
    assert(!frozenManager.expired()
        && frozenDocument->resourceManagerProvider()() == frozenManager.lock().get()
        && !frozenDocument->physicsWorld2DProvider()());
    frozenDocument.reset();
    assert(frozenManager.expired());

    RecoveryFixture::write(fixture.root / "rules.arp", snapshot.scripts[0].savedFileBytes);
    RecoveryFixture::write(fixture.root / "authoring.arp.yaml", snapshot.scene.savedFileBytes);
    candidate = Preparation::prepare(snapshot, fixture.root, diagnostic);
    assert(candidate && candidate->conflicts.empty());
    const auto scriptId = candidate->scripts[0].id;
    RecoveryFixture::write(fixture.root / "rules.arp", "external edit\n");
    RecoveryFixture::write(fixture.root / "authoring.arp.yaml", "external invalid scene\n");
    candidate = Preparation::prepare(snapshot, fixture.root, diagnostic);
    assert(candidate && candidate->conflicts.size() == 2
        && candidate->conflicts[0].kind == Preparation::ConflictKind::Changed
        && candidate->conflicts[1].kind == Preparation::ConflictKind::Changed
        && candidate->scripts[0].id == scriptId);
    // Prove the restored baseline still enforces the existing save conflict
    // contract. No extra overwrite permission follows from recovery.
    ScriptDocumentSession live;
    const auto index = live.append(candidate->scripts[0]);
    assert(live.setActiveIndex(index));
    const auto retiredId = live.activeId();
    assert(live.replaceRecoveredDocuments(candidate->scripts, 0, diagnostic)
        && live.activeId() > retiredId && !live.find(retiredId)
        && candidate->scripts[0].id == retiredId);
    assert(live.saveActiveFile(fixture.root).code
        == ScriptDocumentSession::FileResultCode::ExternalConflict);
    assert(RecoveryFixture::read(fixture.root / "rules.arp") == "external edit\n");
    assert(
        RecoveryFixture::read(fixture.root / "authoring.arp.yaml") == "external invalid scene\n");
    assert(RecoveryFixture::encode(snapshot) == initial);
    auto wrong = snapshot;
    wrong.scene.savedContentHash = "sha256:" + core::pmr::String(64, '0');
    assert(!Preparation::prepare(wrong, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-SCENE-SAVEPOINT-MISMATCH");
    std::filesystem::create_directory(fixture.root / "other");
    assert(!Preparation::prepare(snapshot, fixture.root / "other", diagnostic)
        && diagnostic == "EDITOR-RECOVERY-ROOT-NOT-ADMITTED");
    wrong = snapshot;
    wrong.scene.boundTarget = "different.arp.yaml";
    assert(!Preparation::prepare(wrong, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-SCENE-TARGET-CHANGED");
    std::puts(
        "PASS independent directory candidate, exact baselines/conflicts, semantic save point, no writes and admitted root");
}

void pathAliases()
{
    RecoveryFixture fixture;
    auto snapshot = fixture.snapshot();
    core::pmr::String diagnostic;
    RecoveryFixture::write(fixture.root / "rules.arp", snapshot.scripts[0].savedFileBytes);
    std::filesystem::create_hard_link(fixture.root / "rules.arp", fixture.root / "alias.arp");
    auto other = snapshot.scripts[0];
    other.path = "alias.arp";
    snapshot.scripts.push_back(other);
    assert(!Preparation::prepare(snapshot, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-DOCUMENT-PATH-ALIAS");
    snapshot.scripts.pop_back();
#ifdef _WIN32
    other.path = "RULES.arp";
    snapshot.scripts.push_back(other);
    assert(!Preparation::prepare(snapshot, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-DOCUMENT-PATH-ALIAS");
#endif
    std::puts("PASS canonical case and hard-link aliases rejected across restored documents");
}

class ProjectRecoveryFixture final {
public:
    ProjectRecoveryFixture()
    {
        ProjectDiagnostic issue;
        assert(ProjectManifestCodec::decode(
            tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"),
            manifest,
            issue));
        manifest.entryScene = resource::deriveResourceUuid("recovery-test", "entry");
        manifest.physics2d =
            physics2d::WorldConfig{{0, -9.8f}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
        assert(ProjectSceneAuthoring::prepareTemplate(
            workspace.root, manifest, ProjectTemplateKind::Game2D, diagnostic));
        manifest.editorMetadata = "editor-project.yaml";
        RecoveryFixture::write(workspace.root / "editor-project.yaml",
            "type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: []\n");
        ProjectScriptModule module;
        module.id = "gameplay";
        module.resourceId = resource::deriveResourceUuid("recovery-test", "script");
        module.source = "gameplay.arp";
        module.schemaVersion = 1;
        manifest.scriptModules.push_back(std::move(module));
        RecoveryFixture::write(workspace.root / "gameplay.arp", "// source-only, not executed\n");
        assert(ProjectManifestCodec::encode(manifest, savedManifest, issue));
        RecoveryFixture::write(workspace.root / "project.arp.yaml", savedManifest);
        assert(resources.load(workspace.root).success);
        const auto factory = ProjectSceneAuthoring::factory(
            resources.manager(), resources.physicsWorld2D(), diagnostic);
        assert(factory);
        auto document = core::makeUnique<EditorSceneDocument>(false);
        document->setResourceManagerProvider([this] {
            return &resources.manager();
        });
        document->setPhysicsWorld2DProvider([this] {
            return resources.physicsWorld2D();
        });
        const auto scene = resources.manager().find<scene::SceneAssetResource>(manifest.entryScene);
        assert(scene.ready()
            && document->loadRuntimeScene(*scene.get(), resources.manager(), *factory, diagnostic));
        snapshot = workspace.snapshot();
        assert(EditorSceneAuthoringCodec::encode(
            *document, snapshot.scene.savedFileBytes, diagnostic));
        assert(EditorSceneAuthoringCodec::contentHash(
            *document, snapshot.scene.savedContentHash, diagnostic));
        const auto created = document->createObject("recovered unsaved object");
        assert(created.value() && document->addComponent("RigidBody2DComponent", created)
            && document->addComponent("Collider2DComponent", created));
        assert(EditorSceneAuthoringCodec::encode(*document, snapshot.scene.yaml, diagnostic));
        snapshot.project.emplace();
        snapshot.project->id = manifest.id;
        snapshot.project->savedFileBytes = savedManifest;
        auto draft = manifest;
        draft.name = "Recovered name draft";
        assert(ProjectManifestCodec::encode(draft, snapshot.project->draftYaml, issue));
        assert(Preparation::capturePackages(
            workspace.root, savedManifest, snapshot.project->packages, diagnostic));
        assert(
            Preparation::captureInputs(workspace.root, resources, *snapshot.project, diagnostic));
        assert(snapshot.project->metadata && snapshot.project->inputFiles.size() == 2);
        RecoveryFixture::write(
            workspace.root / "authoring.arp.yaml", snapshot.scene.savedFileBytes);
        RecoveryFixture::write(workspace.root / "rules.arp", snapshot.scripts[0].savedFileBytes);
    }

    RecoveryFixture workspace;
    ProjectManifest manifest;
    core::pmr::String savedManifest, diagnostic;
    EditorProjectResources resources;
    EditorRecoverySnapshot snapshot;
};

void projectRecovery()
{
    ProjectRecoveryFixture fixture;
    const auto source = RecoveryFixture::encode(fixture.snapshot);
    auto candidate =
        Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic);
    if (!candidate)
        std::fprintf(stderr, "project prepare: %s\n", fixture.diagnostic.c_str());
    assert(candidate && candidate->conflicts.empty() && candidate->document
        && candidate->resources.manager()
            .find<scene::SceneAssetResource>(fixture.manifest.entryScene)
            .ready()
        && candidate->resources.physicsWorld2D() != fixture.resources.physicsWorld2D()
        && candidate->resources.retainManager() != fixture.resources.retainManager());
    core::pmr::String yaml;
    assert(EditorSceneAuthoringCodec::encode(*candidate->document, yaml, fixture.diagnostic)
        && yaml == fixture.snapshot.scene.yaml);
    assert(candidate->resources.manifest()->name == fixture.manifest.name
        && candidate->submitted.project->draftYaml != candidate->resources.manifestSnapshot());
    assert(
        RecoveryFixture::read(fixture.workspace.root / "project.arp.yaml") == fixture.savedManifest
        && RecoveryFixture::encode(fixture.snapshot) == source);
    EditorShell shell;
    assert(shell
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                fixture.workspace.root)
            .ok());
    const auto version = shell.stateVersion();
    auto workspace =
        Preparation::prepareForWorkspace(fixture.snapshot, shell, false, fixture.diagnostic);
    if (!workspace)
        std::fprintf(stderr, "project workspace preparation: %s\n", fixture.diagnostic.c_str());
    assert(workspace && workspace->preparedState() && workspace->preparedState()->projectDraft());
    const auto& state = *workspace->preparedState();
    assert(state.projectDraft()->name == "Recovered name draft" && state.projectDirty()
        && state.projectDraft()->entryScene == fixture.manifest.entryScene
        && state.projectSavedBytes() == fixture.savedManifest
        && workspace->recovery->resources.manifest()->name == fixture.manifest.name
        && shell.projectManifest()->name == fixture.manifest.name && !shell.projectDirty()
        && shell.stateVersion() == version);
    auto clean = fixture.snapshot;
    clean.project->draftYaml = fixture.savedManifest + "# different spelling, same settings\n";
    auto cleanWorkspace = Preparation::prepareForWorkspace(clean, shell, false, fixture.diagnostic);
    assert(cleanWorkspace && !cleanWorkspace->preparedState()->projectDirty()
        && cleanWorkspace->preparedState()->projectDraft()->name == fixture.manifest.name
        && cleanWorkspace->preparedState()->projectSavedBytes() == fixture.savedManifest);
    auto wrong = fixture.snapshot;
    wrong.project->packages[0].fingerprint = "sha256:" + core::pmr::String(64, '0');
    assert(!Preparation::prepare(wrong, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-PACKAGE-BINDING-CONFLICT");
    wrong = fixture.snapshot;
    auto node = Yaml::parseSingleDocument(wrong.project->draftYaml);
    node["engineContract"] = "other/1";
    wrong.project->draftYaml = Yaml::serialize(node);
    assert(!Preparation::prepare(wrong, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-PROJECT-DRAFT-CONTRACT");
    wrong = fixture.snapshot;
    wrong.project.reset();
    assert(!Preparation::prepare(wrong, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-PROJECT-CONTEXT-CHANGED");
    RecoveryFixture::write(
        fixture.workspace.root / "project.arp.yaml", fixture.savedManifest + "# external\n");
    assert(!Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-PROJECT-BASELINE-CONFLICT");
    RecoveryFixture::write(fixture.workspace.root / "project.arp.yaml", fixture.savedManifest);
    const auto packageManifest = fixture.workspace.root
        / std::filesystem::u8path(fixture.manifest.assetPackages[0]) / "manifest.arpkg.yaml";
    const auto originalPackage = RecoveryFixture::read(packageManifest);
    RecoveryFixture::write(packageManifest, originalPackage + "# same schema, different bytes\n");
    assert(!Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-PACKAGE-BINDING-CONFLICT");
    assert(RecoveryFixture::read(packageManifest) != originalPackage && candidate->document);
    std::puts(
        "PASS real 2D package/resource/physics candidate, unsaved draft, exact package and project conflicts, failure preserves existing candidate");
}

void externalInputs()
{
    ProjectRecoveryFixture fixture;
    auto& project = *fixture.snapshot.project;
    EditorRecoverySnapshot roundTrip;
    assert(EditorRecoveryCodec::decode(
               RecoveryFixture::encode(fixture.snapshot), roundTrip, fixture.diagnostic)
        && roundTrip.project->metadata->fileBytes == project.metadata->fileBytes
        && ProjectInputSnapshot::equal(roundTrip.project->inputFiles, project.inputFiles));
    const auto baseline = RecoveryFixture::encode(fixture.snapshot);
    const auto metadataPath = fixture.workspace.root / "editor-project.yaml";
    RecoveryFixture::write(metadataPath, project.metadata->fileBytes + "# changed\n");
    assert(!Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-METADATA-BASELINE-CONFLICT");
    RecoveryFixture::write(metadataPath, project.metadata->fileBytes);
    const auto modulePath = fixture.workspace.root / "gameplay.arp";
    const auto moduleBytes = RecoveryFixture::read(modulePath);
    RecoveryFixture::write(modulePath, "// saved source changed\n");
    assert(!Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "PROJECT-INPUT-CONTENT-CHANGED");
    auto unchanged = project;
    assert(!Preparation::captureInputs(
               fixture.workspace.root, fixture.resources, unchanged, fixture.diagnostic)
        && ProjectInputSnapshot::equal(unchanged.inputFiles, project.inputFiles));
    RecoveryFixture::write(modulePath, moduleBytes);
    auto omitted = fixture.snapshot;
    omitted.project->inputFiles.clear();
    assert(!Preparation::prepare(omitted, fixture.workspace.root, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-INPUT-CLOSURE-MISMATCH");
    std::filesystem::remove(modulePath);
    assert(!Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic));
    RecoveryFixture::write(modulePath, moduleBytes);
    assert(Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic)
        && RecoveryFixture::encode(fixture.snapshot) == baseline);
    std::puts(
        "PASS exact metadata/source provenance, omitted closure, missing inputs and stale owner rejection; no source writes by recovery");
}

void fileFreshness()
{
    RecoveryFixture fixture;
    auto snapshot = fixture.snapshot();
    core::pmr::String diagnostic;
    RecoveryFixture::write(fixture.root / "rules.arp", snapshot.scripts[0].savedFileBytes);
    RecoveryFixture::write(fixture.root / "authoring.arp.yaml", snapshot.scene.savedFileBytes);
    auto candidate = Preparation::prepare(snapshot, fixture.root, diagnostic);
    assert(candidate && candidate->files.size() == 2
        && Preparation::validateFiles(*candidate, fixture.root, diagnostic));
    const auto original = RecoveryFixture::encode(candidate->submitted);
    RecoveryFixture::write(fixture.root / "rules.arp", "external A\n");
    assert(!Preparation::validateFiles(*candidate, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-DOCUMENT-BYTES-STALE");
    candidate = Preparation::prepare(snapshot, fixture.root, diagnostic);
    assert(candidate && candidate->conflicts.size() == 1
        && candidate->conflicts[0].kind == Preparation::ConflictKind::Changed);
    // Both versions conflict with the saved baseline. Merely comparing their
    // conflict enum would wrongly accept a changed precommit context.
    RecoveryFixture::write(fixture.root / "rules.arp", "external B\n");
    assert(!Preparation::validateFiles(*candidate, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-DOCUMENT-BYTES-STALE");
    RecoveryFixture::write(fixture.root / "rules.arp", "external A\n");
    assert(Preparation::validateFiles(*candidate, fixture.root, diagnostic));
    std::filesystem::remove(fixture.root / "rules.arp");
    assert(!Preparation::validateFiles(*candidate, fixture.root, diagnostic));
    candidate = Preparation::prepare(snapshot, fixture.root, diagnostic);
    assert(candidate && !candidate->files[1].bytes);
    RecoveryFixture::write(fixture.root / "rules.arp", "");
    assert(!Preparation::validateFiles(*candidate, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-DOCUMENT-BYTES-STALE");
    std::filesystem::remove(fixture.root / "rules.arp");
    assert(Preparation::validateFiles(*candidate, fixture.root, diagnostic));
    RecoveryFixture::write(fixture.root / "project.arp.yaml", "new project context");
    assert(!Preparation::validateFiles(*candidate, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-PROJECT-CONTEXT-CHANGED");
    std::filesystem::remove(fixture.root / "project.arp.yaml");
    std::error_code linkError;
    std::filesystem::create_symlink(
        fixture.root / "missing.yaml", fixture.root / "project.arp.yaml", linkError);
    if (!linkError) {
        assert(!Preparation::validateFiles(*candidate, fixture.root, diagnostic)
            && diagnostic == "EDITOR-RECOVERY-PROJECT-CONTEXT-CHANGED");
        assert(!Preparation::prepare(snapshot, fixture.root, diagnostic)
            && diagnostic == "EDITOR-RECOVERY-PROJECT-CONTEXT-CHANGED");
        std::filesystem::remove(fixture.root / "project.arp.yaml");
    } else
        std::puts("RECOVERY-SYMLINK: OS denied fixture creation; case not exercised");
    auto observed = std::move(candidate->files);
    assert(!Preparation::validateFiles(*candidate, fixture.root, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-OBSERVATION-SET-INVALID");
    candidate->files = std::move(observed);
    std::filesystem::create_directory(fixture.root / "other");
    assert(!Preparation::validateFiles(*candidate, fixture.root / "other", diagnostic)
        && diagnostic == "EDITOR-RECOVERY-FILE-CONTEXT-STALE");
    assert(Preparation::validateFiles(*candidate, fixture.root, diagnostic)
        && RecoveryFixture::encode(candidate->submitted) == original);
    ProjectRecoveryFixture project;
    auto loaded = Preparation::prepare(project.snapshot, project.workspace.root, diagnostic);
    assert(loaded && Preparation::validateFiles(*loaded, project.workspace.root, diagnostic));
    RecoveryFixture::write(
        project.workspace.root / "gameplay.arp", "// changed after preparation\n");
    assert(!Preparation::validateFiles(*loaded, project.workspace.root, diagnostic)
        && diagnostic == "PROJECT-INPUT-CONTENT-CHANGED");
    assert(loaded->document && candidate->document);
    std::puts(
        "PASS precommit exact-byte freshness, changed-to-changed, missing versus empty, context/input changes and failure preserves candidates");
}

void smoke()
{
    ProjectRecoveryFixture fixture;
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            auto candidate =
                Preparation::prepare(fixture.snapshot, fixture.workspace.root, fixture.diagnostic);
            assert(candidate);
            auto captured = *fixture.snapshot.project;
            assert(Preparation::captureInputs(
                       fixture.workspace.root, fixture.resources, captured, fixture.diagnostic)
                && ProjectPackageSnapshot::equal(
                    captured.packages, candidate->resources.packageBindings()));
            assert(candidate && candidate->conflicts.empty() && candidate->scripts[0].id == 0
                && Preparation::validateFiles(
                    *candidate, fixture.workspace.root, fixture.diagnostic));
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary independent resource/physics/scene/script candidate destruction cycles: core live bytes/count delta=0");
}

void packageOwnerProvenance()
{
    ProjectRecoveryFixture fixture;
    assert(fixture.resources.packageProvenanceBound()
        && fixture.resources.packageProvenanceDiagnostic().empty()
        && ProjectPackageSnapshot::equal(
            fixture.resources.packageBindings(), fixture.snapshot.project->packages));
    auto produced = *fixture.snapshot.project;
    produced.packages.clear();
    assert(Preparation::captureInputs(
               fixture.workspace.root, fixture.resources, produced, fixture.diagnostic)
        && ProjectPackageSnapshot::equal(produced.packages, fixture.snapshot.project->packages));
    const auto directory =
        fixture.workspace.root / std::filesystem::u8path(fixture.manifest.assetPackages[0]);
    const auto path = directory / "manifest.arpkg.yaml";
    const auto original = RecoveryFixture::read(path);
    const auto oldBindings = fixture.resources.packageBindings();
    const auto retained = fixture.resources.retainManager();
    RecoveryFixture::write(path, original + "# changed after live owner loaded\n");
    // Fresh disk fingerprints cannot retroactively describe the older live owner.
    auto counterfeit = fixture.snapshot;
    assert(Preparation::capturePackages(fixture.workspace.root,
        fixture.savedManifest,
        counterfeit.project->packages,
        fixture.diagnostic));
    assert(!ProjectPackageSnapshot::equal(oldBindings, counterfeit.project->packages));
    const auto before = RecoveryFixture::encode(counterfeit);
    assert(!Preparation::captureInputs(
               fixture.workspace.root, fixture.resources, *counterfeit.project, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-PACKAGE-OWNER-STALE"
        && RecoveryFixture::encode(counterfeit) == before
        && ProjectPackageSnapshot::equal(fixture.resources.packageBindings(), oldBindings)
        && fixture.resources.retainManager() == retained);
    EditorProjectResources refreshed;
    assert(refreshed.load(fixture.workspace.root).success && refreshed.packageProvenanceBound());
    assert(Preparation::captureInputs(
        fixture.workspace.root, refreshed, *counterfeit.project, fixture.diagnostic));
    assert(Preparation::prepare(counterfeit, fixture.workspace.root, fixture.diagnostic));
    fixture.resources.publishAssetPackages(std::move(refreshed));
    assert(fixture.resources.retainManager() != retained
        && fixture.resources.packageProvenanceBound()
        && ProjectPackageSnapshot::equal(
            fixture.resources.packageBindings(), counterfeit.project->packages));
    const auto refreshedBindings = fixture.resources.packageBindings();
    const auto refreshedManager = fixture.resources.retainManager();
    RecoveryFixture::write(path, "type: invalid\n");
    assert(!fixture.resources.load(fixture.workspace.root).success
        && fixture.resources.retainManager() == refreshedManager
        && fixture.resources.packageProvenanceBound()
        && ProjectPackageSnapshot::equal(fixture.resources.packageBindings(), refreshedBindings));
    RecoveryFixture::write(path, original);
    auto package = resource::InternalAssetPackageReader().read(directory);
    assert(package.success && !package.package.closurePath.empty());
    package.package.closurePath.clear();
    assert(resource::InternalAssetPackageWriter().write(package.package, directory).success);
    EditorProjectResources legacy;
    const auto loaded = legacy.load(fixture.workspace.root);
    if (!loaded.success)
        std::fprintf(stderr, "legacy normal load: %s\n", loaded.diagnostics.c_str());
    assert(loaded.success && !legacy.packageProvenanceBound()
        && !legacy.packageProvenanceDiagnostic().empty()
        && legacy.manager().find<scene::SceneAssetResource>(fixture.manifest.entryScene).ready());
    auto rejected = *fixture.snapshot.project;
    assert(!Preparation::captureInputs(fixture.workspace.root, legacy, rejected, fixture.diagnostic)
        && fixture.diagnostic.find("EDITOR-RECOVERY-PACKAGE-OWNER-UNBOUND:") == 0);
    std::puts(
        "PASS original package owner provenance, counterfeit recapture rejection, explicit reload/replacement, failed load preservation and legacy recovery gate");
}

void explicitUnsupportedAndBudgets()
{
    ProjectRecoveryFixture fixture;
    core::pmr::Vector<EditorRecoveryPackage> destination{{"sentinel", "unchanged"}};
    const auto directory =
        fixture.workspace.root / std::filesystem::u8path(fixture.manifest.assetPackages[0]);
    const auto package = resource::InternalAssetPackageReader().read(directory);
    assert(package.success);
    const auto indexPath = directory / std::filesystem::u8path(package.package.closurePath);
    const auto original = RecoveryFixture::read(indexPath);
    auto index = Yaml::parseSingleDocument(original);
    index["maxDecodedBytes"] = static_cast<std::uint64_t>(Preparation::maxPackageBytes + 1u);
    RecoveryFixture::write(indexPath, Yaml::serialize(index));
    assert(!Preparation::capturePackages(
               fixture.workspace.root, fixture.savedManifest, destination, fixture.diagnostic)
        && fixture.diagnostic == "EDITOR-RECOVERY-PACKAGE-DECODED-BUDGET" && destination.size() == 1
        && destination[0].directory == "sentinel");
    RecoveryFixture::write(indexPath, original);
    assert(Preparation::capturePackages(
               fixture.workspace.root, fixture.savedManifest, destination, fixture.diagnostic)
        && destination.size() == 1
        && destination[0].fingerprint == fixture.snapshot.project->packages[0].fingerprint);
    std::puts(
        "PASS original decoded-budget declarations rejected, capture output unchanged on failure");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke();
    else {
        directoryRecovery();
        pathAliases();
        projectRecovery();
        externalInputs();
        fileFreshness();
        explicitUnsupportedAndBudgets();
        packageOwnerProvenance();
    }
}
