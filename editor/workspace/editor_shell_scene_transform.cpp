#include "editor/workspace/editor_shell.h"

#include <cstdio>
#include <new>

namespace arp::editor {

EditorSceneTransformSettings EditorShell::sceneTransformSettings() const noexcept
{
    EditorSceneTransformSettings result;
    result.tool = sceneTransformTool_;
    result.space = sceneTransformSpace_;
    result.pivot = sceneTransformPivot_;
    result.snap = sceneTransformSnap_;
    return result;
}

EditorCommandOutcome EditorShell::setSceneTransformSettings(
    const EditorSceneTransformSettings& settings, const EditorSceneTransformPanel* authority)
{
    if (!settings.valid())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto gate = sceneTransformSettingsGate(authority);
    if (!gate.success)
        return gate;
    if (settings.same(sceneTransformSettings()))
        return {true, false, EditorDiagnosticCode::None};
    // The three labels and their exact allocators are validated before any write.
    // UI Widget::setText moves into the existing PMR string; equal allocators
    // make the publication below allocation-free, with no control callback.
    try {
        const char* names[] = {
            "scene_transform_space", "scene_transform_pivot", "scene_transform_snap"};
        ui::Widget* widgets[3]{};
        for (unsigned i = 0; i != 3; ++i) {
            widgets[i] = runtime().canvas().findWidget(id(names[i]));
            if (!widgets[i] || widgets[i]->desc().controlType != ui::UiControlType::Button)
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        }
        char caption[192];
        const auto length = std::snprintf(caption,
            sizeof(caption),
            "Snap %s | Move %.6g | Turn %.6g rad | Scale %.6g",
            settings.snap.enabled ? "On" : "Off",
            settings.snap.moveStep,
            settings.snap.rotateStepRadians,
            settings.snap.scaleStep);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(caption))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        core::pmr::String labels[] = {
            core::pmr::String(
                settings.space == EditorSceneTransformSpace::Local ? "Local" : "World",
                widgets[0]->desc().text.get_allocator().resource()),
            core::pmr::String(settings.pivot == EditorSceneTransformPivot::Active ? "Pivot Active"
                    : settings.pivot == EditorSceneTransformPivot::Center         ? "Pivot Center"
                                                                          : "Pivot Individual",
                widgets[1]->desc().text.get_allocator().resource()),
            core::pmr::String(caption, widgets[2]->desc().text.get_allocator().resource())};
        for (unsigned i = 0; i != 3; ++i)
            widgets[i]->setText(std::move(labels[i]));
        sceneTransformTool_ = settings.tool;
        sceneTransformSpace_ = settings.space;
        sceneTransformPivot_ = settings.pivot;
        sceneTransformSnap_ = settings.snap;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        return {true, true, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
}

EditorCommandOutcome EditorShell::setSceneTransformSpace(EditorSceneTransformSpace space)
{
    auto settings = sceneTransformSettings();
    settings.space = space;
    return setSceneTransformSettings(settings);
}

EditorCommandOutcome EditorShell::sceneTransformSettingsGate(
    const EditorSceneTransformPanel* authority) const
{
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    const bool ownsPanel = authority == &sceneTransformPanel_ && sceneTransformPanel_.active();
    const bool foreignUi = projectPanel_.active() || scenePanel_.active()
        || scriptFilePanel_.active() || exitPanel_.active() || presentationState_.menuOpen();
    if ((authority && !ownsPanel) || foreignUi || (sceneTransformPanel_.active() && !ownsPanel)
        || activeTransformEditObjectId_ != 0 || activeSceneSelectionTransformEditId() != 0
        || sceneViewportGesture_ != SceneViewportGesture::None || sceneViewportRightButtonHeld_)
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    return {true, false, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::setSceneTransformSnap(const EditorSceneTransformSnap& snap)
{
    auto settings = sceneTransformSettings();
    settings.snap = snap;
    return setSceneTransformSettings(settings);
}

void EditorShell::captureSceneTransformGesture(const scene::GameObject& object)
{
    sceneGizmoObject_ = object.id();
    sceneGizmoStartLocal_ = object.local_transform();
    sceneGizmoLastLocal_ = sceneGizmoStartLocal_;
    sceneGizmoParentId_ = object.parentId();
    const auto* parent = object.parent_object();
    sceneGizmoParentWorld_ = parent ? parent->world_transform() : scene::Transform::identity();
    sceneGizmoDocumentEpoch_ = sceneDocumentEpoch_;
    sceneGizmoHistoryEpoch_ = sceneHistoryEpoch_.ticket();
    sceneGizmoSingleEditId_ = activeSingleSceneTransform_ ? activeSingleSceneTransform_->id : 0;
}

bool EditorShell::beginSceneTransformGesture(const scene::GameObject& object, unsigned axis)
{
    if (sceneViewportAdditionalSelection_.empty()) {
        if (submitCommand(BeginSceneTransformEditCommand{object.id().value()},
                EditorCommandOrigin::PlatformInput)
                .status
            != EditorResponseStatus::Completed)
            return false;
        sceneGizmoBatchEditId_ = 0;
    } else {
        TransformSceneSelectionCommand operation;
        operation.tool = sceneTransformTool_;
        operation.space = sceneTransformSpace_;
        operation.pivot = sceneTransformPivot_;
        operation.axis = axis;
        if (submitCommand(BeginSceneSelectionTransformEditCommand{operation},
                EditorCommandOrigin::PlatformInput)
                .status
            != EditorResponseStatus::Completed)
            return false;
        sceneGizmoBatchEditId_ = activeSceneSelectionTransformEditId();
    }
    captureSceneTransformGesture(object);
    return true;
}

bool EditorShell::sceneTransformGestureCurrent() const
{
    if (sceneGizmoBatchEditId_)
        return sceneSelectionTransformEditCurrent(sceneGizmoBatchEditId_);
    if (sceneGizmoDocumentEpoch_ != sceneDocumentEpoch_ || !sceneGizmoHistoryEpoch_.isCurrent()
        || activeTransformEditObjectId_ != sceneGizmoObject_.value()
        || !singleSceneTransformEditCurrent()
        || activeSingleSceneTransform_->id != sceneGizmoSingleEditId_
        || sceneDocument_.selectedObject() != sceneGizmoObject_)
        return false;
    const auto* object = sceneDocument_.scene().findObject(sceneGizmoObject_);
    if (!object || object->parentId() != sceneGizmoParentId_
        || !EditorSceneTransformFrame::same(object->local_transform(), sceneGizmoLastLocal_))
        return false;
    const auto* parent = object->parent_object();
    return EditorSceneTransformFrame::same(
        parent ? parent->world_transform() : scene::Transform::identity(), sceneGizmoParentWorld_);
}

EditorCommandOutcome EditorShell::finishSceneTransformGesture(bool endActive)
{
    EditorCommandOutcome result{true, false, EditorDiagnosticCode::None};
    const auto batchId = endActive ? activeSceneSelectionTransformEditId() : sceneGizmoBatchEditId_;
    if (batchId) {
        result = submitCommand(
            EndSceneSelectionTransformEditCommand{batchId}, EditorCommandOrigin::PlatformInput)
                     .intoNestedOutcome();
        if (!result.success && activeSceneSelectionTransformEditId() == batchId)
            return result;
    }
    // Never end a different document's gesture if numeric IDs were reused.
    if (endActive
        || (sceneGizmoDocumentEpoch_ == sceneDocumentEpoch_ && activeSingleSceneTransform_
            && activeSingleSceneTransform_->id == sceneGizmoSingleEditId_
            && activeTransformEditObjectId_ == sceneGizmoObject_.value()
            && activeTransformEditObjectId_ != 0)) {
        if (activeTransformEditObjectId_) {
            result = submitCommand(EndSceneTransformEditCommand{activeTransformEditObjectId_},
                EditorCommandOrigin::PlatformInput)
                         .intoNestedOutcome();
            if (!result.success && activeTransformEditObjectId_)
                return result;
        }
    }
    sceneViewportGesture_ = SceneViewportGesture::None;
    sceneGizmoObject_ = scene::GameObjectId::invalid();
    sceneGizmoAxis_ = SceneGizmoAxis::None;
    sceneGizmoHistoryEpoch_ = {};
    sceneGizmoBatchEditId_ = 0;
    sceneGizmoSingleEditId_ = 0;
    return result;
}

EditorCommandOutcome EditorShell::setSceneTransformPivot(EditorSceneTransformPivot pivot)
{
    auto settings = sceneTransformSettings();
    settings.pivot = pivot;
    return setSceneTransformSettings(settings);
}
} // namespace arp::editor
