#include "2d/physics_components.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/tests/fixtures/scene_persistence_fixture.h"
#include "editor/tests/fixtures/script_scene_shell_fixture.h"
#include "editor/workspace/editor_recovery_preparation.h"
#include "editor/workspace/editor_recovery_session.h"
#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_script_policy.h"
#include "reflection/instance.h"

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using Task = arp::editor::tests::ScenePersistenceFixture;

using ShellFiles = ScriptSceneShellFixture;

void roundTrip(ShellFiles& files)
{
    EditorShell shell;
    files.open(shell);
    const auto retired = shell.sceneDocument().scriptCatalog();
    ShellFiles::command(shell, SetSceneObjectNameCommand("Local B"));
    check(shell.sceneDirty());
    const auto saving = shell.submitCommand(SaveSceneCommand("scene.yaml"));
    ShellFiles::command(shell, SetSceneObjectNameCommand("Local C"));
    ShellFiles::succeeded(Task::finish(shell, saving), true);
    check(
        shell.sceneDirty() && files.read("scene.yaml").find("Local B") != core::pmr::String::npos);
    ShellFiles::command(shell, UndoSceneCommand{});
    check(!shell.sceneDirty());
    ShellFiles::load(shell);
    check(!shell.sceneDirty() && files.amount(shell) == 7);
    for (const auto& entry : retired->modules().entries())
        check(entry.module->liveInstances() == 0);
    const auto retained = shell.sceneDocument().scriptCatalog();
    shell.sceneDocument()
        .scene()
        .findComponent(files.scriptId)
        ->scriptInstance()
        .ptr<reflection::instance>()
        ->slot("amount") = reflection::object(11);
    check(shell.sceneDirty());
    ShellFiles::succeeded(
        Task::finish(shell, shell.submitCommand(SaveSceneCommand("scene.yaml"))), true);
    ShellFiles::load(shell);
    check(files.amount(shell) == 11 && !shell.sceneDirty());
    for (const auto& entry : retained->modules().entries())
        check(entry.module->liveInstances() == 0);
    // Restoring the source is not needed by observation, and no initializer or
    // constructor may execute in this actual passive Shell domain.
    std::filesystem::remove(files.files.root / "primary.arp");
    check(!shell.sceneDirty());
    check(shell.submitCommand(SaveSceneCommand("scene.yaml")).status
        != EditorResponseStatus::Completed);
    files.files.write("primary.arp", Fixture::primarySource);
    check(shell.sceneDocument().scene().name() == "Shell script root");
    // Explicit new-scene transitions retain the native v3 path.
    ShellFiles::command(shell, NewSceneCommand{true});
    std::filesystem::remove(files.files.root / "native.yaml");
    ShellFiles::succeeded(
        Task::finish(shell, shell.submitCommand(SaveSceneCommand("native.yaml"))), true);
    check(core::YamlContract::uint32(
              core::YamlContract::parseSingleDocument(files.read("native.yaml")), "contractVersion")
        == EditorSceneAuthoringCodec::contractVersion);
    files.files.write("scene.yaml", files.original);
}

void conflicts(ShellFiles& files)
{
    EditorShell shell;
    files.open(shell);
    ShellFiles::command(shell, SetSceneObjectNameCommand("Local B"));
    auto outside = files.original;
    outside.replace(outside.find("Local A"), 7, "Outside");
    const auto timestamp = std::filesystem::last_write_time(files.files.root / "scene.yaml");
    files.files.write("scene.yaml", outside);
    std::filesystem::last_write_time(files.files.root / "scene.yaml", timestamp);
    SaveSceneCommand overwrite("scene.yaml");
    overwrite.overwriteExisting = true;
    auto result = Task::finish(shell, shell.submitCommand(overwrite));
    check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && result.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-FILE-EXTERNAL-CHANGE"
        && !result.scenePersistenceTaskCommitted && shell.sceneDirty());
    check(files.read("scene.yaml") == outside);
    files.files.write("scene.yaml", files.original);
    files.files.write("other.yaml", files.original);
    overwrite.relativePath = "other.yaml";
    const auto preflight = shell.submitCommand(overwrite);
    ShellFiles::command(shell, SetSceneObjectNameCommand("Local C"));
    result = Task::finish(shell, preflight);
    check(result.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-FILE-PREFLIGHT-STALE"
        && !result.scenePersistenceTaskCommitted && files.read("other.yaml") == files.original);
    ShellFiles::succeeded(Task::finish(shell, shell.submitCommand(overwrite)), true);
    files.files.write("bad.yaml", "type: arp.editor-scene\ncontractVersion: 99\n");
    LoadSceneCommand load("bad.yaml");
    load.discardChanges = true;
    result = Task::finish(shell, shell.submitCommand(load));
    check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
        && result.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-FILE-VERSION");
    check(shell.sceneDocument().scene().findObject(files.child)->name() == "Local C");
    const auto pendingLoad = shell.submitCommand(LoadSceneCommand("other.yaml"));
    ShellFiles::command(shell, SetSceneObjectNameCommand("Local D"));
    result = Task::finish(shell, pendingLoad);
    check(result.scenePersistenceTaskDiagnostic == "EDITOR-SCENE-LOAD-STALE-DOCUMENT");
    files.files.write("scene.yaml", files.original);
}

void staleContext(ShellFiles& files)
{
    for (bool source : {false, true}) {
        EditorShell shell;
        files.open(shell);
        ShellFiles::command(shell, SetSceneObjectNameCommand("Local B"));
        const auto saving = shell.submitCommand(SaveSceneCommand("scene.yaml"));
        files.files.write(source ? "primary.arp" : "project.arp.yaml",
            core::pmr::String(source ? Fixture::primarySource : files.manifest.c_str())
                + "\n# changed\n");
        const auto result = Task::finish(shell, saving);
        check(result.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Failed
                && result.scenePersistenceTaskCommitted && shell.sceneDirty(),
            result.scenePersistenceTaskDiagnostic);
        check(files.read("scene.yaml").find("Local B") != core::pmr::String::npos);
        files.files.write("primary.arp", Fixture::primarySource);
        files.files.write("project.arp.yaml", files.manifest);
        files.files.write("scene.yaml", files.original);
    }
    EditorShell shell;
    files.open(shell);
    const auto epoch = shell.sceneDocument().ownerTicket();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    SaveSceneCommand never("never.yaml");
    {
        core::CpuMemoryQuotaScope scope(quota);
        check(shell.submitCommand(std::move(never)).status != EditorResponseStatus::Completed);
    }
    check(epoch.isCurrent() && !shell.sceneDirty()
        && !std::filesystem::exists(files.files.root / "never.yaml"));
    const auto accepted = shell.submitCommand(SaveSceneCommand("scene.yaml"));
    {
        core::CpuMemoryQuotaScope scope(quota);
        shell.pollTasks();
    }
    // The taken result is retained if command dispatch could not allocate.
    ShellFiles::succeeded(Task::finish(shell, accepted), true);
}

EditorRecoverySnapshot recoverySnapshot(ShellFiles& files, EditorShell& shell)
{
    EditorRecoverySnapshot snapshot;
    snapshot.sessionId = "00000000-0000-4000-8000-000000000098";
    snapshot.sequence = 1;
    snapshot.workspaceEpoch = 1;
    snapshot.projectRoot = core::FilePath::utf8(files.files.root);
    snapshot.workspace = "3D";
    snapshot.project = EditorRecoveryProject{};
    auto& project = *snapshot.project;
    project.id = files.files.manifest.id;
    project.savedFileBytes = files.manifest;
    project.draftYaml = files.manifest;
    EditorProjectResources resources;
    check(resources.load(files.files.root).success);
    core::pmr::String diagnostic;
    check(
        EditorRecoveryPreparation::captureInputs(files.files.root, resources, project, diagnostic),
        diagnostic);
    const auto input = EditorSceneScriptPolicy::inputs(files.files.root, files.files.manifest);
    check(EditorSceneScriptCodec::observeContentHash(
              shell.sceneDocument(), input, snapshot.scene.savedContentHash, diagnostic),
        diagnostic);
    snapshot.scene.savedFileBytes = files.original;
    snapshot.scene.path = "scene.yaml";
    snapshot.scene.boundTarget = "scene.yaml";
    ShellFiles::command(shell, SetSceneObjectNameCommand("Unsaved"));
    shell.sceneDocument()
        .scene()
        .findComponent(files.scriptId)
        ->scriptInstance()
        .ptr<reflection::instance>()
        ->slot("amount") = reflection::object(23);
    check(EditorSceneScriptCodec::observeSnapshot(
              shell.sceneDocument(), input, snapshot.scene.yaml, diagnostic),
        diagnostic);
    return snapshot;
}

void recoveryRoundTrip(ShellFiles& files)
{
    EditorShell shell;
    files.open(shell);
    auto snapshot = recoverySnapshot(files, shell);
    const auto epoch = shell.sceneDocument().ownerTicket();
    core::pmr::String diagnostic, wire;
    check(EditorRecoveryCodec::encode(snapshot, wire, diagnostic), diagnostic);
    EditorRecoverySnapshot reopened;
    check(EditorRecoveryCodec::decode(wire, reopened, diagnostic), diagnostic);
    EditorRecoverySession session;
    check(session.open(files.files.root / "recovery", diagnostic), diagnostic);
    snapshot.sessionId = session.sessionId();
    snapshot.sequence = session.nextSequence();
    check(session.submit(snapshot, EditorRecoverySession::TimePoint{})
        == EditorRecoverySubmitStatus::Accepted);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (session.pending() && std::chrono::steady_clock::now() < deadline) {
        session.poll();
        std::this_thread::yield();
    }
    check(session.result().status == EditorRecoveryWriteStatus::Saved, session.result().diagnostic);
    const auto slot = core::readTextFile(
        session.directory() / std::filesystem::u8path(EditorRecoveryRegion::slotName(0)));
    check(slot.success);
    check(EditorRecoveryCodec::decode(slot.text, reopened, diagnostic), diagnostic);
    check(reopened.scene.yaml == snapshot.scene.yaml
        && reopened.scene.savedFileBytes == files.original);
    check(session.discardOwned(diagnostic), diagnostic);
    auto candidate =
        EditorRecoveryPreparation::prepareForWorkspace(reopened, shell, true, diagnostic);
    check(bool(candidate), diagnostic);
    check(candidate->preparedState()->sceneAuthoringVersion()
            == EditorSceneScriptCodec::contractVersion
        && candidate->preparedState()->sceneSavedHash() == snapshot.scene.savedContentHash);
    auto& restored = *candidate->recovery->document;
    check(&restored.scene() != &shell.sceneDocument().scene());
    check(restored.sceneServiceSession() != shell.sceneDocument().sceneServiceSession());
    check(restored.sceneServiceSession()->resources() == &candidate->recovery->resources.manager()
        && restored.sceneServiceSession()->physics()
            == candidate->recovery->resources.physicsWorld2D());
    check(candidate->recovery->resources.physicsWorld2D()
        != shell.sceneDocument().sceneServiceSession()->physics());
    check(restored.scene().name() == "Shell script root"
        && restored.scene().findObject(files.child)->name() == "Unsaved"
        && restored.scene()
                .findComponent(files.scriptId)
                ->scriptInstance()
                .ptr<reflection::instance>()
                ->slot("amount")
                .cast<int>()
            == 23);
    auto* body = restored.scene().findComponent(files.bodyId);
    auto* link = restored.scene().findObject(files.child)->findComponent(files.linkId);
    check(link->scriptInstance().ptr<reflection::instance>()->slot("body")
        == body->nativeReflectionObject());
    check(
        EditorRecoveryPreparation::validateForWorkspace(*candidate, shell, diagnostic), diagnostic);
    const auto ticket = EditorRecoveryAdmission::capture(shell, true, diagnostic);
    check(bool(ticket), diagnostic);
    check(std::filesystem::remove(files.files.root / "primary.arp"));
    check(EditorRecoveryAdmission::validate(shell, *ticket, diagnostic), diagnostic);
    check(!EditorRecoveryPreparation::validateForWorkspace(*candidate, shell, diagnostic));
    files.files.write("primary.arp", Fixture::primarySource);
    check(
        EditorRecoveryPreparation::validateForWorkspace(*candidate, shell, diagnostic), diagnostic);
    shell.sceneDocument()
        .scene()
        .findComponent(files.scriptId)
        ->scriptInstance()
        .ptr<reflection::instance>()
        ->slot("amount") = reflection::object(24);
    check(!EditorRecoveryAdmission::validate(shell, *ticket, diagnostic));
    shell.sceneDocument()
        .scene()
        .findComponent(files.scriptId)
        ->scriptInstance()
        .ptr<reflection::instance>()
        ->slot("amount") = reflection::object(23);
    check(
        EditorRecoveryPreparation::validateForWorkspace(*candidate, shell, diagnostic), diagnostic);
    auto catalog = restored.scriptCatalog();
    candidate.reset();
    for (const auto& entry : catalog->modules().entries())
        check(entry.module->liveInstances() == 0);
    check(epoch.isCurrent() && shell.sceneDirty() && files.amount(shell) == 23
        && files.read("scene.yaml") == files.original);
}

void recoveryRejections(ShellFiles& files)
{
    EditorShell shell;
    files.open(shell);
    const auto snapshot = recoverySnapshot(files, shell);
    core::pmr::String diagnostic;
    const auto prepare = [&](const EditorRecoverySnapshot& value) {
        return EditorRecoveryPreparation::prepareForWorkspace(value, shell, true, diagnostic);
    };
    auto candidate = prepare(snapshot);
    check(bool(candidate), diagnostic);
    // Both live and candidate observation must consume ordinary retained slots.
    // Neither can execute a user getter or perform source IO to compute a hash.
    const auto ticket = EditorRecoveryAdmission::capture(shell, true, diagnostic);
    check(bool(ticket), diagnostic);
    files.files.write("primary.arp", core::pmr::String(Fixture::primarySource) + "\n# changed\n");
    check(EditorRecoveryAdmission::validate(shell, *ticket, diagnostic), diagnostic);
    check(!EditorRecoveryPreparation::validateForWorkspace(*candidate, shell, diagnostic));
    check(!prepare(snapshot));
    files.files.write("primary.arp", Fixture::primarySource);
    check(
        EditorRecoveryPreparation::validateForWorkspace(*candidate, shell, diagnostic), diagnostic);
    candidate->recovery->document->scene()
        .findComponent(files.scriptId)
        ->scriptInstance()
        .ptr<reflection::instance>()
        ->slot("amount") = reflection::object(99);
    check(!EditorRecoveryPreparation::validateForWorkspace(*candidate, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-CANDIDATE-CONTENT-CHANGED");
    auto noProject = snapshot;
    noProject.project.reset();
    check(!EditorRecoveryCodec::validate(noProject, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-SCRIPT-PROJECT-REQUIRED");
    auto bad = snapshot;
    bad.scene.savedContentHash.back() = bad.scene.savedContentHash.back() == '0' ? '1' : '0';
    check(!prepare(bad) && diagnostic == "EDITOR-RECOVERY-SCENE-SAVEPOINT-MISMATCH");
    EditorSceneDocument native(false);
    auto mixed = snapshot;
    check(EditorSceneAuthoringCodec::encode(native, mixed.scene.savedFileBytes, diagnostic),
        diagnostic);
    check(!EditorRecoveryCodec::validate(mixed, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-SCENE-FORMAT-MISMATCH");
    auto node = core::YamlContract::parseSingleDocument(snapshot.scene.yaml);
    const auto rejectWire = [&](const core::YamlContract::Node& wireNode) {
        auto changed = snapshot;
        changed.scene.yaml = core::YamlContract::serialize(wireNode);
        check(!EditorRecoveryCodec::validate(changed, diagnostic));
    };
    auto duplicate = node;
    duplicate["objects"][0]["id"] = duplicate["root"]["id"];
    rejectWire(duplicate);
    auto reference = node;
    reference["objects"][0]["components"][0]["state"]["references"][0]["component"] = 99999;
    rejectWire(reference);
    auto version = node;
    version["contractVersion"] = 99;
    rejectWire(version);
    auto wrongField = snapshot;
    node["root"]["components"][1]["state"]["fields"]["amount"] = "wrong type";
    wrongField.scene.yaml = core::YamlContract::serialize(node);
    // Structure is not schema admission. The actual cold module/field restore
    // must fail before any candidate can become a workspace publication token.
    check(!prepare(wrongField));
    auto unbound = snapshot;
    unbound.scene.path.clear();
    unbound.scene.boundTarget.clear();
    unbound.scene.savedContentHash.clear();
    unbound.scene.savedFileBytes.clear();
    auto unsaved = prepare(unbound);
    check(bool(unsaved) && unsaved->preparedState()->sceneAuthoringVersion() == 4
            && unsaved->preparedState()->sceneSavedHash().empty(),
        diagnostic);
    files.files.write("scene.yaml", core::pmr::String(files.original) + "\n# external\n");
    auto conflict = prepare(snapshot);
    check(bool(conflict) && conflict->recovery->conflicts.size() == 1, diagnostic);
    files.files.write("scene.yaml", files.original);
    check(!EditorRecoveryPreparation::validateForWorkspace(*conflict, shell, diagnostic));
    const auto epoch = shell.sceneDocument().ownerTicket();
    {
        core::CpuMemoryQuotaScope scope(core::makeShared<core::CpuMemoryQuota>(1));
        check(!prepare(snapshot));
    }
    check(bool(prepare(snapshot)), diagnostic);
    check(epoch.isCurrent() && files.read("scene.yaml") == files.original);
    check(shell.sceneDirty() && files.amount(shell) == 23);
}

} // namespace

int main(int argc, char** argv)
{
    ShellFiles files;
    if (argc == 2
        && (std::string_view(argv[1]) == "--recovery"
            || std::string_view(argv[1]) == "--recovery-smoke")) {
        recoveryRoundTrip(files);
        if (std::string_view(argv[1]) == "--recovery-smoke") {
            const auto baseline = core::defaultCpuAllocator().stats();
            for (unsigned round = 0; round < 20; ++round) {
                recoveryRoundTrip(files);
                const auto current = core::defaultCpuAllocator().stats();
                check(current.liveBytes == baseline.liveBytes);
                check(current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
            }
            std::printf(
                "ordinary_recovery_cycles=20 live_bytes_delta=0 live_count_delta=0 retired_instances=0 peak_bytes=%llu\n",
                static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
        } else
            recoveryRejections(files);
        std::puts(
            "PASS v4 recovery wire, independent actual domain, baseline and delayed validation");
        return 0;
    }
    roundTrip(files);
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            roundTrip(files);
            const auto current = core::defaultCpuAllocator().stats();
            check(current.liveBytes == baseline.liveBytes);
            check(current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        }
        std::printf(
            "ordinary_shell_cycles=20 live_bytes_delta=0 live_count_delta=0 retired_instances=0 peak_bytes=%llu\n",
            static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
    } else {
        conflicts(files);
        staleContext(files);
        std::puts(
            "PASS actual v4 Shell load/save/reopen, observation, conflicts, submitted baseline, freshness and allocation rejection");
    }
}
