#include "editor/workspace/editor_shell.h"

#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <system_error>
#include <type_traits>

namespace arp::editor {
using namespace layout;
constexpr std::size_t kSceneInspectorDynamicRows = 64;
constexpr std::size_t kSceneInspectorWidgetsPerRow = 4;
constexpr std::size_t kSceneInspectorDynamicWidgetCapacity =
    kSceneInspectorDynamicRows * (kSceneInspectorWidgetsPerRow + 6u)
    + kMaxSceneInspectorVector2ListElements * kSceneInspectorWidgetsPerRow
    + kMaxSceneInspectorObjectLinks * 3u + 16u;
constexpr std::size_t kSceneInspectorVector2ListPointBudget = 64u;

std::string_view componentIcon(std::string_view typeName)
{
    if (typeName == "MeshComponent" || typeName == "ColliderComponent"
        || typeName == "CollisionShape3DComponent")
        return "icon-cube";
    if (typeName == "LightComponent")
        return "icon-world";
    if (typeName == "CameraComponent")
        return "icon-tool-viewport";
    if (typeName.find("Probe") != std::string_view::npos)
        return "icon-probe";
    if (typeName.find("Audio") != std::string_view::npos
        || typeName.find("Sound") != std::string_view::npos)
        return "icon-sound";
    return "icon-node";
}

bool EditorShell::iconSourceRect(std::string_view resourceId, ui::LayoutRect& sourceRect) const
{
    for (const auto& texture : loaded_.document().textures()) {
        if (std::string_view(texture.resourceId.data(), texture.resourceId.size()) == resourceId) {
            sourceRect = texture.sourceRect;
            return sourceRect.valid();
        }
    }
    return false;
}

void appendIndex(core::pmr::String& text, std::size_t index)
{
    char buffer[std::numeric_limits<std::size_t>::digits10 + 2u];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), index);
    if (result.ec == std::errc{})
        text.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

bool EditorShell::containsIgnoringCase(
    const core::pmr::String& value, const core::pmr::String& query)
{
    return std::search(value.begin(), value.end(), query.begin(), query.end(), [](char a, char b) {
        return core::toLowerAscii(a) == core::toLowerAscii(b);
    }) != value.end();
}

core::pmr::Vector<SceneHierarchyEntry> EditorShell::visibleSceneHierarchyEntries() const
{
    auto hierarchy = sceneDocument_.hierarchyEntries();
    if (presentationState_.workspace() != "3D")
        return hierarchy;
    if (sceneDockTab_ != "Scene")
        return {};
    if (!sceneHierarchySearch_.empty()) {
        core::pmr::Vector<std::uint8_t> visibleRows{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::size_t> ancestorRows{&core::defaultCpuMemoryResource()};
        visibleRows.resize(hierarchy.size(), 0u);
        ancestorRows.reserve(hierarchy.size());
        for (std::size_t row = 0; row < hierarchy.size(); ++row) {
            const auto depth = hierarchy[row].depth;
            while (ancestorRows.size() > depth)
                ancestorRows.pop_back();
            const auto* object = sceneDocument_.scene().findObject(hierarchy[row].objectId);
            if (!object)
                continue;
            if (containsIgnoringCase(object->name(), sceneHierarchySearch_)) {
                visibleRows[row] = 1u;
                for (const auto ancestor : ancestorRows)
                    visibleRows[ancestor] = 1u;
            }
            if (ancestorRows.size() == depth)
                ancestorRows.push_back(row);
        }

        core::pmr::Vector<SceneHierarchyEntry> filtered{&core::defaultCpuMemoryResource()};
        filtered.reserve(hierarchy.size());
        for (std::size_t row = 0; row < hierarchy.size(); ++row)
            if (visibleRows[row] != 0u)
                filtered.push_back(hierarchy[row]);
        hierarchy = std::move(filtered);
    } else if (!sceneHierarchyCollapsedObjects_.empty()) {
        core::pmr::Vector<SceneHierarchyEntry> visible{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::uint8_t> collapsedAncestors{&core::defaultCpuMemoryResource()};
        visible.reserve(hierarchy.size());
        collapsedAncestors.reserve(hierarchy.size());
        for (const auto& entry : hierarchy) {
            while (collapsedAncestors.size() > entry.depth)
                collapsedAncestors.pop_back();
            const bool hiddenByAncestor = std::any_of(
                collapsedAncestors.begin(), collapsedAncestors.end(), [](std::uint8_t collapsed) {
                    return collapsed != 0u;
                });
            if (hiddenByAncestor) {
                collapsedAncestors.push_back(1u);
                continue;
            }
            visible.push_back(entry);
            const bool collapsed = std::find(sceneHierarchyCollapsedObjects_.begin(),
                                       sceneHierarchyCollapsedObjects_.end(),
                                       entry.objectId)
                != sceneHierarchyCollapsedObjects_.end();
            collapsedAncestors.push_back(collapsed ? 1u : 0u);
        }
        hierarchy = std::move(visible);
    }
    return hierarchy;
}

EditorShell::EditorShell()
{
    sceneDocument_.setResourceManagerProvider([this] {
        return &projectResources_.manager();
    });
    sceneDocument_.setPhysicsWorld2DProvider([this] {
        return projectResources_.physicsWorld2D();
    });
}

void EditorAction::onClick(ui::UiEventContext&)
{
    shell_.activate(source_);
}

void EditorEntry::onTextChanged(const core::pmr::String&, const core::pmr::String& value)
{
    shell_.entryChanged(source_, value);
}

void EditorPropertyCheckbox::onValueChanged(
    const ui::UiControlValue&, const ui::UiControlValue& after)
{
    const core::pmr::String value(
        after.boolean ? "true" : "false", &core::defaultCpuMemoryResource());
    shell_.entryChanged(source_, value);
}

void EditorPropertyColorPicker::onValueChanged(
    const ui::UiControlValue&, const ui::UiControlValue& after)
{
    shell_.colorEntryChanged(source_, after.color);
}

void EditorEnumDropdown::onSelectionChanged(std::uint32_t, std::uint32_t selected)
{
    if (source_ == "scene_component_add") {
        if (selected < options_.size() && !options_[selected].commandValue.empty())
            (void)shell_.submitCommand(AddSceneComponentCommand(std::string_view(
                options_[selected].commandValue.data(), options_[selected].commandValue.size())));
        return;
    }
    if (selected < options_.size())
        shell_.entryChanged(
            source_, useCommandValue_ ? options_[selected].commandValue : options_[selected].name);
}

ui::UiResult EditorEnumDropdown::applyDefaultAction(
    const ui::UiEvent& event, ui::UiEventContext& context)
{
    if (event.kind == ui::UiInputEventKind::PointerUp
        || event.kind == ui::UiInputEventKind::KeyboardActivate
        || (event.kind == ui::UiInputEventKind::KeyDown && !event.repeat
            && (event.key == input::InputKey::Enter || event.key == input::InputKey::Space
                || event.key == input::InputKey::NumpadEnter))) {
        if (optionWidgets_.empty())
            openMenu();
        else
            closeMenu();
        context.markHandled();
        return ui::UiResult::success();
    }
    if (event.kind == ui::UiInputEventKind::PointerDown)
        return press(event, context);
    if (event.kind == ui::UiInputEventKind::PointerUp
        && context.runtime().capturedControl() == widgetId())
        context.markHandled();
    return ui::UiResult::success();
}

ui::UiResult EditorEnumDropdown::applyInteraction(
    const ui::UiControlInteraction& interaction, ui::UiEventContext& context)
{
    if (interaction.kind == ui::UiControlInteractionKind::Click) {
        if (optionWidgets_.empty())
            openMenu();
        else
            closeMenu();
        context.markHandled();
        return ui::UiResult::success();
    }
    return ui::Dropdown::applyInteraction(interaction, context);
}

void EditorEnumDropdown::openMenu()
{
    if (!runtime() || !optionWidgets_.empty())
        return;
    const auto* widget = runtime()->canvas().findWidget(widgetId());
    if (!widget || !widget->parentId().valid())
        return;
    const auto layout = runtime()->canvas().resolveLayout();
    const auto* widgetLayout = layout.ok() ? layout.findWidget(widgetId()) : nullptr;
    const auto* parentLayout = layout.ok() ? layout.findWidget(widget->parentId()) : nullptr;
    if (!widgetLayout || !parentLayout || parentLayout->rect.width <= 0.0f)
        return;
    const auto menuParent = runtime()->canvas().rootId();
    const float optionX = useCommandValue_ ? parentLayout->rect.x : widgetLayout->rect.x;
    const float optionWidth =
        useCommandValue_ ? parentLayout->rect.width : widgetLayout->rect.width;
    for (std::size_t index = 0; index < options_.size(); ++index) {
        ui::UiControlDesc optionDesc;
        optionDesc.type = ui::UiControlType::Button;
        auto& optionName = optionDesc.widget.name;
        optionName.append(source_.data(), source_.size());
        optionName.append("_option_");
        std::array<char, std::numeric_limits<std::size_t>::digits10 + 2> indexText{};
        const auto [indexEnd, indexError] =
            std::to_chars(indexText.data(), indexText.data() + indexText.size(), index);
        if (indexError != std::errc{}) {
            closeMenu();
            return;
        }
        optionName.append(indexText.data(), static_cast<std::size_t>(indexEnd - indexText.data()));
        const auto registeredSource = optionName;
        optionDesc.widget.localRect = ui::LayoutRect::fromOriginSize(optionX,
            widgetLayout->rect.y + widgetLayout->rect.height
                + static_cast<float>(index) * widgetLayout->rect.height,
            optionWidth,
            widgetLayout->rect.height);
        optionDesc.widget.styleId = desc().widget.styleId;
        optionDesc.widget.fontToken = desc().widget.fontToken;
        optionDesc.widget.text = options_[index].name;
        optionDesc.widget.acceptsFocus = true;
        optionDesc.initialValue = ui::UiControlValue::fromBool(false);
        const auto created = runtime()->createControl(
            core::makeUnique<EditorEnumOptionButton>(*this, static_cast<std::uint32_t>(index)),
            std::move(optionDesc),
            menuParent);
        if (!created.ok()) {
            closeMenu();
            return;
        }
        optionWidgets_.push_back(created.widgetId());
        shell_.presentationState_.registerSource(created.widgetId().value(), registeredSource);
    }
}

void EditorEnumDropdown::closeMenu()
{
    if (!runtime())
        return;
    for (const auto optionWidget : optionWidgets_) {
        shell_.presentationState_.unregisterSource(optionWidget.value());
        (void)runtime()->removeControl(optionWidget);
    }
    optionWidgets_.clear();
}

void EditorEnumDropdown::choose(std::uint32_t selected)
{
    if (selected < options_.size()) {
        closeMenu();
        (void)setValue(ui::UiControlValue::fromIndex(selected), ui::UiNotifyPolicy::Notify);
    }
}

void EditorEnumOptionButton::onClick(ui::UiEventContext&)
{
    dropdown_.choose(index_);
}

core::UniquePtr<ui::Control> EditorFactory::create(const ui::UiControlDesc& desc) const
{
    if (source_ == "properties")
        return core::makeUnique<EditorInspectorView>(shell_);
    if (source_ == "inspector_scroll")
        return core::makeUnique<EditorScrollThumb>(shell_);
    if (source_ == "scene_component_add") {
        core::pmr::Vector<SceneInspectorEnumOption> options{&core::defaultCpuMemoryResource()};
        const auto catalog = EditorSceneDocument::componentCatalog();
        options.reserve(catalog.size());
        for (std::size_t index = 0; index < catalog.size(); ++index) {
            options.emplace_back();
            options.back().name = catalog[index].displayName;
            options.back().commandValue = catalog[index].typeName;
            options.back().value = static_cast<int>(index);
        }
        return core::makeUnique<EditorEnumDropdown>(shell_, source_, std::move(options));
    }
    if (desc.type == ui::UiControlType::Button)
        return core::makeUnique<EditorAction>(shell_, source_);
    return core::makeUnique<EditorEntry>(shell_, source_);
}

void EditorInspectorView::onValueChanged(const ui::UiControlValue&, const ui::UiControlValue& after)
{
    shell_.inspectorScrollChanged(after.scroll.y);
}

ui::UiResult EditorScrollThumb::applyDefaultAction(
    const ui::UiEvent& event, ui::UiEventContext& context)
{
    if (event.kind == ui::UiInputEventKind::PointerDown) {
        const auto layout = context.runtime().canvas().resolveLayout();
        if (!layout.ok())
            return ui::UiResult::failure(layout.diagnostic().code, layout.diagnostic().message);
        const auto* thumb = layout.findWidget(widgetId());
        if (!thumb)
            return ui::UiResult::failure(
                ui::UiDiagnosticCode::InvalidWidgetId, "UI-INSPECTOR-THUMB-MISSING");
        grabOffset_ = event.y - thumb->rect.y;
        return press(event, context);
    }
    if (event.kind == ui::UiInputEventKind::PointerMove
        && context.runtime().capturedControl() == widgetId()) {
        context.markHandled();
        shell_.dragInspectorThumb(event.y - grabOffset_);
        return ui::UiResult::success();
    }
    if (event.kind == ui::UiInputEventKind::PointerUp
        && context.runtime().capturedControl() == widgetId()) {
        context.markHandled();
        return ui::UiResult::success();
    }
    return ui::Button::applyDefaultAction(event, context);
}

ui::UiResult EditorShell::load(const std::filesystem::path& path, std::filesystem::path projectRoot)
{
    ui::UiArpuiRuntimeLoadDesc desc;
    desc.path = path;
    auto document = ui::UiArpuiRuntimeLoader().load(desc);
    if (!document.ok())
        return ui::UiResult::failure(document.diagnostic().code, document.diagnostic().message);
    desc.maxWidgetCount =
        document.document().nodes().size() + kSceneInspectorDynamicWidgetCapacity + 1u;
    ui::UiControlFactoryRegistry factories(document.document().nodes().size());
    for (const auto& n : document.document().nodes()) {
        if (n.text.find_first_of("\r\n") != core::pmr::String::npos)
            return ui::UiResult::failure(ui::UiDiagnosticCode::UnsupportedControlBehavior,
                core::pmr::String(
                    "Editor text must be one line: ", &core::defaultCpuMemoryResource())
                    + n.sourceId);
        if (n.controlType == ui::UiControlType::Button
            || n.controlType == ui::UiControlType::TextInput
            || n.controlType == ui::UiControlType::ScrollView
            || n.controlType == ui::UiControlType::Dropdown) {
            auto result = factories.registerFactory(
                n.sourceId, core::makeUnique<EditorFactory>(*this, n.sourceId));
            if (!result.ok())
                return result;
        }
    }
    desc.factories = &factories;
    loaded_ = ui::UiArpuiRuntimeLoader().load(desc);
    if (!loaded_.ok())
        return ui::UiResult::failure(loaded_.diagnostic().code, loaded_.diagnostic().message);
    for (const auto& n : loaded_.document().nodes())
        presentationState_.registerSource(id(n.sourceId).value(), n.sourceId);
    const auto contentNode = std::find_if(
        loaded_.document().nodes().begin(), loaded_.document().nodes().end(), [](const auto& n) {
            return n.sourceId == "inspector_content";
        });
    if (contentNode == loaded_.document().nodes().end())
        return ui::UiResult::failure(
            ui::UiDiagnosticCode::RuntimeLoadFailed, "UI-INSPECTOR-CONTENT-MISSING");
    auto* inspectorPanel = runtime().canvas().findWidget(id("inspector"));
    auto* inspectorView = runtime().canvas().findWidget(id("properties"));
    if (!inspectorPanel || !inspectorView)
        return ui::UiResult::failure(
            ui::UiDiagnosticCode::RuntimeLoadFailed, "UI-INSPECTOR-PANEL-MISSING");
    const auto inspectorViewParent =
        runtime().reparentControl(inspectorView->id(), inspectorPanel->id());
    if (!inspectorViewParent.ok())
        return inspectorViewParent;
    inspectorView = runtime().canvas().findWidget(id("properties"));
    inspectorView->setLocalRect(ui::LayoutRect::fromOriginSize(8.0f, 142.0f, 302.0f, 840.0f));
    inspectorContentHeight_ = contentNode->layout.height;
    const auto inspectorContentId = id("inspector_content");
    auto* inspectorContent = runtime().canvas().findWidget(inspectorContentId);
    if (!inspectorContent)
        return ui::UiResult::failure(
            ui::UiDiagnosticCode::RuntimeLoadFailed, "UI-INSPECTOR-CONTENT-MISSING");
    core::pmr::Vector<ui::WidgetId> pendingInspectorWidgets(inspectorContent->childIds().begin(),
        inspectorContent->childIds().end(),
        &core::defaultCpuMemoryResource());
    while (!pendingInspectorWidgets.empty()) {
        const auto widgetId = pendingInspectorWidgets.back();
        pendingInspectorWidgets.pop_back();
        auto* widget = runtime().canvas().findWidget(widgetId);
        if (!widget)
            continue;
        staticInspectorWidgetIds_.push_back(widgetId);
        for (const auto childId : widget->childIds())
            pendingInspectorWidgets.push_back(childId);
    }

    struct SceneInspectorControlLayout {
        const char* source;
        float x;
        float y;
        float width;
        float height;
    };

    constexpr SceneInspectorControlLayout sceneInspectorLayout[] = {
        {"scene_name_label", 6.0f, 5.0f, 336.0f, 24.0f},
        {"scene_name", 6.0f, 31.0f, 336.0f, 28.0f},
        {"scene_transform_header", 28.0f, 68.0f, 314.0f, 25.0f},
        {"scene_component_add", 6.0f, 212.0f, 336.0f, 28.0f},
    };
    for (const auto& entry : sceneInspectorLayout) {
        const auto widgetId = id(entry.source);
        auto* widget = runtime().canvas().findWidget(widgetId);
        if (!widget)
            return ui::UiResult::failure(
                ui::UiDiagnosticCode::RuntimeLoadFailed, "UI-SCENE-INSPECTOR-CONTROL-MISSING");
        const auto reparented = runtime().reparentControl(widgetId, inspectorContentId);
        if (!reparented.ok())
            return reparented;
        widget = runtime().canvas().findWidget(widgetId);
        if (!widget)
            return ui::UiResult::failure(
                ui::UiDiagnosticCode::InvalidWidgetId, "UI-SCENE-INSPECTOR-CONTROL-MISSING");
        const auto sceneInspectorRect =
            ui::LayoutRect::fromOriginSize(entry.x, entry.y, entry.width, entry.height);
        widget->setLocalRect(sceneInspectorRect);
        const auto& appliedRect = widget->desc().localRect;
        if (appliedRect.x != sceneInspectorRect.x || appliedRect.y != sceneInspectorRect.y
            || appliedRect.width != sceneInspectorRect.width
            || appliedRect.height != sceneInspectorRect.height)
            return ui::UiResult::failure(
                ui::UiDiagnosticCode::InvalidLayoutAnchor, "UI-SCENE-INSPECTOR-PLACEMENT-FAILED");
    }
    auto* thumb = runtime().canvas().findWidget(id("inspector_scroll"));
    const auto viewLayout = runtime().canvas().resolveLayout();
    if (!thumb || !viewLayout.ok())
        return ui::UiResult::failure(
            ui::UiDiagnosticCode::InvalidLayoutAnchor, "UI-INSPECTOR-SCROLL-LAYOUT-INVALID");
    const auto* viewRect = viewLayout.findWidget(id("properties"));
    const auto* trackRect = viewLayout.findWidget(id("inspector_scroll_track"));
    if (!viewRect || !trackRect || inspectorContentHeight_ <= 0.0f)
        return ui::UiResult::failure(
            ui::UiDiagnosticCode::InvalidLayoutAnchor, "UI-INSPECTOR-SCROLL-METADATA-INVALID");
    const float thumbHeight = std::min(trackRect->rect.height,
        viewRect->rect.height * viewRect->rect.height / inspectorContentHeight_);
    auto thumbRect = thumb->desc().localRect;
    thumbRect.height = thumbHeight;
    thumb->setLocalRect(thumbRect);
    const auto initialScroll = runtime().setValue(
        id("properties"), ui::UiControlValue::fromScroll(0, 0), ui::UiNotifyPolicy::Notify);
    if (!initialScroll.ok())
        return ui::UiResult::failure(
            initialScroll.diagnostic().code, initialScroll.diagnostic().message);
    // Decorative foreground widgets must not steal pointer input from fields/buttons.
    for (const auto& n : loaded_.document().nodes())
        if (n.controlType == ui::UiControlType::Label
            || n.controlType == ui::UiControlType::Image) {
            runtime().canvas().findWidget(id(n.sourceId))->setAcceptsPointerInput(false);
        }
    visible("output_panel", false);
    visible("menu_popup", false);
    visible("signals_content", false);
    visible("groups_content", false);
    auto result = runtime().setValue(id("projection"), ui::UiControlValue::fromBool(true));
    if (!result.ok())
        return ui::UiResult::failure(result.diagnostic().code, result.diagnostic().message);
    for (unsigned n = 2; n <= 20; ++n) {
        char key[32];
        std::snprintf(key, sizeof(key), "layers_%u", n);
        presentationState_.toggleDisabledMask(key);
    }
    std::error_code pathError;
    auto canonicalRoot = std::filesystem::weakly_canonical(
        projectRoot.empty() ? std::filesystem::current_path(pathError) : projectRoot, pathError);
    if (pathError || canonicalRoot.empty()
        || !std::filesystem::is_directory(canonicalRoot, pathError))
        return ui::UiResult::failure(ui::UiDiagnosticCode::RuntimeLoadFailed,
            core::pmr::String("EDITOR-FILESYSTEM-ROOT: project root is not a readable directory",
                &core::defaultCpuMemoryResource()));
    const auto resources = projectResources_.load(canonicalRoot);
    if (!resources.success)
        return ui::UiResult::failure(
            ui::UiDiagnosticCode::RuntimeLoadFailed, resources.diagnostics);
    projectBrowser_.setRoot(std::move(canonicalRoot));
    if (!resources.diagnostics.empty())
        feedback(resources.diagnostics);
    if (const auto* viewport = runtime().canvas().findWidget(id("viewport")))
        gameViewportRect_ = viewport->desc().localRect;
    refreshFileSystem();
    updateWorkspaceChrome();
    return ui::UiResult::success();
}

float EditorShell::inspectorScrollOffset() const
{
    const auto* control = loaded_.runtime().findControl(id("properties"));
    return control ? control->value().scroll.y : 0.0f;
}

void EditorShell::inspectorScrollChanged(float offset)
{
    (void)submitCommand(SetInspectorScrollCommand{offset});
}

void EditorShell::applyInspectorScrollChanged(float offset)
{
    auto layout = runtime().canvas().resolveLayout();
    if (!layout.ok()) {
        diagnostic_ = layout.diagnostic();
        return;
    }
    const auto* view = layout.findWidget(id("properties"));
    const auto* track = layout.findWidget(id("inspector_scroll_track"));
    auto* content = runtime().canvas().findWidget(id("inspector_content"));
    auto* thumb = runtime().canvas().findWidget(id("inspector_scroll"));
    if (!view || !track || !content || !thumb)
        return;
    const float maxOffset = std::max(0.0f, inspectorContentHeight_ - view->rect.height);
    offset = std::clamp(offset, 0.0f, maxOffset);
    auto contentRect = content->desc().localRect;
    contentRect.y = -offset;
    content->setLocalRect(contentRect);
    auto thumbRect = thumb->desc().localRect;
    const float travel = std::max(0.0f, track->rect.height - thumbRect.height);
    thumbRect.y = track->rect.y + (maxOffset > 0.0f ? offset / maxOffset : 0.0f) * travel;
    thumb->setLocalRect(thumbRect);
}

void EditorShell::dragInspectorThumb(float top)
{
    (void)submitCommand(DragInspectorThumbCommand{top});
}

void EditorShell::applyDragInspectorThumb(float top)
{
    auto layout = runtime().canvas().resolveLayout();
    if (!layout.ok()) {
        diagnostic_ = layout.diagnostic();
        return;
    }
    const auto* track = layout.findWidget(id("inspector_scroll_track"));
    const auto* thumb = layout.findWidget(id("inspector_scroll"));
    const auto* view = layout.findWidget(id("properties"));
    if (!track || !thumb || !view)
        return;
    const float travel = std::max(0.0f, track->rect.height - thumb->rect.height);
    const float maxOffset = std::max(0.0f, inspectorContentHeight_ - view->rect.height);
    const float fraction =
        travel > 0.0f ? std::clamp((top - track->rect.y) / travel, 0.0f, 1.0f) : 0.0f;
    auto result = runtime().setValue(id("properties"),
        ui::UiControlValue::fromScroll(0.0f, fraction * maxOffset),
        ui::UiNotifyPolicy::Notify);
    if (!result.ok())
        diagnostic_ = result.diagnostic();
}

void EditorShell::text(const core::pmr::String& source, core::pmr::String value)
{
    auto result = runtime().setValue(id(source), ui::UiControlValue::fromText(std::move(value)));
    if (!result.ok())
        diagnostic_ = result.diagnostic();
}

void EditorShell::visible(const core::pmr::String& source, bool value)
{
    auto result = runtime().setVisible(id(source), value);
    if (!result.ok())
        diagnostic_ = result.diagnostic();
}

void EditorShell::feedback(core::pmr::String message)
{
    text("output_status", std::move(message));
}

void EditorShell::reportViewportDiagnostic(std::string_view diagnostic)
{
    core::pmr::String message("3D viewport: ", &core::defaultCpuMemoryResource());
    message.append(diagnostic.data(), diagnostic.size());
    feedback(std::move(message));
    presentationState_.openOutput();
    visible("output_panel", true);
}

bool EditorShell::sceneViewportObjectSelected(scene::GameObjectId objectId) const noexcept
{
    if (!objectId.valid() || sceneDocument_.scene().findObject(objectId) == nullptr)
        return false;
    return objectId == sceneDocument_.selectedObject()
        || std::find(sceneViewportAdditionalSelection_.begin(),
               sceneViewportAdditionalSelection_.end(),
               objectId)
        != sceneViewportAdditionalSelection_.end();
}

GamePreviewModel::Request EditorShell::consumeGamePreviewCommand()
{
    return gamePreview_.consumeRequest();
}

void EditorShell::setGamePreviewState(bool running, bool starting, const char* status)
{
    SetGamePreviewStateCommand command(status ? std::string_view(status) : std::string_view{});
    command.running = running;
    command.starting = starting;
    (void)submitCommand(std::move(command), EditorCommandOrigin::Internal);
}

void EditorShell::applyGamePreviewState(
    bool running, bool starting, const core::pmr::String& status)
{
    gamePreview_.setState(running, starting);
    presentationState_.openOutput();
    visible("output_panel", true);
    feedback(status);
}

EditorResponse EditorShell::submitCommand(
    EditorCommandPayload payload, EditorCommandOrigin origin, std::uint64_t correlationId)
{
    EditorCommandEnvelope command(std::move(payload));
    command.origin = origin;
    command.correlationId = correlationId;
    command.sourceSequence = nextCommandSequence_++;
    return submitCommand(std::move(command));
}

EditorResponse EditorShell::submitCommand(EditorCommandEnvelope command)
{
    return commandDispatcher_.dispatch(command, *this);
}

void EditorShell::activate(const core::pmr::String& source)
{
    constexpr std::string_view toggleComponentPrefix = "scene_component_toggle_";
    constexpr std::string_view removeComponentPrefix = "scene_component_remove_";
    const std::string_view sourceView(source.data(), source.size());
    const auto activateComponentAction = [&](std::string_view prefix, bool toggle) {
        if (sourceView.compare(0u, prefix.size(), prefix) != 0)
            return false;
        const auto objectBegin = sourceView.data() + prefix.size();
        const auto objectEnd = sourceView.find('_', prefix.size());
        if (objectEnd != std::string_view::npos) {
            std::uint64_t objectId = 0u;
            std::uint64_t componentId = 0u;
            const auto parsedObject =
                std::from_chars(objectBegin, sourceView.data() + objectEnd, objectId);
            const auto parsedComponent = std::from_chars(sourceView.data() + objectEnd + 1u,
                sourceView.data() + sourceView.size(),
                componentId);
            if (parsedObject.ec == std::errc{} && parsedObject.ptr == sourceView.data() + objectEnd
                && parsedComponent.ec == std::errc{}
                && parsedComponent.ptr == sourceView.data() + sourceView.size() && objectId != 0u
                && componentId != 0u) {
                if (toggle) {
                    const auto collapsed = std::find_if(collapsedInspectorComponents_.begin(),
                        collapsedInspectorComponents_.end(),
                        [&](const auto& item) {
                            return item.objectId == objectId && item.componentId == componentId;
                        });
                    if (collapsed == collapsedInspectorComponents_.end())
                        collapsedInspectorComponents_.push_back({objectId, componentId});
                    else
                        collapsedInspectorComponents_.erase(collapsed);
                    sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                    refreshSceneInspector();
                } else {
                    RemoveSceneComponentCommand command;
                    command.objectId = objectId;
                    command.componentId = componentId;
                    (void)submitCommand(command);
                    const auto collapsed = std::find_if(collapsedInspectorComponents_.begin(),
                        collapsedInspectorComponents_.end(),
                        [&](const auto& item) {
                            return item.objectId == objectId && item.componentId == componentId;
                        });
                    if (collapsed != collapsedInspectorComponents_.end())
                        collapsedInspectorComponents_.erase(collapsed);
                }
            }
        }
        return true;
    };
    if (activateComponentAction(toggleComponentPrefix, true)
        || activateComponentAction(removeComponentPrefix, false))
        return;
    if (core::startsWith(source, "scene_property_")) {
        const auto properties = sceneDocument_.inspectorProperties(sceneDocument_.selectedObject());
        for (const auto& property : properties) {
            if (property.kind == SceneInspectorPropertyKind::SceneObjectLinkList
                && property.editable) {
                const auto base = scenePropertyControlSource(property);
                const auto addSource = base + "_link_add";
                const auto sourceView = std::string_view(source.data(), source.size());
                const auto removePrefix = base + "_link_remove_";
                const bool remove = sourceView.compare(0, removePrefix.size(), removePrefix) == 0;
                if (source == addSource || remove) {
                    SetSceneComponentPropertyCommand command(
                        std::string_view(property.name.data(), property.name.size()));
                    command.objectId = property.objectId;
                    command.componentId = property.componentId;
                    command.valueKind = ScenePropertyValueKind::SceneObjectLinkList;
                    command.sceneObjectLinkListValue = property.sceneObjectLinkListValue;
                    if (source == addSource) {
                        if (command.sceneObjectLinkListValue.size()
                            >= kMaxSceneInspectorObjectLinks)
                            return;
                        const auto target = std::find_if(sceneDocument_.scene().objects().begin(),
                            sceneDocument_.scene().objects().end(),
                            [&](const auto& candidate) {
                                return candidate->id().value() != property.objectId;
                            });
                        if (target == sceneDocument_.scene().objects().end())
                            return;
                        std::size_t suffix = command.sceneObjectLinkListValue.size() + 1u;
                        core::pmr::String name("Link", &core::defaultCpuMemoryResource());
                        appendIndex(name, suffix);
                        while (std::any_of(command.sceneObjectLinkListValue.begin(),
                            command.sceneObjectLinkListValue.end(),
                            [&](const auto& link) {
                                return link.name == name;
                            })) {
                            name = "Link";
                            appendIndex(name, ++suffix);
                        }
                        command.sceneObjectLinkListValue.emplace_back();
                        auto& link = command.sceneObjectLinkListValue.back();
                        link.name = std::move(name);
                        link.targetObjectId = (*target)->id().value();
                    } else {
                        const auto indexText = sourceView.substr(removePrefix.size());
                        std::size_t index = 0;
                        const auto parsed = std::from_chars(
                            indexText.data(), indexText.data() + indexText.size(), index);
                        if (parsed.ec != std::errc{}
                            || parsed.ptr != indexText.data() + indexText.size()
                            || index >= command.sceneObjectLinkListValue.size())
                            return;
                        command.sceneObjectLinkListValue.erase(
                            command.sceneObjectLinkListValue.begin()
                            + static_cast<std::ptrdiff_t>(index));
                    }
                    const auto versionBefore = stateVersion();
                    const auto response = submitCommand(std::move(command));
                    if (response.status == EditorResponseStatus::Completed
                        && stateVersion() != versionBefore) {
                        sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                        refreshSceneInspector();
                    }
                    return;
                }
            }
            if (property.kind != SceneInspectorPropertyKind::Vector2List || !property.editable)
                continue;
            const auto base = scenePropertyControlSource(property);
            const auto addSource = base + "_point_add";
            std::size_t removeIndex = 0;
            const auto removePrefix = base + "_point_remove_";
            const auto sourceView = std::string_view(source.data(), source.size());
            const bool remove = sourceView.compare(0,
                                    removePrefix.size(),
                                    std::string_view(removePrefix.data(), removePrefix.size()))
                == 0;
            if (source == addSource || remove) {
                SetSceneComponentPropertyCommand command(
                    std::string_view(property.name.data(), property.name.size()));
                command.objectId = property.objectId;
                command.componentId = property.componentId;
                command.valueKind = ScenePropertyValueKind::Vector2List;
                command.vector2ListValue.assign(
                    property.vector2ListValue.begin(), property.vector2ListValue.end());
                if (source == addSource) {
                    if (command.vector2ListValue.size() == kMaxSceneInspectorVector2ListElements)
                        return;
                    command.vector2ListValue.push_back({});
                } else {
                    const auto indexText = sourceView.substr(removePrefix.size());
                    const auto parsed = std::from_chars(
                        indexText.data(), indexText.data() + indexText.size(), removeIndex);
                    if (parsed.ec != std::errc{}
                        || parsed.ptr != indexText.data() + indexText.size()
                        || removeIndex >= command.vector2ListValue.size())
                        return;
                    command.vector2ListValue.erase(command.vector2ListValue.begin()
                        + static_cast<std::ptrdiff_t>(removeIndex));
                }
                const auto versionBefore = stateVersion();
                const auto response = submitCommand(std::move(command));
                if (response.status == EditorResponseStatus::Completed
                    && stateVersion() != versionBefore) {
                    sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                    refreshSceneInspector();
                }
                return;
            }
        }
    }
    if (source == "files_build_terrain") {
        const auto relative =
            core::FilePath::utf8(selectedFilePath_.lexically_relative(projectRoot()));
        BuildTerrainResourceCommand command;
        command.authoringPath = relative;
        command.packagePath = relative + ".arpkg";
        (void)submitCommand(std::move(command));
    } else if (source == "scene_object_add")
        (void)submitCommand(CreateSceneObjectCommand("GameObject"));
    else if (source == "scene_object_delete")
        (void)submitCommand(DestroySceneObjectCommand{});
    else if (source == "scene_component_remove")
        (void)submitCommand(RemoveSceneComponentCommand{});
    else
        (void)submitCommand(ActivateControlCommand(std::string_view(source.data(), source.size())));
}

void EditorShell::applyActivation(const core::pmr::String& source)
{
    if (source == "tool_1") {
        sceneTransformTool_ = SceneTransformTool::Move;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        feedback("Move tool");
    } else if (source == "tool_2") {
        sceneTransformTool_ = SceneTransformTool::Rotate;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        feedback("Rotate tool");
    } else if (source == "tool_3") {
        sceneTransformTool_ = SceneTransformTool::Scale;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        feedback("Scale tool");
    } else if (source == "tool_4") {
        sceneTransformTool_ = SceneTransformTool::Select;
        sceneGizmoAxis_ = SceneGizmoAxis::None;
        feedback("Select tool");
    } else if (core::startsWith(source, "node_")) {
        presentationState_.select(std::string_view(source.data() + 5, source.size() - 5));
        text("selected_name", presentationState_.selection());
        text("probe_type",
            core::startsWith(presentationState_.selection(), "ReflectionProbe") ? "ReflectionProbe"
                                                                                : "Node3D");
        feedback("Selected " + presentationState_.selection());
    } else if (core::startsWith(source, "workspace_")) {
        presentationState_.setWorkspace(std::string_view(source.data() + 10, source.size() - 10));
        const bool scriptMode = presentationState_.workspace() == "Script";
        visible("perspective", !scriptMode);
        visible("perspective_text", !scriptMode);
        visible("perspective_more", !scriptMode);
        updateWorkspaceChrome();
        if (scriptMode && scriptDocuments_.active())
            scriptFocused_ = true;
        feedback("Workspace: " + presentationState_.workspace());
    } else if (source == "tab_scene" || source == "tab_import") {
        sceneDockTab_ = source == "tab_import" ? "Import" : "Scene";
        updateWorkspaceChrome();
        feedback("Dock tab: " + sceneDockTab_);
    } else if (core::startsWith(source, "scene_") && source != "scene_menu"
        && source != "scene_search") {
        presentationState_.setScene(std::string_view(source.data() + 6, source.size() - 6));
        feedback("Scene tab: " + presentationState_.scene());
    } else if (source == "scene_menu") {
        presentationState_.setScene("menu");
        feedback("Scene tab: menu");
    } else if (source == "tab_files" || source == "tab_history"
        || core::startsWith(source, "dock_")) {
        if (source == "tab_files")
            presentationState_.setDock("FileSystem");
        else if (source == "tab_history")
            presentationState_.setDock("History");
        else
            presentationState_.setDock(std::string_view(source.data() + 5, source.size() - 5));
        updateWorkspaceChrome();
    } else if (core::startsWith(source, "folder_") || core::startsWith(source, "asset_")) {
        std::size_t visibleIndex = std::numeric_limits<std::size_t>::max();
        for (std::size_t slot = 0; slot < kFileSlotCount; ++slot)
            if (source == kFileSlots[slot] && fileScrollOffset_ + slot < visibleFileEntryCount()) {
                visibleIndex = fileScrollOffset_ + slot;
                break;
            }
        if (visibleIndex < visibleFileEntryCount()) {
            const auto& visibleEntries = projectBrowser_.visibleEntryIndices();
            const auto& entries = projectBrowser_.entries();
            const auto entryIndex = visibleEntries[visibleIndex];
            if (entryIndex < entries.size()) {
                const auto& clicked = entries[entryIndex];
                if (clicked.directory) {
                    navigateTo(clicked.path);
                } else {
                    selectedFilePath_ = clicked.path;
                    if (isArpPath(clicked.path)) {
                        const auto now = std::chrono::steady_clock::now();
                        if (lastClickedScriptPath_ == clicked.path
                            && now - lastScriptClickTime_ <= std::chrono::milliseconds(500)) {
                            openScriptFile(clicked.path);
                            lastClickedScriptPath_.clear();
                        } else {
                            lastClickedScriptPath_ = clicked.path;
                            lastScriptClickTime_ = now;
                            feedback("Selected " + clicked.name);
                        }
                    } else
                        feedback("Selected " + clicked.name);
                    updateFileSlots();
                }
            }
        }
    } else if (source == "file_root") {
        navigateTo(projectBrowser_.root());
    } else if (source == "files_back") {
        if (projectBrowser_.back()) {
            selectedFilePath_.clear();
            fileScrollOffset_ = 0;
            lastClickedScriptPath_.clear();
            refreshFileSystem();
        }
    } else if (source == "files_forward") {
        if (projectBrowser_.forward()) {
            selectedFilePath_.clear();
            fileScrollOffset_ = 0;
            lastClickedScriptPath_.clear();
            refreshFileSystem();
        }
    } else if (source == "update_mode") {
        constexpr const char* modes[] = {"Once (Fast)   v", "Always   v", "Disabled   v"};
        const auto mode = presentationState_.advanceUpdateMode();
        auto* w = runtime().canvas().findWidget(id(source));
        w->setText(modes[mode]);
    } else if (core::startsWith(source, "cull_") || core::startsWith(source, "reflection_")
        || core::startsWith(source, "layers_")) {
        presentationState_.toggleDisabledMask(std::string_view(source.data(), source.size()));
    } else if (core::startsWith(source, "bottom_")) {
        presentationState_.toggleOutput();
        visible("output_panel", presentationState_.outputOpen());
        feedback(source.substr(7) + " dock opened");
    } else if (core::startsWith(source, "menu_")) {
        presentationState_.toggleMenu();
        visible("menu_popup", presentationState_.menuOpen());
    } else if (source == "close_menu") {
        presentationState_.closeMenu();
        visible("menu_popup", false);
    } else if (source == "run_play") {
        presentationState_.openOutput();
        visible("output_panel", true);
        presentationState_.closeMenu();
        visible("menu_popup", false);
        if (!gamePreview_.requestStart())
            feedback("Game is already running or starting.");
        else {
            feedback("Starting game preview...");
        }
    } else if (source == "run_stop") {
        presentationState_.openOutput();
        visible("output_panel", true);
        presentationState_.closeMenu();
        visible("menu_popup", false);
        if (!gamePreview_.requestStop())
            feedback("Game is not running.");
        else {
            feedback("Stopping game preview...");
        }
    } else if (source == "run_pause") {
        presentationState_.openOutput();
        visible("output_panel", true);
        feedback("Pause is not supported by this game runtime.");
    } else if (source == "save_scene" || source == "open_scene") {
        presentationState_.openOutput();
        visible("output_panel", true);
        presentationState_.closeMenu();
        visible("menu_popup", false);
        feedback("UI prototype: " + source + " has no project command backend.");
    } else
        feedback("Activated: " + source);
}

void EditorShell::filter(const core::pmr::String& prefix, const core::pmr::String& query)
{
    for (const auto& n : loaded_.document().nodes())
        if (n.controlType == ui::UiControlType::Button
            && core::startsWith(n.sourceId, prefix.c_str())) {
            const bool match = query.empty() || containsIgnoringCase(n.text, query);
            visible(n.sourceId, match);
            if (prefix == "node_") {
                const auto name = n.sourceId.substr(5);
                visible("node_icon_" + name, match);
                visible("node_eye_" + name, match);
                const auto branch = id("branch_" + name);
                if (branch.valid())
                    visible("branch_" + name, match);
            }
            if (prefix == "folder_") {
                const auto name = n.sourceId.substr(7);
                visible("folder_icon_" + name, match);
                visible("folder_arrow_" + name, match);
            }
        }
}

void EditorShell::entryChanged(const core::pmr::String& source, const core::pmr::String& value)
{
    if (core::startsWith(source, "scene_property_")) {
        const auto properties = sceneDocument_.inspectorProperties(sceneDocument_.selectedObject());
        const auto property =
            std::find_if(properties.begin(), properties.end(), [&](const auto& item) {
                const auto base = scenePropertyControlSource(item);
                if (base == source)
                    return true;
                if ((item.kind == SceneInspectorPropertyKind::Vector2
                        || item.kind == SceneInspectorPropertyKind::Vector3)
                    && source.size() == base.size() + 2u
                    && source.compare(0, base.size(), base) == 0 && source[base.size()] == '_')
                    return source.back() == 'x' || source.back() == 'y'
                        || (item.kind == SceneInspectorPropertyKind::Vector3
                            && source.back() == 'z');
                if (item.kind != SceneInspectorPropertyKind::Vector2List) {
                    if (item.kind != SceneInspectorPropertyKind::SceneObjectLinkList)
                        return false;
                    const auto basePrefix = base + "_link_";
                    const auto sourceView = std::string_view(source.data(), source.size());
                    if (sourceView.compare(0, basePrefix.size(), basePrefix) != 0)
                        return false;
                    auto rest = sourceView.substr(basePrefix.size());
                    const auto separator = rest.find('_');
                    if (separator == std::string_view::npos)
                        return false;
                    const auto field = rest.substr(0, separator);
                    if (field != "name" && field != "target")
                        return false;
                    const auto indexText = rest.substr(separator + 1u);
                    std::size_t index = 0;
                    const auto parsed = std::from_chars(
                        indexText.data(), indexText.data() + indexText.size(), index);
                    return parsed.ec == std::errc{}
                    && parsed.ptr == indexText.data() + indexText.size()
                        && index < item.sceneObjectLinkListValue.size();
                }
                const auto pointPrefix = base + "_point_";
                const auto sourceView = std::string_view(source.data(), source.size());
                if (sourceView.compare(0,
                        pointPrefix.size(),
                        std::string_view(pointPrefix.data(), pointPrefix.size()))
                    != 0)
                    return false;
                const auto axisSeparator = sourceView.rfind('_');
                if (axisSeparator == std::string_view::npos
                    || axisSeparator + 2u != sourceView.size()
                    || (sourceView.back() != 'x' && sourceView.back() != 'y'))
                    return false;
                const auto indexText =
                    sourceView.substr(pointPrefix.size(), axisSeparator - pointPrefix.size());
                std::size_t index = 0;
                const auto parsed =
                    std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
                return parsed.ec == std::errc{} && parsed.ptr == indexText.data() + indexText.size()
                    && index < item.vector2ListValue.size();
            });
        if (property == properties.end() || !property->editable)
            return;
        if (property->componentType == "TransformComponent") {
            const auto selected = sceneDocument_.selectedObject();
            const auto* object = sceneDocument_.scene().findObject(
                scene::GameObjectId::fromValue(property->objectId));
            char* end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            if (!object || selected.value() != property->objectId || end == value.c_str()
                || *end != '\0' || !std::isfinite(parsed) || source.empty()) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                return;
            }
            auto transform = object->transform().localTransform();
            const char axis = source.back();
            if (property->name == "position") {
                if (axis == 'x')
                    transform.translation.x = parsed;
                else if (axis == 'y')
                    transform.translation.y = parsed;
                else if (axis == 'z')
                    transform.translation.z = parsed;
                else
                    return;
            } else if (property->name == "scale") {
                if (axis == 'x')
                    transform.scale.x = parsed;
                else if (axis == 'y')
                    transform.scale.y = parsed;
                else if (axis == 'z')
                    transform.scale.z = parsed;
                else
                    return;
            } else if (property->name == "rotation") {
                float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
                if (!core::quaternionToYawPitchRollRadians(transform.rotation, yaw, pitch, roll)) {
                    sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                    refreshSceneInspector();
                    return;
                }
                constexpr float degreesPerRadian = 180.0f / 3.14159265358979f;
                yaw *= degreesPerRadian;
                pitch *= degreesPerRadian;
                roll *= degreesPerRadian;
                if (axis == 'x')
                    pitch = parsed;
                else if (axis == 'y')
                    yaw = parsed;
                else if (axis == 'z')
                    roll = parsed;
                else
                    return;
                transform.rotation = core::Quat::fromYawPitchRoll(yaw, pitch, roll);
            } else
                return;

            SetSceneObjectTransformCommand command;
            command.translation = transform.translation;
            command.rotation = transform.rotation;
            command.scale = transform.scale;
            sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
            const auto response = submitCommand(command);
            if (response.status != EditorResponseStatus::Completed) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
            }
            return;
        }
        SetSceneComponentPropertyCommand command(
            std::string_view(property->name.data(), property->name.size()));
        command.objectId = property->objectId;
        command.componentId = property->componentId;
        if (property->kind == SceneInspectorPropertyKind::Boolean) {
            if (value == "true")
                command.booleanValue = true;
            else if (value == "false")
                command.booleanValue = false;
            else {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Expected true or false");
                return;
            }
            command.valueKind = ScenePropertyValueKind::Boolean;
        } else if (property->kind == SceneInspectorPropertyKind::Integer) {
            int parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Invalid integer property value");
                return;
            }
            command.integerValue = parsed;
            command.valueKind = ScenePropertyValueKind::Integer;
        } else if (property->kind == SceneInspectorPropertyKind::UnsignedInteger) {
            std::uint64_t parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Invalid unsigned integer property value");
                return;
            }
            command.unsignedIntegerValue = parsed;
            command.valueKind = ScenePropertyValueKind::UnsignedInteger;
        } else if (property->kind == SceneInspectorPropertyKind::Number) {
            char* end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Invalid numeric property value");
                return;
            }
            command.numberValue = parsed;
            command.valueKind = ScenePropertyValueKind::Number;
        } else if (property->kind == SceneInspectorPropertyKind::Enumeration) {
            const auto option = std::find_if(property->enumOptions.begin(),
                property->enumOptions.end(),
                [&](const auto& candidate) {
                    return candidate.name == value;
                });
            if (option == property->enumOptions.end()) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Unknown enum value");
                return;
            }
            command.integerValue = option->value;
            command.valueKind = ScenePropertyValueKind::Enumeration;
        } else if (property->kind == SceneInspectorPropertyKind::Vector2
            || property->kind == SceneInspectorPropertyKind::Vector3) {
            char* end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Invalid vector component");
                return;
            }
            const auto axis = source.back();
            if (property->kind == SceneInspectorPropertyKind::Vector2) {
                command.vector2Value = property->vector2Value;
                if (axis == 'x')
                    command.vector2Value.x = parsed;
                else if (axis == 'y')
                    command.vector2Value.y = parsed;
                else
                    return;
                command.valueKind = ScenePropertyValueKind::Vector2;
            } else {
                command.vector3Value = property->vector3Value;
                if (axis == 'x')
                    command.vector3Value.x = parsed;
                else if (axis == 'y')
                    command.vector3Value.y = parsed;
                else
                    command.vector3Value.z = parsed;
                command.valueKind = ScenePropertyValueKind::Vector3;
            }
        } else if (property->kind == SceneInspectorPropertyKind::Vector2List) {
            const auto base = scenePropertyControlSource(*property);
            const auto pointPrefix = base + "_point_";
            const auto sourceView = std::string_view(source.data(), source.size());
            const auto axisSeparator = sourceView.rfind('_');
            if (sourceView.compare(
                    0, pointPrefix.size(), std::string_view(pointPrefix.data(), pointPrefix.size()))
                    != 0
                || axisSeparator == std::string_view::npos
                || axisSeparator + 2u != sourceView.size())
                return;
            const auto indexText =
                sourceView.substr(pointPrefix.size(), axisSeparator - pointPrefix.size());
            std::size_t index = 0;
            const auto parsedIndex =
                std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
            if (parsedIndex.ec != std::errc{}
                || parsedIndex.ptr != indexText.data() + indexText.size()
                || index >= property->vector2ListValue.size())
                return;
            char* end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Invalid point coordinate");
                return;
            }
            command.vector2ListValue.assign(
                property->vector2ListValue.begin(), property->vector2ListValue.end());
            if (sourceView[axisSeparator + 1u] == 'x')
                command.vector2ListValue[index].x = parsed;
            else if (sourceView[axisSeparator + 1u] == 'y')
                command.vector2ListValue[index].y = parsed;
            else
                return;
            command.valueKind = ScenePropertyValueKind::Vector2List;
        } else if (property->kind == SceneInspectorPropertyKind::String) {
            if (value.size() > 512u) {
                sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
                refreshSceneInspector();
                feedback("Resource path exceeds 512 bytes");
                return;
            }
            command.stringValue = value;
            command.valueKind = ScenePropertyValueKind::String;
        } else if (property->kind == SceneInspectorPropertyKind::ResourceReference) {
            command.stringValue = value;
            command.valueKind = ScenePropertyValueKind::ResourceReference;
        } else if (property->kind == SceneInspectorPropertyKind::SceneObjectReference) {
            std::uint64_t objectId = 0;
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), objectId);
            if (value.empty() || parsed.ec != std::errc{}
                || parsed.ptr != value.data() + value.size()) {
                if (!value.empty())
                    return;
                objectId = 0;
            }
            command.sceneObjectReferenceValue = objectId;
            command.valueKind = ScenePropertyValueKind::SceneObjectReference;
        } else if (property->kind == SceneInspectorPropertyKind::SceneObjectLinkList) {
            const auto sourceView = std::string_view(source.data(), source.size());
            const auto marker = sourceView.rfind("_link_");
            if (marker == std::string_view::npos)
                return;
            const auto fieldStart = marker + 6u;
            const auto separator = sourceView.find('_', fieldStart);
            if (separator == std::string_view::npos)
                return;
            const auto field = sourceView.substr(fieldStart, separator - fieldStart);
            const auto indexText = sourceView.substr(separator + 1u);
            std::size_t index = 0;
            const auto parsedIndex =
                std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
            if (parsedIndex.ec != std::errc{}
                || parsedIndex.ptr != indexText.data() + indexText.size()
                || index >= property->sceneObjectLinkListValue.size())
                return;
            command.sceneObjectLinkListValue = property->sceneObjectLinkListValue;
            if (field == "name") {
                if (value.empty() || value.size() > 128u)
                    return;
                command.sceneObjectLinkListValue[index].name = value;
            } else if (field == "target") {
                std::uint64_t targetId = 0;
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), targetId);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                    || targetId == 0u)
                    return;
                command.sceneObjectLinkListValue[index].targetObjectId = targetId;
            } else
                return;
            command.valueKind = ScenePropertyValueKind::SceneObjectLinkList;
        } else
            return;
        (void)submitCommand(std::move(command));
    } else if (source == "scene_name")
        (void)submitCommand(
            SetSceneObjectNameCommand(std::string_view(value.data(), value.size())));
    else
        (void)submitCommand(SetTextValueCommand(std::string_view(source.data(), source.size()),
            std::string_view(value.data(), value.size())));
}

void EditorShell::colorEntryChanged(
    const core::pmr::String& source, const ui::UiControlColorValue& value)
{
    const auto properties = sceneDocument_.inspectorProperties(sceneDocument_.selectedObject());
    const auto property =
        std::find_if(properties.begin(), properties.end(), [&](const auto& candidate) {
            return scenePropertyControlSource(candidate) == source;
        });
    if (property == properties.end() || property->kind != SceneInspectorPropertyKind::Color4
        || !property->editable)
        return;

    SetSceneComponentPropertyCommand command(
        std::string_view(property->name.data(), property->name.size()));
    command.objectId = property->objectId;
    command.componentId = property->componentId;
    command.valueKind = ScenePropertyValueKind::Color4;
    command.color4Value = {value.r, value.g, value.b, value.a};
    (void)submitCommand(std::move(command));
}

void EditorShell::applyEntryChanged(const core::pmr::String& source, const core::pmr::String& value)
{
    if (source == "scene_search") {
        sceneHierarchySearch_ = value;
        sceneHierarchyScrollOffset_ = 0u;
        sceneHierarchyDraggingScrollbar_ = false;
        hierarchyDragObject_ = scene::GameObjectId::invalid();
        hierarchyDragTarget_ = scene::GameObjectId::invalid();
        hierarchyDragging_ = false;
        visible("scene_search_hint", value.empty());
        if (presentationState_.workspace() != "3D")
            filter("node_", value);
    } else if (source == "file_search") {
        projectBrowser_.setSearch(value);
        fileScrollOffset_ = 0;
        visible("file_search_hint", value.empty());
        updateFileSlots();
    } else if (source == "property_search") {
        visible("property_search_hint", value.empty());
        // Filter complete property rows, preserving their labels and field bindings.
        constexpr const char* rows[] = {
            "intensity", "blend", "max_distance", "lod", "projection", "interior", "shadows"};
        for (const char* row : rows) {
            auto key = core::pmr::String(row);
            auto* w = runtime().canvas().findWidget(id(key + "_label"));
            bool match = value.empty() || containsIgnoringCase(w->desc().text, value);
            visible(key, match);
            visible(key + "_label", match);
            for (const char* suffix : {"_line", "_slider_thumb", "_background", "_on"})
                if (id(key + suffix).valid())
                    visible(key + suffix, match);
        }
    } else
        feedback(presentationState_.selection() + " / " + source + " = " + value);
}

void EditorShell::updateWorkspaceChrome()
{
    const bool scriptMode = presentationState_.workspace() == "Script";
    const bool sceneMode = presentationState_.workspace() == "3D";
    const bool legacyInspector =
        !scriptMode && !sceneMode && presentationState_.dock() == "Inspector";
    visible("toolbar", !scriptMode);
    for (unsigned index = 0; index <= 12; ++index) {
        char name[32];
        std::snprintf(name, sizeof(name), "tool_%u", index);
        visible(name, !scriptMode);
        std::snprintf(name, sizeof(name), "tool_icon_%u", index);
        const bool transformToolIcon = index >= 1u && index <= 4u;
        visible(name, !scriptMode && (!sceneMode || transformToolIcon));
    }
    for (const char* name : {"toolbar_dots", "transform", "view"})
        visible(name, !scriptMode && !sceneMode);
    for (const char* name : {"scene_menu",
             "scene_level",
             "scene_player",
             "scene_menu_icon",
             "scene_level_icon",
             "scene_player_icon",
             "scene_level_close",
             "scene_dots",
             "scene_plus"})
        visible(name, !scriptMode && !sceneMode);
    visible("tab_scene", !scriptMode);
    visible("tab_import", !scriptMode);
    const bool showSceneSearch = !scriptMode && (!sceneMode || sceneDockTab_ == "Scene");
    for (const char* name : {"scene_search", "scene_search_hint", "scene_search_icon"})
        visible(name, showSceneSearch);
    for (const char* name : {"scene_object_add",
             "scene_object_delete",
             "scene_component_add",
             "scene_name",
             "scene_name_label",
             "scene_transform_header"})
        if (id(name).valid())
            visible(name, sceneMode);
    for (const auto& node : loaded_.document().nodes())
        if (core::startsWith(node.sourceId, "node_")
            || core::startsWith(node.sourceId, "node_icon_")
            || core::startsWith(node.sourceId, "node_eye_")
            || core::startsWith(node.sourceId, "branch_"))
            visible(node.sourceId, !sceneMode);
    visible("scene_add", !sceneMode);
    visible("scene_more", !sceneMode);
    visible("properties", presentationState_.dock() == "Inspector");
    for (const char* name : {"property_search", "property_search_hint", "property_search_icon"})
        visible(name, legacyInspector);
    visible("inspector_scroll_track", presentationState_.dock() == "Inspector");
    visible("inspector_scroll", presentationState_.dock() == "Inspector");
    for (const auto widgetId : staticInspectorWidgetIds_)
        (void)runtime().setVisible(widgetId, !sceneMode);
    for (const char* name : {"selected_header", "selected_icon", "selected_name"})
        visible(name, sceneMode && presentationState_.dock() == "Inspector");
    if (!sceneMode) {
        text("selected_name", "ReflectionProbe2");
        if (auto* selectedIcon = runtime().canvas().findWidget(id("selected_icon"))) {
            ui::LayoutRect sourceRect;
            selectedIcon->setTextureResourceId(
                core::pmr::String("icon-probe", &core::defaultCpuMemoryResource()));
            if (iconSourceRect("icon-probe", sourceRect))
                selectedIcon->setTextureSourceRect(sourceRect);
        }
    }

    if (auto* viewport = runtime().canvas().findWidget(id("viewport"))) {
        auto rect = gameViewportRect_;
        if (scriptMode) {
            rect.y -= 39.0f;
            rect.height += 39.0f;
        }
        viewport->setLocalRect(rect);
        viewport->setVisible(!scriptMode && !sceneMode);
    }
    if (sceneMode)
        refreshSceneInspector();
    for (const auto widgetId : sceneInspectorWidgetIds_)
        (void)runtime().setVisible(widgetId, sceneMode);
}

void EditorShell::refreshSceneInspector()
{
    const auto* object = sceneDocument_.scene().findObject(sceneDocument_.selectedObject());
    if (!object)
        return;
    text("selected_name", object->name());
    text("scene_name", object->name());
    if (auto* selectedIcon = runtime().canvas().findWidget(id("selected_icon"))) {
        std::string_view icon = "icon-node";
        for (const auto& component : object->components()) {
            const auto candidate = componentIcon(component->scriptTypeName());
            if (candidate != "icon-node") {
                icon = candidate;
                break;
            }
        }
        selectedIcon->setTextureResourceId(
            core::pmr::String(icon, &core::defaultCpuMemoryResource()));
        ui::LayoutRect sourceRect;
        if (iconSourceRect(icon, sourceRect))
            selectedIcon->setTextureSourceRect(sourceRect);
    }
    std::size_t componentCount = 0;
    for (const auto& component : object->components())
        if (component->scriptTypeName() != std::string_view("TransformComponent"))
            ++componentCount;
    core::pmr::String header("Transform  |  ", &core::defaultCpuMemoryResource());
    const auto count = std::to_string(componentCount);
    header.append(count.data(), count.size());
    header += " components";
    text("scene_transform_header", std::move(header));

    if (sceneInspectorWidgetOwner_ == object->id())
        return;
    for (const auto widgetId : sceneInspectorWidgetIds_)
        if (auto* control =
                dynamic_cast<EditorEnumDropdown*>(runtime().findControlObject(widgetId)))
            control->closeMenu();
    for (const auto widgetId : sceneInspectorWidgetIds_) {
        (void)runtime().removeControl(widgetId);
        presentationState_.unregisterSource(widgetId.value());
    }
    sceneInspectorWidgetIds_.clear();
    const auto createInspectorIcon =
        [&](std::string_view source, std::string_view resource, float x, float y, float size) {
            ui::LayoutRect sourceRect;
            if (!iconSourceRect(resource, sourceRect))
                return;
            ui::UiControlDesc desc;
            desc.type = ui::UiControlType::Image;
            desc.widget.name.assign(source.data(), source.size());
            desc.widget.localRect = ui::LayoutRect::fromOriginSize(x, y, size, size);
            desc.widget.styleId = "image";
            desc.widget.textureResourceId.assign(resource.data(), resource.size());
            desc.widget.textureSourceRect = sourceRect;
            desc.widget.hasTextureSourceRect = true;
            desc.widget.acceptsPointerInput = false;
            const auto created = runtime().createControl(std::move(desc), id("inspector_content"));
            if (!created.ok())
                return;
            sceneInspectorWidgetIds_.push_back(created.widgetId());
            const core::pmr::String registeredSource(source, &core::defaultCpuMemoryResource());
            presentationState_.registerSource(created.widgetId().value(), registeredSource);
        };
    createInspectorIcon("scene_transform_icon", "icon-tool-move", 6.0f, 72.0f, 16.0f);
    if (collapsedInspectorOwner_ != object->id()) {
        collapsedInspectorComponents_.clear();
        collapsedInspectorOwner_ = object->id();
    }
    sceneInspectorWidgetOwner_ = object->id();

    auto properties = buildSceneInspectorProperties(
        sceneDocument_, detail::sceneResourceManager(sceneDocument_), object->id());
    constexpr const char* axes[] = {"x", "y", "z"};
    float transformRowY = 101.0f;
    for (const auto& property : properties) {
        if (property.componentType != "TransformComponent"
            || property.kind != SceneInspectorPropertyKind::Vector3 || !property.editable)
            continue;
        const auto source = scenePropertyControlSource(property);
        ui::UiControlDesc labelDesc;
        labelDesc.type = ui::UiControlType::Label;
        labelDesc.widget.name = source + "_label";
        labelDesc.widget.localRect =
            ui::LayoutRect::fromOriginSize(6.0f, transformRowY, 114.0f, 24.0f);
        labelDesc.widget.styleId = "text";
        labelDesc.widget.fontToken = "font.editor";
        labelDesc.widget.text = property.name == "position" ? "Position"
            : property.name == "rotation"                   ? "Rotation"
                                                            : "Scale";
        const auto labelCreated =
            runtime().createControl(std::move(labelDesc), id("inspector_content"));
        if (labelCreated.ok()) {
            sceneInspectorWidgetIds_.push_back(labelCreated.widgetId());
            presentationState_.registerSource(labelCreated.widgetId().value(), source + "_label");
        }

        const float values[] = {
            property.vector3Value.x, property.vector3Value.y, property.vector3Value.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto axisLabelSource = source + "_" + axes[axis] + "_label";
            ui::UiControlDesc axisLabelDesc;
            axisLabelDesc.type = ui::UiControlType::Label;
            axisLabelDesc.widget.name = axisLabelSource;
            axisLabelDesc.widget.localRect = ui::LayoutRect::fromOriginSize(
                108.0f + static_cast<float>(axis) * 76.0f, transformRowY, 12.0f, 24.0f);
            axisLabelDesc.widget.styleId = axis == 0u ? "axis-x" : axis == 1u ? "axis-y" : "axis-z";
            axisLabelDesc.widget.fontToken = "font.editor.semibold";
            axisLabelDesc.widget.text = axes[axis];
            const auto axisLabelCreated =
                runtime().createControl(std::move(axisLabelDesc), id("inspector_content"));
            if (axisLabelCreated.ok()) {
                sceneInspectorWidgetIds_.push_back(axisLabelCreated.widgetId());
                presentationState_.registerSource(
                    axisLabelCreated.widgetId().value(), axisLabelSource);
            }
            char displayedComponent[48];
            std::snprintf(displayedComponent,
                sizeof(displayedComponent),
                "%.6g",
                static_cast<double>(values[axis]));
            const auto axisSource = source + "_" + axes[axis];
            ui::UiControlDesc componentDesc;
            componentDesc.type = ui::UiControlType::TextInput;
            componentDesc.widget.name = axisSource;
            componentDesc.widget.localRect = ui::LayoutRect::fromOriginSize(
                120.0f + static_cast<float>(axis) * 76.0f, transformRowY, 54.0f, 24.0f);
            componentDesc.widget.styleId = "input";
            componentDesc.widget.fontToken = "font.editor";
            componentDesc.widget.text = displayedComponent;
            componentDesc.initialValue = ui::UiControlValue::fromText(
                core::pmr::String(displayedComponent, &core::defaultCpuMemoryResource()));
            const auto created =
                runtime().createControl(core::makeUnique<EditorEntry>(*this, axisSource),
                    std::move(componentDesc),
                    id("inspector_content"));
            if (created.ok()) {
                sceneInspectorWidgetIds_.push_back(created.widgetId());
                presentationState_.registerSource(created.widgetId().value(), axisSource);
            }
        }
        transformRowY += 32.0f;
    }

    float rowY = 248.0f;
    std::size_t dynamicRowCount = 0;
    std::size_t vector2ListPointCount = 0;
    std::uint64_t lastComponentId = 0;
    bool capacityExceeded = false;
    for (const auto& property : properties) {
        if (property.componentType == "TransformComponent")
            continue;
        if (property.componentId != lastComponentId) {
            lastComponentId = property.componentId;
            std::string_view componentTitle(
                property.componentType.data(), property.componentType.size());
            for (const auto& entry : EditorSceneDocument::componentCatalog()) {
                if (entry.typeName == componentTitle) {
                    componentTitle = entry.displayName;
                    break;
                }
            }
            core::pmr::String headerSource(
                "scene_component_header_", &core::defaultCpuMemoryResource());
            headerSource += std::to_string(property.objectId);
            headerSource.push_back('_');
            headerSource += std::to_string(property.componentId);
            core::pmr::String removeSource(
                "scene_component_remove_", &core::defaultCpuMemoryResource());
            removeSource += std::to_string(property.objectId);
            removeSource.push_back('_');
            removeSource += std::to_string(property.componentId);
            core::pmr::String toggleSource(
                "scene_component_toggle_", &core::defaultCpuMemoryResource());
            toggleSource += std::to_string(property.objectId);
            toggleSource.push_back('_');
            toggleSource += std::to_string(property.componentId);
            const bool collapsed = std::any_of(collapsedInspectorComponents_.begin(),
                collapsedInspectorComponents_.end(),
                [&](const auto& item) {
                    return item.objectId == property.objectId
                        && item.componentId == property.componentId;
                });
            ui::UiControlDesc headerDesc;
            headerDesc.type = ui::UiControlType::Button;
            headerDesc.widget.name = headerSource;
            headerDesc.widget.localRect = ui::LayoutRect::fromOriginSize(6.0f, rowY, 226.0f, 25.0f);
            headerDesc.widget.styleId = "section";
            headerDesc.widget.fontToken = "font.editor";
            ui::UiControlDesc titleDesc;
            titleDesc.type = ui::UiControlType::Label;
            titleDesc.widget.name = headerSource + "_title";
            titleDesc.widget.localRect = ui::LayoutRect::fromOriginSize(50.0f, rowY, 176.0f, 25.0f);
            titleDesc.widget.styleId = "text";
            titleDesc.widget.fontToken = "font.editor";
            titleDesc.widget.text.assign(componentTitle.data(), componentTitle.size());
            titleDesc.widget.acceptsPointerInput = false;
            const auto headerCreated =
                runtime().createControl(core::makeUnique<EditorAction>(*this, toggleSource),
                    std::move(headerDesc),
                    id("inspector_content"));
            if (headerCreated.ok()) {
                sceneInspectorWidgetIds_.push_back(headerCreated.widgetId());
                presentationState_.registerSource(headerCreated.widgetId().value(), headerSource);
            }
            const auto titleCreated =
                runtime().createControl(std::move(titleDesc), id("inspector_content"));
            if (titleCreated.ok()) {
                sceneInspectorWidgetIds_.push_back(titleCreated.widgetId());
                presentationState_.registerSource(
                    titleCreated.widgetId().value(), headerSource + "_title");
            }
            const auto disclosureIconSource = headerSource + "_disclosure_icon";
            createInspectorIcon(disclosureIconSource,
                collapsed ? "icon-chevron-right" : "icon-chevron-down",
                8.0f,
                rowY + 4.0f,
                16.0f);
            const auto typeIconSource = headerSource + "_type_icon";
            createInspectorIcon(
                typeIconSource, componentIcon(property.componentType), 28.0f, rowY + 4.0f, 16.0f);
            ui::UiControlDesc removeDesc;
            removeDesc.type = ui::UiControlType::Button;
            removeDesc.widget.name = removeSource;
            removeDesc.widget.localRect =
                ui::LayoutRect::fromOriginSize(236.0f, rowY, 54.0f, 23.0f);
            removeDesc.widget.styleId = "button";
            removeDesc.widget.fontToken = "font.editor";
            removeDesc.widget.text = "Remove";
            const auto removeCreated =
                runtime().createControl(core::makeUnique<EditorAction>(*this, removeSource),
                    std::move(removeDesc),
                    id("inspector_content"));
            if (removeCreated.ok()) {
                sceneInspectorWidgetIds_.push_back(removeCreated.widgetId());
                presentationState_.registerSource(removeCreated.widgetId().value(), removeSource);
            }
            rowY += 25.0f;
        }
        const bool componentCollapsed = std::any_of(collapsedInspectorComponents_.begin(),
            collapsedInspectorComponents_.end(),
            [&](const auto& item) {
                return item.objectId == property.objectId
                    && item.componentId == property.componentId;
            });
        if (componentCollapsed)
            continue;
        if (dynamicRowCount == kSceneInspectorDynamicRows) {
            capacityExceeded = true;
            break;
        }
        ++dynamicRowCount;
        const auto source = scenePropertyControlSource(property);
        if (!property.editable) {
            ui::UiControlDesc desc;
            desc.type = ui::UiControlType::Label;
            desc.widget.name = source;
            desc.widget.localRect = ui::LayoutRect::fromOriginSize(6.0f, rowY, 288.0f, 21.0f);
            desc.widget.styleId = "muted";
            desc.widget.fontToken = "font.editor";
            if (property.kind == SceneInspectorPropertyKind::ResourceReference) {
                desc.widget.text = property.resourceType;
                desc.widget.text += ": ";
                desc.widget.text +=
                    property.stringValue.empty() ? "<unassigned>" : property.stringValue;
            } else {
                const std::string_view propertyName(property.name.data(), property.name.size());
                const auto separator = propertyName.rfind('.');
                const auto displayName = separator == std::string_view::npos
                    ? propertyName
                    : propertyName.substr(separator + 1u);
                desc.widget.text =
                    core::pmr::String(displayName, &core::defaultCpuMemoryResource());
                desc.widget.text += ": ";
                if (property.kind == SceneInspectorPropertyKind::Unsupported) {
                    desc.widget.text +=
                        property.typeName.size() > 28 ? "unsupported type" : property.typeName;
                } else {
                    desc.widget.text += sceneInspectorDisplayValue(property);
                }
                desc.widget.text += property.kind == SceneInspectorPropertyKind::Unsupported
                    ? " (unsupported)"
                    : " (read only)";
            }
            const auto created = runtime().createControl(std::move(desc), id("inspector_content"));
            if (created.ok()) {
                sceneInspectorWidgetIds_.push_back(created.widgetId());
                presentationState_.registerSource(created.widgetId().value(), source);
            }
            rowY += 23.0f;
            if (property.kind == SceneInspectorPropertyKind::ResourceReference
                && !property.resourceDiagnostic.empty()) {
                const auto diagnosticSource = source + "_diagnostic";
                ui::UiControlDesc diagnosticDesc;
                diagnosticDesc.type = ui::UiControlType::Label;
                diagnosticDesc.widget.name = diagnosticSource;
                diagnosticDesc.widget.localRect =
                    ui::LayoutRect::fromOriginSize(6.0f, rowY, 288.0f, 30.0f);
                diagnosticDesc.widget.styleId = "muted";
                diagnosticDesc.widget.fontToken = "font.editor";
                diagnosticDesc.widget.text = property.resourceDiagnostic;
                const auto diagnosticCreated =
                    runtime().createControl(std::move(diagnosticDesc), id("inspector_content"));
                if (diagnosticCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(diagnosticCreated.widgetId());
                    presentationState_.registerSource(
                        diagnosticCreated.widgetId().value(), diagnosticSource);
                }
                rowY += 30.0f;
            }
            continue;
        }

        ui::UiControlDesc labelDesc;
        labelDesc.type = ui::UiControlType::Label;
        labelDesc.widget.name = source + "_label";
        labelDesc.widget.localRect = ui::LayoutRect::fromOriginSize(6.0f, rowY, 114.0f, 24.0f);
        labelDesc.widget.styleId = "text";
        labelDesc.widget.fontToken = "font.editor";
        const std::string_view propertyName(property.name.data(), property.name.size());
        const auto separator = propertyName.rfind('.');
        const auto displayName = separator == std::string_view::npos
            ? propertyName
            : propertyName.substr(separator + 1u);
        labelDesc.widget.text = core::pmr::String(displayName, &core::defaultCpuMemoryResource());
        const auto labelCreated =
            runtime().createControl(std::move(labelDesc), id("inspector_content"));
        if (labelCreated.ok()) {
            sceneInspectorWidgetIds_.push_back(labelCreated.widgetId());
            presentationState_.registerSource(labelCreated.widgetId().value(), source + "_label");
        }

        if (property.kind == SceneInspectorPropertyKind::Vector3) {
            const float values[] = {
                property.vector3Value.x, property.vector3Value.y, property.vector3Value.z};
            constexpr const char* axes[] = {"x", "y", "z"};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto axisLabelSource = source + "_" + axes[axis] + "_label";
                ui::UiControlDesc axisLabelDesc;
                axisLabelDesc.type = ui::UiControlType::Label;
                axisLabelDesc.widget.name = axisLabelSource;
                axisLabelDesc.widget.localRect = ui::LayoutRect::fromOriginSize(
                    108.0f + static_cast<float>(axis) * 76.0f, rowY, 12.0f, 24.0f);
                axisLabelDesc.widget.styleId = axis == 0u ? "axis-x"
                    : axis == 1u                          ? "axis-y"
                                                          : "axis-z";
                axisLabelDesc.widget.fontToken = "font.editor.semibold";
                axisLabelDesc.widget.text = axes[axis];
                const auto axisLabelCreated =
                    runtime().createControl(std::move(axisLabelDesc), id("inspector_content"));
                if (axisLabelCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(axisLabelCreated.widgetId());
                    presentationState_.registerSource(
                        axisLabelCreated.widgetId().value(), axisLabelSource);
                }
                char displayedComponent[48];
                std::snprintf(displayedComponent,
                    sizeof(displayedComponent),
                    "%.6g",
                    static_cast<double>(values[axis]));
                const auto axisSource = source + "_" + axes[axis];
                ui::UiControlDesc componentDesc;
                componentDesc.type = ui::UiControlType::TextInput;
                componentDesc.widget.name = axisSource;
                componentDesc.widget.localRect = ui::LayoutRect::fromOriginSize(
                    120.0f + static_cast<float>(axis) * 76.0f, rowY, 54.0f, 24.0f);
                componentDesc.widget.styleId = "input";
                componentDesc.widget.fontToken = "font.editor";
                componentDesc.widget.text = displayedComponent;
                componentDesc.initialValue = ui::UiControlValue::fromText(
                    core::pmr::String(displayedComponent, &core::defaultCpuMemoryResource()));
                const auto componentCreated =
                    runtime().createControl(core::makeUnique<EditorEntry>(*this, axisSource),
                        std::move(componentDesc),
                        id("inspector_content"));
                if (componentCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(componentCreated.widgetId());
                    presentationState_.registerSource(
                        componentCreated.widgetId().value(), axisSource);
                }
            }
            rowY += 25.0f;
            continue;
        }

        if (property.kind == SceneInspectorPropertyKind::Vector2) {
            const float values[] = {property.vector2Value.x, property.vector2Value.y};
            constexpr const char* axes[] = {"x", "y"};
            for (std::size_t axis = 0; axis < 2; ++axis) {
                char displayedComponent[48];
                std::snprintf(displayedComponent,
                    sizeof(displayedComponent),
                    "%.6g",
                    static_cast<double>(values[axis]));
                const auto axisSource = source + "_" + axes[axis];
                ui::UiControlDesc componentDesc;
                componentDesc.type = ui::UiControlType::TextInput;
                componentDesc.widget.name = axisSource;
                componentDesc.widget.localRect = ui::LayoutRect::fromOriginSize(
                    126.0f + static_cast<float>(axis) * 53.0f, rowY, 49.0f, 24.0f);
                componentDesc.widget.styleId = "input";
                componentDesc.widget.fontToken = "font.editor";
                componentDesc.widget.text = displayedComponent;
                componentDesc.initialValue = ui::UiControlValue::fromText(
                    core::pmr::String(displayedComponent, &core::defaultCpuMemoryResource()));
                const auto componentCreated =
                    runtime().createControl(core::makeUnique<EditorEntry>(*this, axisSource),
                        std::move(componentDesc),
                        id("inspector_content"));
                if (componentCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(componentCreated.widgetId());
                    presentationState_.registerSource(
                        componentCreated.widgetId().value(), axisSource);
                }
            }
            rowY += 25.0f;
            continue;
        }

        if (property.kind == SceneInspectorPropertyKind::Vector2List) {
            const auto pointCount = std::min(property.vector2ListValue.size(),
                kSceneInspectorVector2ListPointBudget - vector2ListPointCount);
            for (std::size_t index = 0; index < pointCount; ++index) {
                auto pointSource = source;
                pointSource += "_point_";
                appendIndex(pointSource, index);
                auto pointLabelText =
                    core::pmr::String("Point ", &core::defaultCpuMemoryResource());
                appendIndex(pointLabelText, index);
                ui::UiControlDesc pointLabel;
                pointLabel.type = ui::UiControlType::Label;
                pointLabel.widget.name = pointSource + "_label";
                pointLabel.widget.localRect =
                    ui::LayoutRect::fromOriginSize(6.0f, rowY, 114.0f, 24.0f);
                pointLabel.widget.styleId = "text";
                pointLabel.widget.fontToken = "font.editor";
                pointLabel.widget.text = std::move(pointLabelText);
                const auto labelCreated =
                    runtime().createControl(std::move(pointLabel), id("inspector_content"));
                if (labelCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(labelCreated.widgetId());
                    presentationState_.registerSource(
                        labelCreated.widgetId().value(), pointSource + "_label");
                }

                const float components[] = {
                    property.vector2ListValue[index].x, property.vector2ListValue[index].y};
                constexpr const char* axes[] = {"x", "y"};
                for (std::size_t axis = 0; axis < 2u; ++axis) {
                    char displayed[48];
                    std::snprintf(displayed,
                        sizeof(displayed),
                        "%.6g",
                        static_cast<double>(components[axis]));
                    const auto axisSource = pointSource + "_" + axes[axis];
                    ui::UiControlDesc input;
                    input.type = ui::UiControlType::TextInput;
                    input.widget.name = axisSource;
                    input.widget.localRect = ui::LayoutRect::fromOriginSize(
                        126.0f + static_cast<float>(axis) * 53.0f, rowY, 49.0f, 24.0f);
                    input.widget.styleId = "input";
                    input.widget.fontToken = "font.editor";
                    input.widget.text = displayed;
                    input.initialValue = ui::UiControlValue::fromText(
                        core::pmr::String(displayed, &core::defaultCpuMemoryResource()));
                    const auto inputCreated =
                        runtime().createControl(core::makeUnique<EditorEntry>(*this, axisSource),
                            std::move(input),
                            id("inspector_content"));
                    if (inputCreated.ok()) {
                        sceneInspectorWidgetIds_.push_back(inputCreated.widgetId());
                        presentationState_.registerSource(
                            inputCreated.widgetId().value(), axisSource);
                    }
                }

                auto removeSource = source;
                removeSource += "_point_remove_";
                appendIndex(removeSource, index);
                ui::UiControlDesc removeButton;
                removeButton.type = ui::UiControlType::Button;
                removeButton.widget.name = removeSource;
                removeButton.widget.localRect =
                    ui::LayoutRect::fromOriginSize(232.0f, rowY, 52.0f, 24.0f);
                removeButton.widget.styleId = "button";
                removeButton.widget.fontToken = "font.editor";
                removeButton.widget.text = "Remove";
                const auto removeCreated =
                    runtime().createControl(core::makeUnique<EditorAction>(*this, removeSource),
                        std::move(removeButton),
                        id("inspector_content"));
                if (removeCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(removeCreated.widgetId());
                    presentationState_.registerSource(
                        removeCreated.widgetId().value(), removeSource);
                }
                rowY += 25.0f;
            }
            vector2ListPointCount += pointCount;
            if (property.vector2ListValue.size() < kMaxSceneInspectorVector2ListElements
                && vector2ListPointCount < kSceneInspectorVector2ListPointBudget) {
                const auto addSource = source + "_point_add";
                ui::UiControlDesc addButton;
                addButton.type = ui::UiControlType::Button;
                addButton.widget.name = addSource;
                addButton.widget.localRect =
                    ui::LayoutRect::fromOriginSize(126.0f, rowY, 104.0f, 24.0f);
                addButton.widget.styleId = "button";
                addButton.widget.fontToken = "font.editor";
                addButton.widget.text = "Add Point";
                const auto addCreated =
                    runtime().createControl(core::makeUnique<EditorAction>(*this, addSource),
                        std::move(addButton),
                        id("inspector_content"));
                if (addCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(addCreated.widgetId());
                    presentationState_.registerSource(addCreated.widgetId().value(), addSource);
                }
                rowY += 25.0f;
            }
            continue;
        }

        if (property.kind == SceneInspectorPropertyKind::SceneObjectLinkList) {
            const auto linkCount =
                std::min(property.sceneObjectLinkListValue.size(), kMaxSceneInspectorObjectLinks);
            for (std::size_t index = 0; index < linkCount; ++index) {
                auto rowSource = source + "_link_";
                appendIndex(rowSource, index);
                ui::UiControlDesc label;
                label.type = ui::UiControlType::Label;
                label.widget.name = rowSource + "_label";
                label.widget.localRect = ui::LayoutRect::fromOriginSize(6.0f, rowY, 32.0f, 24.0f);
                label.widget.styleId = "text";
                label.widget.fontToken = "font.editor";
                label.widget.text = "Link";
                const auto labelCreated =
                    runtime().createControl(std::move(label), id("inspector_content"));
                if (labelCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(labelCreated.widgetId());
                    presentationState_.registerSource(
                        labelCreated.widgetId().value(), rowSource + "_label");
                }

                ui::UiControlDesc nameInput;
                nameInput.type = ui::UiControlType::TextInput;
                auto nameSource = source + "_link_name_";
                appendIndex(nameSource, index);
                nameInput.widget.name = nameSource;
                nameInput.widget.localRect =
                    ui::LayoutRect::fromOriginSize(42.0f, rowY, 92.0f, 24.0f);
                nameInput.widget.styleId = "input";
                nameInput.widget.fontToken = "font.editor";
                nameInput.widget.text = property.sceneObjectLinkListValue[index].name;
                nameInput.initialValue =
                    ui::UiControlValue::fromText(property.sceneObjectLinkListValue[index].name);
                const auto nameCreated =
                    runtime().createControl(core::makeUnique<EditorEntry>(*this, nameSource),
                        std::move(nameInput),
                        id("inspector_content"));
                if (nameCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(nameCreated.widgetId());
                    presentationState_.registerSource(nameCreated.widgetId().value(), nameSource);
                }

                const auto targetText =
                    std::to_string(property.sceneObjectLinkListValue[index].targetObjectId);
                ui::UiControlDesc targetInput;
                targetInput.type = ui::UiControlType::TextInput;
                auto targetSource = source + "_link_target_";
                appendIndex(targetSource, index);
                targetInput.widget.name = targetSource;
                targetInput.widget.localRect =
                    ui::LayoutRect::fromOriginSize(138.0f, rowY, 92.0f, 24.0f);
                targetInput.widget.styleId = "input";
                targetInput.widget.fontToken = "font.editor";
                targetInput.widget.text = targetText;
                targetInput.initialValue = ui::UiControlValue::fromText(
                    core::pmr::String(targetText, &core::defaultCpuMemoryResource()));
                const auto targetCreated =
                    runtime().createControl(core::makeUnique<EditorEntry>(*this, targetSource),
                        std::move(targetInput),
                        id("inspector_content"));
                if (targetCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(targetCreated.widgetId());
                    presentationState_.registerSource(
                        targetCreated.widgetId().value(), targetSource);
                }

                auto removeSource = source + "_link_remove_";
                appendIndex(removeSource, index);
                ui::UiControlDesc removeButton;
                removeButton.type = ui::UiControlType::Button;
                removeButton.widget.name = removeSource;
                removeButton.widget.localRect =
                    ui::LayoutRect::fromOriginSize(234.0f, rowY, 60.0f, 24.0f);
                removeButton.widget.styleId = "button";
                removeButton.widget.fontToken = "font.editor";
                removeButton.widget.text = "Remove";
                const auto removeCreated =
                    runtime().createControl(core::makeUnique<EditorAction>(*this, removeSource),
                        std::move(removeButton),
                        id("inspector_content"));
                if (removeCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(removeCreated.widgetId());
                    presentationState_.registerSource(
                        removeCreated.widgetId().value(), removeSource);
                }
                rowY += 25.0f;
            }

            const bool hasTarget = std::any_of(sceneDocument_.scene().objects().begin(),
                sceneDocument_.scene().objects().end(),
                [&](const auto& candidate) {
                    return candidate->id().value() != property.objectId;
                });
            if (hasTarget
                && property.sceneObjectLinkListValue.size() < kMaxSceneInspectorObjectLinks) {
                const auto addSource = source + "_link_add";
                ui::UiControlDesc addButton;
                addButton.type = ui::UiControlType::Button;
                addButton.widget.name = addSource;
                addButton.widget.localRect =
                    ui::LayoutRect::fromOriginSize(126.0f, rowY, 104.0f, 24.0f);
                addButton.widget.styleId = "button";
                addButton.widget.fontToken = "font.editor";
                addButton.widget.text = "Add Link";
                const auto addCreated =
                    runtime().createControl(core::makeUnique<EditorAction>(*this, addSource),
                        std::move(addButton),
                        id("inspector_content"));
                if (addCreated.ok()) {
                    sceneInspectorWidgetIds_.push_back(addCreated.widgetId());
                    presentationState_.registerSource(addCreated.widgetId().value(), addSource);
                }
                rowY += 25.0f;
            }
            continue;
        }

        if (property.kind == SceneInspectorPropertyKind::Boolean) {
            ui::UiControlDesc checkboxDesc;
            checkboxDesc.type = ui::UiControlType::Checkbox;
            checkboxDesc.widget.name = source;
            checkboxDesc.widget.localRect =
                ui::LayoutRect::fromOriginSize(126.0f, rowY + 3.0f, 18.0f, 18.0f);
            checkboxDesc.widget.styleId = "checkbox";
            checkboxDesc.initialValue = ui::UiControlValue::fromBool(property.booleanValue);
            const auto checkboxCreated =
                runtime().createControl(core::makeUnique<EditorPropertyCheckbox>(*this, source),
                    std::move(checkboxDesc),
                    id("inspector_content"));
            if (checkboxCreated.ok()) {
                sceneInspectorWidgetIds_.push_back(checkboxCreated.widgetId());
                presentationState_.registerSource(checkboxCreated.widgetId().value(), source);
            }
            rowY += 25.0f;
            continue;
        }

        if (property.kind == SceneInspectorPropertyKind::Color4) {
            ui::UiControlDesc colorDesc;
            colorDesc.type = ui::UiControlType::ColorPicker;
            colorDesc.widget.name = source;
            colorDesc.widget.localRect =
                ui::LayoutRect::fromOriginSize(126.0f, rowY, 168.0f, 24.0f);
            colorDesc.widget.styleId = "input";
            colorDesc.initialValue = ui::UiControlValue::fromColor(property.color4Value.x,
                property.color4Value.y,
                property.color4Value.z,
                property.color4Value.w);
            const auto colorCreated =
                runtime().createControl(core::makeUnique<EditorPropertyColorPicker>(*this, source),
                    std::move(colorDesc),
                    id("inspector_content"));
            if (colorCreated.ok()) {
                sceneInspectorWidgetIds_.push_back(colorCreated.widgetId());
                presentationState_.registerSource(colorCreated.widgetId().value(), source);
            }
            rowY += 25.0f;
            continue;
        }

        char displayedValue[48];
        if (property.kind == SceneInspectorPropertyKind::Boolean)
            std::snprintf(displayedValue,
                sizeof(displayedValue),
                "%s",
                property.booleanValue ? "true" : "false");
        else if (property.kind == SceneInspectorPropertyKind::Integer)
            std::snprintf(displayedValue, sizeof(displayedValue), "%d", property.integerValue);
        else if (property.kind == SceneInspectorPropertyKind::UnsignedInteger)
            std::snprintf(displayedValue,
                sizeof(displayedValue),
                "%llu",
                static_cast<unsigned long long>(property.unsignedIntegerValue));
        else if (property.kind == SceneInspectorPropertyKind::Enumeration) {
            const auto option = std::find_if(property.enumOptions.begin(),
                property.enumOptions.end(),
                [&](const auto& candidate) {
                    return candidate.value == property.integerValue;
                });
            std::snprintf(displayedValue,
                sizeof(displayedValue),
                "%s",
                option == property.enumOptions.end() ? "<invalid>" : option->name.c_str());
        } else if (property.kind == SceneInspectorPropertyKind::ResourceReference
            || property.kind == SceneInspectorPropertyKind::SceneObjectReference) {
            const auto option = std::find_if(property.enumOptions.begin(),
                property.enumOptions.end(),
                [&](const auto& candidate) {
                    return candidate.value == property.integerValue;
                });
            std::snprintf(displayedValue,
                sizeof(displayedValue),
                "%s",
                option == property.enumOptions.end() ? "<invalid>" : option->name.c_str());
        } else if (property.kind == SceneInspectorPropertyKind::String)
            std::snprintf(displayedValue, sizeof(displayedValue), "%s", "");
        else
            std::snprintf(displayedValue,
                sizeof(displayedValue),
                "%.6g",
                static_cast<double>(property.numberValue));

        ui::UiControlDesc inputDesc;
        const bool resourceReference =
            property.kind == SceneInspectorPropertyKind::ResourceReference;
        const bool sceneObjectReference =
            property.kind == SceneInspectorPropertyKind::SceneObjectReference;
        const bool dropdownProperty = property.kind == SceneInspectorPropertyKind::Enumeration
            || resourceReference || sceneObjectReference;
        inputDesc.type =
            dropdownProperty ? ui::UiControlType::Dropdown : ui::UiControlType::TextInput;
        inputDesc.widget.name = source;
        inputDesc.widget.localRect = ui::LayoutRect::fromOriginSize(126.0f, rowY, 168.0f, 24.0f);
        inputDesc.widget.styleId = "input";
        inputDesc.widget.fontToken = "font.editor";
        const auto inputCreated = [&]() {
            if (dropdownProperty) {
                std::uint32_t selectedIndex = 0;
                for (std::size_t optionIndex = 0; optionIndex < property.enumOptions.size();
                    ++optionIndex) {
                    inputDesc.optionLabels.push_back(property.enumOptions[optionIndex].name);
                    if (property.enumOptions[optionIndex].value == property.integerValue)
                        selectedIndex = static_cast<std::uint32_t>(optionIndex);
                }
                inputDesc.initialValue = ui::UiControlValue::fromIndex(selectedIndex);
                return runtime().createControl(core::makeUnique<EditorEnumDropdown>(*this,
                                                   source,
                                                   property.enumOptions,
                                                   resourceReference || sceneObjectReference),
                    std::move(inputDesc),
                    id("inspector_content"));
            }
            const auto textValue = property.kind == SceneInspectorPropertyKind::String
                ? property.stringValue
                : core::pmr::String(displayedValue, &core::defaultCpuMemoryResource());
            inputDesc.widget.text = textValue;
            inputDesc.initialValue = ui::UiControlValue::fromText(textValue);
            return runtime().createControl(core::makeUnique<EditorEntry>(*this, source),
                std::move(inputDesc),
                id("inspector_content"));
        }();
        if (inputCreated.ok()) {
            sceneInspectorWidgetIds_.push_back(inputCreated.widgetId());
            presentationState_.registerSource(inputCreated.widgetId().value(), source);
        }
        rowY += 25.0f;
        if (resourceReference && !property.resourceDiagnostic.empty()) {
            const auto diagnosticSource = source + "_diagnostic";
            ui::UiControlDesc diagnosticDesc;
            diagnosticDesc.type = ui::UiControlType::Label;
            diagnosticDesc.widget.name = diagnosticSource;
            diagnosticDesc.widget.localRect =
                ui::LayoutRect::fromOriginSize(6.0f, rowY, 288.0f, 30.0f);
            diagnosticDesc.widget.styleId = "muted";
            diagnosticDesc.widget.fontToken = "font.editor";
            diagnosticDesc.widget.text = property.resourceDiagnostic;
            const auto diagnosticCreated =
                runtime().createControl(std::move(diagnosticDesc), id("inspector_content"));
            if (diagnosticCreated.ok()) {
                sceneInspectorWidgetIds_.push_back(diagnosticCreated.widgetId());
                presentationState_.registerSource(
                    diagnosticCreated.widgetId().value(), diagnosticSource);
            }
            rowY += 30.0f;
        }
    }
    if (capacityExceeded) {
        core::pmr::String diagnosticSource(
            "scene_inspector_capacity_diagnostic", &core::defaultCpuMemoryResource());
        ui::UiControlDesc desc;
        desc.type = ui::UiControlType::Label;
        desc.widget.name = diagnosticSource;
        desc.widget.localRect = ui::LayoutRect::fromOriginSize(6.0f, rowY, 288.0f, 21.0f);
        desc.widget.styleId = "muted";
        desc.widget.fontToken = "font.editor";
        desc.widget.text = "Inspector property limit reached (64)";
        const auto created = runtime().createControl(std::move(desc), id("inspector_content"));
        if (created.ok()) {
            sceneInspectorWidgetIds_.push_back(created.widgetId());
            presentationState_.registerSource(created.widgetId().value(), diagnosticSource);
        }
        rowY += 23.0f;
    }
    const float componentAddY = rowY + 8.0f;
    if (auto* addComponent = runtime().canvas().findWidget(id("scene_component_add")))
        addComponent->setLocalRect(
            ui::LayoutRect::fromOriginSize(6.0f, componentAddY, 336.0f, 28.0f));
    if (auto* content = runtime().canvas().findWidget(id("inspector_content"))) {
        inspectorContentHeight_ = std::max(420.0f, componentAddY + 36.0f);
        auto contentRect = content->desc().localRect;
        contentRect.height = inspectorContentHeight_;
        content->setLocalRect(contentRect);
        const auto layout = runtime().canvas().resolveLayout();
        const auto* view = layout.ok() ? layout.findWidget(id("properties")) : nullptr;
        const auto* track = layout.ok() ? layout.findWidget(id("inspector_scroll_track")) : nullptr;
        auto* thumb = runtime().canvas().findWidget(id("inspector_scroll"));
        if (view && track && thumb) {
            const float thumbHeight = std::min(track->rect.height,
                view->rect.height * view->rect.height / inspectorContentHeight_);
            auto thumbRect = thumb->desc().localRect;
            thumbRect.height = thumbHeight;
            thumb->setLocalRect(thumbRect);
        }
    }
}

ui::UiInputFrameDispatchResult EditorShell::scenario()
{
    pollScenePersistence();
    input::InputFrame frame;
    auto click = [&](float x, float y) {
        frame.add(input::InputEvent::pointerMoved(x, y));
        frame.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
        frame.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
    };
    click(130, 318);  // ReflectionProbe1
    click(1760, 503); // Interior
    click(930, 21);
    click(1850, 235); // workspace and update mode
    click(1830, 265);
    frame.add(input::InputEvent::keyPressed(input::InputKey::Backspace));
    frame.add(input::InputEvent::textInput("5"));
    const auto layout = runtime().canvas().resolveLayout();
    if (!layout.ok())
        return ui::UiInputFrameDispatchResult{};
    const auto* thumb = layout.findWidget(id("inspector_scroll"));
    if (!thumb)
        return ui::UiInputFrameDispatchResult{};
    frame.add(input::InputEvent::pointerMoved(1902.0f, thumb->rect.y + 3.0f));
    frame.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
    frame.add(input::InputEvent::pointerMoved(1902.0f, thumb->rect.y + 30.0f));
    frame.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
    return runtime().dispatchFrame(frame, {0, 64});
}
} // namespace arp::editor
