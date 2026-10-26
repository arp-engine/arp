#include "2d/scene_sprite_extractor.h"
#include "3d/components.h"
#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/editor_remote_client_fixture.h"
#include "editor/tests/fixtures/transform_selection_fixture.h"
#include "editor/viewport/editor_scene_component_gizmos.h"
#include "editor/viewport/editor_scene_viewport_frame_builder.h"
#include "editor/workspace/editor_scene_transaction.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using Status = EditorResponseStatus;

std::uint64_t epoch(EditorShell& shell)
{
    const auto query = shell.submitCommand(GetSceneInspectorSelectionQuery{});
    assert(query.status == Status::Completed && query.hasSceneInspectorSelection);
    return query.sceneInspectorSelection.documentEpoch;
}

EditorResponse view(EditorShell& shell, scene::GameObjectId id, bool hidden, bool locked)
{
    return shell.submitCommand(SetSceneObjectViewCommand{id.value(), epoch(shell), hidden, locked});
}

core::pmr::String serialized(EditorShell& shell)
{
    core::pmr::String yaml, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), yaml, diagnostic));
    return yaml;
}

void reject(EditorShell& shell, EditorCommandPayload payload)
{
    const auto before = serialized(shell);
    const auto version = shell.stateVersion();
    const auto result = shell.submitCommand(std::move(payload));
    assert(result.status == Status::Failed);
    assert(shell.stateVersion() == version && serialized(shell) == before);
}

void writesAndHistory(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture f(files);
    auto& shell = f.shell;
    assert(
        shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status == Status::Completed);
    SetSceneObjectNameCommand rename("Changed");
    rename.objectId = f.first.value();
    assert(shell.submitCommand(rename).status == Status::Completed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == Status::Completed);
    const auto before = serialized(shell);
    const auto selection = shell.sceneDocument().selectedObject();
    assert(view(shell, f.first, true, true).status == Status::Completed);
    assert(serialized(shell) == before && shell.sceneDocument().selectedObject() == selection);
    assert(!shell.sceneObjectPickable(f.first) && !shell.sceneObjectPickable(f.child));
    assert(!shell.sceneObjectVisible(f.child) && shell.sceneObjectVisible(f.second));
    const auto version = shell.stateVersion();
    assert(view(shell, f.first, true, true).status == Status::Completed);
    assert(shell.stateVersion() == version);
    reject(shell, rename);
    reject(shell, SetSceneObjectActiveCommand{f.first.value(), false});
    reject(shell, SetSceneObjectTranslationCommand{1, 2, 3});
    SetSceneObjectTransformCommand transform;
    transform.objectId = f.first.value();
    reject(shell, transform);
    reject(shell, BeginSceneTransformEditCommand{f.first.value()});
    reject(shell, DestroySceneObjectCommand{});
    AddSceneComponentCommand add("LightComponent");
    add.objectId = f.first.value();
    reject(shell, add);
    RemoveSceneComponentCommand remove;
    remove.objectId = f.first.value();
    reject(shell, remove);
    SetSceneComponentPropertyCommand property("enabled");
    property.objectId = f.first.value();
    reject(shell, property);
    ReparentSceneObjectCommand reparent;
    reparent.objectId = f.child.value();
    reparent.parentId = f.second.value();
    reject(shell, reparent);
    reject(shell, RedoSceneCommand{});
    const char* issue = nullptr;
    assert(!shell.prepareSceneTransform(TransformSceneSelectionCommand{}, issue));
    assert(issue && std::string_view(issue) == "EDITOR-TRANSFORM-LOCKED");
    // Hierarchy selection remains usable. Locking a descendant also prevents
    // an ancestor's indirect mutation, without preventing unrelated siblings.
    assert(view(shell, f.first, false, false).status == Status::Completed);
    assert(view(shell, f.child, false, true).status == Status::Completed);
    reject(shell, transform);
    reject(shell, SetSceneObjectActiveCommand{f.first.value(), false});
    reject(shell, DestroySceneObjectCommand{});
    assert(shell.submitCommand(rename).status == Status::Completed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == Status::Completed);
    assert(view(shell, f.child, false, false).status == Status::Completed);
    assert(shell.submitCommand(RedoSceneCommand{}).status == Status::Completed);
    // A transform prepared while unlocked cannot bypass a subsequently locked
    // child, even when no dispatcher version changed (native test ingress).
    auto token = f.prepare();
    assert(shell.execute(SetSceneObjectViewCommand{f.child.value(), epoch(shell), false, true})
            .success);
    assert(shell.commitSceneTransform(*token) == EditorShell::TransformCommitResult::Stale);
    assert(view(shell, f.child, false, false).status == Status::Completed);
    assert(view(shell, shell.sceneDocument().scene().rootId(), false, true).status
        == Status::Completed);
    reject(shell, CreateSceneObjectCommand("Blocked"));
    assert(view(shell, shell.sceneDocument().scene().rootId(), false, false).status
        == Status::Completed);
    assert(shell.submitCommand(CreateSceneObjectCommand("Allowed")).status == Status::Completed);
    std::puts(
        "PASS shared direct/inherited write barriers, hidden/locked selection, no-op/Redo preservation and prepared transform recheck");
}

void batchAndReplacement(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture f(files);
    auto& shell = f.shell;
    f.apply();
    assert(view(shell, f.child, false, true).status == Status::Completed);
    reject(shell, UndoSceneCommand{});
    assert(view(shell, f.child, false, false).status == Status::Completed);
    f.history(true);
    assert(view(shell, f.first, true, false).status == Status::Completed);
    f.history(false);
    const auto e = epoch(shell);
    assert(shell.submitCommand(BeginSceneTransformEditCommand{f.first.value()}).status
        == Status::Completed);
    reject(shell, SetSceneObjectViewCommand{f.first.value(), e, false, true});
    assert(shell.submitCommand(EndSceneTransformEditCommand{f.first.value()}).status
        == Status::Completed);
    reject(shell, SetSceneObjectViewCommand{f.first.value(), e + 1, false, true});
    EditorSceneDocument candidate(false);
    auto same = candidate.createObject("Replacement");
    while (same.value() < f.first.value())
        same = candidate.createObject("Replacement");
    assert(same == f.first);
    core::pmr::String diagnostic;
    auto replacement = shell.prepareSceneReplacement(candidate, diagnostic);
    assert(replacement);
    assert(shell.commitSceneReplacement(*replacement, candidate)
        == EditorShell::SceneReplacementResult::Committed);
    assert(shell.sceneObjectView(same).valid && shell.sceneObjectVisible(same));
    assert(shell.sceneObjectEditable(same));
    std::puts(
        "PASS batch history lock barrier, active gesture/stale epoch rejection and replacement numeric ID isolation");
}

void oom(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture f(files);
    auto& shell = f.shell;
    const auto e = epoch(shell);
    for (const auto bytes : {std::size_t(1), std::size_t(4096)}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto version = shell.stateVersion();
        {
            core::CpuMemoryQuotaScope scope(quota);
            const auto result =
                shell.submitCommand(SetSceneObjectViewCommand{f.first.value(), e, true, true});
            // At the larger quota preparation may commit and Inspector may
            // quarantine. The receipt must match the actual published model.
            assert((result.status == Status::Completed)
                == shell.sceneObjectView(f.first).local.locked);
            if (result.status != Status::Completed)
                assert(shell.stateVersion() == version);
        }
        assert(view(shell, f.first, false, false).status == Status::Completed);
        assert(shell.retrySceneInspector());
        assert(quota->liveBytes() == 0);
    }
    std::puts("PASS view preparation/presentation allocation failure receipts and retry");
}

core::pmr::String fields(const SetSceneObjectViewCommand& command)
{
    char buffer[256];
    std::snprintf(buffer,
        sizeof(buffer),
        "\"objectId\":%llu,\"documentEpoch\":%llu,\"hidden\":%s,\"locked\":%s",
        (unsigned long long)command.objectId,
        (unsigned long long)command.documentEpoch,
        command.hidden ? "true" : "false",
        command.locked ? "true" : "false");
    return core::pmr::String(buffer);
}

void protocols(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture f(files);
    auto& shell = f.shell;
    tests::EditorProtocolFixture protocol(shell);
    const SetSceneObjectViewCommand command{f.first.value(), epoch(shell), true, true};
    const auto base = fields(command);
    const auto before = serialized(shell);
    const auto primary = shell.sceneDocument().selectedObject();
    for (const bool mcp : {false, true}) {
        assert(view(shell, f.first, false, false).status == Status::Completed);
        const auto version = shell.stateVersion();
        auto result = protocol.call("scene.object.view.set", base, mcp);
        assert(result.body_text().find("completed") != std::string_view::npos);
        assert(
            shell.stateVersion() == version + 1 && shell.sceneObjectView(f.child).effective.locked);
        result = protocol.call("scene.object.view.set", base, mcp);
        assert(result.body_text().find("completed") != std::string_view::npos);
        assert(shell.stateVersion() == version + 1);
        const auto query = shell.submitCommand(GetSceneHierarchyQuery{});
        assert(query.sceneHierarchy.documentEpoch == command.documentEpoch);
        for (const auto& entry : query.sceneHierarchy.entries) {
            if (entry.objectId == f.first.value())
                assert(
                    entry.hidden && entry.locked && entry.effectiveHidden && entry.effectiveLocked);
            if (entry.objectId == f.child.value())
                assert(!entry.hidden && !entry.locked && entry.effectiveHidden
                    && entry.effectiveLocked);
        }
        result = protocol.call("scene.hierarchy.get", {}, mcp);
        assert(result.body_text().find("\"effectiveHidden\":true") != std::string_view::npos);
        assert(result.body_text().find("\"documentEpoch\":") != std::string_view::npos);
        const auto rejected = [&](std::string_view invalid) {
            const auto response = protocol.call("scene.object.view.set", invalid, mcp);
            assert(response.body_text().find("completed") == std::string_view::npos);
            assert(shell.stateVersion() == version + 1);
        };
        for (const auto suffix : {",\"unknown\":1",
                 ",\"active\":false",
                 ",\"hidden\":false",
                 ",\"locked\":false",
                 ",\"objectId\":1",
                 ",\"documentEpoch\":1",
                 ",\"expectedStateVersion\":0,\"expectedStateVersion\":1",
                 ",\"idempotencyKey\":\"a\",\"idempotencyKey\":\"b\""})
            rejected(base + suffix);
        for (const auto invalid : {"\"objectId\":1,\"documentEpoch\":1,\"hidden\":true",
                 "\"objectId\":1,\"documentEpoch\":1,\"locked\":false",
                 "\"objectId\":1,\"hidden\":true,\"locked\":false",
                 "\"objectId\":1,\"documentEpoch\":0,\"hidden\":true,\"locked\":false",
                 "\"objectId\":0,\"documentEpoch\":1,\"hidden\":true,\"locked\":false",
                 "\"objectId\":1,\"documentEpoch\":1,\"hidden\":1,\"locked\":false",
                 "\"objectId\":1,\"documentEpoch\":1,\"hidden\":true,\"locked\":\"false\""})
            rejected(invalid);
        auto stale = command;
        ++stale.documentEpoch;
        rejected(fields(stale));
        rejected(base + ",\"expectedStateVersion\":0");
        const auto lockedWrite = protocol.call("scene.active.set",
            "\"objectId\":" + core::pmr::String(std::to_string(f.child.value()))
                + ",\"active\":false",
            mcp);
        assert(lockedWrite.body_text().find("scene_object_locked") != std::string_view::npos);
        assert(shell.stateVersion() == version + 1);
    }
    const auto key = base + ",\"idempotencyKey\":\"view-flags\"";
    assert(protocol.call("scene.object.view.set", key).body_text().find("completed")
        != std::string_view::npos);
    assert(view(shell, f.first, false, false).status == Status::Completed);
    const auto resetVersion = shell.stateVersion();
    assert(protocol.call("scene.object.view.set", key, true).body_text().find("completed")
        != std::string_view::npos);
    assert(!shell.sceneObjectView(f.first).local.locked && shell.stateVersion() == resetVersion);
    for (unsigned field = 0; field < 4; ++field) {
        auto other = command;
        if (field == 0)
            other.objectId = f.second.value();
        if (field == 1)
            ++other.documentEpoch;
        if (field == 2)
            other.hidden = false;
        if (field == 3)
            other.locked = false;
        assert(protocol
                   .call("scene.object.view.set",
                       fields(other) + ",\"idempotencyKey\":\"view-flags\"",
                       true)
                   .body_text()
                   .find("idempotency_key_conflict")
            != std::string_view::npos);
        assert(shell.stateVersion() == resetVersion);
    }
    assert(serialized(shell) == before && shell.sceneDocument().selectedObject() == primary);
    std::puts(
        "PASS REST/MCP strict fields/types/duplicates/epoch/version, inherited hierarchy, lock barrier and complete cross-protocol idempotency");
}

void loopback(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture f(files);
    auto& shell = f.shell;
    constexpr auto token = "01234567890123456789012345678901";
    EditorRemoteServer server;
    assert(server.start(shell, token, 0));
    tests::EditorRemoteClientFixture transport(server, token);
    const SetSceneObjectViewCommand command{f.first.value(), epoch(shell), true, true};
    for (const bool mcp : {false, true}) {
        assert(view(shell, f.first, false, false).status == Status::Completed);
        std::string body = mcp
            ? R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.object.view.set","arguments":{)"
            : R"({"schemaVersion":1,"command":"scene.object.view.set",)";
        body += fields(command);
        body += mcp ? "}}}" : "}";
        const auto version = shell.stateVersion();
        const auto send = [&](bool authorized) {
            return transport.send(network::http::Method::Post,
                mcp ? "/mcp" : "/api/v1/commands",
                body,
                authorized,
                mcp ? "2025-11-25" : "");
        };
        assert(send(false).first == 401 && shell.stateVersion() == version);
        const auto changed = send(true);
        assert(changed.first == 200 && changed.second.find("completed") != std::string::npos);
        assert(
            shell.stateVersion() == version + 1 && shell.sceneObjectView(f.child).effective.locked);
        assert(send(true).first == 200 && shell.stateVersion() == version + 1);
    }
    server.stop();
    transport.shutdown();
    std::puts(
        "PASS real authenticated loopback REST/MCP match typed hide/lock/no-op and owner-thread semantics");
}

void pickingFilters(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture f(files);
    auto& shell = f.shell;
    assert(
        shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status == Status::Completed);
    assert(view(shell, f.first, false, true).status == Status::Completed);
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* viewport = layout.findWidget(shell.id("viewport"));
    assert(viewport);
    float x = 0, y = 0;
    assert(shell.sceneViewportCamera().project(
        f.local(f.second).translation, viewport->rect.width, viewport->rect.height, x, y));
    EditorInputCommand click;
    click.primaryButton = true;
    click.button = EditorPointerButton::Left;
    click.x = viewport->rect.x + x;
    click.y = viewport->rect.y + y;
    const auto applyClick = [&] {
        click.kind = EditorInputKind::PointerDown;
        assert(shell.submitCommand(click).status == Status::Completed);
        click.kind = EditorInputKind::PointerUp;
        assert(shell.submitCommand(click).status == Status::Completed);
    };
    applyClick();
    assert(shell.sceneDocument().selectedObject() == f.second);
    assert(view(shell, f.second, true, false).status == Status::Completed);
    assert(
        shell.submitCommand(SelectSceneObjectCommand{f.first.value()}).status == Status::Completed);
    applyClick();
    assert(shell.sceneDocument().selectedObject() == f.first);
    std::puts(
        "PASS locked selection cannot block another viewport origin pick; hidden objects cannot be picked");
}

void projectionConsumers(const tests::RecoveryFixture& files)
{
    tests::TransformSelectionFixture f(files);
    auto& shell = f.shell;
    auto& scene = shell.sceneDocument().scene();
    const auto source = scene.extractTraversalSnapshot({EditorSceneViewState::maxEntries});
    const auto before = serialized(shell);
    const auto camera = buildEditorSceneViewportFrame(scene,
        source,
        shell.stateVersion(),
        640,
        360,
        {0, 0, 640, 360},
        &shell.projectResources(),
        EditorSceneViewState::maxEntries);
    assert(camera.frame.cameraObjectId.valid());
    const auto root = scene.rootId();
    assert(view(shell, root, true, false).status == Status::Completed);
    const char* diagnostic = nullptr;
    const auto visible = shell.visibleSceneSnapshot(source, diagnostic);
    assert(visible && visible->nodes().empty());
    const auto hidden = buildEditorSceneViewportFrame(scene,
        source,
        shell.stateVersion(),
        640,
        360,
        {0, 0, 640, 360},
        &shell.projectResources(),
        EditorSceneViewState::maxEntries,
        &*visible);
    assert(hidden.valid() && hidden.frame.drawItems.empty() && !hidden.frame.sky.present);
    assert(hidden.frame.cameraObjectId == camera.frame.cameraObjectId);
    scene2d::SceneSpriteExtractor sprites(
        shell.projectResources(), EditorSceneViewState::maxEntries, true);
    assert(sprites.extract(scene, *visible) && sprites.items().empty());
    const auto gizmos = buildEditorSceneComponentGizmos(scene,
        shell.projectResources(),
        16.f / 9.f,
        kEditorSceneComponentGizmoSegmentBudget,
        &*visible);
    assert(gizmos.valid && gizmos.segments.empty());
    assert(!shell.sceneSelectionGizmoVisible() && serialized(shell) == before);
    // Hidden broken resources never enter resolution; showing them must fail.
    const auto terrainId = shell.sceneDocument().createObject("broken terrain");
    auto& terrain = scene.findObject(terrainId)->addComponent<scene3d::TerrainComponent>();
    terrain.set_resource_id(core::pmr::String("missing-terrain"));
    const auto withTerrain = scene.extractTraversalSnapshot({EditorSceneViewState::maxEntries});
    const auto all = buildEditorSceneViewportFrame(scene,
        withTerrain,
        shell.stateVersion(),
        640,
        360,
        {0, 0, 640, 360},
        &shell.projectResources(),
        EditorSceneViewState::maxEntries);
    assert(all.validation.diagnostic == EditorSceneViewportDiagnostic::TerrainResourceInvalid);
    const auto withoutTerrain = shell.visibleSceneSnapshot(withTerrain, diagnostic);
    assert(withoutTerrain && withoutTerrain->nodes().empty());
    const auto excluded = buildEditorSceneViewportFrame(scene,
        withTerrain,
        shell.stateVersion(),
        640,
        360,
        {0, 0, 640, 360},
        &shell.projectResources(),
        EditorSceneViewState::maxEntries,
        &*withoutTerrain);
    assert(excluded.valid());
    assert(view(shell, root, false, true).status == Status::Completed);
    assert(!shell.sceneSelectionGizmoVisible() && shell.sceneObjectVisible(terrainId));
    std::puts(
        "PASS hidden extraction before resource resolution, original camera preserved, empty Sprite/gizmo and hidden/locked transform tool gates");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (!smoke) {
        pickingFilters(files);
        projectionConsumers(files);
        protocols(files);
        loopback(files);
    }
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        writesAndHistory(files);
        batchAndReplacement(files);
        oom(files);
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes);
        assert(current.allocationCount - current.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary full Shell view-state/history/replacement cycles; core live bytes/count delta=0");
}
