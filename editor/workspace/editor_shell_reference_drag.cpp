#include "editor/workspace/editor_shell.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <new>

namespace arp::editor {
namespace {
bool contains(ui::LayoutRect rect, float x, float y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

// Widget names encode the full authoring identity, not a menu index or label.
// Admission additionally requires the current tracked Widget and exact metadata.
bool decodeField(std::string_view source, EditorSceneReferenceAssignment::Intent& intent)
{
    constexpr std::string_view prefix = "scene_property_";
    if (source.substr(0, prefix.size()) != prefix)
        return false;
    const auto objectEnd = source.find('_', prefix.size());
    const auto componentEnd =
        objectEnd == source.npos ? source.npos : source.find('_', objectEnd + 1);
    if (componentEnd == source.npos)
        return false;
    const auto object =
        std::from_chars(source.data() + prefix.size(), source.data() + objectEnd, intent.objectId);
    const auto component = std::from_chars(
        source.data() + objectEnd + 1, source.data() + componentEnd, intent.componentId);
    const auto path = source.substr(componentEnd + 1);
    if (object.ec != std::errc{} || object.ptr != source.data() + objectEnd
        || component.ec != std::errc{} || component.ptr != source.data() + componentEnd
        || !intent.objectId || !intent.componentId || path.empty()
        || path.size() > EditorSceneDocument::maxPropertyNameBytes)
        return false;
    intent.property = path;
    return true;
}
} // namespace

void EditorShell::reportSceneReferenceFailure()
{
    sceneInspectorReferenceFailureOwner_ = sceneDocument_.selectedObject();
    sceneInspectorReferenceFailureTicket_ = sceneDocument_.ownerTicket();
    sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
    refreshSceneInspector();
}

bool EditorShell::sceneReferenceDragCurrent() const
{
    const auto& drag = sceneReferenceDrag_;
    if (!drag.captured || drag.cancelled || presentationState_.workspace() != "3D"
        || presentationState_.dock() != "Inspector" || !sceneNavigationGate().success
        || sceneInspectorState_ != InspectorState::Ready
        || !sceneViewportAdditionalSelection_.empty()
        || sceneDocument_.selectedObject().value() != drag.objectId
        || sceneDocumentEpoch_ != drag.documentEpoch || resourceRevision_ != drag.resourceRevision
        || !drag.sceneOwner.sameCurrentEpoch(sceneDocument_.ownerTicket())
        || !drag.sceneOwner.sameCurrentEpoch(sceneInspectorSceneTicket_)
        || !sceneInspectorSelectionKey_.matches(sceneDocument_.selectedObject(), {}))
        return false;
    const auto* panel =
        runtime().canvas().findWidget(terrainBrowserWidget("terrain_browser_panel"));
    return !panel || !panel->visible();
}

const char* EditorShell::prepareSceneReferenceDrop(
    float x, float y, EditorSceneReferenceAssignment::Intent& intent, ui::WidgetId& target)
{
    using Assignment = EditorSceneReferenceAssignment;
    target = {};
    if (!sceneReferenceDragCurrent())
        return "EDITOR-REFERENCE-DRAG-STALE";
    const auto layout = runtime().canvas().resolveLayout();
    if (!layout.ok())
        return "EDITOR-REFERENCE-DRAG-LAYOUT";
    const auto hit = runtime().canvas().hitTest(layout, {x, y}).widgetId();
    const auto* resolved = layout.findWidget(hit);
    const auto* widget = runtime().canvas().findWidget(hit);
    if (!resolved || !widget || !resolved->visibleInHierarchy || !resolved->enabledInHierarchy
        || resolved->controlType != ui::UiControlType::Dropdown
        || !widget->desc().acceptsPointerInput || !acceptsInspectorInput(widget->name(), hit)
        || presentationState_.sourceFor(hit.value()) != widget->name()
        || !decodeField(widget->name(), intent) || intent.objectId != sceneReferenceDrag_.objectId)
        return "EDITOR-REFERENCE-DRAG-TARGET";
    intent.documentEpoch = sceneReferenceDrag_.documentEpoch;
    intent.resourceRevision = sceneReferenceDrag_.resourceRevision;
    intent.sourceKind = sceneReferenceDrag_.fileSource ? Assignment::SourceKind::Resource
                                                       : Assignment::SourceKind::Object;
    intent.sourceObjectId = sceneReferenceDrag_.sourceObjectId;
    intent.sourceResourceId = sceneReferenceDrag_.resourceId.data();
    Assignment::Context context;
    context.documentEpoch = sceneDocumentEpoch_;
    context.resourceRevision = resourceRevision_;
    context.selectedObjectId = sceneDocument_.selectedObject().value();
    context.selectedCount = 1;
    context.sceneOwner = sceneReferenceDrag_.sceneOwner;
    Assignment::Prepared prepared;
    const auto* issue = Assignment::prepare(sceneDocument_,
        projectResources_.manager(),
        projectResources_.resourceLocations(),
        context,
        intent,
        prepared);
    // Even an incompatible reference has a real target for rejected feedback.
    target = hit;
    return issue;
}

bool EditorShell::handleSceneReferenceDrag(const EditorInputCommand& event)
{
    using Kind = EditorInputKind;
    auto& drag = sceneReferenceDrag_;
    const bool pointer = event.kind == Kind::PointerDown || event.kind == Kind::PointerUp
        || event.kind == Kind::PointerMoved || event.kind == Kind::PointerDelta
        || event.kind == Kind::Wheel;
    bool consumed = drag.captured && pointer;
    if (event.kind == Kind::FocusLost) {
        const bool tail = drag.captured || drag.suppressRelease;
        drag.reset();
        drag.suppressRelease = tail;
        return false; // The UI must receive the same cancellation barrier.
    }
    if (drag.suppressRelease) {
        if (event.kind == Kind::PointerDown)
            drag.reset();
        else if (event.kind == Kind::PointerUp && event.primaryButton) {
            drag.reset();
            return true;
        }
    }
    if (event.kind == Kind::KeyDown && event.key == EditorInputKey::Escape && drag.captured) {
        drag.cancel();
        return true;
    }
    if (!pointer)
        return false;
    try {
        if (!std::isfinite(event.x) || !std::isfinite(event.y)) {
            drag.cancel();
            if (event.kind == Kind::PointerUp && event.primaryButton)
                drag.reset();
            return consumed;
        }
        if (drag.captured) {
            if (!sceneReferenceDragCurrent())
                drag.cancel();
            drag.x = event.x;
            drag.y = event.y;
            const double dx = double(drag.x) - drag.startX, dy = double(drag.y) - drag.startY;
            if (dx * dx + dy * dy >= double(EditorSceneReferenceDrag::thresholdPixels)
                    * EditorSceneReferenceDrag::thresholdPixels)
                drag.dragging = true;
            if (event.kind == Kind::PointerUp && event.primaryButton) {
                if (!drag.cancelled && !drag.dragging) {
                    const auto source = drag.sourceObjectId;
                    const auto file = drag.sourceWidget;
                    drag.reset();
                    if (source)
                        (void)submitCommand(
                            SelectSceneObjectCommand{source}, EditorCommandOrigin::PlatformInput);
                    else if (const auto* widget = runtime().canvas().findWidget(file))
                        activate(widget->name());
                } else if (!drag.cancelled) {
                    AssignSceneReferenceCommand command;
                    ui::WidgetId target;
                    const auto* issue =
                        prepareSceneReferenceDrop(event.x, event.y, command.intent, target);
                    drag.reset(); // Shared command is independent of pointer capture.
                    if (!issue) {
                        const auto result =
                            submitCommand(std::move(command), EditorCommandOrigin::PlatformInput);
                        if (result.status != EditorResponseStatus::Completed)
                            reportSceneReferenceFailure();
                        else if (sceneInspectorReferenceFailureOwner_.valid()) {
                            sceneInspectorReferenceFailureOwner_ = {};
                            sceneInspectorWidgetOwner_ = {};
                            refreshSceneInspector();
                        }
                        feedback(result.status == EditorResponseStatus::Completed
                                ? "Reference assigned"
                                : "Reference drop failed; value kept");
                    } else {
                        reportSceneReferenceFailure();
                        feedback("Reference drop rejected; value kept");
                    }
                } else
                    drag.reset();
            } else if (!drag.cancelled && drag.dragging && event.kind != Kind::Wheel) {
                EditorSceneReferenceAssignment::Intent intent;
                drag.allowed =
                    !prepareSceneReferenceDrop(event.x, event.y, intent, drag.targetWidget);
            }
            return true;
        }
        if (event.kind != Kind::PointerDown || !event.primaryButton || modifiers_.control()
            || modifiers_.shift() || presentationState_.workspace() != "3D"
            || presentationState_.dock() != "Inspector" || !sceneNavigationGate().success
            || hierarchyDragObject_.valid() || sceneHierarchyDraggingScrollbar_
            || fileDraggingScrollbar_ || sceneInspectorState_ != InspectorState::Ready
            || !sceneViewportAdditionalSelection_.empty())
            return false;
        if (const auto* panel =
                runtime().canvas().findWidget(terrainBrowserWidget("terrain_browser_panel"));
            panel && panel->visible())
            return false;
        const auto layout = runtime().canvas().resolveLayout();
        if (!layout.ok())
            return false;
        const auto hit = runtime().canvas().hitTest(layout, {event.x, event.y}).widgetId();
        const auto* resolved = layout.findWidget(hit);
        if (!resolved || !resolved->visibleInHierarchy || !resolved->enabledInHierarchy)
            return false;
        EditorSceneReferenceDrag candidate;
        if (sceneDockTab_ == "Scene" && sceneHierarchyPointerHit(layout, event.x, event.y)
            && event.x < layout::sceneHierarchyActionRect(
                   layout::sceneHierarchyClip(layout, id("left_scene")),
                   0,
                   layout::SceneHierarchyAction::Visibility)
                    .x) {
            const auto hierarchy = visibleSceneHierarchyEntries();
            const auto offset = std::min(sceneHierarchyScrollOffset_,
                layout::sceneHierarchyMaxScrollOffset(hierarchy.size()));
            const auto row = static_cast<std::size_t>(
                (event.y - layout::kSceneHierarchyTop) / layout::kSceneHierarchyRowHeight);
            if (offset + row < hierarchy.size()
                && contains(layout::sceneHierarchyIconRect(row, hierarchy[offset + row].depth),
                    event.x,
                    event.y))
                candidate.sourceObjectId = hierarchy[offset + row].objectId.value();
        }
        if (!candidate.sourceObjectId && !filesPresentationFailed_) {
            for (std::size_t slot = 0; slot < layout::kFileSlotCount; ++slot) {
                if (hit != id(layout::kFileSlots[slot]))
                    continue;
                const auto& visible = projectBrowser_.visibleEntryIndices();
                const auto& entries = projectBrowser_.entries();
                const auto index = fileScrollOffset_ + slot;
                if (index >= visible.size() || visible[index] >= entries.size()
                    || entries[visible[index]].directory)
                    break;
                ProjectResourceLocations::Definition definition;
                if (projectResources_.resourceLocations().identify(
                        entries[visible[index]].path, projectResources_.manager(), definition)
                    || definition.resourceId.size() != candidate.resourceId.size() - 1)
                    break;
                std::memcpy(candidate.resourceId.data(),
                    definition.resourceId.data(),
                    definition.resourceId.size());
                candidate.fileSource = true;
                candidate.sourceWidget = hit;
                break;
            }
        }
        if (!candidate.sourceObjectId && !candidate.fileSource)
            return false;
        candidate.captured = true;
        candidate.objectId = sceneDocument_.selectedObject().value();
        candidate.documentEpoch = sceneDocumentEpoch_;
        candidate.resourceRevision = resourceRevision_;
        candidate.sceneOwner = sceneInspectorSceneTicket_;
        candidate.x = candidate.startX = event.x;
        candidate.y = candidate.startY = event.y;
        drag = candidate;
        if (!sceneReferenceDragCurrent())
            drag.cancel();
        return true;
    } catch (const std::bad_alloc&) {
        drag.cancel();
        if (event.kind == Kind::PointerUp && event.primaryButton)
            drag.reset();
        // No setter runs unless cold preparation and shared dispatch succeed.
        return consumed || drag.captured;
    }
}

void EditorShell::appendSceneReferenceDrag(const ui::CanvasLayoutResult& layout,
    core::pmr::Vector<renderer::ui::UiDrawCommand>& commands) const
{
    const auto& drag = sceneReferenceDrag_;
    if (!layout.ok() || !drag.captured || !drag.dragging || presentationState_.workspace() != "3D")
        return;
    const auto clip = ui::LayoutRect::fromOriginSize(
        0, 0, runtime().canvas().desc().viewport.width, runtime().canvas().desc().viewport.height);
    renderer::ui::UiDrawCommand label;
    label.kind = renderer::ui::UiDrawCommandKind::Text;
    label.rect = ui::LayoutRect::fromOriginSize(
        std::clamp(drag.x + 12, 0.f, std::max(0.f, clip.width - 260)),
        std::clamp(drag.y + 22, 0.f, std::max(0.f, clip.height - 26)),
        260,
        24);
    label.clipRect = clip;
    label.text = drag.cancelled ? "Reference drag cancelled"
        : drag.allowed          ? "Release to assign reference"
                                : "Cannot assign here";
    label.fontToken = "font.editor";
    label.styleId = "text";
    label.color[0] = drag.allowed ? 0.4f : 1.f;
    label.color[1] = drag.allowed ? 1.f : 0.6f;
    label.color[2] = drag.allowed ? 0.6f : 0.4f;
    label.color[3] = 1.f;
    auto background = label;
    background.kind = renderer::ui::UiDrawCommandKind::Box;
    background.text.clear();
    background.color[0] = background.color[1] = background.color[2] = 0.08f;
    commands.push_back(std::move(background));
    // Existing UI text lowering only; no new GPU intent or shader.
    commands.push_back(std::move(label));
}
} // namespace arp::editor
