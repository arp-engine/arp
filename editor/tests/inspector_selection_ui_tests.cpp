#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_inspector_selection_smoke.h"
#include "editor/workspace/editor_scene_transaction.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <rapidjson/document.h>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;
using Response = EditorResponseStatus;
constexpr std::string_view scenarios[]{"selection_same",
    "selection_mixed",
    "selection_incompatible",
    "selection_ambiguous",
    "selection_failed",
    "selection_retry",
    "selection_single"};

void load(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
}

void run(const tests::RecoveryFixture& files, std::string_view scenario)
{
    EditorShell shell;
    load(shell, files);
    core::pmr::String diagnostic;
    if (!EditorInspectorSelectionSmoke::run(shell, scenario, diagnostic)) {
        std::fprintf(stderr,
            "selection smoke %.*s: %s\n",
            int(scenario.size()),
            scenario.data(),
            diagnostic.c_str());
        assert(false);
    }
    const auto draws = shell.drawCommands();
    assert(draws.ok() && !draws.commands().empty());
}

EditorResponse snapshot(EditorShell& shell)
{
    auto result = shell.submitCommand(GetSceneInspectorSelectionQuery{});
    assert(result.status == Response::Completed && result.hasSceneInspectorSelection);
    return result;
}

void queryAndProtocols(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    core::pmr::String diagnostic;
    assert(EditorInspectorSelectionSmoke::run(shell, "selection_mixed", diagnostic));
    const auto initial = shell.submitCommand(GetEditorStateQuery{}).snapshot;
    const auto before = snapshot(shell);
    assert(before.sceneInspectorSelection.objects.size() == 2
        && before.sceneInspectorSelection.documentEpoch);
    const auto version = shell.stateVersion();
    core::pmr::String hash, afterHash;
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), hash, diagnostic));
    tests::EditorProtocolFixture protocol(shell);
    for (const bool mcp : {false, true}) {
        const auto result = protocol.call("scene.inspector.selection.get", {}, mcp);
        rapidjson::Document json;
        json.Parse<rapidjson::kParseFullPrecisionFlag>(
            result.body_text().data(), result.body_text().size());
        assert(!json.HasParseError());
        const rapidjson::Value& response =
            mcp ? json["result"]["structuredContent"] : static_cast<const rapidjson::Value&>(json);
        assert(response["stateVersion"].GetUint64() == version);
        const auto& value = response["sceneInspectorSelection"];
        assert(value["readOnly"].GetBool() && value["selectedCount"].GetUint64() == 2
            && value["documentEpoch"].GetUint64() == before.sceneInspectorSelection.documentEpoch);
        assert(value["objects"].Size() == 2
            && value["fields"].Size() == before.sceneInspectorSelection.fields.size());
        for (rapidjson::SizeType i = 0; i < value["objects"].Size(); ++i) {
            const auto& object = value["objects"][i]["inspector"];
            const auto& source = before.sceneInspectorSelection.objects[i].inspector;
            assert(object["objectId"].GetUint64() == source.objectId
                && object["properties"].Size() == source.properties.size());
        }
        bool mixed = false;
        for (const auto& field : value["fields"].GetArray()) {
            mixed = mixed || field["mixed"].GetBool();
            for (const auto& member : field["members"].GetArray()) {
                const auto& p =
                    before.sceneInspectorSelection.objects[member["objectIndex"].GetUint64()]
                        .inspector.properties[member["propertyIndex"].GetUint64()];
                assert(member["objectId"].GetUint64() == p.objectId
                    && member["componentId"].GetUint64() == p.componentId);
            }
        }
        assert(mixed);
        for (const auto bad : {R"("objectId":1)",
                 R"("value":1)",
                 R"("idempotencyKey":"no-query-cache")",
                 R"("unexpected":true)"}) {
            const auto rejected = protocol.call("scene.inspector.selection.get", bad, mcp);
            assert(mcp ? rejected.body_text().find(R"("isError":true)") != std::string_view::npos
                       : rejected.status().value() == 400);
        }
        const auto stale =
            protocol.call("scene.inspector.selection.get", R"("expectedStateVersion":0)", mcp);
        assert(stale.body_text().find("stale_state_version") != std::string_view::npos);
    }
    assert(shell.stateVersion() == version);
    assert(EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), afterHash, diagnostic)
        && hash == afterHash);
    assert(shell.submitCommand(GetEditorStateQuery{}).snapshot.sceneDirty == initial.sceneDirty);
    EditorResponse offThread;
    std::thread worker([&] {
        offThread = shell.submitCommand(GetSceneInspectorSelectionQuery{});
    });
    worker.join();
    assert(offThread.status == Response::Rejected
        && offThread.diagnostic == EditorDiagnosticCode::WrongOwnerThread);
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        core::CpuMemoryQuotaScope scope(quota);
        const auto rejected = shell.submitCommand(GetSceneInspectorSelectionQuery{});
        assert(rejected.status == Response::Failed
            && rejected.diagnostic == EditorDiagnosticCode::CommandPreparationFailed
            && !rejected.hasSceneInspectorSelection);
    }
    assert(shell.stateVersion() == version && before.sceneInspectorSelection.objects.size() == 2);
    std::puts(
        "PASS owner-thread typed/REST/MCP exact selection query, strict fields, stale/OOM rejection, unchanged Scene/version");
}

void completeSelectionKeyAndReplacement(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    core::pmr::String diagnostic;
    assert(EditorInspectorSelectionSmoke::run(shell, "selection_same", diagnostic));
    const auto firstSnapshot = snapshot(shell);
    const auto primary = firstSnapshot.sceneInspectorSelection.objects[0].inspector.objectId;
    const auto second = firstSnapshot.sceneInspectorSelection.objects[1].inspector.objectId;
    const auto oldLabel = EditorControlInput::find(shell, "scene_selection_readonly");
    assert(oldLabel.valid());
    const auto third = shell.sceneDocument().createObject("Selection C");
    SelectSceneObjectsCommand select;
    select.primaryObjectId = primary;
    select.objectIds = {primary, third.value()};
    assert(shell.submitCommand(select).status == Response::Completed);
    assert(!shell.runtime().canvas().findWidget(oldLabel));
    const auto changed = snapshot(shell);
    assert(changed.sceneInspectorSelection.objects[1].inspector.objectId == third.value()
        && firstSnapshot.sceneInspectorSelection.objects[1].inspector.objectId == second);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!shell.retrySceneInspector());
    }
    assert(shell.inspectorState() == EditorShell::InspectorState::Failed);
    // Same primary, different complete selection must recover from the old failed attempt.
    select.objectIds = {primary, second};
    assert(shell.submitCommand(select).status == Response::Completed);
    assert(shell.inspectorState() == EditorShell::InspectorState::Ready);
    EditorSceneDocument candidate(false);
    const auto incoming = candidate.createObject("Incoming");
    auto token = shell.prepareSceneReplacement(candidate, diagnostic);
    assert(token
        && shell.sceneReplacementReadiness(*token, candidate)
            == EditorShell::SceneReplacementResult::Ready);
    select.objectIds = {primary, third.value()};
    // execute does not advance dispatcher version: the view key still invalidates preparation.
    assert(shell.execute(select).success);
    assert(shell.sceneReplacementReadiness(*token, candidate)
        == EditorShell::SceneReplacementResult::Stale);
    token = shell.prepareSceneReplacement(candidate, diagnostic);
    const auto oldOwner = shell.sceneDocument().ownerTicket();
    assert(token
        && shell.commitSceneReplacement(*token, candidate)
            == EditorShell::SceneReplacementResult::Committed);
    assert(shell.retrySceneInspector() && shell.sceneDocument().selectedObject() == incoming);
    const auto after = snapshot(shell);
    assert(after.sceneInspectorSelection.selectedCount == 1
        && !oldOwner.sameCurrentEpoch(shell.sceneDocument().ownerTicket())
        && !EditorControlInput::find(shell, "scene_selection_readonly").valid());
    assert(shell.acceptsInspectorInput("scene_name"));
    // Internal candidate commit does not publish a document epoch. The full
    // authoring command does, independently of the opaque Scene owner ticket.
    assert(shell.submitCommand(NewSceneCommand{true}).status == Response::Completed);
    assert(snapshot(shell).sceneInspectorSelection.documentEpoch
        != firstSnapshot.sceneInspectorSelection.documentEpoch);
    std::puts(
        "PASS full selection key, independent old values, failure-attempt replacement and Scene epoch handoff");
}

void budgetAndHistory(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    load(shell, files);
    core::pmr::String diagnostic;
    assert(EditorInspectorSelectionSmoke::run(shell, "selection_same", diagnostic));
    const auto initial = snapshot(shell);
    const auto primary = initial.sceneInspectorSelection.objects[0].inspector.objectId;
    assert(shell.submitCommand(SetSceneObjectNameCommand("Changed")).status == Response::Completed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == Response::Completed);
    (void)snapshot(shell);
    assert(shell.submitCommand(RedoSceneCommand{}).status == Response::Completed);
    assert(shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(primary))->name()
        == "Changed");
    SelectSceneObjectsCommand selection;
    selection.primaryObjectId = primary;
    selection.objectIds.push_back(primary);
    for (std::size_t i = 0; i < EditorSceneInspectorSelection::editorBudget.maxObjects; ++i)
        selection.objectIds.push_back(shell.sceneDocument().createObject("Budget member").value());
    assert(shell.submitCommand(selection).status == Response::Completed);
    const auto overloaded = snapshot(shell);
    assert(overloaded.sceneInspectorSelection.selectedCount
            == EditorSceneInspectorSelection::editorBudget.maxObjects + 1
        && overloaded.sceneInspectorSelection.diagnostic == "EDITOR-INSPECTOR-SELECTION-BUDGET"
        && overloaded.sceneInspectorSelection.objects.empty());
    assert(shell.submitCommand(SelectSceneObjectCommand{primary}).status == Response::Completed
        && shell.acceptsInspectorInput("scene_name"));
    SceneInspectorProperty wide;
    wide.kind = SceneInspectorPropertyKind::UnsignedInteger;
    wide.unsignedIntegerValue = std::numeric_limits<std::uint64_t>::max();
    assert(sceneInspectorDisplayValue(wide) == "18446744073709551615");
    std::puts(
        "PASS query preserves Redo, explicit selection budget and recovery, full uint64 display");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        for (const auto scenario : scenarios)
            run(files, scenario);
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned cycle = 0; cycle < 20; ++cycle) {
            run(files, scenarios[cycle % std::size(scenarios)]);
            const auto current = core::defaultCpuAllocator().stats();
            assert(current.liveBytes == baseline.liveBytes
                && current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 ordinary warmed selection/query/UI/failure/retry/destruction cycles: core live bytes/count delta=0");
    } else {
        for (const auto scenario : scenarios)
            run(files, scenario);
        queryAndProtocols(files);
        completeSelectionKeyAndReplacement(files);
        budgetAndHistory(files);
    }
}
