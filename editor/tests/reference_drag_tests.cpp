#include "2d/physics_components.h"
#include "editor/tests/fixtures/resource_navigation_fixture.h"
#include "editor/validation/editor_reference_drag_input.h"
#include "editor/validation/editor_reference_drag_smoke.h"

#include <cstdio>
#include <limits>

namespace {
using namespace arp;
using namespace arp::editor;
using Input = EditorReferenceDragInput;
using Status = EditorResponseStatus;

void load(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    shell.activate("workspace_3D");
}

core::pmr::String serialized(EditorShell& shell)
{
    core::pmr::String yaml, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), yaml, diagnostic));
    return yaml;
}

void objects(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    auto world = core::makeShared<physics2d::PhysicsWorld2D>(
        physics2d::WorldConfig{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f});
    assert(world->valid());
    shell.sceneDocument().setPhysicsWorld2DProvider([world] {
        return world;
    });
    const auto create = [&](const char* type) {
        assert(
            shell.submitCommand(CreateSceneObjectCommand("Same name")).status == Status::Completed);
        assert(shell.submitCommand(AddSceneComponentCommand(type)).status == Status::Completed);
        return shell.sceneDocument().selectedObject();
    };
    const auto a = create("RigidBody2DComponent");
    const auto b = create("RigidBody2DComponent");
    const auto anchor = create("RigidBody2DComponent");
    const auto owner = create("Joint2DComponent");
    auto* joint = shell.sceneDocument()
                      .scene()
                      .findObject(owner)
                      ->getComponent<physics2d::Joint2DComponent>();
    assert(joint);
    auto properties = joint->editorAuthoringProperties();
    properties.cast<physics2d::Joint2DEditorProperties&>().bodyA = a.value();
    properties.cast<physics2d::Joint2DEditorProperties&>().bodyB = anchor.value();
    assert(joint->applyEditorAuthoringProperties(properties));
    assert(shell.retrySceneInspector());
    Input pointer(shell);
    const auto field = Input::field(shell, "bodyA");
    const auto target = Input::center(shell, field);
    const auto source = Input::objectIcon(shell, b);
    const auto before = serialized(shell);
    assert(pointer.down(source));
    assert(shell.sceneDocument().selectedObject() == owner);
    assert(pointer.move(target));
    assert(Input::feedback(shell, "Release to assign reference"));
    assert(pointer.up(target));
    assert(shell.sceneDocument().selectedObject() == owner);
    assert(joint->editorAuthoringProperties().cast<physics2d::Joint2DEditorProperties&>().bodyA
        == b.value());
    assert(shell.submitCommand(UndoSceneCommand{}).status == Status::Completed);
    assert(serialized(shell) == before);
    assert(shell.submitCommand(RedoSceneCommand{}).status == Status::Completed);
    // No-op and rejected drops preserve the existing Redo branch.
    assert(shell.submitCommand(UndoSceneCommand{}).status == Status::Completed);
    const auto unchanged = serialized(shell);
    assert(pointer.down(Input::objectIcon(shell, owner)) && pointer.move(target));
    assert(Input::feedback(shell, "Cannot assign here"));
    assert(pointer.up(target) && serialized(shell) == unchanged);
    assert(shell.submitCommand(RedoSceneCommand{}).status == Status::Completed);
    // Real tracked Widget, disabled and clipped targets must fail closed.
    for (const unsigned mode : {0u, 1u, 2u, 3u, 4u, 5u}) {
        const auto fieldNow = Input::field(shell, "bodyA");
        const auto destination = Input::center(shell, fieldNow);
        const auto state = serialized(shell);
        assert(pointer.down(Input::objectIcon(shell, a)));
        if (mode == 0)
            shell.runtime().canvas().findWidget(fieldNow)->setEnabled(false);
        else if (mode == 1)
            shell.runtime().canvas().findWidget(fieldNow)->setVisible(false);
        else if (mode == 2)
            assert(pointer.send(
                platform::WindowInputEvent::Kind::KeyDown, {}, platform::WindowKey::Escape));
        else if (mode == 3)
            assert(pointer.send(platform::WindowInputEvent::Kind::FocusLost));
        else if (mode == 4) {
            assert(shell.submitCommand(SelectSceneObjectCommand{a.value()}).status
                == Status::Completed);
            assert(shell.submitCommand(SelectSceneObjectCommand{owner.value()}).status
                == Status::Completed);
        } else {
            auto* widget = shell.runtime().canvas().findWidget(fieldNow);
            auto rect = widget->desc().localRect;
            rect.y += 2000;
            widget->setLocalRect(rect);
        }
        assert(pointer.move(destination) && pointer.up(destination));
        assert(serialized(shell) == state);
        assert(shell.retrySceneInspector());
    }
    // Focus loss cannot poison the next legitimate down, and icon short-click
    // still selects; text dragging remains the established reparent gesture.
    const auto click = Input::objectIcon(shell, a);
    assert(pointer.down(click) && pointer.up(click));
    assert(shell.sceneDocument().selectedObject() == a);
    std::puts(
        "PASS object icon drag preserves Inspector, shared history/Redo, metadata rejection, Widget/clip/selection/Escape/FocusLost gates and short click");
}

void resources(const tests::RecoveryFixture& root)
{
    for (const auto scenario : {"drag_hover", "drag_assigned", "drag_rejected", "drag_cancelled"}) {
        EditorShell shell;
        load(shell, root);
        core::pmr::String diagnostic;
        assert(EditorReferenceDragSmoke::run(shell, scenario, diagnostic));
        if (std::string_view(scenario) == "drag_assigned") {
            assert(shell.submitCommand(UndoSceneCommand{}).status == Status::Completed);
            assert(shell.submitCommand(RedoSceneCommand{}).status == Status::Completed);
        }
    }
}

void resourceFailureGates(const tests::RecoveryFixture& root)
{
    using Files = tests::ResourceNavigationFixture;
    Files files(root.root / "resource-gates");
    EditorShell shell;
    load(shell, root);
    auto* mesh = files.prepare(shell);
    mesh->set_material_resource_id(Files::alternateId);
    assert(shell.retrySceneInspector());
    assert(shell.submitCommand(Files::command(shell, *mesh)).status == Status::Completed);
    mesh->set_material_resource_id(files.materialId);
    assert(shell.retrySceneInspector());
    ui::WidgetId source;
    for (const auto* slot : layout::kFileSlots) {
        const auto id = shell.id(slot);
        const auto* widget = shell.runtime().canvas().findWidget(id);
        if (widget && widget->visible()
            && widget->desc().text == core::FilePath::utf8(files.second.filename()))
            source = id;
    }
    assert(source.valid());
    const auto start = Input::center(shell, source);
    auto target = Input::center(shell, Input::field(shell, "materialResourceId"));
    Input pointer(shell);
    // Cold capture never forwards the source down to UI; Escape/FocusLost and
    // dispatcher admission failure must likewise swallow the eventual release.
    for (const std::size_t bytes : {1u, 4096u, 32768u}) {
        target = Input::center(shell, Input::field(shell, "materialResourceId"));
        assert(pointer.down(start));
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        platform::WindowInputEvent up;
        up.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
        up.mouseButton = platform::WindowMouseButton::Left;
        up.hasPosition = true;
        up.x = target.x;
        up.y = target.y;
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(shell.handlePlatformInput(up));
        }
        assert(mesh->material_resource_id() == files.materialId);
        assert(pointer.send(platform::WindowInputEvent::Kind::FocusLost));
        // Clear either an admitted or unadmitted cancelled pointer tail.
        assert(pointer.up(target));
        assert(shell.retrySceneInspector());
        assert(quota->liveBytes() == 0);
    }
    const auto original = tests::RecoveryFixture::read(files.second);
    target = Input::center(shell, Input::field(shell, "materialResourceId"));
    assert(pointer.down(start));
    tests::RecoveryFixture::write(files.second, original + "\n# external edit\n");
    assert(pointer.move(target) && Input::feedback(shell, "Cannot assign here"));
    assert(pointer.up(target));
    assert(mesh->material_resource_id() == files.materialId);
    tests::RecoveryFixture::write(files.second, original);
    // A foreign overlay with the exact same display name is not a current
    // tracked Inspector Widget; hit testing must not commit through it.
    target = Input::center(shell, Input::field(shell, "materialResourceId"));
    ui::UiControlDesc overlay;
    overlay.type = ui::UiControlType::Dropdown;
    overlay.widget.styleId = "input";
    overlay.widget.fontToken = "font.editor";
    overlay.widget.name =
        shell.runtime().canvas().findWidget(Input::field(shell, "materialResourceId"))->name();
    overlay.widget.localRect = ui::LayoutRect::fromOriginSize(target.x - 10, target.y - 10, 20, 20);
    overlay.optionLabels.push_back("Spoofed control");
    overlay.initialValue = ui::UiControlValue::fromIndex(0);
    const auto blocker =
        shell.runtime().createControl(std::move(overlay), shell.runtime().canvas().rootId());
    assert(blocker.ok());
    assert(pointer.down(start) && pointer.move(target));
    // This deliberately unregistered control is a hit-test adversary, not a
    // legal Editor draw source. Do not render it through the source registry.
    assert(pointer.up(target));
    assert(mesh->material_resource_id() == files.materialId);
    assert(shell.runtime().removeControl(blocker.widgetId()).ok());
    // Generation retirement is checked at release, even after allowed hover.
    target = Input::center(shell, Input::field(shell, "materialResourceId"));
    assert(pointer.down(start) && pointer.move(target));
    assert(Input::feedback(shell, "Release to assign reference"));
    assert(shell.projectResources().unload(Files::alternateId).success);
    assert(pointer.up(target));
    assert(mesh->material_resource_id() == files.materialId);
    std::puts(
        "PASS OOM capture/release consumption, exact file tamper rejection, overlay identity and resource retirement gates");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned cycle = 0; cycle <= 20; ++cycle) {
            objects(files);
            resources(files);
            const auto current = core::defaultCpuAllocator().stats();
            if (!cycle)
                baseline = current;
            assert(current.liveBytes == baseline.liveBytes
                && current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 warmed ordinary reference drag/history/failure/destroy cycles; core live bytes/count delta=0");
    } else {
        objects(files);
        resources(files);
        resourceFailureGates(files);
    }
}
