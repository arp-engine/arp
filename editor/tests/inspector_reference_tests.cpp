#include "2d/physics_components.h"
#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_inspector_reference_smoke.h"
#include "editor/workspace/editor_scene_document.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::editor;

void requiredReferenceDoesNotSelectFirstBody()
{
    const physics2d::WorldConfig config{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
    auto world = core::makeShared<physics2d::PhysicsWorld2D>(config);
    assert(world->valid());
    EditorSceneDocument document(false);
    document.setPhysicsWorld2DProvider([world] {
        return world;
    });
    const auto owner = document.createObject("Joint");
    assert(document.addComponent("Joint2DComponent", owner));
    const auto target = document.createObject("First body");
    assert(document.addComponent("RigidBody2DComponent", target));
    const auto properties = buildSceneInspectorProperties(document, nullptr, owner);
    const auto property = std::find_if(properties.begin(), properties.end(), [](const auto& p) {
        return p.kind == SceneInspectorPropertyKind::SceneObjectReference && p.name == "bodyA";
    });
    assert(property != properties.end() && property->sceneObjectReferenceValue == 0);
    const auto selected = std::find_if(
        property->enumOptions.begin(), property->enumOptions.end(), [&](const auto& option) {
            return option.value == property->integerValue;
        });
    assert(selected != property->enumOptions.end());
    std::printf("required empty reference: actual ID=0, displayed=%s, commandValue=%s\n",
        selected->name.c_str(),
        selected->commandValue.c_str());
    std::fflush(stdout);
    assert(selected->commandValue == "0" && selected->name == "<Unassigned>");
    assert(!property->resourceDiagnostic.empty());
}

SceneInspectorProperty reference(
    std::uint64_t id, bool optional = false, std::string_view type = "RigidBody2DComponent")
{
    SceneInspectorProperty property;
    property.kind = SceneInspectorPropertyKind::SceneObjectReference;
    property.sceneObjectType = type;
    property.sceneObjectReferenceValue = id;
    property.resourceOptional = optional;
    property.editable = true;
    return property;
}

const SceneInspectorEnumOption& selected(const SceneInspectorProperty& property)
{
    const auto found = std::find_if(
        property.enumOptions.begin(), property.enumOptions.end(), [&](const auto& option) {
            return option.value == property.integerValue;
        });
    assert(found != property.enumOptions.end());
    return *found;
}

void referenceStatesAndAllocationFailure()
{
    EditorSceneDocument document(false);
    auto& first = document.scene().createObject("Duplicate");
    auto& second = document.scene().createObject("Duplicate");
    auto& wrongType = document.scene().createObject("Not a body");
    // These are schema-only components; presentation must not initialize physics.
    first.addComponent<physics2d::RigidBody2DComponent>(nullptr);
    second.addComponent<physics2d::RigidBody2DComponent>(nullptr);
    const auto check = [&](SceneInspectorProperty input,
                           std::string_view name,
                           std::string_view diagnostic,
                           std::size_t count) {
        const auto before = input.sceneObjectReferenceValue;
        const auto result = EditorSceneObjectReferenceChoices::build(document.scene(), input);
        char value[32];
        std::snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(before));
        assert(input.sceneObjectReferenceValue == before && input.enumOptions.empty());
        assert(result.sceneObjectReferenceValue == before && result.editable == input.editable);
        assert(result.enumOptions.size() == count && selected(result).commandValue == value);
        assert(selected(result).name == name
            && result.resourceDiagnostic.find(diagnostic) != core::pmr::String::npos);
        for (std::size_t i = 0; i < result.enumOptions.size(); ++i)
            assert(result.enumOptions[i].value == int(i));
        return result;
    };
    check(reference(0), "<Unassigned>", "Target is required", 3);
    check(reference(0, true), "<None>", "", 3);
    const auto valid = check(reference(second.id().value()), "Duplicate", "", 2);
    assert(valid.resourceDiagnostic.empty() && valid.integerValue == 1);
    const auto optional = check(reference(second.id().value(), true), "Duplicate", "", 3);
    assert(optional.integerValue == 2 && optional.resourceDiagnostic.empty());
    const auto missingId = std::numeric_limits<std::uint64_t>::max();
    const auto missing =
        check(reference(missingId), "<Missing: 18446744073709551615>", "Missing object", 3);
    assert(missing.resourceDiagnostic.find("18446744073709551615") != core::pmr::String::npos);
    assert(sceneInspectorDisplayValue(missing) == "<Missing: 18446744073709551615>");
    const auto missingOptional =
        check(reference(missingId, true), "<Missing: 18446744073709551615>", "Missing object", 4);
    assert(missingOptional.integerValue == 3);
    const auto mismatch = EditorSceneObjectReferenceChoices::build(
        document.scene(), reference(wrongType.id().value()));
    assert(selected(mismatch).name.find("<Wrong type:") == 0
        && mismatch.resourceDiagnostic.find("Wrong type: RigidBody2DComponent")
            != core::pmr::String::npos);
    const auto wildcard = EditorSceneObjectReferenceChoices::build(
        document.scene(), reference(wrongType.id().value(), false, "*"));
    assert(selected(wildcard).name == "Not a body" && wildcard.resourceDiagnostic.empty()
        && wildcard.enumOptions.size() == document.scene().objects().size());
    auto readonly = reference(first.id().value());
    readonly.editable = false;
    assert(!EditorSceneObjectReferenceChoices::build(document.scene(), readonly).editable);
    EditorSceneDocument empty(false);
    const auto noCandidates = EditorSceneObjectReferenceChoices::build(empty.scene(), reference(0));
    assert(noCandidates.enumOptions.size() == 1 && selected(noCandidates).commandValue == "0");
    const auto gone = first.id().value();
    assert(document.scene().destroyObject(first.id()).ok());
    const auto stale = EditorSceneObjectReferenceChoices::build(document.scene(), reference(gone));
    assert(stale.resourceDiagnostic.find("Missing object") == 0);

    // Fail copying an already prepared property; caller-owned prior options survive.
    const auto baseline = core::defaultCpuAllocator().stats();
    bool rejected = false;
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    try {
        core::CpuMemoryQuotaScope scope(quota);
        (void)EditorSceneObjectReferenceChoices::build(document.scene(), missing);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    quota.reset();
    assert(rejected && selected(missing).commandValue == "18446744073709551615");
    const auto after = core::defaultCpuAllocator().stats();
    assert(after.liveBytes == baseline.liveBytes
        && after.allocationCount - after.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    std::puts(
        "PASS reference states, full uint64 ID, duplicate names, wildcard, readonly, no candidates and allocation rollback");
}

void shellAndProtocols(const tests::RecoveryFixture& files, std::string_view scenario)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    core::pmr::String diagnostic;
    const bool ready = EditorInspectorReferenceSmoke::run(shell, scenario, diagnostic);
    if (!ready)
        std::fprintf(stderr,
            "reference fixture %.*s failed: %s\n",
            int(scenario.size()),
            scenario.data(),
            diagnostic.c_str());
    assert(ready);
    if (scenario == "ref_repaired") {
        const auto repaired = shell.submitCommand(GetSceneInspectorQuery{});
        const auto findBodyA = [](const auto& properties) {
            return std::find_if(properties.begin(), properties.end(), [](const auto& p) {
                return p.name == "bodyA"
                    && p.kind == SceneInspectorPropertyKind::SceneObjectReference;
            });
        };
        const auto property = findBodyA(repaired.sceneInspector.properties);
        assert(property != repaired.sceneInspector.properties.end());
        const auto targetId = property->sceneObjectReferenceValue;
        assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
        const auto undone = shell.submitCommand(GetSceneInspectorQuery{});
        const auto empty = findBodyA(undone.sceneInspector.properties);
        assert(empty != undone.sceneInspector.properties.end()
            && empty->sceneObjectReferenceValue == 0 && selected(*empty).name == "<Unassigned>"
            && !empty->resourceDiagnostic.empty());
        SetSceneComponentPropertyCommand noOp("bodyA");
        noOp.objectId = empty->objectId;
        noOp.componentId = empty->componentId;
        noOp.valueKind = ScenePropertyValueKind::SceneObjectReference;
        const auto unchanged = shell.submitCommand(std::move(noOp));
        assert(unchanged.status == EditorResponseStatus::Completed
            && unchanged.stateVersion == undone.stateVersion);
        assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
        const auto redone = shell.submitCommand(GetSceneInspectorQuery{});
        const auto restored = findBodyA(redone.sceneInspector.properties);
        assert(restored != redone.sceneInspector.properties.end()
            && restored->sceneObjectReferenceValue == targetId
            && restored->resourceDiagnostic.empty());
        const auto* control = shell.runtime().findControlObject(
            EditorControlInput::find(shell, scenePropertyControlSource(*restored)));
        assert(control && control->value().index == std::uint32_t(restored->integerValue));
    }
    const auto state = shell.submitCommand(GetEditorStateQuery{});
    const auto query = shell.submitCommand(GetSceneInspectorQuery{});
    assert(query.status == EditorResponseStatus::Completed && query.hasSceneInspector
        && query.stateVersion == state.stateVersion);
    const auto bodyA = std::find_if(query.sceneInspector.properties.begin(),
        query.sceneInspector.properties.end(),
        [](const auto& p) {
            return p.name == "bodyA" && p.kind == SceneInspectorPropertyKind::SceneObjectReference;
        });
    assert(bodyA != query.sceneInspector.properties.end());
    // The current shared UI atlas contract accepts single-line text only.
    assert(bodyA->resourceDiagnostic.find_first_of("\r\n") == core::pmr::String::npos);
    tests::EditorProtocolFixture protocol(shell);
    for (bool mcp : {false, true}) {
        const auto response = protocol.call("scene.inspector.get", {}, mcp);
        assert(response.status().value() == 200);
        const auto text = response.body_text();
        assert(text.find(selected(*bodyA).name) != std::string_view::npos);
        assert(text.find("sceneObjectReference") != std::string_view::npos);
        if (!bodyA->resourceDiagnostic.empty())
            assert(text.find("diagnostic") != std::string_view::npos);
    }
    // Refresh under OOM cannot change native reference IDs and retry is usable.
    const auto id = bodyA->sceneObjectReferenceValue;
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!shell.retrySceneInspector());
    }
    assert(shell.inspectorState() == EditorShell::InspectorState::Failed
        && shell.retrySceneInspector());
    const auto retried = shell.submitCommand(GetSceneInspectorQuery{});
    const auto current = std::find_if(retried.sceneInspector.properties.begin(),
        retried.sceneInspector.properties.end(),
        [](const auto& p) {
            return p.name == "bodyA" && p.kind == SceneInspectorPropertyKind::SceneObjectReference;
        });
    assert(current != retried.sceneInspector.properties.end()
        && current->sceneObjectReferenceValue == id);
    assert(retried.stateVersion == query.stateVersion);
}

void lifetime(const tests::RecoveryFixture& files)
{
    constexpr const char* scenarios[] = {
        "ref_unassigned", "ref_missing", "ref_type", "ref_repaired"};
    for (const auto scenario : scenarios)
        shellAndProtocols(files, scenario);
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        shellAndProtocols(files, scenarios[cycle % 4]);
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 ordinary warmed reference UI/query/repair/corruption/OOM/retry/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(files);
    else {
        requiredReferenceDoesNotSelectFirstBody();
        referenceStatesAndAllocationFailure();
        for (const auto scenario : {"ref_unassigned", "ref_missing", "ref_type", "ref_repaired"})
            shellAndProtocols(files, scenario);
    }
}
