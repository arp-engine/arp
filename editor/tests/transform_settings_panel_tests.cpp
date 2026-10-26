#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/transform_selection_fixture.h"
#include "editor/validation/editor_control_input.h"

#include <array>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;
using Fixture = tests::TransformSelectionFixture;
using Status = EditorResponseStatus;
using Settings = EditorSceneTransformSettings;

ui::Widget& widget(EditorShell& shell, const char* source)
{
    auto* value = shell.runtime().canvas().findWidget(shell.id(source));
    assert(value);
    return *value;
}

EditorResponse activate(EditorShell& shell, const char* source)
{
    return shell.submitCommand(ActivateControlCommand(source));
}

void click(EditorShell& shell, const char* source)
{
    assert(EditorControlInput::click(shell, source));
}

void text(EditorShell& shell, const char* source, std::string_view value)
{
    assert(shell.submitCommand(SetTextValueCommand(source, value)).status == Status::Completed);
}

std::string_view text(EditorShell& shell, const char* source)
{
    const auto* control = shell.runtime().findControlObject(shell.id(source));
    assert(control && control->value().kind == ui::UiControlValueKind::Text);
    return control->value().text;
}

bool opened(EditorShell& shell)
{
    return widget(shell, "transform_ui_modal").visible();
}

void draft(EditorShell& shell)
{
    click(shell, "transform_ui_tool"); // Select -> Move
    click(shell, "transform_ui_tool"); // Rotate
    click(shell, "transform_ui_space");
    click(shell, "transform_ui_pivot");
    click(shell, "transform_ui_pivot"); // Individual
    click(shell, "transform_ui_snap");
    text(shell, "transform_ui_move", "0.12345678901234566");
    text(shell, "transform_ui_rotate", "0.9876543210987654");
    text(shell, "transform_ui_scale", "0.33333333333333331");
}

void draftCancelAndApply(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.apply();
    f.history(true);
    const auto initial = f.shell.sceneTransformSettings();
    const auto before = f.shell.submitCommand(GetEditorStateQuery{}).snapshot;
    core::pmr::String hash, afterHash, diagnostic;
    assert(EditorSceneAuthoringCodec::contentHash(f.shell.sceneDocument(), hash, diagnostic));
    widget(f.shell, "file_root").setEnabled(false);
    click(f.shell, "scene_transform_settings");
    assert(opened(f.shell) && !widget(f.shell, "tool_1").enabled());
    const auto modalDraws = f.shell.drawCommands();
    assert(modalDraws.ok() && !modalDraws.commands().empty()
        && modalDraws.commands().back().widgetId == f.shell.id("transform_ui_cancel"));
    draft(f.shell);
    assert(f.shell.sceneTransformSettings().same(initial));
    assert(f.shell.submitCommand(SetSceneTransformSettingsCommand{initial}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    assert(f.shell.submitCommand(GetSceneTransformSettingsQuery{})
            .sceneTransformSettings.same(initial));
    click(f.shell, "transform_ui_cancel");
    assert(!opened(f.shell) && widget(f.shell, "tool_1").enabled()
        && !widget(f.shell, "file_root").enabled());
    assert(f.shell.sceneTransformSettings().same(initial));
    click(f.shell, "scene_transform_settings");
    assert(text(f.shell, "transform_ui_move") == "1.0");
    draft(f.shell);
    const auto version = f.shell.stateVersion();
    click(f.shell, "transform_ui_apply");
    assert(f.shell.stateVersion() == version + 1 && !opened(f.shell));
    const auto value = f.shell.sceneTransformSettings();
    assert(value.tool == EditorSceneTransformTool::Rotate
        && value.space == EditorSceneTransformSpace::Local
        && value.pivot == EditorSceneTransformPivot::Individual && value.snap.enabled
        && value.snap.moveStep == 0.12345678901234566
        && value.snap.rotateStepRadians == 0.9876543210987654
        && value.snap.scaleStep == 0.33333333333333331);
    assert(widget(f.shell, "scene_transform_space").desc().text == "Local"
        && widget(f.shell, "scene_transform_pivot").desc().text == "Pivot Individual");
    const auto after = f.shell.submitCommand(GetEditorStateQuery{}).snapshot;
    assert(before.selection == after.selection && before.sceneDirty == after.sceneDirty
        && before.scenePath == after.scenePath && before.sceneRuntimeId == after.sceneRuntimeId
        && before.sceneCameraYaw == after.sceneCameraYaw
        && before.sceneCameraPitch == after.sceneCameraPitch
        && before.sceneCameraDistance == after.sceneCameraDistance
        && core::distance(before.sceneCameraTarget, after.sceneCameraTarget) == 0);
    assert(EditorSceneAuthoringCodec::contentHash(f.shell.sceneDocument(), afterHash, diagnostic)
        && hash == afterHash);
    f.history(false);
    assert(f.local(f.first).translation.x == 3);
    f.history(true);
    assert(f.local(f.first).translation.x == 2 && f.shell.sceneTransformSettings().same(value));
    click(f.shell, "scene_transform_settings");
    assert(text(f.shell, "transform_ui_move") == "0.12345678901234566");
    assert(widget(f.shell, "transform_ui_tool").desc().text == "rotate");
    click(f.shell, "transform_ui_cancel");
    std::puts(
        "PASS native pointer draft/cancel/apply/reopen, exact double text, restored background flags, one Apply version, Scene/camera/history isolation");
}

void invalidInputAndBusy(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    click(f.shell, "scene_transform_settings");
    const auto initial = f.shell.sceneTransformSettings();
    for (const auto* field : {"transform_ui_move", "transform_ui_rotate", "transform_ui_scale"}) {
        const core::pmr::String old(text(f.shell, field));
        for (const auto* token : {"",
                 "0",
                 "-0",
                 "-1",
                 "nan",
                 "inf",
                 "1e9999",
                 "1e-9999",
                 "+1",
                 "1.2junk",
                 " 1",
                 "1 ",
                 "0x1p1"}) {
            text(f.shell, field, token);
            const auto version = f.shell.stateVersion();
            assert(activate(f.shell, "transform_ui_apply").status == Status::Failed);
            assert(opened(f.shell) && widget(f.shell, "transform_ui_invalid").visible());
            assert(f.shell.stateVersion() == version
                && f.shell.sceneTransformSettings().same(initial));
        }
        text(f.shell, field, old);
    }
    // Numeric input uses the real control event callback, not a shadow draft.
    text(f.shell, "transform_ui_move", "");
    click(f.shell, "transform_ui_move");
    assert(f.shell.runtime().dispatch(ui::UiInputEvent::textInput("5e-1")).ok());
    assert(text(f.shell, "transform_ui_move") == "5e-1");
    assert(!widget(f.shell, "transform_ui_invalid").visible());
    SetGamePreviewStateCommand play("transform settings pending Play");
    play.running = true;
    assert(f.shell.submitCommand(play).status == Status::Completed);
    assert(
        activate(f.shell, "transform_ui_apply").diagnostic == EditorDiagnosticCode::WorkspaceBusy);
    assert(opened(f.shell) && widget(f.shell, "transform_ui_failed").visible());
    assert(f.shell.sceneTransformSettings().same(initial));
    play.running = false;
    assert(f.shell.submitCommand(play).status == Status::Completed);
    click(f.shell, "transform_ui_apply");
    assert(!opened(f.shell) && f.shell.sceneTransformSettings().snap.moveStep == 0.5);
    assert(f.shell.submitCommand(SetTextValueCommand("transform_ui_move", "2")).status
        == Status::Failed);
    assert(activate(f.shell, "transform_ui_apply").status == Status::Failed);
    f.shell.activate("workspace_Game");
    assert(activate(f.shell, "scene_transform_settings").status == Status::Failed);
    std::puts(
        "PASS strict locale-independent invalid numeric fields even Snap Off, actual text event, Play Busy/retry, hidden drafts and launcher denied");
}

struct Flags {
    ui::WidgetId id;
    bool visible, enabled;
};

core::pmr::Vector<Flags> flags(EditorShell& shell)
{
    core::pmr::Vector<Flags> result;
    for (auto id : shell.runtime().canvas().traversal()) {
        const auto* value = shell.runtime().canvas().findWidget(id);
        result.push_back({id, value->visible(), value->enabled()});
    }
    return result;
}

void sameFlags(EditorShell& shell, const core::pmr::Vector<Flags>& values)
{
    for (auto value : values) {
        const auto* current = shell.runtime().canvas().findWidget(value.id);
        assert(
            current && current->visible() == value.visible && current->enabled() == value.enabled);
    }
}

void failureAndRetry(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    const auto original = f.shell.sceneTransformSettings();
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto initialFlags = flags(f.shell);
    EditorCommandPayload launch = ActivateControlCommand("scene_transform_settings");
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!f.shell.execute(launch).success);
    }
    assert(!opened(f.shell) && f.shell.sceneTransformSettings().same(original));
    sameFlags(f.shell, initialFlags);
    // Measure existing traversal's own peak, then allow that preparation but
    // reject the additional background snapshot while traversal remains alive.
    auto measured = core::makeShared<core::CpuMemoryQuota>();
    {
        core::CpuMemoryQuotaScope scope(measured);
        const auto order = f.shell.runtime().canvas().traversal();
        assert(!order.empty());
    }
    auto backgroundQuota = core::makeShared<core::CpuMemoryQuota>(measured->peakBytes());
    EditorModalSession session;
    bool rejected = false;
    const auto version = f.shell.stateVersion();
    try {
        core::CpuMemoryQuotaScope scope(backgroundQuota);
        (void)session.open(f.shell, "transform_ui_modal");
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    assert(rejected && backgroundQuota->liveBytes() == 0 && f.shell.stateVersion() == version);
    sameFlags(f.shell, initialFlags);
    assert(f.shell.sceneTransformSettings().same(original));
    click(f.shell, "scene_transform_settings");
    text(f.shell, "transform_ui_move", "2");
    EditorCommandPayload apply = ActivateControlCommand("transform_ui_apply");
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!f.shell.execute(apply).success);
    }
    assert(opened(f.shell) && widget(f.shell, "transform_ui_failed").visible());
    assert(text(f.shell, "transform_ui_move") == "2"
        && f.shell.sceneTransformSettings().same(original));
    click(f.shell, "transform_ui_apply");
    assert(!opened(f.shell) && f.shell.sceneTransformSettings().snap.moveStep == 2);
    // General modal close after release uses only cached IDs/flags; no allocation.
    assert(session.open(f.shell, "transform_ui_modal"));
    const auto before = core::defaultCpuAllocator().stats();
    {
        core::CpuMemoryQuotaScope scope(quota);
        session.close(f.shell);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == before.allocationCount);
    assert(quota->liveBytes() == 0);
    std::puts(
        "PASS OOM opening and background-snapshot preparation preserve all flags/preferences, Apply failure keeps draft with fixed retry hint, allocation-free modal close");
}

void protocol(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    tests::EditorProtocolFixture remote(f.shell);
    for (const bool mcp : {false, true}) {
        const auto success = [&](std::string_view name, std::string_view fields) {
            const auto result = remote.call(name, fields, mcp);
            assert(mcp ? result.body_text().find(R"("isError":false)") != std::string_view::npos
                       : result.status().value() == 200);
        };
        success("editor.activate", R"("source":"scene_transform_settings")");
        success("editor.text.set", R"("source":"transform_ui_move","value":"1.25")");
        success("editor.activate", R"("source":"transform_ui_snap")");
        assert(f.shell.submitCommand(SetSceneTransformSettingsCommand{}).diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        success("scene.transform.settings.get", {});
        const auto rejected =
            remote.call("editor.text.set", R"("source":"transform_ui_rotate","value":"1\n2")", mcp);
        assert(mcp ? rejected.body_text().find(R"("isError":true)") != std::string_view::npos
                   : rejected.status().value() == 400);
        success("editor.activate", R"("source":"transform_ui_apply")");
        assert(!opened(f.shell) && f.shell.sceneTransformSettings().snap.moveStep == 1.25);
        const auto hidden =
            remote.call("editor.text.set", R"("source":"transform_ui_move","value":"2")", mcp);
        assert(hidden.body_text().find(mcp ? R"("isError":true)" : R"("status":"failed")")
            != std::string_view::npos);
    }
    click(f.shell, "scene_transform_settings");
    const auto old = core::pmr::String(text(f.shell, "transform_ui_move"));
    assert(
        f.shell.submitCommand(SetTextValueCommand("transform_ui_move", core::pmr::String(65, '1')))
            .status
        == Status::Failed);
    assert(text(f.shell, "transform_ui_move") == old);
    assert(
        f.shell
            .submitCommand(SetTextValueCommand("transform_ui_move", core::pmr::String("a\0b", 3)))
            .status
        == Status::Failed);
    click(f.shell, "transform_ui_cancel");
    std::puts(
        "PASS REST/MCP same settings panel draft/Apply gate, hidden text rejection, malformed text and explicit 64-byte budget");
}

void lifecycle(const tests::RecoveryFixture& files)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Fixture f(files);
            click(f.shell, "scene_transform_settings");
            text(f.shell, "transform_ui_move", "0");
            assert(activate(f.shell, "transform_ui_apply").status == Status::Failed);
            text(f.shell, "transform_ui_move", "0.5");
            click(f.shell, "transform_ui_apply");
            click(f.shell, "scene_transform_settings");
            text(f.shell, "transform_ui_scale", "2");
            click(f.shell, "transform_ui_cancel");
            assert(f.shell.drawCommands().ok());
            click(f.shell, "scene_transform_settings"); // destruction while draft open
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary settings-modal invalid/retry/Apply/Cancel/draw/open-destruction cycles core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle(files);
    else {
        draftCancelAndApply(files);
        invalidInputAndBusy(files);
        failureAndRetry(files);
        protocol(files);
    }
}
