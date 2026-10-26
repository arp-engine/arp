#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace arp::editor {

bool EditorShell::sceneHierarchyPointerHit(
    const ui::CanvasLayoutResult& resolved, float x, float y) const
{
    const auto panel = id("left_scene");
    if (!layout::sceneHierarchyClip(resolved, panel).contains({x, y}))
        return false;
    const auto hit = runtime().canvas().hitTest(resolved, {x, y}).widgetId();
    return runtime().canvas().isInSubtree(hit, panel);
}

bool EditorShell::sceneViewPointerTarget(
    float x, float y, EditorSceneViewPointer::Target& target) const
{
    if (!std::isfinite(x) || !std::isfinite(y) || presentationState_.workspace() != "3D"
        || sceneDockTab_ != "Scene")
        return false;
    const auto resolved = runtime().canvas().resolveLayout();
    const auto panel = id("left_scene");
    const auto clip = layout::sceneHierarchyClip(resolved, panel);
    if (!clip.contains({x, y})
        || clip.width
            < layout::kSceneHierarchyActionWidth * 2 + layout::kSceneHierarchyScrollbarWidth)
        return false;
    if (!sceneHierarchyPointerHit(resolved, x, y))
        return false;
    const auto hierarchy = visibleSceneHierarchyEntries();
    const auto offset = std::min(
        sceneHierarchyScrollOffset_, layout::sceneHierarchyMaxScrollOffset(hierarchy.size()));
    const auto row = static_cast<std::size_t>(
        (y - layout::kSceneHierarchyTop) / layout::kSceneHierarchyRowHeight);
    if (row >= layout::kSceneHierarchyVisibleRows || offset + row >= hierarchy.size())
        return false;
    for (const auto action :
        {layout::SceneHierarchyAction::Visibility, layout::SceneHierarchyAction::Lock}) {
        const auto rect = layout::sceneHierarchyActionRect(clip, row, action);
        if (rect.contains({x, y})) {
            target = {hierarchy[offset + row].objectId, panel, row, offset, action, rect};
            return true;
        }
    }
    return false;
}

bool EditorShell::handleSceneViewPointer(const EditorInputCommand& event)
{
    using Kind = EditorInputKind;
    auto& pointer = sceneViewPointer_;
    const bool position = event.kind == Kind::PointerDown || event.kind == Kind::PointerUp
        || event.kind == Kind::PointerMoved || event.kind == Kind::PointerDelta
        || event.kind == Kind::Wheel;
    if (event.kind == Kind::FocusLost) {
        const bool tail = pointer.captured || pointer.suppressRelease;
        pointer.reset();
        pointer.suppressRelease = tail;
        return false;
    }
    if (pointer.suppressRelease) {
        if (event.kind == Kind::PointerDown)
            pointer.reset();
        else if (event.kind == Kind::PointerUp && event.primaryButton) {
            pointer.reset();
            return true;
        }
    }
    if (pointer.captured && event.kind == Kind::KeyDown && event.key == EditorInputKey::Escape) {
        pointer.cancel();
        return true;
    }
    if (!position)
        return false;
    const bool owned = pointer.captured;
    try {
        if (!owned) {
            if (event.kind != Kind::PointerDown || !event.primaryButton
                || sceneReferenceDrag_.captured)
                return false;
            EditorSceneViewPointer::Target target;
            if (!sceneViewPointerTarget(event.x, event.y, target))
                return false;
            pointer.captured = true;
            pointer.target = target;
            pointer.documentEpoch = sceneDocumentEpoch_;
            pointer.viewRevision = sceneViewState_.revision();
            pointer.owner = sceneDocument_.ownerTicket();
        }
        const auto* terrainPanel =
            runtime().canvas().findWidget(terrainBrowserWidget("terrain_browser_panel"));
        if (!sceneNavigationGate().success || authoringUiBlocked() || presentationState_.menuOpen()
            || (terrainPanel && terrainPanel->visible()) || hierarchyDragObject_.valid()
            || sceneHierarchyDraggingScrollbar_ || fileDraggingScrollbar_
            || pointer.documentEpoch != sceneDocumentEpoch_
            || pointer.viewRevision != sceneViewState_.revision()
            || !pointer.owner.sameCurrentEpoch(sceneDocument_.ownerTicket()))
            pointer.cancel();
        if (!pointer.cancelled) {
            EditorSceneViewPointer::Target current;
            if (event.kind == Kind::Wheel || !sceneViewPointerTarget(event.x, event.y, current)
                || !pointer.target.same(current))
                pointer.cancel();
        }
        if (event.kind == Kind::PointerUp && event.primaryButton) {
            const auto target = pointer.target;
            const auto epoch = pointer.documentEpoch;
            const bool commit = !pointer.cancelled;
            pointer.reset(); // Shared command is independent of this input capture.
            if (commit) {
                const auto view = sceneObjectView(target.object);
                if (view.valid) {
                    auto flags = view.local;
                    if (target.action == layout::SceneHierarchyAction::Visibility)
                        flags.hidden = !flags.hidden;
                    else
                        flags.locked = !flags.locked;
                    const auto response = submitCommand(
                        SetSceneObjectViewCommand{
                            target.object.value(), epoch, flags.hidden, flags.locked},
                        EditorCommandOrigin::PlatformInput);
                    feedback(response.status == EditorResponseStatus::Completed
                            ? "View updated; tinted icons are local, amber bars are inherited"
                            : "View change rejected; state kept");
                }
            }
        }
        return true;
    } catch (const std::bad_alloc&) {
        pointer.cancel();
        const bool consumed = owned || pointer.captured;
        if (event.kind == Kind::PointerUp && event.primaryButton)
            pointer.reset();
        return consumed;
    }
}

core::pmr::Vector<SceneInspectorProperty> EditorShell::sceneInspectorProperties(
    scene::GameObjectId id) const
{
    auto properties = buildSceneInspectorProperties(
        sceneDocument_, detail::sceneResourceManager(sceneDocument_), id);
    for (auto& property : properties)
        property.editable = property.editable
            && sceneObjectEditable(id, property.componentType == "TransformComponent");
    return properties;
}

EditorSceneViewState::Observation EditorShell::sceneObjectView(
    scene::GameObjectId id) const noexcept
{
    return sceneViewState_.observe(sceneDocument_, sceneDocumentEpoch_, id);
}

bool EditorShell::sceneObjectEditable(scene::GameObjectId id, bool subtree) const noexcept
{
    const auto view = sceneObjectView(id);
    return view.valid && !view.effective.locked
        && (!subtree || sceneViewState_.subtreeEditable(sceneDocument_, sceneDocumentEpoch_, id));
}

bool EditorShell::sceneObjectVisible(scene::GameObjectId id) const noexcept
{
    const auto view = sceneObjectView(id);
    return view.valid && !view.effective.hidden;
}

bool EditorShell::sceneObjectPickable(scene::GameObjectId id) const noexcept
{
    const auto view = sceneObjectView(id);
    if (!view.valid || view.effective.hidden || view.effective.locked)
        return false;
    const auto* current = sceneDocument_.scene().findObject(id);
    for (std::size_t depth = 0; current && depth <= sceneDocument_.scene().objects().size();
        ++depth) {
        if (!current->active())
            return false;
        if (!current->parentId().valid())
            return current->id() == sceneDocument_.scene().rootId();
        current = sceneDocument_.scene().findObject(current->parentId());
    }
    return false;
}

bool EditorShell::sceneSelectionGizmoVisible() const noexcept
{
    const auto allowed = [this](scene::GameObjectId id) {
        return sceneObjectPickable(id) && sceneObjectEditable(id, true);
    };
    return allowed(sceneDocument_.selectedObject())
        && std::all_of(sceneViewportAdditionalSelection_.begin(),
            sceneViewportAdditionalSelection_.end(),
            allowed);
}

std::optional<scene::SceneTraversalSnapshot> EditorShell::visibleSceneSnapshot(
    const scene::SceneTraversalSnapshot& source, const char*& diagnostic) const noexcept
{
    return sceneViewState_.visibleSnapshot(sceneDocument_, sceneDocumentEpoch_, source, diagnostic);
}

EditorCommandOutcome EditorShell::sceneObjectWriteGate(std::uint64_t id, bool subtree) const
{
    const auto target = id ? scene::GameObjectId::fromValue(id) : sceneDocument_.selectedObject();
    if (!sceneObjectView(target).valid)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (!sceneObjectEditable(target, subtree))
        return {false, false, EditorDiagnosticCode::SceneObjectLocked};
    return {true, false, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::setSceneObjectView(const SetSceneObjectViewCommand& command)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    if (authoringUiBlocked() || presentationState_.menuOpen())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (!command.documentEpoch || command.documentEpoch != sceneDocumentEpoch_)
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    EditorSceneViewState::Prepared prepared;
    const char* diagnostic = nullptr;
    const EditorSceneViewState::Intent intent{command.documentEpoch,
        sceneDocument_.ownerTicket(),
        scene::GameObjectId::fromValue(command.objectId),
        {command.hidden, command.locked}};
    if (!sceneViewState_.prepare(sceneDocument_, sceneDocumentEpoch_, intent, prepared, diagnostic))
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    const auto result = sceneViewState_.commit(sceneDocument_, sceneDocumentEpoch_, prepared);
    if (result == EditorSceneViewState::CommitResult::Unchanged)
        return {true, false, EditorDiagnosticCode::None};
    if (result != EditorSceneViewState::CommitResult::Changed)
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    // View publication is complete. Inspector presentation uses U4's bounded
    // failure quarantine/retry and cannot claim to roll back the model.
    sceneTransformGizmoViewValid_ = false;
    sceneGizmoAxis_ = SceneGizmoAxis::None;
    sceneInspectorWidgetOwner_ = {};
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

} // namespace arp::editor
