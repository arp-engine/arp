#include "core/diagnostics/diagnostic_error.h"
#include "core/io/filesystem_path.h"
#include "editor/ingress/editor_platform_input_router.h"
#include "editor/ingress/platform_input_adapter.h"
#include "editor/ui/editor_ui_assets.h"
#include "editor/web/browser_rendering.h"
#include "editor/web/browser_workspace.h"
#include "editor/workspace/editor_shell.h"
#include "input/input.h"
#include "platform/window.h"
#include "renderer/gui/gui_ordered_renderer.h"

#include <array>
#include <emscripten.h>
#include <emscripten/html5.h>

// clang-format off
EM_JS(void, editorPublish, (int frames, int draws, int objects, int dirty, int busy,
    int scriptDirty, const char* message), {
    const state = Module.editorState || (Module.editorState = {});
    Object.assign(state, {frames, draws, objects, dirty: !!dirty, busy: !!busy,
        scriptDirty: !!scriptDirty, message: UTF8ToString(message), backend: 'WebGL1',
        gpuCompletion: false, persistent: false});
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

    void create();
    void tick();
    bool action(core::StringView, core::StringView);
    bool importWorkspace(core::StringView);

    const char* scriptText() const { return shell_->scriptText().c_str(); }

    core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines() const noexcept override
    {
        return pipelines_;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle target) override
    {
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

    EditorResponse command(EditorCommandPayload value)
    {
        return shell_->submitCommand(std::move(value));
    }

    platform::WindowSystem windows_;
    platform::PlatformWindow window_;
    input::InputSystem input_{{}};
    core::UniquePtr<EditorShell> shell_, candidate_;
    std::optional<BrowserWorkspace::Staged> staged_, workspace_;
    std::optional<EditorUiAssets> assets_;
    platform::RenderSystem system_;
    std::array<std::shared_ptr<rhi::RenderPipeline>, 1> pipelines_;
    core::UniquePtr<renderer::GuiOrderedRenderer> gui_;
    core::pmr::Vector<renderer::ui::UiDrawCommand> commands_;
    core::pmr::String message_{"Virtual workspace: download an export to keep your work"};
    std::uint64_t frames_ = 0, importSerial_ = 0;
    std::uint64_t exportTaskId_ = 0, candidateTaskId_ = 0;
    int width_ = 1440, height_ = 810;
    bool exportPending_ = false;
};

core::UniquePtr<BrowserEditor> app;

core::UniquePtr<EditorShell> BrowserEditor::loadShell(const std::filesystem::path& root)
{
    auto shell = core::makeUnique<EditorShell>();
    const auto loaded = shell->load("/app/editor/resources/editor.arpui", root);
    if (!loaded.ok())
        throw core::DiagnosticError(loaded.diagnostic().message);
    if (assets_)
        assets_->applyTextMetrics(*shell);
    if (shell->submitCommand(ActivateControlCommand("workspace_3D")).status
        != EditorResponseStatus::Completed)
        throw core::DiagnosticError("EDITOR-WEB-WORKSPACE-INITIALIZATION");
    shell->reportViewportDiagnostic(
        "ARP-EDITOR-WEBGL-VIEWPORT-UNAVAILABLE: scene GPU mapping is not yet admitted");
    return shell;
}

void BrowserEditor::create()
{
    core::requireSuccessfulResult(windows_.initialize(), "EDITOR-WEB-WINDOW");
    platform::WindowDesc window;
    window.title = "ARP Editor WebGL";
    window.width = width_;
    window.height = height_;
    window_ = windows_.createWindow(window);
    if (!window_.valid())
        throw core::DiagnosticError("EDITOR-WEB-WINDOW-CREATE");
    shell_ = loadShell("/workspace/default");
    auto authored = shell_->drawCommands();
    if (!authored.ok())
        throw core::DiagnosticError(authored.diagnostics());
    assets_.emplace(EditorUiAssets::build(*shell_, "/app/editor/resources", authored.commands()));
    if (!assets_->ok())
        throw core::DiagnosticError(assets_->diagnostic());
    system_ = BrowserRendering::create();
    if (!system_.valid())
        throw core::DiagnosticError(system_.diagnostics());
    core::pmr::String diagnostic;
    pipelines_[0] = BrowserRendering::pipeline(system_, diagnostic);
    if (!pipelines_[0])
        throw core::DiagnosticError(diagnostic);
    gui_ = core::makeUnique<renderer::GuiOrderedRenderer>(*system_.gpuDevice());
    auto quota = core::makeShared<core::CpuMemoryQuota>(16 * BrowserRendering::MiB);
    core::requireSuccessfulResult(gui_->create({2,
                                      BrowserRendering::maxCommands,
                                      4,
                                      1,
                                      quota->limitBytes(),
                                      quota,
                                      system_.orderedGraphicsExecution(),
                                      pipelines_[0],
                                      pipelines_[0]->shaderReflection().sourceSha256,
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
        return shell_->openScriptFile(shell_->projectRoot() / std::filesystem::path(arg));
    if (action == "save-script")
        return shell_->saveScript();
    if (action == "save-project")
        return command(SaveProjectCommand{}).status == EditorResponseStatus::Completed;
    if (action == "replace-script")
        return command(ReplaceScriptTextCommand(shell_->activeScriptDocumentId(), arg)).status
            == EditorResponseStatus::Completed;
    if (action == "save-scene" || action == "export") {
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
        message_ = "Saving scene to virtual workspace...";
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
    const auto state = command(GetEditorStateQuery{}).snapshot;
    if (candidate_ || exportPending_ || state.sceneIoPending || state.sceneDirty
        || shell_->hasUnsavedScripts() || state.projectDirty) {
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

void BrowserEditor::tick()
{
    const bool replacingWorkspace = bool(candidate_);
    shell_->pollTasks();
    if (candidate_) {
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
            } else {
                shell_ = std::move(candidate_);
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
            message_.c_str());
        return;
    }
    auto authored = shell_->drawCommands();
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
        state.sceneIoPending || bool(candidate_),
        shell_->hasUnsavedScripts(),
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

extern "C" EMSCRIPTEN_KEEPALIVE const char* editor_script_text()
{
    return arp::editor::app ? arp::editor::app->scriptText() : "";
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

int main()
try {
    arp::editor::app = arp::core::makeUnique<arp::editor::BrowserEditor>();
    arp::editor::app->create();
    emscripten_set_main_loop(
        [] {
            try {
                arp::editor::app->tick();
            } catch (const std::exception& error) {
                editorFatal(error.what());
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
