#include "editor/ingress/platform_input_adapter.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/viewport/editor_scene_transform_frame.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "editor/workspace/editor_shell.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;

bool close(core::Vec3 a, core::Vec3 b)
{
    return core::distance(a, b) < 0.0001f;
}

bool close(core::Quat a, core::Quat b)
{
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::abs(std::abs(dot) - 1.0f) < 0.00001f;
}

void snapContract()
{
    using Tool = EditorSceneTransformTool;
    const auto before = core::defaultCpuAllocator().stats();
    EditorSceneTransformSnap snap;
    double result = 123;
    assert(!snap.prepare(Tool::Move, 0.13, result) && result == 0.13);
    snap.enabled = true;
    snap.moveStep = 0.5;
    snap.rotateStepRadians = 0.25;
    snap.scaleStep = 0.125;
    assert(!snap.prepare(Tool::Move, 0.25, result) && result == 0.5);
    assert(!snap.prepare(Tool::Move, -0.25, result) && result == -0.5);
    assert(!snap.prepare(Tool::Move, std::nextafter(0.25, 0.0), result) && result == 0);
    assert(!snap.prepare(Tool::Move, std::nextafter(-0.25, 0.0), result) && result == 0);
    assert(!snap.prepare(Tool::Rotate, 0.125, result) && result == 0.25);
    assert(!snap.prepare(Tool::Scale, -0.0625, result) && result == -0.125);
    assert(!snap.prepare(Tool::Move, -0.0, result) && result == 0 && !std::signbit(result));
    result = 123;
    for (double step : {0.0,
             -1.0,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = snap;
        invalid.scaleStep = step;
        invalid.enabled = false;
        assert(!invalid.valid() && invalid.prepare(Tool::Move, 1, result) && result == 123);
    }
    assert(snap.prepare(Tool::Select, 1, result) && result == 123);
    assert(
        snap.prepare(Tool::Move, std::numeric_limits<double>::infinity(), result) && result == 123);
    snap.moveStep = std::numeric_limits<double>::denorm_min();
    assert(std::string_view(snap.prepare(Tool::Move, 1, result)) == "EDITOR-TRANSFORM-SNAP-RANGE"
        && result == 123);
    snap.moveStep = 1;
    assert(snap.prepare(Tool::Move, 9007199254740992.0, result) && result == 123);
    assert(!snap.prepare(Tool::Move, 9007199254740991.0, result) && result == 9007199254740991.0);
    snap.moveStep = std::numeric_limits<double>::max() * 0.6;
    result = 123;
    assert(snap.prepare(Tool::Move, std::numeric_limits<double>::max(), result) && result == 123);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount
        && before.deallocationCount == after.deallocationCount);
    std::puts(
        "PASS snap numeric contract: half away from zero, adjacent boundaries, units, finite/range failure isolation, zero allocations");
}

void transformFrameContract()
{
    using Frame = EditorSceneTransformFrame;
    using Space = EditorSceneTransformSpace;
    using Tool = EditorSceneTransformTool;
    const auto allocationBefore = core::defaultCpuAllocator().stats();
    const auto quarterTurn = core::Quat::fromAxisAngle({0, 1, 0}, 1.5707963267948966f);
    Frame local, world;
    assert(Frame::build(quarterTurn, Space::Local, local));
    assert(Frame::build(quarterTurn, Space::World, world));
    assert(close(local.axes[0], {0, 0, -1}) && close(world.axes[0], {1, 0, 0}));
    assert(close(local.planeU(0), local.axes[1]) && close(local.planeV(0), local.axes[2]));
    const auto frameBefore = local;
    assert(!Frame::build({}, static_cast<Space>(255), local));
    assert(close(local.axes[0], frameBefore.axes[0]));
    assert(!Frame::build({0, 0, 0, 0}, Space::Local, local));
    scene::Transform start;
    start.rotation = quarterTurn;
    start.scale = {2, 3, 4};
    scene::Transform result;
    const auto identity = scene::Transform::identity();
    assert(!Frame::prepare(Tool::Move, Space::Local, 0, 0.5, start, identity, result));
    assert(close(result.translation, {0, 0, -0.5f}));
    assert(!Frame::prepare(Tool::Move, Space::World, 0, 0.5, start, identity, result));
    assert(close(result.translation, {0.5f, 0, 0}));
    assert(!Frame::prepare(
        Tool::Rotate, Space::Local, 0, 1.5707963267948966, start, identity, result));
    assert(close(core::rotate(result.rotation, {0, 1, 0}), {1, 0, 0}));
    assert(!Frame::prepare(
        Tool::Rotate, Space::World, 0, 1.5707963267948966, start, identity, result));
    assert(close(core::rotate(result.rotation, {0, 1, 0}), {0, 0, 1}));
    assert(!Frame::prepare(Tool::Scale, Space::Local, 0, 0.5, start, identity, result));
    assert(close(result.scale, {2.5f, 3, 4}));
    assert(!Frame::prepare(Tool::Scale, Space::World, 0, 0.5, start, identity, result));
    assert(close(result.scale, {2, 3, 4.5f}));
    start.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 0.7853981633974483f);
    const auto beforeRejection = result;
    assert(
        std::string_view(Frame::prepare(Tool::Scale, Space::World, 0, 0.5, start, identity, result))
        == "EDITOR-TRANSFORM-WORLD-SCALE-SHEAR");
    assert(Frame::same(beforeRejection, result));
    assert(!Frame::prepare(Tool::Scale, Space::World, 0, 0, start, identity, result));
    assert(Frame::same(result, start));
    assert(Frame::prepare(Tool::Move, Space::World, 3, 1, start, identity, result));
    assert(Frame::prepare(Tool::Move,
        Space::World,
        0,
        std::numeric_limits<double>::quiet_NaN(),
        start,
        identity,
        result));
    scene::Transform parent;
    parent.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 1.5707963267948966f);
    parent.scale = {-2, 3, 4};
    start.rotation = {};
    start.scale = {-2, 3, -4};
    assert(
        !Frame::prepare(Tool::Rotate, Space::Local, 0, 1.5707963267948966, start, parent, result));
    assert(close(
        core::rotate(scene::composeTransforms(parent, result).rotation, {0, 1, 0}), {0, 0, 1}));
    assert(
        !Frame::prepare(Tool::Rotate, Space::World, 0, 1.5707963267948966, start, parent, result));
    assert(close(
        core::rotate(scene::composeTransforms(parent, result).rotation, {0, 1, 0}), {-1, 0, 0}));
    assert(!Frame::prepare(Tool::Move, Space::World, 0, 0.5, start, parent, result));
    assert(close(result.translation, {0, -1.0f / 6, 0}));
    assert(!Frame::prepare(Tool::Scale, Space::Local, 0, 0.5, start, parent, result));
    assert(close(result.scale, {-1.5f, 3, -4}));
    assert(!Frame::prepare(Tool::Scale, Space::World, 0, 0.5, start, parent, result));
    assert(close(result.scale, {-2, 3 + 1.0f / 6, -4}));
    parent.scale.x = Frame::minimumParentScale;
    const auto beforeSingular = result;
    assert(std::string_view(Frame::prepare(Tool::Move, Space::Local, 0, 1, start, parent, result))
        == "EDITOR-TRANSFORM-PARENT-SINGULAR");
    assert(Frame::same(beforeSingular, result));
    const auto allocationAfter = core::defaultCpuAllocator().stats();
    assert(allocationBefore.allocationCount == allocationAfter.allocationCount
        && allocationBefore.deallocationCount == allocationAfter.deallocationCount);
    std::puts(
        "PASS shared transform axes: local/world intent, noncommuting rotation, aligned scale, shear/singular rejection, negative parent scale, zero allocations");
}

// Project the real camera/viewport and submit native input. No test access to
// the Shell's private gizmo state or direct Transform writes is required.
class GestureFixture final {
public:
    explicit GestureFixture(const tests::RecoveryFixture& files)
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        shell.activate("workspace_3D");
        assert(shell.submitCommand(CreateSceneObjectCommand("Gesture target")).status
            == EditorResponseStatus::Completed);
        object = shell.sceneDocument().selectedObject();
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(shell.id("viewport"));
        assert(viewport);
        rect = viewport->rect;
    }

    core::Vec3 project(core::Vec3 world) const
    {
        float x = 0, y = 0;
        assert(shell.sceneViewportCamera().project(world, rect.width, rect.height, x, y));
        return {x + rect.x, y + rect.y, 0};
    }

    core::Vec3 beginLinear(bool scale)
    {
        shell.activate(scale ? "tool_3" : "tool_1");
        const auto world = shell.sceneDocument().scene().findObject(object)->world_transform();
        EditorSceneTransformFrame frame;
        assert(
            EditorSceneTransformFrame::build(world.rotation, shell.sceneTransformSpace(), frame));
        const auto origin = project(world.translation);
        const auto axis = project(world.translation + frame.axes[0]) - origin;
        pointer = origin + core::scale(axis, 0.65f);
        input(platform::WindowInputEvent::Kind::MouseButtonDown);
        return axis;
    }

    core::Vec3 beginRotate()
    {
        shell.activate("tool_2");
        const auto world = shell.sceneDocument().scene().findObject(object)->world_transform();
        EditorSceneTransformFrame frame;
        assert(
            EditorSceneTransformFrame::build(world.rotation, shell.sceneTransformSpace(), frame));
        const auto origin = project(world.translation);
        pointer = project(world.translation + core::scale(frame.planeU(2), 1.25f * std::cos(0.47f))
            + core::scale(frame.planeV(2), 1.25f * std::sin(0.47f)));
        input(platform::WindowInputEvent::Kind::MouseButtonDown);
        return pointer - origin;
    }

    void delta(core::Vec3 value)
    {
        auto event = native(platform::WindowInputEvent::Kind::MouseMoved);
        event.dx = value.x;
        event.dy = value.y;
        if (std::isfinite(value.x) && std::isfinite(value.y)) {
            event.x += value.x;
            event.y += value.y;
        }
        pointer = {event.x, event.y, 0};
        submit(event);
    }

    void finish(bool focusLost = false)
    {
        input(focusLost ? platform::WindowInputEvent::Kind::FocusLost
                        : platform::WindowInputEvent::Kind::MouseButtonUp);
    }

    scene::Transform local() const
    {
        return shell.sceneDocument().scene().findObject(object)->local_transform();
    }

    void history(const scene::Transform& before, const scene::Transform& after)
    {
        assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
        assert(close(local().translation, before.translation) && close(local().scale, before.scale)
            && close(local().rotation, before.rotation));
        assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
        assert(close(local().translation, after.translation) && close(local().scale, after.scale)
            && close(local().rotation, after.rotation));
    }

    EditorShell shell;
    scene::GameObjectId object;

    void windowExtent(float width, float height)
    {
        windowWidth = width;
        windowHeight = height;
    }

    void pose(const scene::Transform& value, std::uint64_t target = 0)
    {
        SetSceneObjectTransformCommand command;
        command.objectId = target ? target : object.value();
        command.translation = value.translation;
        command.rotation = value.rotation;
        command.scale = value.scale;
        assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
    }

private:
    platform::WindowInputEvent native(platform::WindowInputEvent::Kind kind) const
    {
        platform::WindowInputEvent event;
        event.kind = kind;
        event.mouseButton = platform::WindowMouseButton::Left;
        event.x = pointer.x;
        event.y = pointer.y;
        return event;
    }

    void input(platform::WindowInputEvent::Kind kind)
    {
        auto event = native(kind);
        submit(event);
    }

    void submit(platform::WindowInputEvent& event)
    {
        if (windowWidth > 0) {
            const auto& canvas = shell.runtime().canvas().desc().viewport;
            event.x *= windowWidth / canvas.width;
            event.y *= windowHeight / canvas.height;
            event.dx *= windowWidth / canvas.width;
            event.dy *= windowHeight / canvas.height;
            event.hasPosition = event.kind == platform::WindowInputEvent::Kind::MouseButtonDown
                || event.kind == platform::WindowInputEvent::Kind::MouseButtonUp;
            assert(!EditorCanvasInputMapping(windowWidth, windowHeight, canvas.width, canvas.height)
                    .apply(event));
        }
        shell.handlePlatformInput(event);
    }

    ui::LayoutRect rect;
    core::Vec3 pointer;
    float windowWidth = 0;
    float windowHeight = 0;
};

void snapPreference(const tests::RecoveryFixture& files)
{
    GestureFixture fixture(files);
    auto& shell = fixture.shell;
    core::pmr::String beforeHash, afterHash, diagnostic;
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), beforeHash, diagnostic));
    auto settings = shell.sceneTransformSnap();
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(SetSceneTransformSnapCommand{settings}).status
            == EditorResponseStatus::Completed
        && shell.stateVersion() == version);
    auto invalid = settings;
    invalid.rotateStepRadians = 0;
    assert(shell.submitCommand(SetSceneTransformSnapCommand{invalid}).diagnostic
        == EditorDiagnosticCode::InvalidCommandPayload);
    assert(shell.stateVersion() == version && shell.sceneTransformSnap().same(settings));
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* button = layout.findWidget(shell.id("scene_transform_snap"));
    assert(button && button->visibleInHierarchy);
    const float x = button->rect.x + button->rect.width * 0.5f;
    const float y = button->rect.y + button->rect.height * 0.5f;
    assert(shell.runtime().canvas().hitTest(layout, {x, y}).widgetId()
        == shell.id("scene_transform_snap"));
    assert(shell.runtime().dispatch(ui::UiInputEvent::pointerDown(x, y)).ok());
    assert(shell.runtime().dispatch(ui::UiInputEvent::pointerUp(x, y)).ok());
    settings.enabled = true;
    assert(shell.sceneTransformSnap().same(settings) && shell.stateVersion() > version);
    assert(shell.runtime()
               .canvas()
               .findWidget(shell.id("scene_transform_snap"))
               ->desc()
               .text.find("Snap On")
        == 0);
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), afterHash, diagnostic));
    assert(beforeHash == afterHash);
    shell.activate("menu_Project");
    shell.activate("project_ui_new");
    assert(shell.submitCommand(SetSceneTransformSnapCommand{}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    shell.activate("project_ui_cancel");
    const auto axis = fixture.beginLinear(false);
    assert(shell.submitCommand(SetSceneTransformSnapCommand{}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    fixture.delta(core::scale(axis, 0.6f));
    fixture.finish();
    assert(close(fixture.local().translation, {1, 0, 0}));
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(close(fixture.local().translation, {}) && shell.sceneTransformSnap().same(settings));
    shell.activate("workspace_Game");
    assert(!shell.runtime().canvas().findWidget(shell.id("scene_transform_snap"))->visible());
    std::puts(
        "PASS snap preference: real button, typed/no-op/invalid/busy, caption, unchanged scene content and history");
}

void snappedInput(const tests::RecoveryFixture& files)
{
    EditorSceneTransformSnap settings;
    settings.enabled = true;
    settings.moveStep = 0.5;
    settings.rotateStepRadians = 0.25;
    settings.scaleStep = 0.5;
    for (bool scale : {false, true}) {
        for (float direction : {-1.f, 1.f}) {
            GestureFixture fixture(files);
            fixture.windowExtent(1440, 810);
            scene::Transform start;
            start.translation = {0.13f, -0.4f, 0};
            start.scale = {1.1f, 1, 1};
            fixture.pose(start);
            assert(fixture.shell.submitCommand(SetSceneTransformSnapCommand{settings}).status
                == EditorResponseStatus::Completed);
            const auto axis = fixture.beginLinear(scale);
            for (unsigned step = 0; step != 2; ++step) {
                fixture.delta(core::scale(axis, direction * 0.1f));
                assert(EditorSceneTransformFrame::same(start, fixture.local()));
            }
            // Invalid native motion is already rejected by canvas ingress.
            // Exercise the command boundary too, without asking the fixture's
            // valid-native-event mapper to accept a NaN.
            EditorInputCommand invalid;
            invalid.deltaX = std::numeric_limits<float>::quiet_NaN();
            (void)fixture.shell.submitCommand(invalid);
            assert(EditorSceneTransformFrame::same(start, fixture.local()));
            fixture.delta(core::scale(axis, direction * 0.1f));
            const auto expected =
                scale ? start.scale.x + direction * 0.5f : start.translation.x + direction * 0.5f;
            assert(std::abs(
                       (scale ? fixture.local().scale.x : fixture.local().translation.x) - expected)
                < 0.0001f);
            fixture.delta(core::scale(axis, -direction * 0.3f));
            assert(EditorSceneTransformFrame::same(start, fixture.local()));
            fixture.delta(core::scale(axis, direction * 0.3f));
            fixture.finish();
            fixture.history(start, fixture.local());
        }
    }
    GestureFixture fixture(files);
    scene::Transform start;
    start.rotation = core::Quat::fromAxisAngle({0, 1, 0}, 0.45f);
    fixture.pose(start);
    start = fixture.local();
    assert(fixture.shell.submitCommand(SetSceneTransformSnapCommand{settings}).status
        == EditorResponseStatus::Completed);
    auto radius = fixture.beginRotate();
    for (unsigned step = 0; step != 2; ++step) {
        const core::Vec3 next{radius.x * std::cos(0.1f) - radius.y * std::sin(0.1f),
            radius.x * std::sin(0.1f) + radius.y * std::cos(0.1f),
            0};
        fixture.delta(next - radius);
        radius = next;
        if (step == 0)
            assert(EditorSceneTransformFrame::same(start, fixture.local()));
    }
    fixture.finish();
    assert(close(fixture.local().rotation,
               core::Quat::fromAxisAngle({0, 0, 1}, 0.25f) * start.rotation)
        || close(fixture.local().rotation,
            core::Quat::fromAxisAngle({0, 0, 1}, -0.25f) * start.rotation));
    fixture.history(start, fixture.local());
    std::puts(
        "PASS snapped real Move/Scale/Rotate input: sub-step accumulation, negative motion, relative origin, invalid-event isolation, reverse, Undo/Redo");
}

void spacePreference(const tests::RecoveryFixture& files)
{
    GestureFixture fixture(files);
    auto& shell = fixture.shell;
    using Space = EditorSceneTransformSpace;
    core::pmr::String beforeHash, afterHash, diagnostic;
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), beforeHash, diagnostic));
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(SetSceneTransformSpaceCommand{Space::World}).status
        == EditorResponseStatus::Completed);
    assert(shell.stateVersion() == version);
    assert(shell.submitCommand(SetSceneTransformSpaceCommand{static_cast<Space>(255)}).status
        != EditorResponseStatus::Completed);
    assert(shell.stateVersion() == version);
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* button = layout.findWidget(shell.id("scene_transform_space"));
    assert(button && button->visibleInHierarchy);
    const float x = button->rect.x + button->rect.width * 0.5f;
    const float y = button->rect.y + button->rect.height * 0.5f;
    assert(shell.runtime().canvas().hitTest(layout, {x, y}).widgetId()
        == shell.id("scene_transform_space"));
    assert(shell.runtime().dispatch(ui::UiInputEvent::pointerDown(x, y)).ok());
    assert(shell.runtime().dispatch(ui::UiInputEvent::pointerUp(x, y)).ok());
    assert(shell.sceneTransformSpace() == Space::Local && shell.stateVersion() > version);
    assert(shell.runtime().canvas().findWidget(shell.id("scene_transform_space"))->desc().text
        == "Local");
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), afterHash, diagnostic));
    assert(beforeHash == afterHash);
    shell.activate("menu_Project");
    shell.activate("project_ui_new");
    assert(shell.submitCommand(SetSceneTransformSpaceCommand{Space::World}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    shell.activate("project_ui_cancel");
    const auto axis = fixture.beginLinear(false);
    assert(shell.submitCommand(SetSceneTransformSpaceCommand{Space::World}).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    assert(shell.submitCommand(ActivateControlCommand("tool_2")).diagnostic
        == EditorDiagnosticCode::WorkspaceBusy);
    fixture.delta(core::scale(axis, 0.5f));
    fixture.finish();
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(close(fixture.local().translation, {}));
    assert(shell.sceneTransformSpace() == Space::Local);
    shell.activate("workspace_Game");
    assert(!shell.runtime().canvas().findWidget(shell.id("scene_transform_space"))->visible());
    std::puts(
        "PASS space preference: actual button hit, typed/no-op/busy/invalid, unchanged scene content and history");
}

void localWorldInput(const tests::RecoveryFixture& files)
{
    using Space = EditorSceneTransformSpace;
    for (bool scale : {false, true}) {
        for (Space space : {Space::Local, Space::World}) {
            GestureFixture fixture(files);
            fixture.windowExtent(1440, 810);
            scene::Transform start;
            start.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 0.7853981633974483f);
            fixture.pose(start);
            assert(fixture.shell.submitCommand(SetSceneTransformSpaceCommand{space}).status
                == EditorResponseStatus::Completed);
            const auto axis = fixture.beginLinear(scale);
            fixture.delta(core::scale(axis, 0.25f));
            fixture.delta(core::scale(axis, 0.25f));
            fixture.finish();
            const auto after = fixture.local();
            if (scale && space == Space::World) {
                assert(EditorSceneTransformFrame::same(after, start));
            } else {
                if (scale)
                    assert(close(after.scale, {1.5f, 1, 1}));
                else
                    assert(close(after.translation,
                        space == Space::Local ? core::Vec3{0.35355339f, 0.35355339f, 0}
                                              : core::Vec3{0.5f, 0, 0}));
                fixture.history(start, after);
            }
        }
    }
    for (Space space : {Space::Local, Space::World}) {
        GestureFixture fixture(files);
        scene::Transform start;
        start.rotation = core::Quat::fromAxisAngle({0, 1, 0}, 0.45f);
        fixture.pose(start);
        assert(fixture.shell.submitCommand(SetSceneTransformSpaceCommand{space}).status
            == EditorResponseStatus::Completed);
        auto radius = fixture.beginRotate();
        for (unsigned step = 0; step != 2; ++step) {
            const core::Vec3 next{radius.x * std::cos(0.15f) - radius.y * std::sin(0.15f),
                radius.x * std::sin(0.15f) + radius.y * std::cos(0.15f),
                0};
            fixture.delta(next - radius);
            radius = next;
        }
        fixture.finish();
        const auto delta = core::Quat::fromAxisAngle({0, 0, 1}, 0.3f);
        const auto inverseDelta = core::conjugate(delta);
        const auto after = fixture.local();
        const auto positive =
            space == Space::Local ? start.rotation * delta : delta * start.rotation;
        const auto negative =
            space == Space::Local ? start.rotation * inverseDelta : inverseDelta * start.rotation;
        assert(close(after.rotation, positive) || close(after.rotation, negative));
        fixture.history(start, after);
    }
    std::puts(
        "PASS real local/world Move/Rotate/Scale input: rotated axes, noncommuting rotations, shear rejection, Undo/Redo");
}

void staleGesture(const tests::RecoveryFixture& files)
{
    {
        GestureFixture fixture(files);
        assert(fixture.shell.submitCommand(CreateSceneObjectCommand("Parent")).status
            == EditorResponseStatus::Completed);
        const auto parent = fixture.shell.sceneDocument().selectedObject();
        assert(fixture.shell
                   .submitCommand(
                       ReparentSceneObjectCommand{fixture.object.value(), parent.value(), 0})
                   .status
            == EditorResponseStatus::Completed);
        assert(fixture.shell.submitCommand(SelectSceneObjectCommand{fixture.object.value()}).status
            == EditorResponseStatus::Completed);
        const auto axis = fixture.beginLinear(false);
        const auto version = fixture.shell.stateVersion();
        fixture.delta(core::scale(axis, 0.25f));
        assert(fixture.shell.stateVersion() == version + 1);
        const auto accepted = fixture.local();
        SetSceneObjectTransformCommand parentEdit;
        parentEdit.objectId = parent.value();
        parentEdit.translation = {2, 0, 0};
        for (EditorCommandPayload command : {EditorCommandPayload(parentEdit),
                 EditorCommandPayload(SelectSceneObjectCommand{parent.value()}),
                 EditorCommandPayload(NewSceneCommand{true})}) {
            const auto response = fixture.shell.submitCommand(std::move(command));
            assert(response.status == EditorResponseStatus::Failed
                && response.diagnostic == EditorDiagnosticCode::WorkspaceBusy);
        }
        assert(fixture.shell.stateVersion() == version + 1);
        assert(EditorSceneTransformFrame::same(fixture.local(), accepted));
        fixture.finish();
        assert(fixture.shell.stateVersion() == version + 1);
        fixture.history(scene::Transform{}, accepted);
    }
    {
        GestureFixture fixture(files);
        const auto axis = fixture.beginLinear(false);
        fixture.delta(core::scale(axis, 0.25f));
        const auto accepted = fixture.local();
        // Native owner mutation bypasses Editor history. The parent pose guard
        // must still invalidate this gesture, without relying on stateVersion.
        auto* parent = fixture.shell.sceneDocument().scene().findObject(
            fixture.shell.sceneDocument().scene().findObject(fixture.object)->parentId());
        assert(parent);
        auto changed = parent->local_transform();
        changed.scale = {2, 1, 1};
        parent->set_local_transform(changed);
        fixture.delta(core::scale(axis, 0.25f));
        assert(EditorSceneTransformFrame::same(fixture.local(), accepted));
        fixture.finish();
    }
    std::puts(
        "PASS single gesture branch freezes independent writes/selection; native parent edits retire stale input without reverse writes");
}

void canvasMappingContract()
{
    using Kind = platform::WindowInputEvent::Kind;
    platform::WindowInputEvent event;
    event.kind = Kind::MouseMoved;
    event.windowId = 23;
    event.x = 720;
    event.y = -90;
    event.dx = 36;
    event.dy = -9;
    const auto before = core::defaultCpuAllocator().stats();
    assert(!EditorCanvasInputMapping(1440, 810, 1920, 1080).apply(event));
    assert(event.x == 960 && event.y == -120 && event.dx == 48 && event.dy == -12);
    assert(!event.hasPosition && event.windowId == 23 && event.kind == Kind::MouseMoved);
    // The platform sends absolute PointerMoved followed by delta MouseMoved.
    // Both must arrive at the same canvas position, despite hasPosition=false.
    event.kind = Kind::PointerMoved;
    event.x = 720;
    event.y = -90;
    assert(!EditorCanvasInputMapping(1440, 810, 1920, 1080).apply(event));
    assert(event.x == 960 && event.y == -120 && event.dx == 48 && event.dy == -12);
    event.kind = Kind::MouseWheel;
    event.hasPosition = true;
    event.wheelDelta = -2;
    event.x = 640;
    event.y = 360;
    assert(!EditorCanvasInputMapping(1280, 720, 1920, 1080).apply(event));
    assert(event.x == 960 && event.y == 540 && event.wheelDelta == -2);
    for (float invalid : {0.f,
             -1.f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN()}) {
        assert(std::string_view(EditorCanvasInputMapping(invalid, 810, 1920, 1080).apply(event))
            == "EDITOR-INPUT-CANVAS-EXTENT");
        assert(EditorCanvasInputMapping(1440, invalid, 1920, 1080).apply(event));
        assert(EditorCanvasInputMapping(1440, 810, invalid, 1080).apply(event));
        assert(EditorCanvasInputMapping(1440, 810, 1920, invalid).apply(event));
        assert(event.x == 960 && event.y == 540 && event.wheelDelta == -2);
    }
    event.kind = Kind::MouseMoved;
    event.dx = std::numeric_limits<float>::max();
    assert(std::string_view(EditorCanvasInputMapping(1, 1, 1920, 1080).apply(event))
        == "EDITOR-INPUT-CANVAS-COORDINATE");
    assert(event.x == 960 && event.y == 540 && event.dx == std::numeric_limits<float>::max());
    event.dx = 1;
    event.dy = std::numeric_limits<float>::quiet_NaN();
    assert(EditorCanvasInputMapping(1440, 810, 1920, 1080).apply(event));
    assert(event.x == 960 && event.y == 540 && event.dx == 1 && std::isnan(event.dy));
    event.dy = 1;
    event.x = std::numeric_limits<float>::infinity();
    assert(EditorCanvasInputMapping(1440, 810, 1920, 1080).apply(event));
    assert(std::isinf(event.x) && event.y == 540 && event.dx == 1 && event.dy == 1);
    event.hasPosition = false;
    for (Kind kind : {Kind::FocusLost, Kind::KeyUp, Kind::KeyDown, Kind::TextInput}) {
        event.kind = kind;
        assert(!EditorCanvasInputMapping(0, 0, 0, 0).apply(event));
        assert(std::isinf(event.x) && event.y == 540 && event.dx == 1 && event.dy == 1);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount
        && before.deallocationCount == after.deallocationCount);
    std::puts(
        "PASS canvas input contract: positions/deltas, wheel, cancellation, atomic finite rejection, zero allocations");
}

void canvasGesture(const tests::RecoveryFixture& files)
{
    for (const auto extent : {core::Vec3{1280, 720, 0},
             core::Vec3{1920, 1080, 0},
             core::Vec3{1440, 810, 0},
             core::Vec3{1000, 900, 0}}) {
        GestureFixture fixture(files);
        fixture.windowExtent(extent.x, extent.y);
        const auto before = fixture.local();
        const auto axis = fixture.beginLinear(false);
        fixture.delta(core::scale(axis, 0.25f));
        fixture.delta(core::scale(axis, 0.25f));
        fixture.finish();
        assert(close(fixture.local().translation, {0.5f, 0, 0}));
        fixture.history(before, fixture.local());
    }
    std::puts("PASS 720p/1080p/1440x810/nonuniform logical window input -> canvas -> real gesture");
}

void linear(const tests::RecoveryFixture& files, bool scale)
{
    GestureFixture fixture(files);
    const auto before = fixture.local();
    const auto axis = fixture.beginLinear(scale);
    fixture.delta(core::scale(axis, 0.25f));
    const auto accepted = fixture.local();
    fixture.delta({std::numeric_limits<float>::quiet_NaN(), 0, 0});
    fixture.delta(core::scale(axis, 500000.0f));
    assert(close(fixture.local().translation, accepted.translation)
        && close(fixture.local().scale, accepted.scale));
    fixture.delta(core::scale(axis, 0.25f));
    const auto after = fixture.local();
    const auto actual = scale ? after.scale : after.translation;
    const core::Vec3 expected = scale ? core::Vec3{1.5f, 1, 1} : core::Vec3{0.5f, 0, 0};
    std::fprintf(stderr,
        "%s two deltas: actual=(%f,%f,%f) expected=(%f,%f,%f)\n",
        scale ? "Scale" : "Move",
        actual.x,
        actual.y,
        actual.z,
        expected.x,
        expected.y,
        expected.z);
    assert(close(actual, expected));
    fixture.delta({});
    assert(close(fixture.local().translation, after.translation)
        && close(fixture.local().scale, after.scale));
    fixture.finish();
    fixture.history(before, after);
}

void rotation(const tests::RecoveryFixture& files)
{
    GestureFixture fixture(files);
    const auto before = fixture.local();
    auto radius = fixture.beginRotate();
    const auto turn = [](core::Vec3 p) {
        return core::Vec3{p.x * std::cos(0.15f) - p.y * std::sin(0.15f),
            p.x * std::sin(0.15f) + p.y * std::cos(0.15f),
            0};
    };
    auto next = turn(radius);
    fixture.delta(next - radius);
    const auto first = fixture.local().rotation;
    assert(!close(first, core::Quat{}));
    fixture.delta({std::numeric_limits<float>::quiet_NaN(), 0, 0});
    radius = next;
    next = turn(radius);
    fixture.delta(next - radius);
    const auto after = fixture.local();
    assert(close(after.rotation, core::normalize(first * first)));
    fixture.finish(true);
    fixture.finish();
    fixture.history(before, after);
    assert(close(after.translation, before.translation) && close(after.scale, before.scale));
}

void reverseAndRestart(const tests::RecoveryFixture& files)
{
    GestureFixture fixture(files);
    assert(fixture.shell.submitCommand(SetSceneObjectNameCommand("Gesture history anchor")).status
        == EditorResponseStatus::Completed);
    const auto before = fixture.local();
    auto axis = fixture.beginLinear(false);
    fixture.delta(core::scale(axis, 0.5f));
    fixture.delta(core::scale(axis, -0.5f));
    fixture.finish();
    assert(close(fixture.local().translation, before.translation));
    assert(
        fixture.shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(fixture.shell.sceneDocument().scene().findObject(fixture.object)->name()
        == "Gesture target");
    assert(close(fixture.local().translation, before.translation));
    axis = fixture.beginLinear(false);
    fixture.delta(core::scale(axis, 0.25f));
    fixture.delta(core::scale(axis, 0.25f));
    fixture.finish(true);
    const auto after = fixture.local();
    assert(close(after.translation, {0.5f, 0, 0}));
    fixture.history(before, after);
}

void parentTransform(const tests::RecoveryFixture& files)
{
    GestureFixture fixture(files);
    assert(fixture.shell.submitCommand(CreateSceneObjectCommand("Nonuniform parent")).status
        == EditorResponseStatus::Completed);
    const auto parent = fixture.shell.sceneDocument().selectedObject();
    SetSceneObjectTransformCommand transform;
    transform.objectId = parent.value();
    transform.translation = {4, 2, -3};
    transform.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 1.5707963267948966f);
    transform.scale = {2, 3, 1};
    assert(fixture.shell.submitCommand(transform).status == EditorResponseStatus::Completed);
    assert(fixture.shell
               .submitCommand(ReparentSceneObjectCommand{fixture.object.value(), parent.value(), 0})
               .status
        == EditorResponseStatus::Completed);
    assert(fixture.shell.submitCommand(SelectSceneObjectCommand{fixture.object.value()}).status
        == EditorResponseStatus::Completed);
    assert(fixture.shell.submitCommand(FocusSceneSelectionCommand{}).status
        == EditorResponseStatus::Completed);
    const auto before = fixture.local();
    const auto worldBefore =
        fixture.shell.sceneDocument().scene().findObject(fixture.object)->world_transform();
    const auto axis = fixture.beginLinear(false);
    fixture.delta(core::scale(axis, 0.25f));
    fixture.delta(core::scale(axis, 0.25f));
    fixture.finish();
    const auto after = fixture.local();
    const auto worldAfter =
        fixture.shell.sceneDocument().scene().findObject(fixture.object)->world_transform();
    assert(close(worldAfter.translation, worldBefore.translation + core::Vec3{0.5f, 0, 0}));
    assert(close(after.translation, before.translation + core::Vec3{0, -1.0f / 6.0f, 0}));
    fixture.history(before, after);
}

void accumulatorContract()
{
    const auto before = core::defaultCpuAllocator().stats();
    EditorSceneGestureAccumulator input;
    assert(input.addLinear(5, 0, {20, 0, 0}) && input.amount() == 0.25);
    assert(!input.addLinear(1, 0, {}) && input.amount() == 0.25);
    assert(!input.addLinear(std::numeric_limits<float>::quiet_NaN(), 0, {20, 0, 0})
        && input.amount() == 0.25);
    assert(input.addLinear(5, 0, {20, 0, 0}) && input.amount() == 0.5);
    input.reset({1, 0, 0});
    assert(!input.addRotation(-1, 0, 1) && input.amount() == 0);
    assert(!input.addRotation(0, 1, 0) && input.amount() == 0);
    assert(input.addRotation(0, 1, 1) && std::abs(input.amount() - 0.7853981633974483) < 1e-12);
    auto candidate = input;
    assert(candidate.addRotation(-1, 0, 1));
    // Discarding a proposal must not consume the accepted pointer or angle.
    assert(std::abs(input.amount() - 0.7853981633974483) < 1e-12);
    assert(input.addRotation(-1, 0, 1) && std::abs(input.amount() - 1.5707963267948966) < 1e-12);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount
        && before.deallocationCount == after.deallocationCount);
    std::puts(
        "PASS bounded accumulator: analytic totals, invalid direction/radius/delta, rejected proposal isolation, zero allocations");
}

void rejectedBegin(const tests::RecoveryFixture& files)
{
    GestureFixture fixture(files);
    assert(fixture.shell.submitCommand(SetSceneObjectNameCommand("Gesture history anchor")).status
        == EditorResponseStatus::Completed);
    const auto before = fixture.local();
    assert(
        fixture.shell.submitCommand(BeginSceneTransformEditCommand{fixture.object.value()}).status
        == EditorResponseStatus::Completed);
    const auto axis = fixture.beginLinear(false);
    fixture.delta(core::scale(axis, 0.5f));
    assert(close(fixture.local().translation, before.translation));
    fixture.finish();
    assert(
        fixture.shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(fixture.shell.sceneDocument().scene().findObject(fixture.object)->name()
        == "Gesture target");
    assert(close(fixture.local().translation, before.translation));
    std::puts(
        "PASS rejected nested Begin does not start a viewport gesture or mutate the held object");
}

void lifetime(const tests::RecoveryFixture& files)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            GestureFixture fixture(files);
            fixture.windowExtent(1440, 810);
            assert(
                fixture.shell
                    .submitCommand(SetSceneTransformSpaceCommand{EditorSceneTransformSpace::Local})
                    .status
                == EditorResponseStatus::Completed);
            EditorSceneTransformSnap snap;
            snap.enabled = true;
            snap.moveStep = 0.5;
            assert(fixture.shell.submitCommand(SetSceneTransformSnapCommand{snap}).status
                == EditorResponseStatus::Completed);
            const auto axis = fixture.beginLinear(false);
            fixture.delta(core::scale(axis, 0.25f));
            fixture.delta(core::scale(axis, 0.25f));
            assert(close(fixture.local().translation, {0.5f, 0, 0}));
            fixture.finish();
            assert(fixture.shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary transform gesture/draw/destruction cycles: core live bytes/count delta=0");
}

// Exercise real command admission with the envelope and quota prepared first.
// End must publish the history prepared by Begin, including on input barriers.
bool singleSafety(const tests::RecoveryFixture& files)
{
    bool passed = true;
    unsigned failures = 0, commits = 0;
    for (unsigned operation = 0; operation != 6; ++operation) {
        for (const auto budget :
            std::array<std::size_t, 7>{1, 128, 512, 2048, 8192, 32768, 262144}) {
            GestureFixture f(files);
            auto& shell = f.shell;
            assert(shell.submitCommand(SetSceneObjectNameCommand("Branch")).status
                == EditorResponseStatus::Completed);
            assert(
                shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
            const auto before = f.local();
            if (operation == 3 || operation == 4)
                (void)f.beginLinear(false);
            else if (operation != 0)
                assert(shell.submitCommand(BeginSceneTransformEditCommand{f.object.value()}).status
                    == EditorResponseStatus::Completed);
            SetSceneObjectTransformCommand update;
            update.objectId = f.object.value();
            update.translation = {3, 4, 5};
            update.rotation = core::Quat::fromAxisAngle({0, 0, 1}, 0.5f);
            update.scale = {2, 3, 4};
            if (operation >= 2 && operation != 5)
                assert(shell.submitCommand(update).status == EditorResponseStatus::Completed);
            EditorCommandPayload payload = BeginSceneTransformEditCommand{f.object.value()};
            if (operation == 1)
                payload = update;
            else if (operation == 2 || operation == 5)
                payload = EndSceneTransformEditCommand{f.object.value()};
            else if (operation == 3 || operation == 4) {
                EditorInputCommand input;
                input.kind =
                    operation == 3 ? EditorInputKind::PointerUp : EditorInputKind::FocusLost;
                input.primaryButton = true;
                input.button = EditorPointerButton::Left;
                payload = input;
            }
            EditorCommandEnvelope envelope{std::move(payload)};
            const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            const auto version = shell.stateVersion();
            const auto pose = f.local();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            EditorResponse response;
            bool threw = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                response = shell.submitCommand(std::move(envelope));
            } catch (const std::bad_alloc&) {
                threw = true;
            }
            const auto allocationDelta =
                core::defaultCpuAllocator().stats().allocationCount - allocations;
            const bool committed = !threw && response.status == EditorResponseStatus::Completed;
            failures += !committed;
            commits += committed;
            bool coherent = !threw;
            if (operation == 0) {
                coherent &= shell.stateVersion() == version
                    && EditorSceneTransformFrame::same(f.local(), before);
                if (!committed)
                    coherent &=
                        shell.submitCommand(BeginSceneTransformEditCommand{f.object.value()}).status
                        == EditorResponseStatus::Completed;
                coherent &=
                    shell.submitCommand(EndSceneTransformEditCommand{f.object.value()}).status
                    == EditorResponseStatus::Completed;
                coherent &= shell.submitCommand(RedoSceneCommand{}).status
                    == EditorResponseStatus::Completed;
                coherent &= shell.sceneDocument().scene().findObject(f.object)->name() == "Branch";
            } else if (operation == 1) {
                coherent &= shell.stateVersion() == version + (committed ? 1 : 0);
                coherent &= EditorSceneTransformFrame::same(f.local(),
                    committed ? scene::Transform{update.translation, update.scale, update.rotation}
                              : pose);
                if (!threw) {
                    assert(
                        shell.submitCommand(EndSceneTransformEditCommand{f.object.value()}).status
                        == EditorResponseStatus::Completed);
                    if (committed)
                        f.history(before, f.local());
                }
            } else {
                coherent &= committed && shell.stateVersion() == version && allocationDelta == 0;
                if (!threw) {
                    if (operation == 5) {
                        coherent &= shell.submitCommand(RedoSceneCommand{}).status
                            == EditorResponseStatus::Completed;
                        coherent &=
                            shell.sceneDocument().scene().findObject(f.object)->name() == "Branch";
                    } else
                        f.history(before, pose);
                }
            }
            std::printf(
                "single safety op=%u budget=%zu threw=%u committed=%u allocationDelta=%llu coherent=%u\n",
                operation,
                budget,
                threw,
                committed,
                static_cast<unsigned long long>(allocationDelta),
                coherent);
            std::fflush(stdout);
            passed &= coherent;
        }
    }
    std::printf("single safety rejected=%u completed=%u passed=%u\n", failures, commits, passed);
    return passed;
}

void singleAuthority(const tests::RecoveryFixture& files)
{
    GestureFixture f(files);
    auto& shell = f.shell;
    const auto target = f.object.value();
    assert(shell.submitCommand(BeginSceneTransformEditCommand{target}).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(EndSceneTransformEditCommand{target + 1}).status
        == EditorResponseStatus::Failed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Failed);
    EditorSceneDocument candidate;
    core::pmr::String diagnostic;
    auto stale = shell.prepareSceneReplacement(candidate, diagnostic);
    assert(stale);
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(EndSceneTransformEditCommand{target}).status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(BeginSceneTransformEditCommand{target}).status
        == EditorResponseStatus::Completed);
    assert(shell.stateVersion() == version);
    assert(shell.sceneReplacementReadiness(*stale, candidate)
        == EditorShell::SceneReplacementResult::Stale);
    assert(shell.submitCommand(SetSceneObjectTranslationCommand{1, 2, 3}).status
        == EditorResponseStatus::Completed);
    auto* object = shell.sceneDocument().scene().findObject(f.object);
    auto external = object->local_transform();
    external.translation.x = 9;
    object->set_local_transform(external);
    assert(shell.submitCommand(SetSceneObjectTranslationCommand{4, 5, 6}).status
        == EditorResponseStatus::Failed);
    assert(EditorSceneTransformFrame::same(f.local(), external));
    assert(shell.submitCommand(EndSceneTransformEditCommand{target}).status
        == EditorResponseStatus::Failed);
    assert(shell.submitCommand(BeginSceneTransformEditCommand{target}).status
        == EditorResponseStatus::Completed);
    auto replacement = shell.prepareSceneReplacement(candidate, diagnostic);
    assert(replacement);
    const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.commitSceneReplacement(*replacement, candidate)
            == EditorShell::SceneReplacementResult::Committed);
    }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(shell.submitCommand(EndSceneTransformEditCommand{target}).status
        == EditorResponseStatus::Failed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    std::puts(
        "PASS wrong End retains authority; no-op gesture IDs invalidate replacement; external pose preserved; prepared Scene replacement retires live single owner without allocation");
}

void singleSafetyLifetime(const tests::RecoveryFixture& files)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            GestureFixture f(files);
            EditorCommandEnvelope begin{BeginSceneTransformEditCommand{f.object.value()}};
            const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            {
                core::CpuMemoryQuotaScope scope(quota);
                const auto response = f.shell.submitCommand(std::move(begin));
                assert(response.status == EditorResponseStatus::Failed
                    && response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
            }
            const auto axis = f.beginLinear(false);
            f.delta(core::scale(axis, 0.25f));
            const auto accepted = f.local();
            if (cycle % 2) {
                EditorInputCommand held;
                held.kind = EditorInputKind::KeyDown;
                held.key =
                    cycle % 4 == 1 ? EditorInputKey::RightControl : EditorInputKey::RightShift;
                assert(f.shell.submitCommand(held).status == EditorResponseStatus::Completed);
            }
            const auto version = f.shell.stateVersion();
            {
                core::CpuMemoryQuotaScope scope(quota);
                f.finish(cycle % 2);
            }
            // End does not bump content version; clearing a held physical side
            // on FocusLost is a separate real input-state change, counted once.
            assert(f.shell.stateVersion() == version + cycle % 2);
            f.history(scene::Transform{}, accepted);
            assert(f.shell.drawCommands().ok());
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary single Begin failure/retry, input/FocusLost End, Undo/Redo/draw/destruction cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--safety-smoke") {
        singleSafetyLifetime(files);
        return 0;
    }
    if (argc == 2
        && (std::string_view(argv[1]) == "--safety"
            || std::string_view(argv[1]) == "--probe-safety")) {
        if (!singleSafety(files))
            return 2;
        singleAuthority(files);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(files);
    else {
        accumulatorContract();
        snapContract();
        transformFrameContract();
        snapPreference(files);
        snappedInput(files);
        spacePreference(files);
        localWorldInput(files);
        staleGesture(files);
        canvasMappingContract();
        canvasGesture(files);
        linear(files, false);
        linear(files, true);
        rotation(files);
        reverseAndRestart(files);
        parentTransform(files);
        rejectedBegin(files);
        std::puts("PASS native multi-event Move/Scale/Rotate and one-gesture Undo/Redo/FocusLost");
    }
}
