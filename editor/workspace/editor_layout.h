#pragma once
#include "core/math/geometry.h"
#include "ui/ui.h"

#include <cstddef>

namespace arp::editor::layout {
constexpr const char* kFileSlots[] = {"folder_door",
    "folder_effects_shared",
    "folder_enemies",
    "folder_level",
    "folder_main",
    "folder_menu",
    "folder_player",
    "asset_0",
    "asset_1",
    "asset_2",
    "asset_3"};
constexpr const char* kFolderSlotNames[] = {
    "door", "effects_shared", "enemies", "level", "main", "menu", "player"};
constexpr std::size_t kFileSlotCount = sizeof(kFileSlots) / sizeof(kFileSlots[0]);
constexpr float kCodeLineHeight = 22.0f;
constexpr float kCodeTextVerticalOffset = 4.0f;
// DengXian's ideograph ink sits a little below the Consolas lowercase
// optical center at the shared baseline. Correct CJK runs for mixed comments.
constexpr float kCodeCjkOpticalVerticalOffset = -4.0f;
// The authored left_files panel ends at x=284; keep the 6px track inside it.
constexpr float kFileScrollbarX = 273.0f;
constexpr float kFileScrollbarY = 726.0f;
constexpr float kFileScrollbarWidth = 6.0f;
constexpr float kFileScrollbarHeight = static_cast<float>(kFileSlotCount) * 24.0f;
constexpr float kScriptScrollbarWidth = 6.0f;
constexpr float kWorkspaceTabsY = 42.0f;
constexpr float kWorkspaceTabsHeight = 35.0f;
constexpr float kScriptEditorHeaderHeight = 30.0f;
constexpr float kSceneHierarchyLeft = 8.0f;
constexpr float kSceneHierarchyTop = 115.0f;
constexpr float kSceneHierarchyWidth = 352.0f;
constexpr float kSceneHierarchyRowHeight = 24.0f;
constexpr float kSceneHierarchyVisibleHeight = 432.0f;
constexpr float kSceneHierarchyTextInset = 12.0f;
constexpr float kSceneHierarchyDepthIndent = 14.0f;
constexpr float kSceneHierarchyScrollbarWidth = 6.0f;
constexpr std::size_t kSceneHierarchyVisibleRows =
    static_cast<std::size_t>(kSceneHierarchyVisibleHeight / kSceneHierarchyRowHeight);

// Reserve action columns inside the actual authored panel, not the nominal
// hierarchy width. The same clipped geometry drives drawing and input.
inline ui::LayoutRect sceneHierarchyClip(const ui::CanvasLayoutResult& layout, ui::WidgetId panel)
{
    const auto* widget = layout.findWidget(panel);
    if (!layout.ok() || !widget || !widget->visibleInHierarchy || !widget->enabledInHierarchy)
        return {};
    const auto intersect = [](ui::LayoutRect a, ui::LayoutRect b) {
        const auto rect = core::intersectRect(core::Rect2<float>{a.x, a.y, a.width, a.height},
            core::Rect2<float>{b.x, b.y, b.width, b.height});
        return ui::LayoutRect::fromOriginSize(rect.x, rect.y, rect.width, rect.height);
    };
    return intersect(intersect(widget->rect, layout.clipRect(panel)),
        ui::LayoutRect::fromOriginSize(kSceneHierarchyLeft,
            kSceneHierarchyTop,
            kSceneHierarchyWidth,
            kSceneHierarchyVisibleHeight));
}

enum class SceneHierarchyAction { Visibility, Lock };
constexpr float kSceneHierarchyActionWidth = 24.f;

inline ui::LayoutRect sceneHierarchyActionRect(
    ui::LayoutRect clip, std::size_t row, SceneHierarchyAction action)
{
    const auto columns = action == SceneHierarchyAction::Visibility ? 2.f : 1.f;
    return ui::LayoutRect::fromOriginSize(
        clip.x + clip.width - kSceneHierarchyScrollbarWidth - columns * kSceneHierarchyActionWidth,
        kSceneHierarchyTop + static_cast<float>(row) * kSceneHierarchyRowHeight,
        kSceneHierarchyActionWidth,
        kSceneHierarchyRowHeight);
}

// Shared by presentation and pointer admission; the painted icon is the object
// reference handle. Text/disclosure keep their existing selection/reparent use.
inline ui::LayoutRect sceneHierarchyIconRect(std::size_t row, std::size_t depth)
{
    const auto boundedDepth = depth < 12u ? depth : 12u;
    return ui::LayoutRect::fromOriginSize(kSceneHierarchyLeft + kSceneHierarchyTextInset
            + static_cast<float>(boundedDepth) * kSceneHierarchyDepthIndent + 3.0f,
        kSceneHierarchyTop + static_cast<float>(row) * kSceneHierarchyRowHeight + 4.0f,
        16.0f,
        16.0f);
}

constexpr std::size_t sceneHierarchyMaxScrollOffset(std::size_t contentRows)
{
    return contentRows > kSceneHierarchyVisibleRows ? contentRows - kSceneHierarchyVisibleRows : 0u;
}
} // namespace arp::editor::layout
