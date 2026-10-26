#include "editor/tests/fixtures/scene_persistence_fixture.h"
#include "editor/tests/fixtures/transform_selection_fixture.h"
#include "editor/validation/editor_scene_keyboard_smoke.h"
#include "tests/fixtures/ui_control_event_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using Fixture = arp::editor::tests::TransformSelectionFixture;

EditorResponse key(EditorShell& shell, EditorInputKey value, bool down = true)
{
    EditorInputCommand input;
    input.kind = down ? EditorInputKind::KeyDown : EditorInputKind::KeyUp;
    input.key = value;
    return shell.submitCommand(input);
}

bool probe(const arp::editor::tests::RecoveryFixture& files)
{
    bool passed = true;
    {
        Fixture f(files);
        auto version = f.shell.stateVersion();
        key(f.shell, EditorInputKey::Other);
        const auto ignored = f.shell.stateVersion() - version;
        key(f.shell, EditorInputKey::LeftControl);
        version = f.shell.stateVersion();
        key(f.shell, EditorInputKey::Z);
        const auto empty = f.shell.stateVersion() - version;
        std::printf("ignored versionDelta=%llu; empty Undo versionDelta=%llu\n",
            static_cast<unsigned long long>(ignored),
            static_cast<unsigned long long>(empty));
        passed &= ignored == 0 && empty == 0;
        key(f.shell, EditorInputKey::LeftControl, false);
        assert(f.shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status
            == EditorResponseStatus::Completed);
        assert(f.shell.submitCommand(SetSceneObjectTranslationCommand{5, 0, 0}).status
            == EditorResponseStatus::Completed);
        key(f.shell, EditorInputKey::LeftControl);
        version = f.shell.stateVersion();
        const auto result = key(f.shell, EditorInputKey::Z);
        const auto delta = f.shell.stateVersion() - version;
        std::printf("real Undo versionDelta=%llu restored=%u\n",
            static_cast<unsigned long long>(delta),
            f.local(f.first).translation.x == 2);
        passed &= result.status == EditorResponseStatus::Completed && delta == 1
            && f.local(f.first).translation.x == 2;
    }
    {
        Fixture f(files);
        auto version = f.shell.stateVersion();
        key(f.shell, EditorInputKey::F);
        const auto focus = f.shell.stateVersion() - version;
        version = f.shell.stateVersion();
        key(f.shell, EditorInputKey::F);
        const auto repeat = f.shell.stateVersion() - version;
        version = f.shell.stateVersion();
        key(f.shell, EditorInputKey::R);
        const auto tool = f.shell.stateVersion() - version;
        version = f.shell.stateVersion();
        key(f.shell, EditorInputKey::R);
        const auto toolRepeat = f.shell.stateVersion() - version;
        std::printf("focus versionDelta=%llu repeat=%llu; tool versionDelta=%llu repeat=%llu\n",
            static_cast<unsigned long long>(focus),
            static_cast<unsigned long long>(repeat),
            static_cast<unsigned long long>(tool),
            static_cast<unsigned long long>(toolRepeat));
        passed &= focus == 1 && repeat == 0 && tool == 1 && toolRepeat == 0;
    }
    {
        Fixture f(files);
        const auto version = f.shell.stateVersion();
        EditorResponse result;
        const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            result = key(f.shell, EditorInputKey::F);
        }
        const auto delta = f.shell.stateVersion() - version;
        std::printf("focus quota1 status=%u diagnostic=%u versionDelta=%llu\n",
            static_cast<unsigned>(result.status),
            static_cast<unsigned>(result.diagnostic),
            static_cast<unsigned long long>(delta));
        passed &= result.status == EditorResponseStatus::Failed
            && result.diagnostic == EditorDiagnosticCode::InvalidCommandPayload && delta == 0;
    }
    return passed;
}

void expect(EditorShell& shell,
    EditorInputKey value,
    std::uint64_t delta,
    EditorResponseStatus status = EditorResponseStatus::Completed,
    EditorDiagnosticCode diagnostic = EditorDiagnosticCode::None)
{
    const auto before = shell.stateVersion();
    const auto response = key(shell, value);
    if (response.status != status || response.diagnostic != diagnostic
        || shell.stateVersion() != before + delta)
        std::fprintf(stderr,
            "key=%u status=%u/%u diagnostic=%u/%u versionDelta=%llu/%llu\n",
            static_cast<unsigned>(value),
            static_cast<unsigned>(response.status),
            static_cast<unsigned>(status),
            static_cast<unsigned>(response.diagnostic),
            static_cast<unsigned>(diagnostic),
            static_cast<unsigned long long>(shell.stateVersion() - before),
            static_cast<unsigned long long>(delta));
    assert(response.status == status && response.diagnostic == diagnostic
        && shell.stateVersion() == before + delta);
}

void historyAndNoOps(const arp::editor::tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto& shell = f.shell;
    for (bool down : {true, false}) {
        const auto version = shell.stateVersion();
        key(shell, EditorInputKey::Other, down);
        assert(shell.stateVersion() == version);
    }
    EditorInputCommand text;
    text.kind = EditorInputKind::Text;
    text.text = "unhandled";
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(text).status == EditorResponseStatus::Completed
        && shell.stateVersion() == version);
    key(shell, EditorInputKey::RightControl);
    expect(shell, EditorInputKey::Z, 0);
    expect(shell, EditorInputKey::Y, 0);
    key(shell, EditorInputKey::RightShift);
    expect(shell, EditorInputKey::Z, 0);
    key(shell, EditorInputKey::RightShift, false);
    key(shell, EditorInputKey::RightControl, false);
    assert(shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(SetSceneObjectTranslationCommand{5, 0, 0}).status
        == EditorResponseStatus::Completed);
    key(shell, EditorInputKey::LeftControl);
    key(shell, EditorInputKey::RightControl);
    key(shell, EditorInputKey::LeftControl, false);
    expect(shell, EditorInputKey::Z, 1);
    assert(f.local(f.first).translation.x == 2);
    key(shell, EditorInputKey::LeftShift);
    expect(shell, EditorInputKey::Z, 1);
    assert(f.local(f.first).translation.x == 5);
    expect(shell, EditorInputKey::Z, 0);
    key(shell, EditorInputKey::LeftShift, false);
    const auto epoch = shell.submitCommand(GetSceneInspectorSelectionQuery{})
                           .sceneInspectorSelection.documentEpoch;
    assert(
        shell.submitCommand(SetSceneObjectViewCommand{f.first.value(), epoch, false, true}).status
        == EditorResponseStatus::Completed);
    expect(shell,
        EditorInputKey::Z,
        0,
        EditorResponseStatus::Failed,
        EditorDiagnosticCode::SceneObjectLocked);
    assert(f.local(f.first).translation.x == 5);
    assert(
        shell.submitCommand(SetSceneObjectViewCommand{f.first.value(), epoch, false, false}).status
        == EditorResponseStatus::Completed);
    expect(shell, EditorInputKey::Z, 1);
    expect(shell, EditorInputKey::Y, 1);
    assert(f.local(f.first).translation.x == 5);
    std::puts(
        "PASS ignored/empty/history/locked refusal and remaining-side shortcuts have exact receipts");
}

void focusToolsAndQuota(const arp::editor::tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto& shell = f.shell;
    expect(shell, EditorInputKey::F, 1);
    expect(shell, EditorInputKey::F, 0);
    expect(shell, EditorInputKey::R, 1);
    expect(shell, EditorInputKey::R, 0);
    expect(shell, EditorInputKey::Q, 1);
    expect(shell, EditorInputKey::W, 1);
    expect(shell, EditorInputKey::E, 1);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        expect(shell,
            EditorInputKey::F,
            0,
            EditorResponseStatus::Failed,
            EditorDiagnosticCode::InvalidCommandPayload);
        expect(shell,
            EditorInputKey::R,
            0,
            EditorResponseStatus::Failed,
            EditorDiagnosticCode::InvalidCommandPayload);
    }
    assert(shell.sceneTransformTool() == EditorSceneTransformTool::Rotate && !quota->liveBytes());
    SetGamePreviewStateCommand play("scene key gate");
    play.running = true;
    assert(shell.submitCommand(play).status == EditorResponseStatus::Completed);
    expect(shell,
        EditorInputKey::R,
        0,
        EditorResponseStatus::Failed,
        EditorDiagnosticCode::WorkspaceBusy);
    expect(shell,
        EditorInputKey::F,
        0,
        EditorResponseStatus::Failed,
        EditorDiagnosticCode::WorkspaceBusy);
    play.running = false;
    assert(shell.submitCommand(play).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(ActivateControlCommand("scene_transform_settings")).status
        == EditorResponseStatus::Completed);
    expect(shell, EditorInputKey::R, 0);
    expect(shell, EditorInputKey::F, 0);
    assert(shell.submitCommand(ActivateControlCommand("transform_ui_cancel")).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(BeginSceneSelectionTransformEditCommand{}).status
        == EditorResponseStatus::Completed);
    expect(shell,
        EditorInputKey::F,
        0,
        EditorResponseStatus::Failed,
        EditorDiagnosticCode::WorkspaceBusy);
    EditorInputCommand lost;
    lost.kind = EditorInputKind::FocusLost;
    assert(shell.submitCommand(lost).status == EditorResponseStatus::Completed);
    std::puts(
        "PASS F/tool changes/no-ops, quota, Play, modal and owned gesture preserve actual diagnostics");
}

void textPriorityAndNavigation(const arp::editor::tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto& shell = f.shell;
    assert(shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status
        == EditorResponseStatus::Completed);
    auto numeric = arp::tests::UiControlEventFixture::control(ui::UiControlType::NumericInput);
    numeric.widget.name = "scene-keyboard-number";
    const auto created =
        shell.runtime().createControl(std::move(numeric), shell.runtime().rootId());
    assert(created.ok());
    for (const auto id : {shell.id("scene_name"), created.widgetId()}) {
        assert(shell.runtime().requestFocus(id).ok());
        const auto tool = shell.sceneTransformTool();
        for (const auto value : {EditorInputKey::Q,
                 EditorInputKey::W,
                 EditorInputKey::E,
                 EditorInputKey::R,
                 EditorInputKey::F})
            expect(shell, value, 0);
        key(shell, EditorInputKey::LeftControl);
        for (const auto value : {EditorInputKey::Z, EditorInputKey::Y, EditorInputKey::S})
            expect(shell, value, 0);
        key(shell, EditorInputKey::LeftControl, false);
        assert(shell.sceneTransformTool() == tool && shell.runtime().focusedControl() == id);
    }
    assert(shell.runtime().requestFocus({}).ok());
    assert(shell.runtime().removeControl(created.widgetId()).ok());
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(shell.id("viewport"));
    assert(viewport);
    EditorInputCommand pointer;
    pointer.kind = EditorInputKind::PointerDown;
    pointer.button = EditorPointerButton::Right;
    pointer.x = viewport->rect.x + viewport->rect.width * .5f;
    pointer.y = viewport->rect.y + viewport->rect.height * .5f;
    assert(shell.submitCommand(pointer).status == EditorResponseStatus::Completed);
    const auto target = shell.sceneViewportCamera().target();
    expect(shell, EditorInputKey::W, 1);
    assert(core::distance(target, shell.sceneViewportCamera().target()) > 0);
    pointer.kind = EditorInputKind::PointerUp;
    assert(shell.submitCommand(pointer).status == EditorResponseStatus::Completed);
    shell.activate("workspace_Game");
    expect(shell, EditorInputKey::Other, 0);
    std::puts(
        "PASS actual TextInput/NumericInput priority, right-held camera command and inactive workspace no-op");
}

void saveWorkflow(const arp::editor::tests::RecoveryFixture& files)
{
    Fixture f(files);
    auto& shell = f.shell;
    assert(shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status
        == EditorResponseStatus::Completed);
    SaveSceneCommand initial("keyboard.arp.yaml");
    initial.overwriteExisting = true; // This fixture owns this isolated path.
    const auto accepted = shell.submitCommand(initial);
    assert(arp::editor::tests::ScenePersistenceFixture::finish(shell, accepted)
               .scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(shell.submitCommand(SetSceneObjectTranslationCommand{7, 0, 0}).status
        == EditorResponseStatus::Completed);
    key(shell, EditorInputKey::RightControl);
    // Accepted IO is pending; the typed save advances only on completion.
    const auto savingVersion = shell.stateVersion();
    const auto pending = key(shell, EditorInputKey::S);
    assert(pending.status == EditorResponseStatus::Completed
        && shell.stateVersion() == savingVersion && pending.hasSnapshot
        && pending.snapshot.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Pending
        && pending.snapshot.scenePersistenceTaskId != 0
        && pending.snapshot.scenePersistenceTaskId != accepted.snapshot.scenePersistenceTaskId);
    expect(shell,
        EditorInputKey::S,
        0,
        EditorResponseStatus::Failed,
        EditorDiagnosticCode::WorkspaceBusy);
    assert(arp::editor::tests::ScenePersistenceFixture::finish(shell, pending)
               .scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    assert(!shell.sceneDirty()
        && !arp::editor::tests::RecoveryFixture::read(files.root / "keyboard.arp.yaml").empty());
    key(shell, EditorInputKey::RightShift);
    expect(shell, EditorInputKey::S, 1);
    assert(shell.runtime().canvas().findWidget(shell.id("scene_ui_modal"))->visible());
    expect(shell, EditorInputKey::S, 0);
    assert(shell.submitCommand(ActivateControlCommand("scene_ui_cancel")).status
        == EditorResponseStatus::Completed);
    assert(shell.runtime().requestFocus({}).ok());
    SetGamePreviewStateCommand play("save gate");
    play.running = true;
    assert(shell.submitCommand(play).status == EditorResponseStatus::Completed);
    key(shell, EditorInputKey::LeftControl);
    expect(shell,
        EditorInputKey::S,
        0,
        EditorResponseStatus::Failed,
        EditorDiagnosticCode::WorkspaceBusy);
    assert(!shell.runtime().canvas().findWidget(shell.id("scene_ui_modal"))->visible());
    std::puts(
        "PASS Ctrl+S real async save/task refusal, Shift+Ctrl+S one modal version, Play rejection");
}

void nativePreparation(std::string_view scenario)
{
    EditorShell shell;
    assert(shell
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                std::filesystem::path(ARP_SOURCE_DIR))
            .ok());
    core::pmr::String diagnostic;
    assert(EditorSceneKeyboardSmoke::run(shell, scenario, diagnostic));
}
} // namespace

int main(int argc, char** argv)
{
    arp::editor::tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--probe")
        return probe(files) ? 0 : 2;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        historyAndNoOps(files);
        focusToolsAndQuota(files);
        textPriorityAndNavigation(files);
        saveWorkflow(files);
        for (const auto scenario : {"scene_keys_tool", "scene_keys_text", "scene_keys_save"})
            nativePreparation(scenario);
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary scene keyboard/history/focus/save/modal/destruction cycles: core live bytes/count delta=0");
    return probe(files) ? 0 : 2;
}
