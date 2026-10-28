#include "editor/tests/fixtures/editor_cli_client_fixture.h"
#include "editor/tests/fixtures/editor_remote_client_fixture.h"
#include "editor/tests/fixtures/script_scene_shell_fixture.h"

#include <cstdio>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace {
using namespace arp;
using namespace arp::editor;
using namespace arp::editor::testfixtures;
using Files = ScriptSceneShellFixture;
using Reply = std::pair<std::uint16_t, std::string>;
constexpr auto token = "script-fields-loopback-0123456789-0123456789";

std::string jsonString(std::string_view text)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    check(writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size())));
    return {buffer.GetString(), buffer.GetSize()};
}

std::string arguments(
    std::uint64_t owner, std::uint64_t component, std::string_view fields, bool enabled = true)
{
    return "\"objectId\":" + std::to_string(owner)
        + (component ? ",\"componentId\":" + std::to_string(component) : "") + ",\"resourceId\":"
        + jsonString(primaryId) + ",\"scriptType\":\"Mover\",\"moduleSchemaVersion\":1,\"enabled\":"
        + (enabled ? "true" : "false") + ",\"fieldsYaml\":" + jsonString(fields);
}

std::string rest(std::string_view command, std::string_view args = {})
{
    return "{\"schemaVersion\":1,\"command\":" + jsonString(command)
        + (args.empty() ? "" : "," + std::string(args)) + "}";
}

std::string mcp(std::string_view command, std::string_view args = {})
{
    return "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":"
        + jsonString(command) + ",\"arguments\":{" + std::string(args) + "}}}";
}

void completed(const Reply& reply, bool isMcp = false)
{
    check(reply.first == 200, core::pmr::String(reply.second));
    rapidjson::Document parsed;
    parsed.Parse(reply.second.data(), reply.second.size());
    check(!parsed.HasParseError());
    const auto& response = isMcp ? parsed["result"]["structuredContent"] : parsed;
    check(response["status"] == "completed", core::pmr::String(reply.second));
}

void cycle(Files& files, bool withCli)
{
    core::SharedPtr<const EditorScriptComponentCatalog> retained;
    reflection::object receiver;
    {
        EditorShell shell;
        files.open(shell);
        retained = shell.sceneDocument().scriptCatalog();
        const auto owner = shell.sceneDocument().scene().rootId().value();
        const auto component = files.scriptId.value();
        receiver = shell.sceneDocument().scene().findComponent(files.scriptId)->scriptInstance();
        EditorRemoteServer remote;
        check(remote.start(shell, token, 0));
        arp::editor::tests::EditorRemoteClientFixture client(remote, token);
        const auto sendRest = [&](std::string_view request) {
            return client.send(network::http::Method::Post, "/api/v1/commands", request, true);
        };
        const auto sendMcp = [&](std::string_view request) {
            return client.send(network::http::Method::Post,
                "/mcp",
                request,
                true,
                EditorRemoteProtocol::mcpProtocolVersion);
        };
        const auto discovery =
            client.send(network::http::Method::Get, "/api/v1/discovery", {}, true);
        check(discovery.first == 200
            && discovery.second.find("scene.script.fields.set") != std::string::npos);
        const auto tools = sendMcp("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
        rapidjson::Document listing;
        listing.Parse(tools.second.data(), tools.second.size());
        check(tools.first == 200 && !listing.HasParseError());
        bool found = false;
        for (const auto& tool : listing["result"]["tools"].GetArray()) {
            if (std::string_view(tool["name"].GetString()) != "scene.script.fields.set")
                continue;
            found = true;
            const auto& schema = tool["inputSchema"];
            check(!schema["additionalProperties"].GetBool());
            check(schema["required"].Size() == 7);
            check(schema["properties"]["fieldsYaml"]["maxLength"].GetUint64()
                == EditorSceneScriptPolicy::fields().maxYamlBytes);
        }
        check(found);

        const auto originalVersion = shell.stateVersion();
        const auto args = arguments(owner, component, "amount: 19\n", false);
        const auto request =
            rest("scene.script.fields.set", args + ",\"idempotencyKey\":\"fields\"");
        completed(sendRest(request));
        check(files.amount(shell) == 19 && shell.stateVersion() == originalVersion + 1
            && !shell.sceneDocument().scene().findComponent(files.scriptId)->enabled());
        const auto version = shell.stateVersion();
        completed(
            sendMcp(mcp("scene.script.fields.set", args + ",\"idempotencyKey\":\"fields\"")), true);
        check(shell.stateVersion() == version && shell.sceneHistoryUsage().undoEntries == 1);
        const auto rejected = [&](const Reply& reply) {
            check(reply.first >= 400 || reply.second.find("\"isError\":true") != std::string::npos
                    || reply.second.find("\"status\":\"failed\"") != std::string::npos
                    || reply.second.find("\"status\":\"rejected\"") != std::string::npos,
                core::pmr::String(reply.second));
            check(files.amount(shell) == 19 && shell.stateVersion() == version
                && shell.sceneHistoryUsage().undoEntries == 1
                && shell.sceneHistoryUsage().redoEntries == 0);
        };
        // Every field participates in cross-protocol idempotency, including the
        // two identities, module binding, complete YAML bytes and enabled state.
        for (const auto& replacement : {arguments(owner + 1, component, "amount: 19\n", false),
                 arguments(owner, component + 1, "amount: 19\n", false),
                 arguments(owner, component, "amount: 20\n", false),
                 arguments(owner, component, "amount: 19\n", true)}) {
            const auto reply = sendRest(
                rest("scene.script.fields.set", replacement + ",\"idempotencyKey\":\"fields\""));
            check(reply.first == 409);
            rejected(reply);
        }
        for (const auto& change : {std::pair<std::string, std::string>{primaryId, secondaryId},
                 {"\"Mover\"", "\"PhysicsLink\""},
                 {"\"moduleSchemaVersion\":1", "\"moduleSchemaVersion\":2"}}) {
            auto changed = args;
            const auto offset = changed.find(change.first);
            check(offset != std::string::npos);
            changed.replace(offset, change.first.size(), change.second);
            const auto reply =
                sendMcp(mcp("scene.script.fields.set", changed + ",\"idempotencyKey\":\"fields\""));
            check(reply.second.find("idempotency_key_conflict") != std::string::npos);
            rejected(reply);
        }
        rejected(sendRest(rest("scene.script.fields.set",
            args + ",\"expectedStateVersion\":" + std::to_string(version - 1))));
        rejected(sendRest(rest("scene.script.fields.set", args + ",\"enabled\":true")));
        rejected(sendMcp(mcp("scene.script.fields.set", args + ",\"fieldsYaml\":\"amount: 20\"")));
        rejected(sendRest(rest("scene.script.fields.set", args + ",\"unknown\":true")));
        rejected(sendRest(rest("scene.undo", "\"fieldsYaml\":\"amount: 20\"")));
        rejected(sendMcp(mcp("scene.script.fields.set", args + ",\"moduleSchema\":1")));
        for (const auto* invalid : {"amount: wrong\n",
                 "[\n",
                 "amount: 1\n---\namount: 2\n",
                 "{}\n",
                 "amount: 1\nextra: 2\n"})
            rejected(
                sendRest(rest("scene.script.fields.set", arguments(owner, component, invalid))));
        rejected(sendMcp(mcp("scene.script.fields.set",
            arguments(owner,
                component,
                std::string(EditorSceneScriptPolicy::fields().maxYamlBytes + 1, 'x')))));
        rejected(sendRest(rest("scene.script.fields.set",
            arguments(owner, component, std::string("amount: 2\0junk", 14)))));
        auto wrongSchema = args;
        wrongSchema.replace(wrongSchema.find("\"moduleSchemaVersion\":1"),
            23,
            "\"moduleSchemaVersion\":4294967296");
        rejected(sendRest(rest("scene.script.fields.set", wrongSchema)));
        files.files.write(
            "primary.arp", core::pmr::String(Fixture::primarySource) + "\n# source changed\n");
        rejected(
            sendMcp(mcp("scene.script.fields.set", arguments(owner, component, "amount: 21\n"))));
        files.files.write("primary.arp", Fixture::primarySource);

        completed(sendMcp(mcp("scene.undo")), true);
        check(files.amount(shell) == 7 && !shell.sceneDirty());
        completed(sendRest(rest("scene.redo")));
        check(files.amount(shell) == 19);
        const auto attachment = arguments(files.child.value(), 0, "amount: 29\n", false);
        const auto addRequest =
            rest("scene.script.add", attachment + ",\"idempotencyKey\":\"attach\"");
        completed(sendRest(addRequest));
        auto* child = shell.sceneDocument().scene().findObject(files.child);
        const auto attachedId = child->componentIds().back();
        const auto attachedReceiver = child->findComponent(attachedId)->scriptInstance();
        check(attachedReceiver.ptr<reflection::instance>()->slot("amount").cast<int>() == 29
            && !child->findComponent(attachedId)->enabled());
        const auto attachedVersion = shell.stateVersion();
        completed(
            sendMcp(mcp("scene.script.add", attachment + ",\"idempotencyKey\":\"attach\"")), true);
        check(shell.stateVersion() == attachedVersion && child->components().size() == 3);
        check(sendRest(rest("scene.script.add", attachment + ",\"componentId\":1")).first == 400);
        check(sendRest(rest("scene.script.add",
                           arguments(files.child.value(), 0, "amount: 30\n")
                               + ",\"idempotencyKey\":\"attach\""))
                  .first
            == 409);
        check(shell.stateVersion() == attachedVersion && child->components().size() == 3);
        completed(sendMcp(mcp("scene.undo")), true);
        check(!child->findComponent(attachedId)
            && !attachedReceiver.ptr<reflection::instance>()->alive());
        completed(sendRest(rest("scene.redo")));
        check(child->findComponent(attachedId)
            && child->findComponent(attachedId)->scriptInstance() != attachedReceiver);
        if (withCli) {
            const auto endpoint = "http://127.0.0.1:" + std::to_string(remote.port());
            const auto ownerText = std::to_string(owner), componentText = std::to_string(component);
            check(arp::editor::tests::runEditorCli(remote,
                endpoint,
                token,
                {"scene.script.add",
                    ownerText,
                    secondaryId,
                    "PhysicsLink",
                    "1",
                    "true",
                    "body: null\n"}));
            const auto cliLinkId = shell.sceneDocument().scene().componentIds().back();
            check(shell.sceneDocument()
                    .scene()
                    .findComponent(cliLinkId)
                    ->scriptInstance()
                    .ptr<reflection::instance>()
                    ->slot("body")
                    .is_undefined());
            completed(sendMcp(mcp("scene.undo")), true);
            check(!shell.sceneDocument().scene().findComponent(cliLinkId));

            check(arp::editor::tests::runEditorCli(remote,
                endpoint,
                token,
                {"scene.script.fields.set",
                    ownerText,
                    componentText,
                    primaryId,
                    "Mover",
                    "1",
                    "true",
                    "amount: 23\n"}));
            check(files.amount(shell) == 23);
            completed(sendRest(rest("scene.undo")));
            check(files.amount(shell) == 19);
            check(arp::editor::tests::runEditorCli(remote, endpoint, token, {"scene.redo"}));
            check(files.amount(shell) == 23);
            // CLI raw integer/bool tokens cannot inject extra JSON members.
            check(!arp::editor::tests::runEditorCli(remote,
                endpoint,
                token,
                {"scene.script.fields.set",
                    "1,\"evil\":1",
                    componentText,
                    primaryId,
                    "Mover",
                    "1",
                    "true",
                    "amount: 30\n"}));
            check(!arp::editor::tests::runEditorCli(remote,
                endpoint,
                token,
                {"scene.script.fields.set",
                    ownerText,
                    componentText,
                    primaryId,
                    "Mover",
                    "1",
                    "null",
                    "amount: 30\n"}));
            check(files.amount(shell) == 23);
        }
        const int expected = withCli ? 23 : 19;
        SaveSceneCommand save("remote-fields.yaml");
        save.overwriteExisting = true;
        Files::succeeded(
            arp::editor::tests::ScenePersistenceFixture::finish(shell, shell.submitCommand(save)),
            true);
        Files::load(shell, "remote-fields.yaml");
        const auto* restoredChild =
            shell.sceneDocument().scene().findObject(files.child)->findComponent(attachedId);
        check(restoredChild
            && restoredChild->scriptInstance()
                    .ptr<reflection::instance>()
                    ->slot("amount")
                    .cast<int>()
                == 29);
        check(files.amount(shell) == expected && !shell.sceneDirty()
            && !receiver.ptr<reflection::instance>()->alive());
        client.shutdown();
        remote.stop();
    }
    for (const auto& entry : retained->modules().entries())
        check(entry.module->liveInstances() == 0);
}
} // namespace

int main(int argc, char** argv)
{
    Files files;
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    cycle(files, !smoke);
    if (smoke) {
        const auto baseline = core::defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            cycle(files, false);
            const auto current = core::defaultCpuAllocator().stats();
            check(current.liveBytes == baseline.liveBytes);
            check(current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
        }
        std::printf(
            "ordinary_script_field_remote_cycles=20 live_bytes_delta=0 live_count_delta=0 retired_instances=0 peak_bytes=%llu\n",
            static_cast<unsigned long long>(core::defaultCpuAllocator().stats().peakBytes));
    }
    std::puts(
        "PASS real REST/MCP v4 fields, cross-protocol history, strict inputs, exact idempotency and save/reopen");
}
