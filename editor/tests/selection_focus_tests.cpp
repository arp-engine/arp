#include "editor/ingress/platform_input_adapter.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_key_input.h"
#include "editor/viewport/editor_scene_selection_focus.h"
#include "editor/workspace/editor_shell.h"
#include "tests/fixtures/ui_control_event_fixture.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;
using Focus = EditorSceneSelectionFocus;

bool close(core::Vec3 a, core::Vec3 b)
{
    return std::abs(a.x - b.x) < 0.0001f && std::abs(a.y - b.y) < 0.0001f
        && std::abs(a.z - b.z) < 0.0001f;
}

void samePose(const EditorSceneViewportCamera& a, const EditorSceneViewportCamera& b)
{
    assert(close(a.target(), b.target()) && a.distance() == b.distance()
        && a.yawDegrees() == b.yawDegrees() && a.pitchDegrees() == b.pitchDegrees());
}

void initialize(EditorShell& shell, const arp::editor::tests::RecoveryFixture& fixture)
{
    assert(shell
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                fixture.root)
            .ok());
}

void numerical()
{
    scene::Scene scene("focus", scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
    auto& parent = scene.createObject("parent");
    scene::Transform transform;
    transform.translation = {10, 4, -3};
    transform.scale = {2, 3, 1};
    transform.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 1.57079632679f);
    parent.set_local_transform(transform);
    parent.setActive(false);
    auto& child = scene.createObject("child");
    child.set_local_transform(scene::Transform::fromTranslation({1, 2, 1}));
    assert(scene.reparent(child.id(), parent.id()).ok());
    auto& other = scene.createObject("other");
    other.set_local_transform(scene::Transform::fromTranslation({-4, -2, 8}));
    const auto snapshot = scene.extractTraversalSnapshot({});
    assert(snapshot.ok() && !snapshot.requireNode(child.id()).activeInHierarchy);
    EditorSceneViewportCamera camera;
    const auto original = camera;
    const auto before = core::defaultCpuAllocator().stats();
    assert(Focus::apply(camera, snapshot, child.id(), {}, 972, 689) == Focus::Result::Changed);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
    assert(close(camera.target(), {4, 6, -2}) && camera.distance() == original.distance()
        && camera.yawDegrees() == original.yawDegrees()
        && camera.pitchDegrees() == original.pitchDegrees());
    float x, y;
    assert(camera.project({4, 6, -2}, 972, 689, x, y) && std::abs(x - 486) < 0.001f
        && std::abs(y - 344.5f) < 0.001f);
    const std::array additional{other.id()};
    assert(
        Focus::apply(camera, snapshot, child.id(), additional, 972, 689) == Focus::Result::Changed
        && close(camera.target(), {0, 2, 3}));
    assert(Focus::apply(camera, snapshot, child.id(), additional, 972, 689)
        == Focus::Result::Unchanged);
    const auto saved = camera;
    const std::array repeated{other.id(), other.id()};
    assert(Focus::apply(camera, snapshot, child.id(), repeated, 972, 689)
        == Focus::Result::InvalidSelection);
    const std::array primary{child.id()};
    assert(Focus::apply(camera, snapshot, child.id(), primary, 972, 689)
        == Focus::Result::InvalidSelection);
    assert(Focus::apply(camera, snapshot, scene::GameObjectId::fromValue(99999), {}, 972, 689)
        == Focus::Result::InvalidSelection);
    assert(
        Focus::apply(camera, snapshot, child.id(), {}, 0, 689) == Focus::Result::InvalidViewport);
    assert(
        Focus::apply(camera, snapshot, child.id(), {}, 972, std::numeric_limits<float>::quiet_NaN())
        == Focus::Result::InvalidViewport);
    auto nodes = snapshot.nodes();
    for (auto& node : nodes)
        if (node.objectId == child.id())
            node.worldTransform.translation.x = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = scene::SceneTraversalSnapshot::success(std::move(nodes));
    assert(
        Focus::apply(camera, invalid, child.id(), {}, 972, 689) == Focus::Result::InvalidTransform);
    nodes = snapshot.nodes();
    for (auto& node : nodes)
        if (node.objectId == child.id())
            node.worldTransform.translation = {std::numeric_limits<float>::max(), 0, 0};
    const auto huge = scene::SceneTraversalSnapshot::success(std::move(nodes));
    assert(Focus::apply(camera, huge, child.id(), {}, 972, 689) == Focus::Result::CameraRange);
    samePose(camera, saved);
    std::puts(
        "PASS world-origin focus: native parent TRS/inactive, multi-select center, no allocation, invalid/stale/duplicate/precision failure preserves camera");
}

void commandAndInput(const arp::editor::tests::RecoveryFixture& fixture)
{
    EditorShell shell;
    initialize(shell, fixture);
    shell.activate("workspace_3D");
    assert(shell.submitCommand(CreateSceneObjectCommand("focus target")).status
        == EditorResponseStatus::Completed);
    const auto object = shell.sceneDocument().selectedObject();
    assert(shell.submitCommand(SetSceneObjectTranslationCommand{11, 2, 3}).status
        == EditorResponseStatus::Completed);
    core::pmr::String baseline, diagnostic, after;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), baseline, diagnostic));
    const auto dirty = shell.sceneDirty();
    const auto pose = shell.sceneViewportCamera();
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(FocusSceneSelectionCommand{}).status
        == EditorResponseStatus::Completed);
    assert(shell.stateVersion() == version + 1
        && close(shell.sceneViewportCamera().target(), {11, 2, 3})
        && shell.sceneViewportCamera().distance() == pose.distance());
    assert(shell.submitCommand(FocusSceneSelectionCommand{}).stateVersion == version + 1);
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic)
        && after == baseline && shell.sceneDocument().selectedObject() == object
        && shell.sceneDirty() == dirty);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(
        close(shell.sceneDocument().scene().findObject(object)->local_transform().translation, {}));
    assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);

    // The native WindowKey normalization and the typed input share the same path.
    platform::WindowInputEvent native;
    native.kind = platform::WindowInputEvent::Kind::KeyDown;
    native.key = platform::WindowKey::F;
    const auto key = normalizePlatformInput(native);
    assert(key.key == EditorInputKey::F);
    shell.sceneDocument().setSelectedTranslation(8, -1, 4);
    assert(shell.runtime().requestFocus(shell.id("scene_name")).ok());
    const auto focused = shell.sceneViewportCamera();
    (void)shell.submitCommand(key);
    samePose(shell.sceneViewportCamera(), focused);
    assert(shell.runtime().requestFocus({}).ok());
    EditorInputCommand shift;
    shift.kind = EditorInputKind::KeyDown;
    shift.key = EditorInputKey::LeftShift;
    (void)shell.submitCommand(shift);
    (void)shell.submitCommand(key);
    samePose(shell.sceneViewportCamera(), focused);
    shift.kind = EditorInputKind::KeyUp;
    (void)shell.submitCommand(shift);
    (void)shell.submitCommand(key);
    assert(close(shell.sceneViewportCamera().target(), {8, -1, 4}));
    shell.activate("workspace_Game");
    shell.sceneDocument().setSelectedTranslation(7, -1, 4);
    const auto gamePose = shell.sceneViewportCamera();
    (void)shell.submitCommand(key);
    samePose(shell.sceneViewportCamera(), gamePose);
    shell.activate("workspace_3D");
    shell.activate("open_scene");
    const auto modalPose = shell.sceneViewportCamera();
    assert(shell.submitCommand(FocusSceneSelectionCommand{}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    (void)shell.submitCommand(key);
    samePose(shell.sceneViewportCamera(), modalPose);
    shell.activate("scene_ui_cancel");
    assert(shell.submitCommand(BeginSceneTransformEditCommand{object.value()}).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(FocusSceneSelectionCommand{}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    (void)shell.submitCommand(EndSceneTransformEditCommand{object.value()});
    const auto beforeQuota = shell.sceneViewportCamera();
    auto quota = core::makeShared<core::CpuMemoryQuota>();
    quota->setLimitBytes(1);
    const auto beforeVersion = shell.stateVersion();
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.submitCommand(FocusSceneSelectionCommand{}).status
            == EditorResponseStatus::Failed);
    }
    assert(!quota->liveBytes() && shell.stateVersion() == beforeVersion);
    samePose(shell.sceneViewportCamera(), beforeQuota);
    const auto state = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    assert(close(state.sceneCameraTarget, beforeQuota.target())
        && state.sceneCameraDistance == beforeQuota.distance());
    std::puts(
        "PASS shared focus command/F normalization, no-op, document bytes/selection/dirty/Undo/Redo isolation, text/Shift/Game/modal/gesture/quota guards, readonly camera snapshot");
}

void protocol(const arp::editor::tests::RecoveryFixture& fixture)
{
    EditorShell shell;
    initialize(shell, fixture);
    shell.activate("workspace_3D");
    assert(shell.submitCommand(CreateSceneObjectCommand("remote focus")).status
        == EditorResponseStatus::Completed);
    shell.sceneDocument().setSelectedTranslation(3, 2, 1);
    EditorRemoteProtocol protocol("01234567890123456789012345678901");
    const auto request = [&](std::string_view body, bool mcp = false) {
        network::http::Request request;
        request.set_method(network::http::Method::Post);
        request.set_target(mcp ? "/mcp" : "/api/v1/commands");
        request.add_header("Authorization", "Bearer 01234567890123456789012345678901");
        request.add_header("Accept", "application/json");
        request.add_header("MCP-Protocol-Version", "2025-11-25");
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        return protocol.handle(request, shell);
    };
    const auto prefix =
        core::pmr::String(R"({"schemaVersion":1,"command":"scene.selection.focus")");
    for (const auto suffix : {R"(,"objectId":1})", R"(,"path":"bad"})", R"(,"unknown":1})"})
        assert(request(prefix + suffix).status().value() == 400);
    assert(request(prefix + R"(,"expectedStateVersion":0})").body_text().find("stale_state_version")
        != std::string_view::npos);
    assert(request(prefix + R"(,"idempotencyKey":"focus-1"})").status().value() == 200);
    assert(close(shell.sceneViewportCamera().target(), {3, 2, 1}));
    shell.sceneDocument().setSelectedTranslation(-2, 4, 8);
    assert(request(prefix + R"(,"idempotencyKey":"focus-1"})").status().value() == 200);
    assert(close(shell.sceneViewportCamera().target(), {3, 2, 1}));
    assert(
        request(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.selection.focus","arguments":{}}})",
            true)
            .body_text()
            .find(R"("isError":false)")
        != std::string_view::npos);
    assert(close(shell.sceneViewportCamera().target(), {-2, 4, 8}));
    assert(
        request(
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene.selection.focus","arguments":{"objectId":1}}})",
            true)
            .body_text()
            .find("unknown_tool_argument")
        != std::string_view::npos);
    const auto state = request(R"({"schemaVersion":1,"command":"editor.state.get"})");
    assert(state.body_text().find(R"("sceneCamera":{"target":{"x":-2,"y":4,"z":8})")
        != std::string_view::npos);
    std::puts(
        "PASS REST/MCP focus strict fields, stale version, replay isolation and camera state serialization");
}

void toolInputPriority(EditorShell& shell, const arp::editor::tests::RecoveryFixture& fixture)
{
    using Key = platform::WindowKey;
    using Tool = EditorSceneTransformTool;
    const auto key = [&](Key value, bool down = true) {
        EditorKeyInput::send(shell, value, down);
    };
    const std::array keys{Key::Q, Key::W, Key::E, Key::R};
    const std::array normalized{
        EditorInputKey::Q, EditorInputKey::W, EditorInputKey::E, EditorInputKey::R};
    const std::array tools{Tool::Select, Tool::Move, Tool::Rotate, Tool::Scale};
    core::pmr::String before, after, diagnostic;
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), before, diagnostic));
    const auto selected = shell.sceneDocument().selectedObject();
    for (std::size_t i = 0; i != keys.size(); ++i) {
        platform::WindowInputEvent native;
        native.kind = platform::WindowInputEvent::Kind::KeyDown;
        native.key = keys[i];
        assert(normalizePlatformInput(native).key == normalized[i]);
        key(keys[i]);
        assert(shell.sceneTransformSettings().tool == tools[i]);
    }
    const auto focusedGuard = [&](ui::WidgetId id) {
        assert(shell.runtime().requestFocus(id).ok());
        const auto settings = shell.sceneTransformSettings();
        const auto camera = shell.sceneViewportCamera();
        for (const auto value : {Key::Q, Key::W, Key::E, Key::R, Key::F})
            key(value);
        assert(shell.sceneTransformSettings().same(settings));
        samePose(shell.sceneViewportCamera(), camera);
        assert(shell.runtime().focusedControl() == id);
        assert(shell.runtime().requestFocus({}).ok());
    };
    const auto name = shell.id("scene_name");
    const auto nameText = core::pmr::String(shell.runtime().findControlObject(name)->value().text);
    focusedGuard(name);
    assert(shell.runtime().findControlObject(name)->value().text == nameText);
    // Editor authoring currently uses TextInput for numeric fields. Exercise the
    // generic NumericInput branch with an explicit real UI control fixture.
    auto number = arp::tests::UiControlEventFixture::control(ui::UiControlType::NumericInput);
    number.widget.name = "keyboard-priority-numeric-fixture";
    number.initialValue = ui::UiControlValue::fromNumber(0);
    const auto created = shell.runtime().createControl(std::move(number), shell.runtime().rootId());
    assert(created.ok());
    const auto numeric = created.widgetId();
    const auto numericValue = shell.runtime().findControlObject(numeric)->value().number;
    focusedGuard(numeric);
    assert(shell.runtime().findControlObject(numeric)->value().number == numericValue);
    assert(shell.runtime().removeControl(numeric).ok());
    for (const auto control : {Key::LeftControl, Key::RightControl}) {
        key(control);
        key(Key::W);
        assert(shell.sceneTransformSettings().tool == Tool::Scale);
        key(control, false);
    }
    key(Key::LeftShift);
    key(Key::W);
    assert(shell.sceneTransformSettings().tool == Tool::Move);
    const auto shiftedCamera = shell.sceneViewportCamera();
    key(Key::F);
    samePose(shell.sceneViewportCamera(), shiftedCamera);
    key(Key::LeftShift, false);
    // FocusLost clears a held modifier, so the next plain tool key works.
    key(Key::RightControl);
    EditorKeyInput::focusLost(shell);
    key(Key::R);
    assert(shell.sceneTransformSettings().tool == Tool::Scale);

    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(shell.id("viewport"));
    assert(viewport);
    platform::WindowInputEvent pointer;
    pointer.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
    pointer.mouseButton = platform::WindowMouseButton::Right;
    pointer.x = viewport->rect.x + viewport->rect.width * .5f;
    pointer.y = viewport->rect.y + viewport->rect.height * .5f;
    shell.handlePlatformInput(pointer);
    const auto looking = shell.sceneViewportCamera();
    key(Key::W);
    assert(shell.sceneTransformSettings().tool == Tool::Scale);
    assert(!close(shell.sceneViewportCamera().target(), looking.target()));
    key(Key::E);
    assert(shell.sceneTransformSettings().tool == Tool::Scale);
    pointer.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
    shell.handlePlatformInput(pointer);
    key(Key::E);
    assert(shell.sceneTransformSettings().tool == Tool::Rotate);

    assert(shell.submitCommand(ActivateControlCommand("scene_transform_settings")).status
        == EditorResponseStatus::Completed);
    focusedGuard(shell.id("transform_ui_move"));
    assert(shell.sceneTransformSettings().tool == Tool::Rotate);
    assert(shell.submitCommand(ActivateControlCommand("transform_ui_cancel")).status
        == EditorResponseStatus::Completed);
    SetGamePreviewStateCommand play("keyboard priority fixture");
    for (const bool starting : {true, false}) {
        play.starting = starting;
        play.running = !starting;
        assert(shell.submitCommand(play).status == EditorResponseStatus::Completed);
        const auto camera = shell.sceneViewportCamera();
        for (auto value : {Key::Q, Key::W, Key::E, Key::R, Key::F})
            key(value);
        assert(shell.sceneTransformSettings().tool == Tool::Rotate);
        samePose(shell.sceneViewportCamera(), camera);
    }
    play.running = play.starting = false;
    assert(shell.submitCommand(play).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(BeginSceneTransformEditCommand{selected.value()}).status
        == EditorResponseStatus::Completed);
    key(Key::W);
    assert(shell.sceneTransformSettings().tool == Tool::Rotate);
    EditorKeyInput::focusLost(shell);
    key(Key::W);
    assert(shell.sceneTransformSettings().tool == Tool::Move);
    const auto begun = shell.submitCommand(BeginSceneSelectionTransformEditCommand{});
    assert(begun.status == EditorResponseStatus::Completed);
    key(Key::R);
    assert(shell.sceneTransformSettings().tool == Tool::Move);
    EditorKeyInput::focusLost(shell);
    key(Key::R);
    assert(shell.sceneTransformSettings().tool == Tool::Scale);

    shell.activate("workspace_Game");
    for (auto value : keys)
        key(value);
    assert(shell.sceneTransformSettings().tool == Tool::Scale);
    const auto script = fixture.root / "keyboard-priority.arp";
    arp::editor::tests::RecoveryFixture::write(script, "pass\n");
    assert(shell.openScriptFile(script));
    assert(shell.scriptEditorCapturesKeyboard());
    for (auto value : keys)
        key(value);
    platform::WindowInputEvent typed;
    typed.kind = platform::WindowInputEvent::Kind::TextInput;
    typed.text = "qwer";
    shell.handlePlatformInput(typed);
    assert(shell.scriptText().find("qwer") != shell.scriptText().npos);
    assert(shell.sceneTransformSettings().tool == Tool::Scale);
    shell.activate("workspace_3D");
    key(Key::Q);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        key(Key::E);
    }
    assert(shell.sceneTransformSettings().tool == Tool::Select && !quota->liveBytes());
    key(Key::E);
    assert(shell.sceneTransformSettings().tool == Tool::Rotate);
    assert(shell.sceneDocument().selectedObject() == selected);
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), after, diagnostic)
        && before == after);
}

void toolInputPriority(const arp::editor::tests::RecoveryFixture& fixture)
{
    EditorShell shell;
    initialize(shell, fixture);
    shell.activate("workspace_3D");
    assert(shell.submitCommand(CreateSceneObjectCommand("keyboard target")).status
        == EditorResponseStatus::Completed);
    toolInputPriority(shell, fixture);
    std::puts(
        "PASS native QWER/F focus priority: TextInput/NumericInput, modifiers/FocusLost, right camera, modal, Play, single/batch gesture, Script/Game, OOM retry, unchanged Scene/selection");
}

void lifetime(const arp::editor::tests::RecoveryFixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            initialize(shell, fixture);
            shell.activate("workspace_3D");
            assert(shell.submitCommand(CreateSceneObjectCommand("lifecycle")).status
                == EditorResponseStatus::Completed);
            shell.sceneDocument().setSelectedTranslation(3, 2, 1);
            assert(shell.submitCommand(FocusSceneSelectionCommand{}).status
                == EditorResponseStatus::Completed);
            const auto version = shell.stateVersion();
            assert(shell.submitCommand(FocusSceneSelectionCommand{}).stateVersion == version);
            toolInputPriority(shell, fixture);
            assert(shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary focus/tool input priority/modal/Play/gesture/Script/OOM retry/draw/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    arp::editor::tests::RecoveryFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(fixture);
    else {
        numerical();
        commandAndInput(fixture);
        toolInputPriority(fixture);
        protocol(fixture);
    }
}
