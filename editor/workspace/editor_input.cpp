#include <cwchar>
#include <limits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "core/utf8_cursor.h"
#include "editor/ingress/platform_input_adapter.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"
#include "ui/scrollbar_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace arp::editor {
using namespace layout;

namespace {
bool writeSystemClipboardText(std::string_view utf8)
{
#if defined(_WIN32)
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength < 0 || (!utf8.empty() && wideLength == 0))
        return false;
    core::pmr::Vector<wchar_t> wide(
        static_cast<std::size_t>(wideLength), L'\0', &core::defaultCpuMemoryResource());
    if (wideLength > 0
        && MultiByteToWideChar(CP_UTF8,
               MB_ERR_INVALID_CHARS,
               utf8.data(),
               static_cast<int>(utf8.size()),
               wide.data(),
               wideLength)
            != wideLength)
        return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (!memory)
        return false;
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    if (!target) {
        GlobalFree(memory);
        return false;
    }
    if (!wide.empty())
        std::memcpy(target, wide.data(), wide.size() * sizeof(wchar_t));
    target[wide.size()] = L'\0';
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return false;
    }
    const bool ok =
        EmptyClipboard() != FALSE && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    if (!ok)
        GlobalFree(memory);
    return ok;
#else
    (void)utf8;
    return false;
#endif
}

bool readSystemClipboardText(core::pmr::String& utf8)
{
#if defined(_WIN32)
    if (!OpenClipboard(nullptr))
        return false;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return false;
    }
    const auto* source = static_cast<const wchar_t*>(GlobalLock(data));
    if (!source) {
        CloseClipboard();
        return false;
    }
    const auto wideLength = std::wcslen(source);
    bool ok = wideLength <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    int bytes = 0;
    if (ok && wideLength > 0) {
        bytes = WideCharToMultiByte(CP_UTF8,
            WC_ERR_INVALID_CHARS,
            source,
            static_cast<int>(wideLength),
            nullptr,
            0,
            nullptr,
            nullptr);
        ok = bytes > 0;
    }
    if (ok) {
        utf8.resize(static_cast<std::size_t>(bytes));
        if (bytes > 0)
            ok = WideCharToMultiByte(CP_UTF8,
                     WC_ERR_INVALID_CHARS,
                     source,
                     static_cast<int>(wideLength),
                     utf8.data(),
                     bytes,
                     nullptr,
                     nullptr)
                == bytes;
        if (!ok)
            utf8.clear();
    }
    GlobalUnlock(data);
    CloseClipboard();
    return ok;
#else
    (void)utf8;
    return false;
#endif
}

} // namespace

void EditorShell::handlePlatformInput(const platform::WindowInputEvent& event)
{
    (void)submitCommand(normalizePlatformInput(event), EditorCommandOrigin::PlatformInput);
}

void EditorShell::applyPlatformInput(const EditorInputCommand& event)
{
    using Kind = EditorInputKind;
    if (event.kind == Kind::FocusLost) {
        if (activeTransformEditObjectId_ != 0u)
            (void)submitCommand(EndSceneTransformEditCommand{activeTransformEditObjectId_},
                EditorCommandOrigin::PlatformInput);
        sceneGizmoObject_ = scene::GameObjectId::invalid();
        controlHeld_ = shiftHeld_ = scriptDraggingSelection_ = false;
        sceneViewportRightButtonHeld_ = false;
        sceneViewportGesture_ = SceneViewportGesture::None;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        hierarchyDragObject_ = scene::GameObjectId::invalid();
        hierarchyDragTarget_ = scene::GameObjectId::invalid();
        hierarchyDragging_ = false;
        sceneHierarchyDraggingScrollbar_ = false;
        return;
    }
    if (event.kind == Kind::PointerUp && event.button == EditorPointerButton::Right) {
        sceneViewportRightButtonHeld_ = false;
        if (sceneViewportGesture_ == SceneViewportGesture::Look)
            sceneViewportGesture_ = SceneViewportGesture::None;
        return;
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
                if (shiftHeld_) {
                    selection.objectIds.push_back(sceneDocument_.selectedObject().value());
                    selection.primaryObjectId = sceneDocument_.selectedObject().value();
                    for (const auto selected : sceneViewportAdditionalSelection_)
                        selection.objectIds.push_back(selected.value());
                }
                for (const auto& object : sceneDocument_.scene().objects()) {
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
        if (activeTransformEditObjectId_ != 0u)
            (void)submitCommand(EndSceneTransformEditCommand{activeTransformEditObjectId_},
                EditorCommandOrigin::PlatformInput);
        sceneGizmoObject_ = scene::GameObjectId::invalid();
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
        return;
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
        return;
    }
    if (event.kind == Kind::PointerUp
        && (event.button == EditorPointerButton::Middle
            || event.button == EditorPointerButton::Right)) {
        sceneViewportGesture_ = SceneViewportGesture::None;
        return;
    }
    if (event.kind == Kind::PointerUp && event.primaryButton) {
        fileDraggingScrollbar_ = false;
        scriptDraggingScrollbar_ = false;
        scriptDraggingSelection_ = false;
        return;
    }
    if (event.kind == Kind::PointerDelta && hierarchyDragObject_.valid()) {
        const float dx = event.x - hierarchyDragStartX_;
        const float dy = event.y - hierarchyDragStartY_;
        if (!hierarchyDragging_ && dx * dx + dy * dy >= 25.0f)
            hierarchyDragging_ = true;
        if (hierarchyDragging_ && event.x >= layout::kSceneHierarchyLeft
            && event.x < layout::kSceneHierarchyLeft + layout::kSceneHierarchyWidth
            && event.y >= layout::kSceneHierarchyTop
            && event.y < layout::kSceneHierarchyTop + layout::kSceneHierarchyVisibleHeight) {
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
        return;
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
        return;
    }
    if (event.kind == Kind::PointerMoved && scriptDraggingScrollbar_
        && presentationState_.workspace() == "Script") {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport) {
            const float trackY = viewport->rect.y + 34.0f,
                        trackHeight = viewport->rect.height - 58.0f;
            const auto visible =
                static_cast<std::size_t>((viewport->rect.height - 42.0f) / kCodeLineHeight);
            const auto thumb = ui::ScrollbarGeometry::thumb(trackY,
                trackHeight,
                scriptLineCount(scriptTextView()),
                visible,
                scriptDocuments_.activeScrollLine(),
                32.0f);
            if (thumb.visible) {
                const float travel = trackHeight - thumb.height;
                const float fraction = travel > 0.0f
                    ? std::clamp(
                          (event.y - trackY - scriptScrollbarGrabOffset_) / travel, 0.0f, 1.0f)
                    : 0.0f;
                scriptDocuments_.setActiveScrollLine(static_cast<std::size_t>(
                    std::lround(fraction * static_cast<float>(thumb.maxOffset))));
            }
        }
        return;
    }
    if (event.kind == Kind::Wheel) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        const bool inSceneHierarchy = presentationState_.workspace() == "3D"
            && sceneDockTab_ == "Scene" && event.x >= layout::kSceneHierarchyLeft
            && event.x < layout::kSceneHierarchyLeft + layout::kSceneHierarchyWidth
            && event.y >= layout::kSceneHierarchyTop
            && event.y < layout::kSceneHierarchyTop + layout::kSceneHierarchyVisibleHeight;
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
        } else if (presentationState_.workspace() == "Script" && viewport
            && event.x >= viewport->rect.x && event.x < viewport->rect.x + viewport->rect.width
            && event.y >= viewport->rect.y && event.y < viewport->rect.y + viewport->rect.height) {
            const auto delta = static_cast<int>(std::lround(event.wheelDelta * 3.0f));
            const auto visible =
                static_cast<std::size_t>((viewport->rect.height - 42.0f) / kCodeLineHeight);
            const auto maxOffset = scriptLineCount(scriptTextView()) > visible
                ? scriptLineCount(scriptTextView()) - visible
                : 0u;
            if (delta > 0)
                scriptDocuments_.setActiveScrollLine(
                    scriptDocuments_.activeScrollLine() > static_cast<std::size_t>(delta)
                        ? scriptDocuments_.activeScrollLine() - static_cast<std::size_t>(delta)
                        : 0);
            else if (delta < 0)
                scriptDocuments_.setActiveScrollLine(std::min(maxOffset,
                    scriptDocuments_.activeScrollLine() + static_cast<std::size_t>(-delta)));
        } else if (event.x < 285.0f && event.y >= 700.0f && event.y < 1034.0f) {
            const auto delta = static_cast<int>(std::lround(event.wheelDelta * 3.0f));
            if (delta > 0)
                fileScrollOffset_ = fileScrollOffset_ > static_cast<std::size_t>(delta)
                    ? fileScrollOffset_ - static_cast<std::size_t>(delta)
                    : 0;
            else if (delta < 0)
                fileScrollOffset_ += static_cast<std::size_t>(-delta);
            updateFileSlots();
        }
        return;
    }
    if (event.kind == Kind::KeyUp) {
        if (event.key == EditorInputKey::LeftControl || event.key == EditorInputKey::RightControl)
            controlHeld_ = false;
        if (event.key == EditorInputKey::LeftShift || event.key == EditorInputKey::RightShift)
            shiftHeld_ = false;
        return;
    }
    if (event.kind == Kind::KeyDown
        && (event.key == EditorInputKey::LeftControl
            || event.key == EditorInputKey::RightControl)) {
        controlHeld_ = true;
        return;
    }
    if (event.kind == Kind::KeyDown
        && (event.key == EditorInputKey::LeftShift || event.key == EditorInputKey::RightShift)) {
        shiftHeld_ = true;
        return;
    }

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
        return;
    }
    if (presentationState_.workspace() == "3D" && event.kind == Kind::PointerDown
        && event.primaryButton) {
        if (event.x >= layout::kSceneHierarchyLeft
            && event.x < layout::kSceneHierarchyLeft + layout::kSceneHierarchyWidth
            && event.y >= layout::kSceneHierarchyTop
            && event.y < layout::kSceneHierarchyTop + layout::kSceneHierarchyVisibleHeight) {
            const auto hierarchy = visibleSceneHierarchyEntries();
            const auto maxOffset = layout::sceneHierarchyMaxScrollOffset(hierarchy.size());
            sceneHierarchyScrollOffset_ = std::min(sceneHierarchyScrollOffset_, maxOffset);
            const float scrollbarX = layout::kSceneHierarchyLeft + layout::kSceneHierarchyWidth
                - layout::kSceneHierarchyScrollbarWidth;
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
                return;
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
                    return;
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
            return;
        }
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport && event.x >= viewport->rect.x
            && event.x < viewport->rect.x + viewport->rect.width && event.y >= viewport->rect.y
            && event.y < viewport->rect.y + viewport->rect.height) {
            const auto selectedId = sceneDocument_.selectedObject();
            const auto* selectedObject = sceneDocument_.scene().findObject(selectedId);
            if (selectedObject) {
                sceneGizmoAxis_ = SceneGizmoAxis::None;
                const auto origin = selectedObject->world_transform().translation;
                constexpr core::Vec3 axes[] = {
                    {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
                constexpr core::Vec3 planeU[] = {
                    {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
                constexpr core::Vec3 planeV[] = {
                    {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
                if (sceneTransformTool_ == SceneTransformTool::Rotate) {
                    float centerX = 0.0f, centerY = 0.0f;
                    if (sceneViewportCamera_.project(origin,
                            viewport->rect.width,
                            viewport->rect.height,
                            centerX,
                            centerY)) {
                        centerX += viewport->rect.x;
                        centerY += viewport->rect.y;
                        constexpr float radius = 1.25f;
                        constexpr unsigned segments = 48u;
                        float bestDistance = 9.0f;
                        for (std::size_t axisIndex = 0; axisIndex < 3u; ++axisIndex) {
                            float priorX = 0.0f, priorY = 0.0f;
                            bool priorValid = false;
                            float orientationSign = 1.0f;
                            float ux = 0.0f, uy = 0.0f, vx = 0.0f, vy = 0.0f;
                            const bool basisValid =
                                sceneViewportCamera_.project(origin + planeU[axisIndex],
                                    viewport->rect.width,
                                    viewport->rect.height,
                                    ux,
                                    uy)
                                && sceneViewportCamera_.project(origin + planeV[axisIndex],
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
                                if (std::abs(determinant) > 0.001f)
                                    orientationSign = determinant < 0.0f ? -1.0f : 1.0f;
                            }
                            for (unsigned segment = 0u; segment <= segments; ++segment) {
                                const float angle = 6.283185307179586f
                                    * static_cast<float>(segment % segments)
                                    / static_cast<float>(segments);
                                const auto point = origin
                                    + core::scale(planeU[axisIndex], radius * std::cos(angle))
                                    + core::scale(planeV[axisIndex], radius * std::sin(angle));
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
                                            sceneGizmoWorldAxis_ = axes[axisIndex];
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
                            sceneGizmoObject_ = selectedId;
                            sceneGizmoStartTransform_ = selectedObject->world_transform();
                            sceneGizmoLastPointer_ = {event.x - centerX, event.y - centerY, 0.0f};
                            sceneViewportGesture_ = SceneViewportGesture::TransformAxis;
                            (void)submitCommand(BeginSceneTransformEditCommand{selectedId.value()},
                                EditorCommandOrigin::PlatformInput);
                            return;
                        }
                    }
                } else if (sceneTransformTool_ == SceneTransformTool::Move
                    || sceneTransformTool_ == SceneTransformTool::Scale) {
                    float bestDistance = 9.0f;
                    for (std::size_t axisIndex = 0; axisIndex < 3u; ++axisIndex) {
                        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
                        if (!sceneViewportCamera_.project(
                                origin, viewport->rect.width, viewport->rect.height, x0, y0)
                            || !sceneViewportCamera_.project(origin + axes[axisIndex],
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
                            sceneGizmoWorldAxis_ = axes[axisIndex];
                            sceneGizmoScreenAxis_ = {sx, sy, 0.0f};
                        }
                    }
                    if (sceneGizmoAxis_ != SceneGizmoAxis::None) {
                        sceneGizmoObject_ = selectedId;
                        sceneGizmoStartTransform_ = selectedObject->world_transform();
                        sceneViewportGesture_ = SceneViewportGesture::TransformAxis;
                        (void)submitCommand(BeginSceneTransformEditCommand{selectedId.value()},
                            EditorCommandOrigin::PlatformInput);
                        return;
                    }
                }
            }
            if (!shiftHeld_) {
                scene::GameObjectId nearest = scene::GameObjectId::invalid();
                float nearestDistance = 14.0f;
                for (const auto& object : sceneDocument_.scene().objects()) {
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
            return;
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
                shiftHeld_ ? SceneViewportGesture::Pan : SceneViewportGesture::Orbit;
            if (!shiftHeld_) {
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
        return;
    }
    if (presentationState_.workspace() == "3D" && event.kind == Kind::PointerDelta
        && sceneViewportGesture_ != SceneViewportGesture::None) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport && sceneViewportGesture_ == SceneViewportGesture::TransformAxis) {
            const float lengthSquared = sceneGizmoScreenAxis_.x * sceneGizmoScreenAxis_.x
                + sceneGizmoScreenAxis_.y * sceneGizmoScreenAxis_.y;
            const auto* object = sceneDocument_.scene().findObject(sceneGizmoObject_);
            if (object
                && (sceneTransformTool_ == SceneTransformTool::Rotate || lengthSquared >= 1.0f)) {
                auto transform = sceneGizmoStartTransform_;
                if (sceneTransformTool_ == SceneTransformTool::Rotate) {
                    const core::Vec3 nextPointer = {sceneGizmoLastPointer_.x + event.deltaX,
                        sceneGizmoLastPointer_.y + event.deltaY,
                        0.0f};
                    const float cross = sceneGizmoLastPointer_.x * nextPointer.y
                        - sceneGizmoLastPointer_.y * nextPointer.x;
                    const float dot = sceneGizmoLastPointer_.x * nextPointer.x
                        + sceneGizmoLastPointer_.y * nextPointer.y;
                    if (std::isfinite(cross) && std::isfinite(dot)
                        && (std::abs(cross) + std::abs(dot) > 0.0001f)) {
                        const float angle =
                            std::atan2(cross, dot) * sceneGizmoScreenOrientationSign_;
                        transform.rotation =
                            core::normalize(core::Quat::fromAxisAngle(sceneGizmoWorldAxis_, angle)
                                * sceneGizmoStartTransform_.rotation);
                        sceneGizmoLastPointer_ = nextPointer;
                    } else
                        return;
                } else if (lengthSquared >= 1.0f
                    && sceneTransformTool_ == SceneTransformTool::Move) {
                    const float worldDelta = (event.deltaX * sceneGizmoScreenAxis_.x
                                                 + event.deltaY * sceneGizmoScreenAxis_.y)
                        / lengthSquared;
                    transform.translation =
                        transform.translation + core::scale(sceneGizmoWorldAxis_, worldDelta);
                } else if (lengthSquared >= 1.0f
                    && sceneTransformTool_ == SceneTransformTool::Scale) {
                    const float worldDelta = (event.deltaX * sceneGizmoScreenAxis_.x
                                                 + event.deltaY * sceneGizmoScreenAxis_.y)
                        / lengthSquared;
                    if (sceneGizmoAxis_ == SceneGizmoAxis::X)
                        transform.scale.x += worldDelta;
                    else if (sceneGizmoAxis_ == SceneGizmoAxis::Y)
                        transform.scale.y += worldDelta;
                    else if (sceneGizmoAxis_ == SceneGizmoAxis::Z)
                        transform.scale.z += worldDelta;
                } else
                    return;
                const auto* parent = object->parent_object();
                const auto parentWorld =
                    parent ? parent->world_transform() : scene::Transform::identity();
                constexpr float minimumInvertibleScale = 0.000001f;
                if (!std::isfinite(parentWorld.scale.x) || !std::isfinite(parentWorld.scale.y)
                    || !std::isfinite(parentWorld.scale.z)
                    || std::abs(parentWorld.scale.x) <= minimumInvertibleScale
                    || std::abs(parentWorld.scale.y) <= minimumInvertibleScale
                    || std::abs(parentWorld.scale.z) <= minimumInvertibleScale)
                    return;
                const auto localTransform = scene::deriveLocalTransform(parentWorld, transform);
                SetSceneObjectTransformCommand command;
                command.objectId = sceneGizmoObject_.value();
                command.translation = localTransform.translation;
                command.rotation = localTransform.rotation;
                command.scale = localTransform.scale;
                (void)submitCommand(command, EditorCommandOrigin::PlatformInput);
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
        return;
    }

    if (event.kind == Kind::PointerDown && event.primaryButton && event.x >= kFileScrollbarX - 2.0f
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
        return;
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
                    if (event.x >= tabX + width - 24.0f)
                        closeScriptDocument(index);
                    else
                        activateScriptDocument(index);
                    return;
                }
                tabX += width + 2.0f;
            }
            return;
        }
    }
    if (presentationState_.workspace() == "Script" && event.kind == Kind::PointerDown
        && event.primaryButton) {
        const auto layout = runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(id("viewport"));
        if (viewport) {
            const float trackX =
                viewport->rect.x + viewport->rect.width - kScriptScrollbarWidth - 2.0f;
            const float trackY = viewport->rect.y + 34.0f,
                        trackHeight = viewport->rect.height - 58.0f;
            if (event.x >= trackX && event.x < trackX + kScriptScrollbarWidth && event.y >= trackY
                && event.y < trackY + trackHeight) {
                const auto visible =
                    static_cast<std::size_t>((viewport->rect.height - 42.0f) / kCodeLineHeight);
                const auto thumb = ui::ScrollbarGeometry::thumb(trackY,
                    trackHeight,
                    scriptLineCount(scriptTextView()),
                    visible,
                    scriptDocuments_.activeScrollLine(),
                    32.0f);
                if (thumb.visible) {
                    scriptFocused_ = true;
                    scriptDraggingScrollbar_ = true;
                    scriptScrollbarGrabOffset_ =
                        (event.y >= thumb.y && event.y < thumb.y + thumb.height)
                        ? event.y - thumb.y
                        : thumb.height * 0.5f;
                    const float travel = trackHeight - thumb.height;
                    const float fraction = travel > 0.0f
                        ? std::clamp(
                              (event.y - trackY - scriptScrollbarGrabOffset_) / travel, 0.0f, 1.0f)
                        : 0.0f;
                    scriptDocuments_.setActiveScrollLine(static_cast<std::size_t>(
                        std::lround(fraction * static_cast<float>(thumb.maxOffset))));
                }
                return;
            }
        }
    }
    if (presentationState_.workspace() == "Script" && event.kind == Kind::PointerDown
        && event.primaryButton && [&]() {
               const auto layout = runtime().canvas().resolveLayout();
               const auto* viewport = layout.findWidget(id("viewport"));
               return viewport && event.x >= viewport->rect.x + 54.0f
                   && event.x < viewport->rect.x + viewport->rect.width - 8.0f
                   && event.y >= viewport->rect.y + kScriptEditorHeaderHeight + 4.0f
                   && event.y < viewport->rect.y + viewport->rect.height - 24.0f;
           }()) {
        scriptFocused_ = true;
        auto offset = scriptOffsetAt(event.x, event.y);
        const auto now = std::chrono::steady_clock::now();
        const bool doubleClick = now - lastEditorClickTime_ < std::chrono::milliseconds(450)
            && (offset == lastScriptClickOffset_
                || offset == core::Utf8Cursor::next(scriptTextView(), lastScriptClickOffset_)
                || core::Utf8Cursor::next(scriptTextView(), offset) == lastScriptClickOffset_);
        lastEditorClickTime_ = now;
        lastScriptClickOffset_ = offset;
        if (doubleClick && !scriptTextView().empty()) {
            std::size_t probe = offset;
            if (probe == scriptTextView().size()
                || (probe < scriptTextView().size() && scriptTextView()[probe] == '\n')) {
                if (probe > 0)
                    probe = core::Utf8Cursor::previous(scriptTextView(), probe);
            }
            auto isWordByte = [&](std::size_t at) {
                if (at >= scriptTextView().size() || scriptTextView()[at] == '\n')
                    return false;
                const auto ch = static_cast<unsigned char>(scriptTextView()[at]);
                return ch >= 0x80u || std::isalnum(ch) || ch == '_';
            };
            if (isWordByte(probe)) {
                auto begin = probe, end = core::Utf8Cursor::next(scriptTextView(), probe);
                while (begin > 0) {
                    const auto prev = core::Utf8Cursor::previous(scriptTextView(), begin);
                    if (!isWordByte(prev))
                        break;
                    begin = prev;
                }
                while (end < scriptTextView().size() && isWordByte(end))
                    end = core::Utf8Cursor::next(scriptTextView(), end);
                scriptDocuments_.setActiveSelection(end, begin);
            } else {
                scriptDocuments_.setActiveSelection(offset, offset);
            }
            scriptDraggingSelection_ = false;
        } else {
            moveScriptCursor(offset, shiftHeld_);
            scriptDraggingSelection_ = true;
        }
        return;
    }
    if (event.kind == Kind::PointerUp && event.primaryButton) {
        scriptDraggingSelection_ = false;
        return;
    }
    if (event.kind == Kind::PointerMoved && presentationState_.workspace() == "Script"
        && scriptDraggingSelection_) {
        moveScriptCursor(scriptOffsetAt(event.x, event.y), true);
        return;
    }
    if (event.kind == Kind::KeyDown && controlHeld_ && presentationState_.workspace() == "3D"
        && event.key == EditorInputKey::Z && !shiftHeld_) {
        (void)submitCommand(UndoSceneCommand{}, EditorCommandOrigin::PlatformInput);
        return;
    }
    if (event.kind == Kind::KeyDown && controlHeld_ && presentationState_.workspace() == "3D"
        && (event.key == EditorInputKey::Y || (event.key == EditorInputKey::Z && shiftHeld_))) {
        (void)submitCommand(RedoSceneCommand{}, EditorCommandOrigin::PlatformInput);
        return;
    }
    if (event.kind == Kind::KeyDown && sceneViewportRightButtonHeld_
        && presentationState_.workspace() == "3D") {
        float horizontal = 0.0f, forward = 0.0f;
        if (event.key == EditorInputKey::A)
            horizontal = -1.0f;
        else if (event.key == EditorInputKey::D)
            horizontal = 1.0f;
        else if (event.key == EditorInputKey::W)
            forward = 1.0f;
        else if (event.key == EditorInputKey::S)
            forward = -1.0f;
        if (horizontal != 0.0f || forward != 0.0f) {
            const auto layout = runtime().canvas().resolveLayout();
            const auto* viewport = layout.findWidget(id("viewport"));
            if (viewport) {
                SceneViewportCameraCommand camera;
                camera.operation = SceneViewportCameraOperation::Move;
                camera.deltaX = horizontal;
                camera.deltaY = forward;
                camera.viewportWidth = viewport->rect.width;
                camera.viewportHeight = viewport->rect.height;
                (void)submitCommand(camera, EditorCommandOrigin::PlatformInput);
            }
            return;
        }
    }
    if (event.kind == Kind::KeyDown && !controlHeld_ && presentationState_.workspace() == "3D") {
        const auto focusedId = runtime().focusedControl();
        const auto* focused =
            focusedId.valid() ? runtime().canvas().findWidget(focusedId) : nullptr;
        const bool textEntryFocused = focused
            && (focused->desc().controlType == ui::UiControlType::TextInput
                || focused->desc().controlType == ui::UiControlType::NumericInput);
        if (!textEntryFocused) {
            const char* toolControl = event.key == EditorInputKey::Q ? "tool_4"
                : event.key == EditorInputKey::W                     ? "tool_1"
                : event.key == EditorInputKey::E                     ? "tool_2"
                : event.key == EditorInputKey::R                     ? "tool_3"
                                                                     : nullptr;
            if (toolControl) {
                activate(toolControl);
                return;
            }
        }
    }
    if (!scriptEditorCapturesKeyboard())
        return;
    if (event.kind == Kind::Text) {
        if (!controlHeld_)
            insertScriptText(event.text.data(), event.text.size());
        return;
    }
    if (event.kind != Kind::KeyDown)
        return;
    if (controlHeld_ && event.key == EditorInputKey::S) {
        saveScript();
        return;
    }
    if (controlHeld_ && event.key == EditorInputKey::Z && !shiftHeld_) {
        (void)submitCommand(
            UndoScriptCommand{activeScriptDocumentId()}, EditorCommandOrigin::PlatformInput);
        return;
    }
    if (controlHeld_
        && (event.key == EditorInputKey::Y || (shiftHeld_ && event.key == EditorInputKey::Z))) {
        (void)submitCommand(
            RedoScriptCommand{activeScriptDocumentId()}, EditorCommandOrigin::PlatformInput);
        return;
    }
    if (controlHeld_ && event.key == EditorInputKey::A) {
        scriptDocuments_.setActiveSelection(scriptTextView().size(), 0);
        return;
    }
    if (controlHeld_ && event.key == EditorInputKey::C) {
        if (scriptSelectionStart() != scriptSelectionEnd())
            writeSystemClipboardText(
                std::string_view(scriptTextView().data() + scriptSelectionStart(),
                    scriptSelectionEnd() - scriptSelectionStart()));
        return;
    }
    if (controlHeld_ && event.key == EditorInputKey::X) {
        if (scriptSelectionStart() != scriptSelectionEnd()
            && writeSystemClipboardText(
                std::string_view(scriptTextView().data() + scriptSelectionStart(),
                    scriptSelectionEnd() - scriptSelectionStart())))
            replaceScriptRange(
                scriptSelectionStart(), scriptSelectionEnd() - scriptSelectionStart(), "", 0);
        return;
    }
    if (controlHeld_ && event.key == EditorInputKey::V) {
        core::pmr::String pasted(&core::defaultCpuMemoryResource());
        if (!readSystemClipboardText(pasted))
            return;
        core::pmr::String normalized(&core::defaultCpuMemoryResource());
        normalized.reserve(pasted.size());
        for (std::size_t i = 0; i < pasted.size(); ++i) {
            if (pasted[i] == '\r') {
                if (i + 1 < pasted.size() && pasted[i + 1] == '\n')
                    ++i;
                normalized.push_back('\n');
            } else
                normalized.push_back(pasted[i]);
        }
        insertScriptText(normalized.data(), normalized.size());
        return;
    }
    if (event.key == EditorInputKey::Backspace) {
        if (scriptSelectionStart() != scriptSelectionEnd())
            replaceScriptRange(
                scriptSelectionStart(), scriptSelectionEnd() - scriptSelectionStart(), "", 0);
        else if (scriptDocuments_.activeCursor() > 0) {
            const auto cursor = scriptDocuments_.activeCursor();
            const auto start = core::Utf8Cursor::previous(scriptTextView(), cursor);
            replaceScriptRange(start, cursor - start, "", 0);
        }
        return;
    }
    if (event.key == EditorInputKey::Delete) {
        if (scriptSelectionStart() != scriptSelectionEnd())
            replaceScriptRange(
                scriptSelectionStart(), scriptSelectionEnd() - scriptSelectionStart(), "", 0);
        else if (scriptDocuments_.activeCursor() < scriptTextView().size()) {
            const auto cursor = scriptDocuments_.activeCursor();
            const auto end = core::Utf8Cursor::next(scriptTextView(), cursor);
            replaceScriptRange(cursor, end - cursor, "", 0);
        }
        return;
    }
    if (event.key == EditorInputKey::Enter || event.key == EditorInputKey::NumpadEnter) {
        insertScriptText("\n", 1);
        return;
    }
    if (event.key == EditorInputKey::Tab) {
        insertScriptText("    ", 4);
        return;
    }
    if (event.key == EditorInputKey::ArrowLeft) {
        auto target = scriptDocuments_.activeCursor();
        if (!shiftHeld_ && scriptSelectionStart() != scriptSelectionEnd())
            target = scriptSelectionStart();
        else
            target = core::Utf8Cursor::previous(scriptTextView(), target);
        moveScriptCursor(target, shiftHeld_);
        return;
    }
    if (event.key == EditorInputKey::ArrowRight) {
        auto target = scriptDocuments_.activeCursor();
        if (!shiftHeld_ && scriptSelectionStart() != scriptSelectionEnd())
            target = scriptSelectionEnd();
        else
            target = core::Utf8Cursor::next(scriptTextView(), target);
        moveScriptCursor(target, shiftHeld_);
        return;
    }
    if (event.key == EditorInputKey::Home || event.key == EditorInputKey::End) {
        const auto cursor = scriptDocuments_.activeCursor();
        const auto previous = scriptTextView().rfind('\n', cursor == 0 ? 0 : cursor - 1);
        const auto lineStart = previous == core::pmr::String::npos ? 0 : previous + 1;
        const auto next = scriptTextView().find('\n', cursor);
        const auto lineEnd = next == core::pmr::String::npos ? scriptTextView().size() : next;
        moveScriptCursor(event.key == EditorInputKey::Home ? lineStart : lineEnd, shiftHeld_);
        return;
    }
    if (event.key == EditorInputKey::ArrowUp || event.key == EditorInputKey::ArrowDown) {
        moveScriptCursorVertical(event.key == EditorInputKey::ArrowUp ? -1 : 1);
        return;
    }
    if (event.key == EditorInputKey::PageUp || event.key == EditorInputKey::PageDown) {
        for (int i = 0; i < 30; ++i)
            moveScriptCursorVertical(event.key == EditorInputKey::PageUp ? -1 : 1);
        return;
    }
}

} // namespace arp::editor
