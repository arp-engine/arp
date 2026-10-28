#include "editor/workspace/editor_script_inspector.h"

#include "core/text/number_format.h"
#include "core/text/string_utils.h"
#include "editor/workspace/editor_enum_popup_layout.h"
#include "editor/workspace/editor_scene_script_policy.h"
#include "editor/workspace/editor_script_component_form.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace arp::editor {
namespace {
using D = reflection::SerializationDescriptor;
using Y = core::YamlContract;

void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw std::invalid_argument(diagnostic);
}

EditorCommandOutcome rejected()
{
    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
}

// Each widget carries a non-reused presentation generation. Neither a field
// name nor a component ID alone can authorize an event from a retired widget.
core::pmr::String sourceName(std::uint64_t generation, std::size_t index)
{
    return core::pmr::String("scene_script_") + core::formatUnsigned(generation) + "_"
        + core::formatUnsigned(index);
}
} // namespace

struct EditorScriptInspector::Session {
    enum class Mode { Unassigned, None, Value };
    enum class Action {
        Edit,
        Choice,
        OptionalNone,
        OptionalValue,
        ListCreate,
        ListAdd,
        ListRemove,
        Apply,
        Reset,
        BeginAdd,
        ConfirmAdd,
        CancelAdd,
        ToggleEnabled
    };

    struct Node {
        const D* type = nullptr;
        std::size_t field = 0, depth = 0;
        Mode mode = Mode::Unassigned;
        core::pmr::String text;
        core::pmr::Vector<std::size_t> children;
    };

    struct Entry {
        Entry(const EditorSceneDocument& document,
            const scene::Component& component,
            EditorScriptComponentState state)
            : id(component.id()), guard(component.nativeReflectionObject()),
              expected(std::move(state)),
              form(EditorScriptComponentForm::fromState(
                  document, *expected, EditorSceneScriptPolicy::fields()))
        {}

        Entry(
            const EditorSceneDocument& document, const scriptvm::ScriptComponentIdentity& identity)
            : form(EditorScriptComponentForm::create(
                  document, identity, true, EditorSceneScriptPolicy::fields()))
        {}

        bool pending() const noexcept { return !expected.has_value(); }

        scene::ComponentId id;
        reflection::object guard;
        std::optional<EditorScriptComponentState> expected;
        EditorScriptComponentForm form;
        core::pmr::Vector<Node> nodes;
        core::pmr::Vector<std::size_t> roots;
        bool dirty = false, conflict = false;
        core::pmr::String error;
        ui::WidgetId status;
    };

    struct Binding {
        core::pmr::String source;
        ui::WidgetId widget;
        std::size_t entry = 0, node = 0, element = 0;
        Action action = Action::Edit;
        // BeginAdd resolves this exact retained identity, not the displayed name.
        scriptvm::ScriptComponentIdentity identity;
        // Choice admission is exact, including canonical reference YAML.
        core::pmr::Vector<SceneInspectorEnumOption> choices;
    };

    core::LifetimeEpoch::Ticket ticket;
    scene::GameObjectId owner;
    reflection::object ownerGuard;
    core::SharedPtr<const EditorScriptComponentCatalog> catalog;
    core::pmr::Vector<Entry> entries;
    core::pmr::Vector<Binding> bindings;
    core::pmr::String catalogError;

    bool current(const EditorShell& shell) const
    {
        const auto* object = shell.sceneDocument().scene().findObject(owner);
        return object && ownerGuard.native_reference_alive()
            && ownerGuard.storageIdentity() == object->nativeReflectionObject().storageIdentity()
            && owner == shell.sceneDocument().selectedObject()
            && ticket.sameCurrentEpoch(shell.sceneDocument().ownerTicket())
            && catalog == shell.sceneDocument().scriptCatalog();
    }

    static std::size_t node(Entry& entry,
        const D& type,
        std::size_t field,
        std::size_t depth,
        const reflection::object* value,
        const EditorScriptComponentArchive& archive)
    {
        require(entry.nodes.size() < maxNodes
                && depth < EditorSceneScriptPolicy::fields().fields.maxDepth,
            "EDITOR-SCRIPT-INSPECTOR-NODE-BUDGET");
        const auto index = entry.nodes.size();
        entry.nodes.push_back(Node{&type, field, depth});
        if (!value)
            return index;
        entry.nodes[index].mode = Mode::Value;
        if (type.kind == D::Kind::Optional) {
            if (value->is_undefined())
                entry.nodes[index].mode = Mode::None;
            else {
                require(bool(type.element), "EDITOR-SCRIPT-INSPECTOR-ELEMENT");
                const auto child = node(entry, *type.element, field, depth + 1, value, archive);
                entry.nodes[index].children.push_back(child);
            }
        } else if (type.kind == D::Kind::Sequence) {
            require(
                type.element && value->is<reflection::hlist>(), "EDITOR-SCRIPT-INSPECTOR-SEQUENCE");
            for (const auto& item : value->cast<const reflection::hlist&>()) {
                const auto child = node(entry, *type.element, field, depth + 1, &item, archive);
                entry.nodes[index].children.push_back(child);
            }
        } else {
            auto text = archive.writeFieldElement(
                entry.form.identity(), entry.form.field(field).name, depth, *value);
            if (!text.empty() && text.back() == '\n')
                text.pop_back();
            // Scalars, including multiline strings, use the formal writer's
            // escaped scalar representation. No loss of precision or new codec.
            if (type.kind == D::Kind::Scalar)
                require(text.size() <= maxTextBytes
                        && text.find_first_of("\r\n") == core::pmr::String::npos,
                    "EDITOR-SCRIPT-INSPECTOR-TEXT-BUDGET");
            else
                require(type.kind == D::Kind::Reference, "EDITOR-SCRIPT-INSPECTOR-TYPE");
            entry.nodes[index].text = std::move(text);
        }
        return index;
    }

    static void populate(Entry& entry, const EditorScriptComponentArchive& archive)
    {
        for (std::size_t field = 0; field < entry.form.size(); ++field) {
            if (entry.form.value(field)) {
                const auto value = archive.readFieldElement(entry.form.identity(),
                    entry.form.field(field).name,
                    0,
                    *entry.form.value(field));
                entry.roots.push_back(
                    node(entry, *entry.form.field(field).type, field, 0, &value, archive));
            } else
                entry.roots.push_back(
                    node(entry, *entry.form.field(field).type, field, 0, nullptr, archive));
        }
    }

    static reflection::object value(
        const Entry& entry, std::size_t index, const EditorScriptComponentArchive& archive)
    {
        const auto& node = entry.nodes.at(index);
        require(node.mode != Mode::Unassigned, "EDITOR-SCRIPT-FORM-UNASSIGNED");
        if (node.type->kind == D::Kind::Optional) {
            if (node.mode == Mode::None)
                return {};
            return value(entry, node.children.at(0), archive);
        }
        if (node.type->kind == D::Kind::Sequence) {
            reflection::hlist list;
            list.reserve(node.children.size());
            for (const auto child : node.children)
                list.push_back(value(entry, child, archive));
            return reflection::object(std::move(list));
        }
        return archive.readFieldElement(
            entry.form.identity(), entry.form.field(node.field).name, node.depth, node.text);
    }

    // Retired list/optional subtrees consume no future draft capacity. Only an
    // unpublished candidate is compacted; allocation failure is atomic.
    static void compact(Entry& entry)
    {
        auto previous = std::move(entry.nodes);
        const auto copy = [&](const auto& self, std::size_t source) -> std::size_t {
            auto node = previous.at(source);
            node.children.clear();
            const auto destination = entry.nodes.size();
            entry.nodes.push_back(std::move(node));
            for (const auto oldChild : previous[source].children) {
                const auto child = self(self, oldChild);
                entry.nodes[destination].children.push_back(child);
            }
            return destination;
        };
        for (auto& root : entry.roots)
            root = copy(copy, root);
    }

    void validateBudget() const
    {
        std::size_t nodes = 0, bytes = 0;
        for (const auto& entry : entries) {
            nodes += entry.nodes.size();
            for (const auto& node : entry.nodes)
                bytes += node.text.size();
        }
        require(nodes <= maxNodes && bytes <= EditorSceneScriptPolicy::fields().maxYamlBytes,
            "EDITOR-SCRIPT-INSPECTOR-DRAFT-BUDGET");
    }

    const Binding* find(std::string_view source) const noexcept
    {
        const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& item) {
            return item.source == source;
        });
        return found == bindings.end() ? nullptr : &*found;
    }
};

EditorScriptInspector::EditorScriptInspector() = default;
EditorScriptInspector::~EditorScriptInspector() = default;

bool EditorScriptInspector::handles(std::string_view source) noexcept
{
    return core::startsWith(source, "scene_script_");
}

void EditorScriptInspector::prepare(EditorShell& shell)
{
    if (!shell.sceneViewportAdditionalSelection_.empty() || !shell.sceneDocument_.scriptCatalog()) {
        session_.reset();
        return;
    }
    // Preparation is a rebuild-only cold path. Ordinary refresh/hash never
    // archives script fields or checks disk sources.
    auto candidate = core::makeUnique<Session>();
    candidate->ticket = shell.sceneDocument_.ownerTicket();
    candidate->owner = shell.sceneDocument_.selectedObject();
    candidate->catalog = shell.sceneDocument_.scriptCatalog();
    const auto* owner = shell.sceneDocument_.scene().findObject(candidate->owner);
    require(owner, "EDITOR-SCRIPT-INSPECTOR-OWNER");
    candidate->ownerGuard = owner->nativeReflectionObject();
    const bool retain = session_ && session_->current(shell);
    EditorScriptComponentArchive archive(shell.sceneDocument_, EditorSceneScriptPolicy::fields());
    for (const auto& component : owner->components()) {
        if (component->scriptInstance().is_undefined())
            continue;
        auto state = archive.capture(*component);
        const Session::Entry* previous = nullptr;
        if (retain)
            for (const auto& entry : session_->entries)
                if (entry.id == component->id()
                    && entry.guard.storageIdentity()
                        == component->nativeReflectionObject().storageIdentity()
                    && entry.guard.native_reference_alive())
                    previous = &entry;
        if (previous && previous->dirty) {
            candidate->entries.push_back(*previous);
            candidate->entries.back().conflict =
                !EditorScriptComponentArchive::equal(*previous->expected, state);
        } else {
            candidate->entries.emplace_back(shell.sceneDocument_, *component, std::move(state));
            Session::populate(candidate->entries.back(), archive);
        }
    }
    if (retain) {
        candidate->catalogError = session_->catalogError;
        for (const auto& entry : session_->entries)
            if (entry.pending())
                candidate->entries.push_back(entry);
    }
    candidate->validateBudget();
    session_.swap(candidate);
}

void EditorScriptInspector::append(EditorShell& shell, float& rowY)
{
    if (!session_)
        return;
    require(generation_ != std::numeric_limits<std::uint64_t>::max(),
        "EDITOR-SCRIPT-INSPECTOR-GENERATION");
    ++generation_;
    session_->bindings.clear();
    const bool editable = shell.sceneObjectEditable(session_->owner);
    const auto create = [&](ui::UiControlDesc desc, core::UniquePtr<ui::Control> control = {}) {
        desc.widget.enabled = editable;
        const auto name = desc.widget.name;
        const auto result = control
            ? shell.createInspectorControl(
                  std::move(control), std::move(desc), shell.id("inspector_content"))
            : shell.createInspectorControl(std::move(desc), shell.id("inspector_content"));
        shell.presentationState_.registerSource(result.widgetId().value(), name);
        if (!session_->bindings.empty() && session_->bindings.back().source == name)
            session_->bindings.back().widget = result.widgetId();
        return result.widgetId();
    };
    const auto desc = [&](ui::UiControlType type,
                          std::string_view name,
                          std::string_view text,
                          float x,
                          float width,
                          std::string_view style) {
        ui::UiControlDesc result;
        result.type = type;
        result.widget.name = name;
        result.widget.text = text;
        result.widget.fontToken = "font.editor";
        result.widget.styleId = style;
        result.widget.localRect = ui::LayoutRect::fromOriginSize(x, rowY, width, 24);
        result.widget.acceptsPointerInput = type != ui::UiControlType::Label;
        result.widget.acceptsFocus = type != ui::UiControlType::Label;
        return result;
    };
    const auto label = [&](std::string_view name, std::string_view text, bool muted = false) {
        const auto id =
            create(desc(ui::UiControlType::Label, name, text, 6, 288, muted ? "muted" : "text"));
        rowY += 25;
        return id;
    };
    const auto binding =
        [&](std::size_t entry, std::size_t node, Session::Action action, std::size_t element = 0) {
            require(
                session_->bindings.size() < maxBindings, "EDITOR-SCRIPT-INSPECTOR-BINDING-BUDGET");
            Session::Binding item;
            item.source = sourceName(generation_, session_->bindings.size());
            item.entry = entry;
            item.node = node;
            item.action = action;
            item.element = element;
            session_->bindings.push_back(std::move(item));
            return session_->bindings.back().source;
        };
    const auto button = [&](std::size_t entry,
                            std::size_t node,
                            Session::Action action,
                            std::string_view text,
                            float x,
                            float width,
                            std::size_t element = 0) {
        const auto source = binding(entry, node, action, element);
        create(desc(ui::UiControlType::Button, source, text, x, width, "button"),
            core::makeUnique<EditorAction>(shell, source));
    };
    EditorScriptComponentArchive archive(shell.sceneDocument_, EditorSceneScriptPolicy::fields());
    const auto hierarchy = shell.sceneDocument_.hierarchyEntries();
    if (!session_->catalogError.empty())
        label(sourceName(generation_, 0) + "_catalog_error", session_->catalogError);
    for (std::size_t e = 0; e < session_->entries.size(); ++e) {
        auto& entry = session_->entries[e];
        if (!entry.pending()
            && std::any_of(shell.collapsedInspectorComponents_.begin(),
                shell.collapsedInspectorComponents_.end(),
                [&](const auto& item) {
                    return item.objectId == session_->owner.value()
                        && item.componentId == entry.id.value();
                }))
            continue;
        const auto prefix = sourceName(generation_, e) + "_display";
        if (entry.pending())
            label(prefix + "_add", "New ScriptComponent - not attached");
        label(prefix + "_title", entry.form.displayName());
        entry.status = label(prefix + "_status",
            !entry.error.empty() ? std::string_view(entry.error)
                : entry.conflict ? "Conflict - Reset to current values"
                : entry.dirty
                ? (entry.pending() ? "Draft - Confirm to attach" : "Draft - Apply to save fields")
                : entry.pending() ? "Fill every field, then Attach script"
                                  : "Script fields (Apply / Reset)",
            true);
        if (entry.pending()) {
            button(e,
                0,
                Session::Action::ToggleEnabled,
                entry.form.enabled() ? "Component active: true" : "Component active: false",
                6,
                282);
            rowY += 25;
        }
        const auto render =
            [&](const auto& self, std::size_t index, core::pmr::String path) -> void {
            const auto& node = entry.nodes.at(index);
            const auto labelName = prefix + "_node_" + core::formatUnsigned(index);
            label(labelName + "_label", path);
            if (node.type->kind == D::Kind::Optional) {
                label(labelName + "_mode",
                    node.mode == Session::Mode::Unassigned ? "<Unassigned>"
                        : node.mode == Session::Mode::None ? "None"
                                                           : "Value",
                    true);
                button(e, index, Session::Action::OptionalNone, "None", 6, 130);
                button(e, index, Session::Action::OptionalValue, "Value", 142, 146);
                rowY += 25;
                if (node.mode == Session::Mode::Value)
                    self(self, node.children.at(0), path + " value");
                return;
            }
            if (node.type->kind == D::Kind::Sequence) {
                if (node.mode == Session::Mode::Unassigned) {
                    button(e, index, Session::Action::ListCreate, "Create empty list", 6, 282);
                    rowY += 25;
                    return;
                }
                label(labelName + "_size",
                    core::formatUnsigned(node.children.size()) + " items",
                    true);
                for (std::size_t i = 0; i < node.children.size(); ++i) {
                    self(self, node.children[i], path + " [" + core::formatUnsigned(i) + "]");
                    button(e, index, Session::Action::ListRemove, "Remove item", 6, 282, i);
                    rowY += 25;
                }
                button(e, index, Session::Action::ListAdd, "Add item (unassigned)", 6, 282);
                rowY += 25;
                return;
            }
            const bool boolean = node.type->kind == D::Kind::Scalar
                && node.type->scalarType == reflection::type_of<bool>();
            if (node.type->kind == D::Kind::Reference || boolean) {
                core::pmr::Vector<SceneInspectorEnumOption> options;
                options.push_back({"<Unassigned>", "", 0});
                if (boolean) {
                    options.push_back({"false", "false", 1});
                    options.push_back({"true", "true", 2});
                } else {
                    std::size_t inspected = 0;
                    const auto consider = [&](const scene::GameObject& owner,
                                              scene::ComponentId component) {
                        require(++inspected <= maxReferenceCandidates,
                            "EDITOR-SCRIPT-INSPECTOR-REFERENCE-BUDGET");
                        auto target = Y::Node::mapping();
                        target["owner"] = owner.id().value();
                        target["component"] = component.value();
                        core::pmr::String canonical;
                        try {
                            const auto actual = archive.readFieldElement(entry.form.identity(),
                                entry.form.field(node.field).name,
                                node.depth,
                                Y::serialize(target));
                            canonical = archive.writeFieldElement(entry.form.identity(),
                                entry.form.field(node.field).name,
                                node.depth,
                                actual);
                        } catch (const std::invalid_argument&) {
                            // The formal exact resolver owns reference type admission.
                            return;
                        }
                        if (!canonical.empty() && canonical.back() == '\n')
                            canonical.pop_back();
                        auto title = owner.name() + " [" + core::formatUnsigned(owner.id().value())
                            + ":" + core::formatUnsigned(component.value()) + "]";
                        options.push_back(
                            {std::move(title), std::move(canonical), int(options.size())});
                    };
                    for (const auto& item : hierarchy) {
                        const auto* owner = shell.sceneDocument_.scene().findObject(item.objectId);
                        require(owner, "EDITOR-SCRIPT-INSPECTOR-REFERENCE-OWNER");
                        consider(*owner, {});
                        for (const auto& component : owner->components())
                            consider(*owner, component->id());
                    }
                }
                std::uint32_t selected = 0;
                if (node.mode == Session::Mode::Value)
                    for (std::size_t i = 1; i < options.size(); ++i)
                        if (options[i].commandValue == node.text)
                            selected = std::uint32_t(i);
                require(node.mode != Session::Mode::Value || selected != 0,
                    "EDITOR-SCRIPT-INSPECTOR-REFERENCE-STALE");
                const auto source = binding(e, index, Session::Action::Choice);
                session_->bindings.back().choices = options;
                auto input = desc(
                    ui::UiControlType::Dropdown, source, options[selected].name, 6, 282, "input");
                for (const auto& option : options)
                    input.optionLabels.push_back(option.name);
                input.initialValue = ui::UiControlValue::fromIndex(selected);
                create(std::move(input),
                    core::makeUnique<EditorEnumDropdown>(shell,
                        source,
                        std::move(options),
                        true,
                        ui::WidgetId{},
                        EditorEnumPopupLayout::inspectorMaxRows));
            } else {
                require(node.type->kind == D::Kind::Scalar, "EDITOR-SCRIPT-INSPECTOR-TYPE");
                if (node.type->scalarType == reflection::type_of<core::pmr::String>())
                    label(labelName + "_help", "Quoted text; \\n inserts a line break", true);
                if (node.mode == Session::Mode::Unassigned)
                    label(labelName + "_unassigned", "<Unassigned>", true);
                const auto source = binding(e, index, Session::Action::Edit);
                auto input = desc(ui::UiControlType::TextInput, source, node.text, 6, 282, "input");
                input.maxTextBytes = maxTextBytes;
                input.initialValue = ui::UiControlValue::fromText(node.text);
                create(std::move(input), core::makeUnique<EditorEntry>(shell, source));
            }
            rowY += 25;
        };
        for (std::size_t field = 0; field < entry.roots.size(); ++field)
            render(render, entry.roots[field], entry.form.field(field).name);
        button(e,
            0,
            entry.pending() ? Session::Action::ConfirmAdd : Session::Action::Apply,
            entry.pending() ? "Attach script" : "Apply fields",
            6,
            150);
        button(e,
            0,
            entry.pending() ? Session::Action::CancelAdd : Session::Action::Reset,
            entry.pending() ? "Cancel" : "Reset draft",
            162,
            126);
        rowY += 33;
    }
}

void EditorScriptInspector::addMenuOptions(
    EditorShell& shell, core::pmr::Vector<SceneInspectorEnumOption>& options)
{
    auto prepared = options;
    prepared.erase(std::remove_if(prepared.begin(),
                       prepared.end(),
                       [](const auto& option) {
                           return handles(option.commandValue);
                       }),
        prepared.end());
    if (!session_ || !session_->current(shell)
        || shell.sceneAuthoringVersion_ != EditorSceneScriptCodec::contractVersion) {
        options.swap(prepared);
        return;
    }
    require(generation_ != std::numeric_limits<std::uint64_t>::max(),
        "EDITOR-SCRIPT-INSPECTOR-GENERATION");
    auto bindings = session_->bindings;
    bindings.erase(std::remove_if(bindings.begin(),
                       bindings.end(),
                       [](const auto& binding) {
                           return binding.action == Session::Action::BeginAdd;
                       }),
        bindings.end());
    const auto generation = generation_ + 1;
    for (const auto& row : session_->catalog->entries()) {
        require(bindings.size() < maxBindings, "EDITOR-SCRIPT-INSPECTOR-BINDING-BUDGET");
        Session::Binding binding;
        binding.source = sourceName(generation, bindings.size());
        binding.action = Session::Action::BeginAdd;
        binding.identity = row.identity;
        const auto title =
            core::pmr::String(row.available() ? "Script: " : "Unavailable: ") + row.displayName;
        prepared.push_back({title, binding.source, static_cast<int>(prepared.size())});
        bindings.push_back(std::move(binding));
    }
    // No allocations follow publication; old popup sources cannot be reused.
    session_->bindings.swap(bindings);
    options.swap(prepared);
    generation_ = generation;
}

EditorCommandOutcome EditorScriptInspector::edit(
    EditorShell& shell, std::string_view source, std::string_view text)
{
    const auto navigation = shell.sceneNavigationGate();
    if (!navigation.success)
        return navigation;
    if (shell.authoringUiBlocked() || !shell.acceptsInspectorInput(source) || !session_
        || !session_->current(shell))
        return rejected();
    const auto gate = shell.sceneObjectWriteGate(session_->owner.value(), true);
    if (!gate.success)
        return gate;
    const auto* binding = session_->find(source);
    if (!binding
        || (binding->action != Session::Action::Edit && binding->action != Session::Action::Choice))
        return rejected();
    try {
        auto& entry = session_->entries.at(binding->entry);
        require(entry.pending() || entry.guard.native_reference_alive(),
            "EDITOR-SCRIPT-INSPECTOR-COMPONENT-STALE");
        auto& node = entry.nodes.at(binding->node);
        if (binding->action == Session::Action::Choice) {
            require(std::any_of(binding->choices.begin(),
                        binding->choices.end(),
                        [&](const auto& option) {
                            return option.commandValue == text;
                        }),
                "EDITOR-SCRIPT-INSPECTOR-CHOICE");
        } else
            require(ui::TextInputContract::validate(text, maxTextBytes).ok(),
                "EDITOR-SCRIPT-INSPECTOR-TEXT-CONTRACT");
        const auto mode = binding->action == Session::Action::Choice && text.empty()
            ? Session::Mode::Unassigned
            : Session::Mode::Value;
        if (node.text == text && node.mode == mode)
            return {true, false, EditorDiagnosticCode::None};
        core::pmr::String candidate(text);
        std::size_t bytes = candidate.size();
        for (const auto& item : session_->entries)
            for (const auto& cell : item.nodes)
                if (&cell != &node)
                    bytes += cell.text.size();
        require(bytes <= EditorSceneScriptPolicy::fields().maxYamlBytes,
            "EDITOR-SCRIPT-INSPECTOR-DRAFT-BUDGET");
        const auto presented = binding->action == Session::Action::Edit
            ? ui::UiControlValue::fromText(candidate)
            : ui::UiControlValue::fromIndex(std::uint32_t(std::distance(binding->choices.begin(),
                  std::find_if(
                      binding->choices.begin(), binding->choices.end(), [&](const auto& choice) {
                          return choice.commandValue == text;
                      }))));
        require(shell.runtime().setValue(binding->widget, presented).ok(),
            "EDITOR-SCRIPT-INSPECTOR-INPUT-SYNC");
        // Notify the draft status before publication. No Scene/history write.
        require(shell.runtime()
                    .setValue(entry.status,
                        ui::UiControlValue::fromText(entry.conflict
                                ? "Conflict - Reset to current values"
                                : entry.pending() ? "Draft - Confirm to attach"
                                                  : "Draft - Apply to save fields"))
                    .ok(),
            "EDITOR-SCRIPT-INSPECTOR-STATUS");
        node.text = std::move(candidate);
        node.mode = mode;
        entry.dirty = true;
        entry.error.clear();
        return {true, true, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        // Native TextInput notification follows its own value update. If the
        // draft cannot accept that update, quarantine until retry restores the
        // retained draft instead of letting Apply commit an invisible old value.
        shell.failSceneInspector("EDITOR-SCRIPT-INSPECTOR-EDIT-ALLOCATION");
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    } catch (const std::exception&) {
        shell.failSceneInspector("EDITOR-SCRIPT-INSPECTOR-EDIT-FAILED");
        return rejected();
    }
}

EditorCommandOutcome EditorScriptInspector::activate(EditorShell& shell, std::string_view source)
{
    const auto navigation = shell.sceneNavigationGate();
    if (!navigation.success)
        return navigation;
    if (shell.authoringUiBlocked() || !shell.acceptsInspectorInput(source) || !session_
        || !session_->current(shell))
        return rejected();
    const auto gate = shell.sceneObjectWriteGate(session_->owner.value(), true);
    if (!gate.success)
        return gate;
    const auto* found = session_->find(source);
    if (!found || found->action == Session::Action::Edit
        || found->action == Session::Action::Choice)
        return rejected();
    if (found->action == Session::Action::BeginAdd) {
        try {
            const auto* row = session_->catalog->find(found->identity);
            require(row, "EDITOR-SCRIPT-INSPECTOR-CATALOG-STALE");
            auto candidate = core::makeUnique<Session>(*session_);
            if (!row->available())
                candidate->catalogError = row->diagnostic;
            else {
                candidate->catalogError.clear();
                candidate->entries.erase(std::remove_if(candidate->entries.begin(),
                                             candidate->entries.end(),
                                             [](const auto& entry) {
                                                 return entry.pending();
                                             }),
                    candidate->entries.end());
                candidate->entries.emplace_back(shell.sceneDocument_, row->identity);
                EditorScriptComponentArchive archive(
                    shell.sceneDocument_, EditorSceneScriptPolicy::fields());
                Session::populate(candidate->entries.back(), archive);
                candidate->validateBudget();
            }
            session_.swap(candidate);
        } catch (const std::bad_alloc&) {
            shell.failSceneInspector("EDITOR-SCRIPT-INSPECTOR-ATTACH-ALLOCATION");
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        } catch (const std::exception&) {
            return rejected();
        }
        shell.sceneInspectorWidgetOwner_ = {};
        shell.refreshSceneInspector();
        return {true, true, EditorDiagnosticCode::None};
    }
    const auto entryIndex = found->entry;
    std::optional<std::size_t> failedField;
    const auto notice = [&](std::string_view message) noexcept {
        try {
            auto& entry = session_->entries.at(entryIndex);
            core::pmr::String text(message);
            if (failedField)
                text += entry.form.field(*failedField).name;
            if (!shell.runtime().setValue(entry.status, ui::UiControlValue::fromText(text)).ok()) {
                shell.failSceneInspector("EDITOR-SCRIPT-INSPECTOR-ERROR-SYNC");
                return;
            }
            entry.error = std::move(text);
        } catch (...) {
            shell.failSceneInspector("EDITOR-SCRIPT-INSPECTOR-ERROR-SYNC");
        }
    };
    EditorCommandOutcome outcome{true, true, EditorDiagnosticCode::None};
    try {
        const auto binding = *found;
        const auto& entry = session_->entries.at(binding.entry);
        require(entry.pending() || entry.guard.native_reference_alive(),
            "EDITOR-SCRIPT-INSPECTOR-COMPONENT-STALE");
        auto* owner = shell.sceneDocument_.scene().findObject(session_->owner);
        auto* component = owner ? owner->findComponent(entry.id) : nullptr;
        require(entry.pending()
                || (component
                    && component->nativeReflectionObject().storageIdentity()
                        == entry.guard.storageIdentity()),
            "EDITOR-SCRIPT-INSPECTOR-COMPONENT-STALE");
        EditorScriptComponentArchive archive(
            shell.sceneDocument_, EditorSceneScriptPolicy::fields());
        auto candidate = core::makeUnique<Session>(*session_);
        auto& changed = candidate->entries.at(binding.entry);
        if (binding.action == Session::Action::CancelAdd) {
            require(entry.pending(), "EDITOR-SCRIPT-INSPECTOR-ACTION");
            candidate->entries.erase(
                candidate->entries.begin() + static_cast<std::ptrdiff_t>(binding.entry));
        } else if (binding.action == Session::Action::ToggleEnabled) {
            require(entry.pending(), "EDITOR-SCRIPT-INSPECTOR-ACTION");
            changed.form.setEnabled(!changed.form.enabled());
            changed.dirty = true;
            changed.error.clear();
        } else if (binding.action == Session::Action::Reset) {
            require(!entry.pending(), "EDITOR-SCRIPT-INSPECTOR-ACTION");
            changed = Session::Entry(shell.sceneDocument_, *component, archive.capture(*component));
            Session::populate(changed, archive);
        } else if (binding.action == Session::Action::Apply
            || binding.action == Session::Action::ConfirmAdd) {
            require(entry.pending() == (binding.action == Session::Action::ConfirmAdd),
                "EDITOR-SCRIPT-INSPECTOR-ACTION");
            require(entry.pending()
                    || EditorScriptComponentArchive::equal(
                        archive.capture(*component), *entry.expected),
                "EDITOR-SCRIPT-INSPECTOR-CONFLICT");
            auto form = entry.form;
            for (std::size_t i = 0; i < entry.roots.size(); ++i) {
                failedField = i;
                form.assign(shell.sceneDocument_,
                    i,
                    archive.writeField(entry.form.identity(),
                        form.field(i).name,
                        Session::value(entry, entry.roots[i], archive)));
            }
            failedField.reset();
            const auto state = form.prepare(shell.sceneDocument_);
            // Prepare the post-commit presentation first. The nested command's
            // synchronous refresh is suppressed until this candidate is swapped.
            if (entry.pending())
                candidate->entries.erase(
                    candidate->entries.begin() + static_cast<std::ptrdiff_t>(binding.entry));
            else {
                changed = Session::Entry(shell.sceneDocument_, *component, state);
                Session::populate(changed, archive);
            }
            candidate->validateBudget();

            struct Applying {
                bool& flag;

                explicit Applying(bool& value) : flag(value) { flag = true; }

                ~Applying() { flag = false; }
            } applying(applying_);

            if (entry.pending()) {
                AddSceneScriptComponentCommand command(state.identity.scriptResourceId,
                    state.identity.scriptType,
                    state.identity.moduleSchemaVersion,
                    state.fields,
                    state.enabled);
                command.objectId = session_->owner.value();
                outcome = shell.submitCommand(std::move(command)).intoNestedOutcome();
            } else {
                SetSceneScriptFieldsCommand command(state.identity.scriptResourceId,
                    state.identity.scriptType,
                    state.identity.moduleSchemaVersion,
                    state.fields,
                    state.enabled);
                command.objectId = session_->owner.value();
                command.componentId = entry.id.value();
                outcome = shell.submitCommand(std::move(command)).intoNestedOutcome();
            }
            if (!outcome.success) {
                notice(entry.pending() ? "Attachment rejected - draft retained"
                                       : "Apply rejected - draft retained");
                return outcome;
            }
        } else {
            auto& node = changed.nodes.at(binding.node);
            if (binding.action == Session::Action::OptionalNone) {
                require(node.type->kind == D::Kind::Optional, "EDITOR-SCRIPT-INSPECTOR-ACTION");
                node.mode = Session::Mode::None;
                node.children.clear();
            } else if (binding.action == Session::Action::OptionalValue
                || binding.action == Session::Action::ListAdd) {
                require(node.type->element
                        && (node.type->kind == D::Kind::Optional
                            || node.type->kind == D::Kind::Sequence),
                    "EDITOR-SCRIPT-INSPECTOR-ACTION");
                const auto* type = node.type->element.get();
                const auto field = node.field, depth = node.depth;
                if (binding.action == Session::Action::OptionalValue
                    && node.mode == Session::Mode::Value)
                    return {true, false, EditorDiagnosticCode::None};
                const auto child =
                    Session::node(changed, *type, field, depth + 1, nullptr, archive);
                changed.nodes[binding.node].children.push_back(child);
                changed.nodes[binding.node].mode = Session::Mode::Value;
            } else if (binding.action == Session::Action::ListCreate) {
                require(
                    node.type->kind == D::Kind::Sequence && node.mode == Session::Mode::Unassigned,
                    "EDITOR-SCRIPT-INSPECTOR-ACTION");
                node.mode = Session::Mode::Value;
            } else if (binding.action == Session::Action::ListRemove) {
                require(
                    node.type->kind == D::Kind::Sequence && binding.element < node.children.size(),
                    "EDITOR-SCRIPT-INSPECTOR-ACTION");
                node.children.erase(
                    node.children.begin() + static_cast<std::ptrdiff_t>(binding.element));
            } else
                return rejected();
            changed.dirty = true;
            changed.error.clear();
            Session::compact(changed);
        }
        candidate->validateBudget();
        session_.swap(candidate);
    } catch (const std::bad_alloc&) {
        notice("Apply allocation failed - draft kept");
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    } catch (const core::YamlContractError&) {
        notice(failedField ? "Invalid value: " : "Invalid fields - draft retained");
        return rejected();
    } catch (const std::exception& error) {
        notice(std::string_view(error.what()) == "EDITOR-SCRIPT-INSPECTOR-CONFLICT"
                ? "Conflict - Reset to current values"
                : failedField ? "Invalid value: "
                              : "Apply rejected - draft retained");
        return rejected();
    }
    // Presentation failure is isolated by refresh; a successful typed receipt
    // remains successful even when the UI needs an explicit retry.
    shell.sceneInspectorWidgetOwner_ = {};
    shell.refreshSceneInspector();
    return outcome;
}
} // namespace arp::editor
