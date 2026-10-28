#pragma once

#include "editor/tests/fixtures/script_scene_shell_fixture.h"

namespace arp::editor::testfixtures {

// A disk-backed v4 project, shared by real-widget tests and Vulkan captures.
// All initial values are explicit; user initializers and constructors are traps.
class ScriptInspectorFixture final {
public:
    explicit ScriptInspectorFixture(std::string_view declarations = {})
    {
        files.manifest.scriptModules.resize(1);
        files.write("primary.arp", core::pmr::String(source) + core::pmr::String(declarations));
        ProjectDiagnostic issue;
        core::pmr::String manifest, diagnostic;
        check(ProjectManifestCodec::encode(files.manifest, manifest, issue));
        files.write("project.arp.yaml", manifest);
        EditorSceneDocument document(false);
        check(document.bindServiceSession(Fixture::services(), diagnostic), diagnostic);
        check(files.prepare(document, diagnostic), diagnostic);
        document.scene().setName("Script Inspector project");
        owner = document.createObject("Script fields");
        other = document.createObject("Reference target");
        auto fields = Y::parseSingleDocument(initial);
        fields["target"] = Y::Node();
        EditorScriptComponentArchive archive(document, EditorSceneScriptPolicy::fields());
        const auto state = archive.prepare({primaryId, "Fields", 1}, true, Y::serialize(fields));
        scene::Scene::IdentityRestoration transaction(document.scene(), {0, 1});
        auto& component = archive.stage(transaction, owner, {}, 1, state, true);
        componentId = component.id();
        archive.apply(component, state, transaction);
        transaction.prepare();
        transaction.commit();
        core::pmr::String scene;
        check(EditorSceneScriptCodec::encode(document,
                  EditorSceneScriptPolicy::inputs(files.root, files.manifest),
                  scene,
                  diagnostic),
            diagnostic);
        files.write("scene.yaml", scene);
    }

    void open(EditorShell& shell)
    {
        check(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
        ScriptSceneShellFixture::load(shell);
        shell.activate("workspace_3D");
        command(shell, SelectSceneObjectCommand{owner.value()});
        check(
            shell.inspectorState() == EditorShell::InspectorState::Ready, shell.inspectorFailure());
    }

    static void command(EditorShell& shell, EditorCommandPayload payload)
    {
        ScriptSceneShellFixture::command(shell, std::move(payload));
    }

    const scene::Component& component(const EditorShell& shell) const
    {
        const auto* object = shell.sceneDocument().scene().findObject(owner);
        check(object && object->findComponent(componentId));
        return *object->findComponent(componentId);
    }

    EditorScriptComponentState state(const EditorShell& shell) const
    {
        return EditorScriptComponentArchive(
            shell.sceneDocument(), EditorSceneScriptPolicy::fields())
            .capture(component(shell));
    }

    // Resolve actual, currently published dynamic widgets. shell.id() only
    // addresses static bindings and must not stand in for an actual UI event.
    static ui::WidgetId control(EditorShell& shell,
        std::string_view path,
        ui::UiControlType type,
        std::string_view caption = {})
    {
        bool foundPath = path.empty();
        const auto& children =
            shell.runtime().canvas().findWidget(shell.id("inspector_content"))->childIds();
        ui::WidgetId pending;
        for (const auto id : children) {
            const auto* widget = shell.runtime().canvas().findWidget(id);
            if (widget && widget->desc().text == "New ScriptComponent - not attached")
                pending = id;
        }
        bool inTarget = !pending.valid();
        for (const auto id : children) {
            if (id == pending)
                inTarget = true;
            if (!inTarget)
                continue;
            const auto* widget = shell.runtime().canvas().findWidget(id);
            const auto* object = shell.runtime().findControlObject(id);
            if (!widget || !object || !EditorScriptInspector::handles(widget->name()))
                continue;
            if (object->controlType() == ui::UiControlType::Label && widget->desc().text == path)
                foundPath = true;
            if (foundPath && object->controlType() == type
                && (caption.empty() || widget->desc().text == caption))
                return id;
        }
        check(false,
            core::pmr::String("test widget missing: ") + core::pmr::String(path) + " / "
                + core::pmr::String(caption));
        return {};
    }

    static core::pmr::String name(EditorShell& shell, ui::WidgetId id)
    {
        return shell.runtime().canvas().findWidget(id)->name();
    }

    static bool label(EditorShell& shell, std::string_view text)
    {
        for (const auto id :
            shell.runtime().canvas().findWidget(shell.id("inspector_content"))->childIds()) {
            const auto* widget = shell.runtime().canvas().findWidget(id);
            if (widget && EditorScriptInspector::handles(widget->name())
                && widget->desc().text == text)
                return true;
        }
        return false;
    }

    static void click(EditorShell& shell, std::string_view caption, std::string_view path = {})
    {
        ui::UiControlInteraction event;
        event.kind = ui::UiControlInteractionKind::Click;
        event.widgetId = control(shell, path, ui::UiControlType::Button, caption);
        check(shell.runtime().interact(event).ok());
    }

    static void edit(EditorShell& shell, std::string_view path, std::string_view value)
    {
        ui::UiControlInteraction event;
        event.kind = ui::UiControlInteractionKind::EditText;
        event.widgetId = control(shell, path, ui::UiControlType::TextInput);
        event.text = value;
        check(shell.runtime().interact(event).ok());
    }

    static void choose(EditorShell& shell, std::string_view path, std::string_view caption)
    {
        const auto id = control(shell, path, ui::UiControlType::Dropdown);
        const auto* object = shell.runtime().findControlObject(id);
        const auto& options = object->desc().optionLabels;
        std::size_t index = 0;
        while (index < options.size() && options[index] != caption)
            ++index;
        check(index < options.size(), "test choice missing");
        check(shell.runtime()
                .interact(ui::UiControlInteraction::select(
                    id, std::uint32_t(index), std::uint32_t(options.size())))
                .ok());
    }

    // Open the real static Add menu, then click its exact dynamic script row.
    // The popup uses bounded scrolling; reveal the row through that control.
    static ui::WidgetId scriptMenu(
        EditorShell& shell, std::string_view caption = "Script: primary / Fields")
    {
        const auto* add = shell.runtime().canvas().findWidget(shell.id("scene_component_add"));
        check(add);
        command(shell, SetInspectorScrollCommand{add->desc().localRect.y});
        ui::UiControlInteraction open;
        open.kind = ui::UiControlInteractionKind::Click;
        open.widgetId = shell.id("scene_component_add");
        check(shell.runtime().interact(open).ok());
        const auto find = [&](const auto& self, ui::WidgetId parent) -> ui::WidgetId {
            const auto* widget = shell.runtime().canvas().findWidget(parent);
            if (!widget)
                return {};
            if (widget->desc().text == caption)
                return parent;
            for (const auto child : widget->childIds()) {
                const auto result = self(self, child);
                if (result.valid())
                    return result;
            }
            return {};
        };
        const auto row = find(find, shell.runtime().canvas().rootId());
        if (!row.valid()) {
            std::fprintf(stderr,
                "menu state=%u diagnostic=%s catalog=%llu\n",
                unsigned(shell.inspectorState()),
                shell.inspectorFailure(),
                static_cast<unsigned long long>(shell.sceneDocument().scriptCatalog()
                        ? shell.sceneDocument().scriptCatalog()->entries().size()
                        : 0));
            const auto dump = [&](const auto& self, ui::WidgetId parent) -> void {
                const auto* widget = shell.runtime().canvas().findWidget(parent);
                if (!widget)
                    return;
                if (widget->name().find("scene_component_add") == 0)
                    std::fprintf(
                        stderr, "%s: %s\n", widget->name().c_str(), widget->desc().text.c_str());
                for (const auto child : widget->childIds())
                    self(self, child);
            };
            dump(dump, shell.runtime().canvas().rootId());
        }
        check(row.valid(), "dynamic script catalog row missing");
        const auto* widget = shell.runtime().canvas().findWidget(row);
        const auto* popup = shell.runtime().canvas().findWidget(widget->parentId());
        const auto offset = std::max(0.f,
            widget->desc().localRect.y + widget->desc().localRect.height
                - popup->desc().localRect.height);
        check(shell.runtime()
                .setValue(widget->parentId(),
                    ui::UiControlValue::fromScroll(0, offset),
                    ui::UiNotifyPolicy::Notify)
                .ok());
        return row;
    }

    static void beginAttachment(EditorShell& shell)
    {
        ui::UiControlInteraction select;
        select.kind = ui::UiControlInteractionKind::Click;
        select.widgetId = scriptMenu(shell);
        check(shell.runtime().interact(select).ok());
        check(label(shell, "New ScriptComponent - not attached"));
    }

    static void fillAttachment(EditorShell& shell)
    {
        edit(shell, "amount", "23");
        edit(shell, "counter", "9007199254740993");
        edit(shell, "speed", "1.2345678901234567");
        choose(shell, "visible", "true");
        edit(shell, "title", "\"初始\\nSurvivor\"");
        click(shell, "Create empty list", "tags");
        click(shell, "None", "target");
        click(shell, "Create empty list", "links");
        click(shell, "Create empty list", "choices");
        choose(shell, "enabled", "false");
    }

    using Y = core::YamlContract;
    Fixture files;
    scene::GameObjectId owner, other;
    scene::ComponentId componentId;

    // clang-format off
    static constexpr const char* source = R"ARP(from arp_scene import GameObject, Component, main_scene


# Values are supplied by the scene archive, never by running this code.
class Fields(ScriptComponent):
    amount: int
    counter: int64
    speed: float
    visible: bool
    title: string
    tags: [string]
    target: GameObject?
    links: [Component]
    choices: [[string?]]
    enabled: bool

    def __init__(self):
        main_scene.set_object_name("ConstructorExecuted")


main_scene.set_object_name("InitializerExecuted")
)ARP";
    static constexpr const char* initial = R"YAML(amount: 7
counter: 9007199254740993
speed: 1.2345678901234567
visible: true
title: "中文\nSecond line"
tags: ["one", ""]
target: null
links: []
choices: []
enabled: false
)YAML";
    // clang-format on
};
} // namespace arp::editor::testfixtures
