#include "core/text/number_format.h"
#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/scene_persistence_fixture.h"
#include "editor/tests/fixtures/transform_selection_fixture.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <rapidjson/document.h>

namespace {
using namespace arp;
using namespace arp::editor;
using Fixture = tests::TransformSelectionFixture;
using Settings = EditorSceneTransformSettings;
using Status = EditorResponseStatus;
constexpr const char* labels[] = {
    "scene_transform_space", "scene_transform_pivot", "scene_transform_snap"};

Settings changed()
{
    Settings value;
    value.tool = EditorSceneTransformTool::Rotate;
    value.space = EditorSceneTransformSpace::Local;
    value.pivot = EditorSceneTransformPivot::Individual;
    value.snap.enabled = true;
    value.snap.moveStep = 0.12345678901234566;
    value.snap.rotateStepRadians = 0.9876543210987654;
    value.snap.scaleStep = 0.33333333333333331;
    return value;
}

std::array<core::pmr::String, 3> captions(EditorShell& shell)
{
    std::array<core::pmr::String, 3> result;
    for (unsigned i = 0; i != 3; ++i)
        result[i] = shell.runtime().canvas().findWidget(shell.id(labels[i]))->desc().text;
    return result;
}

using ProtocolFixture = tests::EditorProtocolFixture;

core::pmr::String settingsFields(const Settings& settings)
{
    core::pmr::String result = R"("settings":{"tool":")";
    result += Settings::toolName(settings.tool);
    result += R"(","space":")";
    result += Settings::spaceName(settings.space);
    result += R"(","pivot":")";
    result += Settings::pivotName(settings.pivot);
    result += R"(","snapEnabled":)";
    result += settings.snap.enabled ? "true" : "false";
    result += R"(,"moveStep":)";
    assert(core::appendFloating(result, settings.snap.moveStep));
    result += R"(,"rotateStepRadians":)";
    assert(core::appendFloating(result, settings.snap.rotateStepRadians));
    result += R"(,"scaleStep":)";
    assert(core::appendFloating(result, settings.snap.scaleStep));
    result += '}';
    return result;
}

void valuesAndIsolation(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    f.select(true);
    f.apply();
    f.history(true); // preferences must preserve this Redo
    auto initial = f.shell.submitCommand(GetEditorStateQuery{}).snapshot;
    const auto defaults = f.shell.sceneTransformSettings();
    assert(defaults.same(Settings{}) && initial.sceneTransformSettings.same(defaults));
    const auto version = f.shell.stateVersion();
    core::pmr::String hash, afterHash, diagnostic;
    assert(EditorSceneAuthoringCodec::contentHash(f.shell.sceneDocument(), hash, diagnostic));
    const auto value = changed();
    assert(
        f.shell.submitCommand(SetSceneTransformSettingsCommand{value}).status == Status::Completed);
    assert(f.shell.stateVersion() == version + 1);
    assert(f.shell.sceneTransformSettings().same(value));
    auto response = f.shell.submitCommand(GetSceneTransformSettingsQuery{});
    assert(response.status == Status::Completed && response.hasSceneTransformSettings
        && response.sceneTransformSettings.same(value) && !response.hasSnapshot);
    auto snapshot = f.shell.submitCommand(GetEditorStateQuery{}).snapshot;
    assert(snapshot.sceneTransformSettings.same(value));
    assert(snapshot.selection == initial.selection
        && snapshot.sceneCameraYaw == initial.sceneCameraYaw
        && snapshot.sceneCameraPitch == initial.sceneCameraPitch
        && snapshot.sceneCameraDistance == initial.sceneCameraDistance
        && core::distance(snapshot.sceneCameraTarget, initial.sceneCameraTarget) == 0
        && snapshot.sceneDirty == initial.sceneDirty && f.shell.sceneViewportObjectSelected(f.child)
        && f.shell.sceneViewportObjectSelected(f.second));
    assert(EditorSceneAuthoringCodec::contentHash(f.shell.sceneDocument(), afterHash, diagnostic)
        && hash == afterHash);
    const auto text = captions(f.shell);
    assert(text[0] == "Local" && text[1] == "Pivot Individual"
        && text[2].find("Snap On | Move 0.123457") == 0);
    assert(
        f.shell.submitCommand(SetSceneTransformSettingsCommand{value}).status == Status::Completed);
    assert(f.shell.stateVersion() == version + 1);
    f.history(false);
    assert(f.local(f.first).translation.x == 3);
    f.history(true);
    assert(f.local(f.first).translation.x == 2 && f.shell.sceneTransformSettings().same(value));
    std::puts(
        "PASS complete values/query/snapshot, single version/no-op, labels, Scene/selection/camera/history isolation");
}

void invalidAndPreparationFailure(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    const auto start = f.shell.sceneTransformSettings();
    const auto text = captions(f.shell);
    const auto version = f.shell.stateVersion();
    const auto reject = [&](Settings value) {
        assert(f.shell.submitCommand(SetSceneTransformSettingsCommand{value}).diagnostic
            == EditorDiagnosticCode::InvalidCommandPayload);
        assert(f.shell.sceneTransformSettings().same(start) && f.shell.stateVersion() == version
            && captions(f.shell) == text);
    };
    auto bad = changed();
    bad.tool = static_cast<EditorSceneTransformTool>(255);
    reject(bad);
    bad = changed();
    bad.space = static_cast<EditorSceneTransformSpace>(255);
    reject(bad);
    bad = changed();
    bad.pivot = static_cast<EditorSceneTransformPivot>(255);
    reject(bad);
    for (const auto value : {0.0,
             -1.0,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN()}) {
        for (unsigned field = 0; field != 3; ++field) {
            bad = changed();
            bad.snap.enabled = false;
            if (field == 0)
                bad.snap.moveStep = value;
            if (field == 1)
                bad.snap.rotateStepRadians = value;
            if (field == 2)
                bad.snap.scaleStep = value;
            reject(bad);
        }
    }
    // Build payload and quota outside the failure scope. Check labels as well as scalar state.
    EditorCommandPayload payload = SetSceneTransformSettingsCommand{changed()};
    for (const auto budget : {1u, 64u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
        const auto before = core::defaultCpuAllocator().stats();
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(!f.shell.execute(payload).success);
            const auto query = f.shell.execute(GetSceneTransformSettingsQuery{});
            assert(query.success && query.sceneTransformSettings.same(start));
        }
        assert(quota->liveBytes() == 0);
        assert(core::defaultCpuAllocator().stats().liveBytes == before.liveBytes);
        assert(f.shell.sceneTransformSettings().same(start) && captions(f.shell) == text
            && f.shell.stateVersion() == version);
    }
    assert(f.shell.submitCommand(std::move(payload)).status == Status::Completed);
    // Broken declared widget binding is diagnosed before publishing any setting.
    assert(f.shell.runtime().canvas().removeSubtree(f.shell.id("scene_transform_pivot")).ok());
    const auto accepted = f.shell.sceneTransformSettings();
    assert(f.shell.submitCommand(SetSceneTransformSettingsCommand{}).status == Status::Failed);
    assert(f.shell.sceneTransformSettings().same(accepted));
    std::puts(
        "PASS invalid enums/disabled invalid steps, allocation failure retains labels/scalars, missing widget fails closed, retry succeeds");
}

void busyAndLegacy(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    const auto checkBusy = [&] {
        const auto version = f.shell.stateVersion();
        const auto value = f.shell.sceneTransformSettings();
        assert(f.shell.submitCommand(SetSceneTransformSettingsCommand{value}).diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        assert(
            f.shell.submitCommand(SetSceneTransformSpaceCommand{EditorSceneTransformSpace::Local})
                .diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        assert(
            f.shell.submitCommand(SetSceneTransformPivotCommand{EditorSceneTransformPivot::Center})
                .diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        assert(f.shell.submitCommand(SetSceneTransformSnapCommand{}).diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        assert(f.shell.submitCommand(ActivateControlCommand("tool_2")).diagnostic
            == EditorDiagnosticCode::WorkspaceBusy);
        const auto query = f.shell.submitCommand(GetSceneTransformSettingsQuery{});
        assert(query.status == Status::Completed && query.sceneTransformSettings.same(value)
            && f.shell.stateVersion() == version);
    };
    f.shell.activate("menu_Project");
    f.shell.activate("project_ui_new");
    checkBusy();
    f.shell.activate("project_ui_cancel");
    assert(f.shell.submitCommand(BeginSceneTransformEditCommand{f.first.value()}).status
        == Status::Completed);
    checkBusy();
    assert(f.shell.submitCommand(EndSceneTransformEditCommand{f.first.value()}).status
        == Status::Completed);
    assert(f.shell.submitCommand(BeginSceneSelectionTransformEditCommand{}).status
        == Status::Completed);
    checkBusy();
    assert(f.shell
               .submitCommand(EndSceneSelectionTransformEditCommand{
                   f.shell.activeSceneSelectionTransformEditId()})
               .status
        == Status::Completed);
    SetGamePreviewStateCommand play("settings Play gate");
    play.running = true;
    assert(f.shell.submitCommand(play).status == Status::Completed);
    checkBusy();
    play.running = false;
    play.starting = true;
    assert(f.shell.submitCommand(play).status == Status::Completed);
    checkBusy();
    play.starting = false;
    assert(f.shell.submitCommand(play).status == Status::Completed);
    // A queued save remains logically pending until its completion command is consumed.
    const auto saved = f.shell.submitCommand(SaveSceneCommand("settings.arp.yaml"));
    assert(saved.status == Status::Completed);
    checkBusy();
    assert(tests::ScenePersistenceFixture::finish(f.shell, saved).scenePersistenceTaskStatus
        == ScenePersistenceTaskStatus::Succeeded);
    f.shell.activate("tool_1");
    assert(f.shell.sceneTransformSettings().tool == EditorSceneTransformTool::Move);
    f.shell.activate("scene_transform_space");
    f.shell.activate("scene_transform_pivot");
    f.shell.activate("scene_transform_snap");
    const auto value = f.shell.sceneTransformSettings();
    assert(value.space == EditorSceneTransformSpace::Local
        && value.pivot == EditorSceneTransformPivot::Center && value.snap.enabled);
    const auto version = f.shell.stateVersion();
    assert(
        f.shell.submitCommand(SetSceneTransformSettingsCommand{value}).status == Status::Completed
        && f.shell.stateVersion() == version);
    std::puts(
        "PASS modal/single/group/Play/IO Busy including same-value, readonly query available, legacy/tool buttons share settings");
}

void protocol(const tests::RecoveryFixture& files)
{
    Fixture f(files);
    ProtocolFixture remote(f.shell);
    const auto value = changed();
    const auto fields = settingsFields(value);
    for (const bool mcp : {false, true}) {
        auto body = fields;
        body +=
            mcp ? R"(,"idempotencyKey":"mcp-settings")" : R"(,"idempotencyKey":"rest-settings")";
        assert(remote.call("scene.transform.settings.set", body, mcp).status().value() == 200);
        assert(f.shell.sceneTransformSettings().same(value));
        const auto version = f.shell.stateVersion();
        assert(remote.call("scene.transform.settings.set", body, mcp).status().value() == 200
            && f.shell.stateVersion() == version);
        for (unsigned field = 0; field != 7; ++field) {
            auto other = value;
            if (field == 0)
                other.tool = EditorSceneTransformTool::Move;
            if (field == 1)
                other.space = EditorSceneTransformSpace::World;
            if (field == 2)
                other.pivot = EditorSceneTransformPivot::Active;
            if (field == 3)
                other.snap.enabled = false;
            if (field == 4)
                other.snap.moveStep = std::nextafter(other.snap.moveStep, 1.0);
            if (field == 5)
                other.snap.rotateStepRadians = std::nextafter(other.snap.rotateStepRadians, 1.0);
            if (field == 6)
                other.snap.scaleStep = std::nextafter(other.snap.scaleStep, 1.0);
            auto conflict = settingsFields(other);
            conflict += mcp ? R"(,"idempotencyKey":"mcp-settings")"
                            : R"(,"idempotencyKey":"rest-settings")";
            assert(remote.call("scene.transform.settings.set", conflict, mcp)
                       .body_text()
                       .find("idempotency_key_conflict")
                != std::string_view::npos);
        }
        auto stale = fields;
        stale += R"(,"expectedStateVersion":0)";
        assert(remote.call("scene.transform.settings.set", stale, mcp)
                   .body_text()
                   .find("stale_state_version")
            != std::string_view::npos);
        const auto result = remote.call("scene.transform.settings.get", {}, mcp);
        rapidjson::Document json;
        json.Parse<rapidjson::kParseFullPrecisionFlag>(
            result.body_text().data(), result.body_text().size());
        assert(!json.HasParseError());
        const auto& settings = mcp ? json["result"]["structuredContent"]["sceneTransformSettings"]
                                   : json["sceneTransformSettings"];
        assert(settings["moveStep"].GetDouble() == value.snap.moveStep
            && settings["rotateStepRadians"].GetDouble() == value.snap.rotateStepRadians
            && settings["scaleStep"].GetDouble() == value.snap.scaleStep);
        assert(f.shell.stateVersion() == version);
        const std::string_view malformed[] = {R"("settings":{})",
            R"("settings":{"tool":"rotate","space":"local","pivot":"active","snapEnabled":false,"moveStep":1,"rotateStepRadians":1,"moveStep":2})",
            R"("settings":{"tool":"rotate","space":"local","pivot":"active","snapEnabled":false,"moveStep":1,"rotateStepRadians":1,"scaleStep":1,"extra":1})",
            R"("settings":{"tool":"Move","space":"local","pivot":"active","snapEnabled":false,"moveStep":1,"rotateStepRadians":1,"scaleStep":1})",
            R"("settings":{"tool":"move","space":"local","pivot":"active","snapEnabled":0,"moveStep":1,"rotateStepRadians":1,"scaleStep":1})",
            R"("settings":{"tool":"move","space":"local","pivot":"active","snapEnabled":false,"moveStep":"1","rotateStepRadians":1,"scaleStep":1})",
            R"("settings":{"tool":"move","space":"local","pivot":"active","snapEnabled":false,"moveStep":0,"rotateStepRadians":1,"scaleStep":1})",
            R"("settings":null)",
            R"("tool":"move")",
            R"("settings":{},"settings":{})"};
        for (auto invalid : malformed) {
            const auto rejected = remote.call("scene.transform.settings.set", invalid, mcp);
            assert(mcp ? rejected.body_text().find(R"("isError":true)") != std::string_view::npos
                       : rejected.status().value() == 400);
            assert(
                f.shell.stateVersion() == version && f.shell.sceneTransformSettings().same(value));
        }
        const auto rejected =
            remote.call("scene.transform.settings.get", R"("idempotencyKey":"read")", mcp);
        assert(mcp ? rejected.body_text().find(R"("isError":true)") != std::string_view::npos
                   : rejected.status().value() == 400);
    }
    assert(remote.call("scene.selection.focus", fields).status().value() == 400);
    assert(remote
               .call("scene.transform.settings.get",
                   R"("expectedStateVersion":0,"expectedStateVersion":1)")
               .status()
               .value()
        == 400);
    const auto state = remote.call("editor.state.get");
    assert(state.body_text().find("sceneTransformSettings") != std::string_view::npos);
    const auto catalog = remote.request(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})", true);
    assert(catalog.body_text().find("scene.transform.settings.get") != std::string_view::npos
        && catalog.body_text().find("rotateStepRadians") != std::string_view::npos);
    std::puts(
        "PASS REST/MCP exact double roundtrip, strict seven fields/enums/types/duplicates, stale version and per-field idempotency conflicts");
}

void lifecycle(const tests::RecoveryFixture& files)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Fixture f(files);
            assert(f.shell.submitCommand(SetSceneTransformSettingsCommand{changed()}).status
                == Status::Completed);
            ProtocolFixture remote(f.shell);
            assert(remote.call("scene.transform.settings.get").status().value() == 200);
            assert(f.shell.submitCommand(SetSceneTransformSettingsCommand{}).status
                == Status::Completed);
            assert(f.shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary settings/query/draw/destruction cycles core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle(files);
    else {
        valuesAndIsolation(files);
        invalidAndPreparationFailure(files);
        busyAndLegacy(files);
        protocol(files);
    }
}
