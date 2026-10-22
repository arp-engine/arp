#include "2d/components.h"
#include "2d/particle_emitter_component.h"
#include "2d/physics_components.h"
#include "2d/sprite_animation_component.h"
#include "2d/tile_map_resource_component.h"
#include "3d/animator_component.h"
#include "3d/components.h"
#include "editor/remote/editor_remote_server.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_shell.h"
#include "network/http/client.h"
#include "network/io_service.h"
#include "network/socket_runtime.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "scene/runtime_components.h"
#include "scene/scene_asset_resource.h"
#include "tests/fixtures/tile_map_fixture.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::string createAnimatorPackage(const std::filesystem::path& projectRoot)
{
    using namespace arp;
    const auto packageRoot = projectRoot / "Assets" / "Animator.arpkg";
    std::filesystem::create_directories(projectRoot / "Source");

    assets::CharacterImportResult imported;
    imported.success = true;
    imported.character.mesh = assets::MeshAsset::debugTriangle();
    imported.character.skinnedVertices.resize(imported.character.mesh.vertices.size());
    for (auto& vertex : imported.character.skinnedVertices) {
        vertex.joints = {0u, 0u, 0u, 0u};
        vertex.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    }
    animation::Joint rootJoint;
    rootJoint.name = "root";
    imported.character.skeleton.joints.push_back(rootJoint);
    imported.character.bindPose.push_back(animation::LocalTransform{});
    animation::AnimationClip clip;
    clip.name = "idle";
    clip.durationSeconds = 1.0f;
    animation::AnimationChannel channel;
    channel.jointIndex = 0u;
    channel.jointName = "root";
    channel.targetPath = "translation";
    channel.keyframes.push_back(animation::JointKeyframe{});
    auto end = animation::JointKeyframe{};
    end.timeSeconds = 1.0f;
    channel.keyframes.push_back(end);
    clip.channels.push_back(channel);
    imported.character.clips.push_back(clip);
    animation::AnimationClip walkClip;
    walkClip.name = "walk";
    walkClip.durationSeconds = 1.0f;
    animation::AnimationChannel walkChannel;
    walkChannel.jointIndex = 0u;
    walkChannel.jointName = "root";
    walkChannel.targetPath = "translation";
    walkChannel.keyframes.push_back(animation::JointKeyframe{});
    auto walkEnd = animation::JointKeyframe{};
    walkEnd.timeSeconds = 1.0f;
    walkChannel.keyframes.push_back(walkEnd);
    walkClip.channels.push_back(walkChannel);
    imported.character.clips.push_back(walkClip);
    imported.stateMachine.animator.name = "remote-editor-controller";
    imported.stateMachine.animator.defaultStateName = "idle";
    animation::AnimatorState idle;
    idle.name = "idle";
    idle.clipName = "idle";
    imported.stateMachine.animator.states.push_back(idle);
    animation::AnimatorState walk;
    walk.name = "walk";
    walk.clipName = "walk";
    imported.stateMachine.animator.states.push_back(walk);

    resource::PrefabAssetGraphBuildDesc desc;
    desc.ids.rootId = text("0b5f7a2d-78a7-47d2-91f5-4ab0f8b827e1");
    desc.ids.prefabId = desc.ids.rootId;
    desc.ids.basicShaderId = text("f0817aa6-f639-46c9-b9de-0ea317a2b94d");
    desc.alias = text("prefab/remote-animator");
    desc.displayName = text("remote-animator");
    desc.sourcePath = text((projectRoot / "Source").string().c_str());
    desc.sourceKind = text("SyntheticPrefabFolder");
    desc.shaderPaths.basicMaterialPreview = text("shaders/basic.slang");
    desc.shaderPaths.stylizedScene = text("shaders/stylized.slang");
    desc.shaderPaths.guiOverlay = text("shaders/gui.slang");
    desc.startupSplashShaderPath = text("shaders/startup.slang");
    desc.runtimePayloadPackageRoot = packageRoot;
    desc.importedCharacter = &imported;
    const auto package = resource::PrefabAssetGraphBuilder().buildPackage(desc);
    assert(package.success);
    assert(resource::InternalAssetPackageWriter().write(package.package, packageRoot).success);
    {
        std::ofstream manifest(projectRoot / "project.arp.yaml", std::ios::binary);
        manifest << "format: ArpEditorProject\nversion: 1\nassetPackages:\n"
                    "  - Assets/Animator.arpkg\n";
        assert(manifest.good());
    }
    return package.ids.prefabId.c_str();
}

bool isReadyResource(void* context, std::string_view resourceType, std::string_view resourceId)
{
    auto* manager = static_cast<arp::resource::ResourceManager*>(context);
    const auto* record = manager->recordSnapshot(resourceId);
    return record && record->state == arp::resource::ResourceState::Ready
        && std::string_view(record->type.data(), record->type.size()) == resourceType;
}

#if defined(_WIN32)
std::string quoteWindowsArgument(std::string_view value)
{
    std::string result("\"");
    std::size_t backslashes = 0;
    for (const auto character : value) {
        if (character == '\\') {
            ++backslashes;
            continue;
        }
        if (character == '"')
            result.append(backslashes * 2 + 1, '\\');
        else
            result.append(backslashes, '\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}

bool runMcpSdkClient(arp::editor::EditorRemoteServer& remote,
    std::string_view endpoint,
    std::string_view token,
    std::uint64_t documentId)
{
    const auto* module = std::getenv("ARP_MCP_CLIENT_MODULE");
    if (!module || !*module)
        return true;

    const auto script =
        std::filesystem::path(ARP_SOURCE_DIR) / "tests/tooling/editor_mcp_client_interop.mjs";
    const auto command = quoteWindowsArgument("node.exe") + " "
        + quoteWindowsArgument(script.string()) + " " + quoteWindowsArgument(module) + " "
        + quoteWindowsArgument(endpoint) + " " + quoteWindowsArgument(token) + " "
        + quoteWindowsArgument(std::to_string(documentId));
    auto mutableCommand = arp::core::pmr::String(command, &arp::core::defaultCpuMemoryResource());
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process))
        return false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    DWORD exitCode = STILL_ACTIVE;
    while (std::chrono::steady_clock::now() < deadline) {
        remote.pump();
        if (WaitForSingleObject(process.hProcess, 1) == WAIT_OBJECT_0)
            break;
        Sleep(1);
    }
    if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0)
        TerminateProcess(process.hProcess, 1);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}
#endif

bool runEditorCli(arp::editor::EditorRemoteServer& remote,
    std::string_view endpoint,
    std::string_view token,
    std::initializer_list<std::string_view> arguments)
{
#if defined(ARP_EDITOR_CLI_EXECUTABLE) && defined(_WIN32)
    const auto command = quoteWindowsArgument(ARP_EDITOR_CLI_EXECUTABLE) + " --url "
        + quoteWindowsArgument(endpoint) + " --token " + quoteWindowsArgument(token);
    std::string completeCommand = command;
    for (const auto argument : arguments)
        completeCommand += " " + quoteWindowsArgument(argument);
    auto mutableCommand =
        arp::core::pmr::String(completeCommand, &arp::core::defaultCpuMemoryResource());
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        remote.pump();
        if (WaitForSingleObject(process.hProcess, 1) == WAIT_OBJECT_0)
            break;
        Sleep(1);
    }
    const bool completed = WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0;
    if (!completed)
        TerminateProcess(process.hProcess, 1);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return completed && exitCode == 0;
#elif defined(ARP_EDITOR_CLI_EXECUTABLE)
    std::vector<std::string> command{
        ARP_EDITOR_CLI_EXECUTABLE, "--url", std::string(endpoint), "--token", std::string(token)};
    for (const auto argument : arguments)
        command.emplace_back(argument);
    std::vector<char*> argv;
    argv.reserve(command.size() + 1u);
    for (auto& argument : command)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    const auto child = fork();
    if (child < 0)
        return false;
    if (child == 0) {
        execv(ARP_EDITOR_CLI_EXECUTABLE, argv.data());
        _exit(127);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        remote.pump();
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (result < 0)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    return false;
#else
    (void)remote;
    (void)endpoint;
    (void)token;
    (void)arguments;
    return false;
#endif
}
} // namespace

int main()
{
    using namespace arp;
    assert(editor::EditorRemoteServer::isLoopbackAddress("127.0.0.1"));
    assert(!editor::EditorRemoteServer::isLoopbackAddress("0.0.0.0"));
    assert(!editor::EditorRemoteServer::isLoopbackAddress("192.168.1.20"));
    assert(!editor::EditorRemoteServer::isLoopbackAddress("localhost"));
    const auto root = std::filesystem::temp_directory_path()
        / ("arp_editor_remote_"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto script = root / "remote.arp";
    {
        std::ofstream output(script, std::ios::binary);
        output << "let value = 1;\n";
    }
    const auto animatorPrefabId = createAnimatorPackage(root);

    editor::EditorShell shell;
    const auto loaded =
        shell.load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root);
    assert(loaded.ok() && shell.openScriptFile(script));
    physics2d::WorldConfig physicsConfig{
        {0.0f, -9.8f}, 32u, 64u, 16u, 128u, 256u, 128u, 32u, 4u, 1.0f / 60.0f};
    auto physicsWorld = core::makeShared<physics2d::PhysicsWorld2D>(physicsConfig);
    assert(physicsWorld->valid());
    shell.sceneDocument().setPhysicsWorld2DProvider([physicsWorld] {
        return physicsWorld;
    });
    editor::EditorRemoteServer invalidBind;
    constexpr std::string_view token = "loopback-test-token-0123456789-0123456789";
    assert(!invalidBind.start(shell, token, 0, "localhost"));
    editor::EditorRemoteServer missingCredential;
    assert(!missingCredential.start(shell, "short-token", 0));
    editor::EditorRemoteServer nonLoopback;
    assert(!nonLoopback.start(shell, token, 0, "192.168.1.20"));
    assert(!nonLoopback.running());
    assert(nonLoopback.diagnostic().find("requires HTTPS") != std::string::npos);
    editor::EditorRemoteServer remote;
    assert(remote.start(shell, token, 0));
    assert(remote.running() && remote.port() != 0);

    network::SocketRuntime clientRuntime;
    assert(clientRuntime.valid());
    network::io_service clientService;
    network::io_context clientContext(&clientService);
    std::thread clientReactor([&] {
        clientService.run();
    });
    network::http::Client client(&clientContext);

    const auto send = [&](network::http::Method method,
                          std::string_view path,
                          std::string_view body,
                          bool authorized,
                          std::string_view protocolVersion = {}) {
        network::http::ClientRequest request(
            method, "http://127.0.0.1:" + std::to_string(remote.port()) + std::string(path));
        if (authorized)
            assert(request.add_header("Authorization", "Bearer " + std::string(token))
                == network::http::DiagnosticCode::none);
        if (!body.empty()) {
            request.set_content_type(network::http::ContentType::Json);
            request.set_body_text(body);
        }
        assert(request.add_header("Accept", "application/json, text/event-stream")
            == network::http::DiagnosticCode::none);
        if (!protocolVersion.empty())
            assert(request.add_header("MCP-Protocol-Version", protocolVersion)
                == network::http::DiagnosticCode::none);
        bool done = false;
        network::http::ClientStatus resultStatus;
        std::uint16_t statusCode = 0;
        std::string responseBody;
        client.async_send(request,
            [&](const network::http::ClientStatus& status,
                const network::http::Response& response) {
                resultStatus = status;
                statusCode = response.status().value();
                responseBody.assign(response.body_text());
                done = true;
            });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            remote.pump();
            clientContext.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(done && resultStatus.success());
        return std::pair<std::uint16_t, std::string>{statusCode, std::move(responseBody)};
    };

    const auto unauthorized = send(network::http::Method::Get, "/api/v1/discovery", {}, false);
    assert(unauthorized.first == 401);
    const auto mcpUnauthorized = send(network::http::Method::Post, "/mcp", "{}", false);
    assert(mcpUnauthorized.first == 401);
    const auto discovery = send(network::http::Method::Get, "/api/v1/discovery", {}, true);
    assert(discovery.first == 200);
    for (const auto commandName : {"editor.state.get",
             "editor.activate",
             "editor.search",
             "scene.inspector.get",
             "scene.hierarchy.get",
             "scene.component.catalog.get",
             "scene.component.add",
             "scene.component.remove",
             "scene.component.property.set",
             "scene.undo",
             "scene.redo",
             "script.replace",
             "script.save",
             "script.undo",
             "script.redo"})
        assert(discovery.second.find(commandName) != std::string::npos);
    const auto mcpTools = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":40,"method":"tools/list"})json",
        true,
        "2025-11-25");
    assert(mcpTools.first == 200 && mcpTools.second.find("scene.inspector.get") != std::string::npos
        && mcpTools.second.find("scene.hierarchy.get") != std::string::npos
        && mcpTools.second.find("scene.component.catalog.get") != std::string::npos
        && mcpTools.second.find("scene.component.add") != std::string::npos
        && mcpTools.second.find("scene.component.remove") != std::string::npos
        && mcpTools.second.find("scene.component.property.set") != std::string::npos
        && mcpTools.second.find("\"vector2\"") != std::string::npos
        && mcpTools.second.find("\"vector2List\"") != std::string::npos
        && mcpTools.second.find("\"sceneObjectReference\"") != std::string::npos);

    assert(shell.submitCommand(editor::CreateSceneObjectCommand("RemoteInspectorObject")).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("MeshComponent")).status
        == editor::EditorResponseStatus::Completed);
    assert(
        shell.submitCommand(editor::AddSceneComponentCommand("ParticleEmitter2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("AnimatorComponent")).status
        == editor::EditorResponseStatus::Completed);
    physics2d::WorldConfig remotePhysicsConfig{
        {0.0f, -9.8f}, 32, 64, 16, 128, 256, 128, 32, 4, 1.0f / 60.0f};
    auto remotePhysicsWorld = core::makeShared<physics2d::PhysicsWorld2D>(remotePhysicsConfig);
    assert(remotePhysicsWorld->valid());
    shell.sceneDocument().setPhysicsWorld2DProvider([remotePhysicsWorld] {
        return remotePhysicsWorld;
    });
    assert(shell.submitCommand(editor::AddSceneComponentCommand("RigidBody2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("Collider2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* inspectorObject =
        shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    assert(inspectorObject);
    auto* inspectorMesh = inspectorObject->getComponent<scene3d::MeshComponent>();
    auto* inspectorParticleEmitter =
        inspectorObject->getComponent<scene2d::ParticleEmitter2DComponent>();
    auto* inspectorAnimator = inspectorObject->getComponent<scene3d::AnimatorComponent>();
    auto* inspectorCollider = inspectorObject->getComponent<physics2d::Collider2DComponent>();
    assert(inspectorMesh);
    assert(inspectorParticleEmitter);
    assert(inspectorAnimator);
    assert(inspectorCollider && inspectorCollider->handle());
    const auto inspectorObjectId = inspectorObject->id().value();
    const auto inspectorMeshId = inspectorMesh->id().value();
    const auto inspectorParticleEmitterId = inspectorParticleEmitter->id().value();
    const auto inspectorAnimatorId = inspectorAnimator->id().value();
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("RemoteJointBody")).status
        == editor::EditorResponseStatus::Completed);
    const auto jointBodyObjectId = shell.sceneDocument().selectedObject().value();
    assert(shell.submitCommand(editor::AddSceneComponentCommand("RigidBody2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().select(scene::GameObjectId::fromValue(inspectorObjectId)));
    assert(shell.submitCommand(editor::AddSceneComponentCommand("Joint2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    inspectorObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    auto* inspectorJoint = inspectorObject->getComponent<physics2d::Joint2DComponent>();
    assert(inspectorJoint && !inspectorJoint->handle());
    const auto jointComponentId = inspectorJoint->id().value();
    const auto restJointBodyA = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(jointComponentId)
            + ",\"property\":\"bodyA\",\"kind\":\"sceneObjectReference\",\"value\":"
            + std::to_string(inspectorObjectId) + "}",
        true);
    assert(restJointBodyA.first == 200 && !inspectorJoint->handle());
    const auto mcpJointBodyB = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":54,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.property.set\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(jointComponentId)
            + ",\"property\":\"bodyB\",\"kind\":\"sceneObjectReference\",\"value\":"
            + std::to_string(jointBodyObjectId) + "}}}",
        true,
        "2025-11-25");
    assert(mcpJointBodyB.first == 200 && inspectorJoint->handle());
    const auto bodyBObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(jointBodyObjectId));
    const auto bodyBComponentId =
        bodyBObject->getComponent<physics2d::RigidBody2DComponent>()->id();
    const auto bodyRemovalVersion = shell.stateVersion();
    assert(shell
               .submitCommand(
                   editor::RemoveSceneComponentCommand{jointBodyObjectId, bodyBComponentId.value()})
               .status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == bodyRemovalVersion && inspectorJoint->handle());
    const auto mcpJointUndo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":55,"method":"tools/call","params":{"name":"scene.undo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(mcpJointUndo.first == 200
        && mcpJointUndo.second.find("\"isError\":false") != std::string::npos);
    assert(inspectorJoint->bodyBObjectId() == 0 && !inspectorJoint->handle());
    assert(send(network::http::Method::Post,
               "/api/v1/commands",
               R"json({"schemaVersion":1,"command":"scene.redo"})json",
               true)
               .first
        == 200);
    assert(inspectorJoint->handle());
    assert(
        shell
            .submitCommand(editor::RemoveSceneComponentCommand{inspectorObjectId, jointComponentId})
            .status
        == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().select(scene::GameObjectId::fromValue(inspectorObjectId)));
    const auto inspectorProperties = shell.sceneDocument().inspectorProperties(
        scene::GameObjectId::fromValue(inspectorObjectId));
    const auto hierarchyQuery = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.hierarchy.get"})json",
        true);
    assert(hierarchyQuery.first == 200
        && hierarchyQuery.second.find("\"sceneHierarchy\"") != std::string::npos
        && hierarchyQuery.second.find("\"objectId\":" + std::to_string(inspectorObjectId))
            != std::string::npos
        && hierarchyQuery.second.find("\"selectedObjectId\":" + std::to_string(inspectorObjectId))
            != std::string::npos);
    const auto catalogQuery = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.component.catalog.get"})json",
        true);
    assert(catalogQuery.first == 200
        && catalogQuery.second.find("\"sceneComponentCatalog\"") != std::string::npos
        && catalogQuery.second.find("\"typeName\":\"MeshComponent\"") != std::string::npos
        && catalogQuery.second.find("\"typeName\":\"UnknownComponent\"") == std::string::npos);

    const auto beforeMissingAnimationDependency = shell.stateVersion();
    const auto missingAnimationDependency = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId) + ",\"type\":\"SpriteAnimationComponent\"}",
        true);
    assert(missingAnimationDependency.first == 500
        && shell.stateVersion() == beforeMissingAnimationDependency);
    const auto spriteRendererAdd = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId) + ",\"type\":\"SpriteRendererComponent\"}",
        true);
    assert(spriteRendererAdd.first == 200);
    const auto spriteAnimationAdd = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId) + ",\"type\":\"SpriteAnimationComponent\"}",
        true);
    assert(spriteAnimationAdd.first == 200);
    auto* animationObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    assert(animationObject && animationObject->getComponent<scene2d::SpriteRendererComponent>());
    auto* spriteAnimation = animationObject->getComponent<scene2d::SpriteAnimationComponent>();
    assert(spriteAnimation && !spriteAnimation->valid());
    const auto spriteAnimationId = spriteAnimation->id().value();
    const auto animationInspector = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.inspector.get\",\"objectId\":"
            + std::to_string(inspectorObjectId) + "}",
        true);
    assert(animationInspector.first == 200
        && animationInspector.second.find("\"componentId\":" + std::to_string(spriteAnimationId))
            != std::string::npos
        && animationInspector.second.find("\"path\":\"speed\"") != std::string::npos
        && animationInspector.second.find("\"path\":\"eventBudget\"") != std::string::npos);
    const auto animationSpeedSet = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(spriteAnimationId)
            + ",\"property\":\"speed\",\"kind\":\"number\",\"value\":1.75}",
        true);
    assert(animationSpeedSet.first == 200 && spriteAnimation->speed() == 1.75);
    const auto animationUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(animationUndo.first == 200 && spriteAnimation->speed() == 1.0);
    const auto animationRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":48,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(animationRedo.first == 200
        && animationRedo.second.find("\"isError\":false") != std::string::npos
        && spriteAnimation->speed() == 1.75);

    const auto baseColor = std::find_if(
        inspectorProperties.begin(), inspectorProperties.end(), [&](const auto& property) {
            return property.componentId == inspectorMeshId && property.name == "baseColor";
        });
    assert(baseColor != inspectorProperties.end());
    const auto inspectorQuery = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.inspector.get\",\"objectId\":"
            + std::to_string(inspectorObjectId) + "}",
        true);
    assert(inspectorQuery.first == 200
        && inspectorQuery.second.find("\"sceneInspector\"") != std::string::npos
        && inspectorQuery.second.find("\"componentId\":" + std::to_string(inspectorMeshId))
            != std::string::npos
        && inspectorQuery.second.find("\"path\":\"baseColor\"") != std::string::npos
        && inspectorQuery.second.find("\"kind\":\"color4\"") != std::string::npos
        && inspectorQuery.second.find("\"path\":\"velocitySpread\"") != std::string::npos
        && inspectorQuery.second.find("\"kind\":\"vector2\"") != std::string::npos
        && inspectorQuery.second.find("\"resourceType\":\"MeshAssetResource\"") != std::string::npos
        && inspectorQuery.second.find("<None>") != std::string::npos);
    assert(inspectorQuery.second.find("\"componentId\":" + std::to_string(inspectorAnimatorId))
            != std::string::npos
        && inspectorQuery.second.find("\"path\":\"prefabResourceId\"") != std::string::npos
        && inspectorQuery.second.find("\"path\":\"defaultStateName\"") != std::string::npos);
    const auto animatorPrefabSet = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorAnimatorId)
            + ",\"property\":\"prefabResourceId\",\"kind\":\"resourceReference\",\"value\":\""
            + animatorPrefabId + "\"}",
        true);
    assert(animatorPrefabSet.first == 200
        && inspectorAnimator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .prefabResourceId
            == std::string_view(animatorPrefabId)
        && inspectorAnimator->evaluate(scene3d::AnimatorEvaluationRequest{}).ok());
    const auto animatorPrefabUndo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":51,"method":"tools/call","params":{"name":"scene.undo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(animatorPrefabUndo.first == 200
        && animatorPrefabUndo.second.find("\"isError\":false") != std::string::npos);
    assert(inspectorAnimator->editorAuthoringProperties()
            .cast<const scene3d::AnimatorEditorProperties&>()
            .prefabResourceId.empty());
    const auto animatorPrefabRedo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.redo"})json",
        true);
    assert(animatorPrefabRedo.first == 200
        && inspectorAnimator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .prefabResourceId
            == std::string_view(animatorPrefabId)
        && inspectorAnimator->evaluate(scene3d::AnimatorEvaluationRequest{}).ok());
    const auto animatorStateSet = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorAnimatorId)
            + ",\"property\":\"defaultStateName\",\"kind\":\"string\",\"value\":\"walk\"}",
        true);
    assert(animatorStateSet.first == 200
        && inspectorAnimator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .defaultStateName
            == "walk");
    const auto animatorStateMcpSet = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":49,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.property.set\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorAnimatorId)
            + ",\"property\":\"defaultStateName\",\"kind\":\"string\",\"value\":\"idle\"}}}",
        true,
        "2025-11-25");
    assert(animatorStateMcpSet.first == 200
        && animatorStateMcpSet.second.find("\"isError\":false") != std::string::npos
        && inspectorAnimator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .defaultStateName
            == "idle");
    const auto animatorStateUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(animatorStateUndo.first == 200
        && inspectorAnimator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .defaultStateName
            == "walk");
    const auto animatorStateRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":50,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(animatorStateRedo.first == 200
        && inspectorAnimator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .defaultStateName
            == "idle");
    core::pmr::String animatorSceneYaml(&core::defaultCpuMemoryResource());
    core::pmr::String animatorSceneDiagnostic(&core::defaultCpuMemoryResource());
    assert(editor::EditorSceneAuthoringCodec::encode(
        shell.sceneDocument(), animatorSceneYaml, animatorSceneDiagnostic));
    editor::EditorSceneDocument animatorRoundTrip;
    animatorRoundTrip.setResourceManagerProvider([&shell] {
        return &shell.projectResources();
    });
    animatorRoundTrip.setPhysicsWorld2DProvider([physicsWorld] {
        return physicsWorld;
    });
    assert(editor::EditorSceneAuthoringCodec::decode(
        std::string_view(animatorSceneYaml.data(), animatorSceneYaml.size()),
        animatorRoundTrip,
        &isReadyResource,
        &shell.projectResources(),
        animatorSceneDiagnostic));
    auto* roundTripObject =
        animatorRoundTrip.scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    assert(roundTripObject);
    auto* roundTripAnimator = roundTripObject->getComponent<scene3d::AnimatorComponent>();
    assert(roundTripAnimator
        && roundTripAnimator->editorAuthoringProperties()
                .cast<const scene3d::AnimatorEditorProperties&>()
                .defaultStateName
            == "idle"
        && std::string_view(roundTripAnimator->editorAuthoringProperties()
                   .cast<const scene3d::AnimatorEditorProperties&>()
                   .prefabResourceId)
            == animatorPrefabId
        && roundTripAnimator->evaluate(scene3d::AnimatorEvaluationRequest{}).ok());
    const auto beforeRemoteVector2 = shell.stateVersion();
    const auto restVector2Property = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorParticleEmitterId)
            + ",\"property\":\"velocitySpread\",\"kind\":\"vector2\",\"value\":{\"x\":1.25,\"y\":2.5}}",
        true);
    assert(restVector2Property.first == 200 && shell.stateVersion() == beforeRemoteVector2 + 1);
    const auto remoteParticleProperties = inspectorParticleEmitter->editorAuthoringProperties();
    const auto& remoteParticleValues =
        remoteParticleProperties.cast<const scene2d::ParticleEmitter2DEditorProperties&>();
    assert(remoteParticleValues.velocitySpread.x == 1.25f
        && remoteParticleValues.velocitySpread.y == 2.5f);
    const auto propertyCommand =
        [&](std::string_view commandName, float red, bool withIdempotencyKey = false) {
            return std::string("{\"schemaVersion\":1,\"command\":\"") + std::string(commandName)
                + "\",\"objectId\":" + std::to_string(inspectorObjectId)
                + ",\"componentId\":" + std::to_string(inspectorMeshId)
                + ",\"property\":\"baseColor\",\"kind\":\"color4\",\"value\":{\"r\":"
                + std::to_string(red) + ",\"g\":0.25,\"b\":0.5,\"a\":1.0}"
                + (withIdempotencyKey ? ",\"idempotencyKey\":\"remote-color\"" : "") + "}";
        };
    const auto beforeRemoteProperty = shell.stateVersion();
    const auto restProperty = send(network::http::Method::Post,
        "/api/v1/commands",
        propertyCommand("scene.component.property.set", 0.75f, true),
        true);
    assert(restProperty.first == 200 && shell.stateVersion() == beforeRemoteProperty + 1);
    assert(inspectorMesh->editorAuthoringProperties()
               .cast<const scene3d::MeshComponentEditorProperties&>()
               .baseColor.x
        == 0.75f);
    const auto committedPropertyVersion = shell.stateVersion();
    const auto restPropertyReplay = send(network::http::Method::Post,
        "/api/v1/commands",
        propertyCommand("scene.component.property.set", 0.75f, true),
        true);
    assert(restPropertyReplay.first == 200 && shell.stateVersion() == committedPropertyVersion);
    const auto conflictingPropertyReplay = send(network::http::Method::Post,
        "/api/v1/commands",
        propertyCommand("scene.component.property.set", 0.8f, true),
        true);
    assert(
        conflictingPropertyReplay.first == 409 && shell.stateVersion() == committedPropertyVersion);
    const auto beforeRejectedProperty = shell.stateVersion();
    const auto vector2TypeMismatch = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorMeshId)
            + ",\"property\":\"baseColor\",\"kind\":\"vector2\",\"value\":{\"x\":0.1,\"y\":0.2}}",
        true);
    assert(vector2TypeMismatch.first == 500 && shell.stateVersion() == beforeRejectedProperty);
    const auto rejectedProperty = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorMeshId)
            + ",\"property\":\"baseColor\",\"kind\":\"number\",\"value\":0.1}",
        true);
    assert(rejectedProperty.first == 500 && shell.stateVersion() == beforeRejectedProperty);
    const auto malformedProperty = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorMeshId)
            + ",\"property\":\"baseColor\",\"kind\":\"color4\",\"value\":{\"r\":1e100,\"g\":0.25,\"b\":0.5,\"a\":1.0}}",
        true);
    assert(malformedProperty.first == 400 && shell.stateVersion() == beforeRejectedProperty);
    const auto mcpPropertyWrite = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.property.set\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorMeshId)
            + ",\"property\":\"baseColor\",\"kind\":\"color4\",\"value\":{\"r\":0.8,\"g\":0.25,\"b\":0.5,\"a\":1.0}}}}",
        true,
        "2025-11-25");
    assert(mcpPropertyWrite.first == 200
        && mcpPropertyWrite.second.find("\"isError\":false") != std::string::npos
        && inspectorMesh->editorAuthoringProperties()
                .cast<const scene3d::MeshComponentEditorProperties&>()
                .baseColor.x
            == 0.8f);
    const auto restPropertyUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(restPropertyUndo.first == 200);
    const auto mcpPropertyRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":42,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(mcpPropertyRedo.first == 200
        && mcpPropertyRedo.second.find("\"isError\":false") != std::string::npos);
    assert(inspectorMesh->editorAuthoringProperties()
               .cast<const scene3d::MeshComponentEditorProperties&>()
               .baseColor.x
        == 0.8f);
    const auto mcpInspector = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":43,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.inspector.get\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId) + "}}}",
        true,
        "2025-11-25");
    assert(mcpInspector.first == 200
        && mcpInspector.second.find("\"isError\":false") != std::string::npos
        && mcpInspector.second.find("\"r\":0.8") != std::string::npos);
    const auto colliderPointsBody =
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
        + std::to_string(inspectorObjectId)
        + ",\"componentId\":" + std::to_string(inspectorCollider->id().value())
        + ",\"property\":\"points\",\"kind\":\"vector2List\",\"value\":["
          "{\"x\":-1,\"y\":-0.5},{\"x\":1,\"y\":-0.5},{\"x\":0,\"y\":1}],"
          "\"idempotencyKey\":\"remote-collider-points\"}";
    const auto beforeColliderPoints = shell.stateVersion();
    const auto restColliderPoints =
        send(network::http::Method::Post, "/api/v1/commands", colliderPointsBody, true);
    assert(restColliderPoints.first == 200 && shell.stateVersion() == beforeColliderPoints + 1
        && inspectorCollider->desc().points.size() == 3u
        && inspectorCollider->desc().points[2].y == 1.0f);
    const auto colliderPointsVersion = shell.stateVersion();
    const auto replayColliderPoints =
        send(network::http::Method::Post, "/api/v1/commands", colliderPointsBody, true);
    assert(replayColliderPoints.first == 200 && shell.stateVersion() == colliderPointsVersion);
    auto conflictingColliderPointsBody = colliderPointsBody;
    const auto firstPointX = conflictingColliderPointsBody.find("\"x\":-1");
    assert(firstPointX != std::string::npos);
    conflictingColliderPointsBody.replace(firstPointX + 4u, 2u, "-2");
    const auto conflictingColliderPoints =
        send(network::http::Method::Post, "/api/v1/commands", conflictingColliderPointsBody, true);
    assert(conflictingColliderPoints.first == 409 && shell.stateVersion() == colliderPointsVersion
        && inspectorCollider->desc().points[0].x == -1.0f);
    const auto mcpColliderPoints = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":46,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.property.set\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(inspectorCollider->id().value())
            + ",\"property\":\"points\",\"kind\":\"vector2List\",\"value\":[{\"x\":-2,\"y\":-1},{\"x\":2,\"y\":-1},{\"x\":0,\"y\":2}]}}}",
        true,
        "2025-11-25");
    assert(mcpColliderPoints.first == 200
        && mcpColliderPoints.second.find("\"isError\":false") != std::string::npos
        && inspectorCollider->desc().points[0].x == -2.0f);
    const auto colliderPointsUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(colliderPointsUndo.first == 200 && inspectorCollider->desc().points[0].x == -1.0f);
    const auto colliderPointsRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":47,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(colliderPointsRedo.first == 200
        && colliderPointsRedo.second.find("\"isError\":false") != std::string::npos
        && inspectorCollider->desc().points[2].y == 2.0f);
    const auto colliderPointsSnapshot = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.inspector.get\",\"objectId\":"
            + std::to_string(inspectorObjectId) + "}",
        true);
    assert(colliderPointsSnapshot.first == 200
        && colliderPointsSnapshot.second.find("\"kind\":\"vector2List\"") != std::string::npos
        && colliderPointsSnapshot.second.find("\"y\":2") != std::string::npos);
    editor::RemoveSceneComponentCommand removeRemoteCollider;
    removeRemoteCollider.objectId = inspectorObjectId;
    removeRemoteCollider.componentId = inspectorCollider->id().value();
    assert(shell.submitCommand(removeRemoteCollider).status
        == editor::EditorResponseStatus::Completed);
    editor::RemoveSceneComponentCommand removeRemoteBody;
    removeRemoteBody.objectId = inspectorObjectId;
    removeRemoteBody.componentId =
        inspectorObject->getComponent<physics2d::RigidBody2DComponent>()->id().value();
    assert(shell.submitCommand(removeRemoteBody).status == editor::EditorResponseStatus::Completed);
    const auto mcpHierarchy = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":44,"method":"tools/call","params":{"name":"scene.hierarchy.get","arguments":{}}})json",
        true,
        "2025-11-25");
    const auto mcpCatalog = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":45,"method":"tools/call","params":{"name":"scene.component.catalog.get","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(mcpHierarchy.first == 200
        && mcpHierarchy.second.find("\"sceneHierarchy\"") != std::string::npos
        && mcpCatalog.first == 200
        && mcpCatalog.second.find("\"typeName\":\"MeshComponent\"") != std::string::npos);

    const auto beforeRemoteComponentAdd = shell.stateVersion();
    const auto componentAddBody =
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
        + std::to_string(inspectorObjectId)
        + ",\"type\":\"TimerComponent\",\"idempotencyKey\":\"remote-component-add\"}";
    const auto restComponentAdd =
        send(network::http::Method::Post, "/api/v1/commands", componentAddBody, true);
    assert(restComponentAdd.first == 200 && shell.stateVersion() == beforeRemoteComponentAdd + 1);
    auto* remoteAddedObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    assert(remoteAddedObject
        && remoteAddedObject->components().back()->scriptTypeName()
            == std::string_view("TimerComponent"));
    const auto remoteTimerId = remoteAddedObject->components().back()->id().value();
    const auto committedComponentAddVersion = shell.stateVersion();
    const auto replayComponentAdd =
        send(network::http::Method::Post, "/api/v1/commands", componentAddBody, true);
    assert(replayComponentAdd.first == 200 && shell.stateVersion() == committedComponentAddVersion
        && remoteAddedObject->components().back()->id().value() == remoteTimerId);
    const auto conflictingComponentAdd = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"type\":\"TweenComponent\",\"idempotencyKey\":\"remote-component-add\"}",
        true);
    assert(conflictingComponentAdd.first == 409);
    assert(shell.stateVersion() == committedComponentAddVersion);
    const auto mcpComponentRemove = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":46,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.remove\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(remoteTimerId) + "}}}",
        true,
        "2025-11-25");
    assert(mcpComponentRemove.first == 200
        && mcpComponentRemove.second.find("\"isError\":false") != std::string::npos
        && !remoteAddedObject->findComponent(scene::ComponentId::fromValue(remoteTimerId)));
    const auto restComponentUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(restComponentUndo.first == 200
        && remoteAddedObject->components().back()->scriptTypeName()
            == std::string_view("TimerComponent"));
    const auto mcpComponentRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":47,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(mcpComponentRedo.first == 200
        && mcpComponentRedo.second.find("\"isError\":false") != std::string::npos
        && remoteAddedObject->components().back()->scriptTypeName()
            != std::string_view("TimerComponent"));

    const auto configuredAddBody =
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
        + std::to_string(inspectorObjectId)
        + ",\"type\":\"TimerComponent\",\"properties\":[{\"property\":\"eventName\",\"kind\":\"string\",\"value\":\"remote.started\"},{\"property\":\"seconds\",\"kind\":\"number\",\"value\":1.25},{\"property\":\"startOnEnable\",\"kind\":\"boolean\",\"value\":true}],\"idempotencyKey\":\"configured-component-add\"}";
    const auto beforeConfiguredAdd = shell.stateVersion();
    const auto configuredAdd =
        send(network::http::Method::Post, "/api/v1/commands", configuredAddBody, true);
    assert(configuredAdd.first == 200 && shell.stateVersion() == beforeConfiguredAdd + 1);
    remoteAddedObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    auto* configuredTimer = remoteAddedObject->components().back()->scriptTypeName()
            == std::string_view("TimerComponent")
        ? static_cast<scene::TimerComponent*>(remoteAddedObject->components().back().get())
        : nullptr;
    assert(configuredTimer);
    const auto configuredTimerId = configuredTimer->id();
    const auto configuredTimerProperties = shell.sceneDocument().inspectorProperties(
        scene::GameObjectId::fromValue(inspectorObjectId));
    assert(std::any_of(configuredTimerProperties.begin(),
        configuredTimerProperties.end(),
        [&](const auto& property) {
            return property.componentId == configuredTimer->id().value()
                && property.name == "seconds" && property.numberValue == 1.25f;
        }));
    const auto configuredAddReplay =
        send(network::http::Method::Post, "/api/v1/commands", configuredAddBody, true);
    assert(configuredAddReplay.first == 200 && shell.stateVersion() == beforeConfiguredAdd + 1);
    const auto conflictingConfiguredAdd = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"type\":\"TimerComponent\",\"properties\":[{\"property\":\"eventName\",\"kind\":\"string\",\"value\":\"remote.started\"},{\"property\":\"seconds\",\"kind\":\"number\",\"value\":1.5},{\"property\":\"startOnEnable\",\"kind\":\"boolean\",\"value\":true}],\"idempotencyKey\":\"configured-component-add\"}",
        true);
    assert(
        conflictingConfiguredAdd.first == 409 && shell.stateVersion() == beforeConfiguredAdd + 1);
    const auto malformedConfiguredAdd = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"type\":\"MeshComponent\",\"properties\":[{\"property\":\"baseColor\",\"kind\":\"number\",\"value\":0.5}]}",
        true);
    assert(malformedConfiguredAdd.first == 500 && shell.stateVersion() == beforeConfiguredAdd + 1);
    const auto configuredAddUndo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":58,"method":"tools/call","params":{"name":"scene.undo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(configuredAddUndo.first == 200 && !remoteAddedObject->findComponent(configuredTimerId));
    const auto configuredAddRedo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.redo"})json",
        true);
    assert(configuredAddRedo.first == 200);
    remoteAddedObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    configuredTimer = remoteAddedObject->components().back()->scriptTypeName()
            == std::string_view("TimerComponent")
        ? static_cast<scene::TimerComponent*>(remoteAddedObject->components().back().get())
        : nullptr;
    assert(configuredTimer);
    assert(shell
               .submitCommand(editor::RemoveSceneComponentCommand{
                   inspectorObjectId, configuredTimer->id().value()})
               .status
        == editor::EditorResponseStatus::Completed);

    assert(tests::loadSpriteFixture(shell.projectResources())
        && tests::loadTileFixture(shell.projectResources()));
    const auto tileMapAddVersion = shell.stateVersion();
    const auto tileMapAdd = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"type\":\"TileMapResourceComponent\",\"properties\":[{\"property\":\"tileMapResourceId\",\"kind\":\"string\",\"value\":\""
            + tests::tileMapId
            + "\"},{\"property\":\"maxResidentChunks\",\"kind\":\"unsignedInteger\",\"value\":2},{\"property\":\"maxVisibleCells\",\"kind\":\"unsignedInteger\",\"value\":12},{\"property\":\"firstObjectId\",\"kind\":\"unsignedInteger\",\"value\":100}]}",
        true);
    assert(tileMapAdd.first == 200 && shell.stateVersion() == tileMapAddVersion + 1);
    auto* tileMapObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    auto* remoteTileMap =
        tileMapObject ? tileMapObject->getComponent<scene2d::TileMapResourceComponent>() : nullptr;
    assert(
        remoteTileMap && remoteTileMap->runtime() && remoteTileMap->desc().maxVisibleCells == 12u);
    const auto remoteTileMapId = remoteTileMap->id().value();
    const auto mcpTileMapBudget = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":60,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.property.set\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(remoteTileMapId)
            + ",\"property\":\"maxVisibleCells\",\"kind\":\"unsignedInteger\",\"value\":16}}}",
        true,
        "2025-11-25");
    assert(mcpTileMapBudget.first == 200
        && mcpTileMapBudget.second.find("\"isError\":false") != std::string::npos
        && remoteTileMap->runtime() && remoteTileMap->desc().maxVisibleCells == 16u);
    const auto tileMapUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(tileMapUndo.first == 200 && remoteTileMap->desc().maxVisibleCells == 12u);
    const auto tileMapRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":61,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(tileMapRedo.first == 200 && remoteTileMap->desc().maxVisibleCells == 16u);
    const auto mcpTileMapInspector = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":62,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.inspector.get\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId) + "}}}",
        true,
        "2025-11-25");
    assert(mcpTileMapInspector.first == 200
        && mcpTileMapInspector.second.find("TileMapResourceComponent") != std::string::npos
        && mcpTileMapInspector.second.find("maxVisibleCells") != std::string::npos
        && mcpTileMapInspector.second.find("\"value\":16") != std::string::npos
        && mcpTileMapInspector.second.find(tests::tileMapId) != std::string::npos
        && mcpTileMapInspector.second.find("<Unavailable:") == std::string::npos);
    assert(
        shell.submitCommand(editor::RemoveSceneComponentCommand{inspectorObjectId, remoteTileMapId})
            .status
        == editor::EditorResponseStatus::Completed);

    const auto objectLinksAdd = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.add\",\"objectId\":"
            + std::to_string(inspectorObjectId) + ",\"type\":\"ObjectLinksComponent\"}",
        true);
    assert(objectLinksAdd.first == 200);
    inspectorObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    auto* objectLinks =
        inspectorObject ? inspectorObject->getComponent<scene::ObjectLinksComponent>() : nullptr;
    assert(objectLinks && objectLinks->links.empty());
    const auto objectLinksComponentId = objectLinks->id().value();
    const auto objectLinksRestSet = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(objectLinksComponentId)
            + ",\"property\":\"links\",\"kind\":\"sceneObjectLinkList\",\"value\":[{\"name\":\"spawn\",\"target\":"
            + std::to_string(jointBodyObjectId) + "}]}",
        true);
    assert(objectLinksRestSet.first == 200 && objectLinks->links.size() == 1u
        && objectLinks->links.front().name == "spawn"
        && objectLinks->links.front().target.value() == jointBodyObjectId);
    const auto objectLinksMcpSet = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":63,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.property.set\",\"arguments\":{\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(objectLinksComponentId)
            + ",\"property\":\"links\",\"kind\":\"sceneObjectLinkList\",\"value\":[{\"name\":\"home\",\"target\":"
            + std::to_string(jointBodyObjectId) + "}]}}}",
        true,
        "2025-11-25");
    assert(objectLinksMcpSet.first == 200
        && objectLinksMcpSet.second.find("\"isError\":false") != std::string::npos
        && objectLinks->links.size() == 1u && objectLinks->links.front().name == "home");
    const auto objectLinksUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(objectLinksUndo.first == 200 && objectLinks->links.size() == 1u
        && objectLinks->links.front().name == "spawn");
    const auto objectLinksRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":64,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(objectLinksRedo.first == 200 && objectLinks->links.size() == 1u
        && objectLinks->links.front().name == "home");
    const auto beforeInvalidObjectLinks = shell.stateVersion();
    const auto invalidObjectLinks = send(network::http::Method::Post,
        "/api/v1/commands",
        "{\"schemaVersion\":1,\"command\":\"scene.component.property.set\",\"objectId\":"
            + std::to_string(inspectorObjectId)
            + ",\"componentId\":" + std::to_string(objectLinksComponentId)
            + ",\"property\":\"links\",\"kind\":\"sceneObjectLinkList\",\"value\":[{\"name\":\"home\",\"target\":"
            + std::to_string(jointBodyObjectId)
            + "},{\"name\":\"home\",\"target\":" + std::to_string(jointBodyObjectId) + "}]}",
        true);
    if (invalidObjectLinks.first != 200 || shell.stateVersion() != beforeInvalidObjectLinks
        || objectLinks->links.size() != 1u || objectLinks->links.front().name != "home")
        std::fprintf(stderr,
            "duplicate ObjectLinks response=%d state=%llu expected=%llu body=%s\n",
            invalidObjectLinks.first,
            static_cast<unsigned long long>(shell.stateVersion()),
            static_cast<unsigned long long>(beforeInvalidObjectLinks),
            invalidObjectLinks.second.c_str());
    assert(invalidObjectLinks.first == 500 && shell.stateVersion() == beforeInvalidObjectLinks
        && objectLinks->links.size() == 1u && objectLinks->links.front().name == "home");

    assert(shell.submitCommand(editor::CreateSceneObjectCommand("McpConfiguredObject")).status
        == editor::EditorResponseStatus::Completed);
    const auto configuredObjectId = shell.sceneDocument().selectedObject().value();
    const auto mcpConfiguredMeshAdd = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":59,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.component.add\",\"arguments\":{\"objectId\":"
            + std::to_string(configuredObjectId)
            + ",\"type\":\"MeshComponent\",\"properties\":[{\"property\":\"baseColor\",\"kind\":\"color4\",\"value\":{\"r\":0.25,\"g\":0.5,\"b\":0.75,\"a\":1}}]}}}",
        true,
        "2025-11-25");
    assert(mcpConfiguredMeshAdd.first == 200
        && mcpConfiguredMeshAdd.second.find("\"isError\":false") != std::string::npos);
    const auto* mcpConfiguredObject = shell.sceneDocument().scene().findObject(
        scene::GameObjectId::fromValue(configuredObjectId));
    const auto* mcpConfiguredMesh =
        mcpConfiguredObject ? mcpConfiguredObject->getComponent<scene3d::MeshComponent>() : nullptr;
    assert(mcpConfiguredMesh && mcpConfiguredMesh->desc().baseColorTint.x == 0.25f
        && mcpConfiguredMesh->desc().baseColorTint.z == 0.75f);

    const auto documentId = shell.activeScriptDocumentId();
    const auto command = "{\"schemaVersion\":1,\"command\":\"script.replace\",\"documentId\":"
        + std::to_string(documentId)
        + ",\"text\":\"remote over tcp\\n\",\"idempotencyKey\":\"tcp-retry\"}";
    const auto before = shell.stateVersion();
    const auto replaced = send(network::http::Method::Post, "/api/v1/commands", command, true);
    assert(replaced.first == 200 && shell.scriptText() == "remote over tcp\n");
    const auto committedVersion = shell.stateVersion();
    assert(committedVersion == before + 1);
    const auto replay = send(network::http::Method::Post, "/api/v1/commands", command, true);
    assert(replay.first == 200 && shell.stateVersion() == committedVersion);

    assert(shell.submitCommand(editor::CreateSceneObjectCommand("RemoteHistoryObject")).status
        == editor::EditorResponseStatus::Completed);
    auto* remoteHistoryObject =
        shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    assert(remoteHistoryObject);
    editor::SetSceneObjectTransformCommand remoteTransform;
    remoteTransform.objectId = remoteHistoryObject->id().value();
    remoteTransform.translation = {3.0f, 4.0f, 5.0f};
    assert(shell.submitCommand(remoteTransform).status == editor::EditorResponseStatus::Completed);
    const auto remoteObjectId = remoteHistoryObject->id();
    const auto restUndo = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.undo"})json",
        true);
    assert(restUndo.first == 200);
    remoteHistoryObject = shell.sceneDocument().scene().findObject(remoteObjectId);
    assert(remoteHistoryObject
        && remoteHistoryObject->transform().localTransform().translation.x == 0.0f
        && remoteHistoryObject->transform().localTransform().translation.y == 0.0f);
    const auto mcpRedo = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":41,"method":"tools/call","params":{"name":"scene.redo","arguments":{}}})json",
        true,
        "2025-11-25");
    assert(mcpRedo.first == 200 && mcpRedo.second.find("\"isError\":false") != std::string::npos);
    remoteHistoryObject = shell.sceneDocument().scene().findObject(remoteObjectId);
    assert(remoteHistoryObject
        && remoteHistoryObject->transform().localTransform().translation.x == 3.0f
        && remoteHistoryObject->transform().localTransform().translation.y == 4.0f);

    const auto cliEndpoint = "http://127.0.0.1:" + std::to_string(remote.port());
    const auto beforeCliState = shell.stateVersion();
    assert(runEditorCli(remote, cliEndpoint, token, {"state"}));
    assert(shell.stateVersion() == beforeCliState);
    constexpr std::string_view cliText = "edited through arp_editor_cli\n";
    const auto documentIdText = std::to_string(documentId);
    const std::string cliTextArgument(cliText);
    assert(runEditorCli(
        remote, cliEndpoint, token, {"script.replace", documentIdText, cliTextArgument}));
    assert(shell.scriptText() == cliText);
    const auto inspectorObjectIdText = std::to_string(inspectorObjectId);
    const auto inspectorMeshIdText = std::to_string(inspectorMeshId);
    assert(
        runEditorCli(remote, cliEndpoint, token, {"scene.inspector.get", inspectorObjectIdText}));
    assert(runEditorCli(remote, cliEndpoint, token, {"scene.hierarchy.get"}));
    assert(runEditorCli(remote, cliEndpoint, token, {"scene.component.catalog.get"}));
    assert(runEditorCli(remote,
        cliEndpoint,
        token,
        {"scene.component.add",
            inspectorObjectIdText,
            "TimerComponent",
            "[{\"property\":\"eventName\",\"kind\":\"string\",\"value\":\"cli.started\"},{\"property\":\"seconds\",\"kind\":\"number\",\"value\":2}]"}));
    auto* cliComponentObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    auto* cliConfiguredTimer = cliComponentObject->components().back()->scriptTypeName()
            == std::string_view("TimerComponent")
        ? static_cast<scene::TimerComponent*>(cliComponentObject->components().back().get())
        : nullptr;
    assert(cliConfiguredTimer);
    const auto cliConfiguredTimerId = cliConfiguredTimer->id();
    assert(shell
               .submitCommand(editor::RemoveSceneComponentCommand{
                   inspectorObjectId, cliConfiguredTimerId.value()})
               .status
        == editor::EditorResponseStatus::Completed);
    assert(runEditorCli(remote,
        cliEndpoint,
        token,
        {"scene.component.add", inspectorObjectIdText, "AudioListenerComponent"}));
    cliComponentObject =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(inspectorObjectId));
    assert(cliComponentObject
        && cliComponentObject->components().back()->scriptTypeName()
            == std::string_view("AudioListenerComponent"));
    const auto cliAudioListenerId =
        std::to_string(cliComponentObject->components().back()->id().value());
    assert(runEditorCli(remote,
        cliEndpoint,
        token,
        {"scene.component.remove", inspectorObjectIdText, std::string_view(cliAudioListenerId)}));
    assert(!cliComponentObject->findComponent(
        scene::ComponentId::fromValue(std::stoull(cliAudioListenerId))));
    const auto beforeCliProperty = shell.stateVersion();
    assert(runEditorCli(remote,
        cliEndpoint,
        token,
        {"scene.component.property.set",
            inspectorObjectIdText,
            inspectorMeshIdText,
            "baseColor",
            "color4",
            R"json({"r":0.2,"g":0.3,"b":0.4,"a":1.0})json"}));
    assert(shell.stateVersion() == beforeCliProperty + 1
        && inspectorMesh->editorAuthoringProperties()
                .cast<const scene3d::MeshComponentEditorProperties&>()
                .baseColor.x
            == 0.2f);
    assert(runEditorCli(remote, cliEndpoint, token, {"scene.undo"}));
    assert(inspectorMesh->editorAuthoringProperties()
               .cast<const scene3d::MeshComponentEditorProperties&>()
               .baseColor.x
        == 0.8f);
    assert(runEditorCli(remote, cliEndpoint, token, {"scene.redo"}));
    assert(inspectorMesh->editorAuthoringProperties()
               .cast<const scene3d::MeshComponentEditorProperties&>()
               .baseColor.x
        == 0.2f);
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("CliReparentParent")).status
        == editor::EditorResponseStatus::Completed);
    const auto parentObjectId = shell.sceneDocument().selectedObject().value();
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("CliReparentChild")).status
        == editor::EditorResponseStatus::Completed);
    const auto childObjectId = shell.sceneDocument().selectedObject().value();
    assert(runEditorCli(remote,
        cliEndpoint,
        token,
        {"scene.reparent", std::to_string(childObjectId), std::to_string(parentObjectId), "0"}));
    const auto* reparentedChild =
        shell.sceneDocument().scene().findObject(scene::GameObjectId::fromValue(childObjectId));
    assert(reparentedChild && reparentedChild->parentId().value() == parentObjectId);

    const auto mcpInvalid = send(network::http::Method::Post, "/mcp", "{}", true);
    assert(mcpInvalid.first == 400);
    const auto mcpUnknownMethod = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":17,"method":"unknown/method"})json",
        true,
        "2025-11-25");
    assert(mcpUnknownMethod.first == 200
        && mcpUnknownMethod.second.find("-32601") != std::string::npos);
    const auto mcpGetUnsupported = send(network::http::Method::Get, "/mcp", {}, true);
    assert(mcpGetUnsupported.first == 405);

    const auto taskIdFrom = [](std::string_view body) {
        const auto key = body.find("\"taskId\":");
        assert(key != std::string_view::npos);
        const auto start = key + sizeof("\"taskId\":") - 1u;
        const auto end = body.find_first_not_of("0123456789", start);
        return std::stoull(std::string(body.substr(start, end - start)));
    };
    const auto finishTask = [&](std::uint64_t taskId) {
        std::string result;
        for (std::size_t attempt = 0; attempt < 1000; ++attempt) {
            (void)shell.scenario();
            const auto query = send(network::http::Method::Post,
                "/api/v1/commands",
                "{\"schemaVersion\":1,\"command\":\"scene.task.get\",\"taskId\":"
                    + std::to_string(taskId) + "}",
                true);
            assert(query.first == 200);
            result = query.second;
            if (result.find("\"status\":\"succeeded\"") != std::string::npos
                || result.find("\"status\":\"failed\"") != std::string::npos)
                return result;
        }
        assert(false && "scene persistence task did not reach a terminal status");
        return result;
    };

    const auto restSave = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.save","path":"remote-scene.arp.yaml","idempotencyKey":"scene-save-retry"})json",
        true);
    assert(restSave.first == 200
        && restSave.second.find("\"status\":\"pending\"") != std::string::npos);
    const auto restSaveTaskId = taskIdFrom(restSave.second);
    const auto restSaveReplay = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.save","path":"remote-scene.arp.yaml","idempotencyKey":"scene-save-retry"})json",
        true);
    assert(restSaveReplay.first == 200 && taskIdFrom(restSaveReplay.second) == restSaveTaskId);
    const auto restSaveResult = finishTask(restSaveTaskId);
    assert(restSaveResult.find("\"status\":\"succeeded\"") != std::string::npos);
    assert(std::filesystem::exists(root / "remote-scene.arp.yaml"));

    const auto preservedObjectId = shell.sceneDocument().selectedObject();
    auto* preservedObject = shell.sceneDocument().scene().findObject(preservedObjectId);
    assert(preservedObject);
    const auto preservedName =
        std::string(preservedObject->name().data(), preservedObject->name().size());
    const auto preservedObjectCount = shell.sceneDocument().scene().objects().size();
    auto invalidScenePath = root / "invalid-scene.arp.yaml";
    {
        std::ofstream invalidScene(invalidScenePath, std::ios::binary | std::ios::trunc);
        invalidScene << "type: arp.editor-scene\ncontractVersion: 99\n";
    }
    const auto mcpLoad = send(network::http::Method::Post,
        "/mcp",
        R"json({"jsonrpc":"2.0","id":52,"method":"tools/call","params":{"name":"scene.load","arguments":{"path":"remote-scene.arp.yaml"}}})json",
        true,
        "2025-11-25");
    assert(mcpLoad.first == 200 && mcpLoad.second.find("\"isError\":false") != std::string::npos);
    const auto loadTaskId = taskIdFrom(mcpLoad.second);
    const auto mcpTaskQuery = send(network::http::Method::Post,
        "/mcp",
        "{\"jsonrpc\":\"2.0\",\"id\":53,\"method\":\"tools/call\",\"params\":{\"name\":\"scene.task.get\",\"arguments\":{\"taskId\":"
            + std::to_string(loadTaskId) + "}}}",
        true,
        "2025-11-25");
    assert(mcpTaskQuery.first == 200);
    assert(mcpTaskQuery.second.find("scenePersistenceTask") != std::string::npos);
    const auto loadResult = finishTask(loadTaskId);
    assert(loadResult.find("\"status\":\"succeeded\"") != std::string::npos);
    bool loadedRigidBodyBoundToRemoteWorld = false;
    for (const auto& object : shell.sceneDocument().scene().objects()) {
        const auto* body = object->getComponent<physics2d::RigidBody2DComponent>();
        loadedRigidBodyBoundToRemoteWorld = loadedRigidBodyBoundToRemoteWorld
            || (body && body->handle() && body->world() == remotePhysicsWorld.get());
    }
    assert(loadedRigidBodyBoundToRemoteWorld);

    const auto failedLoad = send(network::http::Method::Post,
        "/api/v1/commands",
        R"json({"schemaVersion":1,"command":"scene.load","path":"invalid-scene.arp.yaml"})json",
        true);
    assert(failedLoad.first == 200);
    const auto failedLoadResult = finishTask(taskIdFrom(failedLoad.second));
    assert(failedLoadResult.find("\"status\":\"failed\"") != std::string::npos);
    assert(shell.sceneDocument().scene().objects().size() == preservedObjectCount);
    preservedObject = shell.sceneDocument().scene().findObject(preservedObjectId);
    assert(preservedObject
        && std::string_view(preservedObject->name().data(), preservedObject->name().size())
            == preservedName);

#if defined(_WIN32)
    const auto* mcpClientModule = std::getenv("ARP_MCP_CLIENT_MODULE");
    if (mcpClientModule && *mcpClientModule) {
        const auto endpoint = "http://127.0.0.1:" + std::to_string(remote.port()) + "/mcp";
        assert(runMcpSdkClient(remote, endpoint, token, documentId));
        assert(shell.scriptText() == "edited through MCP client SDK\n");
    }
#endif

    remote.stop();
    clientContext.shutdown();
    clientReactor.join();
    clientContext.dispatch();
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    assert(!cleanupError);
    return 0;
}
