#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/tests/fixtures/scene_persistence_fixture.h"
#include "editor/validation/editor_scene_modal_smoke.h"
#include "editor/workspace/editor_shell.h"

#include <array>
#include <cstdio>
#include <new>

namespace {
using namespace arp;
using namespace arp::editor;

void initialize(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    shell.activate("workspace_3D");
    assert(shell.submitCommand(CreateSceneObjectCommand("Modal target")).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(ActivateControlCommand("tool_4")).status
        == EditorResponseStatus::Completed);
}

bool visible(EditorShell& shell)
{
    return shell.runtime().canvas().findWidget(shell.id("scene_ui_modal"))->visible();
}

bool probe(const tests::RecoveryFixture& files)
{
    bool passed = true;
    for (const auto* source :
        {"new_scene", "open_scene", "save_scene_as", "duplicate_scene", "export_scene"}) {
        for (const auto budget : std::array<std::size_t, 5>{1, 128, 512, 4096, 131072}) {
            EditorShell shell;
            initialize(shell, files);
            EditorCommandEnvelope command{ActivateControlCommand(source)};
            const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            const auto version = shell.stateVersion();
            bool threw = false;
            EditorResponse response;
            {
                core::CpuMemoryQuotaScope scope(quota);
                try {
                    response = shell.submitCommand(std::move(command));
                } catch (const std::bad_alloc&) {
                    threw = true;
                }
            }
            const auto delta = shell.stateVersion() - version;
            const bool shown = visible(shell);
            const bool background =
                shell.runtime().canvas().findWidget(shell.id("viewport"))->enabled();
            EditorInputCommand key;
            key.kind = EditorInputKind::KeyDown;
            key.key = EditorInputKey::E;
            (void)shell.submitCommand(key);
            const bool keyboard = shell.sceneTransformTool() == EditorSceneTransformTool::Rotate;
            const bool coherent = shown ? (!background && !keyboard) : (background && keyboard);
            std::printf(
                "scene modal source=%s budget=%zu threw=%u visible=%u background=%u keyboard=%u versionDelta=%llu coherent=%u\n",
                source,
                budget,
                threw,
                shown,
                background,
                keyboard,
                static_cast<unsigned long long>(delta),
                coherent);
            passed &= !threw && coherent;
        }
    }
    return passed;
}

EditorResponse action(EditorShell& shell, std::string_view source)
{
    return shell.submitCommand(ActivateControlCommand(source));
}

bool widgetVisible(EditorShell& shell, const char* name)
{
    return shell.runtime().canvas().findWidget(shell.id(name))->visible();
}

// Observe acceptance independently of the response. A failed response must not
// conceal an accepted worker request, even if preparing the receipt exhausted memory.
bool receiptProbe(const tests::RecoveryFixture& files, bool enforce)
{
    bool passed = true;
    for (std::size_t budget = 24576; budget <= 32768; budget += 128) {
        EditorShell shell;
        initialize(shell, files);
        assert(action(shell, "save_scene_as").status == EditorResponseStatus::Completed);
        core::pmr::String path("receipt-");
        path += std::to_string(budget);
        path += ".arp.yaml";
        assert(shell.submitCommand(SetTextValueCommand{"scene_ui_path", path}).status
            == EditorResponseStatus::Completed);
        EditorCommandEnvelope command(ActivateControlCommand("scene_ui_submit"));
        EditorResponse response;
        bool threw = false;
        {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            core::CpuMemoryQuotaScope scope(quota);
            try {
                response = shell.submitCommand(std::move(command));
            } catch (const std::bad_alloc&) {
                threw = true;
            }
        }
        auto state = shell.submitCommand(GetEditorStateQuery{});
        const bool accepted = state.snapshot.sceneIoPending;
        const bool receipt = response.status == EditorResponseStatus::Completed
            && response.hasSnapshot && response.snapshot.scenePersistenceTaskId != 0;
        std::printf("receipt budget=%zu threw=%u accepted=%u receipt=%u diagnostic=%u\n",
            budget,
            threw,
            accepted,
            receipt,
            static_cast<unsigned>(response.diagnostic));
        passed &= !threw && accepted == receipt;
        if (receipt) {
            const auto id = response.snapshot.scenePersistenceTaskId;
            const auto version = shell.stateVersion();
            assert(action(shell, "scene_ui_retry").status == EditorResponseStatus::Completed);
            assert(shell.stateVersion() == version);
            assert(
                tests::ScenePersistenceFixture::finish(shell, response).scenePersistenceTaskStatus
                == ScenePersistenceTaskStatus::Succeeded);
            assert(shell.submitCommand(GetScenePersistenceTaskQuery{id}).hasSnapshot);
        } else if (accepted) {
            // For the pre-fix probe only: drain the independently observed task;
            // never invent a task ID to turn the missing receipt into success.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            do {
                shell.pollTasks();
                state = shell.submitCommand(GetEditorStateQuery{});
                assert(std::chrono::steady_clock::now() < deadline);
            } while (state.snapshot.sceneIoPending);
        }
    }
    if (enforce)
        assert(passed);
    return passed;
}

void entryCases(const tests::RecoveryFixture& files, bool full)
{
    for (const auto* source :
        {"new_scene", "open_scene", "save_scene_as", "duplicate_scene", "export_scene"}) {
        for (const auto budget : std::array<std::size_t, 5>{1, 128, 512, 4096, 131072}) {
            if (!full && budget != 1 && budget != 131072)
                continue;
            EditorShell shell;
            initialize(shell, files);
            const auto object = shell.sceneDocument().selectedObject();
            const auto objectCount = shell.sceneDocument().scene().objects().size();
            assert(shell.submitCommand(SetSceneObjectNameCommand{"Modal renamed"}).status
                == EditorResponseStatus::Completed);
            const auto version = shell.stateVersion();
            EditorCommandEnvelope command{ActivateControlCommand(source)};
            EditorResponse response;
            {
                const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
                core::CpuMemoryQuotaScope scope(quota);
                response = shell.submitCommand(std::move(command));
            }
            assert(shell.sceneDocument().selectedObject() == object && shell.sceneDirty());
            if (!visible(shell)) {
                assert(response.status == EditorResponseStatus::Failed
                    && shell.stateVersion() == version);
                assert(action(shell, source).status == EditorResponseStatus::Completed);
                assert(visible(shell) && shell.stateVersion() == version + 1);
            } else {
                assert(response.status == EditorResponseStatus::Completed
                    && shell.stateVersion() == version + 1);
            }
            const auto opened = shell.stateVersion();
            assert(action(shell, source).status == EditorResponseStatus::Completed
                && shell.stateVersion() == opened);
            assert(action(shell, "duplicate_scene").status == EditorResponseStatus::Completed
                && shell.stateVersion() == opened);
            EditorCommandEnvelope cancel(ActivateControlCommand("scene_ui_cancel"));
            {
                const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
                core::CpuMemoryQuotaScope scope(quota);
                assert(shell.submitCommand(std::move(cancel)).status
                    == EditorResponseStatus::Completed);
            }
            assert(!visible(shell) && shell.stateVersion() == opened + 1
                && shell.runtime().canvas().findWidget(shell.id("viewport"))->enabled());
            assert(
                shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
            assert(shell.sceneDocument().scene().objects().size() == objectCount
                && shell.sceneDocument().scene().findObject(object)->name() == "Modal target");
            assert(
                shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
            assert(shell.sceneDocument().selectedObject() == object
                && shell.sceneDocument().scene().findObject(object)->name() == "Modal renamed");
        }
    }
    std::puts(
        "PASS five launchers: preparation rejection, same-intent retry, one publication, repeat no-op, OOM cancel and preserved history");
}

void confirmationCases(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    initialize(shell, files);
    const auto objectCount = shell.sceneDocument().scene().objects().size();
    EditorCommandEnvelope once(ActivateControlCommand("new_scene"));
    once.idempotencyKey = "scene-modal-open-once";
    once.idempotencyFingerprint = "activate:new_scene";
    const auto opened = shell.submitCommand(once);
    assert(opened.status == EditorResponseStatus::Completed);
    assert(action(shell, "scene_ui_cancel").status == EditorResponseStatus::Completed);
    const auto cancelled = shell.stateVersion();
    assert(shell.submitCommand(once).stateVersion == opened.stateVersion
        && shell.stateVersion() == cancelled && !visible(shell));
    assert(action(shell, "new_scene").status == EditorResponseStatus::Completed);
    const auto prompting = shell.stateVersion();
    assert(action(shell, "scene_ui_submit").diagnostic == EditorDiagnosticCode::UnsavedChanges
        && shell.stateVersion() == prompting + 1 && widgetVisible(shell, "scene_ui_confirm"));
    assert(shell.submitCommand(CreateSceneObjectCommand("Changed after confirmation")).status
        == EditorResponseStatus::Completed);
    const auto stale = shell.stateVersion();
    assert(action(shell, "scene_ui_confirm").diagnostic == EditorDiagnosticCode::StaleDocumentTarget
        && shell.stateVersion() == stale && !widgetVisible(shell, "scene_ui_confirm"));
    assert(action(shell, "scene_ui_submit").status == EditorResponseStatus::Completed);
    EditorCommandEnvelope confirm(ActivateControlCommand("scene_ui_confirm"));
    const auto beforeFailure = shell.stateVersion();
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(std::move(confirm)).status == EditorResponseStatus::Failed);
    }
    assert(shell.stateVersion() == beforeFailure && widgetVisible(shell, "scene_ui_failed")
        && shell.sceneDocument().scene().objects().size() == objectCount + 1);
    assert(action(shell, "scene_ui_retry").status == EditorResponseStatus::Completed
        && shell.stateVersion() == beforeFailure);
    assert(action(shell, "scene_ui_confirm").status == EditorResponseStatus::Completed
        && shell.stateVersion() == beforeFailure + 1 && !visible(shell)
        && !shell.sceneDocument().scene().findObjectByName("Modal target"));
    std::puts(
        "PASS idempotent launcher, stale confirmation, failed confirm, display-only retry and real New commit");
}

void pathAndSaveCases(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    initialize(shell, files);
    assert(shell.submitCommand(SetTextValueCommand{"scene_ui_path", "hidden.arp.yaml"}).status
        == EditorResponseStatus::Failed);
    assert(action(shell, "save_scene_as").status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(SetTextValueCommand{"scene_ui_path", core::pmr::String(512, 'a')})
               .status
        == EditorResponseStatus::Completed);
    for (auto invalid : {core::pmr::String(513, 'a'), core::pmr::String("\xff", 1)}) {
        assert(shell.submitCommand(SetTextValueCommand{"scene_ui_path", invalid}).status
            == EditorResponseStatus::Failed);
    }
    assert(shell.submitCommand(SetTextValueCommand{"scene_ui_path", "ordinary.arp.yaml"}).status
        == EditorResponseStatus::Completed);
    EditorCommandEnvelope pathFailure(SetTextValueCommand{"scene_ui_path", "changed.arp.yaml"});
    const auto editing = shell.stateVersion();
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(std::move(pathFailure)).diagnostic
            == EditorDiagnosticCode::CommandPreparationFailed);
    }
    assert(shell.stateVersion() == editing && widgetVisible(shell, "scene_ui_failed"));
    assert(action(shell, "scene_ui_retry").status == EditorResponseStatus::Completed);
    assert(shell.runtime().findControl(shell.id("scene_ui_path"))->value().text
        == "ordinary.arp.yaml");
    EditorCommandEnvelope failed(ActivateControlCommand("scene_ui_submit"));
    const auto version = shell.stateVersion();
    {
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(std::move(failed)).diagnostic
            == EditorDiagnosticCode::CommandPreparationFailed);
    }
    assert(shell.stateVersion() == version && widgetVisible(shell, "scene_ui_failed"));
    assert(!std::filesystem::exists(files.root / "ordinary.arp.yaml"));
    assert(action(shell, "scene_ui_retry").status == EditorResponseStatus::Completed
        && shell.stateVersion() == version);
    const auto accepted = action(shell, "scene_ui_submit");
    assert(accepted.status == EditorResponseStatus::Completed && accepted.hasSnapshot
        && accepted.snapshot.scenePersistenceTaskId != 0 && shell.stateVersion() == version);
    assert(tests::ScenePersistenceFixture::finish(shell, accepted).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(!visible(shell) && !shell.sceneDirty() && shell.stateVersion() == version + 1);
    const auto disk = tests::RecoveryFixture::read(files.root / "ordinary.arp.yaml");
    tests::RecoveryFixture::write(files.root / "other.arp.yaml", disk);
    assert(action(shell, "save_scene_as").status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(SetTextValueCommand{"scene_ui_path", "other.arp.yaml"}).status
        == EditorResponseStatus::Completed);
    const auto prompt = action(shell, "scene_ui_submit");
    assert(prompt.status == EditorResponseStatus::Completed
        && prompt.diagnostic == EditorDiagnosticCode::SceneOverwriteRequired
        && !shell.submitCommand(GetEditorStateQuery{}).snapshot.sceneIoPending
        && widgetVisible(shell, "scene_ui_confirm"));
    assert(action(shell, "scene_ui_cancel").status == EditorResponseStatus::Completed);
    assert(tests::RecoveryFixture::read(files.root / "other.arp.yaml") == disk);
    std::filesystem::remove(files.root / "other.arp.yaml");
    assert(action(shell, "duplicate_scene").status == EditorResponseStatus::Completed);
    const auto copying = shell.stateVersion();
    assert(action(shell, "scene_ui_submit").status == EditorResponseStatus::Completed
        && shell.stateVersion() == copying + 1 && !visible(shell) && shell.sceneDirty());
    assert(tests::RecoveryFixture::read(files.root / "ordinary.arp.yaml") == disk);
    assert(action(shell, "open_scene").status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(SetTextValueCommand{"scene_ui_path", "ordinary.arp.yaml"}).status
        == EditorResponseStatus::Completed);
    assert(action(shell, "scene_ui_submit").diagnostic == EditorDiagnosticCode::UnsavedChanges);
    const auto load = action(shell, "scene_ui_confirm");
    assert(tests::ScenePersistenceFixture::finish(shell, load).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(!visible(shell) && !shell.sceneDirty());
    std::filesystem::remove(files.root / "ordinary.arp.yaml");
    std::puts(
        "PASS path byte/UTF-8 gates, failed submit/retry, exact save/load task receipts, Duplicate preserves source bytes");
}

void nativePreparation()
{
    for (const auto scenario : {"scene_modal_open",
             "scene_modal_failed",
             "scene_modal_retry",
             "scene_modal_saved",
             "scene_modal_confirm"}) {
        EditorShell shell;
        core::pmr::String diagnostic;
        assert(EditorSceneModalSmoke::run(shell, scenario, diagnostic));
    }
}

void exportReceiptCases(const tests::RecoveryFixture& files)
{
    unsigned acceptedCount = 0;
    for (const auto budget : std::array<std::size_t, 4>{1, 8192, 32768, 131072}) {
        EditorShell shell;
        initialize(shell, files);
        ProjectManifest manifest;
        ProjectDiagnostic diagnostic;
        assert(ProjectManifestCodec::decode(
            tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"),
            manifest,
            diagnostic));
        manifest.entryScene = "4305ee93-3897-43ae-93da-ecbb0c569c84";
        const auto root = files.root / ("export-" + std::to_string(budget));
        assert(shell
                   .submitCommand(CreateProjectCommand{
                       core::FilePath::utf8(root), manifest, true, ProjectTemplateKind::Game2D})
                   .status
            == EditorResponseStatus::Completed);
        assert(
            shell.submitCommand(DuplicateSceneCommand{}).status == EditorResponseStatus::Completed);
        assert(action(shell, "export_scene").status == EditorResponseStatus::Completed);
        assert(
            shell.submitCommand(SetTextValueCommand{"scene_ui_path", "assets/modal-export"}).status
            == EditorResponseStatus::Completed);
        EditorCommandEnvelope submit(ActivateControlCommand("scene_ui_submit"));
        EditorResponse response;
        const auto version = shell.stateVersion();
        {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            core::CpuMemoryQuotaScope scope(quota);
            response = shell.submitCommand(std::move(submit));
        }
        const bool accepted =
            shell.submitCommand(GetEditorStateQuery{}).snapshot.sceneExportPending;
        const bool receipt = response.status == EditorResponseStatus::Completed
            && response.hasSnapshot && response.snapshot.sceneExportTask.taskId != 0;
        assert(accepted == receipt);
        std::printf("export budget=%zu accepted=%u receipt=%u\n", budget, accepted, receipt);
        if (!accepted) {
            assert(shell.stateVersion() == version
                && !std::filesystem::exists(root / "assets/modal-export"));
            assert(action(shell, "scene_ui_cancel").status == EditorResponseStatus::Completed);
            continue;
        }
        assert(shell.stateVersion() == version + 1 && response.snapshot.sceneExportPending);
        ++acceptedCount;
        const auto acceptedVersion = shell.stateVersion();
        assert(action(shell, "scene_ui_retry").status == EditorResponseStatus::Completed
            && shell.stateVersion() == acceptedVersion);
        assert(action(shell, "scene_ui_cancel").status == EditorResponseStatus::Completed);
        const auto id = response.snapshot.sceneExportTask.taskId;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            shell.pollTasks();
            const auto result = shell.submitCommand(GetSceneExportTaskQuery{id});
            assert(result.hasSnapshot);
            if (result.snapshot.sceneExportTask.status != SceneExportStatus::Pending) {
                assert(result.snapshot.sceneExportTask.status
                        == SceneExportStatus::ExportedUnregistered
                    && std::filesystem::is_regular_file(root / "assets/modal-export/scene.yaml"));
                break;
            }
            assert(std::chrono::steady_clock::now() < deadline);
        }
    }
    assert(acceptedCount != 0);
    std::puts(
        "PASS export preparation quotas: rejection or actual task receipt; display retry/cancel never repeats worker IO");
}

void bindingCases(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    initialize(shell, files);
    assert(shell.runtime().removeControl(shell.id("scene_ui_retry")).ok());
    const auto version = shell.stateVersion();
    assert(action(shell, "open_scene").diagnostic == EditorDiagnosticCode::InvalidCommandPayload
        && shell.stateVersion() == version && !visible(shell)
        && shell.runtime().canvas().findWidget(shell.id("viewport"))->enabled());
    std::puts("PASS missing fixed control rejects before publishing modal authority");
}

void terrainGate(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    initialize(shell, files);
    assert(
        shell.submitCommand(ShowTerrainBrowserCommand{}).status == EditorResponseStatus::Completed);
    const auto version = shell.stateVersion();
    assert(action(shell, "open_scene").diagnostic == EditorDiagnosticCode::WorkspaceBusy
        && shell.stateVersion() == version && !visible(shell));
    assert(shell.submitCommand(ShowTerrainBrowserCommand{false}).status
        == EditorResponseStatus::Completed);
    assert(action(shell, "open_scene").status == EditorResponseStatus::Completed);
    assert(action(shell, "scene_ui_cancel").status == EditorResponseStatus::Completed);
    std::puts("PASS visible Terrain browser explicitly gates Scene modal, close then retry");
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--probe")
        return probe(files) ? 0 : 2;
    if (argc == 2 && std::string_view(argv[1]) == "--receipt-probe")
        return receiptProbe(files, false) ? 0 : 2;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        entryCases(files, cycle == 0);
        confirmationCases(files);
        pathAndSaveCases(files);
        if (cycle == 0) {
            receiptProbe(files, true);
            nativePreparation();
            exportReceiptCases(files);
            bindingCases(files);
            terrainGate(files);
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (cycle == 0)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary scene modal failure/cancel/retry/save/load/destruction cycles: core live bytes/count delta=0");
    return 0;
}
