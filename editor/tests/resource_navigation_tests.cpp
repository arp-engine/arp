#include "editor/tests/fixtures/editor_protocol_fixture.h"
#include "editor/tests/fixtures/editor_remote_client_fixture.h"
#include "editor/tests/fixtures/resource_navigation_fixture.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_resource_navigation_smoke.h"
#include "editor/workspace/project_browser_model.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace arp::editor;
using Fixture = tests::ResourceNavigationFixture;

void browserPreparation(const tests::RecoveryFixture& files)
{
    tests::RecoveryFixture::write(files.root / "file.yaml", "test");
    ProjectBrowserModel browser;
    browser.setRoot(std::filesystem::canonical(files.root));
    assert(browser.refresh());
    browser.setSearch("missing");
    ProjectBrowserModel output;
    std::size_t index = 999;
    assert(browser.prepareReveal(files.root / "missing", output, index));
    assert(index == 999 && output.root().empty() && browser.search() == "missing");
    assert(!browser.prepareReveal(files.root / "file.yaml", output, index));
    assert(output.search().empty() && output.entries()[index].path == files.root / "file.yaml");
    assert(browser.search() == "missing");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        index = 999;
        assert(browser.prepareReveal(files.root / "file.yaml", output, index));
    }
    assert(index == 999 && output.search().empty() && quota->liveBytes() == 0);
}

void commands(const tests::RecoveryFixture& files, const Fixture& workspace)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    auto* mesh = workspace.prepare(shell);
    const auto command = Fixture::command(shell, *mesh);
    const auto source = Fixture::source(shell);
    const auto owner = shell.sceneDocument().selectedObject();
    SetSceneObjectNameCommand rename("Renamed");
    rename.objectId = owner.value();
    assert(shell.submitCommand(std::move(rename)).status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    core::pmr::String before, after, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), before, diagnostic));
    const auto dirty = shell.sceneDirty();
    const auto camera = shell.sceneViewportCamera();
    for (const auto value : {"", "not-a-uuid", tests::ProjectPackageFixture::shaderId}) {
        mesh->set_material_resource_id(core::pmr::String(value));
        assert(shell.submitCommand(command).diagnostic
            == EditorDiagnosticCode::ResourceDefinitionUnavailable);
        assert(shell.selectedFilePath().empty());
    }
    mesh->set_material_resource_id(workspace.materialId);
    shell.entryChanged("file_search", "no matches");
    assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
    assert(shell.selectedFilePath() == workspace.first && !shell.filesPresentationFailed());
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(command).stateVersion == version);
    assert(shell.sceneDocument().selectedObject() == owner && shell.sceneDirty() == dirty);
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic)
        && before == after);
    assert(camera.target().x == shell.sceneViewportCamera().target().x);
    assert(shell.submitCommand(RedoSceneCommand{}).status == EditorResponseStatus::Completed);
    mesh->set_material_resource_id(Fixture::alternateId);
    assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
    assert(shell.selectedFilePath() == workspace.second);
    auto stale = command;
    ++stale.resourceRevision;
    assert(shell.submitCommand(stale).diagnostic == EditorDiagnosticCode::StaleDocumentTarget);
    stale = command;
    ++stale.documentEpoch;
    assert(shell.submitCommand(stale).diagnostic == EditorDiagnosticCode::StaleDocumentTarget);
    shell.activate("open_scene");
    assert(shell.submitCommand(command).diagnostic == EditorDiagnosticCode::WorkspaceBusy);
    shell.activate("scene_ui_cancel");
    const auto bytes = tests::RecoveryFixture::read(workspace.second);
    tests::RecoveryFixture::write(workspace.second, bytes + "\n# changed\n");
    assert(shell.submitCommand(command).diagnostic
        == EditorDiagnosticCode::ResourceDefinitionUnavailable);
    assert(shell.selectedFilePath() == workspace.second);
    assert(EditorControlInput::click(shell, source));
    assert(shell.runtime().canvas().findWidget(shell.id("file_definition_unavailable"))->visible());
    assert(shell.selectedFilePath() == workspace.second);
    tests::RecoveryFixture::write(workspace.second, bytes);
    assert(EditorControlInput::click(shell, source));
    assert(
        !shell.runtime().canvas().findWidget(shell.id("file_definition_unavailable"))->visible());
    tests::EditorProtocolFixture protocol(shell);
    char fields[512];
    std::snprintf(fields,
        sizeof(fields),
        "\"objectId\":%llu,\"componentId\":%llu,\"property\":\"materialResourceId\",\"documentEpoch\":%llu,\"resourceRevision\":%llu",
        (unsigned long long)command.objectId,
        (unsigned long long)command.componentId,
        (unsigned long long)command.documentEpoch,
        (unsigned long long)command.resourceRevision);
    for (const bool mcp : {false, true}) {
        const auto response = protocol.call("scene.resource.locate", fields, mcp);
        assert(response.body_text().find("completed") != core::pmr::String::npos);
        const auto duplicate = protocol.call(
            "scene.resource.locate", core::pmr::String(fields) + ",\"resourceRevision\":1", mcp);
        assert(duplicate.body_text().find(mcp ? "unknown_tool_argument" : "invalid_command_fields")
            != core::pmr::String::npos);
        const auto unknown = protocol.call(
            "scene.resource.locate", core::pmr::String(fields) + ",\"unknown\":1", mcp);
        assert(unknown.body_text().find(mcp ? "unknown_tool_argument" : "unknown_envelope_field")
            != core::pmr::String::npos);
    }
    mesh->set_material_resource_id(workspace.materialId);
    const auto keyed = core::pmr::String(fields) + ",\"idempotencyKey\":\"definition-locate\"";
    assert(protocol.call("scene.resource.locate", keyed).body_text().find("completed")
        != core::pmr::String::npos);
    mesh->set_material_resource_id(Fixture::alternateId);
    assert(protocol.call("scene.resource.locate", keyed).body_text().find("completed")
        != core::pmr::String::npos);
    assert(shell.selectedFilePath() == workspace.first);
    assert(shell.submitCommand(command).status == EditorResponseStatus::Completed
        && shell.selectedFilePath() == workspace.second);
    assert(shell.retrySceneInspector());
    assert(EditorControlInput::click(shell, source));
    const auto oldWidget = EditorControlInput::find(shell, source);
    assert(shell.submitCommand(CreateSceneObjectCommand("Other")).status
        == EditorResponseStatus::Completed);
    assert(!shell.acceptsInspectorInput(source, oldWidget));
    tests::RecoveryFixture::write(files.root / "project.arp.yaml",
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"));
    const auto opened =
        shell.submitCommand(OpenProjectCommand{core::FilePath::utf8(files.root), true});
    if (opened.status != EditorResponseStatus::Completed)
        std::fprintf(stderr,
            "project open status=%d diagnostic=%d\n",
            int(opened.status),
            int(opened.diagnostic));
    assert(opened.status == EditorResponseStatus::Completed);
    assert(shell.submitCommand(command).diagnostic == EditorDiagnosticCode::StaleDocumentTarget);
    std::puts(
        "PASS current resource, exact file, no-op/Redo, stale/busy, files changed, typed/REST/MCP strict fields and idempotency");
}

// Verify native dispatch and both remote transports observe the same current field.
void loopback(const tests::RecoveryFixture& files, const Fixture& workspace)
{
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    auto* mesh = workspace.prepare(shell);
    const auto command = Fixture::command(shell, *mesh);
    constexpr std::string_view token = "01234567890123456789012345678901";
    EditorRemoteServer remote;
    assert(remote.start(shell, token, 0));
    tests::EditorRemoteClientFixture transport(remote, token);
    char fields[512];
    std::snprintf(fields,
        sizeof(fields),
        "\"objectId\":%llu,\"componentId\":%llu,\"property\":\"materialResourceId\",\"documentEpoch\":%llu,\"resourceRevision\":%llu",
        (unsigned long long)command.objectId,
        (unsigned long long)command.componentId,
        (unsigned long long)command.documentEpoch,
        (unsigned long long)command.resourceRevision);
    for (const bool mcp : {false, true}) {
        mesh->set_material_resource_id(workspace.materialId);
        assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
        const auto directVersion = shell.stateVersion();
        std::string body = mcp
            ? R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"scene.resource.locate","arguments":{)"
            : R"({"schemaVersion":1,"command":"scene.resource.locate",)";
        body += fields;
        body += mcp ? "}}}" : "}";
        const auto response = transport.send(network::http::Method::Post,
            mcp ? "/mcp" : "/api/v1/commands",
            body,
            true,
            mcp ? "2025-11-25" : "");
        assert(response.first == 200 && response.second.find("completed") != std::string::npos);
        assert(
            shell.selectedFilePath() == workspace.first && shell.stateVersion() == directVersion);
        mesh->set_material_resource_id(Fixture::alternateId);
        const auto current = transport.send(network::http::Method::Post,
            mcp ? "/mcp" : "/api/v1/commands",
            body,
            true,
            mcp ? "2025-11-25" : "");
        assert(current.first == 200 && current.second.find("completed") != std::string::npos);
        assert(shell.selectedFilePath() == workspace.second
            && shell.stateVersion() == directVersion + 1);
        const auto source = shell.sceneDocument().selectedObject();
        mesh->set_material_resource_id("not-a-uuid");
        const auto rejected = transport.send(network::http::Method::Post,
            mcp ? "/mcp" : "/api/v1/commands",
            body,
            true,
            mcp ? "2025-11-25" : "");
        assert(rejected.second.find("resource_definition_unavailable") != std::string::npos);
        assert(shell.selectedFilePath() == workspace.second
            && shell.sceneDocument().selectedObject() == source);
        assert(shell.stateVersion() == directVersion + 1);
    }
    remote.stop();
    transport.shutdown();
    std::puts(
        "PASS real loopback REST/MCP match typed current-field navigation/no-op/failure semantics");
}

void failedFileInput(const tests::RecoveryFixture& files, const Fixture& workspace)
{
    // Ordinary directory with enough entries to exercise a real scrollbar.
    for (unsigned index = 0; index < 17; ++index) {
        char name[24];
        std::snprintf(name, sizeof(name), "z-file-%u.yaml", index);
        tests::RecoveryFixture::write(workspace.second.parent_path() / name, "unmapped test file");
    }
    EditorShell shell;
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    auto* mesh = workspace.prepare(shell);
    mesh->set_material_resource_id(Fixture::alternateId);
    assert(shell.submitCommand(Fixture::command(shell, *mesh)).status
        == EditorResponseStatus::Completed);
    const auto hasScrollbar = [&] {
        const auto draws = shell.drawCommands();
        assert(draws.ok());
        return std::any_of(draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
            return draw.widgetId == shell.id("files_view")
                && draw.rect.x == layout::kFileScrollbarX;
        });
    };
    assert(hasScrollbar());
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!shell.retryFileBrowserPresentation());
    }
    const auto isolated = [&] {
        assert(shell.filesPresentationFailed() && shell.selectedFilePath() == workspace.second);
        for (const auto* name : layout::kFileSlots)
            assert(!shell.runtime().canvas().findWidget(shell.id(name))->visible());
    };
    isolated();
    EditorInputCommand event;
    event.kind = EditorInputKind::Wheel;
    event.x = 100;
    event.y = 800;
    event.wheelDelta = -1;
    (void)shell.submitCommand(event);
    isolated();
    event.kind = EditorInputKind::PointerDown;
    event.primaryButton = true;
    event.button = EditorPointerButton::Left;
    event.x = layout::kFileScrollbarX;
    event.y = layout::kFileScrollbarY + 120;
    (void)shell.submitCommand(event);
    event.kind = EditorInputKind::PointerMoved;
    event.y += 60;
    (void)shell.submitCommand(event);
    event.kind = EditorInputKind::PointerUp;
    (void)shell.submitCommand(event);
    isolated();
    assert(!hasScrollbar());
    assert(EditorControlInput::click(shell, "file_navigation_retry"));
    assert(!shell.filesPresentationFailed() && hasScrollbar() && quota->liveBytes() == 0);
    std::puts(
        "PASS failed Files view rejects wheel/scrollbar input and hides stale rows/scrollbar until retry");
}

void allocationFailures(const tests::RecoveryFixture& files, const Fixture& workspace)
{
    unsigned rejected = 0;
    for (const auto limit : {1u, 4096u, 16384u, 65536u, 262144u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(limit);
        {
            EditorShell shell;
            assert(shell
                    .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                        files.root)
                    .ok());
            auto* mesh = workspace.prepare(shell);
            const auto command = Fixture::command(shell, *mesh);
            const auto owner = shell.sceneDocument().selectedObject();
            EditorCommandEnvelope envelope(command);
            EditorResponse result;
            {
                core::CpuMemoryQuotaScope scope(quota);
                result = shell.submitCommand(std::move(envelope));
            }
            if (result.status != EditorResponseStatus::Completed) {
                ++rejected;
                assert(shell.selectedFilePath().empty());
            } else
                assert(shell.selectedFilePath() == workspace.first);
            assert(shell.sceneDocument().selectedObject() == owner
                && mesh->materialResourceId() == workspace.materialId);
            assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
        }
        assert(quota->liveBytes() == 0);
    }
    assert(rejected);
    std::puts(
        "PASS ordinary allocation failures preserve browser/Scene before commit; unbounded retry releases each quota");
}

void smoke(const tests::RecoveryFixture& files, const Fixture& workspace)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            assert(shell
                    .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                        files.root)
                    .ok());
            auto* mesh = workspace.prepare(shell);
            const auto command = Fixture::command(shell, *mesh);
            assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
            mesh->set_material_resource_id(Fixture::alternateId);
            assert(shell.submitCommand(command).status == EditorResponseStatus::Completed);
            assert(shell.selectedFilePath() == workspace.second);
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            {
                core::CpuMemoryQuotaScope scope(quota);
                assert(!shell.retryFileBrowserPresentation());
            }
            assert(shell.filesPresentationFailed() && quota->liveBytes() == 0);
            if (!EditorControlInput::click(shell, "file_navigation_retry")) {
                const auto layout = shell.runtime().canvas().resolveLayout();
                const auto target = EditorControlInput::find(shell, "file_navigation_retry");
                const auto* rect = layout.findWidget(target);
                const auto hit = shell.runtime()
                                     .canvas()
                                     .hitTest(layout,
                                         {rect->rect.x + rect->rect.width / 2,
                                             rect->rect.y + rect->rect.height / 2})
                                     .widgetId();
                const auto* widget = shell.runtime().canvas().findWidget(hit);
                std::fprintf(stderr,
                    "retry target=%llu visible=%d enabled=%d hit=%s\n",
                    (unsigned long long)target.value(),
                    rect->visibleInHierarchy,
                    rect->enabledInHierarchy,
                    widget ? widget->desc().name.c_str() : "null");
                assert(false);
            }
            assert(
                !shell.filesPresentationFailed() && shell.selectedFilePath() == workspace.second);
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary Editor package/open/navigate/switch/destroy cycles; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    Fixture workspace(files.root / "project");
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(files, workspace);
    else {
        browserPreparation(files);
        commands(files, workspace);
        loopback(files, workspace);
        failedFileInput(files, workspace);
        allocationFailures(files, workspace);
        for (const auto scenario : {"nav_valid",
                 "nav_current",
                 "nav_missing",
                 "nav_unmapped",
                 "nav_failed",
                 "nav_retry"}) {
            EditorShell shell;
            assert(shell
                    .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                        files.root)
                    .ok());
            core::pmr::String diagnostic;
            if (!EditorResourceNavigationSmoke::run(shell, scenario, diagnostic)) {
                std::fprintf(stderr, "%s: %s\n", scenario, diagnostic.c_str());
                assert(false);
            }
        }
    }
}
