#include "editor/validation/editor_scene_modal_smoke.h"

#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/tests/fixtures/scene_persistence_fixture.h"

#include <cstdio>

namespace arp::editor {
bool EditorSceneModalSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-SCENE-MODAL-SMOKE-INITIALIZE";
    tests::RecoveryFixture files;
    if (!shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok())
        return false;
    shell.activate("workspace_3D");
    if (shell.submitCommand(CreateSceneObjectCommand("Modal target")).status
        != EditorResponseStatus::Completed)
        return false;
    const auto object = shell.sceneDocument().selectedObject();
    const auto version = shell.stateVersion();
    if (shell.submitCommand(ActivateControlCommand("save_scene_as")).status
            != EditorResponseStatus::Completed
        || shell.stateVersion() != version + 1
        || shell.submitCommand(SetTextValueCommand{"scene_ui_path", "native.arp.yaml"}).status
            != EditorResponseStatus::Completed)
        return false;
    diagnostic = "EDITOR-SCENE-MODAL-SMOKE-SUBMIT";
    if (scenario != "scene_modal_open") {
        if (scenario == "scene_modal_confirm") {
            if (shell.submitCommand(ActivateControlCommand("scene_ui_cancel")).status
                    != EditorResponseStatus::Completed
                || shell.submitCommand(ActivateControlCommand("new_scene")).status
                    != EditorResponseStatus::Completed
                || shell.submitCommand(ActivateControlCommand("scene_ui_submit")).status
                    != EditorResponseStatus::Completed)
                return false;
        }
        EditorCommandEnvelope command(ActivateControlCommand(
            scenario == "scene_modal_confirm" ? "scene_ui_confirm" : "scene_ui_submit"));
        const auto failing = shell.stateVersion();
        EditorResponse response;
        {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            core::CpuMemoryQuotaScope scope(quota);
            response = shell.submitCommand(std::move(command));
        }
        if (response.status != EditorResponseStatus::Failed
            || response.diagnostic != EditorDiagnosticCode::CommandPreparationFailed
            || shell.stateVersion() != failing
            || !shell.runtime().canvas().findWidget(shell.id("scene_ui_failed"))->visible()
            || !shell.runtime().canvas().findWidget(shell.id("scene_ui_cancel"))->enabled()
            || shell.sceneDocument().selectedObject() != object)
            return false;
        if (scenario == "scene_modal_retry" || scenario == "scene_modal_saved") {
            if (shell.submitCommand(ActivateControlCommand("scene_ui_retry")).status
                    != EditorResponseStatus::Completed
                || shell.stateVersion() != failing
                || shell.runtime().canvas().findWidget(shell.id("scene_ui_failed"))->visible()
                || !shell.runtime().canvas().findWidget(shell.id("scene_ui_submit"))->enabled())
                return false;
            if (scenario == "scene_modal_saved") {
                const auto accepted =
                    shell.submitCommand(ActivateControlCommand("scene_ui_submit"));
                if (accepted.status != EditorResponseStatus::Completed || !accepted.hasSnapshot
                    || accepted.snapshot.scenePersistenceTaskId == 0
                    || tests::ScenePersistenceFixture::finish(shell, accepted)
                            .scenePersistenceTaskStatus
                        != ScenePersistenceTaskStatus::Succeeded
                    || shell.sceneDirty()
                    || shell.runtime().canvas().findWidget(shell.id("scene_ui_modal"))->visible()
                    || tests::RecoveryFixture::read(files.root / "native.arp.yaml").empty())
                    return false;
            }
        } else if (scenario != "scene_modal_failed" && scenario != "scene_modal_confirm")
            return false;
    }
    diagnostic.clear();
    std::printf(
        "PASS controlled native scene modal scenario=%.*s: preparation, fixed failure/retry, actual receipts and content verified\n",
        static_cast<int>(scenario.size()),
        scenario.data());
    return true;
}
} // namespace arp::editor
