#include "2d/physics_components.h"
#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/editor_remote_client_fixture.h"
#include "editor/tests/fixtures/resource_navigation_fixture.h"
#include "scene/camera_temporal_component.h"

#include <cstdio>
#include <limits>
#include <new>

namespace {
using namespace arp;
using namespace arp::editor;
using Files = tests::ResourceNavigationFixture;

void load(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    shell.activate("workspace_3D");
}

AssignSceneReferenceCommand resourceCommand(EditorShell& shell, const scene3d::MeshComponent& mesh)
{
    const auto identity = Files::command(shell, mesh);
    AssignSceneReferenceCommand result;
    auto& intent = result.intent;
    intent.objectId = identity.objectId;
    intent.componentId = identity.componentId;
    intent.property = identity.property;
    intent.documentEpoch = identity.documentEpoch;
    intent.resourceRevision = identity.resourceRevision;
    intent.sourceKind = EditorSceneReferenceAssignment::SourceKind::Resource;
    intent.sourceResourceId = Files::alternateId;
    return result;
}

core::pmr::String serialized(EditorShell& shell)
{
    core::pmr::String yaml, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), yaml, diagnostic));
    return yaml;
}

core::pmr::String fields(const AssignSceneReferenceCommand& command)
{
    const auto& i = command.intent;
    char values[512];
    std::snprintf(values,
        sizeof(values),
        "\"objectId\":%llu,\"componentId\":%llu,\"documentEpoch\":%llu,\"resourceRevision\":%llu,\"property\":\"%s\",",
        (unsigned long long)i.objectId,
        (unsigned long long)i.componentId,
        (unsigned long long)i.documentEpoch,
        (unsigned long long)i.resourceRevision,
        i.property.c_str());
    core::pmr::String result(values);
    if (i.sourceKind == EditorSceneReferenceAssignment::SourceKind::Resource) {
        result += "\"sourceKind\":\"resource\",\"sourceResourceId\":\"";
        result += i.sourceResourceId;
        result += '"';
    } else {
        std::snprintf(values,
            sizeof(values),
            "\"sourceKind\":\"object\",\"sourceObjectId\":%llu",
            (unsigned long long)i.sourceObjectId);
        result += values;
    }
    return result;
}

void assertRejected(EditorShell& shell, const AssignSceneReferenceCommand& command)
{
    const auto bytes = serialized(shell);
    const auto version = shell.stateVersion();
    const auto dirty = shell.sceneDirty();
    assert(shell.submitCommand(command).status != EditorResponseStatus::Completed);
    assert(serialized(shell) == bytes && shell.sceneDirty() == dirty
        && shell.stateVersion() == version);
}

void resourceHistory(const tests::RecoveryFixture& root, const Files& files)
{
    EditorShell shell;
    load(shell, root);
    auto* mesh = files.prepare(shell);
    auto command = resourceCommand(shell, *mesh);
    SetSceneObjectNameCommand rename("Renamed");
    rename.objectId = command.intent.objectId;
    assert(shell.submitCommand(std::move(rename)).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    auto noop = command;
    noop.intent.sourceResourceId = files.materialId;
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(noop).stateVersion == version);
    auto invalid = command;
    invalid.intent.sourceResourceId = tests::ProjectPackageFixture::shaderId;
    assertRejected(shell, invalid);
    assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject())->name()
        == "Renamed");
    const auto before = serialized(shell);
    assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
    assert(mesh->material_resource_id() == Files::alternateId);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(serialized(shell) == before);
    assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(mesh->material_resource_id() == Files::alternateId);
    const auto after = shell.stateVersion();
    assert(shell.submitCommand(RedoSceneCommand{}).stateVersion == after);
    for (unsigned kind = 0; kind != 6; ++kind) {
        invalid = command;
        switch (kind) {
        case 0:
            ++invalid.intent.documentEpoch;
            break;
        case 1:
            ++invalid.intent.resourceRevision;
            break;
        case 2:
            invalid.intent.sourceObjectId = command.intent.objectId;
            break;
        case 3:
            invalid.intent.property = "missing";
            break;
        case 4:
            invalid.intent.sourceResourceId.clear();
            break;
        case 5:
            invalid.intent.componentId = std::numeric_limits<std::uint64_t>::max();
            break;
        }
        assertRejected(shell, invalid);
    }
    shell.activate("open_scene");
    assert(shell.submitCommand(command).diagnostic == EditorDiagnosticCode::WorkspaceBusy);
    shell.activate("scene_ui_cancel");
    SelectSceneObjectsCommand multiple;
    multiple.primaryObjectId = command.intent.objectId;
    multiple.objectIds = {command.intent.objectId, shell.sceneDocument().scene().rootId().value()};
    assert(shell.submitCommand(std::move(multiple)).status == EditorResponseStatus::Completed);
    assertRejected(shell, command);
    assert(shell.submitCommand(SelectSceneObjectCommand{command.intent.objectId}).status
        == EditorResponseStatus::Completed);
    const auto fileBytes = tests::RecoveryFixture::read(files.second);
    tests::RecoveryFixture::write(files.second, fileBytes + "\n# externally modified\n");
    assertRejected(shell, command);
    tests::RecoveryFixture::write(files.second, fileBytes);
    std::puts(
        "PASS resource assignment, exact identity/file/type/selection gates, one-field history and no-op/rejected Redo preservation");
}

void objectHistory(const tests::RecoveryFixture& root)
{
    EditorShell shell;
    load(shell, root);
    auto world = core::makeShared<physics2d::PhysicsWorld2D>(
        physics2d::WorldConfig{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f});
    assert(world->valid());
    shell.sceneDocument().setPhysicsWorld2DProvider([world] {
        return world;
    });
    const auto create = [&](const char* type) {
        assert(shell.submitCommand(CreateSceneObjectCommand("Same name")).status
            == EditorResponseStatus::Completed);
        assert(shell.submitCommand(AddSceneComponentCommand(type)).status
            == EditorResponseStatus::Completed);
        return shell.sceneDocument().selectedObject();
    };
    const auto a = create("RigidBody2DComponent");
    const auto b = create("RigidBody2DComponent");
    const auto anchor = create("RigidBody2DComponent");
    const auto owner = create("Joint2DComponent");
    auto* joint = shell.sceneDocument()
                      .scene()
                      .findObject(owner)
                      ->getComponent<physics2d::Joint2DComponent>();
    assert(joint);
    auto props = joint->editorAuthoringProperties();
    props.cast<physics2d::Joint2DEditorProperties&>().bodyA = a.value();
    props.cast<physics2d::Joint2DEditorProperties&>().bodyB = anchor.value();
    assert(joint->applyEditorAuthoringProperties(props));
    assert(shell.retrySceneInspector());
    const auto identity = shell.submitCommand(GetSceneInspectorSelectionQuery{});
    AssignSceneReferenceCommand command;
    command.intent.objectId = owner.value();
    command.intent.componentId = joint->id().value();
    command.intent.documentEpoch = identity.sceneInspectorSelection.documentEpoch;
    command.intent.resourceRevision = identity.sceneInspectorSelection.resourceRevision;
    command.intent.property = "bodyA";
    command.intent.sourceObjectId = b.value();
    const auto before = serialized(shell);
    assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
    tests::EditorProtocolFixture protocol(shell);
    assert(protocol.call("scene.undo").body_text().find("completed") != core::pmr::String::npos);
    assert(serialized(shell) == before);
    for (const bool mcp : {false, true}) {
        const auto response = protocol.call("scene.reference.assign", fields(command), mcp);
        if (response.body_text().find("completed") == core::pmr::String::npos)
            std::fprintf(stderr,
                "object assignment protocol=%s: %.*s\n",
                mcp ? "MCP" : "REST",
                (int)response.body_text().size(),
                response.body_text().data());
        assert(response.body_text().find("completed") != core::pmr::String::npos);
        assert(protocol.call("scene.undo", {}, !mcp).body_text().find("completed")
            != core::pmr::String::npos);
        assert(serialized(shell) == before);
    }
    const auto keyed = fields(command) + ",\"idempotencyKey\":\"assign-object\"";
    assert(protocol.call("scene.reference.assign", keyed).body_text().find("completed")
        != core::pmr::String::npos);
    auto otherSource = command;
    otherSource.intent.sourceObjectId = a.value();
    const auto conflict = protocol.call("scene.reference.assign",
        fields(otherSource) + ",\"idempotencyKey\":\"assign-object\"",
        true);
    assert(conflict.body_text().find("idempotency_key_conflict") != core::pmr::String::npos);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    assert(serialized(shell) == before);
    command.intent.sourceObjectId = owner.value();
    assertRejected(shell, command);
    auto& runtime = shell.sceneDocument()
                        .scene()
                        .findObject(owner)
                        ->addComponent<scene::CameraTemporalComponent>();
    command.intent.componentId = runtime.id().value();
    command.intent.property = "previousFovDegrees_";
    command.intent.sourceObjectId = b.value();
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(command).status != EditorResponseStatus::Completed);
    assert(shell.stateVersion() == version);
    assert(shell.sceneDocument().removeComponent(owner, runtime.id()));
    std::puts(
        "PASS live joint object reference through shared typed/REST/MCP history; actual metadata rejects invalid and read-only targets");
}

void protocols(const tests::RecoveryFixture& root, const Files& files)
{
    EditorShell shell;
    load(shell, root);
    auto* mesh = files.prepare(shell);
    auto command = resourceCommand(shell, *mesh);
    tests::EditorProtocolFixture protocol(shell);
    const auto base = fields(command);
    const auto keyed = base + ",\"idempotencyKey\":\"assign-reference\"";
    assert(protocol.call("scene.reference.assign", keyed).body_text().find("completed")
        != core::pmr::String::npos);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    const auto undone = shell.stateVersion();
    assert(protocol.call("scene.reference.assign", keyed, true).body_text().find("completed")
        != core::pmr::String::npos);
    assert(mesh->material_resource_id() == files.materialId && shell.stateVersion() == undone);
    for (unsigned field = 0; field != 7; ++field) {
        auto conflict = command;
        switch (field) {
        case 0:
            ++conflict.intent.objectId;
            break;
        case 1:
            ++conflict.intent.componentId;
            break;
        case 2:
            ++conflict.intent.documentEpoch;
            break;
        case 3:
            ++conflict.intent.resourceRevision;
            break;
        case 4:
            conflict.intent.property = "other";
            break;
        case 5:
            conflict.intent.sourceResourceId = files.materialId;
            break;
        case 6:
            conflict.intent.sourceKind = EditorSceneReferenceAssignment::SourceKind::Object;
            conflict.intent.sourceResourceId.clear();
            conflict.intent.sourceObjectId = command.intent.objectId;
            break;
        }
        const auto result = protocol.call("scene.reference.assign",
            fields(conflict) + ",\"idempotencyKey\":\"assign-reference\"",
            true);
        assert(result.body_text().find("idempotency_key_conflict") != core::pmr::String::npos);
        assert(shell.stateVersion() == undone && mesh->material_resource_id() == files.materialId);
    }
    for (const bool mcp : {false, true}) {
        for (const auto extra :
            {",\"sourceObjectId\":1", ",\"resourceRevision\":1", ",\"unknown\":1"}) {
            const auto response = protocol.call("scene.reference.assign", base + extra, mcp);
            assert(response.body_text().find("completed") == core::pmr::String::npos);
            assert(
                shell.stateVersion() == undone && mesh->material_resource_id() == files.materialId);
        }
        auto malformed = command;
        malformed.intent.property.assign(EditorSceneDocument::maxPropertyNameBytes + 1, 'x');
        assert(protocol.call("scene.reference.assign", fields(malformed), mcp)
                   .body_text()
                   .find("completed")
            == core::pmr::String::npos);
    }
    std::puts(
        "PASS strict REST/MCP arguments, complete source/target/revision fingerprint, cross-protocol receipt replay without reapplying mutation");
}

void loopback(const tests::RecoveryFixture& root, const Files& files)
{
    EditorShell shell;
    load(shell, root);
    auto* mesh = files.prepare(shell);
    const auto command = resourceCommand(shell, *mesh);
    constexpr auto token = "01234567890123456789012345678901";
    EditorRemoteServer server;
    assert(server.start(shell, token, 0));
    tests::EditorRemoteClientFixture transport(server, token);
    for (const bool mcp : {false, true}) {
        std::string body = mcp
            ? R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.reference.assign","arguments":{)"
            : R"({"schemaVersion":1,"command":"scene.reference.assign",)";
        body += fields(command);
        body += mcp ? "}}}" : "}";
        const auto before = shell.stateVersion();
        const auto result = transport.send(network::http::Method::Post,
            mcp ? "/mcp" : "/api/v1/commands",
            body,
            true,
            mcp ? "2025-11-25" : "");
        assert(result.first == 200 && result.second.find("completed") != std::string::npos);
        assert(mesh->material_resource_id() == Files::alternateId
            && shell.stateVersion() == before + 1);
        const auto noop = transport.send(network::http::Method::Post,
            mcp ? "/mcp" : "/api/v1/commands",
            body,
            true,
            mcp ? "2025-11-25" : "");
        assert(noop.first == 200 && noop.second.find("completed") != std::string::npos);
        assert(shell.stateVersion() == before + 1);
        assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
        assert(mesh->material_resource_id() == files.materialId);
    }
    server.stop();
    transport.shutdown();
    std::puts(
        "PASS actual loopback REST/MCP assignment matches typed mutation, no-op and Undo semantics");
}

void allocation(const tests::RecoveryFixture& root, const Files& files)
{
    unsigned rejected = 0, completed = 0;
    for (const auto budget : {1u, 256u, 2048u, 16384u, 262144u, 4194304u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget);
        {
            EditorShell shell;
            load(shell, root);
            auto* mesh = files.prepare(shell);
            const auto command = resourceCommand(shell, *mesh);
            const auto before = serialized(shell);
            const auto version = shell.stateVersion();
            bool success = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                success = shell.submitCommand(command).status == EditorResponseStatus::Completed;
            } catch (const std::bad_alloc&) {
            }
            if (!success) {
                ++rejected;
                assert(serialized(shell) == before && shell.stateVersion() == version);
                assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
            } else
                ++completed;
            assert(mesh->material_resource_id() == Files::alternateId);
            assert(
                shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
            assert(serialized(shell) == before);
        }
        assert(quota->liveBytes() == 0);
    }
    assert(rejected && completed);
    std::puts(
        "PASS bounded quota rejection preserves Scene/version, retry and Undo release ownership");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture root;
    Files files(root.root / "reference-project");
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned cycle = 0; cycle <= 20; ++cycle) {
            resourceHistory(root, files);
            objectHistory(root);
            const auto current = core::defaultCpuAllocator().stats();
            if (!cycle)
                baseline = current;
            assert(current.liveBytes == baseline.liveBytes
                && current.allocationCount - current.deallocationCount
                    == baseline.allocationCount - baseline.deallocationCount);
        }
        std::puts(
            "PASS 20 warmed ordinary shared object/resource assignment/history cycles; core live bytes/count delta=0");
    } else {
        resourceHistory(root, files);
        objectHistory(root);
        protocols(root, files);
        loopback(root, files);
        allocation(root, files);
    }
}
