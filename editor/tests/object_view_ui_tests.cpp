#include "2d/physics_components.h"
#include "editor/tests/fixtures/resource_navigation_fixture.h"
#include "editor/tests/fixtures/transform_selection_fixture.h"
#include "editor/validation/editor_object_view_input.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using Action = layout::SceneHierarchyAction;
using Status = EditorResponseStatus;

std::uint64_t epoch(EditorShell& shell)
{
    const auto result = shell.submitCommand(GetSceneHierarchyQuery{});
    assert(result.hasSceneHierarchy && result.status == Status::Completed);
    return result.sceneHierarchy.documentEpoch;
}

void set(EditorShell& shell, scene::GameObjectId object, bool hidden, bool locked)
{
    assert(
        shell.submitCommand(SetSceneObjectViewCommand{object.value(), epoch(shell), hidden, locked})
            .status
        == Status::Completed);
}

core::pmr::String yaml(EditorShell& shell)
{
    core::pmr::String result, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), result, diagnostic));
    return result;
}

const ui::Widget& widget(EditorShell& shell, std::string_view source)
{
    const auto* result =
        shell.runtime().canvas().findWidget(EditorControlInput::find(shell, source));
    assert(result);
    return *result;
}

void inspector(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture fixture(files);
    auto& shell = fixture.shell;
    assert(shell.submitCommand(SelectSceneObjectCommand{fixture.first.value()}).status
        == Status::Completed);
    assert(shell.submitCommand(AddSceneComponentCommand("LightComponent")).status
        == Status::Completed);
    const auto original = shell.submitCommand(GetSceneInspectorQuery{fixture.first.value()});
    assert(original.hasSceneInspector);
    const auto raw = shell.sceneDocument().inspectorProperties(fixture.first);
    core::pmr::String position;
    std::uint64_t component = 0;
    for (const auto& property : raw) {
        if (property.componentType == "TransformComponent" && property.name == "position")
            position = scenePropertyControlSource(property) + "_x";
        if (property.componentType == "LightComponent")
            component = property.componentId;
    }
    assert(!position.empty() && component);
    const auto retired = widget(shell, position).id();
    const auto before = yaml(shell);
    const auto selected = shell.sceneDocument().selectedObject();
    // Open Add before locking: the retired popup must not accept a delayed item.
    auto* dropdown = dynamic_cast<EditorEnumDropdown*>(
        shell.runtime().findControlObject(shell.id("scene_component_add")));
    assert(dropdown);
    ui::UiControlInteraction click;
    click.kind = ui::UiControlInteractionKind::Click;
    click.widgetId = shell.id("scene_component_add");
    assert(shell.runtime().interact(click).ok());
    set(shell, fixture.first, false, true);
    assert(shell.inspectorState() == EditorShell::InspectorState::Ready);
    const auto locked = shell.submitCommand(GetSceneInspectorQuery{fixture.first.value()});
    for (const auto& property : locked.sceneInspector.properties)
        assert(!property.editable);
    const auto currentRaw = shell.sceneDocument().inspectorProperties(fixture.first);
    assert(raw.size() == currentRaw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        assert(raw[i].editable == currentRaw[i].editable);
    assert(!shell.acceptsInspectorInput(position, retired));
    assert(!shell.runtime().canvas().findWidget(retired));
    const auto& value = widget(shell, position);
    assert(!value.desc().acceptsFocus && !value.desc().acceptsPointerInput
        && value.desc().text == "2");
    assert(!widget(shell, "scene_name").visible() && !widget(shell, "scene_active").visible());
    assert(widget(shell, "scene_locked_name").desc().text == "first");
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* active = layout.findWidget(widget(shell, "scene_locked_active").id());
    const auto* name = layout.findWidget(shell.id("scene_name_label"));
    assert(active && name && active->rect.x >= name->rect.x + name->rect.width);
    shell.entryChanged("scene_name", "Blocked");
    shell.entryChanged(position, "999");
    shell.activate("scene_active");
    core::pmr::String toggle = "scene_component_toggle_"
        + core::pmr::String(std::to_string(fixture.first.value())) + "_"
        + core::pmr::String(std::to_string(component));
    shell.activate(toggle);
    shell.activate(toggle);
    assert(yaml(shell) == before && shell.sceneDocument().selectedObject() == selected);
    set(shell, fixture.first, false, false);
    assert(widget(shell, position).desc().acceptsFocus && widget(shell, "scene_name").enabled());
    set(shell, fixture.child, false, true);
    const auto ancestor = shell.submitCommand(GetSceneInspectorQuery{fixture.first.value()});
    bool editableOwn = false;
    for (const auto& property : ancestor.sceneInspector.properties) {
        if (property.componentType == "TransformComponent")
            assert(!property.editable);
        else
            editableOwn |= property.editable;
    }
    assert(editableOwn && widget(shell, "scene_name").enabled());
    assert(
        !widget(shell, position).desc().acceptsFocus && !widget(shell, "scene_active").visible());
    assert(shell.drawCommands().ok());
    std::puts(
        "PASS locked Inspector keeps values/folding, isolates old inputs and popup; ancestor Transform/Active alone readonly; metadata unchanged");
}

void pointer(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture fixture(files);
    auto& shell = fixture.shell;
    const auto selected = shell.sceneDocument().selectedObject();
    const auto before = yaml(shell);
    const auto dirty = shell.sceneDirty();
    const auto root = shell.sceneDocument().scene().rootId();
    EditorReferenceDragInput input(shell);
    const auto point = EditorObjectViewInput::action(shell, root, Action::Visibility);
    assert(input.down(point) && input.up(point));
    assert(shell.sceneObjectView(root).local.hidden && !shell.sceneObjectVisible(fixture.child));
    assert(EditorObjectViewInput::click(shell, root, Action::Lock));
    assert(shell.sceneObjectView(fixture.child).effective.locked);
    assert(shell.sceneDocument().selectedObject() == selected && shell.sceneDirty() == dirty);
    // Hidden/locked objects remain selectable in the hierarchy, including via
    // the object reference handle's short-click path.
    const auto childIcon = EditorReferenceDragInput::objectIcon(shell, fixture.child);
    assert(input.down(childIcon) && input.up(childIcon));
    assert(shell.sceneDocument().selectedObject() == fixture.child);
    assert(EditorObjectViewInput::click(shell, root, Action::Visibility));
    assert(EditorObjectViewInput::click(shell, root, Action::Lock));
    assert(!shell.sceneObjectView(root).local.hidden && !shell.sceneObjectView(root).local.locked);
    // Each cancel owns its release, even when it would land on a different action.
    const auto lock = EditorObjectViewInput::action(shell, root, Action::Lock);
    assert(input.down(point) && input.move(lock) && input.up(lock));
    assert(!shell.sceneObjectView(root).local.hidden && !shell.sceneObjectView(root).local.locked);
    assert(input.down(point));
    assert(input.send(platform::WindowInputEvent::Kind::KeyDown, {}, platform::WindowKey::Escape));
    assert(input.up(point));
    assert(!shell.sceneObjectView(root).local.hidden);
    assert(input.down(point));
    assert(input.send(platform::WindowInputEvent::Kind::FocusLost));
    assert(input.up(point));
    assert(!shell.sceneObjectView(root).local.hidden);
    assert(input.down(point));
    assert(shell.submitCommand(SelectSceneObjectCommand{fixture.first.value()}).status
        == Status::Completed);
    assert(input.up(point) && !shell.sceneObjectView(root).local.hidden);
    // Read-only queries retain capture; a wheel invalidates it and is swallowed.
    assert(input.down(point));
    (void)epoch(shell);
    assert(input.up(point) && shell.sceneObjectView(root).local.hidden);
    assert(EditorObjectViewInput::click(shell, root, Action::Visibility));
    assert(input.down(point));
    assert(input.send(platform::WindowInputEvent::Kind::MouseWheel, point));
    assert(input.up(point) && !shell.sceneObjectView(root).local.hidden);
    // OOM dispatcher admission cannot forward an owned release to Runtime.
    assert(input.down(point));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    platform::WindowInputEvent release;
    release.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
    release.mouseButton = platform::WindowMouseButton::Left;
    release.x = point.x;
    release.y = point.y;
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(shell.handlePlatformInput(release));
    }
    assert(input.send(platform::WindowInputEvent::Kind::FocusLost));
    assert(input.up(point));
    assert(!shell.sceneObjectView(root).local.hidden && yaml(shell) == before);
    assert(shell.sceneDirty() == dirty);
    std::puts(
        "PASS painted root show/unlock, inherited flags, exact action capture, outside/Escape/focus/independent command/wheel/OOM orphan-up cancellation");
}

void failureAndNavigation(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    tests::ResourceNavigationFixture resourceFiles(files.root / "view-ui-resource");
    auto* mesh = resourceFiles.prepare(shell);
    const auto object = shell.sceneDocument().selectedObject();
    const auto locate = tests::ResourceNavigationFixture::source(shell);
    set(shell, object, false, true);
    assert(widget(shell, locate).desc().acceptsPointerInput && shell.acceptsInspectorInput(locate));
    assert(EditorControlInput::click(shell, locate));
    assert(shell.sceneDocument().selectedObject() == object
        && mesh->material_resource_id() == resourceFiles.materialId);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!shell.retrySceneInspector());
    }
    // A committed unlock while Failed must keep the quarantine until Retry.
    set(shell, object, false, false);
    assert(shell.inspectorState() == EditorShell::InspectorState::Failed);
    assert(!shell.acceptsInspectorInput("scene_name"));
    assert(shell.retrySceneInspector());
    assert(widget(shell, "scene_name").enabled());
    assert(shell.drawCommands().ok());
    std::puts(
        "PASS locked resource definition navigation and committed view change while Inspector Failed; explicit Retry reads current flags");
}

void targetRevalidation(const tests::RecoveryFixture& files)
{
    for (unsigned mode = 0; mode < 6; ++mode) {
        tests::TransformSelectionFixture fixture(files);
        auto& shell = fixture.shell;
        const auto root = shell.sceneDocument().scene().rootId();
        const auto point = EditorObjectViewInput::action(shell, root, Action::Visibility);
        const auto before = yaml(shell);
        EditorReferenceDragInput input(shell);
        assert(input.down(point));
        ui::WidgetId blocker;
        auto* panel = shell.runtime().canvas().findWidget(shell.id("left_scene"));
        if (mode == 0) {
            ui::UiControlDesc desc;
            desc.type = ui::UiControlType::Button;
            desc.widget.name = "view_action_occluder";
            desc.widget.styleId = "button";
            desc.widget.localRect = {point.x - 10, point.y - 10, 20, 20};
            const auto result =
                shell.runtime().createControl(std::move(desc), shell.runtime().canvas().rootId());
            assert(result.ok());
            blocker = result.widgetId();
        } else if (mode == 1) {
            auto rect = panel->desc().localRect;
            rect.width -= layout::kSceneHierarchyActionWidth;
            panel->setLocalRect(rect);
        } else if (mode == 2)
            panel->setVisible(false);
        else if (mode == 3)
            panel->setEnabled(false);
        else if (mode == 4)
            assert(shell
                    .execute(
                        SetSceneObjectViewCommand{fixture.child.value(), epoch(shell), false, true})
                    .success);
        else {
            // Native document replacement bypasses command cancellation. The
            // old ticket must still reject the same numeric root ID on release.
            shell.sceneDocument() = EditorSceneDocument(false);
            assert(shell.retrySceneInspector());
        }
        assert(input.up(point));
        assert(!shell.sceneObjectView(shell.sceneDocument().scene().rootId()).local.hidden);
        if (mode != 5)
            assert(yaml(shell) == before);
        if (blocker.valid())
            assert(shell.runtime().removeControl(blocker).ok());
    }
    std::puts(
        "PASS release revalidates occlusion, panel geometry/visibility/enabled, view revision and native Scene owner epoch");
}

void publicationFailureAndObjectNavigation(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture fixture(files);
    auto& shell = fixture.shell;
    assert(shell.submitCommand(SelectSceneObjectCommand{fixture.first.value()}).status
        == Status::Completed);
    const auto documentEpoch = epoch(shell);
    auto quota = core::makeShared<core::CpuMemoryQuota>(4096);
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto result = shell.execute(
            SetSceneObjectViewCommand{fixture.first.value(), documentEpoch, false, true});
        assert(result.success && result.stateChanged);
    }
    assert(shell.sceneObjectView(fixture.first).local.locked);
    assert(shell.inspectorState() == EditorShell::InspectorState::Failed);
    assert(!shell.acceptsInspectorInput("scene_name"));
    assert(shell.retrySceneInspector());
    assert(!widget(shell, "scene_name").visible());
    set(shell, fixture.first, false, false);
    auto world = core::makeShared<physics2d::PhysicsWorld2D>(
        physics2d::WorldConfig{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f});
    assert(world->valid());
    auto& body = shell.sceneDocument().scene().createObject("Target body");
    body.addComponent<physics2d::RigidBody2DComponent>(nullptr);
    body.set_local_transform(scene::Transform::fromTranslation({8, 1, 0}));
    auto* owner = shell.sceneDocument().scene().findObject(fixture.first);
    auto& joint = owner->addComponent<physics2d::Joint2DComponent>(world);
    auto properties = joint.editorAuthoringProperties();
    properties.cast<physics2d::Joint2DEditorProperties&>().bodyA = body.id().value();
    assert(joint.applyEditorAuthoringProperties(properties));
    set(shell, fixture.first, false, true);
    core::pmr::String source;
    for (const auto& property : shell.sceneDocument().inspectorProperties(fixture.first))
        if (property.name == "bodyA")
            source = "scene_reference_focus_" + scenePropertyControlSource(property);
    assert(!source.empty() && widget(shell, source).desc().acceptsPointerInput);
    assert(EditorControlInput::click(shell, source));
    assert(shell.sceneViewportCamera().target().x == 8
        && shell.sceneDocument().selectedObject() == fixture.first);
    std::puts(
        "PASS committed lock survives Inspector OOM quarantine/retry; readonly object reference still locates its actual target");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (!smoke) {
        failureAndNavigation(files);
        targetRevalidation(files);
        publicationFailureAndObjectNavigation(files);
    }
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        inspector(files);
        pointer(files);
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary view UI/capture/Inspector rebuild/destruction cycles: core live bytes/count delta=0");
}
