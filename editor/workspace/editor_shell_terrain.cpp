#include "3d/components.h"
#include "editor/workspace/editor_shell.h"
#include "resource/terrain_asset_resource.h"

#include <cmath>
#include <cstdio>

namespace arp::editor {
ui::WidgetId EditorShell::terrainBrowserWidget(std::string_view name) const
{
    for (const auto id : terrainBrowserWidgets_) {
        const auto* widget = runtime().canvas().findWidget(id);
        if (widget && widget->name() == name)
            return id;
    }
    return {};
}

bool EditorShell::showTerrainBrowser(bool show)
{
    for (const auto id : terrainBrowserWidgets_)
        if (auto* dropdown = dynamic_cast<EditorEnumDropdown*>(runtime().findControlObject(id)))
            dropdown->closeMenu();
    if (!show) {
        if (!terrainBrowserWidgets_.empty())
            (void)runtime().setVisible(terrainBrowserWidgets_.front(), false);
        return true;
    }
    for (auto it = terrainBrowserWidgets_.rbegin(); it != terrainBrowserWidgets_.rend(); ++it) {
        presentationState_.unregisterSource(it->value());
        (void)runtime().removeControl(*it);
    }
    terrainBrowserWidgets_.clear();
    terrainBrowserSelection_ = 0;
    (void)terrainBrowser_.refresh(projectResources());
    auto create = [&](std::string_view source,
                      std::string_view label,
                      ui::UiControlType type,
                      float x,
                      float y,
                      float width,
                      float height,
                      ui::WidgetId parent) {
        ui::UiControlDesc desc;
        desc.type = type;
        desc.widget.name = source;
        desc.widget.text = label;
        desc.widget.localRect = ui::LayoutRect::fromOriginSize(x, y, width, height);
        desc.widget.styleId = type == ui::UiControlType::Panel ? "panel"
            : type == ui::UiControlType::Button                ? "button"
                                                               : "text";
        desc.widget.fontToken = "font.editor";
        desc.widget.acceptsPointerInput = type != ui::UiControlType::Label;
        const auto created = type == ui::UiControlType::Button
            ? runtime().createControl(
                  core::makeUnique<EditorAction>(*this, core::pmr::String(source)),
                  std::move(desc),
                  parent)
            : runtime().createControl(std::move(desc), parent);
        if (!created.ok())
            return ui::WidgetId{};
        terrainBrowserWidgets_.push_back(created.widgetId());
        presentationState_.registerSource(created.widgetId().value(), core::pmr::String(source));
        return created.widgetId();
    };
    const auto panel = create("terrain_browser_panel",
        "",
        ui::UiControlType::Panel,
        400,
        220,
        1120,
        600,
        runtime().canvas().rootId());
    if (!panel.valid())
        return false;
    create("terrain_browser_title",
        "Terrain Assets",
        ui::UiControlType::Label,
        24,
        20,
        1060,
        32,
        panel);
    core::pmr::Vector<SceneInspectorEnumOption> options;
    ui::UiControlDesc choice;
    choice.type = ui::UiControlType::Dropdown;
    choice.widget.name = "terrain_browser_choice";
    choice.widget.localRect = ui::LayoutRect::fromOriginSize(24, 64, 1060, 32);
    choice.widget.styleId = "input";
    choice.widget.fontToken = "font.editor";
    for (const auto& entry : terrainBrowser_.entries()) {
        SceneInspectorEnumOption option;
        option.name = "Preview: " + entry.previewId;
        option.commandValue = entry.previewId;
        option.value = static_cast<int>(options.size());
        choice.optionLabels.push_back(option.name);
        options.push_back(std::move(option));
    }
    if (options.empty()) {
        SceneInspectorEnumOption option;
        option.name = "No terrain previews";
        choice.optionLabels.push_back(option.name);
        options.push_back(std::move(option));
    }
    choice.initialValue = ui::UiControlValue::fromIndex(0);
    const auto selected = runtime().createControl(
        core::makeUnique<EditorEnumDropdown>(
            *this, core::pmr::String("terrain_browser_choice"), std::move(options), true),
        std::move(choice),
        panel);
    if (!selected.ok())
        return false;
    terrainBrowserWidgets_.push_back(selected.widgetId());
    runtime()
        .canvas()
        .findWidget(selected.widgetId())
        ->setEnabled(!terrainBrowser_.entries().empty());
    presentationState_.registerSource(
        selected.widgetId().value(), core::pmr::String("terrain_browser_choice"));
    constexpr const char* fields[]{
        "identity", "grid", "extent", "height", "payload", "vegetation", "status"};
    for (std::size_t i = 0; i < std::size(fields); ++i)
        if (!create(core::pmr::String("terrain_browser_") + fields[i],
                "",
                ui::UiControlType::Label,
                24,
                116 + float(i) * 48,
                1060,
                34,
                panel)
                .valid())
            return false;
    if (!create("terrain_browser_open",
            "Open / Focus Terrain",
            ui::UiControlType::Button,
            24,
            530,
            480,
            40,
            panel)
            .valid()
        || !create(
            "terrain_browser_close", "Close", ui::UiControlType::Button, 880, 530, 204, 40, panel)
            .valid())
        return false;
    updateTerrainBrowserDetails();
    return true;
}

void EditorShell::updateTerrainBrowserDetails()
{
    auto set = [&](const char* name, core::pmr::String value) {
        if (auto* widget = runtime().canvas().findWidget(terrainBrowserWidget(name)))
            widget->setText(std::move(value));
    };
    const bool available = terrainBrowserSelection_ < terrainBrowser_.entries().size();
    if (auto* button = runtime().canvas().findWidget(terrainBrowserWidget("terrain_browser_open")))
        button->setEnabled(available);
    if (!available) {
        set("terrain_browser_status",
            terrainBrowser_.diagnostic().empty()
                ? core::pmr::String("No terrain previews registered in project.arp.yaml")
                : terrainBrowser_.diagnostic());
        return;
    }
    const auto& entry = terrainBrowser_.entries()[terrainBrowserSelection_];
    set("terrain_browser_identity", "Terrain: " + entry.terrainId);
    char text[512];
    std::snprintf(text,
        sizeof(text),
        "Grid: %u x %u tiles; %u x %u samples per tile; %u layers",
        entry.tilesX,
        entry.tilesZ,
        entry.samplesPerEdge,
        entry.samplesPerEdge,
        entry.layerCount);
    set("terrain_browser_grid", text);
    std::snprintf(text,
        sizeof(text),
        "Bounds X/Z: [%.3f, %.3f] / [%.3f, %.3f] m",
        double(entry.minimum.x),
        double(entry.maximum.x),
        double(entry.minimum.z),
        double(entry.maximum.z));
    set("terrain_browser_extent", text);
    std::snprintf(text,
        sizeof(text),
        "Height: [%.3f, %.3f] m; source generation: %llu",
        double(entry.minimum.y),
        double(entry.maximum.y),
        static_cast<unsigned long long>(entry.terrainGeneration));
    set("terrain_browser_height", text);
    std::snprintf(text,
        sizeof(text),
        "Decoded terrain payload: %llu bytes (excludes preview textures/catalog)",
        static_cast<unsigned long long>(entry.sourceBytes));
    set("terrain_browser_payload", text);
    std::snprintf(text,
        sizeof(text),
        "Baked vegetation instances: %llu; vegetation drawing omitted",
        static_cast<unsigned long long>(entry.vegetationInstances));
    set("terrain_browser_vegetation", text);
    set("terrain_browser_status",
        "Prototype: six-layer baked PBR preview; source asset is read-only");
}

EditorCommandOutcome EditorShell::openTerrainPreview(const OpenTerrainPreviewCommand& command)
{
    auto failure = [&](const char* message) {
        if (auto* status =
                runtime().canvas().findWidget(terrainBrowserWidget("terrain_browser_status")))
            status->setText(message);
        feedback(message);
        return EditorCommandOutcome{false, false, EditorDiagnosticCode::InvalidCommandPayload};
    };
    EditorTerrainBrowser candidate;
    if (!command.expectedGeneration || !candidate.refresh(projectResources()))
        return failure("EDITOR-TERRAIN-OPEN-BINDING");
    const EditorTerrainBrowserEntry* selected = nullptr;
    for (const auto& entry : candidate.entries())
        if (entry.previewId == command.previewId
            && entry.previewGeneration == command.expectedGeneration)
            selected = &entry;
    if (!selected)
        return failure("EDITOR-TERRAIN-OPEN-STALE-PREVIEW");
    scene::GameObjectId existing;
    for (const auto& object : sceneDocument_.scene().objects())
        for (const auto& component : object->components()) {
            const auto* terrain = dynamic_cast<const scene3d::TerrainComponent*>(component.get());
            if (terrain && terrain->resourceId() == selected->terrainId
                && terrain->materialResourceId() == selected->previewId) {
                if (existing.valid())
                    return failure("EDITOR-TERRAIN-OPEN-AMBIGUOUS-OBJECT");
                existing = object->id();
            }
        }
    core::Vec3 translation{};
    if (existing.valid()) {
        const auto snapshot = sceneDocument_.scene().extractTraversalSnapshot({});
        if (!snapshot.ok())
            return failure("EDITOR-TERRAIN-OPEN-SCENE");
        for (const auto& node : snapshot.nodes())
            if (node.objectId == existing) {
                const auto& t = node.worldTransform;
                if (!node.activeInHierarchy || t.rotation.x != 0 || t.rotation.y != 0
                    || t.rotation.z != 0 || std::abs(t.rotation.w) != 1 || t.scale.x != 1
                    || t.scale.y != 1 || t.scale.z != 1)
                    return failure("EDITOR-TERRAIN-OPEN-TRANSFORM");
                translation = t.translation;
            }
    }
    auto camera = sceneViewportCamera_;
    if (!camera.frameBounds(selected->minimum + translation,
            selected->maximum + translation,
            gameViewportRect_.width / gameViewportRect_.height))
        return failure("EDITOR-TERRAIN-OPEN-CAMERA-RANGE");
    if (!existing.valid()) {
        const auto before = sceneDocument_.selectedObject();
        if (!execute(CreateSceneObjectCommand("TerrainAsset Preview")).success)
            return failure("EDITOR-TERRAIN-OPEN-CREATE");
        existing = sceneDocument_.selectedObject();
        AddSceneComponentCommand add("TerrainComponent");
        SetSceneComponentPropertyCommand terrain("resourceId"), material("materialResourceId");
        terrain.valueKind = material.valueKind = ScenePropertyValueKind::ResourceReference;
        terrain.stringValue = selected->terrainId;
        material.stringValue = selected->previewId;
        add.initialProperties.push_back(std::move(terrain));
        add.initialProperties.push_back(std::move(material));
        if (!execute(add).success) {
            (void)execute(DestroySceneObjectCommand{});
            (void)execute(SelectSceneObjectCommand{before.value()});
            return failure("EDITOR-TERRAIN-OPEN-COMPONENT");
        }
    } else
        (void)execute(SelectSceneObjectCommand{existing.value()});
    sceneViewportCamera_ = camera;
    (void)execute(ActivateControlCommand("workspace_3D"));
    (void)showTerrainBrowser(false);
    feedback("Terrain preview opened (Prototype); baked vegetation drawing omitted");
    return {true, true, EditorDiagnosticCode::None};
}
} // namespace arp::editor
