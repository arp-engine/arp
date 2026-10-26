#include "2d/physics_components.h"
#include "2d/tile_map_resource_component.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/text/number_format.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/validation/editor_control_input.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_shell.h"
#include "scene/runtime_components.h"
#include "tests/fixtures/tile_map_fixture.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;

class HistoryFixture final {
public:
    HistoryFixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("object-history-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
    }

    ~HistoryFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        assert(!error);
    }

    void initialize(EditorShell& shell) const
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
                .ok());
        shell.activate("workspace_3D");
        completed(shell, NewSceneCommand{true});
        completed(shell, CreateSceneObjectCommand("Original"));
    }

    static EditorResponse completed(EditorShell& shell, EditorCommandPayload command)
    {
        const auto result = shell.submitCommand(std::move(command));
        assert(result.status == EditorResponseStatus::Completed);
        return result;
    }

    static void save(EditorShell& shell, std::string_view path)
    {
        const auto accepted = completed(shell, SaveSceneCommand(path));
        finishIo(shell, accepted);
    }

    static void finishIo(EditorShell& shell, const EditorResponse& accepted)
    {
        assert(accepted.hasSnapshot);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            shell.pollTasks();
            const auto task = completed(
                shell, GetScenePersistenceTaskQuery{accepted.snapshot.scenePersistenceTaskId});
            if (task.snapshot.scenePersistenceTaskStatus != ScenePersistenceTaskStatus::Pending) {
                assert(task.snapshot.scenePersistenceTaskStatus
                    == ScenePersistenceTaskStatus::Succeeded);
                return;
            }
            std::this_thread::yield();
        }
        assert(false && "Scene save timed out");
    }

    std::filesystem::path root;
};

void contract(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto objectId = shell.sceneDocument().selectedObject();
    auto* object = shell.sceneDocument().scene().findObject(objectId);
    const auto* sceneOwner = &shell.sceneDocument().scene();
    const auto uuid = shell.sceneDocument().runtimeSceneId();
    HistoryFixture::save(shell, "baseline.arp.yaml");
    const auto saved = core::readTextFile(fixture.root / "baseline.arp.yaml").text;
    assert(!shell.sceneDirty());
    HistoryFixture::completed(shell, SetSceneObjectNameCommand("Renamed"));
    assert(shell.sceneDirty());
    HistoryFixture::completed(shell, SetSceneObjectTranslationCommand{2, 3, 4});
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->name() == "Renamed" && object->transform().localTransform().translation.x == 0);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->name() == "Original" && !shell.sceneDirty());
    HistoryFixture::completed(shell, RedoSceneCommand{});
    const auto beforeNoop = shell.stateVersion();
    HistoryFixture::completed(shell, SetSceneObjectNameCommand("Renamed"));
    HistoryFixture::completed(shell, SetSceneObjectTranslationCommand{});
    SetSceneObjectTransformCommand unchanged;
    unchanged.objectId = objectId.value();
    HistoryFixture::completed(shell, unchanged);
    assert(shell.stateVersion() == beforeNoop);
    // Invalid edits and no-ops must preserve the pending translation Redo.
    for (const auto name : {std::string_view{},
             std::string_view{"bad\nname"},
             std::string_view{"bad\rname"},
             std::string_view{"bad\0name", 8},
             std::string_view{"\xC0\xAF", 2}}) {
        assert(shell.submitCommand(SetSceneObjectNameCommand(name)).status
            == EditorResponseStatus::Failed);
        assert(shell.stateVersion() == beforeNoop && object->name() == "Renamed");
    }
    assert(shell
               .submitCommand(
                   SetSceneObjectTranslationCommand{std::numeric_limits<float>::infinity(), 0, 0})
               .status
        == EditorResponseStatus::Failed);
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(object->transform().localTransform().translation.x == 2);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(!shell.sceneDirty());

    HistoryFixture::completed(shell, CreateSceneObjectCommand("Other"));
    const auto selection = shell.sceneDocument().selectedObject();
    SetSceneObjectNameCommand targeted("Exact target");
    targeted.objectId = objectId.value();
    HistoryFixture::completed(shell, targeted);
    assert(shell.sceneDocument().selectedObject() == selection && object->name() == "Exact target");
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->name() == "Original" && shell.sceneDocument().selectedObject() == selection);
    HistoryFixture::completed(shell, RedoSceneCommand{});
    // A stale target state fails without consuming its history entry.
    object->setName("External change");
    const auto staleVersion = shell.stateVersion();
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Failed);
    assert(object->name() == "External change" && shell.stateVersion() == staleVersion);
    object->setName("Exact target");
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->name() == "Original");
    assert(core::readTextFile(fixture.root / "baseline.arp.yaml").text == saved);
    assert(&shell.sceneDocument().scene() == sceneOwner
        && shell.sceneDocument().runtimeSceneId() == uuid);
    assert(shell.sceneDocument().scene().findObject(objectId) == object);

    const auto rootId = shell.sceneDocument().scene().rootId();
    const auto beforeRootEdit = shell.stateVersion();
    const auto rootEdit = shell.submitCommand(SetSceneObjectActiveCommand{rootId.value(), false});
    assert(rootEdit.status == EditorResponseStatus::Failed
        && rootEdit.diagnostic == EditorDiagnosticCode::SceneRootActiveUnsupported);
    assert(shell.stateVersion() == beforeRootEdit
        && shell.sceneDocument().scene().findObject(rootId)->active());
    HistoryFixture::completed(shell, SelectSceneObjectCommand{rootId.value()});
    assert(!shell.runtime().canvas().findWidget(shell.id("scene_active"))->desc().enabled);
    HistoryFixture::completed(shell, SelectSceneObjectCommand{objectId.value()});
    assert(shell.runtime().canvas().findWidget(shell.id("scene_active"))->desc().enabled);

    HistoryFixture::completed(shell, SetSceneObjectActiveCommand{objectId.value(), false});
    assert(!object->active());
    const auto disabledVersion = shell.stateVersion();
    HistoryFixture::completed(shell, SetSceneObjectActiveCommand{objectId.value(), false});
    assert(shell.stateVersion() == disabledVersion);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->active());
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(!object->active());
    HistoryFixture::completed(shell, SelectSceneObjectCommand{objectId.value()});
    assert(shell.runtime().canvas().findWidget(shell.id("scene_active"))->desc().text
        == "Active: Off");
    shell.activate("scene_active");
    assert(object->active());
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(!object->active());
    HistoryFixture::completed(shell, BeginSceneTransformEditCommand{objectId.value()});
    HistoryFixture::completed(shell, SetSceneObjectTranslationCommand{1, 0, 0});
    HistoryFixture::completed(shell, SetSceneObjectTranslationCommand{2, 0, 0});
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Failed);
    HistoryFixture::completed(shell, EndSceneTransformEditCommand{objectId.value()});
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->transform().localTransform().translation.x == 0);
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(object->transform().localTransform().translation.x == 2);

    // UI and direct commands consume the same explicit 128-byte name budget.
    HistoryFixture::completed(shell, SelectSceneObjectCommand{objectId.value()});
    const core::pmr::String boundary(128, 'N');
    HistoryFixture::completed(shell, SetSceneObjectNameCommand(boundary));
    assert(shell.runtime().canvas().findWidget(shell.id("scene_name"))->desc().text == boundary);
    assert(shell.submitCommand(SetSceneObjectNameCommand(core::pmr::String(129, 'N'))).status
        == EditorResponseStatus::Failed);
    assert(shell.drawCommands().ok());
    HistoryFixture::save(shell, "renamed.arp.yaml");
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(shell.sceneDirty());
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(!shell.sceneDirty());
    std::puts(
        "PASS exact-target rename/translation Undo, no-op and failure Redo preservation, stale state rejection, save point, 128-byte name and stable owners");
}

void persistence(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto objectId = shell.sceneDocument().selectedObject();
    HistoryFixture::completed(shell, SetSceneObjectNameCommand("Persisted child"));
    HistoryFixture::completed(shell, SetSceneObjectActiveCommand{objectId.value(), false});
    HistoryFixture::save(shell, "inactive.arp.yaml");
    const auto saved = core::readTextFile(fixture.root / "inactive.arp.yaml").text;
    assert(saved.find("active: false") != std::string_view::npos);
    HistoryFixture::completed(shell, SetSceneObjectActiveCommand{objectId.value(), true});
    LoadSceneCommand load("inactive.arp.yaml");
    load.discardChanges = true;
    HistoryFixture::finishIo(shell, HistoryFixture::completed(shell, load));
    const auto* restored =
        shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    assert(restored && restored->name() == "Persisted child" && !restored->active());
    assert(!shell.sceneDirty());
    HistoryFixture::save(shell, "inactive-copy.arp.yaml");
    assert(core::readTextFile(fixture.root / "inactive-copy.arp.yaml").text == saved);
    // A fresh shell proves file recovery independently of the old live objects.
    EditorShell reopened;
    assert(reopened
            .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                fixture.root)
            .ok());
    reopened.activate("workspace_3D");
    HistoryFixture::finishIo(reopened, HistoryFixture::completed(reopened, load));
    const auto* recovered =
        reopened.sceneDocument().scene().findObject(reopened.sceneDocument().selectedObject());
    assert(recovered && recovered->name() == "Persisted child" && !recovered->active());
    std::puts(
        "PASS child name/active save, load, exact-byte round-trip and fresh-shell recovery; Scene root active rejected");
}

void remote(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto objectId = shell.sceneDocument().selectedObject();
    auto* object = shell.sceneDocument().scene().findObject(objectId);
    EditorRemoteProtocol protocol("object-history-token");
    const auto request = [&](std::string_view json, bool mcp = false) {
        network::http::Request input;
        input.set_method(network::http::Method::Post);
        input.set_target(mcp ? "/mcp" : "/api/v1/commands");
        input.add_header("Authorization", "Bearer object-history-token");
        input.add_header("Accept", "application/json");
        input.add_header("MCP-Protocol-Version", "2025-11-25");
        input.set_content_type(network::http::ContentType::Json);
        input.set_body_text(json);
        return protocol.handle(input, shell);
    };
    core::pmr::String prefix(R"({"schemaVersion":1,"command":"scene.rename","objectId":)");
    core::appendUnsigned(prefix, objectId.value());
    const auto body = prefix + R"(,"name":"Remote name","idempotencyKey":"rename-1"})";
    assert(request(body).status().value() == 200);
    assert(object->name() == "Remote name");
    const auto version = shell.stateVersion();
    assert(request(body).status().value() == 200 && shell.stateVersion() == version);
    assert(request(prefix + R"(,"name":"Different name","idempotencyKey":"rename-1"})")
               .body_text()
               .find("idempotency_key_conflict")
        != std::string_view::npos);
    assert(object->name() == "Remote name");
    for (const auto invalid : {prefix + R"(,"name":""})",
             prefix + R"(,"name":"bad\nname"})",
             prefix + R"(,"name":"valid","unknown":1})",
             prefix + R"(,"name":"valid","active":false})",
             core::pmr::String(
                 R"({"schemaVersion":1,"command":"scene.rename","objectId":0,"name":"valid"})")})
        assert(request(invalid).status().value() != 200 && object->name() == "Remote name");
    core::pmr::String mcp(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.active.set","arguments":{"objectId":)");
    core::appendUnsigned(mcp, objectId.value());
    mcp += R"(,"active":false}}})";
    assert(request(mcp, true).body_text().find(R"("isError":false)") != std::string_view::npos);
    assert(!object->active());
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->active());
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->name() == "Original");
    const auto rootId = shell.sceneDocument().scene().rootId().value();
    core::pmr::String select(R"({"schemaVersion":1,"command":"scene.select","objectId":)");
    core::appendUnsigned(select, rootId);
    assert(request(select + "}").status().value() == 200);
    assert(shell.sceneDocument().selectedObject().value() == rootId);
    assert(request(select + R"(,"active":false})").status().value() != 200);
    assert(request(select + R"(,"unknown":true})").status().value() != 200);
    core::pmr::String rootActive(R"({"schemaVersion":1,"command":"scene.active.set","objectId":)");
    core::appendUnsigned(rootActive, rootId);
    assert(request(rootActive + R"(,"active":false})")
               .body_text()
               .find("scene_root_active_unsupported")
        != std::string_view::npos);
    core::pmr::String selectMcp(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"scene.select","arguments":{"objectId":)");
    core::appendUnsigned(selectMcp, objectId.value());
    assert(request(selectMcp + "}}}", true).body_text().find(R"("isError":false)")
        != std::string_view::npos);
    assert(shell.sceneDocument().selectedObject() == objectId);
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(object->name() == "Remote name");
    std::puts("PASS REST/MCP rename and active history, strict fields and idempotency");
}

void lifetime(HistoryFixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            HistoryFixture::completed(shell, SetSceneObjectNameCommand("Ordinary edit"));
            shell.activate("scene_active");
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(shell, SetSceneObjectTranslationCommand{1, 2, 3});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(shell, RedoSceneCommand{});
            assert(shell.drawCommands().ok());
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary object-history create/edit/Undo/Redo/destroy cycles; core live bytes/count delta=0");
}

// Fresh shells exercise first-entry allocation, without relying on retained
// vector capacity. The Redo branch is checked separately after each rejection.
bool safetyProbe(HistoryFixture& fixture)
{
    bool passed = true;
    unsigned failures = 0;
    unsigned commits = 0;
    for (unsigned operation = 0; operation < 15; ++operation) {
        for (const auto budget :
            std::array<std::size_t, 9>{1, 64, 128, 256, 512, 1024, 2048, 8192, 131072}) {
            EditorShell shell;
            fixture.initialize(shell);
            const bool root = operation == 3 || operation == 9 || operation == 10;
            const bool undo = operation == 4 || operation == 7 || operation == 9 || operation == 13;
            const bool redo =
                operation == 5 || operation == 8 || operation == 10 || operation == 14;
            const bool active =
                operation == 1 || operation == 7 || operation == 8 || operation == 11;
            const bool transform =
                operation == 2 || operation == 12 || operation == 13 || operation == 14;
            const bool branch = operation == 6 || operation == 11 || operation == 12;
            const auto id = root ? shell.sceneDocument().scene().rootId()
                                 : shell.sceneDocument().selectedObject();
            auto* object = shell.sceneDocument().scene().findObject(id);
            if (operation >= 4) {
                if (active && !branch)
                    HistoryFixture::completed(
                        shell, SetSceneObjectActiveCommand{id.value(), false});
                else if (transform && !branch)
                    HistoryFixture::completed(shell, SetSceneObjectTranslationCommand{7, 8, 9});
                else {
                    SetSceneObjectNameCommand seed(core::pmr::String(112, 'S'));
                    seed.objectId = id.value();
                    HistoryFixture::completed(shell, seed);
                }
                if (redo)
                    HistoryFixture::completed(shell, UndoSceneCommand{});
            }
            const auto beforeName = object->name();
            const auto selection = shell.sceneDocument().selectedObject();
            const auto owner = &shell.sceneDocument().scene();
            EditorCommandEnvelope command{GetEditorStateQuery{}};
            if (undo)
                command.payload = UndoSceneCommand{};
            else if (redo)
                command.payload = RedoSceneCommand{};
            else if (!active && !transform) {
                SetSceneObjectNameCommand name(core::pmr::String(128, 'N'));
                name.objectId = id.value();
                command.payload = std::move(name);
            } else if (active)
                command.payload = SetSceneObjectActiveCommand{id.value(), false};
            else {
                SetSceneObjectTransformCommand transform;
                transform.objectId = id.value();
                transform.translation = {3, 4, 5};
                transform.rotation = core::Quat::fromAxisAngle({0, 1, 0}, 0.5f);
                transform.scale = {2, 3, 4};
                command.payload = transform;
            }
            if (branch) {
                HistoryFixture::completed(shell, UndoSceneCommand{});
            }
            const auto startVersion = shell.stateVersion();
            core::pmr::String startYaml, diagnostic;
            assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), startYaml, diagnostic));
            const auto startLabel =
                HistoryFixture::completed(shell, GetEditorStateQuery{}).snapshot.scene;
            const auto retry = command.payload;
            const auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            bool threw = false;
            EditorResponse response;
            {
                core::CpuMemoryQuotaScope scope(quota);
                try {
                    response = shell.submitCommand(std::move(command));
                } catch (const std::bad_alloc&) {
                    threw = true;
                }
            }
            const bool completed = !threw && response.status == EditorResponseStatus::Completed;
            core::pmr::String afterYaml;
            assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), afterYaml, diagnostic));
            const bool unchanged = afterYaml == startYaml;
            const auto receiptVersion = shell.stateVersion();
            bool coherent = !threw && &shell.sceneDocument().scene() == owner
                && shell.sceneDocument().selectedObject() == selection;
            const auto expectedLabel = shell.sceneDocument().scene().name();
            coherent &= HistoryFixture::completed(shell, GetEditorStateQuery{}).snapshot.scene
                == expectedLabel;
            if (completed) {
                ++commits;
                coherent &= shell.stateVersion() == startVersion + 1;
                HistoryFixture::completed(shell,
                    undo ? EditorCommandPayload(RedoSceneCommand{})
                         : EditorCommandPayload(UndoSceneCommand{}));
                core::pmr::String restored;
                assert(
                    EditorSceneAuthoringCodec::encode(shell.sceneDocument(), restored, diagnostic));
                coherent &= restored == startYaml;
            } else {
                ++failures;
                coherent &= unchanged && shell.stateVersion() == startVersion
                    && response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed;
                coherent &= HistoryFixture::completed(shell, GetEditorStateQuery{}).snapshot.scene
                    == startLabel;
                if (branch) {
                    HistoryFixture::completed(shell, RedoSceneCommand{});
                    coherent &= object->name() == beforeName;
                } else {
                    // Rejected preparation must leave the exact history entry
                    // available; retry and inverse restore all serialized TRS.
                    HistoryFixture::completed(shell, retry);
                    coherent &= shell.stateVersion() == startVersion + 1;
                    HistoryFixture::completed(shell,
                        undo ? EditorCommandPayload(RedoSceneCommand{})
                             : EditorCommandPayload(UndoSceneCommand{}));
                    core::pmr::String restored;
                    assert(EditorSceneAuthoringCodec::encode(
                        shell.sceneDocument(), restored, diagnostic));
                    coherent &= restored == startYaml;
                }
            }
            std::printf(
                "object safety op=%u budget=%zu threw=%u committed=%u unchanged=%u versionDelta=%llu coherent=%u\n",
                operation,
                budget,
                threw,
                completed,
                unchanged,
                static_cast<unsigned long long>(receiptVersion - startVersion),
                coherent);
            passed &= coherent;
        }
    }
    std::printf("object safety failures=%u commits=%u passed=%u\n", failures, commits, passed);
    return passed && failures != 0 && commits != 0;
}

void safetyLifetime(HistoryFixture& fixture)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            const auto id = shell.sceneDocument().selectedObject().value();
            EditorCommandEnvelope command{SetSceneObjectActiveCommand{id, false}};
            const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            const auto version = shell.stateVersion();
            {
                core::CpuMemoryQuotaScope scope(quota);
                const auto rejected = shell.submitCommand(std::move(command));
                assert(rejected.status == EditorResponseStatus::Failed
                    && rejected.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
            }
            assert(shell.stateVersion() == version);
            HistoryFixture::completed(shell, SetSceneObjectActiveCommand{id, false});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(shell, RedoSceneCommand{});
            HistoryFixture::completed(shell, SetSceneObjectTranslationCommand{1, 2, 3});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(
                shell, SetSceneObjectNameCommand(core::pmr::String(128, 'N')));
            HistoryFixture::completed(shell, UndoSceneCommand{});
            assert(shell.drawCommands().ok());
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (cycle == 0)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary object preparation failure/retry/Undo/Redo/destroy cycles; core live bytes/count delta=0");
}

// Keep the native setup outside the quota. Each command owns its envelope before
// injection so allocation failures exercise admission/history/Scene preparation.
class ReparentFixture final {
public:
    explicit ReparentFixture(HistoryFixture& fixture)
    {
        fixture.initialize(shell);
        child = shell.sceneDocument().selectedObject().value();
        auto& world = shell.sceneDocument().scene();
        old = world.createObject("Old parent").object_id();
        target = world.createObject("Target parent").object_id();
        before = world.createObject("Before sibling").object_id();
        after = world.createObject("After sibling").object_id();
        for (const auto id : {before, child, after})
            assert(world
                    .reparent(
                        scene::GameObjectId::fromValue(id), scene::GameObjectId::fromValue(old))
                    .ok());
        auto transform = scene::Transform::identity();
        transform.translation = {3, -2, 5};
        transform.rotation = core::Quat::fromAxisAngle({0, 1, 0}, 0.7f);
        transform.scale = {2, 3, 4};
        world.findObject(scene::GameObjectId::fromValue(child))->set_local_transform(transform);
        HistoryFixture::completed(shell, SelectSceneObjectCommand{target});
    }

    core::pmr::String yaml() const
    {
        core::pmr::String encoded, diagnostic;
        assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), encoded, diagnostic));
        return encoded;
    }

    EditorShell shell;
    std::uint64_t child = 0, old = 0, target = 0, before = 0, after = 0;
};

void reparentUndoGrowth(HistoryFixture& fixture)
{
    std::uint64_t historyBudget = 0;
    {
        ReparentFixture state(fixture);
        HistoryFixture::completed(
            state.shell, ReparentSceneObjectCommand{state.child, state.target, 0});
        auto measured = core::makeShared<core::CpuMemoryQuota>();
        EditorCommandEnvelope undo{UndoSceneCommand{}};
        {
            core::CpuMemoryQuotaScope scope(measured);
            const auto response = state.shell.submitCommand(std::move(undo));
            assert(response.status == EditorResponseStatus::Completed);
        }
        historyBudget = measured->peakBytes();
        assert(historyBudget);
    }
    ReparentFixture state(fixture);
    auto& shell = state.shell;
    HistoryFixture::completed(shell, ReparentSceneObjectCommand{state.child, state.target, 0});
    auto& world = shell.sceneDocument().scene();
    auto* parent = world.findObject(scene::GameObjectId::fromValue(state.old));
    const auto spare = parent->childIds().capacity() - parent->childIds().size();
    for (std::size_t index = 0; index < spare; ++index) {
        auto& sibling = world.createObject("Native sibling");
        assert(world.reparent(sibling.id(), parent->id()).ok());
    }
    const auto before = state.yaml();
    const auto version = shell.stateVersion();
    EditorCommandEnvelope undo{UndoSceneCommand{}};
    auto quota = core::makeShared<core::CpuMemoryQuota>(historyBudget);
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto response = shell.submitCommand(std::move(undo));
        assert(response.status == EditorResponseStatus::Failed
            && response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
    }
    // The measured history preparation fits; it is the additional target growth
    // that must reject. The original Undo remains available for an exact retry.
    assert(quota->peakBytes() == historyBudget);
    assert(quota->liveBytes() == 0);
    assert(state.yaml() == before && shell.stateVersion() == version);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(shell.stateVersion() == version + 1);
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(state.yaml() == before);
    std::printf(
        "PASS Undo target growth rejects after prepared history, budget=%llu; retry/Redo restores exact YAML\n",
        static_cast<unsigned long long>(historyBudget));
}

bool reparentSafety(HistoryFixture& fixture)
{
    reparentUndoGrowth(fixture);
    unsigned failures = 0, commits = 0, noops = 0;
    for (unsigned operation = 0; operation < 7; ++operation) {
        for (const auto budget :
            std::array<std::size_t, 9>{1, 64, 128, 256, 512, 1024, 2048, 8192, 131072}) {
            ReparentFixture state(fixture);
            auto& shell = state.shell;
            const bool undo = operation == 2, redo = operation == 3;
            const bool branch = operation >= 4;
            if (undo || redo) {
                HistoryFixture::completed(
                    shell, ReparentSceneObjectCommand{state.child, state.target, 0});
                if (redo)
                    HistoryFixture::completed(shell, UndoSceneCommand{});
            }
            if (branch) {
                SetSceneObjectNameCommand seed("Pending Redo");
                seed.objectId = state.child;
                HistoryFixture::completed(shell, seed);
                HistoryFixture::completed(shell, UndoSceneCommand{});
            }
            EditorCommandEnvelope command{GetEditorStateQuery{}};
            if (undo)
                command.payload = UndoSceneCommand{};
            else if (redo)
                command.payload = RedoSceneCommand{};
            else
                command.payload = ReparentSceneObjectCommand{state.child,
                    operation == 1 || operation == 5 ? state.old : state.target,
                    operation == 1       ? 2u
                        : operation == 5 ? 1u
                        : operation == 6 ? 99u
                                         : 0u};
            const auto retry = command.payload;
            const auto start = state.yaml();
            const auto version = shell.stateVersion();
            const auto selection = shell.sceneDocument().selectedObject();
            const auto* owner = &shell.sceneDocument().scene();
            const auto sceneId = shell.sceneDocument().runtimeSceneId();
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            EditorResponse response;
            {
                core::CpuMemoryQuotaScope scope(quota);
                response = shell.submitCommand(std::move(command));
            }
            const bool accepted = response.status == EditorResponseStatus::Completed;
            const bool changed = state.yaml() != start;
            assert(&shell.sceneDocument().scene() == owner);
            assert(shell.sceneDocument().runtimeSceneId() == sceneId);
            assert(shell.sceneDocument().selectedObject() == selection);
            assert(shell.stateVersion() == version + (changed ? 1u : 0u));
            if (!accepted) {
                ++failures;
                assert(!changed);
                assert(response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed
                    || (operation == 6
                        && response.diagnostic == EditorDiagnosticCode::InvalidCommandPayload));
                if (!branch) {
                    HistoryFixture::completed(shell, retry);
                    assert(shell.stateVersion() == version + 1);
                    HistoryFixture::completed(shell,
                        undo ? EditorCommandPayload(RedoSceneCommand{})
                             : EditorCommandPayload(UndoSceneCommand{}));
                    assert(state.yaml() == start);
                }
            } else if (changed) {
                ++commits;
                assert(operation != 5 && operation != 6);
                HistoryFixture::completed(shell,
                    undo ? EditorCommandPayload(RedoSceneCommand{})
                         : EditorCommandPayload(UndoSceneCommand{}));
                assert(state.yaml() == start);
                if (operation == 4) {
                    // A real new branch replaces, rather than revives, name Redo.
                    HistoryFixture::completed(shell, RedoSceneCommand{});
                    assert(shell.sceneDocument()
                               .scene()
                               .findObject(scene::GameObjectId::fromValue(state.child))
                               ->name()
                        == "Original");
                }
            } else {
                ++noops;
                assert(operation == 5);
            }
            if (branch && !changed) {
                HistoryFixture::completed(shell, RedoSceneCommand{});
                assert(shell.sceneDocument()
                           .scene()
                           .findObject(scene::GameObjectId::fromValue(state.child))
                           ->name()
                    == "Pending Redo");
                HistoryFixture::completed(shell, UndoSceneCommand{});
                assert(state.yaml() == start);
            }
            std::printf("reparent safety op=%u quota=%zu accepted=%u changed=%u coherent=1\n",
                operation,
                budget,
                accepted,
                changed);
        }
    }
    std::printf(
        "PASS reparent 63 quota cases failures=%u commits=%u noops=%u\n", failures, commits, noops);
    return failures && commits && noops;
}

void reparentLifetime(HistoryFixture& fixture)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            ReparentFixture state(fixture);
            auto& shell = state.shell;
            const auto before = state.yaml();
            const auto version = shell.stateVersion();
            EditorCommandEnvelope command{ReparentSceneObjectCommand{state.child, state.target, 0}};
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            {
                core::CpuMemoryQuotaScope scope(quota);
                const auto response = shell.submitCommand(std::move(command));
                assert(response.status == EditorResponseStatus::Failed
                    && response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
            }
            assert(state.yaml() == before && shell.stateVersion() == version);
            HistoryFixture::completed(
                shell, ReparentSceneObjectCommand{state.child, state.target, 0});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            assert(state.yaml() == before);
            HistoryFixture::completed(shell, RedoSceneCommand{});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(shell, ReparentSceneObjectCommand{state.child, state.old, 2});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            assert(state.yaml() == before && shell.drawCommands().ok());
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary reparent failure/retry/Undo/Redo/draw/destroy cycles; core live bytes/count delta=0");
}

core::pmr::String presenceYaml(const EditorShell& shell)
{
    core::pmr::String yaml, diagnostic;
    const auto encoded = EditorSceneAuthoringCodec::encode(shell.sceneDocument(), yaml, diagnostic);
    if (!encoded)
        std::fprintf(stderr, "presence YAML rejected: %s\n", diagnostic.c_str());
    assert(encoded);
    return yaml;
}

void identityComponentHistory(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto id = shell.sceneDocument().selectedObject();
    auto* object = shell.sceneDocument().scene().findObject(id);
    const auto* owner = &shell.sceneDocument().scene();
    const auto uuid = shell.sceneDocument().runtimeSceneId();
    assert(shell.sceneDocument().addComponent("LightComponent", id));
    HistoryFixture::completed(shell, AddSceneComponentCommand("MeshComponent"));
    auto* component = object->components().back().get();
    const auto cid = component->id();
    auto old = component->nativeReflectionObject();
    assert(shell.sceneDocument().addComponent("SkyComponent", id));
    const auto originalOrder = object->componentIds();
    HistoryFixture::save(shell, "identity-component-baseline.arp.yaml");
    const auto initial = presenceYaml(shell);
    SetSceneComponentPropertyCommand color("baseColor");
    color.objectId = id.value();
    color.componentId = cid.value();
    color.valueKind = ScenePropertyValueKind::Color4;
    color.color4Value = {0.2f, 0.4f, 0.6f, 1};
    HistoryFixture::completed(shell, color);
    const auto changed = presenceYaml(shell);
    RemoveSceneComponentCommand remove;
    remove.objectId = id.value();
    remove.componentId = cid.value();
    HistoryFixture::completed(shell, remove);
    assert(!object->findComponent(cid) && !old.native_reference_alive());
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->findComponent(cid) && object->componentIds() == originalOrder);
    assert(!old.native_reference_alive() && presenceYaml(shell) == changed);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(presenceYaml(shell) == initial && !shell.sceneDirty());
    // The older Add still identifies the same component after an intervening Remove.
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(!object->findComponent(cid));
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(object->findComponent(cid) && object->componentIds() == originalOrder);
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(presenceYaml(shell) == changed);
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(!object->findComponent(cid));
    assert(
        &shell.sceneDocument().scene() == owner && shell.sceneDocument().runtimeSceneId() == uuid);
    const auto version = shell.stateVersion();
    const auto rejected = shell.submitCommand(AddSceneComponentCommand("RigidBody2DComponent"));
    assert(rejected.status == EditorResponseStatus::Failed
        && rejected.diagnostic == EditorDiagnosticCode::SceneHistoryRestoreRejected);
    assert(shell.stateVersion() == version);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(object->findComponent(cid));
    std::puts(
        "PASS mixed component Add/property/Remove Undo/Redo: exact ID/order, old handle dead and savepoint restored");
}

void identitySubtreeHistory(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    auto& world = shell.sceneDocument().scene();
    const auto parent = shell.sceneDocument().selectedObject();
    auto& child = world.createObject("Child");
    const auto childId = child.id();
    auto& sibling = world.createObject("Sibling");
    const auto siblingId = sibling.id();
    auto& grandchild = world.createObject("Grandchild");
    const auto grandchildId = grandchild.id();
    auto& outside = world.createObject("Outside");
    const auto outsideId = outside.id();
    assert(world.reparent(childId, parent, 0).ok());
    assert(world.reparent(siblingId, parent, 1).ok());
    assert(world.reparent(grandchildId, childId, 0).ok());
    auto pose = scene::Transform::identity();
    pose.translation = {2, 3, 4};
    pose.rotation = core::Quat::fromAxisAngle({0, 1, 0}, 0.3f);
    pose.scale = {2, 3, 4};
    child.set_local_transform(pose);
    auto* internal = dynamic_cast<scene::ObjectLinksComponent*>(
        shell.sceneDocument().addComponent("ObjectLinksComponent", childId));
    auto* external = dynamic_cast<scene::ObjectLinksComponent*>(
        shell.sceneDocument().addComponent("ObjectLinksComponent", outsideId));
    assert(internal && external);
    internal->links.push_back({"descendant", grandchildId});
    internal->links.push_back({"outside", outsideId});
    external->links.push_back({"keep", outsideId});
    external->links.push_back({"removed", grandchildId});
    external->links.push_back({"also removed", parent});
    const auto externalId = external->id(), internalId = internal->id();
    const auto transformId = child.transform().id();
    const auto oldObject = child.nativeReflectionObject(),
               oldTransform = child.transform().nativeReflectionObject(),
               oldComponent = internal->nativeReflectionObject();
    const auto owner = &world;
    const auto uuid = shell.sceneDocument().runtimeSceneId();
    const auto original = presenceYaml(shell);
    HistoryFixture::completed(shell, DestroySceneObjectCommand{});
    assert(!world.findObject(parent) && !world.findObject(grandchildId));
    assert(external->links.size() == 1 && external->links[0].name == "keep");
    const auto deleted = presenceYaml(shell);
    assert(!oldObject.native_reference_alive() && !oldTransform.native_reference_alive()
        && !oldComponent.native_reference_alive());
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(world.findObject(childId)->transform().id() == transformId);
    assert(world.findObject(childId)->findComponent(internalId));
    assert(outside.findComponent(externalId) == external && external->links.size() == 3);
    assert(world.findObject(parent)->childIds()[0] == childId
        && world.findObject(parent)->childIds()[1] == siblingId);
    assert(presenceYaml(shell) == original);
    assert(!oldObject.native_reference_alive() && !oldComponent.native_reference_alive());
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(presenceYaml(shell) == deleted);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(presenceYaml(shell) == original);
    assert(&world == owner && shell.sceneDocument().runtimeSceneId() == uuid);
    // Older object Create history rejects independent native descendants instead
    // of deleting content it never captured. This rejection preserves the branch.
    const auto version = shell.stateVersion();
    const auto staleCreate = shell.submitCommand(UndoSceneCommand{});
    assert(staleCreate.status == EditorResponseStatus::Failed && shell.stateVersion() == version);
    assert(presenceYaml(shell) == original);
    std::puts(
        "PASS complete subtree exact IDs/order/TRS/internal ObjectLinks and atomic external link removal/restoration");
}

void identityFreshCreate(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto beforeSelection = shell.sceneDocument().selectedObject();
    const auto before = presenceYaml(shell);
    HistoryFixture::completed(shell, CreateSceneObjectCommand("New exact object"));
    const auto id = shell.sceneDocument().selectedObject();
    auto* object = shell.sceneDocument().scene().findObject(id);
    const auto tid = object->transform().id();
    const auto old = object->nativeReflectionObject();
    const auto after = presenceYaml(shell);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(!shell.sceneDocument().scene().findObject(id)
        && shell.sceneDocument().selectedObject() == beforeSelection);
    assert(presenceYaml(shell) == before && !old.native_reference_alive());
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(shell.sceneDocument().scene().findObject(id)->transform().id() == tid
        && presenceYaml(shell) == after);
    std::puts(
        "PASS object Create Undo/Redo restores selection and exact object/Transform identities");
}

void identityQuota(HistoryFixture& fixture)
{
    unsigned failures = 0, commits = 0;
    for (unsigned operation = 0; operation < 8; ++operation) {
        for (const auto budget :
            std::array<std::size_t, 8>{1, 128, 512, 2048, 8192, 32768, 131072, 1048576}) {
            EditorShell shell;
            fixture.initialize(shell);
            const auto id = shell.sceneDocument().selectedObject();
            if (operation != 0 && operation != 4)
                HistoryFixture::completed(shell, AddSceneComponentCommand("MeshComponent"));
            if (operation == 2 || operation == 3)
                HistoryFixture::completed(shell, RemoveSceneComponentCommand{});
            if (operation == 3)
                HistoryFixture::completed(shell, UndoSceneCommand{});
            if (operation == 4)
                HistoryFixture::completed(shell, CreateSceneObjectCommand("created"));
            if (operation == 6 || operation == 7)
                HistoryFixture::completed(shell, DestroySceneObjectCommand{});
            if (operation == 7)
                HistoryFixture::completed(shell, UndoSceneCommand{});
            EditorCommandEnvelope command{GetEditorStateQuery{}};
            if (operation == 0)
                command.payload = AddSceneComponentCommand("MeshComponent");
            if (operation == 1)
                command.payload = RemoveSceneComponentCommand{};
            if (operation == 2 || operation == 4 || operation == 6)
                command.payload = UndoSceneCommand{};
            if (operation == 3 || operation == 7)
                command.payload = RedoSceneCommand{};
            if (operation == 5)
                command.payload = DestroySceneObjectCommand{};
            const auto before = presenceYaml(shell);
            const auto version = shell.stateVersion();
            const auto selection = shell.sceneDocument().selectedObject();
            const auto owner = &shell.sceneDocument().scene();
            const auto uuid = shell.sceneDocument().runtimeSceneId();
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
            EditorResponse response;
            {
                core::CpuMemoryQuotaScope scope(quota);
                response = shell.submitCommand(std::move(command));
            }
            if (response.status == EditorResponseStatus::Failed) {
                ++failures;
                assert(response.diagnostic == EditorDiagnosticCode::CommandPreparationFailed);
                assert(shell.stateVersion() == version
                    && shell.sceneDocument().selectedObject() == selection);
                assert(presenceYaml(shell) == before);
                if (operation == 0)
                    HistoryFixture::completed(shell, AddSceneComponentCommand("MeshComponent"));
                if (operation == 1)
                    HistoryFixture::completed(shell, RemoveSceneComponentCommand{});
                if (operation == 2 || operation == 4 || operation == 6)
                    HistoryFixture::completed(shell, UndoSceneCommand{});
                if (operation == 3 || operation == 7)
                    HistoryFixture::completed(shell, RedoSceneCommand{});
                if (operation == 5)
                    HistoryFixture::completed(shell, DestroySceneObjectCommand{});
            } else {
                ++commits;
                assert(response.status == EditorResponseStatus::Completed
                    && shell.stateVersion() == version + 1);
            }
            assert(&shell.sceneDocument().scene() == owner
                && shell.sceneDocument().runtimeSceneId() == uuid);
        }
    }
    assert(failures && commits);
    std::printf(
        "PASS identity history 64 quotas: %u rejections unchanged and retried, %u complete commits\n",
        failures,
        commits);
}

void identityResourceRetry(HistoryFixture& fixture)
{
    resource::ResourceManager resources;
    assert(arp::tests::loadSpriteFixture(resources));
    EditorShell shell;
    fixture.initialize(shell);
    shell.sceneDocument().setResourceManagerProvider([&resources] {
        return &resources;
    });
    const auto id = shell.sceneDocument().selectedObject();
    AddSceneComponentCommand add("SpriteRendererComponent");
    SetSceneComponentPropertyCommand source("spriteResourceId");
    source.valueKind = ScenePropertyValueKind::ResourceReference;
    source.stringValue = arp::tests::spriteA;
    add.initialProperties.push_back(std::move(source));
    HistoryFixture::completed(shell, std::move(add));
    const auto cid = shell.sceneDocument().scene().findObject(id)->components().back()->id();
    const auto original = presenceYaml(shell);
    HistoryFixture::completed(shell, RemoveSceneComponentCommand{});
    const auto removed = presenceYaml(shell);
    assert(resources.unload(arp::tests::spriteA).success);
    const auto version = shell.stateVersion();
    const auto rejected = shell.submitCommand(UndoSceneCommand{});
    assert(rejected.status == EditorResponseStatus::Failed
        && rejected.diagnostic == EditorDiagnosticCode::SceneHistoryRestoreRejected);
    assert(shell.stateVersion() == version && presenceYaml(shell) == removed);
    assert(!shell.sceneDocument().scene().findObject(id)->findComponent(cid));
    const auto repaired = resources.load<scene2d::SpriteResource>({},
        core::pmr::String(arp::tests::spriteA),
        core::pmr::String(arp::tests::atlasA),
        core::pmr::String("left"));
    assert(repaired.success);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(shell.sceneDocument().scene().findObject(id)->findComponent(cid)
        && presenceYaml(shell) == original);
    std::puts(
        "PASS unloaded resource rejects candidate without consuming history; explicit Ready repair restores original component ID");
}

void identityLockedExternal(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    auto& world = shell.sceneDocument().scene();
    const auto target = shell.sceneDocument().selectedObject();
    auto& outside = world.createObject("Locked reference owner");
    auto* links = dynamic_cast<scene::ObjectLinksComponent*>(
        shell.sceneDocument().addComponent("ObjectLinksComponent", outside.id()));
    assert(links);
    links->links.push_back({"incoming", target});
    const auto before = presenceYaml(shell);
    SetSceneObjectViewCommand lock;
    lock.objectId = outside.id().value();
    lock.documentEpoch =
        HistoryFixture::completed(shell, GetSceneHierarchyQuery{}).sceneHierarchy.documentEpoch;
    lock.locked = true;
    HistoryFixture::completed(shell, lock);
    const auto version = shell.stateVersion();
    const auto rejected = shell.submitCommand(DestroySceneObjectCommand{});
    assert(rejected.status == EditorResponseStatus::Failed
        && rejected.diagnostic == EditorDiagnosticCode::SceneObjectLocked);
    assert(shell.stateVersion() == version && presenceYaml(shell) == before
        && links->links.size() == 1);
    lock.locked = false;
    HistoryFixture::completed(shell, lock);
    HistoryFixture::completed(shell, DestroySceneObjectCommand{});
    assert(links->links.empty());
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(presenceYaml(shell) == before);
    std::puts(
        "PASS external ObjectLinks lock barrier and repair/Undo preserve complete reference transaction");
}

void identityBudgets(HistoryFixture& fixture)
{
    using Presence = EditorScenePresence;
    assert(Presence::fits(Presence::operationLimit, Presence::operationLimit));
    assert(Presence::fits(Presence::historyLimit, Presence::historyLimit));
    for (unsigned field = 0; field < 4; ++field) {
        auto cost = Presence::operationLimit;
        if (field == 0)
            ++cost.bytes;
        if (field == 1)
            ++cost.objects;
        if (field == 2)
            ++cost.components;
        if (field == 3)
            ++cost.references;
        assert(!Presence::fits(cost, Presence::operationLimit));
    }
    EditorShell shell;
    fixture.initialize(shell);
    const auto id = shell.sceneDocument().selectedObject();
    auto* timer = dynamic_cast<scene::TimerComponent*>(
        shell.sceneDocument().addComponent("TimerComponent", id));
    assert(timer);
    scene::TimerComponentEditorProperties native;
    native.eventName.assign(Presence::operationLimit.bytes, 'x');
    assert(timer->applyEditorAuthoringProperties(reflection::object(std::move(native))));
    const auto old = timer->nativeReflectionObject();
    const auto version = shell.stateVersion();
    const auto result = shell.submitCommand(RemoveSceneComponentCommand{});
    assert(result.status == EditorResponseStatus::Failed
        && result.diagnostic == EditorDiagnosticCode::SceneHistoryBudgetExceeded);
    assert(shell.stateVersion() == version && old.native_reference_alive());
    assert(shell.sceneDocument().scene().findObject(id)->findComponent(timer->id()) == timer);
    std::puts(
        "PASS exact/one-over object/component/reference/byte budgets; oversized native payload rejects removal before mutation");
}

void identityDeferredFields(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    AddSceneComponentCommand add("TimerComponent");
    SetSceneComponentPropertyCommand enabled("startOnEnable");
    enabled.valueKind = ScenePropertyValueKind::Boolean;
    enabled.booleanValue = true;
    SetSceneComponentPropertyCommand event("eventName");
    event.valueKind = ScenePropertyValueKind::String;
    event.stringValue = "restored.timer";
    SetSceneComponentPropertyCommand seconds("seconds");
    seconds.valueKind = ScenePropertyValueKind::Number;
    seconds.numberValue = 2.5;
    add.initialProperties.push_back(std::move(enabled));
    add.initialProperties.push_back(std::move(event));
    add.initialProperties.push_back(std::move(seconds));
    HistoryFixture::completed(shell, std::move(add));
    const auto before = presenceYaml(shell);
    HistoryFixture::completed(shell, RemoveSceneComponentCommand{});
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(presenceYaml(shell) == before);
    std::puts(
        "PASS complete authoring descriptor prepared before one domain apply; timer does not run during Passive restoration");
}

void identityUnwritableSnapshot(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto objectId = shell.sceneDocument().selectedObject();
    auto* timer = dynamic_cast<scene::TimerComponent*>(
        shell.sceneDocument().addComponent("TimerComponent", objectId));
    assert(timer);
    scene::TimerComponentEditorProperties authored;
    authored.eventName.assign(513, 'x');
    assert(timer->applyEditorAuthoringProperties(reflection::object(std::move(authored))));
    const auto componentId = timer->id();
    const auto before = presenceYaml(shell);
    const auto version = shell.stateVersion();
    for (EditorCommandPayload operation : {EditorCommandPayload(RemoveSceneComponentCommand{}),
             EditorCommandPayload(DestroySceneObjectCommand{})}) {
        const auto rejected = shell.submitCommand(std::move(operation));
        assert(rejected.status == EditorResponseStatus::Failed
            && rejected.diagnostic == EditorDiagnosticCode::SceneHistoryRestoreRejected);
        assert(shell.stateVersion() == version && presenceYaml(shell) == before);
        assert(shell.sceneDocument().scene().findObject(objectId)->findComponent(componentId)
            == timer);
    }
    std::puts("PASS native value beyond typed writer contract rejects deletion before mutation");
}

void identityHistoryEviction(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto objectId = shell.sceneDocument().selectedObject();
    for (unsigned index = 0; index <= 256; ++index) {
        const auto name = std::string("History-") + std::to_string(index);
        HistoryFixture::completed(shell, SetSceneObjectNameCommand(name));
    }
    for (unsigned index = 0; index < 256; ++index)
        HistoryFixture::completed(shell, UndoSceneCommand{});
    const auto* object = shell.sceneDocument().scene().findObject(objectId);
    assert(object && object->name() == "History-0");
    const auto version = shell.stateVersion();
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(shell.stateVersion() == version && object->name() == "History-0");
    for (unsigned index = 0; index < 256; ++index)
        HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(object->name() == "History-256");
    std::puts(
        "PASS bounded 256-entry history evicts oldest complete entries; retained branch Undo/Redo intact");
}

// Real, bounded authoring payloads cross the history byte limit before the
// entry/object/component/reference limits. No synthetic history entry is used.
class HistoryBudgetFixture final {
public:
    static constexpr std::size_t objectsPerGroup = 4;
    static constexpr std::size_t componentsPerObject = 6;

    explicit HistoryBudgetFixture(HistoryFixture& fixture)
    {
        fixture.initialize(shell);
        HistoryFixture::completed(shell, NewSceneCommand{true});
        assert(shell.sceneHistoryUsage().undoEntries == 0);
    }

    EditorScenePresence::Subtree createGroup()
    {
        auto& document = shell.sceneDocument();
        auto& world = document.scene();
        const auto root = world.createObject("Budget group").id();
        for (std::size_t index = 0; index < objectsPerGroup; ++index) {
            const auto id = index ? world.createObject("Budget child").id() : root;
            if (index)
                assert(world.reparent(id, root, index - 1).ok());
            for (const char* type :
                {"CameraComponent", "LightComponent", "SkyComponent", "TimerComponent"})
                assert(document.addComponent(type, id));
            auto* links = dynamic_cast<scene::ObjectLinksComponent*>(
                document.addComponent("ObjectLinksComponent", id));
            assert(links);
            links->links.reserve(kMaxSceneInspectorObjectLinks);
            for (std::size_t linkIndex = 0; linkIndex < kMaxSceneInspectorObjectLinks;
                ++linkIndex) {
                core::pmr::String name(128, 'x');
                const auto suffix = core::formatUnsigned(linkIndex);
                name.replace(name.size() - suffix.size(), suffix.size(), suffix);
                links->links.push_back({std::move(name), root});
            }
        }
        assert(document.select(root));
        return EditorScenePresence::captureSubtree(document, root);
    }

    static bool sameCost(EditorScenePresence::Cost a, EditorScenePresence::Cost b)
    {
        return a.bytes == b.bytes && a.objects == b.objects && a.components == b.components
            && a.references == b.references;
    }

    EditorShell shell;
};

void historyByteEviction(HistoryFixture& fixture)
{
    using Presence = EditorScenePresence;
    HistoryBudgetFixture state(fixture);
    auto& shell = state.shell;
    core::pmr::Vector<Presence::Subtree> groups;
    groups.push_back(state.createGroup());
    HistoryFixture::completed(shell, DestroySceneObjectCommand{});
    const auto unit = shell.sceneHistoryUsage().undo;
    assert(unit.bytes && unit.objects == HistoryBudgetFixture::objectsPerGroup);
    const auto retained = Presence::historyLimit.bytes / unit.bytes;
    const auto operations = retained + 2;
    assert(retained > 1 && operations < 256);
    // Every retained transaction can be restored together within the document
    // limits. The byte limit is the only active eviction constraint here.
    assert(operations * HistoryBudgetFixture::objectsPerGroup + 1
        <= EditorSceneAuthoringCodec::maxObjects);
    assert(operations * HistoryBudgetFixture::objectsPerGroup
                * HistoryBudgetFixture::componentsPerObject
            + 1
        <= EditorSceneAuthoringCodec::maxComponents);
    assert(operations * unit.objects < Presence::historyLimit.objects
        && operations * unit.components < Presence::historyLimit.components
        && operations * unit.references < Presence::historyLimit.references);
    for (std::size_t index = 1; index < operations; ++index) {
        groups.push_back(state.createGroup());
        HistoryFixture::completed(shell, DestroySceneObjectCommand{});
        const auto usage = shell.sceneHistoryUsage();
        const auto count = std::min(index + 1, retained);
        assert(usage.undoEntries == count && usage.redoEntries == 0);
        assert(usage.undo.bytes == count * unit.bytes);
        assert(Presence::fits(usage.undo, Presence::historyLimit));
    }
    const auto beforeRead = core::defaultCpuAllocator().stats();
    const auto version = shell.stateVersion();
    const auto usage = shell.sceneHistoryUsage();
    const auto afterRead = core::defaultCpuAllocator().stats();
    assert(usage.undoEntries == retained && usage.undo.bytes <= Presence::historyLimit.bytes
        && usage.undo.bytes + unit.bytes > Presence::historyLimit.bytes);
    assert(
        beforeRead.allocationCount == afterRead.allocationCount && version == shell.stateVersion());
    for (std::size_t index = operations; index-- > operations - retained;) {
        HistoryFixture::completed(shell, UndoSceneCommand{});
        assert(Presence::matchesSubtree(shell.sceneDocument(), groups[index]));
        const auto current = shell.sceneHistoryUsage();
        auto total = current.undo;
        total += current.redo;
        assert(HistoryBudgetFixture::sameCost(total, usage.undo));
    }
    assert(shell.sceneHistoryUsage().undoEntries == 0);
    const auto undoEnd = shell.stateVersion();
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(shell.stateVersion() == undoEnd);
    for (std::size_t index = 0; index < operations - retained; ++index)
        assert(!shell.sceneDocument().scene().findObject(groups[index].objects.front().id));
    for (std::size_t index = operations - retained; index < operations; ++index) {
        HistoryFixture::completed(shell, RedoSceneCommand{});
        assert(!shell.sceneDocument().scene().findObject(groups[index].objects.front().id));
    }
    assert(HistoryBudgetFixture::sameCost(shell.sceneHistoryUsage().undo, usage.undo));
    const auto redoEnd = shell.stateVersion();
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(shell.stateVersion() == redoEnd);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(shell.sceneHistoryUsage().redoEntries == 2);
    HistoryFixture::completed(shell, CreateSceneObjectCommand("New budget branch"));
    assert(shell.sceneHistoryUsage().redoEntries == 0);
    const auto branchVersion = shell.stateVersion();
    HistoryFixture::completed(shell, RedoSceneCommand{});
    assert(shell.stateVersion() == branchVersion);
    std::printf(
        "PASS real history byte eviction: limit=%zu unit=%zu retained=%zu operations=%zu; exact identities, complete transactions, conserved Undo/Redo ledger and branch retirement\n",
        Presence::historyLimit.bytes,
        unit.bytes,
        retained,
        operations);
}

void historyBudgetLifetime(HistoryFixture& fixture)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            HistoryBudgetFixture state(fixture);
            auto group = state.createGroup();
            HistoryFixture::completed(state.shell, DestroySceneObjectCommand{});
            const auto before = state.shell.sceneHistoryUsage();
            HistoryFixture::completed(state.shell, UndoSceneCommand{});
            assert(EditorScenePresence::matchesSubtree(state.shell.sceneDocument(), group));
            const auto undone = state.shell.sceneHistoryUsage();
            assert(undone.undoEntries == 0 && undone.redoEntries == 1
                && HistoryBudgetFixture::sameCost(undone.redo, before.undo));
            HistoryFixture::completed(state.shell, RedoSceneCommand{});
            assert(
                HistoryBudgetFixture::sameCost(state.shell.sceneHistoryUsage().undo, before.undo));
            assert(state.shell.drawCommands().ok());
            HistoryFixture::completed(state.shell, NewSceneCommand{true});
            const auto cleared = state.shell.sceneHistoryUsage();
            assert(cleared.undoEntries == 0 && cleared.redoEntries == 0 && cleared.undo.bytes == 0
                && cleared.redo.bytes == 0);
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS ordinary warmed20 history budget/Undo/Redo/replacement/draw/destroy; core live bytes/count delta=0");
}

void identityActivation(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto previous = shell.sceneDocument().selectedObject();
    auto version = shell.stateVersion();
    EditorRemoteProtocol protocol("identity-token");
    network::http::Request add;
    add.set_method(network::http::Method::Post);
    add.set_target("/api/v1/commands");
    add.add_header("Authorization", "Bearer identity-token");
    add.set_content_type(network::http::ContentType::Json);
    add.set_body_text(
        R"({"schemaVersion":1,"command":"editor.activate","source":"scene_object_add"})");
    assert(protocol.handle(add, shell).status().value() == 200);
    const auto created = shell.sceneDocument().selectedObject();
    assert(created != previous && shell.stateVersion() == version + 1);
    version = shell.stateVersion();
    HistoryFixture::completed(shell, ActivateControlCommand("scene_object_delete"));
    assert(
        !shell.sceneDocument().scene().findObject(created) && shell.stateVersion() == version + 1);
    HistoryFixture::completed(shell, UndoSceneCommand{});
    assert(shell.sceneDocument().scene().findObject(created));
    std::puts(
        "PASS actual activation create/delete uses presence commands with one nested version");
}

void identityBooleanProjection(HistoryFixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    HistoryFixture::completed(shell, AddSceneComponentCommand("MeshComponent"));
    const auto objectId = shell.sceneDocument().selectedObject();
    const auto properties = shell.sceneDocument().inspectorProperties(objectId);
    const auto field = std::find_if(properties.begin(), properties.end(), [](const auto& value) {
        return value.componentType == "MeshComponent" && value.name == "enabled";
    });
    assert(field != properties.end());
    const auto widget = EditorControlInput::find(shell, scenePropertyControlSource(*field));
    const auto* initialControl = shell.runtime().findControl(widget);
    assert(initialControl && initialControl->value().boolean);
    SetSceneComponentPropertyCommand enabled("enabled");
    enabled.objectId = objectId.value();
    enabled.componentId = field->componentId;
    enabled.valueKind = ScenePropertyValueKind::Boolean;
    enabled.booleanValue = false;
    HistoryFixture::completed(shell, enabled);
    assert(!shell.runtime().findControl(widget)->value().boolean);
    HistoryFixture::completed(shell, RemoveSceneComponentCommand{});
    HistoryFixture::completed(shell, UndoSceneCommand{});
    const auto* restored = shell.runtime().findControl(
        EditorControlInput::find(shell, scenePropertyControlSource(*field)));
    assert(restored && !restored->value().boolean);
    std::puts(
        "PASS typed boolean and restored component project the same Inspector checkbox value");
}

// Exercise all four production component types against one unrelated live
// simulation owner. History must preserve identities and external joint links.
void authoringPhysicsHistory(HistoryFixture& fixture, bool quotas)
{
    using namespace physics2d;
    resource::ResourceManager resources;
    assert(arp::tests::loadSpriteFixture(resources) && arp::tests::loadTileFixture(resources));
    auto world = core::makeShared<PhysicsWorld2D>(
        WorldConfig{{0, -10}, 32, 64, 16, 128, 256, 128, 32, 4, 1.f / 60.f});
    const auto unrelated = world->createBody({BodyType::Dynamic, {7, 9}});
    const auto nativeBefore = world->telemetry();
    EditorShell shell;
    fixture.initialize(shell);
    shell.sceneDocument().setPhysicsWorld2DProvider([world] {
        return world;
    });
    shell.sceneDocument().setResourceManagerProvider([&resources] {
        return &resources;
    });
    // The generated reference contract, rather than the human-readable Doc,
    // must permit an explicitly unconfigured authoring joint.
    Joint2DComponent jointSchema(world, ComponentUsage::Authoring);
    unsigned optionalEndpoints = 0;
    for (const auto& field : EditorSceneDocument::componentProperties(jointSchema))
        if (field.name == "bodyA" || field.name == "bodyB") {
            assert(field.kind == SceneInspectorPropertyKind::SceneObjectReference
                && field.resourceOptional && field.sceneObjectReferenceValue == 0);
            ++optionalEndpoints;
        }
    assert(optionalEndpoints == 2);
    const auto a = shell.sceneDocument().selectedObject();
    const auto* sceneOwner = &shell.sceneDocument().scene();
    const auto complete = [&](EditorCommandPayload command) {
        return HistoryFixture::completed(shell, std::move(command));
    };
    const auto object = [&](scene::GameObjectId id) {
        return shell.sceneDocument().scene().findObject(id);
    };
    complete(AddSceneComponentCommand("RigidBody2DComponent"));
    const auto bodyId = object(a)->getComponent<RigidBody2DComponent>()->id();
    auto oldBody = object(a)->findComponent(bodyId)->nativeReflectionObject();
    complete(AddSceneComponentCommand("Collider2DComponent"));
    const auto colliderId = object(a)->getComponent<Collider2DComponent>()->id();
    auto oldCollider = object(a)->findComponent(colliderId)->nativeReflectionObject();
    SetSceneComponentPropertyCommand radius("radius");
    radius.objectId = a.value();
    radius.componentId = colliderId.value();
    radius.valueKind = ScenePropertyValueKind::Number;
    radius.numberValue = .75f;
    complete(radius);
    RemoveSceneComponentCommand removeCollider;
    removeCollider.objectId = a.value();
    removeCollider.componentId = colliderId.value();
    complete(removeCollider);
    complete(UndoSceneCommand{});
    assert(!oldCollider.native_reference_alive()
        && object(a)->getComponent<Collider2DComponent>()->id() == colliderId
        && object(a)->getComponent<Collider2DComponent>()->desc().radius == .75f);
    RemoveSceneComponentCommand removeBody;
    removeBody.objectId = a.value();
    removeBody.componentId = bodyId.value();
    const auto beforeRefusal = presenceYaml(shell);
    assert(shell.submitCommand(removeBody).status == EditorResponseStatus::Failed
        && presenceYaml(shell) == beforeRefusal);
    complete(CreateSceneObjectCommand("internal body"));
    const auto b = shell.sceneDocument().selectedObject();
    complete(ReparentSceneObjectCommand{b.value(), a.value(), 0});
    complete(AddSceneComponentCommand("RigidBody2DComponent"));
    complete(AddSceneComponentCommand("Joint2DComponent"));
    const auto internalJointId = object(b)->getComponent<Joint2DComponent>()->id();
    const auto setEndpoint = [&](scene::GameObjectId owner,
                                 scene::ComponentId component,
                                 const char* field,
                                 scene::GameObjectId target) {
        SetSceneComponentPropertyCommand value(field);
        value.objectId = owner.value();
        value.componentId = component.value();
        value.valueKind = ScenePropertyValueKind::SceneObjectReference;
        value.sceneObjectReferenceValue = target.value();
        complete(value);
    };
    setEndpoint(b, internalJointId, "bodyA", a);
    setEndpoint(b, internalJointId, "bodyB", b);
    complete(CreateSceneObjectCommand("external joint"));
    const auto external = shell.sceneDocument().selectedObject();
    complete(AddSceneComponentCommand("Joint2DComponent"));
    const auto externalJointId = object(external)->getComponent<Joint2DComponent>()->id();
    setEndpoint(external, externalJointId, "bodyA", a);
    setEndpoint(external, externalJointId, "bodyB", b);
    complete(SelectSceneObjectCommand{a.value()});
    AddSceneComponentCommand tile("TileMapResourceComponent");
    SetSceneComponentPropertyCommand tileId("tileMapResourceId");
    tileId.valueKind = ScenePropertyValueKind::ResourceReference;
    tileId.stringValue = arp::tests::tileMapId;
    tile.initialProperties.push_back(tileId);
    for (auto entry : {std::pair{"maxResidentChunks", 4u},
             std::pair{"maxVisibleCells", 128u},
             std::pair{"firstObjectId", 1000u}}) {
        SetSceneComponentPropertyCommand value(entry.first);
        value.valueKind = ScenePropertyValueKind::UnsignedInteger;
        value.unsignedIntegerValue = entry.second;
        tile.initialProperties.push_back(std::move(value));
    }
    complete(std::move(tile));
    const auto mapId = object(a)->getComponent<scene2d::TileMapResourceComponent>()->id();
    const auto* projection = object(a)->getComponent<scene2d::TileMapResourceComponent>();
    assert(projection->usage() == ComponentUsage::Authoring
        && projection->runtime()->usage() == ComponentUsage::Authoring);
    assert(projection->runtime());
    const auto configured = presenceYaml(shell);
    const auto verifyRestored = [&] {
        assert(&shell.sceneDocument().scene() == sceneOwner && object(a) && object(b));
        assert(object(a)->getComponent<RigidBody2DComponent>()->id() == bodyId
            && !oldBody.native_reference_alive());
        assert(object(a)->getComponent<Collider2DComponent>()->id() == colliderId);
        assert(object(a)->getComponent<scene2d::TileMapResourceComponent>()->id() == mapId);
        const auto* joint = object(b)->getComponent<Joint2DComponent>();
        assert(joint->id() == internalJointId && joint->bodyAObjectId() == a.value()
            && joint->bodyBObjectId() == b.value() && !joint->handle());
        const auto* outside = object(external)->getComponent<Joint2DComponent>();
        assert(outside->id() == externalJointId && outside->bodyAObjectId() == a.value()
            && outside->bodyBObjectId() == b.value());
        assert(presenceYaml(shell) == configured);
    };
    complete(DestroySceneObjectCommand{});
    assert(!object(a) && !object(b));
    assert(object(external)->getComponent<Joint2DComponent>()->bodyAObjectId() == 0
        && object(external)->getComponent<Joint2DComponent>()->bodyBObjectId() == 0);
    assert(resources.unload(arp::tests::tileMapId).success);
    const auto rejectedVersion = shell.stateVersion();
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Failed
        && shell.stateVersion() == rejectedVersion && !object(a));
    assert(resources
            .load<scene2d::TileMapResource>({},
                core::pmr::String(arp::tests::tileMapId),
                core::pmr::String(arp::tests::tileSetId),
                ARP_SOURCE_DIR "/tests/fixtures/scene_2d/tilemap.yaml",
                65536)
            .success);
    complete(UndoSceneCommand{});
    verifyRestored();
    complete(RedoSceneCommand{});
    complete(UndoSceneCommand{});
    verifyRestored();
    if (quotas) {
        unsigned refusals = 0, commits = 0;
        for (auto bytes : {1u, 64u, 512u, 4096u, 16384u, 65536u, 262144u, 1048576u}) {
            auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
            const auto before = presenceYaml(shell);
            const auto version = shell.stateVersion();
            EditorResponse response;
            {
                core::CpuMemoryQuotaScope scope(quota);
                response = shell.submitCommand(DestroySceneObjectCommand{});
            }
            if (response.status == EditorResponseStatus::Failed) {
                ++refusals;
                assert(presenceYaml(shell) == before && shell.stateVersion() == version
                    && shell.sceneDocument().selectedObject() == a);
                complete(DestroySceneObjectCommand{});
            } else {
                ++commits;
                assert(response.status == EditorResponseStatus::Completed);
            }
            complete(UndoSceneCommand{});
            verifyRestored();
        }
        assert(refusals && commits);
    }
    const auto after = world->telemetry();
    assert(after.thirdPartyAllocations == nativeBefore.thirdPartyAllocations
        && after.bodies == nativeBefore.bodies && after.colliders == nativeBefore.colliders
        && after.joints == nativeBefore.joints && world->events().empty());
    BodyState state;
    assert(world->state(unrelated, state) && state.position.x == 7 && state.position.y == 9);
    assert(shell.drawCommands().ok());
    std::puts(
        "PASS four authoring components exact history, internal/external joint references, dependency refusal, resource retry and shared simulation isolation");
}

void authoringPhysicsLifetime(HistoryFixture& fixture)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        authoringPhysicsHistory(fixture, false);
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes
            && now.allocationCount - now.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS ordinary warmed20 physics authoring history cycles; core live bytes/count delta=0");
}

void identityLifetime(HistoryFixture& fixture)
{
    core::CpuMemoryStats baseline;
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            HistoryFixture::completed(shell, AddSceneComponentCommand("MeshComponent"));
            HistoryFixture::completed(shell, RemoveSceneComponentCommand{});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(shell, DestroySceneObjectCommand{});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            HistoryFixture::completed(shell, RedoSceneCommand{});
            HistoryFixture::completed(shell, UndoSceneCommand{});
            assert(shell.drawCommands().ok());
        }
        const auto now = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = now;
        assert(now.liveBytes == baseline.liveBytes);
        assert(now.allocationCount - now.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS ordinary warmed20 component/object identity history/update/draw/destruction; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    HistoryFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--history-budget") {
        historyByteEviction(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--history-budget-smoke") {
        historyBudgetLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--physics-authoring") {
        authoringPhysicsHistory(fixture, true);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--physics-authoring-smoke") {
        authoringPhysicsLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--identity-boolean") {
        identityBooleanProjection(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--identity-history") {
        identityComponentHistory(fixture);
        identitySubtreeHistory(fixture);
        identityFreshCreate(fixture);
        identityQuota(fixture);
        identityResourceRetry(fixture);
        identityLockedExternal(fixture);
        identityBudgets(fixture);
        identityDeferredFields(fixture);
        identityUnwritableSnapshot(fixture);
        identityHistoryEviction(fixture);
        identityActivation(fixture);
        identityBooleanProjection(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--identity-smoke") {
        identityLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--reparent-safety")
        return reparentSafety(fixture) ? 0 : 2;
    if (argc == 2 && std::string_view(argv[1]) == "--reparent-smoke") {
        reparentLifetime(fixture);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--safety-smoke") {
        safetyLifetime(fixture);
        return 0;
    }
    if (argc == 2
        && (std::string_view(argv[1]) == "--probe-safety"
            || std::string_view(argv[1]) == "--safety"))
        return safetyProbe(fixture) ? 0 : 2;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifetime(fixture);
    else {
        contract(fixture);
        persistence(fixture);
        remote(fixture);
    }
}
