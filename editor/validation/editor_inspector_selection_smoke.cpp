#include "editor/validation/editor_inspector_selection_smoke.h"

#include "editor/validation/editor_control_input.h"
#include "reflection/reflection.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
namespace {
struct BooleanObservation {
    bool shared = true;
};

struct NumberObservation {
    double shared = 1;
};

// Explicit native test schema: same component identity, incompatible field kinds.
// The fixture is linked only into tests/native smoke, never arp_editor_shell.
class SelectionSchemaFixture final : public scene::Component {
public:
    explicit SelectionSchemaFixture(bool numeric) : numeric_(numeric) {}

    const char* scriptTypeName() const override { return "SelectionSchemaFixture"; }

    reflection::object editorAuthoringProperties() const override
    {
        return numeric_ ? reflection::object(NumberObservation{})
                        : reflection::object(BooleanObservation{});
    }

private:
    bool numeric_ = false;
};

void registerFixture()
{
    static const bool registered = [] {
        const auto hints = static_cast<std::uint8_t>(reflection::PropertyHint::Editor);
        reflection::registration::class_<BooleanObservation>("EditorSelectionBooleanObservation")
            .property("shared", &BooleanObservation::shared, {}, hints);
        reflection::registration::class_<NumberObservation>("EditorSelectionNumberObservation")
            .property("shared", &NumberObservation::shared, {}, hints);
        return true;
    }();
    (void)registered;
}
} // namespace

bool EditorInspectorSelectionSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-INSPECTOR-SELECTION-SMOKE-SCENARIO";
    if (scenario != "selection_same" && scenario != "selection_mixed"
        && scenario != "selection_incompatible" && scenario != "selection_ambiguous"
        && scenario != "selection_failed" && scenario != "selection_retry"
        && scenario != "selection_single")
        return false;
    registerFixture();
    shell.activate("workspace_3D");
    if (shell.submitCommand(CreateSceneObjectCommand("Selection A")).status
        != EditorResponseStatus::Completed)
        return false;
    const auto first = shell.sceneDocument().selectedObject();
    if (shell.submitCommand(CreateSceneObjectCommand("Selection B")).status
        != EditorResponseStatus::Completed)
        return false;
    const auto second = shell.sceneDocument().selectedObject();
    auto* a = shell.sceneDocument().scene().findObject(first);
    auto* b = shell.sceneDocument().scene().findObject(second);
    if (scenario == "selection_mixed" || scenario == "selection_single")
        b->set_local_transform(scene::Transform::fromTranslation({2, 0, 0}));
    if (scenario == "selection_incompatible" || scenario == "selection_ambiguous") {
        a->addComponent<SelectionSchemaFixture>(false);
        b->addComponent<SelectionSchemaFixture>(true);
        // A different real component also proves missing fields are not guessed.
        if (!shell.sceneDocument().addComponent("CameraComponent", first))
            return false;
        if (scenario == "selection_ambiguous")
            a->addComponent<SelectionSchemaFixture>(true);
    }
    SelectSceneObjectsCommand selection;
    selection.objectIds = {first.value(), second.value()};
    selection.primaryObjectId = first.value();
    if (shell.submitCommand(std::move(selection)).status != EditorResponseStatus::Completed)
        return false;
    const auto version = shell.stateVersion();
    const auto result = shell.submitCommand(GetSceneInspectorSelectionQuery{});
    diagnostic = "EDITOR-INSPECTOR-SELECTION-SMOKE-QUERY";
    if (result.status != EditorResponseStatus::Completed || !result.hasSceneInspectorSelection
        || result.sceneInspectorSelection.selectedCount != 2 || shell.stateVersion() != version
        || shell.inspectorState() != EditorShell::InspectorState::Ready)
        return false;
    const auto& snapshot = result.sceneInspectorSelection;
    using Status = EditorSceneInspectorSelection::Status;
    const auto hasStatus = [&](Status status) {
        return std::any_of(snapshot.fields.begin(), snapshot.fields.end(), [=](const auto& field) {
            return field.status == status;
        });
    };
    if (scenario == "selection_ambiguous") {
        if (snapshot.diagnostic != "EDITOR-INSPECTOR-SELECTION-COMPONENT-AMBIGUOUS")
            return false;
    } else if (!snapshot.diagnostic.empty())
        return false;
    if (scenario == "selection_incompatible"
        && (!hasStatus(Status::Incompatible) || !hasStatus(Status::Missing)))
        return false;
    if (scenario == "selection_mixed"
        && !std::any_of(snapshot.fields.begin(), snapshot.fields.end(), [](const auto& field) {
               return field.mixed;
           }))
        return false;
    if (shell.acceptsInspectorInput("scene_name")
        || shell.acceptsInspectorInput("scene_component_add"))
        return false;
    shell.entryChanged("scene_name", "must not rename primary");
    shell.activate("scene_active");
    if (a->name() != "Selection A" || !a->active() || shell.stateVersion() != version)
        return false;
    if (scenario == "selection_failed" || scenario == "selection_retry") {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            if (shell.retrySceneInspector())
                return false;
        }
        if (shell.inspectorState() != EditorShell::InspectorState::Failed
            || !shell.runtime().cancelInput().ok())
            return false;
        if (scenario == "selection_retry"
            && !EditorControlInput::click(shell, "scene_inspector_retry"))
            return false;
    }
    if (scenario == "selection_single") {
        if (shell.submitCommand(SelectSceneObjectCommand{first.value()}).status
                != EditorResponseStatus::Completed
            || !shell.acceptsInspectorInput("scene_name"))
            return false;
        const auto current = shell.submitCommand(GetSceneInspectorSelectionQuery{});
        if (!current.hasSceneInspectorSelection
            || current.sceneInspectorSelection.selectedCount != 1)
            return false;
    }
    diagnostic.clear();
    std::printf("PASS native Inspector selection scenario=%.*s, source objects=%llu/%llu\n",
        int(scenario.size()),
        scenario.data(),
        static_cast<unsigned long long>(first.value()),
        static_cast<unsigned long long>(second.value()));
    return true;
}
} // namespace arp::editor
