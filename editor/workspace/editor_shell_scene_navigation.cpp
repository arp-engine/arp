#include "editor/viewport/editor_scene_selection_focus.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_reference_focus.h"
#include "editor/workspace/editor_shell.h"

#include <charconv>
#include <new>

namespace arp::editor {

EditorCommandOutcome EditorShell::sceneNavigationGate() const
{
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    if (projectPanel_.modalActive() || scenePanel_.active() || scriptFilePanel_.active()
        || exitPanel_.active() || activeTransformEditObjectId_ != 0
        || sceneViewportGesture_ != SceneViewportGesture::None || sceneViewportRightButtonHeld_
        || hierarchyDragging_)
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    return {true, false, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::focusSceneSelection()
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    return focusSceneObjects(sceneDocument_.selectedObject(), sceneViewportAdditionalSelection_);
}

EditorCommandOutcome EditorShell::focusSceneObjects(
    scene::GameObjectId primary, core::ArrayView<scene::GameObjectId> additional)
{
    if (additional.size() >= EditorSceneAuthoringCodec::maxObjects)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.ok() ? layout.findWidget(id("viewport")) : nullptr;
        if (!viewport)
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        scene::SceneTraversalOptions options;
        options.maxNodeCount = EditorSceneAuthoringCodec::maxObjects + 1u;
        const auto snapshot = sceneDocument_.scene().extractTraversalSnapshot(options);
        using Focus = EditorSceneSelectionFocus;
        const auto result = Focus::apply(sceneViewportCamera_,
            snapshot,
            primary,
            additional,
            viewport->rect.width,
            viewport->rect.height);
        if (result == Focus::Result::Changed || result == Focus::Result::Unchanged)
            return {true, result == Focus::Result::Changed, EditorDiagnosticCode::None};
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    } catch (const std::bad_alloc&) {
        // Navigation failed before publication; the original camera survives.
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
}

EditorCommandOutcome EditorShell::focusSceneReference(const FocusSceneReferenceCommand& command)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    if (command.documentEpoch == 0 || command.documentEpoch != sceneDocumentEpoch_)
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    std::uint64_t target = 0;
    const auto* diagnostic = EditorSceneReferenceFocus::prepare(
        sceneDocument_, command.objectId, command.componentId, command.property, target);
    if (diagnostic) {
        const std::string_view reason(diagnostic);
        const bool preparationFailed = reason == "EDITOR-REFERENCE-FOCUS-ALLOCATION-FAILED"
            || reason == "EDITOR-REFERENCE-FOCUS-BUDGET";
        return {false,
            false,
            preparationFailed ? EditorDiagnosticCode::CommandPreparationFailed
                              : EditorDiagnosticCode::SceneReferenceUnavailable};
    }
    return focusSceneObjects(scene::GameObjectId::fromValue(target), {});
}

EditorCommandOutcome EditorShell::assignSceneReference(const AssignSceneReferenceCommand& command)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    using Assignment = EditorSceneReferenceAssignment;
    Assignment::Context context;
    context.documentEpoch = sceneDocumentEpoch_;
    context.resourceRevision = resourceRevision_;
    context.selectedObjectId = sceneDocument_.selectedObject().value();
    context.selectedCount = 1 + sceneViewportAdditionalSelection_.size();
    context.sceneOwner = sceneInspectorSceneTicket_;
    Assignment::Prepared prepared;
    const auto* issue = Assignment::prepare(sceneDocument_,
        projectResources_.manager(),
        projectResources_.resourceLocations(),
        context,
        command.intent,
        prepared);
    if (issue) {
        const std::string_view reason(issue);
        return {false,
            false,
            reason == "EDITOR-REFERENCE-ASSIGNMENT-STALE"
                ? EditorDiagnosticCode::StaleDocumentTarget
                : reason == "EDITOR-REFERENCE-ASSIGNMENT-ALLOCATION-FAILED"
                    || reason == "EDITOR-REFERENCE-FOCUS-ALLOCATION-FAILED"
                    || reason == "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED"
                    || reason == "EDITOR-REFERENCE-FOCUS-BUDGET"
                ? EditorDiagnosticCode::CommandPreparationFailed
                : EditorDiagnosticCode::SceneReferenceUnavailable};
    }
    if (!prepared.changed)
        return {true, false, EditorDiagnosticCode::None};
    SetSceneComponentPropertyCommand property(command.intent.property);
    property.objectId = command.intent.objectId;
    property.componentId = command.intent.componentId;
    if (prepared.kind == SceneInspectorPropertyKind::SceneObjectReference) {
        property.valueKind = ScenePropertyValueKind::SceneObjectReference;
        property.sceneObjectReferenceValue = prepared.objectId;
    } else {
        property.valueKind = ScenePropertyValueKind::ResourceReference;
        property.stringValue = std::move(prepared.resourceId);
    }
    // Stay inside this dispatch. The existing property implementation owns the
    // setter, preallocated history and fallible Inspector presentation.
    return execute(EditorCommandPayload{std::move(property)});
}

bool EditorShell::activateSceneReferenceFocus(std::string_view source)
{
    constexpr std::string_view prefix = "scene_reference_focus_";
    if (source.substr(0, prefix.size()) != prefix)
        return false;
    // Decode the explicit scenePropertyControlSource identity. Only prepare()
    // resolves field metadata/current target; no unbounded duplicate extraction.
    constexpr std::string_view fieldPrefix = "scene_reference_focus_scene_property_";
    if (source.substr(0, fieldPrefix.size()) != fieldPrefix)
        return true;
    const auto objectEnd = source.find('_', fieldPrefix.size());
    const auto componentEnd =
        objectEnd == std::string_view::npos ? objectEnd : source.find('_', objectEnd + 1);
    if (componentEnd == std::string_view::npos)
        return true;
    FocusSceneReferenceCommand command;
    const auto object = std::from_chars(
        source.data() + fieldPrefix.size(), source.data() + objectEnd, command.objectId);
    const auto component = std::from_chars(
        source.data() + objectEnd + 1, source.data() + componentEnd, command.componentId);
    const auto path = source.substr(componentEnd + 1);
    if (object.ec != std::errc{} || object.ptr != source.data() + objectEnd
        || component.ec != std::errc{} || component.ptr != source.data() + componentEnd
        || command.objectId != sceneDocument_.selectedObject().value() || !command.componentId
        || path.empty() || path.size() > EditorSceneInspectorSelection::editorBudget.maxStringBytes)
        return true;
    command.documentEpoch = sceneDocumentEpoch_;
    command.property = path;
    const auto response = submitCommand(std::move(command));
    if (response.diagnostic == EditorDiagnosticCode::SceneReferenceUnavailable) {
        // An external edit may have invalidated the displayed target. U4 owns
        // refresh failure; navigation never restores or focuses the old ID.
        sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
        refreshSceneInspector();
    }
    return true;
}
} // namespace arp::editor
