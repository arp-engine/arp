#include "core/text/utf8_cursor.h"
#include "editor/ingress/platform_input_adapter.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"
#include "ui/scrollbar_geometry.h"

#include <algorithm>
#include <cmath>

namespace arp::editor {
using namespace layout;

bool EditorShell::handlePlatformInput(const platform::WindowInputEvent& event)
{
    using Kind = platform::WindowInputEvent::Kind;
    const bool pointer = event.kind == Kind::MouseButtonDown || event.kind == Kind::MouseButtonUp
        || event.kind == Kind::PointerMoved || event.kind == Kind::MouseMoved
        || event.kind == Kind::MouseWheel;
    // Admission itself may fail under OOM. An already owned sequence must still
    // fail closed before UI dispatch, even if the executor never runs.
    platformPointerConsumed_ = pointer
        && (scriptDraggingSelection_ || scriptDraggingScrollbar_ || scriptPointerSuppressRelease_
            || sceneViewPointer_.captured
            || (sceneViewPointer_.suppressRelease && event.kind == Kind::MouseButtonUp
                && event.mouseButton == platform::WindowMouseButton::Left)
            || sceneReferenceDrag_.captured
            || (sceneReferenceDrag_.suppressRelease && event.kind == Kind::MouseButtonUp
                && event.mouseButton == platform::WindowMouseButton::Left));
    (void)submitCommand(normalizePlatformInput(event), EditorCommandOrigin::PlatformInput);
    return platformPointerConsumed_;
}

EditorCommandOutcome EditorShell::applyPlatformInput(const EditorInputCommand& event)
{
    using Kind = EditorInputKind;
    const auto scriptBefore = captureScriptInputState();
    const bool keyboard =
        event.kind == Kind::KeyDown || event.kind == Kind::KeyUp || event.kind == Kind::Text;
    const bool scriptKeyboard = presentationState_.workspace() == "Script" && keyboard;
    const auto accepted = [&]() -> EditorCommandOutcome {
        return scriptKeyboard ? scriptInputOutcome(scriptBefore)
                              : EditorCommandOutcome{true, !keyboard, EditorDiagnosticCode::None};
    };
    platformPointerConsumed_ = handleSceneViewPointer(event);
    if (!platformPointerConsumed_)
        platformPointerConsumed_ = handleSceneReferenceDrag(event);
    if (platformPointerConsumed_) {
        // Escape cancels an owned local pointer sequence. Unlike an ignored
        // keyboard event this is a real change; the old pointer tail stays owned.
        if (keyboard && !scriptKeyboard)
            return {true, stateVersion() == scriptBefore.version, EditorDiagnosticCode::None};
        return accepted();
    }
    const auto hierarchyContains = [&] {
        return sceneHierarchyPointerHit(runtime().canvas().resolveLayout(), event.x, event.y);
    };
    if (event.kind == Kind::FocusLost) {
        const bool endingTransform =
            activeTransformEditObjectId_ || activeSceneSelectionTransformEditId();
        const bool clearingModifiers = modifiers_.control() || modifiers_.shift();
        auto ended = finishSceneTransformGesture(true);
        modifiers_.clear();
        cancelScriptPointerCapture();
        sceneViewportRightButtonHeld_ = false;
        sceneViewportGesture_ = SceneViewportGesture::None;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        hierarchyDragObject_ = scene::GameObjectId::invalid();
        hierarchyDragTarget_ = scene::GameObjectId::invalid();
        hierarchyDragging_ = false;
        sceneHierarchyDraggingScrollbar_ = false;
        fileDraggingScrollbar_ = false;
        scriptDraggingScrollbar_ = false;
        if (endingTransform && ended.success && clearingModifiers)
            ended.stateChanged = true;
        return endingTransform ? std::move(ended) : accepted();
    }
    // Physical-side bookkeeping must receive releases even while modal or
    // floating panels suppress authoring shortcuts. No allocation or UI calls.
    if ((event.kind == Kind::KeyDown || event.kind == Kind::KeyUp)
        && EditorModifierState::handles(event.key))
        return {true,
            modifiers_.update(event.key, event.kind == Kind::KeyDown),
            EditorDiagnosticCode::None};
    if (scriptPointerSuppressRelease_) {
        if (const auto tail = applyScriptPointerInput(event))
            return *tail;
    }
    if (event.kind == Kind::PointerUp && event.primaryButton
        && (scriptDraggingSelection_ || scriptDraggingScrollbar_)) {
        if (const auto released = applyScriptPointerInput(event))
            return *released;
    }
    if (authoringUiBlocked())
        return accepted();
    // UI dispatch still handles this floating panel; suppress viewport gestures
    // and shortcuts behind it. FocusLost above must always release gestures.
    if (const auto* panel =
            runtime().canvas().findWidget(terrainBrowserWidget("terrain_browser_panel"));
        panel && panel->visible())
        return accepted();
    if (const auto script = applyScriptPointerInput(event))
        return *script;
    if (event.kind == Kind::PointerUp && event.button == EditorPointerButton::Right) {
        sceneViewportRightButtonHeld_ = false;
        if (sceneViewportGesture_ == SceneViewportGesture::Look)
            sceneViewportGesture_ = SceneViewportGesture::None;
        return accepted();
    }
    if (event.kind == Kind::PointerUp && event.primaryButton
        && sceneViewportGesture_ == SceneViewportGesture::BoxSelect) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport) {
            sceneViewportBoxEndX_ = event.x;
            sceneViewportBoxEndY_ = event.y;
            const float left = std::min(sceneViewportBoxStartX_, sceneViewportBoxEndX_);
            const float right = std::max(sceneViewportBoxStartX_, sceneViewportBoxEndX_);
            const float top = std::min(sceneViewportBoxStartY_, sceneViewportBoxEndY_);
            const float bottom = std::max(sceneViewportBoxStartY_, sceneViewportBoxEndY_);
            const float dx = right - left, dy = bottom - top;
            SelectSceneObjectsCommand selection;
            if (dx * dx + dy * dy < 25.0f) {
                scene::GameObjectId nearest = scene::GameObjectId::invalid();
                float nearestDistance = 14.0f;
                for (const auto& object : sceneDocument_.scene().objects()) {
                    if (!sceneObjectPickable(object->id()))
                        continue;
                    float x = 0.0f, y = 0.0f;
                    if (!sceneViewportCamera_.project(object->world_transform().translation,
                            viewport->rect.width,
                            viewport->rect.height,
                            x,
                            y))
                        continue;
                    const float distanceX = event.x - (viewport->rect.x + x);
                    const float distanceY = event.y - (viewport->rect.y + y);
                    const float distance = std::sqrt(distanceX * distanceX + distanceY * distanceY);
                    if (distance < nearestDistance) {
                        nearest = object->id();
                        nearestDistance = distance;
                    }
                }
                if (nearest.valid())
                    (void)submitCommand(SelectSceneObjectCommand{nearest.value()},
                        EditorCommandOrigin::PlatformInput);
            } else {
                if (modifiers_.shift()) {
                    selection.objectIds.push_back(sceneDocument_.selectedObject().value());
                    selection.primaryObjectId = sceneDocument_.selectedObject().value();
                    for (const auto selected : sceneViewportAdditionalSelection_)
                        selection.objectIds.push_back(selected.value());
                }
                for (const auto& object : sceneDocument_.scene().objects()) {
                    if (!sceneObjectPickable(object->id()))
                        continue;
                    float x = 0.0f, y = 0.0f;
                    if (!sceneViewportCamera_.project(object->world_transform().translation,
                            viewport->rect.width,
                            viewport->rect.height,
                            x,
                            y))
                        continue;
                    x += viewport->rect.x;
                    y += viewport->rect.y;
                    if (x < left || x > right || y < top || y > bottom)
                        continue;
                    if (std::find(selection.objectIds.begin(),
                            selection.objectIds.end(),
                            object->id().value())
                        == selection.objectIds.end())
                        selection.objectIds.push_back(object->id().value());
                    selection.primaryObjectId = object->id().value();
                }
                if (selection.objectIds.empty()) {
                    const auto root = sceneDocument_.scene().rootId();
                    selection.objectIds.push_back(root.value());
                    selection.primaryObjectId = root.value();
                } else if (selection.primaryObjectId == 0u)
                    selection.primaryObjectId = selection.objectIds.back();
                (void)submitCommand(std::move(selection), EditorCommandOrigin::PlatformInput);
            }
        }
        sceneViewportGesture_ = SceneViewportGesture::None;
    }
    if (event.kind == Kind::PointerUp && event.primaryButton) {
        if (activeTransformEditObjectId_ || activeSceneSelectionTransformEditId()
            || sceneViewportGesture_ == SceneViewportGesture::TransformAxis) {
            platformPointerConsumed_ = true;
            return finishSceneTransformGesture(true);
        }
        if (hierarchyDragging_ && hierarchyDragObject_.valid() && hierarchyDragTarget_.valid()) {
            const auto hierarchy = visibleSceneHierarchyEntries();
            const auto targetIt =
                std::find_if(hierarchy.begin(), hierarchy.end(), [&](const auto& entry) {
                    return entry.objectId == hierarchyDragTarget_;
                });
            if (targetIt != hierarchy.end() && hierarchyDragTarget_ != hierarchyDragObject_) {
                const auto* target = sceneDocument_.scene().findObject(hierarchyDragTarget_);
                scene::GameObjectId parentId;
                std::size_t childIndex = 0u;
                const auto firstVisibleRow = std::min(sceneHierarchyScrollOffset_,
                    layout::sceneHierarchyMaxScrollOffset(hierarchy.size()));
                const float rowTop = layout::kSceneHierarchyTop
                    + static_cast<float>((targetIt - hierarchy.begin()) - firstVisibleRow)
                        * layout::kSceneHierarchyRowHeight;
                const float rowFraction =
                    std::clamp((event.y - rowTop) / layout::kSceneHierarchyRowHeight, 0.0f, 1.0f);
                if (target && rowFraction >= 0.34f && rowFraction <= 0.66f) {
                    parentId = hierarchyDragTarget_;
                    childIndex = target->childIds().size();
                } else if (target) {
                    parentId = target->parentId().valid() ? target->parentId()
                                                          : sceneDocument_.scene().rootId();
                    const auto* parent = sceneDocument_.scene().findObject(parentId);
                    if (parent) {
                        if (hierarchyDragTarget_ == sceneDocument_.scene().rootId())
                            childIndex = rowFraction > 0.66f ? parent->childIds().size() : 0u;
                        else {
                            const auto sibling = std::find(parent->childIds().begin(),
                                parent->childIds().end(),
                                hierarchyDragTarget_);
                            if (sibling != parent->childIds().end()) {
                                childIndex =
                                    static_cast<std::size_t>(sibling - parent->childIds().begin());
                                if (rowFraction > 0.66f)
                                    ++childIndex;
                                const auto* source =
                                    sceneDocument_.scene().findObject(hierarchyDragObject_);
                                if (source && source->parentId() == parentId) {
                                    const auto sourceSibling = std::find(parent->childIds().begin(),
                                        parent->childIds().end(),
                                        hierarchyDragObject_);
                                    if (sourceSibling != parent->childIds().end()
                                        && static_cast<std::size_t>(
                                               sourceSibling - parent->childIds().begin())
                                            < childIndex)
                                        --childIndex;
                                }
                            }
                        }
                    }
                }
                if (parentId.valid()) {
                    ReparentSceneObjectCommand command;
                    command.objectId = hierarchyDragObject_.value();
                    command.parentId = parentId.value();
                    command.childIndex = static_cast<std::uint32_t>(childIndex);
                    (void)submitCommand(command, EditorCommandOrigin::PlatformInput);
                }
            }
        }
        hierarchyDragObject_ = scene::GameObjectId::invalid();
        hierarchyDragTarget_ = scene::GameObjectId::invalid();
        hierarchyDragging_ = false;
        sceneViewportGesture_ = SceneViewportGesture::None;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        fileDraggingScrollbar_ = false;
        scriptDraggingScrollbar_ = false;
        sceneHierarchyDraggingScrollbar_ = false;
        scriptDraggingSelection_ = false;
        return accepted();
    }
    if (event.kind == Kind::PointerMoved && sceneHierarchyDraggingScrollbar_) {
        const auto hierarchy = visibleSceneHierarchyEntries();
        const auto trackY = layout::kSceneHierarchyTop;
        const auto trackHeight = layout::kSceneHierarchyVisibleHeight;
        const auto thumb = ui::ScrollbarGeometry::thumb(trackY,
            trackHeight,
            hierarchy.size(),
            layout::kSceneHierarchyVisibleRows,
            sceneHierarchyScrollOffset_,
            layout::kSceneHierarchyRowHeight);
        if (thumb.visible) {
            const float travel = trackHeight - thumb.height;
            const float fraction = travel > 0.0f
                ? std::clamp(
                      (event.y - trackY - sceneHierarchyScrollbarGrabOffset_) / travel, 0.0f, 1.0f)
                : 0.0f;
            sceneHierarchyScrollOffset_ = static_cast<std::size_t>(
                std::lround(fraction * static_cast<float>(thumb.maxOffset)));
        }
        return accepted();
    }
    if (event.kind == Kind::PointerUp
        && (event.button == EditorPointerButton::Middle
            || event.button == EditorPointerButton::Right)) {
        sceneViewportGesture_ = SceneViewportGesture::None;
        return accepted();
    }
    if (event.kind == Kind::PointerUp && event.primaryButton) {
        fileDraggingScrollbar_ = false;
        scriptDraggingScrollbar_ = false;
        scriptDraggingSelection_ = false;
        return accepted();
    }
    if (event.kind == Kind::PointerDelta && hierarchyDragObject_.valid()) {
        const float dx = event.x - hierarchyDragStartX_;
        const float dy = event.y - hierarchyDragStartY_;
        if (!hierarchyDragging_ && dx * dx + dy * dy >= 25.0f)
            hierarchyDragging_ = true;
        if (hierarchyDragging_ && hierarchyContains()) {
            const auto row = static_cast<std::size_t>(
                (event.y - layout::kSceneHierarchyTop) / layout::kSceneHierarchyRowHeight);
            const auto hierarchy = visibleSceneHierarchyEntries();
            const auto firstVisibleRow = std::min(sceneHierarchyScrollOffset_,
                layout::sceneHierarchyMaxScrollOffset(hierarchy.size()));
            const auto targetRow = firstVisibleRow + row;
            hierarchyDragTarget_ =
                targetRow < hierarchy.size() && row < layout::kSceneHierarchyVisibleRows
                ? hierarchy[targetRow].objectId
                : scene::GameObjectId::invalid();
        } else if (hierarchyDragging_)
            hierarchyDragTarget_ = scene::GameObjectId::invalid();
        return accepted();
    }
    if (event.kind == Kind::PointerMoved && fileDraggingScrollbar_) {
        const auto thumb = ui::ScrollbarGeometry::thumb(kFileScrollbarY,
            kFileScrollbarHeight,
            visibleFileEntryCount(),
            kFileSlotCount,
            fileScrollOffset_,
            24.0f);
        if (thumb.visible) {
            const float travel = kFileScrollbarHeight - thumb.height;
            const float fraction = travel > 0.0f
                ? std::clamp(
                      (event.y - kFileScrollbarY - fileScrollbarGrabOffset_) / travel, 0.0f, 1.0f)
                : 0.0f;
            fileScrollOffset_ = static_cast<std::size_t>(
                std::lround(fraction * static_cast<float>(thumb.maxOffset)));
            updateFileSlots();
        }
        return accepted();
    }
    if (event.kind == Kind::Wheel) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        const bool inSceneHierarchy = presentationState_.workspace() == "3D"
            && sceneDockTab_ == "Scene" && sceneHierarchyPointerHit(layout, event.x, event.y);
        if (inSceneHierarchy) {
            const auto hierarchy = visibleSceneHierarchyEntries();
            const auto delta = static_cast<int>(std::lround(event.wheelDelta * 3.0f));
            const auto maxOffset = layout::sceneHierarchyMaxScrollOffset(hierarchy.size());
            if (delta > 0)
                sceneHierarchyScrollOffset_ =
                    sceneHierarchyScrollOffset_ > static_cast<std::size_t>(delta)
                    ? sceneHierarchyScrollOffset_ - static_cast<std::size_t>(delta)
                    : 0u;
            else if (delta < 0)
                sceneHierarchyScrollOffset_ = std::min(
                    maxOffset, sceneHierarchyScrollOffset_ + static_cast<std::size_t>(-delta));
            else
                sceneHierarchyScrollOffset_ = std::min(sceneHierarchyScrollOffset_, maxOffset);
        } else if (presentationState_.workspace() == "3D" && viewport && event.x >= viewport->rect.x
            && event.x < viewport->rect.x + viewport->rect.width && event.y >= viewport->rect.y
            && event.y < viewport->rect.y + viewport->rect.height) {
            (void)submitCommand(SceneViewportCameraCommand{SceneViewportCameraOperation::Zoom,
                                    0.0f,
                                    event.wheelDelta,
                                    viewport->rect.width,
                                    viewport->rect.height},
                EditorCommandOrigin::PlatformInput);
        } else if (!filesPresentationFailed_ && event.x < 285.0f && event.y >= 700.0f
            && event.y < 1034.0f) {
            const auto delta = static_cast<int>(std::lround(event.wheelDelta * 3.0f));
            if (delta > 0)
                fileScrollOffset_ = fileScrollOffset_ > static_cast<std::size_t>(delta)
                    ? fileScrollOffset_ - static_cast<std::size_t>(delta)
                    : 0;
            else if (delta < 0)
                fileScrollOffset_ += static_cast<std::size_t>(-delta);
            updateFileSlots();
        }
        return accepted();
    }
    if (event.kind == Kind::KeyUp)
        return accepted();

    if (presentationState_.workspace() == "3D" && event.kind == Kind::PointerDown
        && event.button == EditorPointerButton::Right) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport && event.x >= viewport->rect.x
            && event.x < viewport->rect.x + viewport->rect.width && event.y >= viewport->rect.y
            && event.y < viewport->rect.y + viewport->rect.height) {
            sceneViewportRightButtonHeld_ = true;
            sceneViewportGesture_ = SceneViewportGesture::Look;
        }
        return accepted();
    }
    if (presentationState_.workspace() == "3D" && event.kind == Kind::PointerDown
        && event.primaryButton) {
        if (sceneDockTab_ == "Scene" && hierarchyContains()) {
            const auto hierarchy = visibleSceneHierarchyEntries();
            const auto maxOffset = layout::sceneHierarchyMaxScrollOffset(hierarchy.size());
            sceneHierarchyScrollOffset_ = std::min(sceneHierarchyScrollOffset_, maxOffset);
            const auto hierarchyClip =
                layout::sceneHierarchyClip(runtime().canvas().resolveLayout(), id("left_scene"));
            const float scrollbarX =
                hierarchyClip.x + hierarchyClip.width - layout::kSceneHierarchyScrollbarWidth;
            if (event.x >= scrollbarX && maxOffset > 0u) {
                const auto thumb = ui::ScrollbarGeometry::thumb(layout::kSceneHierarchyTop,
                    layout::kSceneHierarchyVisibleHeight,
                    hierarchy.size(),
                    layout::kSceneHierarchyVisibleRows,
                    sceneHierarchyScrollOffset_,
                    layout::kSceneHierarchyRowHeight);
                if (thumb.visible) {
                    if (event.y >= thumb.y && event.y < thumb.y + thumb.height) {
                        sceneHierarchyDraggingScrollbar_ = true;
                        sceneHierarchyScrollbarGrabOffset_ = event.y - thumb.y;
                    } else {
                        const auto page = layout::kSceneHierarchyVisibleRows;
                        sceneHierarchyScrollOffset_ = event.y < thumb.y
                            ? (sceneHierarchyScrollOffset_ > page
                                      ? sceneHierarchyScrollOffset_ - page
                                      : 0u)
                            : std::min(maxOffset, sceneHierarchyScrollOffset_ + page);
                    }
                }
                return accepted();
            }
            const auto row = static_cast<std::size_t>(
                (event.y - layout::kSceneHierarchyTop) / layout::kSceneHierarchyRowHeight);
            const auto visibleRow = sceneHierarchyScrollOffset_ + row;
            if (visibleRow < hierarchy.size() && row < layout::kSceneHierarchyVisibleRows) {
                const auto objectId = hierarchy[visibleRow].objectId;
                const auto* object = sceneDocument_.scene().findObject(objectId);
                const float disclosureX = layout::kSceneHierarchyLeft
                    + layout::kSceneHierarchyTextInset
                    + static_cast<float>(std::min(hierarchy[visibleRow].depth, std::size_t{12u}))
                        * layout::kSceneHierarchyDepthIndent
                    - 9.0f;
                if (sceneHierarchySearch_.empty() && object && !object->childIds().empty()
                    && event.x >= disclosureX && event.x < disclosureX + 9.0f) {
                    const auto collapsed = std::find(sceneHierarchyCollapsedObjects_.begin(),
                        sceneHierarchyCollapsedObjects_.end(),
                        objectId);
                    if (collapsed == sceneHierarchyCollapsedObjects_.end())
                        sceneHierarchyCollapsedObjects_.push_back(objectId);
                    else
                        sceneHierarchyCollapsedObjects_.erase(collapsed);
                    const auto updatedHierarchy = visibleSceneHierarchyEntries();
                    sceneHierarchyScrollOffset_ = std::min(sceneHierarchyScrollOffset_,
                        layout::sceneHierarchyMaxScrollOffset(updatedHierarchy.size()));
                    (void)submitCommand(SelectSceneObjectCommand{objectId.value()},
                        EditorCommandOrigin::PlatformInput);
                    return accepted();
                }
                (void)submitCommand(
                    SelectSceneObjectCommand{objectId.value()}, EditorCommandOrigin::PlatformInput);
                if (sceneHierarchySearch_.empty()) {
                    hierarchyDragObject_ = objectId == sceneDocument_.scene().rootId()
                        ? scene::GameObjectId::invalid()
                        : objectId;
                    hierarchyDragTarget_ = hierarchyDragObject_;
                    hierarchyDragStartX_ = event.x;
                    hierarchyDragStartY_ = event.y;
                    hierarchyDragging_ = false;
                }
            }
            return accepted();
        }
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport && event.x >= viewport->rect.x
            && event.x < viewport->rect.x + viewport->rect.width && event.y >= viewport->rect.y
            && event.y < viewport->rect.y + viewport->rect.height) {
            const auto selectedId = sceneDocument_.selectedObject();
            const auto* selectedObject = sceneDocument_.scene().findObject(selectedId);
            if (selectedObject && sceneSelectionGizmoVisible()) {
                sceneGizmoAxis_ = SceneGizmoAxis::None;
                core::Vec3 origin;
                EditorSceneTransformFrame frame;
                if (const auto* diagnostic = sceneTransformSelectionView(origin, frame)) {
                    feedback(diagnostic);
                    return accepted();
                }
                const auto& axes = frame.axes;
                if (sceneTransformTool_ == SceneTransformTool::Rotate) {
                    float centerX = 0.0f, centerY = 0.0f;
                    if (sceneViewportCamera_.project(origin,
                            viewport->rect.width,
                            viewport->rect.height,
                            centerX,
                            centerY)) {
                        centerX += viewport->rect.x;
                        centerY += viewport->rect.y;
                        constexpr float radius = EditorSceneTransformFrame::ringRadius;
                        constexpr unsigned segments = EditorSceneTransformFrame::ringSegments;
                        float bestDistance = 9.0f;
                        for (std::size_t axisIndex = 0; axisIndex < 3u; ++axisIndex) {
                            float priorX = 0.0f, priorY = 0.0f;
                            bool priorValid = false;
                            float orientationSign = 1.0f;
                            float ux = 0.0f, uy = 0.0f, vx = 0.0f, vy = 0.0f;
                            const bool basisValid =
                                sceneViewportCamera_.project(origin + frame.planeU(axisIndex),
                                    viewport->rect.width,
                                    viewport->rect.height,
                                    ux,
                                    uy)
                                && sceneViewportCamera_.project(origin + frame.planeV(axisIndex),
                                    viewport->rect.width,
                                    viewport->rect.height,
                                    vx,
                                    vy);
                            if (basisValid) {
                                ux -= centerX - viewport->rect.x;
                                uy -= centerY - viewport->rect.y;
                                vx -= centerX - viewport->rect.x;
                                vy -= centerY - viewport->rect.y;
                                const float determinant = ux * vy - uy * vx;
                                if (!std::isfinite(determinant) || std::abs(determinant) <= 0.001f)
                                    continue;
                                orientationSign = determinant < 0.0f ? -1.0f : 1.0f;
                            } else
                                continue;
                            for (unsigned segment = 0u; segment <= segments; ++segment) {
                                const float angle = 6.283185307179586f
                                    * static_cast<float>(segment % segments)
                                    / static_cast<float>(segments);
                                const auto point = origin
                                    + core::scale(frame.planeU(axisIndex), radius * std::cos(angle))
                                    + core::scale(
                                        frame.planeV(axisIndex), radius * std::sin(angle));
                                float px = 0.0f, py = 0.0f;
                                const bool valid = sceneViewportCamera_.project(
                                    point, viewport->rect.width, viewport->rect.height, px, py);
                                if (valid) {
                                    px += viewport->rect.x;
                                    py += viewport->rect.y;
                                }
                                if (priorValid && valid) {
                                    const float sx = px - priorX, sy = py - priorY;
                                    const float lengthSquared = sx * sx + sy * sy;
                                    if (lengthSquared > 0.0f) {
                                        const float t = std::clamp(
                                            ((event.x - priorX) * sx + (event.y - priorY) * sy)
                                                / lengthSquared,
                                            0.0f,
                                            1.0f);
                                        const float dx = event.x - (priorX + t * sx);
                                        const float dy = event.y - (priorY + t * sy);
                                        const float distance = std::sqrt(dx * dx + dy * dy);
                                        if (distance < bestDistance) {
                                            bestDistance = distance;
                                            sceneGizmoAxis_ =
                                                static_cast<SceneGizmoAxis>(axisIndex + 1u);
                                            sceneGizmoScreenOrientationSign_ = orientationSign;
                                        }
                                    }
                                }
                                priorX = px;
                                priorY = py;
                                priorValid = valid;
                            }
                        }
                        if (sceneGizmoAxis_ != SceneGizmoAxis::None) {
                            if (!beginSceneTransformGesture(
                                    *selectedObject, static_cast<unsigned>(sceneGizmoAxis_) - 1)) {
                                sceneGizmoAxis_ = SceneGizmoAxis::None;
                                return accepted();
                            }
                            sceneGizmoAccumulator_.reset({event.x - centerX, event.y - centerY, 0});
                            sceneViewportGesture_ = SceneViewportGesture::TransformAxis;
                            return accepted();
                        }
                    }
                } else if (sceneTransformTool_ == SceneTransformTool::Move
                    || sceneTransformTool_ == SceneTransformTool::Scale) {
                    float bestDistance = 9.0f;
                    for (std::size_t axisIndex = 0; axisIndex < 3u; ++axisIndex) {
                        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
                        if (!sceneViewportCamera_.project(
                                origin, viewport->rect.width, viewport->rect.height, x0, y0)
                            || !sceneViewportCamera_.project(origin
                                    + core::scale(
                                        axes[axisIndex], EditorSceneTransformFrame::axisLength),
                                viewport->rect.width,
                                viewport->rect.height,
                                x1,
                                y1))
                            continue;
                        x0 += viewport->rect.x;
                        y0 += viewport->rect.y;
                        x1 += viewport->rect.x;
                        y1 += viewport->rect.y;
                        const float sx = x1 - x0, sy = y1 - y0;
                        const float lengthSquared = sx * sx + sy * sy;
                        if (lengthSquared < 1.0f)
                            continue;
                        const float t =
                            std::clamp(((event.x - x0) * sx + (event.y - y0) * sy) / lengthSquared,
                                0.0f,
                                1.0f);
                        const float dx = event.x - (x0 + t * sx);
                        const float dy = event.y - (y0 + t * sy);
                        const float distance = std::sqrt(dx * dx + dy * dy);
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            sceneGizmoAxis_ = static_cast<SceneGizmoAxis>(axisIndex + 1u);
                            sceneGizmoScreenAxis_ = {sx, sy, 0.0f};
                        }
                    }
                    if (sceneGizmoAxis_ != SceneGizmoAxis::None) {
                        if (!beginSceneTransformGesture(
                                *selectedObject, static_cast<unsigned>(sceneGizmoAxis_) - 1)) {
                            sceneGizmoAxis_ = SceneGizmoAxis::None;
                            return accepted();
                        }
                        sceneGizmoAccumulator_.reset();
                        sceneViewportGesture_ = SceneViewportGesture::TransformAxis;
                        return accepted();
                    }
                }
            }
            if (!modifiers_.shift()) {
                scene::GameObjectId nearest = scene::GameObjectId::invalid();
                float nearestDistance = 14.0f;
                for (const auto& object : sceneDocument_.scene().objects()) {
                    if (!sceneObjectPickable(object->id()))
                        continue;
                    float x = 0.0f, y = 0.0f;
                    if (!sceneViewportCamera_.project(object->world_transform().translation,
                            viewport->rect.width,
                            viewport->rect.height,
                            x,
                            y))
                        continue;
                    const float distanceX = event.x - (viewport->rect.x + x);
                    const float distanceY = event.y - (viewport->rect.y + y);
                    const float distance = std::sqrt(distanceX * distanceX + distanceY * distanceY);
                    if (distance < nearestDistance) {
                        nearest = object->id();
                        nearestDistance = distance;
                    }
                }
                if (nearest.valid())
                    (void)submitCommand(SelectSceneObjectCommand{nearest.value()},
                        EditorCommandOrigin::PlatformInput);
            }
            sceneViewportGesture_ = SceneViewportGesture::BoxSelect;
            sceneViewportBoxStartX_ = sceneViewportBoxEndX_ = event.x;
            sceneViewportBoxStartY_ = sceneViewportBoxEndY_ = event.y;
            return accepted();
        }
    }
    if (presentationState_.workspace() == "3D" && event.kind == Kind::PointerDown
        && event.button == EditorPointerButton::Middle) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport && event.x >= viewport->rect.x
            && event.x < viewport->rect.x + viewport->rect.width && event.y >= viewport->rect.y
            && event.y < viewport->rect.y + viewport->rect.height) {
            sceneViewportGesture_ =
                modifiers_.shift() ? SceneViewportGesture::Pan : SceneViewportGesture::Orbit;
            if (!modifiers_.shift()) {
                const auto* selected =
                    sceneDocument_.scene().findObject(sceneDocument_.selectedObject());
                if (selected) {
                    sceneViewportOrbitPivot_ = selected->world_transform().translation;
                    SceneViewportCameraCommand orbit;
                    orbit.operation = SceneViewportCameraOperation::OrbitAround;
                    orbit.viewportWidth = viewport->rect.width;
                    orbit.viewportHeight = viewport->rect.height;
                    orbit.pivot = sceneViewportOrbitPivot_;
                    (void)submitCommand(orbit, EditorCommandOrigin::PlatformInput);
                }
            }
        }
        return accepted();
    }
    if (presentationState_.workspace() == "3D" && event.kind == Kind::PointerDelta
        && sceneViewportGesture_ != SceneViewportGesture::None) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport && sceneViewportGesture_ == SceneViewportGesture::TransformAxis) {
            if (!sceneTransformGestureCurrent()) {
                auto ended = finishSceneTransformGesture();
                // Retire stale input without overwriting external content. The
                // actual End receipt is preserved even when it rejects stale state.
                return ended;
            }
            const float lengthSquared = sceneGizmoScreenAxis_.x * sceneGizmoScreenAxis_.x
                + sceneGizmoScreenAxis_.y * sceneGizmoScreenAxis_.y;
            const auto* object = sceneDocument_.scene().findObject(sceneGizmoObject_);
            if (object
                && (sceneTransformTool_ == SceneTransformTool::Rotate || lengthSquared >= 1.0f)) {
                auto accumulated = sceneGizmoAccumulator_;
                if (sceneTransformTool_ == SceneTransformTool::Rotate) {
                    if (!accumulated.addRotation(
                            event.deltaX, event.deltaY, sceneGizmoScreenOrientationSign_))
                        return accepted();
                } else if (lengthSquared >= 1.0f
                    && (sceneTransformTool_ == SceneTransformTool::Scale
                        || sceneTransformTool_ == SceneTransformTool::Move)) {
                    if (!accumulated.addLinear(event.deltaX, event.deltaY, sceneGizmoScreenAxis_))
                        return accepted();
                } else
                    return accepted();
                double total = 0;
                if (const auto* diagnostic = sceneTransformSnap_.prepare(
                        sceneTransformTool_, accumulated.amount(), total)) {
                    feedback(diagnostic);
                    return accepted();
                }
                if (sceneGizmoBatchEditId_) {
                    auto response = submitCommand(
                        UpdateSceneSelectionTransformEditCommand{sceneGizmoBatchEditId_, total},
                        EditorCommandOrigin::PlatformInput);
                    if (response.status == EditorResponseStatus::Completed)
                        sceneGizmoAccumulator_ = accumulated;
                    return std::move(response).intoNestedOutcome();
                }
                scene::Transform localTransform;
                if (const auto* diagnostic = EditorSceneTransformFrame::prepare(sceneTransformTool_,
                        sceneTransformSpace_,
                        static_cast<unsigned>(sceneGizmoAxis_) - 1,
                        total,
                        sceneGizmoStartLocal_,
                        sceneGizmoParentWorld_,
                        localTransform)) {
                    feedback(diagnostic);
                    return accepted();
                }
                if (EditorSceneTransformFrame::same(localTransform, object->local_transform())) {
                    // A sub-step event is accepted input even when its snapped
                    // pose is unchanged. Keep its raw amount without re-running
                    // normalization or rebuilding the Inspector.
                    sceneGizmoAccumulator_ = accumulated;
                    return accepted();
                }
                SetSceneObjectTransformCommand command;
                command.objectId = sceneGizmoObject_.value();
                command.translation = localTransform.translation;
                command.rotation = localTransform.rotation;
                command.scale = localTransform.scale;
                auto response = submitCommand(command, EditorCommandOrigin::PlatformInput);
                if (response.status == EditorResponseStatus::Completed) {
                    sceneGizmoAccumulator_ = accumulated;
                    sceneGizmoLastLocal_ = object->local_transform();
                }
                return std::move(response).intoNestedOutcome();
            }
        } else if (viewport && sceneViewportGesture_ == SceneViewportGesture::BoxSelect) {
            sceneViewportBoxEndX_ = event.x;
            sceneViewportBoxEndY_ = event.y;
        } else if (viewport) {
            SceneViewportCameraCommand camera;
            camera.operation = sceneViewportGesture_ == SceneViewportGesture::Look
                ? SceneViewportCameraOperation::Look
                : sceneViewportGesture_ == SceneViewportGesture::Orbit
                ? SceneViewportCameraOperation::OrbitAround
                : SceneViewportCameraOperation::Pan;
            camera.deltaX = event.deltaX;
            camera.deltaY = event.deltaY;
            camera.viewportWidth = viewport->rect.width;
            camera.viewportHeight = viewport->rect.height;
            if (camera.operation == SceneViewportCameraOperation::OrbitAround)
                camera.pivot = sceneViewportOrbitPivot_;
            (void)submitCommand(camera, EditorCommandOrigin::PlatformInput);
        }
        return accepted();
    }

    if (!filesPresentationFailed_ && event.kind == Kind::PointerDown && event.primaryButton
        && event.x >= kFileScrollbarX - 2.0f
        && event.x < kFileScrollbarX + kFileScrollbarWidth + 2.0f && event.y >= kFileScrollbarY
        && event.y < kFileScrollbarY + kFileScrollbarHeight) {
        const auto thumb = ui::ScrollbarGeometry::thumb(kFileScrollbarY,
            kFileScrollbarHeight,
            visibleFileEntryCount(),
            kFileSlotCount,
            fileScrollOffset_,
            24.0f);
        if (thumb.visible) {
            fileDraggingScrollbar_ = true;
            fileScrollbarGrabOffset_ = (event.y >= thumb.y && event.y < thumb.y + thumb.height)
                ? event.y - thumb.y
                : thumb.height * 0.5f;
            const float travel = kFileScrollbarHeight - thumb.height;
            const float fraction = travel > 0.0f
                ? std::clamp(
                      (event.y - kFileScrollbarY - fileScrollbarGrabOffset_) / travel, 0.0f, 1.0f)
                : 0.0f;
            fileScrollOffset_ = static_cast<std::size_t>(
                std::lround(fraction * static_cast<float>(thumb.maxOffset)));
            updateFileSlots();
        }
        return accepted();
    }
    if (presentationState_.workspace() == "Script" && event.kind == Kind::PointerDown
        && event.primaryButton) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* tabs = layout.findWidget(id("scene_tabs"));
        if (tabs && event.y >= tabs->rect.y && event.y < tabs->rect.y + tabs->rect.height
            && event.x >= tabs->rect.x && event.x < tabs->rect.x + tabs->rect.width) {
            float tabX = tabs->rect.x + 4.0f;
            const auto& documents = scriptDocuments_.documents();
            for (std::size_t index = 0; index < documents.size(); ++index) {
                const float width = scriptTabWidth(documents[index].path);
                if (event.x >= tabX && event.x < tabX + width) {
                    if (event.x >= tabX + width - 24.0f) {
                        const auto outcome =
                            scriptFilePanel_.requestClose(*this, documents[index].id);
                        scriptPointerSuppressRelease_ = true;
                        platformPointerConsumed_ = true;
                        return outcome;
                    } else
                        activateScriptDocument(index);
                    return accepted();
                }
                tabX += width + 2.0f;
            }
            return accepted();
        }
    }
    if (keyboard && presentationState_.workspace() == "3D")
        return applySceneKeyboardInput(event);
    return presentationState_.workspace() == "Script" ? applyScriptKeyboardInput(event)
                                                      : accepted();
}

} // namespace arp::editor
