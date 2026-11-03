#include "core/diagnostics/diagnostic_error.h"
#include "core/io/filesystem_path.h"
#include "editor/app/editor_project_source_host.h"
#include "editor/app/editor_web_startup_host.h"
#include "editor/app/editor_workspace_connection_profile.h"
#include "editor/ingress/editor_platform_input_router.h"
#include "editor/ingress/platform_input_adapter.h"
#include "editor/remote/editor_remote_protocol.h"
#include "editor/ui/editor_ui_assets.h"
#include "editor/validation/editor_viewport_resource_smoke.h"
#include "editor/viewport/editor_scene_viewport_composite.h"
#include "editor/viewport/editor_scene_viewport_frame_builder.h"
#include "editor/viewport/editor_scene_viewport_ordered_renderer.h"
#include "editor/web/browser_rendering.h"
#include "editor/web/browser_workspace.h"
#include "editor/workspace/editor_shell.h"
#include "input/input.h"
#include "platform/window.h"
#include "renderer/gui/gui_ordered_renderer.h"
#include "tests/fixtures/ordered_graphics_catalog_fixture.h"

#include <array>
#include <cmath>
#include <emscripten.h>
#include <emscripten/html5.h>

// clang-format off
EM_JS(void, editorPublish, (int frames, int draws, int objects, int dirty, int busy,
    int scriptDirty, int connected, int writable, const char* message), {
    const state = Module.editorState || (Module.editorState = {});
    Object.assign(state, {frames, draws, objects, dirty: !!dirty, busy: !!busy,
        scriptDirty: !!scriptDirty, message: UTF8ToString(message), backend: 'WebGL1',
        gpuCompletion: false, connectedWorkspace: !!connected,
        workspaceReadOnly: !!connected && !writable, persistent: !!connected && !!writable});
    if (Module.onEditorState) Module.onEditorState(state);
});
EM_JS(void, editorFatal, (const char* message), {
    Module.editorError = UTF8ToString(message);
    if (Module.onEditorError) Module.onEditorError(Module.editorError);
    console.error(Module.editorError);
});
EM_JS(void, editorDownload, (const char* text), {
    Module.editorLastExport = UTF8ToString(text);
    if (Module.downloadWorkspace) Module.downloadWorkspace(Module.editorLastExport);
});
EM_JS(int, editorPageAvailable, (), { return Module.editorPageAvailable === false ? 0 : 1; });
EM_JS(int, editorViewportValidationRequested, (), {
    return new URLSearchParams(location.search).has('viewport-smoke') ? 1 : 0;
});
EM_JS(void, editorCatalogValidationPassed, (), { Module.editorCatalogValidated = true; });
EM_JS(int, editorStartupBaseUrl, (int role, char* output, int capacity), {
    const paths=['editor-sources/ui/','editor-sources/fonts/','editor-sources/shaders/'];
    if(role<0||role>=paths.length)return 0;
    const value=new URL(paths[role],location.href).href;
    if(lengthBytesUTF8(value)>=capacity)return 0;
    stringToUTF8(value,output,capacity);return 1;
});
// clang-format on

namespace arp::editor {
namespace {
class BrowserEditor final : public platform::OrderedGraphicsFrameWorkload {
public:
    ~BrowserEditor() override
    {
        candidate_.reset();
        shell_.reset();
    }

    void create(core::SharedPtr<const EditorWebStartupSources>);
    void tick();
    bool action(core::StringView, core::StringView);
    bool importWorkspace(core::StringView);
    bool connectWorkspace(core::StringView, core::StringView);
    const char* remoteRequest(core::StringView method,
        core::StringView path,
        core::StringView body,
        core::StringView session,
        core::StringView accept,
        core::StringView protocolVersion);

    int remoteStatus() const noexcept { return remoteStatus_; }

    rhi::MemoryStats gpuMemory() const { return system_.memoryStats(); }

    const char* scriptText() const { return shell_->scriptText().c_str(); }

    core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines() const noexcept override
    {
        if (!viewportVisible_)
            return {&pipelines_[3], 1};
        return viewport_.hasGeometry() || viewport_.hasSprites()
            ? core::ArrayView<std::shared_ptr<rhi::RenderPipeline>>(pipelines_)
            : core::ArrayView<std::shared_ptr<rhi::RenderPipeline>>(&pipelines_[2], 2);
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle target) override
    {
        if (viewportVisible_)
            viewport_.addPasses(graph, shell_->sceneViewportCamera(), viewportSky_);
        core::requireSuccessfulResult(gui_->addPass(graph,
                                          target,
                                          commands_,
                                          "web.solid",
                                          frames_,
                                          rhi::LoadOp::Clear,
                                          {.08f, .08f, .08f, 1}),
            "EDITOR-WEBGL-DRAW");
    }

private:
    core::UniquePtr<EditorShell> loadShell(const std::filesystem::path& root);
    bool restoreCandidatePresentation();
    bool acceptsWorkspaceReplacement() const;
    void advanceSourceCandidate();
    void discardSourceCandidate(std::string_view);
    static constexpr std::size_t maxConnectionBytes = 64 * 1024;
    static constexpr std::size_t maxConnectionMessageBytes = 4096;

    EditorResponse command(EditorCommandPayload value)
    {
        return shell_->submitCommand(std::move(value));
    }

    core::SharedPtr<const EditorWebStartupSources> startupSources_;
    platform::WindowSystem windows_;
    platform::PlatformWindow window_;
    input::InputSystem input_{{}};
    // Both hosts outlive their Shell and all private file callbacks.
    core::UniquePtr<EditorProjectSourceHost> sourceHost_, candidateSourceHost_;
    core::UniquePtr<EditorWorkspaceConnectionProfile> candidateConnection_;
    core::UniquePtr<EditorShell> shell_, candidate_;
    std::optional<BrowserWorkspace::Staged> staged_, workspace_;
    std::optional<EditorUiAssets> assets_;
    platform::RenderSystem system_;
    std::array<std::shared_ptr<rhi::RenderPipeline>, 4> pipelines_;
    EditorSceneViewportOrderedRenderer viewport_;
    EditorSceneViewportSky viewportSky_;
    bool viewportVisible_ = false;
    core::UniquePtr<renderer::GuiOrderedRenderer> gui_;
    core::pmr::Vector<renderer::ui::UiDrawCommand> commands_;
    core::pmr::String message_{"Virtual workspace: download an export to keep your work"};
    std::uint64_t frames_ = 0, importSerial_ = 0;
    std::uint64_t exportTaskId_ = 0, candidateTaskId_ = 0;
    int width_ = 1440, height_ = 810;
    bool exportPending_ = false;
    bool sourceOpening_ = false;
    core::UniquePtr<EditorRemoteProtocol> remote_;
    core::pmr::String remoteSession_, remoteResponse_;
    int remoteStatus_ = 503;
};

core::UniquePtr<BrowserEditor> app;
core::UniquePtr<EditorWebStartupHost> startup;

const char* BrowserEditor::remoteRequest(core::StringView method,
    core::StringView path,
    core::StringView body,
    core::StringView session,
    core::StringView accept,
    core::StringView protocolVersion)
{
    // Only the browser owner thread invokes this synchronous adapter. The same
    // protocol owns parsing, idempotency and dispatch for desktop and Web.
    remoteStatus_ = 503;
    remoteResponse_ = R"({"error":"web_workspace_busy"})";
    if (candidate_ || exportPending_)
        return remoteResponse_.c_str();
    if (session.size() < 32 || session.size() > 256
        || body.size() > EditorRemoteProtocol::maxRequestBytes
        || (method != "GET" && method != "POST")
        || (path != "/api/v1/commands" && path != "/api/v1/discovery" && path != "/mcp")) {
        remoteStatus_ = 400;
        remoteResponse_ = R"({"error":"web_bridge_invalid_request"})";
        return remoteResponse_.c_str();
    }
    if (remoteSession_ != session) {
        auto protocol = core::makeUnique<EditorRemoteProtocol>(session);
        remoteSession_ = session;
        remote_ = std::move(protocol);
    }
    network::http::Request request;
    request.set_method(method == "GET" ? network::http::Method::Get : network::http::Method::Post);
    request.set_target(path);
    request.set_content_type(network::http::ContentType::Json);
    core::pmr::String authorization("Bearer ");
    authorization += session;
    request.add_header("Authorization", authorization);
    if (!accept.empty())
        request.add_header("Accept", accept);
    if (!protocolVersion.empty())
        request.add_header("MCP-Protocol-Version", protocolVersion);
    request.set_body_text(body);
    const auto response = remote_->handle(request, *shell_);
    remoteResponse_ = response.body_text();
    remoteStatus_ = response.status().value();
    return remoteResponse_.c_str();
}

core::UniquePtr<EditorShell> BrowserEditor::loadShell(const std::filesystem::path& root)
{
    auto shell = core::makeUnique<EditorShell>();
    const auto loaded = shell->loadPreparedUi(startupSources_->ui().document(), root);
    if (!loaded.ok())
        throw core::DiagnosticError(loaded.diagnostic().message);
    if (assets_)
        assets_->applyTextMetrics(*shell);
    if (shell->submitCommand(ActivateControlCommand("workspace_3D")).status
        != EditorResponseStatus::Completed)
        throw core::DiagnosticError("EDITOR-WEB-WORKSPACE-INITIALIZATION");

    return shell;
}

void BrowserEditor::create(core::SharedPtr<const EditorWebStartupSources> sources)
{
    if (!sources)
        throw core::DiagnosticError("EDITOR-WEB-STARTUP-OWNER-MISSING");
    startupSources_ = std::move(sources);
    message_.reserve(maxConnectionMessageBytes);
    core::requireSuccessfulResult(windows_.initialize(), "EDITOR-WEB-WINDOW");
    platform::WindowDesc window;
    window.title = "ARP Editor WebGL";
    window.width = width_;
    window.height = height_;
    window_ = windows_.createWindow(window);
    if (!window_.valid())
        throw core::DiagnosticError("EDITOR-WEB-WINDOW-CREATE");
    shell_ = loadShell("/workspace/default");
    if (editorViewportValidationRequested()) {
        core::pmr::String diagnostic;
        if (!validation::installViewportSmokeScene(
                *shell_, diagnostic, "/app/tests/fixtures/scene_2d/"))
            throw core::DiagnosticError(diagnostic);
    }
    auto authored = shell_->drawCommands();
    if (!authored.ok())
        throw core::DiagnosticError(authored.diagnostics());
    assets_.emplace(
        EditorUiAssets::buildPrepared(*shell_, startupSources_->ui(), authored.commands()));
    if (!assets_->ok())
        throw core::DiagnosticError(assets_->diagnostic());
    system_ = BrowserRendering::create();
    if (!system_.valid())
        throw core::DiagnosticError(system_.diagnostics());
    core::pmr::String diagnostic;
    pipelines_[3] = BrowserRendering::pipeline(system_, *startupSources_, diagnostic);
    pipelines_[2] = BrowserRendering::environmentPipeline(system_, *startupSources_, diagnostic);
    pipelines_[1] = BrowserRendering::spritePipeline(system_, *startupSources_, diagnostic);
    shader::ShaderReflection pbrReflection;
    pipelines_[0] =
        BrowserRendering::pbrPipeline(system_, *startupSources_, pbrReflection, diagnostic);
    for (const auto& pipeline : pipelines_)
        if (!pipeline)
            throw core::DiagnosticError(diagnostic);
    if (editorViewportValidationRequested()) {
        tests::OrderedGraphicsCatalogFixture::run(
            *system_.gpuDevice(), *system_.orderedGraphicsExecution(), pipelines_);
        editorCatalogValidationPassed();
    }
    viewport_.create(*system_.gpuDevice(),
        pipelines_[2],
        pipelines_[0],
        system_.orderedGraphicsExecution(),
        pbrReflection,
        pipelines_[1]);
    gui_ = core::makeUnique<renderer::GuiOrderedRenderer>(*system_.gpuDevice());
    auto quota = core::makeShared<core::CpuMemoryQuota>(16 * BrowserRendering::MiB);
    core::requireSuccessfulResult(gui_->create({2,
                                      BrowserRendering::maxCommands,
                                      4,
                                      1,
                                      quota->limitBytes(),
                                      quota,
                                      system_.orderedGraphicsExecution(),
                                      pipelines_[3],
                                      pipelines_[3]->shaderReflection().sourceSha256,
                                      {1, false, false}}),
        "EDITOR-WEB-GUI");
    auto white = core::shareUnique(system_.gpuDevice()->createTexture({"web.solid",
        {1, 1, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ShaderRead}));
    if (!white)
        throw core::DiagnosticError("EDITOR-WEB-SOLID-TEXTURE");
    const std::array<std::uint8_t, 4> pixel{255, 255, 255, 255};
    rhi::semantics::ClientTextureUpload upload;
    upload.contractVersion = 1;
    upload.extent = {1, 1, 1};
    upload.bytesPerRow = 4;
    upload.bytes = pixel;
    core::requireSuccessfulResult(system_.orderedGraphicsExecution()->uploadTexture(*white, upload),
        "EDITOR-WEB-SOLID-UPLOAD");
    core::requireSuccessfulResult(gui_->registerTexture({"web.solid", white}), "EDITOR-WEB-SOLID");
    core::requireSuccessfulResult(gui_->registerUnifiedAtlas(assets_->atlas()), "EDITOR-WEB-ATLAS");
    assets_->atlas().recordUploadsCompleted(assets_->atlas().pendingUploads().size());
    commands_.reserve(BrowserRendering::maxCommands);
}

bool BrowserEditor::action(core::StringView action, core::StringView arg)
{
    if (candidate_ || exportPending_)
        return false;
    if (action == "create")
        return command(CreateSceneObjectCommand(arg.empty() ? "GameObject" : arg)).status
            == EditorResponseStatus::Completed;
    if (action == "undo")
        return command(UndoSceneCommand{}).status == EditorResponseStatus::Completed;
    if (action == "redo")
        return command(RedoSceneCommand{}).status == EditorResponseStatus::Completed;
    if (action == "component")
        return command(AddSceneComponentCommand(arg)).status == EditorResponseStatus::Completed;
    if (action == "rename")
        return command(SetSceneObjectNameCommand(arg)).status == EditorResponseStatus::Completed;
    if (action == "control")
        return command(ActivateControlCommand(arg)).status == EditorResponseStatus::Completed;
    if (action == "script")
        return command(OpenScriptCommand{core::pmr::String(arg)}).status
            == EditorResponseStatus::Completed;
    if (action == "save-script")
        return shell_->saveScript();
    if (action == "reload-scripts")
        return command(ReloadSceneScriptsCommand{}).status == EditorResponseStatus::Completed;
    if (action == "save-project")
        return command(SaveProjectCommand{}).status == EditorResponseStatus::Completed;
    if (action == "replace-script")
        return command(ReplaceScriptTextCommand(shell_->activeScriptDocumentId(), arg)).status
            == EditorResponseStatus::Completed;
    if (action == "save-scene" || action == "export") {
        if (action == "export" && sourceHost_) {
            message_ = "Connected workspaces save to their source; archive export is unavailable.";
            return false;
        }
        auto state = command(GetEditorStateQuery{}).snapshot;
        if (state.sceneIoPending || state.projectSavePending || state.sceneExportPending)
            return false;
        if (action == "export" && shell_->scriptDirty() && !shell_->saveScript())
            return false;
        if (action == "export" && (shell_->hasUnsavedScripts() || state.projectDirty)) {
            message_ = "Save modified script tabs and project settings before exporting";
            return false;
        }
        auto response =
            command(SaveSceneCommand(state.scenePath.empty() ? "scene.arp.yaml" : state.scenePath));
        if (response.status != EditorResponseStatus::Completed)
            return false;
        exportPending_ = action == "export";
        exportTaskId_ = response.snapshot.scenePersistenceTaskId;
        message_ = sourceHost_ ? "Saving scene to connected workspace..."
                               : "Saving scene to virtual workspace...";
        return true;
    }
    if (action == "reopen") {
        const auto state = command(GetEditorStateQuery{}).snapshot;
        return !state.scenePath.empty()
            && command(LoadSceneCommand(state.scenePath)).status == EditorResponseStatus::Completed;
    }
    message_ = "ARP-EDITOR-WEB-ACTION-UNSUPPORTED";
    return false;
}

bool BrowserEditor::importWorkspace(core::StringView source)
{
    if (!acceptsWorkspaceReplacement()) {
        message_ = "Save/export current changes before importing a workspace";
        return false;
    }
    try {
        auto staged = BrowserWorkspace::stage(source, ++importSerial_);
        auto candidate = loadShell(staged.root);
        candidateTaskId_ = 0;
        if (!staged.scene.empty()) {
            const auto response = candidate->submitCommand(LoadSceneCommand(staged.scene));
            if (response.status != EditorResponseStatus::Completed)
                throw core::DiagnosticError("EDITOR-WEB-IMPORT-SCENE-REJECTED");
            candidateTaskId_ = response.snapshot.scenePersistenceTaskId;
        }
        candidate_ = std::move(candidate);
        staged_.emplace(std::move(staged));
        message_ = "Validating imported workspace...";
        return true;
    } catch (const std::exception& error) {
        message_ = error.what();
        return false;
    }
}

bool BrowserEditor::acceptsWorkspaceReplacement() const
{
    const auto state = shell_->submitCommand(GetEditorStateQuery{}).snapshot;
    const auto uncertain = [](const ProjectSourceSaveReceipt& save) {
        return save.terminal && !save.baselineRefreshed
            && save.outcome.commitState == core::io::FileCommitState::OutcomeUnknown;
    };
    return !candidate_ && !exportPending_ && !state.sceneIoPending && !state.projectSavePending
        && !state.sceneExportPending && state.projectIoTaskStatus != ProjectIoStatus::Pending
        && state.sourceReceiptTaskStatus != ProjectIoStatus::Pending && !state.sceneDirty
        && !state.projectDirty && !shell_->hasUnsavedScripts()
        && !uncertain(state.scriptSaveReceipt) && !uncertain(state.sceneSaveReceipt)
        && !uncertain(state.projectSaveReceipt);
}

bool BrowserEditor::connectWorkspace(core::StringView yaml, core::StringView token)
{
    if (!acceptsWorkspaceReplacement()) {
        message_ =
            "Finish pending operations and resolve unsaved or uncertain saves before connecting.";
        return false;
    }
    const char* diagnostic = nullptr;
    auto profile =
        EditorWorkspaceConnectionProfile::decode(yaml, token, maxConnectionBytes, diagnostic);
    if (!profile) {
        message_ = diagnostic;
        return false;
    }
    try {
        auto host = core::makeUnique<EditorProjectSourceHost>();
        // Private UI/bootstrap data is never a fallback for unavailable remote
        // project bytes. Only a successful original source-open can publish it.
        auto candidate = loadShell("/workspace/default");
        core::pmr::String message("Connecting workspace...");
        message.reserve(maxConnectionMessageBytes);
        if (host->connectRemote(profile->io, profile->editor) != core::io::FileIoStatus::Accepted) {
            message_ = "EDITOR-WEB-WORKSPACE-CONNECTION-NOT-ACCEPTED";
            return false;
        }
        candidate_ = std::move(candidate);
        candidateSourceHost_ = std::move(host);
        candidateConnection_ = std::move(profile);
        sourceOpening_ = false;
        candidateTaskId_ = 0;
        message_.swap(message);
        return true;
    } catch (...) {
        message_ = "EDITOR-WEB-WORKSPACE-CONNECTION-PREPARATION";
        return false;
    }
}

void BrowserEditor::discardSourceCandidate(std::string_view diagnostic)
{
    if (diagnostic.size() <= maxConnectionMessageBytes)
        message_.assign(diagnostic.data(), diagnostic.size());
    else
        message_ = "EDITOR-WEB-WORKSPACE-DIAGNOSTIC-BUDGET";
    candidate_.reset();
    candidateSourceHost_.reset();
    candidateConnection_.reset();
    sourceOpening_ = false;
}

void BrowserEditor::advanceSourceCandidate()
try {
    using S = EditorProjectSourceHost::ConnectionStatus;
    candidateSourceHost_->dispatch();
    const auto status = candidateSourceHost_->connectionStatus();
    if (status == S::Connecting)
        return;
    if (status == S::Failed) {
        discardSourceCandidate(candidateSourceHost_->diagnostic()
                ? candidateSourceHost_->diagnostic()
                : "EDITOR-WEB-WORKSPACE-CONNECTION-FAILED");
        return;
    }
    if (!sourceOpening_) {
        if (!candidateSourceHost_->bindRemote(*candidate_)) {
            discardSourceCandidate("EDITOR-WEB-WORKSPACE-BIND-FAILED");
            return;
        }
        OpenProjectCommand open{candidateConnection_->projectRoot, true};
        open.source = "workspace";
        const auto accepted = candidate_->submitCommand(std::move(open));
        if (accepted.status != EditorResponseStatus::Completed) {
            discardSourceCandidate("EDITOR-WEB-WORKSPACE-PROJECT-NOT-ACCEPTED");
            return;
        }
        candidateTaskId_ = accepted.snapshot.projectIoTaskId;
        sourceOpening_ = true;
    }
    candidate_->pollTasks();
    const auto state = candidate_->submitCommand(GetEditorStateQuery{}).snapshot;
    if (state.projectIoTaskStatus == ProjectIoStatus::Pending)
        return;
    if (state.projectIoTaskId != candidateTaskId_
        || state.projectIoTaskStatus != ProjectIoStatus::Succeeded) {
        discardSourceCandidate(state.projectIoDiagnostic.empty()
                ? std::string_view("EDITOR-WEB-WORKSPACE-PROJECT-FAILED")
                : std::string_view(state.projectIoDiagnostic));
        return;
    }
    core::pmr::String ready(candidateSourceHost_->remoteWritable()
            ? "Connected workspace. Save changes before closing."
            : "Connected workspace (read-only).");
    ready.reserve(maxConnectionMessageBytes);
    if (const auto* id = viewport_.releaseProjectResourcesAfterOrderedIssue()) {
        discardSourceCandidate(id);
        return;
    }
    // Destroy the retired Shell before its host stops producers and drains
    // callbacks. Then retire any old virtual filesystem workspace.
    shell_ = std::move(candidate_);
    sourceHost_ = std::move(candidateSourceHost_);
    workspace_.reset();
    candidateConnection_.reset();
    sourceOpening_ = false;
    message_.swap(ready);
} catch (...) {
    discardSourceCandidate("EDITOR-WEB-WORKSPACE-PROJECT-PREPARATION");
}

void BrowserEditor::tick()
{
    const bool replacingWorkspace = bool(candidate_);
    if (sourceHost_)
        sourceHost_->dispatch();
    shell_->pollTasks();
    if (candidateSourceHost_)
        advanceSourceCandidate();
    else if (candidate_) {
        candidate_->pollTasks();
        const auto state = candidate_->submitCommand(GetEditorStateQuery{}).snapshot;
        if (!state.sceneIoPending) {
            const auto task =
                candidate_->submitCommand(GetScenePersistenceTaskQuery{candidateTaskId_}).snapshot;
            if (!staged_->scene.empty()
                && task.scenePersistenceTaskStatus != ScenePersistenceTaskStatus::Succeeded) {
                message_ = "EDITOR-WEB-IMPORT-SCENE-FAILED: ";
                message_ += task.scenePersistenceTaskDiagnostic;
            } else if (!restoreCandidatePresentation()) {
                message_ = "EDITOR-WEB-IMPORT-SCRIPT-PRESENTATION-FAILED";
            } else if (const auto* id = viewport_.releaseProjectResourcesAfterOrderedIssue()) {
                message_ = id;
            } else {
                shell_ = std::move(candidate_);
                sourceHost_.reset();
                workspace_.reset();
                workspace_.emplace(std::move(*staged_));
                message_ = "Imported into virtual workspace; export to keep changes";
            }
            candidate_.reset();
            staged_.reset();
        }
    }
    if (shell_->consumeGamePreviewCommand() != GamePreviewModel::Request::None)
        shell_->setGamePreviewState(false,
            false,
            "ARP-EDITOR-WEB-PREVIEW-UNAVAILABLE: desktop child processes are unavailable");
    platform::WindowEventBatch events;
    windows_.pumpEvents(events);
    for (auto event : events.inputEvents) {
        // Events collected against the old document must not edit a cold
        // replacement candidate or arrive in the newly published document.
        if (replacingWorkspace)
            continue;
        const auto& canvas = shell_->uiDocument().canvas();
        EditorCanvasInputMapping mapping(
            float(width_), float(height_), canvas.width, canvas.height);
        if (const auto* id = mapping.apply(event))
            throw core::DiagnosticError(id);
        const auto routed =
            EditorPlatformInputRouter::dispatch(*shell_, event, input_, window_.id());
        if (!routed.ok())
            throw core::DiagnosticError(routed.diagnostic.message);
    }
    double cssWidth = 0, cssHeight = 0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    const int nextWidth = int(cssWidth), nextHeight = int(cssHeight);
    if (nextWidth > 0 && nextHeight > 0 && (nextWidth != width_ || nextHeight != height_)) {
        core::requireSuccessfulResult(
            system_.resize({std::uint32_t(nextWidth), std::uint32_t(nextHeight)}),
            "EDITOR-WEB-RESIZE");
        width_ = nextWidth;
        height_ = nextHeight;
        window_.resize(width_, height_);
    }
    // Apply after all pagehide/pageshow listeners finish, before any GPU work.
    core::requireSuccessfulResult(
        system_.setSurfaceAvailable(editorPageAvailable() != 0), "EDITOR-WEB-SURFACE");
    if (!system_.capabilities().canRender)
        return;
    if (candidate_) {
        // Keep the last committed canvas while file workers prepare a new
        // workspace. Polling yields every RAF; no frame/completion is invented.
        editorPublish(int(frames_),
            int(commands_.size()),
            int(shell_->sceneDocument().hierarchyEntries().size()),
            shell_->sceneDirty(),
            true,
            shell_->hasUnsavedScripts(),
            bool(sourceHost_),
            sourceHost_ && sourceHost_->remoteWritable(),
            message_.c_str());
        return;
    }
    auto authored = shell_->drawCommands(kEditorSceneViewportTextureId);
    if (!authored.ok())
        throw core::DiagnosticError(authored.diagnostics());
    auto& atlas = assets_->atlas();
    auto resolved = atlas.resolve(authored.commands());
    if (!resolved.ok())
        throw core::DiagnosticError(resolved.diagnostics());
    if (!atlas.pendingUploads().empty()) {
        core::requireSuccessfulResult(gui_->updateUnifiedAtlas(atlas), "EDITOR-WEB-GLYPHS");
        atlas.recordUploadsCompleted(atlas.pendingUploads().size());
    }
    if (resolved.commands().size() > BrowserRendering::maxCommands)
        throw core::DiagnosticError("EDITOR-WEB-DRAW-CAPACITY");
    commands_.assign(resolved.commands().begin(), resolved.commands().end());
    const auto& canvas = shell_->uiDocument().canvas();
    const float sx = width_ / canvas.width, sy = height_ / canvas.height;
    for (auto& c : commands_) {
        if (c.kind == renderer::ui::UiDrawCommandKind::Image
            && c.textureSource == renderer::ui::UiTextureSource::Atlas
            && c.textureResourceId != atlas.atlas().resourceId) {
            const auto& textures = shell_->uiDocument().textures();
            const auto found =
                std::find_if(textures.begin(), textures.end(), [&](const auto& texture) {
                    return texture.resourceId == c.textureResourceId;
                });
            if (found == textures.end() || found->atlasId != atlas.atlas().resourceId)
                throw core::DiagnosticError("EDITOR-WEB-IMAGE-ATLAS-BINDING");
            c.textureResourceId = found->atlasId;
        }
        c.rect.x *= sx;
        c.rect.width *= sx;
        c.rect.y *= sy;
        c.rect.height *= sy;
        c.clipRect.x *= sx;
        c.clipRect.width *= sx;
        c.clipRect.y *= sy;
        c.clipRect.height *= sy;
    }
    viewportVisible_ = false;
    for (const auto& c : commands_) {
        if (c.kind != renderer::ui::UiDrawCommandKind::Image
            || c.textureResourceId != kEditorSceneViewportTextureId)
            continue;
        if (!std::isfinite(c.rect.width) || !std::isfinite(c.rect.height) || c.rect.width <= 0
            || c.rect.height <= 0 || c.rect.width > BrowserRendering::maxViewportExtent
            || c.rect.height > BrowserRendering::maxViewportExtent)
            throw core::DiagnosticError("EDITOR-WEB-VIEWPORT-EXTENT-BUDGET");
        const rhi::Extent2D extent{
            std::uint32_t(std::ceil(c.rect.width)), std::uint32_t(std::ceil(c.rect.height))};
        viewport_.resize(*system_.gpuDevice(),
            *gui_,
            extent,
            system_.orderedGraphicsCapabilities().attachmentSampleFlipY);
        const auto snapshot = shell_->sceneDocument().scene().extractTraversalSnapshot(
            scene::SceneTraversalOptions{BrowserRendering::maxSceneObjects});
        const char* diagnostic = nullptr;
        const auto visible = shell_->visibleSceneSnapshot(snapshot, diagnostic);
        if (!visible)
            throw core::DiagnosticError(diagnostic);
        const auto frame = buildEditorSceneViewportFrame(shell_->sceneDocument().scene(),
            snapshot,
            shell_->stateVersion(),
            extent.width,
            extent.height,
            {0, 0, extent.width, extent.height},
            &shell_->projectResources(),
            BrowserRendering::maxSceneObjects,
            &*visible);
        if (!frame.valid()
            && frame.validation.diagnostic != EditorSceneViewportDiagnostic::MissingCamera)
            shell_->reportViewportDiagnostic(
                editorSceneViewportDiagnosticCode(frame.validation.diagnostic));
        viewport_.prepareGeometry(*shell_, frame.frame);
        viewport_.prepareSprites(*shell_, *visible, extent);
        viewportSky_ = frame.frame.sky;
        viewportVisible_ = true;
        break;
    }
    const auto report = system_.renderOrderedFrame(*this);
    if (!report.success)
        throw core::DiagnosticError(report.diagnostics);
    ++frames_;
    const auto state = command(GetEditorStateQuery{}).snapshot;
    if (exportPending_ && !state.sceneIoPending) {
        exportPending_ = false;
        const auto task = command(GetScenePersistenceTaskQuery{exportTaskId_}).snapshot;
        if (task.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded
            && task.scenePersistenceTaskCommitted && !state.sceneDirty) {
            const auto source = BrowserWorkspace::encode(shell_->projectRoot(), state.scenePath);
            editorDownload(source.c_str());
            message_ = "Export download requested; virtual saves alone do not survive refresh";
        } else
            message_ = "EDITOR-WEB-EXPORT-SAVE-FAILED";
    }
    editorPublish(int(frames_),
        int(commands_.size()),
        int(shell_->sceneDocument().hierarchyEntries().size()),
        state.sceneDirty || state.projectDirty,
        state.sceneIoPending || bool(candidate_)
            || state.projectIoTaskStatus == ProjectIoStatus::Pending
            || state.sourceReceiptTaskStatus == ProjectIoStatus::Pending,
        shell_->hasUnsavedScripts(),
        bool(sourceHost_),
        sourceHost_ && sourceHost_->remoteWritable(),
        message_.c_str());
}

bool BrowserEditor::restoreCandidatePresentation()
{
    // A reimport of the same explicit project identity preserves the active
    // script only when that exact relative file exists in the new workspace.
    // This changes presentation, never the imported source or scene contents.
    const auto* previous = shell_->projectManifest();
    const auto* next = candidate_->projectManifest();
    if (!previous || !next || previous->id != next->id || shell_->scriptPath().empty())
        return true;
    const auto relative = shell_->scriptPath().lexically_relative(shell_->projectRoot());
    if (!core::FilePath::isSafeRelativePath(core::FilePath::utf8(relative)))
        return false;
    const auto path = candidate_->projectRoot() / relative;
    std::error_code error;
    const bool exists = std::filesystem::is_regular_file(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        return false;
    return !exists || candidate_->openScriptFile(path);
}
} // namespace
} // namespace arp::editor

extern "C" EMSCRIPTEN_KEEPALIVE int editor_action(const char* action, const char* arg)
try {
    return arp::editor::app && arp::editor::app->action(action, arg);
} catch (const std::exception& error) {
    editorFatal(error.what());
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int editor_import_workspace(const char* source)
{
    return arp::editor::app && arp::editor::app->importWorkspace(source);
}

extern "C" EMSCRIPTEN_KEEPALIVE int editor_connect_workspace(const char* yaml, const char* token)
try {
    return yaml && token && arp::editor::app && arp::editor::app->connectWorkspace(yaml, token);
} catch (...) {
    // Configuration/transport rejection must not stop the editor or echo secrets.
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* editor_script_text()
{
    return arp::editor::app ? arp::editor::app->scriptText() : "";
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* editor_remote_request(const char* method,
    const char* path,
    const char* body,
    const char* session,
    const char* accept,
    const char* protocolVersion)
try {
    return arp::editor::app
        ? arp::editor::app->remoteRequest(method, path, body, session, accept, protocolVersion)
        : R"({"error":"editor_not_ready"})";
} catch (const std::exception&) {
    // A request failure must not stop rendering or expose credentials.
    return R"({"error":"web_bridge_request_failed"})";
}

extern "C" EMSCRIPTEN_KEEPALIVE int editor_remote_status()
{
    return arp::editor::app ? arp::editor::app->remoteStatus() : 503;
}

extern "C" EMSCRIPTEN_KEEPALIVE double editor_core_live_bytes()
{
    return static_cast<double>(arp::core::defaultCpuAllocator().stats().liveBytes);
}

extern "C" EMSCRIPTEN_KEEPALIVE double editor_core_live_allocations()
{
    const auto stats = arp::core::defaultCpuAllocator().stats();
    return static_cast<double>(stats.allocationCount - stats.deallocationCount);
}

extern "C" EMSCRIPTEN_KEEPALIVE double editor_gpu_logical_bytes()
{
    return arp::editor::app ? double(arp::editor::app->gpuMemory().allocatedBytes) : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE double editor_gpu_live_resources()
{
    return arp::editor::app ? double(arp::editor::app->gpuMemory().liveResourceCount) : 0;
}

int main()
try {
    std::array<arp::core::pmr::String, 3> urls;
    for (std::size_t i = 0; i < urls.size(); ++i) {
        std::array<char, 4096> value{};
        if (!editorStartupBaseUrl(static_cast<int>(i), value.data(), value.size()))
            throw arp::core::DiagnosticError("EDITOR-WEB-STARTUP-URL-BUDGET");
        urls[i] = value.data();
    }
    arp::editor::startup = arp::core::makeUnique<arp::editor::EditorWebStartupHost>(urls);
    emscripten_set_main_loop(
        [] {
            try {
                if (arp::editor::startup) {
                    auto& host = *arp::editor::startup;
                    host.poll();
                    if (!host.done())
                        return;
                    if (!host.ready())
                        throw arp::core::DiagnosticError(host.diagnostic());
                    auto candidate = arp::core::makeUnique<arp::editor::BrowserEditor>();
                    candidate->create(host.sources());
                    arp::editor::app = std::move(candidate);
                    arp::editor::startup.reset();
                    return;
                }
                arp::editor::app->tick();
            } catch (const std::exception& error) {
                editorFatal(error.what());
                arp::editor::startup.reset();
                emscripten_cancel_main_loop();
            }
        },
        0,
        false);
    return 0;
} catch (const std::exception& error) {
    editorFatal(error.what());
    return 1;
}
