#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_inspector_resource_reference_smoke.h"
#include "editor/workspace/editor_scene_document.h"
#include "tests/fixtures/scene_2d_fixture.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;

const SceneInspectorEnumOption& selected(const SceneInspectorProperty& property)
{
    const auto found = std::find_if(
        property.enumOptions.begin(), property.enumOptions.end(), [&](const auto& option) {
            return option.value == property.integerValue;
        });
    assert(found != property.enumOptions.end());
    return *found;
}

SceneInspectorProperty reference(std::string_view id = {}, bool optional = false)
{
    SceneInspectorProperty property;
    property.kind = SceneInspectorPropertyKind::ResourceReference;
    property.editable = true;
    property.resourceOptional = optional;
    property.resourceType = "SpriteResource";
    property.stringValue = id;
    return property;
}

void requiredResourceDoesNotSelectFirstSprite()
{
    resource::ResourceManager resources;
    assert(arp::tests::loadSpriteFixture(resources));
    EditorSceneDocument document(false);
    const auto object = document.createObject("Resource diagnostic");
    assert(document.addComponent("SpriteRendererComponent", object));
    const auto properties = buildSceneInspectorProperties(document, &resources, object);
    const auto property = std::find_if(properties.begin(), properties.end(), [](const auto& p) {
        return p.name == "spriteResourceId";
    });
    assert(property != properties.end() && property->stringValue.empty());
    const auto option = std::find_if(
        property->enumOptions.begin(), property->enumOptions.end(), [&](const auto& entry) {
            return entry.value == property->integerValue;
        });
    assert(option != property->enumOptions.end());
    std::printf("required empty resource: actual ID='', displayed=%s, commandValue=%s\n",
        option->name.c_str(),
        option->commandValue.c_str());
    std::fflush(stdout);
    assert(option->name == "<Unassigned>" && option->commandValue.empty()
        && !property->resourceDiagnostic.empty());
}

void resourceStatesAndAllocationFailure()
{
    resource::ResourceManager resources;
    assert(arp::tests::loadSpriteFixture(resources));
    const auto build = [&](SceneInspectorProperty source,
                           std::string_view label,
                           std::string_view diagnostic,
                           std::size_t count) {
        const auto result = EditorSceneResourceReferenceChoices::build(&resources, source);
        assert(result.stringValue == source.stringValue && source.enumOptions.empty());
        assert(selected(result).commandValue == source.stringValue && selected(result).name == label
            && result.enumOptions.size() == count);
        assert(result.resourceDiagnostic.find(diagnostic) != core::pmr::String::npos);
        for (std::size_t i = 0; i < result.enumOptions.size(); ++i)
            assert(result.enumOptions[i].value == int(i));
        assert(sceneInspectorDisplayValue(result) == selected(result).name);
        return result;
    };
    build(reference(), "<Unassigned>", "Resource is required", 3);
    assert(build(reference({}, true), "<None>", "", 3).resourceDiagnostic.empty());
    const auto valid = build(reference(arp::tests::spriteB), arp::tests::spriteB, "", 2);
    assert(valid.integerValue == 1 && valid.resourceDiagnostic.empty() && valid.editable);
    auto readonly = reference(arp::tests::spriteA);
    readonly.editable = false;
    assert(!EditorSceneResourceReferenceChoices::build(&resources, readonly).editable);
    constexpr auto absent = "00000000-0000-4000-8000-000000000099";
    const auto missing = build(reference(absent),
        "<Missing: 00000000-0000-4000-8000-000000000099>",
        "Missing resource",
        3);
    const auto wrong = build(reference(arp::tests::atlasA),
        "<Wrong type: 00000000-0000-4000-8000-000000000001>",
        "Wrong type: SpriteResource",
        3);
    assert(wrong.editable && missing.editable);
    const auto lines = EditorSceneResourceReferenceChoices::identifierLines(arp::tests::spriteA);
    assert(lines[0] == "00000000-0000-4000" && lines[1] == "-8000-000000000003");
    const auto invalid =
        build(reference("invalid\nresource"), "<Invalid resource ID>", "Invalid resource ID", 3);
    assert(invalid.stringValue == "invalid\nresource"
        && selected(invalid).name.find_first_of("\r\n") == core::pmr::String::npos);
    const auto invalidLines =
        EditorSceneResourceReferenceChoices::identifierLines(invalid.stringValue);
    assert(invalidLines[0] == "Invalid resource ID" && invalidLines[1].empty());
    assert(resources.declareResource(absent, "SpriteResource").success);
    build(reference(absent),
        "<Not Ready: 00000000-0000-4000-8000-000000000099>",
        "Resource is not Ready",
        3);
    const auto old = resources.find<scene2d::SpriteResource>(arp::tests::spriteA);
    assert(old.ready());
    const auto generation = resources.recordSnapshot(arp::tests::spriteA)->generation;
    assert(resources.unload(arp::tests::spriteA).success && !old.ready());
    build(reference(arp::tests::spriteA),
        "<Not Ready: 00000000-0000-4000-8000-000000000003>",
        "Resource is not Ready",
        2);
    assert(resources
            .load<scene2d::SpriteResource>({},
                core::pmr::String(arp::tests::spriteA),
                core::pmr::String(arp::tests::atlasA),
                core::pmr::String("left"))
            .success);
    assert(resources.recordSnapshot(arp::tests::spriteA)->generation > generation && !old.ready());
    build(reference(arp::tests::spriteA), arp::tests::spriteA, "", 2);

    resource::ResourceManager empty;
    const auto noCandidates = EditorSceneResourceReferenceChoices::build(&empty, reference());
    assert(noCandidates.enumOptions.size() == 1 && selected(noCandidates).name == "<Unassigned>"
        && noCandidates.editable);
    for (const bool optional : {false, true}) {
        const auto unavailable = EditorSceneResourceReferenceChoices::build(
            nullptr, reference(arp::tests::spriteA, optional));
        assert(!unavailable.editable && selected(unavailable).commandValue == arp::tests::spriteA
            && unavailable.resourceDiagnostic == "EDITOR-RESOURCE-MANAGER-UNAVAILABLE");
        const auto unassigned =
            EditorSceneResourceReferenceChoices::build(nullptr, reference({}, optional));
        assert(!unassigned.editable && selected(unassigned).commandValue.empty()
            && selected(unassigned).name == (optional ? "<None>" : "<Unassigned>"));
    }
    auto invalidType = reference(arp::tests::spriteA);
    invalidType.resourceType.clear();
    const auto refused = EditorSceneResourceReferenceChoices::build(&resources, invalidType);
    assert(!refused.editable && selected(refused).commandValue == arp::tests::spriteA
        && refused.resourceDiagnostic.find("exact resource type") != core::pmr::String::npos);

    const auto baseline = core::defaultCpuAllocator().stats();
    unsigned failures = 0;
    for (const std::size_t bytes : {1u, 128u, 512u, 1024u, 4096u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        try {
            core::CpuMemoryQuotaScope scope(quota);
            (void)EditorSceneResourceReferenceChoices::build(&resources, missing);
        } catch (const std::bad_alloc&) {
            ++failures;
        }
        quota.reset();
        assert(selected(missing).commandValue == absent && missing.editable);
        const auto after = core::defaultCpuAllocator().stats();
        assert(after.liveBytes == baseline.liveBytes
            && after.allocationCount - after.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    assert(failures >= 2);
    std::puts(
        "PASS resource states, exact ID/type, generation reload, readonly, manager/metadata/empty and OOM rollback");
}

void capacityIsNotATruncatedInventory()
{
    resource::ResourceManager resources;
    assert(arp::tests::loadSpriteFixture(resources));
    // The existing contract counts non-Ready exact-type records as well.
    for (std::size_t i = 2; i < EditorSceneResourceReferenceChoices::maxEntries; ++i) {
        char id[40];
        std::snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012zu", i + 100);
        assert(resources.declareResource(id, "SpriteResource").success);
    }
    const auto atLimit =
        EditorSceneResourceReferenceChoices::build(&resources, reference(arp::tests::spriteA));
    assert(
        atLimit.editable && atLimit.enumOptions.size() == 2 && atLimit.resourceDiagnostic.empty());
    assert(resources.declareResource("00000000-0000-4000-8000-000000000199", "SpriteResource")
            .success);
    const auto rejected =
        EditorSceneResourceReferenceChoices::build(&resources, reference(arp::tests::spriteA));
    assert(!rejected.editable && rejected.enumOptions.size() == 1
        && selected(rejected).commandValue == arp::tests::spriteA
        && selected(rejected).name == "<Unavailable: 00000000-0000-4000-8000-000000000003>"
        && rejected.resourceDiagnostic.find("exceeds caller entry limit")
            != core::pmr::String::npos);
    std::puts("PASS exact 32/33 record budget: no truncated choices or substitute selection");
}

void shellAndProtocols(const arp::editor::tests::RecoveryFixture& files, std::string_view scenario)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    core::pmr::String diagnostic;
    const auto ready = EditorInspectorResourceReferenceSmoke::run(shell, scenario, diagnostic);
    if (!ready)
        std::fprintf(stderr,
            "resource fixture %.*s failed: %s\n",
            int(scenario.size()),
            scenario.data(),
            diagnostic.c_str());
    assert(ready);
    const auto query = shell.submitCommand(GetSceneInspectorQuery{});
    const auto findProperty = [](const auto& properties) {
        return std::find_if(properties.begin(), properties.end(), [](const auto& p) {
            return p.name == "spriteResourceId";
        });
    };
    const auto property = findProperty(query.sceneInspector.properties);
    assert(query.hasSceneInspector && property != query.sceneInspector.properties.end());
    assert(property->resourceDiagnostic.find_first_of("\r\n") == core::pmr::String::npos);
    arp::editor::tests::EditorProtocolFixture protocol(shell);
    for (const bool mcp : {false, true}) {
        const auto response = protocol.call("scene.inspector.get", {}, mcp);
        assert(response.status().value() == 200);
        assert(response.body_text().find(selected(*property).name) != std::string_view::npos);
    }
    const auto set = [&](std::string_view id) {
        SetSceneComponentPropertyCommand command("spriteResourceId");
        command.objectId = property->objectId;
        command.componentId = property->componentId;
        command.valueKind = ScenePropertyValueKind::ResourceReference;
        command.stringValue = id;
        return shell.submitCommand(std::move(command));
    };
    // All protocol/UI values remain subject to the native current-record gate.
    const auto version = shell.stateVersion();
    for (const auto invalid : {"", arp::tests::atlasA, "00000000-0000-4000-8000-000000000099"})
        assert(
            set(invalid).status == EditorResponseStatus::Failed && shell.stateVersion() == version);
    if (scenario == "res_not_ready")
        assert(set(arp::tests::spriteA).status == EditorResponseStatus::Failed
            && shell.stateVersion() == version);
    if (scenario == "res_repaired") {
        assert(set(arp::tests::spriteA).status == EditorResponseStatus::Completed);
        assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
        auto undone = shell.submitCommand(GetSceneInspectorQuery{});
        assert(findProperty(undone.sceneInspector.properties)->stringValue == arp::tests::spriteB);
        assert(set(arp::tests::spriteB).stateVersion == undone.stateVersion);
        assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
        const auto redone = shell.submitCommand(GetSceneInspectorQuery{});
        const auto restored = findProperty(redone.sceneInspector.properties);
        assert(
            restored->stringValue == arp::tests::spriteA && restored->resourceDiagnostic.empty());
        const auto* control = shell.runtime().findControlObject(
            EditorControlInput::find(shell, scenePropertyControlSource(*restored)));
        assert(control && control->value().index == std::uint32_t(restored->integerValue));
    }
    const auto beforeRetry = shell.submitCommand(GetSceneInspectorQuery{});
    const auto value = findProperty(beforeRetry.sceneInspector.properties)->stringValue;
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!shell.retrySceneInspector());
    }
    assert(shell.inspectorState() == EditorShell::InspectorState::Failed
        && shell.retrySceneInspector());
    const auto retried = shell.submitCommand(GetSceneInspectorQuery{});
    assert(findProperty(retried.sceneInspector.properties)->stringValue == value
        && retried.stateVersion == beforeRetry.stateVersion);
    if (scenario == "res_stale") {
        shell.reportViewportDiagnostic("explicit fixture overwrites the shared Output status");
        const auto failure = EditorControlInput::find(shell, "scene_reference_failure");
        const auto draws = shell.drawCommands();
        assert(failure.valid() && draws.ok()
            && std::any_of(draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
                   return draw.widgetId == failure;
               }));
        assert(shell.projectResources()
                .load<scene2d::SpriteResource>({},
                    core::pmr::String(arp::tests::spriteB),
                    core::pmr::String(arp::tests::atlasB),
                    core::pmr::String("right"))
                .success);
        assert(set(arp::tests::spriteB).status == EditorResponseStatus::Completed);
        assert(!EditorControlInput::find(shell, "scene_reference_failure").valid());
    }
}

constexpr const char* scenarios[] = {"res_unassigned",
    "res_missing",
    "res_type",
    "res_not_ready",
    "res_capacity",
    "res_repaired",
    "res_stale"};

void lifetime(const arp::editor::tests::RecoveryFixture& files)
{
    for (const auto scenario : scenarios)
        shellAndProtocols(files, scenario);
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        shellAndProtocols(files, scenarios[cycle % std::size(scenarios)]);
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 ordinary warmed resource UI/query/repair/corruption/OOM/retry/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    arp::editor::tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(files);
    else {
        requiredResourceDoesNotSelectFirstSprite();
        resourceStatesAndAllocationFailure();
        capacityIsNotATruncatedInventory();
        for (const auto scenario : scenarios)
            shellAndProtocols(files, scenario);
    }
}
