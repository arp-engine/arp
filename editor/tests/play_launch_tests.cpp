#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/app/game_preview.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;

// The same executable can act as a short-lived process fixture. It records the
// exact arguments and exits normally; it never creates a game or GPU workload.
int child(int argc, char** argv)
{
    assert(argc == 9 && std::string_view(argv[1]) == "--resource-root");
    std::ofstream output(std::filesystem::u8path(argv[2]) / "preview-child.txt", std::ios::binary);
    for (int i = 1; i < argc; ++i)
        output << argv[i] << '\n';
    return output ? 0 : 3;
}

class LaunchFixture final {
public:
    LaunchFixture()
    {
        root = std::filesystem::path(ARP_SOURCE_DIR) / "build"
            / ("play-launch-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "resource root");
        root = std::filesystem::canonical(root);
    }

    ~LaunchFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        if (error)
            std::fprintf(stderr,
                "Fixture cleanup failed: %s at %s\n",
                error.message().c_str(),
                root.string().c_str());
        assert(!error);
    }

    static void initialize(EditorShell& shell, const std::filesystem::path& root)
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
                .ok());
    }

    static core::pmr::String document(EditorShell& shell)
    {
        core::pmr::String text, diagnostic;
        assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), text, diagnostic));
        return text;
    }

    static const core::pmr::String& status(EditorShell& shell)
    {
        return shell.runtime().canvas().findWidget(shell.id("output_status"))->desc().text;
    }

    static void visibleFeedback(EditorShell& shell)
    {
        assert(shell.runtime().canvas().findWidget(shell.id("output_panel"))->visible());
        // Native viewport composition is appended after ordinary controls.
        // Output must remain above it for both CPU and external-image paths.
        for (const auto texture :
            {std::string_view{}, std::string_view{"arp.editor.scene-viewport"}}) {
            const auto drawn = shell.drawCommands(texture);
            assert(drawn.ok());
            std::size_t viewport = drawn.commands().size(), output = drawn.commands().size();
            for (std::size_t index = 0; index < drawn.commands().size(); ++index) {
                const auto& command = drawn.commands()[index];
                if (command.widgetId == shell.id("viewport"))
                    viewport = index;
                if (command.widgetId == shell.id("output_status")
                    && command.kind == renderer::ui::UiDrawCommandKind::Text) {
                    output = index;
                    assert(command.text == status(shell));
                    assert(command.clipRect.width > 0 && command.clipRect.height > 0);
                }
            }
            assert(viewport < output && output < drawn.commands().size());
        }
    }

    static void request(EditorShell& shell, GamePreviewHost& host, std::string_view diagnostic)
    {
        shell.activate("run_play");
        assert(host.synchronize(shell, 1440, 810, false));
        assert(!shell.gamePreviewRunning() && status(shell).find(diagnostic) == 0);
        assert(status(shell).find_first_of("\r\n") == core::pmr::String::npos);
        visibleFeedback(shell);
        assert(!host.synchronize(shell, 1440, 810, false));
        // Stop must observe Idle, rather than enqueue a cancellation for a
        // stale starting state. Repeated callers also exercise retry below.
        assert(shell.consumeGamePreviewCommand() == GamePreviewModel::Request::None);
        shell.activate("run_stop");
        assert(status(shell) == "Game is not running.");
        assert(shell.consumeGamePreviewCommand() == GamePreviewModel::Request::None);
    }

    std::filesystem::path root;
};

void contract(LaunchFixture& fixture, const std::filesystem::path& executable)
{
    core::pmr::String diagnostic;
    const auto root = fixture.root / "resource root";
    const auto marker = root / "preview-child.txt";
    // A real executable at the old implicit location must never be launched by
    // default. Its process fixture would leave a marker if it were executed.
#if defined(_WIN32)
    const auto decoy = root / "build/win-auto-release/arp_world_of_claudecraft.exe";
#else
    const auto decoy = root / "build/arp_world_of_claudecraft";
#endif
    std::filesystem::create_directories(decoy.parent_path());
    std::filesystem::copy_file(executable, decoy);
    EditorShell shell;
    LaunchFixture::initialize(shell, root);
    shell.activate("workspace_3D");
    const auto before = LaunchFixture::document(shell);
    const auto dirty = shell.sceneDirty();
    GamePreviewHost generic;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        LaunchFixture::request(shell, generic, "EDITOR-PLAYER-NOT-IMPLEMENTED");
        assert(LaunchFixture::document(shell) == before && shell.sceneDirty() == dirty);
    }
    assert(!std::filesystem::exists(marker) && !generic.requestStop());
    assert(generic.update(nullptr, 0, 0, false) == GamePreviewHost::Update::Idle);

    GamePreviewHost relative("relative-game.exe"), missing(root / "missing.exe"), directory(root);
    for (auto* invalid : {&relative, &missing, &directory}) {
        assert(!invalid->start(root, diagnostic));
        assert(diagnostic.find("EDITOR-LEGACY-PREVIEW-EXECUTABLE") == 0);
        assert(!std::filesystem::exists(marker));
    }
    GamePreviewHost explicitLegacy(executable);
    assert(!explicitLegacy.start("relative-root", diagnostic));
    assert(diagnostic.find("EDITOR-LEGACY-PREVIEW-ROOT") == 0);

    ProjectManifest manifest;
    ProjectDiagnostic projectDiagnostic;
    assert(ProjectManifestCodec::decode(
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"),
        manifest,
        projectDiagnostic));
    assert(shell
               .submitCommand(CreateProjectCommand{core::FilePath::utf8(fixture.root / "project"),
                   manifest,
                   false,
                   ProjectTemplateKind::Empty})
               .status
        == EditorResponseStatus::Completed);
    assert(shell.submitCommand(CreateSceneObjectCommand("unsaved object")).status
        == EditorResponseStatus::Completed);
    SetSceneObjectTransformCommand move;
    move.translation = {3.0f, 2.0f, 1.0f};
    assert(shell.submitCommand(move).status == EditorResponseStatus::Completed);
    const auto projectBefore = LaunchFixture::document(shell);
    const auto manifestBefore = core::readTextFile(shell.projectRoot() / "project.arp.yaml").text;
    LaunchFixture::request(shell, explicitLegacy, "EDITOR-LEGACY-PREVIEW-PROJECT-CONFLICT");
    LaunchFixture::request(shell, generic, "EDITOR-PLAYER-NOT-IMPLEMENTED");
    assert(LaunchFixture::document(shell) == projectBefore && shell.sceneDirty());
    assert(core::readTextFile(shell.projectRoot() / "project.arp.yaml").text == manifestBefore);
    assert(!std::filesystem::exists(shell.projectRoot() / "preview-child.txt"));
    EditorRemoteProtocol protocol("play-launch-token");
    const auto remote = [&](std::string_view body, bool mcp = false) {
        network::http::Request request;
        request.set_method(network::http::Method::Post);
        request.set_target(mcp ? "/mcp" : "/api/v1/commands");
        request.add_header("Authorization", "Bearer play-launch-token");
        request.add_header("Accept", "application/json");
        request.add_header("MCP-Protocol-Version", "2025-11-25");
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        return protocol.handle(request, shell);
    };
    assert(remote(R"({"schemaVersion":1,"command":"editor.activate","source":"run_play"})")
               .status()
               .value()
        == 200);
    assert(generic.synchronize(shell, 1440, 810, false));
    assert(LaunchFixture::status(shell).find("EDITOR-PLAYER-NOT-IMPLEMENTED") == 0);
    assert(
        remote(
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor.activate","arguments":{"source":"run_stop"}}})",
            true)
            .body_text()
            .find("\"isError\":false")
        != std::string_view::npos);
    assert(remote(R"({"schemaVersion":1,"command":"editor.activate","source":"run_pause"})")
               .status()
               .value()
        != 200);
    assert(LaunchFixture::document(shell) == projectBefore);
    assert(shell.submitCommand(UndoSceneCommand{}).status == EditorResponseStatus::Completed);
    const auto* restored = shell.sceneDocument().scene().findObjectByName("unsaved object");
    assert(restored && restored->transform().localTransform().translation.x == 0.0f);
    std::puts(
        "PASS Play launch boundary: no implicit WOCC execution, explicit executable/root gates, project conflict, REST/MCP Start/Stop actions, retry and exact draft/manifest preservation");
}

void processAndLifetime(LaunchFixture& fixture, const std::filesystem::path& executable)
{
    const auto root = fixture.root / "resource root";
    const auto marker = root / "preview-child.txt";
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            LaunchFixture::initialize(shell, root);
            const auto before = LaunchFixture::document(shell);
            GamePreviewHost generic;
            LaunchFixture::request(shell, generic, "EDITOR-PLAYER-NOT-IMPLEMENTED");
            assert(LaunchFixture::document(shell) == before);
            std::filesystem::remove(marker);
            GamePreviewHost explicitLegacy(executable);
            core::pmr::String diagnostic;
            // Trailing separator and spaces must remain one exact argument.
            assert(explicitLegacy.start(root / "", diagnostic));
            assert(diagnostic.empty());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            auto result = GamePreviewHost::Update::Starting;
            do {
                result = explicitLegacy.update(nullptr, 0, 0, false);
                std::this_thread::yield();
            } while (result != GamePreviewHost::Update::Exited
                && std::chrono::steady_clock::now() < deadline);
            assert(result == GamePreviewHost::Update::Exited);
            assert(explicitLegacy.update(nullptr, 0, 0, false) == GamePreviewHost::Update::Idle);
            assert(!explicitLegacy.requestStop());
            const auto observed = core::readTextFile(marker);
            assert(observed.success);
            core::pmr::String expected("--resource-root\n");
            expected += core::FilePath::utf8(std::filesystem::canonical(root));
#if defined(_WIN32)
            // Native argv uses Windows path separators; compare semantic root
            // separately and the exact remaining option bytes below.
            const auto end = observed.text.find('\n', 16);
            assert(std::filesystem::path(observed.text.substr(16, end - 16))
                == std::filesystem::canonical(root));
            assert(observed.text.substr(end)
                == "\n--rhi-backend\nvulkan\n--width\n1280\n--height\n720\n");
#elif defined(__APPLE__)
            expected += "\n--rhi-backend\nmetal\n--width\n1280\n--height\n720\n";
            assert(observed.text == expected);
#else
            expected += "\n--rhi-backend\nvulkan\n--width\n1280\n--height\n720\n";
            assert(observed.text == expected);
#endif
        }
        const auto stats = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = stats;
        assert(stats.liveBytes == baseline.liveBytes
            && stats.allocationCount - stats.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS native process fixture and 20 warmed Play rejection/explicit Legacy spawn/normal exit/destroy cycles; core live bytes/count delta = 0; no game/Interpreter readiness claim");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--resource-root")
        return child(argc, argv);
    LaunchFixture fixture;
    const auto executable = std::filesystem::canonical(std::filesystem::u8path(argv[0]));
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        processAndLifetime(fixture, executable);
    else
        contract(fixture, executable);
}
