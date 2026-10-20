#include "3d/components.h"
#include "core/filesystem_path.h"
#include "core/string_utils.h"
#include "core/utf8_cursor.h"
#include "editor/ui/editor_atlas_layout.h"
#include "editor/viewport/editor_scene_component_gizmos.h"
#include "editor/viewport/editor_scene_viewport_physics_overlay.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_shell.h"
#include "resource/mesh_asset_resources.h"
#include "ui/scrollbar_geometry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>

namespace arp::editor {
using namespace layout;

namespace {
struct Palette {
    const char* name;
    unsigned rgb;
    float alpha;
};

constexpr Palette palette[] = {{"root", 0x181818, 1},
    {"top", 0x131313, 1},
    {"panel", 0x282828, 1},
    {"section", 0x373737, 1},
    {"input", 0x1f1f1f, 1},
    {"button", 0x303030, 1},
    {"selected", 0x454545, 1},
    {"tab", 0x303030, 1},
    {"plain", 0x282828, 0},
    {"text", 0xbdbdbd, 1},
    {"muted", 0x999999, 1},
    {"axis-x", 0xe06c75, 1},
    {"axis-y", 0x78c58a, 1},
    {"axis-z", 0x65a8e5, 1},
    {"green", 0x7cb45b, 1},
    {"image", 0xffffff, 1},
    {"scroll", 0x686868, 1},
    {"mask", 0x4878b2, 1},
    {"checkbox", 0x414141, 1},
    {"slider-thumb", 0xc4c4c4, 1},
    {"code-default", 0xd4d4d4, 1},
    {"code-keyword", 0x569cd6, 1},
    {"code-string", 0xce9178, 1},
    {"code-comment", 0x6a9955, 1},
    {"code-number", 0xb5cea8, 1},
    {"code-function", 0xdcdcaa, 1},
    {"code-type", 0x4ec9b0, 1},
    {"code-decorator", 0xc586c0, 1},
    {"code-line", 0x858585, 1},
    {"code-background", 0x1e1e1e, 1},
    {"code-gutter", 0x252526, 1},
    {"code-current", 0x282828, 1},
    {"viewport_overlay", 0x262626, 0.88f},
    {"transparent", 0x282828, 0},
    {"x", 0xa65b62, 1},
    {"y", 0x82a351, 1},
    {"z", 0x6791bb, 1}};

void color(renderer::ui::UiDrawCommand& c, unsigned rgb, float alpha = 1)
{
    c.color[0] = float((rgb >> 16) & 255) / 255;
    c.color[1] = float((rgb >> 8) & 255) / 255;
    c.color[2] = float(rgb & 255) / 255;
    c.color[3] = alpha;
}

bool scriptKeyword(std::string_view word)
{
    constexpr std::string_view words[] = {"and",
        "as",
        "assert",
        "async",
        "await",
        "break",
        "class",
        "continue",
        "def",
        "elif",
        "else",
        "except",
        "False",
        "finally",
        "for",
        "from",
        "if",
        "import",
        "in",
        "is",
        "lambda",
        "not",
        "or",
        "pass",
        "raise",
        "return",
        "self",
        "try",
        "var",
        "while",
        "with",
        "yield",
        "true",
        "false",
        "null",
        "None",
        "True",
        "extends",
        "match",
        "case"};
    return std::find(std::begin(words), std::end(words), word) != std::end(words);
}

bool scriptBuiltinType(std::string_view word)
{
    constexpr std::string_view types[] = {"any",
        "bool",
        "int",
        "float",
        "str",
        "bytes",
        "object",
        "type",
        "list",
        "dict",
        "tuple",
        "set",
        "frozenset",
        "complex",
        "range"};
    return std::find(std::begin(types), std::end(types), word) != std::end(types);
}

bool scriptTypeAnnotationContext(std::string_view line, std::size_t start)
{
    auto prefix = line.substr(0, start);
    while (!prefix.empty() && (prefix.back() == ' ' || prefix.back() == '\t'))
        prefix.remove_suffix(1);
    return (!prefix.empty() && prefix.back() == ':')
        || (prefix.size() >= 2 && prefix.substr(prefix.size() - 2) == "->");
}

bool scriptLooksLikeTypeName(std::string_view word)
{
    return !word.empty() && std::isupper(static_cast<unsigned char>(word.front())) != 0;
}

const char* scriptTokenStyle(std::string_view line, std::size_t start, std::size_t& end)
{
    const unsigned char first = static_cast<unsigned char>(line[start]);
    if (line[start] == '#') {
        end = line.size();
        return "code-comment";
    }
    if (line[start] == '\'' || line[start] == '"') {
        const char quote = line[start];
        end = start + 1;
        while (end < line.size()) {
            if (line[end] == '\\' && end + 1 < line.size()) {
                end += 2;
                continue;
            }
            if (line[end++] == quote)
                break;
        }
        return "code-string";
    }
    if (line[start] == '@') {
        end = start + 1;
        while (end < line.size()
            && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_'))
            ++end;
        return "code-decorator";
    }
    if (std::isdigit(first)) {
        end = start + 1;
        while (end < line.size()
            && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_'
                || line[end] == '.'))
            ++end;
        return "code-number";
    }
    if (std::isalpha(first) || line[start] == '_') {
        end = start + 1;
        while (end < line.size()
            && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_'))
            ++end;
        const auto word = line.substr(start, end - start);
        if (scriptKeyword(word))
            return "code-keyword";
        if (scriptBuiltinType(word) || scriptTypeAnnotationContext(line, start)
            || scriptLooksLikeTypeName(word))
            return "code-type";
        auto prefix = line.substr(0, start);
        while (!prefix.empty() && (prefix.back() == ' ' || prefix.back() == '\t'))
            prefix.remove_suffix(1);
        const auto previousStart = prefix.find_last_of(" \t");
        const auto previousWord =
            previousStart == std::string_view::npos ? prefix : prefix.substr(previousStart + 1);
        if (previousWord == "class")
            return "code-type";
        if (previousWord == "def")
            return "code-function";
        std::size_t next = end;
        while (next < line.size() && (line[next] == ' ' || line[next] == '\t'))
            ++next;
        if (next < line.size() && line[next] == '(')
            return "code-function";
        return "code-default";
    }
    end = core::Utf8Cursor::next(line, start);
    return "code-default";
}

} // namespace

float EditorShell::scriptTabWidth(const std::filesystem::path& path)
{
    const auto filename = core::FilePath::utf8(path.filename());
    return std::clamp(50.0f + static_cast<float>(filename.size()) * 8.0f, 136.0f, 220.0f);
}

core::pmr::String EditorShell::scriptTabTitle(
    const std::filesystem::path& path, float availableWidth, bool dirty)
{
    const auto filename = core::FilePath::utf8(path.filename());
    const float markerWidth = dirty ? 16.0f : 0.0f;
    const float textWidth = std::max(0.0f, availableWidth - markerWidth);
    auto measure = [](std::string_view value) {
        float width = 0.0f;
        for (std::size_t i = 0; i < value.size();) {
            const auto bytes = core::Utf8Cursor::at(value, i).byteLength;
            width += bytes == 1 ? 8.0f : 16.0f;
            i += bytes;
        }
        return width;
    };

    core::pmr::String title(&core::defaultCpuMemoryResource());
    if (measure(std::string_view(filename.data(), filename.size())) <= textWidth) {
        title = filename;
    } else {
        constexpr std::string_view ellipsis = "...";
        const float prefixWidth = std::max(0.0f, textWidth - 24.0f);
        float used = 0.0f;
        std::size_t end = 0;
        while (end < filename.size()) {
            const auto bytes = core::Utf8Cursor::at(filename, end).byteLength;
            const float glyphWidth = bytes == 1 ? 8.0f : 16.0f;
            if (used + glyphWidth > prefixWidth)
                break;
            used += glyphWidth;
            end += bytes;
        }
        title.append(filename.data(), end);
        title.append(ellipsis.data(), ellipsis.size());
    }
    if (dirty)
        title += " *";
    return title;
}

bool EditorShell::isWideCodepoint(std::uint32_t codepoint)
{
    return (codepoint >= 0x2e80u && codepoint <= 0xa4cfu)
        || (codepoint >= 0xac00u && codepoint <= 0xd7afu)
        || (codepoint >= 0xf900u && codepoint <= 0xfaffu)
        || (codepoint >= 0xfe10u && codepoint <= 0xfe6fu)
        || (codepoint >= 0xff00u && codepoint <= 0xffefu);
}

float EditorShell::utf8AdvancePixels(
    std::string_view value, std::size_t byteOffset, float regularWidth, float cjkWidth)
{
    float width = 0.0f;
    for (std::size_t i = 0; i < byteOffset && i < value.size();) {
        const auto codepoint = core::Utf8Cursor::at(value, i);
        width += isWideCodepoint(codepoint.value) ? cjkWidth : regularWidth;
        i += codepoint.byteLength;
    }
    return width;
}

std::size_t EditorShell::utf8OffsetAtAdvance(
    std::string_view value, float advance, float regularWidth, float cjkWidth)
{
    float current = 0.0f;
    for (std::size_t i = 0; i < value.size();) {
        const auto codepoint = core::Utf8Cursor::at(value, i);
        const float step = isWideCodepoint(codepoint.value) ? cjkWidth : regularWidth;
        if (advance < current + step * 0.5f)
            return i;
        current += step;
        i += codepoint.byteLength;
    }
    return value.size();
}

renderer::ui::UiDrawPlanLowerResult EditorShell::drawCommands(
    const std::string_view viewportTextureResourceId)
{
    auto layout = runtime().canvas().resolveLayout();
    auto extracted = ui::RenderExtractor().extract(runtime().canvas(), layout);
    if (!extracted.ok())
        return renderer::ui::UiDrawPlanLowerResult::failure(extracted.diagnostic().message);
    auto planned = ui::DrawPlanner().plan(extracted.queue());
    if (!planned.ok())
        return renderer::ui::UiDrawPlanLowerResult::failure(planned.diagnostic().message);
    renderer::ui::UiDrawPlanLoweringDesc desc;
    desc.extent = {1920, 1080};
    desc.atlasWidth = editor_atlas::kWidth;
    desc.atlasHeight = editor_atlas::kHeight;
    for (auto p : palette) {
        renderer::ui::UiDrawStyleColor c;
        c.styleId = p.name;
        c.color[0] = float((p.rgb >> 16) & 255) / 255;
        c.color[1] = float((p.rgb >> 8) & 255) / 255;
        c.color[2] = float(p.rgb & 255) / 255;
        c.color[3] = p.alpha;
        desc.styleColors.push_back(std::move(c));
    }
    auto lowered = renderer::ui::UiDrawPlanLowerer().lower(planned.plan(), desc);
    if (!lowered.ok())
        return lowered;
    auto commands = lowered.commands();
    core::pmr::Vector<renderer::ui::UiDrawCommand> roundedCommands;
    roundedCommands.reserve(commands.size() + 64);
    for (auto& c : commands) {
        auto* widget = runtime().canvas().findWidget(c.widgetId);
        if (!widget)
            continue;
        const auto& name = presentationState_.sourceFor(c.widgetId.value());
        const bool textCommand = c.kind == renderer::ui::UiDrawCommandKind::Text;
        bool roundedSelected = false;
        if (textCommand
            && (widget->desc().styleId == "input" || widget->desc().styleId == "mask"
                || name == "file_root")) {
            const float inset = name == "file_root" ? 25.f
                : widget->desc().styleId == "mask"  ? 3.f
                                                    : 8.f;
            c.rect.x += inset;
            c.rect.width -= inset;
        }
        if (textCommand && widget->desc().controlType != ui::UiControlType::Label)
            color(c, 0xbdbdbd);
        bool selectedFileRow = false;
        if (!textCommand && !selectedFilePath_.empty()) {
            for (std::size_t slot = 0; slot < kFileSlotCount; ++slot) {
                if (name == kFileSlots[slot]
                    && fileScrollOffset_ + slot < visibleFileEntryCount()) {
                    const auto& visibleEntries = projectBrowser_.visibleEntryIndices();
                    const auto& entries = projectBrowser_.entries();
                    const auto entryIndex = visibleEntries[fileScrollOffset_ + slot];
                    selectedFileRow = entryIndex < entries.size()
                        && entries[entryIndex].path == selectedFilePath_;
                    break;
                }
            }
        }
        const bool rootSelected =
            name == "file_root" && projectBrowser_.currentDirectory() == projectBrowser_.root();
        if (!textCommand
            && (name == "node_" + presentationState_.selection() || rootSelected
                || selectedFileRow)) {
            color(c, 0x454545);
            roundedSelected = true;
        }
        if (core::startsWith(name, "workspace_")
            && name.substr(10) == presentationState_.workspace()) {
            color(c, textCommand ? 0x69a7ed : 0x131313);
            roundedSelected = !textCommand;
        }
        const bool activeTransformTool =
            (name == "tool_1" && sceneTransformTool_ == SceneTransformTool::Move)
            || (name == "tool_2" && sceneTransformTool_ == SceneTransformTool::Rotate)
            || (name == "tool_3" && sceneTransformTool_ == SceneTransformTool::Scale)
            || (name == "tool_4" && sceneTransformTool_ == SceneTransformTool::Select);
        if (activeTransformTool && !textCommand) {
            color(c, 0x30465a);
            roundedSelected = c.kind == renderer::ui::UiDrawCommandKind::Box;
        }
        if (core::startsWith(name, "dock_") && !textCommand) {
            color(c, name.substr(5) == presentationState_.dock() ? 0x303030 : 0x282828);
            roundedSelected = name.substr(5) == presentationState_.dock();
        }
        if ((name == "tab_scene" || name == "tab_import") && !textCommand) {
            const bool selected = (name == "tab_import") == (sceneDockTab_ == "Import");
            color(c, selected ? 0x303030 : 0x282828);
            roundedSelected = selected;
        }
        if ((name == "scene_level" || name == "scene_menu" || name == "scene_player")
            && !textCommand) {
            color(c, name.substr(6) == presentationState_.scene() ? 0x303030 : 0x131313);
            roundedSelected = name.substr(6) == presentationState_.scene();
        }
        if (widget->desc().controlType == ui::UiControlType::Button && !textCommand
            && runtime().hoveredControl() == c.widgetId && !selectedFileRow) {
            color(c, core::startsWith(name, "node_") ? 0x454545 : 0x3b3b3b);
            roundedSelected = true;
        }
        if (widget->desc().controlType == ui::UiControlType::TextInput && !textCommand
            && runtime().focusedControl() == c.widgetId) {
            color(c, 0x30465a);
            roundedSelected = true;
        }
        if (widget->desc().styleId == "mask" && !textCommand
            && presentationState_.isDisabledMask(std::string_view(name.data(), name.size())))
            color(c, 0x263449);
        if (widget->desc().controlType == ui::UiControlType::Checkbox && !textCommand)
            color(c, runtime().findControl(c.widgetId)->value().boolean ? 0x619cef : 0x414141);
        if (roundedSelected && c.kind == renderer::ui::UiDrawCommandKind::Box) {
            constexpr float corner = static_cast<float>(editor_atlas::kRoundedCorner);
            constexpr float atlasSize = 24.0f;
            const auto& region = editor_atlas::kRoundedSelection;
            const float destX[] = {0.0f, corner, c.rect.width - corner};
            const float destY[] = {0.0f, corner, c.rect.height - corner};
            const float srcX[] = {0.0f, corner, atlasSize - corner};
            const float srcY[] = {0.0f, corner, atlasSize - corner};
            const float widths[] = {corner, c.rect.width - corner * 2.0f, corner};
            const float heights[] = {corner, c.rect.height - corner * 2.0f, corner};
            const float sourceWidths[] = {corner, atlasSize - corner * 2.0f, corner};
            const float sourceHeights[] = {corner, atlasSize - corner * 2.0f, corner};
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col) {
                    if (widths[col] <= 0.0f || heights[row] <= 0.0f)
                        continue;
                    auto patch = c;
                    patch.kind = renderer::ui::UiDrawCommandKind::Image;
                    patch.textureResourceId = "texture/editor-reference";
                    patch.rect = ui::LayoutRect::fromOriginSize(
                        c.rect.x + destX[col], c.rect.y + destY[row], widths[col], heights[row]);
                    patch.textureSourceRect = ui::LayoutRect::fromOriginSize(
                        (region.x + srcX[col]) / float(editor_atlas::kWidth),
                        (region.y + srcY[row]) / float(editor_atlas::kHeight),
                        sourceWidths[col] / float(editor_atlas::kWidth),
                        sourceHeights[row] / float(editor_atlas::kHeight));
                    patch.hasTextureSourceRect = true;
                    roundedCommands.push_back(std::move(patch));
                }
        } else
            roundedCommands.push_back(std::move(c));
    }
    commands = std::move(roundedCommands);
    // Tick marks use the same explicit atlas contract as the rest of the icons.
    for (const char* name : {"projection", "interior", "shadows"}) {
        auto checkbox = id(name);
        if (!runtime().canvas().findWidget(checkbox)->visible()
            || presentationState_.dock() != "Inspector"
            || !runtime().findControl(checkbox)->value().boolean)
            continue;
        for (const auto& resolved : layout.resolvedWidgets())
            if (resolved.widgetId == checkbox) {
                renderer::ui::UiDrawCommand tick;
                tick.kind = renderer::ui::UiDrawCommandKind::Image;
                tick.rect = resolved.rect;
                tick.clipRect = layout.clipRect(checkbox);
                tick.textureResourceId = "icon-check";
                const auto& region = editor_atlas::kCheck;
                tick.hasTextureSourceRect = true;
                tick.textureSourceRect =
                    ui::LayoutRect::fromOriginSize(region.x / float(editor_atlas::kWidth),
                        region.y / float(editor_atlas::kHeight),
                        region.width / float(editor_atlas::kWidth),
                        region.height / float(editor_atlas::kHeight));
                color(tick, 0xffffff);
                commands.push_back(std::move(tick));
                break;
            }
    }
    const auto fileThumb = ui::ScrollbarGeometry::thumb(kFileScrollbarY,
        kFileScrollbarHeight,
        visibleFileEntryCount(),
        kFileSlotCount,
        fileScrollOffset_,
        24.0f);
    if (fileThumb.visible) {
        renderer::ui::UiDrawCommand track;
        track.kind = renderer::ui::UiDrawCommandKind::Box;
        track.widgetId = id("files_view");
        track.rect = ui::LayoutRect::fromOriginSize(
            kFileScrollbarX, kFileScrollbarY, kFileScrollbarWidth, kFileScrollbarHeight);
        track.clipRect = ui::LayoutRect::fromOriginSize(0, 0, 1920, 1080);
        color(track, 0x1b1b1b);
        commands.push_back(std::move(track));
        renderer::ui::UiDrawCommand thumb;
        thumb.kind = renderer::ui::UiDrawCommandKind::Box;
        thumb.widgetId = id("files_view");
        thumb.rect = ui::LayoutRect::fromOriginSize(
            kFileScrollbarX, fileThumb.y, kFileScrollbarWidth, fileThumb.height);
        thumb.clipRect = ui::LayoutRect::fromOriginSize(0, 0, 1920, 1080);
        color(thumb, 0x686868);
        commands.push_back(std::move(thumb));
    }
    if (presentationState_.workspace() == "Script") {
        rebuildScriptTabs(commands);
        rebuildScriptCommands(layout, commands);
    } else if (presentationState_.workspace() == "3D") {
        rebuildSceneCommands(layout, commands, viewportTextureResourceId);
    }
    return renderer::ui::UiDrawPlanLowerResult::success(std::move(commands));
}

void EditorShell::rebuildSceneCommands(const ui::CanvasLayoutResult& layout,
    core::pmr::Vector<renderer::ui::UiDrawCommand>& commands,
    const std::string_view viewportTextureResourceId) const
{
    const auto* viewport = layout.findWidget(id("viewport"));
    if (!viewport)
        return;
    const auto viewportId = id("viewport");
    const auto hierarchyClip = ui::LayoutRect::fromOriginSize(layout::kSceneHierarchyLeft,
        layout::kSceneHierarchyTop,
        layout::kSceneHierarchyWidth,
        layout::kSceneHierarchyVisibleHeight);
    commands.erase(std::remove_if(commands.begin(),
                       commands.end(),
                       [&](const auto& command) {
                           if (command.kind != renderer::ui::UiDrawCommandKind::Text)
                               return false;
                           const auto& rect = command.rect;
                           return rect.x < hierarchyClip.x + hierarchyClip.width
                               && rect.x + rect.width > hierarchyClip.x
                               && rect.y < hierarchyClip.y + hierarchyClip.height
                               && rect.y + rect.height > hierarchyClip.y;
                       }),
        commands.end());
    const auto hierarchy = visibleSceneHierarchyEntries();
    const auto hierarchyVisibleRows = layout::kSceneHierarchyVisibleRows;
    const auto firstVisibleRow = std::min(
        sceneHierarchyScrollOffset_, layout::sceneHierarchyMaxScrollOffset(hierarchy.size()));
    renderer::ui::UiDrawCommand hierarchyPanel;
    hierarchyPanel.kind = renderer::ui::UiDrawCommandKind::Box;
    hierarchyPanel.widgetId = viewportId;
    hierarchyPanel.rect = hierarchyClip;
    hierarchyPanel.clipRect = hierarchyClip;
    color(hierarchyPanel, 0x282828);
    commands.push_back(std::move(hierarchyPanel));
    const float hierarchyTextY = layout::kSceneHierarchyTop + 3.0f;
    const auto addHierarchyIcon =
        [&](std::string_view resource, ui::LayoutRect rect, unsigned tint) {
            ui::LayoutRect sourceRect;
            if (!iconSourceRect(resource, sourceRect))
                return;
            renderer::ui::UiDrawCommand icon;
            icon.kind = renderer::ui::UiDrawCommandKind::Image;
            icon.widgetId = viewportId;
            icon.rect = rect;
            icon.clipRect = hierarchyClip;
            icon.textureResourceId.assign(resource.data(), resource.size());
            icon.textureSourceRect = ui::LayoutRect::fromOriginSize(
                sourceRect.x / static_cast<float>(editor_atlas::kWidth),
                sourceRect.y / static_cast<float>(editor_atlas::kHeight),
                sourceRect.width / static_cast<float>(editor_atlas::kWidth),
                sourceRect.height / static_cast<float>(editor_atlas::kHeight));
            icon.hasTextureSourceRect = true;
            color(icon, tint);
            commands.push_back(std::move(icon));
        };
    for (std::size_t row = 0;
        row < hierarchyVisibleRows && firstVisibleRow + row < hierarchy.size();
        ++row) {
        const auto& entry = hierarchy[firstVisibleRow + row];
        const auto* object = sceneDocument_.scene().findObject(entry.objectId);
        if (!object)
            continue;
        const float rowY =
            layout::kSceneHierarchyTop + static_cast<float>(row) * layout::kSceneHierarchyRowHeight;
        const auto rowRect = ui::LayoutRect::fromOriginSize(layout::kSceneHierarchyLeft,
            rowY,
            layout::kSceneHierarchyWidth,
            layout::kSceneHierarchyRowHeight);
        renderer::ui::UiDrawCommand rowBackground;
        rowBackground.kind = renderer::ui::UiDrawCommandKind::Box;
        rowBackground.widgetId = viewportId;
        rowBackground.rect = rowRect;
        rowBackground.clipRect = hierarchyClip;
        if (entry.objectId == sceneDocument_.selectedObject()
            || (hierarchyDragging_ && entry.objectId == hierarchyDragTarget_)) {
            color(rowBackground,
                entry.objectId == hierarchyDragTarget_ && hierarchyDragging_ ? 0x405348 : 0x454545);
            commands.push_back(std::move(rowBackground));
        }
        const auto depth = std::min(entry.depth, std::size_t{12u});
        const float textX = layout::kSceneHierarchyLeft + layout::kSceneHierarchyTextInset
            + static_cast<float>(depth) * layout::kSceneHierarchyDepthIndent;
        if (!object->childIds().empty()) {
            const bool collapsed = sceneHierarchySearch_.empty()
                && std::find(sceneHierarchyCollapsedObjects_.begin(),
                       sceneHierarchyCollapsedObjects_.end(),
                       entry.objectId)
                    != sceneHierarchyCollapsedObjects_.end();
            addHierarchyIcon(collapsed ? "icon-chevron-right" : "icon-chevron-down",
                ui::LayoutRect::fromOriginSize(textX - 9.0f, rowY + 5.0f, 12.0f, 12.0f),
                0xbdbdbd);
        }
        std::string_view rowIcon = "icon-node";
        for (const auto& component : object->components()) {
            const std::string_view typeName(component->scriptTypeName());
            if (typeName == "MeshComponent" || typeName == "ColliderComponent") {
                rowIcon = "icon-cube";
                break;
            }
            if (typeName == "LightComponent") {
                rowIcon = "icon-world";
                break;
            }
            if (typeName == "CameraComponent") {
                rowIcon = "icon-tool-viewport";
                break;
            }
            if (typeName.find("Probe") != std::string_view::npos) {
                rowIcon = "icon-probe";
                break;
            }
            if (typeName.find("Audio") != std::string_view::npos
                || typeName.find("Sound") != std::string_view::npos) {
                rowIcon = "icon-sound";
                break;
            }
        }
        addHierarchyIcon(rowIcon,
            ui::LayoutRect::fromOriginSize(textX + 3.0f, rowY + 4.0f, 16.0f, 16.0f),
            0xbfc6cc);
        const float eyeX = layout::kSceneHierarchyLeft + layout::kSceneHierarchyWidth - 25.0f;
        addHierarchyIcon(
            "icon-eye", ui::LayoutRect::fromOriginSize(eyeX, rowY + 4.0f, 16.0f, 16.0f), 0x969da3);
        renderer::ui::UiDrawCommand rowLabel;
        rowLabel.kind = renderer::ui::UiDrawCommandKind::Text;
        const float labelX = textX + 22.0f;
        rowLabel.rect = ui::LayoutRect::fromOriginSize(labelX,
            hierarchyTextY + static_cast<float>(row) * layout::kSceneHierarchyRowHeight,
            eyeX - labelX - 8.0f,
            layout::kSceneHierarchyRowHeight);
        rowLabel.clipRect = hierarchyClip;
        rowLabel.text.assign(object->name().data(), object->name().size());
        rowLabel.fontToken = "font.editor";
        rowLabel.styleId = "text";
        color(rowLabel, entry.objectId == sceneDocument_.selectedObject() ? 0xe1e4e7 : 0xbdbdbd);
        commands.push_back(std::move(rowLabel));
    }
    const auto hierarchyThumb = ui::ScrollbarGeometry::thumb(layout::kSceneHierarchyTop,
        layout::kSceneHierarchyVisibleHeight,
        hierarchy.size(),
        hierarchyVisibleRows,
        firstVisibleRow,
        layout::kSceneHierarchyRowHeight);
    if (hierarchyThumb.visible) {
        const float scrollbarX = layout::kSceneHierarchyLeft + layout::kSceneHierarchyWidth
            - layout::kSceneHierarchyScrollbarWidth;
        renderer::ui::UiDrawCommand track;
        track.kind = renderer::ui::UiDrawCommandKind::Box;
        track.widgetId = viewportId;
        track.rect = ui::LayoutRect::fromOriginSize(scrollbarX,
            layout::kSceneHierarchyTop,
            layout::kSceneHierarchyScrollbarWidth,
            layout::kSceneHierarchyVisibleHeight);
        track.clipRect = hierarchyClip;
        color(track, 0x202020);
        commands.push_back(std::move(track));

        renderer::ui::UiDrawCommand thumb;
        thumb.kind = renderer::ui::UiDrawCommandKind::Box;
        thumb.widgetId = viewportId;
        thumb.rect = ui::LayoutRect::fromOriginSize(scrollbarX,
            hierarchyThumb.y,
            layout::kSceneHierarchyScrollbarWidth,
            hierarchyThumb.height);
        thumb.clipRect = hierarchyClip;
        color(thumb, sceneHierarchyDraggingScrollbar_ ? 0xb8b8b8 : 0x777777);
        commands.push_back(std::move(thumb));
    }
    if (presentationState_.workspace() == "3D"
        && (sceneDockTab_ == "Import" || (!sceneHierarchySearch_.empty() && hierarchy.empty()))) {
        renderer::ui::UiDrawCommand status;
        status.kind = renderer::ui::UiDrawCommandKind::Text;
        status.widgetId = viewportId;
        status.rect = ui::LayoutRect::fromOriginSize(layout::kSceneHierarchyLeft + 16.0f,
            layout::kSceneHierarchyTop + 12.0f,
            layout::kSceneHierarchyWidth - 32.0f,
            layout::kSceneHierarchyRowHeight);
        status.clipRect = hierarchyClip;
        status.text =
            sceneDockTab_ == "Import" ? "No importable resource selected" : "No matching nodes";
        status.fontToken = "font.editor";
        status.styleId = "muted";
        color(status, 0x9b9b9b);
        commands.push_back(std::move(status));
    }

    const auto rect = viewport->rect;
    const auto viewportClip = layout.clipRect(id("viewport"));
    auto box =
        [&](ui::LayoutRect bounds, unsigned rgb, float alpha = 1.0f, ui::WidgetId widget = {}) {
            renderer::ui::UiDrawCommand command;
            command.kind = renderer::ui::UiDrawCommandKind::Box;
            command.widgetId = widget.valid() ? widget : viewportId;
            command.rect = bounds;
            command.clipRect = widget.valid() ? layout.clipRect(widget) : viewportClip;
            color(command, rgb, alpha);
            commands.push_back(std::move(command));
        };
    auto label = [&](std::string_view value,
                     float x,
                     float y,
                     unsigned rgb,
                     ui::WidgetId widget = {},
                     float width = 0.0f) {
        renderer::ui::UiDrawCommand command;
        command.kind = renderer::ui::UiDrawCommandKind::Text;
        command.widgetId = widget.valid() ? widget : viewportId;
        command.rect = ui::LayoutRect::fromOriginSize(
            x, y, width > 0.0f ? width : (widget.valid() ? 260.0f : rect.width - 24.0f), 20.0f);
        command.clipRect = widget.valid() ? layout.clipRect(widget) : viewportClip;
        command.text.assign(value.data(), value.size());
        command.fontToken = "font.editor";
        command.styleId = "text";
        color(command, rgb);
        commands.push_back(std::move(command));
    };

    if (viewportTextureResourceId.empty()) {
        box(rect, 0x202326);
    } else {
        renderer::ui::UiDrawCommand image;
        image.kind = renderer::ui::UiDrawCommandKind::Image;
        image.widgetId = viewportId;
        image.rect = rect;
        image.clipRect = viewportClip;
        image.textureSourceRect = ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 1.0f, 1.0f);
        image.hasTextureSourceRect = true;
        image.color[0] = 1.0f;
        image.color[1] = 1.0f;
        image.color[2] = 1.0f;
        image.color[3] = 1.0f;
        image.textureResourceId = viewportTextureResourceId;
        image.textureSource = renderer::ui::UiTextureSource::External;
        commands.push_back(std::move(image));
    }
    auto project = [&](float x, float y, float z) {
        float screenX = 0.0f, screenY = 0.0f, depth = 0.0f;
        sceneViewportCamera_.project(
            core::Vec3{x, y, z}, rect.width, rect.height, screenX, screenY, &depth);
        if (depth <= 0.0f)
            return std::pair<float, float>{-100000.0f, -100000.0f};
        return std::pair<float, float>{rect.x + screenX, rect.y + screenY};
    };
    auto line = [&](float x0, float y0, float x1, float y1, unsigned rgb, float alpha = 0.72f) {
        float t0 = 0.0f, t1 = 1.0f;
        const float dx = x1 - x0, dy = y1 - y0;
        const auto clip = [&](float p, float q) {
            if (std::abs(p) < 0.0001f)
                return q >= 0.0f;
            const float ratio = q / p;
            if (p < 0.0f)
                t0 = std::max(t0, ratio);
            else
                t1 = std::min(t1, ratio);
            return t0 <= t1;
        };
        if (!clip(-dx, x0 - rect.x) || !clip(dx, rect.x + rect.width - x0)
            || !clip(-dy, y0 - rect.y) || !clip(dy, rect.y + rect.height - y0))
            return;
        const float clippedX0 = x0 + dx * t0, clippedY0 = y0 + dy * t0;
        const float clippedX1 = x0 + dx * t1, clippedY1 = y0 + dy * t1;
        const float clippedDx = clippedX1 - clippedX0, clippedDy = clippedY1 - clippedY0;
        const int steps = std::clamp(
            static_cast<int>(std::ceil(std::max(std::abs(clippedDx), std::abs(clippedDy)) / 4.0f)),
            1,
            256);
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            box(ui::LayoutRect::fromOriginSize(
                    clippedX0 + clippedDx * t, clippedY0 + clippedDy * t, 4.0f, 4.0f),
                rgb,
                alpha);
        }
    };
    for (int coordinate = -8; coordinate <= 8; ++coordinate) {
        const auto x0 = project(static_cast<float>(coordinate), 0.0f, -8.0f);
        const auto x1 = project(static_cast<float>(coordinate), 0.0f, 8.0f);
        const auto z0 = project(-8.0f, 0.0f, static_cast<float>(coordinate));
        const auto z1 = project(8.0f, 0.0f, static_cast<float>(coordinate));
        const unsigned gridColor = coordinate == 0 ? 0x454b50 : 0x252a2f;
        line(x0.first, x0.second, x1.first, x1.second, gridColor, 0.32f);
        line(z0.first, z0.second, z1.first, z1.second, gridColor, 0.32f);
    }
    const auto physicsOverlay = buildEditorScenePhysicsOverlay(sceneDocument_.scene(),
        kEditorScenePhysicsOverlaySegmentBudget,
        kEditorScenePhysicsOverlayCircleSegments);
    if (physicsOverlay.valid) {
        for (const auto& segment : physicsOverlay.segments) {
            const auto first = project(segment.from.x, segment.from.y, 0.0f);
            const auto second = project(segment.to.x, segment.to.y, 0.0f);
            const bool selected = sceneViewportObjectSelected(segment.objectId);
            const auto rgb = segment.kind == EditorScenePhysicsOverlayKind::Joint ? 0x75d6a1u
                : segment.sensor                                                  ? 0xd58bdd
                : selected                                                        ? 0xffcc66u
                                                                                  : 0x66c8d4u;
            line(first.first, first.second, second.first, second.second, rgb, 0.9f);
        }
    } else if (physicsOverlay.diagnostic[0] != '\0') {
        label(physicsOverlay.diagnostic, rect.x + 14.0f, rect.y + 32.0f, 0xe58f70);
    }
    const auto componentGizmos = buildEditorSceneComponentGizmos(sceneDocument_.scene(),
        projectResources_.manager(),
        rect.height > 0.0f ? rect.width / rect.height : 0.0f,
        kEditorSceneComponentGizmoSegmentBudget);
    if (componentGizmos.valid) {
        for (const auto& segment : componentGizmos.segments) {
            const auto first = project(segment.from.x, segment.from.y, segment.from.z);
            const auto second = project(segment.to.x, segment.to.y, segment.to.z);
            const bool selected = sceneViewportObjectSelected(segment.objectId);
            const auto rgb = segment.kind == EditorSceneComponentGizmoKind::Camera
                ? (selected ? 0xffdf83u : 0x72c8e8u)
                : segment.kind == EditorSceneComponentGizmoKind::Light
                ? (selected ? 0xffdf83u : 0xf1c75bu)
                : segment.kind == EditorSceneComponentGizmoKind::TerrainAuthoring
                ? (selected ? 0xffdf83u : 0xb59af7u)
                : segment.kind == EditorSceneComponentGizmoKind::TerrainBounds
                ? (selected ? 0xffdf83u : 0x87b7d8u)
                : segment.kind == EditorSceneComponentGizmoKind::CritterField
                ? (selected ? 0xffdf83u : 0xe99b7bu)
                : (selected ? 0xffdf83u : 0x70d1a5u);
            line(first.first, first.second, second.first, second.second, rgb, 0.88f);
        }
    } else if (componentGizmos.diagnostic[0] != '\0') {
        label(componentGizmos.diagnostic, rect.x + 14.0f, rect.y + 50.0f, 0xe58f70);
    }
    label("3D Scene Preview", rect.x + 14.0f, rect.y + 10.0f, 0xe1e4e7);
    label("Transform gizmos  |  select an object in the Scene tree",
        rect.x + 14.0f,
        rect.y + rect.height - 27.0f,
        0x92999f);

    const auto& objects = sceneDocument_.scene().objects();
    auto objectPoint = [&](const scene::GameObject& object) {
        const auto p = object.world_transform().translation;
        return project(p.x, p.y, p.z);
    };
    for (const auto& owned : objects) {
        const auto& object = *owned;
        const auto position = objectPoint(object);
        if (position.first < rect.x || position.first > rect.x + rect.width
            || position.second < rect.y || position.second > rect.y + rect.height)
            continue;
        const bool selected = sceneViewportObjectSelected(object.id());
        const unsigned marker = selected ? 0xffcc66 : 0x76b8ef;
        box(ui::LayoutRect::fromOriginSize(
                position.first - 4.0f, position.second - 4.0f, 9.0f, 9.0f),
            marker);
        const auto worldTransform = object.world_transform();
        const auto model =
            core::multiply(core::multiply(core::Mat4::translation(worldTransform.translation),
                               core::Mat4::rotation(worldTransform.rotation)),
                core::Mat4::scale(worldTransform.scale));
        for (const auto& component : object.components()) {
            const auto* meshComponent =
                dynamic_cast<const scene3d::MeshComponent*>(component.get());
            if (!meshComponent || !meshComponent->enabled() || meshComponent->resourceId().empty())
                continue;
            const auto meshHandle = projectResources_.manager().find<resource::MeshAssetResource>(
                meshComponent->resourceId());
            if (!meshHandle.ready() || !meshHandle.get()->immutableMeshGeometry())
                continue;
            const auto* meshAsset = meshHandle.get()->mesh();
            if (!meshAsset || !meshAsset->validForIndexedDraw() || !meshAsset->bounds.valid)
                continue;

            const core::Vec3 localMin{
                meshAsset->bounds.min[0], meshAsset->bounds.min[1], meshAsset->bounds.min[2]};
            const core::Vec3 localMax{
                meshAsset->bounds.max[0], meshAsset->bounds.max[1], meshAsset->bounds.max[2]};

            core::Vec3 worldMin{std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            core::Vec3 worldMax{std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
            for (unsigned corner = 0u; corner < 8u; ++corner) {
                const core::Vec3 localPoint{(corner & 1u) ? localMax.x : localMin.x,
                    (corner & 2u) ? localMax.y : localMin.y,
                    (corner & 4u) ? localMax.z : localMin.z};
                const auto worldPoint = core::transformPoint(model, localPoint);
                worldMin.x = std::min(worldMin.x, worldPoint.x);
                worldMin.y = std::min(worldMin.y, worldPoint.y);
                worldMin.z = std::min(worldMin.z, worldPoint.z);
                worldMax.x = std::max(worldMax.x, worldPoint.x);
                worldMax.y = std::max(worldMax.y, worldPoint.y);
                worldMax.z = std::max(worldMax.z, worldPoint.z);
            }
            for (unsigned corner = 0u; corner < 8u; ++corner) {
                const core::Vec3 worldPoint{(corner & 1u) ? worldMax.x : worldMin.x,
                    (corner & 2u) ? worldMax.y : worldMin.y,
                    (corner & 4u) ? worldMax.z : worldMin.z};
                const auto first = project(worldPoint.x, worldPoint.y, worldPoint.z);
                for (unsigned axis = 0u; axis < 3u; ++axis) {
                    if ((corner & (1u << axis)) != 0u)
                        continue;
                    const core::Vec3 nextPoint{
                        worldPoint.x + (axis == 0u ? worldMax.x - worldMin.x : 0.0f),
                        worldPoint.y + (axis == 1u ? worldMax.y - worldMin.y : 0.0f),
                        worldPoint.z + (axis == 2u ? worldMax.z - worldMin.z : 0.0f)};
                    const auto second = project(nextPoint.x, nextPoint.y, nextPoint.z);
                    line(first.first,
                        first.second,
                        second.first,
                        second.second,
                        selected ? 0xe9bb63 : 0x7398af);
                }
            }
        }
        if (selected) {
            const auto origin = position;
            if (sceneTransformTool_ == SceneTransformTool::Rotate) {
                constexpr core::Vec3 planeU[] = {
                    {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
                constexpr core::Vec3 planeV[] = {
                    {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
                constexpr float radius = 1.25f;
                constexpr unsigned segments = 48u;
                for (std::size_t axisIndex = 0; axisIndex < 3u; ++axisIndex) {
                    const unsigned colorValue =
                        sceneGizmoAxis_ == static_cast<SceneGizmoAxis>(axisIndex + 1u)
                        ? 0xffffffu
                        : (axisIndex == 0u          ? 0xe36565u
                                  : axisIndex == 1u ? 0x83c77bu
                                                    : 0x6f9fe0u);
                    for (unsigned segment = 0u; segment < segments; ++segment) {
                        const float angle0 = 6.283185307179586f * static_cast<float>(segment)
                            / static_cast<float>(segments);
                        const float angle1 = 6.283185307179586f * static_cast<float>(segment + 1u)
                            / static_cast<float>(segments);
                        const auto point0 = worldTransform.translation
                            + core::scale(planeU[axisIndex], radius * std::cos(angle0))
                            + core::scale(planeV[axisIndex], radius * std::sin(angle0));
                        const auto point1 = worldTransform.translation
                            + core::scale(planeU[axisIndex], radius * std::cos(angle1))
                            + core::scale(planeV[axisIndex], radius * std::sin(angle1));
                        const auto first = project(point0.x, point0.y, point0.z);
                        const auto second = project(point1.x, point1.y, point1.z);
                        line(first.first, first.second, second.first, second.second, colorValue);
                    }
                }
            } else {
                const auto axisX = project(worldTransform.translation.x + 1.0f,
                    worldTransform.translation.y,
                    worldTransform.translation.z);
                const auto axisY = project(worldTransform.translation.x,
                    worldTransform.translation.y + 1.0f,
                    worldTransform.translation.z);
                const auto axisZ = project(worldTransform.translation.x,
                    worldTransform.translation.y,
                    worldTransform.translation.z + 1.0f);
                line(origin.first,
                    origin.second,
                    axisX.first,
                    axisX.second,
                    sceneTransformTool_ == SceneTransformTool::Move
                            && sceneGizmoAxis_ == SceneGizmoAxis::X
                        ? 0xffffff
                        : 0xe36565);
                line(origin.first,
                    origin.second,
                    axisY.first,
                    axisY.second,
                    sceneTransformTool_ == SceneTransformTool::Move
                            && sceneGizmoAxis_ == SceneGizmoAxis::Y
                        ? 0xffffff
                        : 0x83c77b);
                line(origin.first,
                    origin.second,
                    axisZ.first,
                    axisZ.second,
                    sceneTransformTool_ == SceneTransformTool::Move
                            && sceneGizmoAxis_ == SceneGizmoAxis::Z
                        ? 0xffffff
                        : 0x6f9fe0);
            }
        }
    }

    std::array<bool, EditorSceneAuthoringCodec::maxObjects> occupiedLabelCells{};
    const float labelTop = rect.y + 40.0f;
    const float labelBottom = rect.y + rect.height - 36.0f;
    const float labelAreaWidth = std::max(0.0f, rect.width - 16.0f);
    const float labelAreaHeight = std::max(0.0f, labelBottom - labelTop);
    const float labelMaxWidth = std::min(200.0f, labelAreaWidth - 6.0f);
    constexpr float labelPadding = 3.0f;
    constexpr float labelCellGap = 4.0f;
    constexpr float labelHeight = 20.0f;
    const float labelCellWidth = labelMaxWidth + labelPadding * 2.0f + labelCellGap;
    constexpr float labelCellHeight = labelHeight + labelPadding * 2.0f + labelCellGap;
    const std::size_t labelColumnCount = labelCellWidth > 0.0f
        ? std::min(EditorSceneAuthoringCodec::maxObjects,
              static_cast<std::size_t>((labelAreaWidth + labelCellGap) / labelCellWidth))
        : 0u;
    const std::size_t labelRowCount = labelColumnCount > 0u && labelCellHeight > 0.0f
        ? std::min(EditorSceneAuthoringCodec::maxObjects / labelColumnCount,
              static_cast<std::size_t>((labelAreaHeight + labelCellGap) / labelCellHeight))
        : 0u;
    const std::size_t labelCellCount = labelColumnCount * labelRowCount;
    if (labelCellCount == 0u)
        return;
    for (unsigned pass = 0u; pass < 2u && labelCellCount > 0u; ++pass) {
        for (const auto& owned : objects) {
            const auto& object = *owned;
            const bool selectedObject = sceneViewportObjectSelected(object.id());
            if (selectedObject != (pass == 0u))
                continue;
            const auto anchor = objectPoint(object);
            if (anchor.first < rect.x || anchor.first > rect.x + rect.width
                || anchor.second < rect.y || anchor.second > rect.y + rect.height)
                continue;

            const std::string_view name(object.name().data(), object.name().size());
            const float width = std::clamp(static_cast<float>(name.size()) * 8.0f,
                std::min(48.0f, labelMaxWidth),
                labelMaxWidth);
            if (width <= 0.0f)
                continue;

            std::size_t nearestCell = labelCellCount;
            float nearestDistanceSquared = std::numeric_limits<float>::max();
            for (std::size_t cell = 0u; cell < labelCellCount; ++cell) {
                if (occupiedLabelCells[cell])
                    continue;
                const auto column = cell % labelColumnCount;
                const auto row = cell / labelColumnCount;
                const float centerX = rect.x + 8.0f + static_cast<float>(column) * labelCellWidth
                    + labelMaxWidth * 0.5f;
                const float centerY =
                    labelTop + static_cast<float>(row) * labelCellHeight + labelHeight * 0.5f;
                const float dx = centerX - anchor.first;
                const float dy = centerY - anchor.second;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared < nearestDistanceSquared) {
                    nearestDistanceSquared = distanceSquared;
                    nearestCell = cell;
                }
            }
            if (nearestCell == labelCellCount)
                continue;

            occupiedLabelCells[nearestCell] = true;
            const auto column = nearestCell % labelColumnCount;
            const auto row = nearestCell / labelColumnCount;
            const float x = rect.x + 8.0f + static_cast<float>(column) * labelCellWidth;
            const float y = labelTop + static_cast<float>(row) * labelCellHeight;
            const auto backing = ui::LayoutRect::fromOriginSize(x - labelPadding,
                y - labelPadding,
                width + labelPadding * 2.0f,
                labelHeight + labelPadding * 2.0f);
            box(backing, selectedObject ? 0x38342eu : 0x171a1du);
            label(name, x, y, selectedObject ? 0xffdc98 : 0xd0d5d9, {}, width);
            if (nearestCell + 1u == labelCellCount)
                break;
        }
    }
    if (sceneViewportGesture_ == SceneViewportGesture::BoxSelect) {
        const float left = std::min(sceneViewportBoxStartX_, sceneViewportBoxEndX_);
        const float right = std::max(sceneViewportBoxStartX_, sceneViewportBoxEndX_);
        const float top = std::min(sceneViewportBoxStartY_, sceneViewportBoxEndY_);
        const float bottom = std::max(sceneViewportBoxStartY_, sceneViewportBoxEndY_);
        if (right - left >= 1.0f || bottom - top >= 1.0f) {
            box(ui::LayoutRect::fromOriginSize(left, top, right - left, bottom - top),
                0x4b9ed8,
                0.16f);
            box(ui::LayoutRect::fromOriginSize(left, top, right - left, 1.0f), 0x77c7ff);
            box(ui::LayoutRect::fromOriginSize(left, bottom - 1.0f, right - left, 1.0f), 0x77c7ff);
            box(ui::LayoutRect::fromOriginSize(left, top, 1.0f, bottom - top), 0x77c7ff);
            box(ui::LayoutRect::fromOriginSize(right - 1.0f, top, 1.0f, bottom - top), 0x77c7ff);
        }
    }
}

void EditorShell::rebuildScriptTabs(core::pmr::Vector<renderer::ui::UiDrawCommand>& commands) const
{
    const auto layout = runtime().canvas().resolveLayout();
    const auto* tabs = layout.findWidget(id("scene_tabs"));
    if (!tabs)
        return;
    const auto clip = layout.clipRect(id("scene_tabs"));
    float x = tabs->rect.x + 4.0f;
    const auto& documents = scriptDocuments_.documents();
    for (std::size_t index = 0; index < documents.size(); ++index) {
        const auto& document = documents[index];
        const float width = scriptTabWidth(document.path);
        auto add = [&](renderer::ui::UiDrawCommandKind kind,
                       ui::LayoutRect rect,
                       unsigned rgb,
                       const core::pmr::String* value = nullptr,
                       const char* font = "font.editor") {
            renderer::ui::UiDrawCommand command;
            command.kind = kind;
            command.widgetId = id("scene_tabs");
            command.rect = rect;
            command.clipRect = clip;
            color(command, rgb);
            if (value) {
                command.text = *value;
                command.fontToken = font;
                command.styleId = "text";
            }
            commands.push_back(std::move(command));
        };
        const auto tabRect =
            ui::LayoutRect::fromOriginSize(x, tabs->rect.y + 2.0f, width, tabs->rect.height - 4.0f);
        add(renderer::ui::UiDrawCommandKind::Box,
            tabRect,
            index == scriptDocuments_.activeIndex() ? 0x3a3a3a : 0x242424);
        const bool dirty = document.dirty();
        auto title = scriptTabTitle(document.path, width - 38.0f, dirty);
        add(renderer::ui::UiDrawCommandKind::Text,
            ui::LayoutRect::fromOriginSize(x + 11.0f, tabs->rect.y + 8.0f, width - 38.0f, 20.0f),
            index == scriptDocuments_.activeIndex() ? 0xe2e2e2 : 0xb8b8b8,
            &title,
            "font.editor.semibold");
        core::pmr::String close("x", &core::defaultCpuMemoryResource());
        add(renderer::ui::UiDrawCommandKind::Text,
            ui::LayoutRect::fromOriginSize(x + width - 22.0f, tabs->rect.y + 8.0f, 14.0f, 20.0f),
            0x999999,
            &close);
        x += width + 2.0f;
        if (x >= tabs->rect.x + tabs->rect.width)
            break;
    }
}

void EditorShell::rebuildScriptCommands(const ui::CanvasLayoutResult& layout,
    core::pmr::Vector<renderer::ui::UiDrawCommand>& commands) const
{
    const auto* viewport = layout.findWidget(id("viewport"));
    if (!viewport)
        return;
    const auto rect = viewport->rect;
    const auto viewportClip = layout.clipRect(id("viewport"));
    const auto widget = id("viewport");
    auto addBox = [&](ui::LayoutRect box, unsigned rgb) {
        renderer::ui::UiDrawCommand command;
        command.kind = renderer::ui::UiDrawCommandKind::Box;
        command.widgetId = widget;
        command.rect = box;
        command.clipRect = viewportClip;
        color(command, rgb);
        commands.push_back(std::move(command));
    };
    auto addText =
        [&](const core::pmr::String& value, float x, float y, const char* style, unsigned rgb) {
            for (std::size_t start = 0; start < value.size();) {
                const bool cjk = isWideCodepoint(core::Utf8Cursor::at(value, start).value);
                std::size_t end = start + core::Utf8Cursor::at(value, start).byteLength;
                while (end < value.size()
                    && isWideCodepoint(core::Utf8Cursor::at(value, end).value) == cjk)
                    end += core::Utf8Cursor::at(value, end).byteLength;
                const float runX = x
                    + utf8AdvancePixels(std::string_view(value.data(), value.size()),
                        start,
                        codeColumnWidth_,
                        codeCjkColumnWidth_);
                const float available = rect.width - (runX - rect.x) - 8.0f;
                if (available > 1.0f) {
                    renderer::ui::UiDrawCommand command;
                    command.kind = renderer::ui::UiDrawCommandKind::Text;
                    command.widgetId = widget;
                    command.rect = ui::LayoutRect::fromOriginSize(
                        runX, y + (cjk ? kCodeCjkOpticalVerticalOffset : 0.0f), available, 20.0f);
                    command.clipRect = viewportClip;
                    command.text.assign(value.data() + start, end - start);
                    command.styleId = style;
                    command.fontToken = cjk ? "font.code.cjk" : "font.code";
                    color(command, rgb);
                    commands.push_back(std::move(command));
                }
                start = end;
            }
        };
    addBox(rect, 0x1e1e1e);
    addBox(ui::LayoutRect::fromOriginSize(rect.x, rect.y, rect.width, 30.0f), 0x252526);
    const auto filename = scriptPath().empty()
        ? core::pmr::String("No script open", &core::defaultCpuMemoryResource())
        : core::FilePath::utf8(scriptPath().filename());
    addText(core::pmr::String("SCRIPT  ", &core::defaultCpuMemoryResource()) + filename
            + (scriptDirty() ? "  *" : ""),
        rect.x + 14.0f,
        rect.y + 5.0f,
        "code-default",
        0xd4d4d4);
    const float codeTop = rect.y + 34.0f;
    const float gutterWidth = 54.0f;
    addBox(ui::LayoutRect::fromOriginSize(
               rect.x, codeTop, gutterWidth, rect.height - (codeTop - rect.y)),
        0x252526);
    const std::size_t visibleLines =
        static_cast<std::size_t>((rect.height - 42.0f) / kCodeLineHeight);
    const float textX = rect.x + gutterWidth + 14.0f;
    std::size_t lineStart = 0, lineNumber = 0;
    while (
        lineNumber < scriptDocuments_.activeScrollLine() && lineStart < scriptTextView().size()) {
        const auto end = scriptTextView().find('\n', lineStart);
        if (end == core::pmr::String::npos) {
            lineStart = scriptTextView().size();
            break;
        }
        lineStart = end + 1;
        ++lineNumber;
    }
    std::size_t cursorLine = 0, cursorLineStart = 0;
    const auto cursor = scriptDocuments_.activeCursor();
    const auto scrollLine = scriptDocuments_.activeScrollLine();
    for (std::size_t i = 0; i < cursor && i < scriptTextView().size(); ++i) {
        if (scriptTextView()[i] == '\n') {
            ++cursorLine;
            cursorLineStart = i + 1;
        }
    }
    const std::string_view cursorLineText(
        scriptTextView().data() + cursorLineStart, cursor - cursorLineStart);
    const float cursorAdvance = utf8AdvancePixels(
        cursorLineText, cursorLineText.size(), codeColumnWidth_, codeCjkColumnWidth_);
    std::size_t drawn = 0;
    while (drawn < visibleLines && (lineStart <= scriptTextView().size())) {
        const auto lineEndPos = scriptTextView().find('\n', lineStart);
        const std::size_t lineEnd =
            lineEndPos == core::pmr::String::npos ? scriptTextView().size() : lineEndPos;
        const float y = codeTop + drawn * kCodeLineHeight;
        if (lineNumber == cursorLine)
            addBox(ui::LayoutRect::fromOriginSize(
                       rect.x + gutterWidth, y, rect.width - gutterWidth, kCodeLineHeight),
                0x282828);
        const auto selectionStart = scriptSelectionStart(), selectionEnd = scriptSelectionEnd();
        if (selectionStart < selectionEnd && selectionEnd > lineStart
            && selectionStart <= lineEnd) {
            const auto first = std::max(selectionStart, lineStart);
            const auto last = std::min(selectionEnd, lineEnd);
            const std::string_view selectedLine(
                scriptTextView().data() + lineStart, lineEnd - lineStart);
            const float firstAdvance = utf8AdvancePixels(
                selectedLine, first - lineStart, codeColumnWidth_, codeCjkColumnWidth_);
            const float lastAdvance = utf8AdvancePixels(
                selectedLine, last - lineStart, codeColumnWidth_, codeCjkColumnWidth_);
            const bool includesNewline =
                lineEndPos != core::pmr::String::npos && selectionEnd > lineEnd;
            const float width =
                lastAdvance - firstAdvance + (includesNewline ? codeColumnWidth_ : 0.0f);
            if (width > 0.0f)
                addBox(
                    ui::LayoutRect::fromOriginSize(textX + firstAdvance, y, width, kCodeLineHeight),
                    0x264f78);
        }
        const float textY = y + kCodeTextVerticalOffset;
        char number[24];
        std::snprintf(number, sizeof(number), "%zu", lineNumber + 1);
        addText(core::pmr::String(number, &core::defaultCpuMemoryResource()),
            rect.x + 8.0f,
            textY,
            "code-line",
            0x858585);
        const std::string_view line(scriptTextView().data() + lineStart, lineEnd - lineStart);
        std::size_t position = 0;
        while (position < line.size()) {
            std::size_t end = position + 1;
            const char* style = scriptTokenStyle(line, position, end);
            // Keep adjacent punctuation/whitespace with the same syntax style
            // in one text run. This cuts per-frame layout-cache lookups and
            // glyph commands substantially for ordinary code lines.
            while (end < line.size()) {
                std::size_t nextEnd = end + 1;
                const char* nextStyle = scriptTokenStyle(line, end, nextEnd);
                if (std::strcmp(style, nextStyle) != 0)
                    break;
                end = nextEnd;
            }
            const auto rgb = std::strcmp(style, "code-keyword") == 0 ? 0x569cd6u
                : std::strcmp(style, "code-string") == 0             ? 0xce9178u
                : std::strcmp(style, "code-comment") == 0            ? 0x6a9955u
                : std::strcmp(style, "code-number") == 0             ? 0xb5cea8u
                : std::strcmp(style, "code-function") == 0           ? 0xdcdcaau
                : std::strcmp(style, "code-type") == 0               ? 0x4ec9b0u
                : std::strcmp(style, "code-decorator") == 0          ? 0xc586c0u
                                                                     : 0xd4d4d4u;
            const float x =
                textX + utf8AdvancePixels(line, position, codeColumnWidth_, codeCjkColumnWidth_);
            core::pmr::String token(
                line.data() + position, end - position, &core::defaultCpuMemoryResource());
            addText(token, x, textY, style, rgb);
            position = end;
        }
        ++lineNumber;
        ++drawn;
        if (lineEndPos == core::pmr::String::npos)
            break;
        lineStart = lineEndPos + 1;
    }
    if (scriptFocused_ && cursorLine >= scrollLine && cursorLine < scrollLine + visibleLines) {
        const float y = codeTop + static_cast<float>(cursorLine - scrollLine) * kCodeLineHeight;
        const float x = textX + cursorAdvance;
        addBox(
            ui::LayoutRect::fromOriginSize(x, y + kCodeTextVerticalOffset, 1.5f, 16.0f), 0xd4d4d4);
    }
    const float scrollbarX = rect.x + rect.width - kScriptScrollbarWidth - 2.0f;
    const float scrollbarY = codeTop;
    const float scrollbarHeight = rect.height - 58.0f;
    const auto scriptThumb = ui::ScrollbarGeometry::thumb(scrollbarY,
        scrollbarHeight,
        scriptLineCount(scriptTextView()),
        visibleLines,
        scrollLine,
        32.0f);
    if (scriptThumb.visible) {
        addBox(ui::LayoutRect::fromOriginSize(
                   scrollbarX, scrollbarY, kScriptScrollbarWidth, scrollbarHeight),
            0x181818);
        addBox(ui::LayoutRect::fromOriginSize(
                   scrollbarX, scriptThumb.y, kScriptScrollbarWidth, scriptThumb.height),
            0x686868);
    }
    const auto status = scriptDirty()
        ? core::pmr::String("Unsaved changes  |  Ctrl+S to save", &core::defaultCpuMemoryResource())
        : core::pmr::String("ArpScript  |  Ctrl+Z undo  |  Ctrl+Y redo  |  Tab inserts 4 spaces",
              &core::defaultCpuMemoryResource());
    addBox(ui::LayoutRect::fromOriginSize(rect.x, rect.y + rect.height - 24.0f, rect.width, 24.0f),
        0x252526);
    addText(status, rect.x + 12.0f, rect.y + rect.height - 22.0f, "code-line", 0x858585);
}

} // namespace arp::editor
