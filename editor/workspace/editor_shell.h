#pragma once
#include "core/ownership/lifetime_epoch.h"
#include "editor/command/editor_command.h"
#include "editor/viewport/editor_scene_gesture_accumulator.h"
#include "editor/viewport/editor_scene_viewport_camera.h"
#include "editor/workspace/editor_exit_panel.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_presentation_state.h"
#include "editor/workspace/editor_project_panel.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_panel.h"
#include "editor/workspace/editor_scene_persistence.h"
#include "editor/workspace/editor_scene_presence.h"
#include "editor/workspace/editor_scene_reference_drag.h"
#include "editor/workspace/editor_scene_transform_panel.h"
#include "editor/workspace/editor_scene_view_pointer.h"
#include "editor/workspace/editor_scene_view_state.h"
#include "editor/workspace/editor_script_file_panel.h"
#include "editor/workspace/editor_script_input_state.h"
#include "editor/workspace/editor_script_inspector.h"
#include "editor/workspace/editor_terrain_browser.h"
#include "editor/workspace/editor_terrain_build.h"
#include "editor/workspace/game_preview_model.h"
#include "editor/workspace/project_browser_model.h"
#include "editor/workspace/script_document_session.h"
#include "platform/window.h"
#include "renderer/ui/ui_draw_plan.h"
#include "ui/ui_document.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>

namespace arp::editor {

class EditorShell;

class EditorAction final : public ui::Button {
public:
    EditorAction(EditorShell& shell, core::pmr::String source)
        : shell_(shell), source_(std::move(source))
    {}

protected:
    void onClick(ui::UiEventContext&) override;

private:
    EditorShell& shell_;
    core::pmr::String source_;
};

class EditorEntry final : public ui::TextInput {
public:
    EditorEntry(EditorShell& shell, core::pmr::String source)
        : shell_(shell), source_(std::move(source))
    {}

protected:
    void onTextChanged(const core::pmr::String&, const core::pmr::String&) override;

private:
    EditorShell& shell_;
    core::pmr::String source_;
};

class EditorPropertyCheckbox final : public ui::Checkbox {
public:
    EditorPropertyCheckbox(EditorShell& shell, core::pmr::String source)
        : shell_(shell), source_(std::move(source))
    {}

protected:
    void onValueChanged(const ui::UiControlValue&, const ui::UiControlValue& after) override;

private:
    EditorShell& shell_;
    core::pmr::String source_;
};

class EditorPropertyColorPicker final : public ui::ColorPicker {
public:
    EditorPropertyColorPicker(EditorShell& shell, core::pmr::String source)
        : shell_(shell), source_(std::move(source))
    {}

protected:
    void onValueChanged(const ui::UiControlValue&, const ui::UiControlValue& after) override;

private:
    EditorShell& shell_;
    core::pmr::String source_;
};

class EditorEnumDropdown final : public ui::Dropdown {
public:
    EditorEnumDropdown(EditorShell& shell,
        core::pmr::String source,
        core::pmr::Vector<SceneInspectorEnumOption> options,
        bool useCommandValue = false,
        ui::WidgetId popupParent = {},
        std::uint32_t visibleRows = 0,
        SceneInspectorPropertyKind propertyKind = SceneInspectorPropertyKind::Enumeration)
        : shell_(shell), source_(std::move(source)), options_(std::move(options)),
          useCommandValue_(useCommandValue), popupParent_(popupParent), visibleRows_(visibleRows),
          propertyKind_(propertyKind)
    {}

    ui::UiResult applyDefaultAction(const ui::UiEvent& event, ui::UiEventContext& context) override;
    ui::UiResult applyInteraction(
        const ui::UiControlInteraction& interaction, ui::UiEventContext& context) override;
    void choose(std::uint32_t selected);

protected:
    void onSelectionChanged(std::uint32_t, std::uint32_t selected) override;

private:
    friend class EditorShell;
    friend class EditorProjectPanel;
    void openMenu();
    void closeMenu();
    EditorShell& shell_;
    core::pmr::String source_;
    core::pmr::Vector<SceneInspectorEnumOption> options_;
    bool useCommandValue_ = false;
    ui::WidgetId popupParent_, popupScroll_;
    std::uint32_t visibleRows_ = 0;
    SceneInspectorPropertyKind propertyKind_ = SceneInspectorPropertyKind::Enumeration;
    core::pmr::Vector<ui::WidgetId> optionWidgets_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ui::WidgetId> optionLabelWidgets_{&core::defaultCpuMemoryResource()};
};

class EditorEnumOptionButton final : public ui::Button {
public:
    EditorEnumOptionButton(EditorEnumDropdown& dropdown, std::uint32_t index)
        : dropdown_(dropdown), index_(index)
    {}

protected:
    void onClick(ui::UiEventContext&) override;

private:
    EditorEnumDropdown& dropdown_;
    std::uint32_t index_ = 0;
};

class EditorInspectorView final : public ui::ScrollView {
public:
    explicit EditorInspectorView(EditorShell& shell) : shell_(shell) {}

protected:
    void onValueChanged(const ui::UiControlValue&, const ui::UiControlValue& after) override;

private:
    EditorShell& shell_;
};

class EditorScrollThumb final : public ui::Button {
public:
    explicit EditorScrollThumb(EditorShell& shell) : shell_(shell) {}

    ui::UiResult applyDefaultAction(const ui::UiEvent& event, ui::UiEventContext& context) override;

private:
    EditorShell& shell_;
    float grabOffset_ = 0.0f;
};

class EditorFactory final : public ui::IUiControlFactory {
public:
    EditorFactory(EditorShell& shell, core::pmr::String source)
        : shell_(shell), source_(std::move(source))
    {}

    core::UniquePtr<ui::Control> create(const ui::UiControlDesc& desc) const override;

private:
    EditorShell& shell_;
    core::pmr::String source_;
};

// Business state belongs to the shell; dispatch/lifecycle belongs to Arp UI.
class EditorShell final : public IEditorCommandExecutor {
public:
    EditorShell();
    ~EditorShell();
    class PreparedSceneTransform;
    enum class TransformCommitResult {
        Ready,
        Committed,
        NoChange,
        Stale,
        Consumed,
        WrongThread,
        Busy
    };
    // Cold owner-thread primitives. The command dispatcher owns stateVersion;
    // callers must retire the token on the same thread before replacing Scene.
    core::UniquePtr<PreparedSceneTransform> prepareSceneTransform(
        const TransformSceneSelectionCommand&, const char*& diagnostic);
    TransformCommitResult sceneTransformReadiness(const PreparedSceneTransform&) const noexcept;
    TransformCommitResult commitSceneTransform(PreparedSceneTransform&) noexcept;
    class PreparedSceneReplacement;
    using SceneReplacementResult = EditorSceneDocument::ReplacementResult;
    core::UniquePtr<PreparedSceneReplacement> prepareSceneReplacement(
        EditorSceneDocument& candidate, core::pmr::String& diagnostic) const;
    SceneReplacementResult sceneReplacementReadiness(
        const PreparedSceneReplacement&, const EditorSceneDocument& candidate) const noexcept;
    SceneReplacementResult commitSceneReplacement(
        PreparedSceneReplacement&, EditorSceneDocument& candidate) noexcept;
    ui::UiResult load(const std::filesystem::path& path, std::filesystem::path projectRoot = {});
    EditorResponse submitCommand(EditorCommandPayload payload,
        EditorCommandOrigin origin = EditorCommandOrigin::LocalUi,
        std::uint64_t correlationId = 0);
    EditorResponse submitCommand(EditorCommandEnvelope command);

    void pollTasks();
    // Cold host binding, never a native handle in a command or remote payload.
    // The service/queue outlive this Shell and all accepted callbacks.
    bool bindProjectSourceFiles(core::io::FileService&,
        core::PoolHandle mount,
        std::string_view alias,
        EditorProjectIoBudget = {});

    bool terrainBuildPending() const noexcept { return terrainBuild_.pending(); }

    std::uint64_t stateVersion() const noexcept { return commandDispatcher_.stateVersion(); }

    struct SceneHistoryUsage {
        EditorScenePresence::Cost undo, redo;
        std::size_t undoEntries = 0, redoEntries = 0;
    };

    // Owner-thread diagnostic of the conservative history ledger, not measured
    // heap usage. Reading it neither allocates nor changes the history branch.
    SceneHistoryUsage sceneHistoryUsage() const noexcept;

    EditorCommandOutcome execute(const EditorCommandPayload& payload) override;
    // True consumes this event before UI dispatch; captured releases must never
    // reach an unrelated control that did not receive the matching down.
    bool handlePlatformInput(const platform::WindowInputEvent& event);
    GamePreviewModel::Request consumeGamePreviewCommand();
    void setGamePreviewState(bool running, bool starting, const char* status);

    bool gamePreviewRunning() const { return gamePreview_.running(); }

    bool scriptEditorCapturesKeyboard() const
    {
        return !authoringUiBlocked() && presentationState_.workspace() == "Script"
            && !scriptDocuments_.activePath().empty();
    }

    bool openScriptFile(const std::filesystem::path& path);

    std::size_t openScriptCount() const { return scriptDocuments_.size(); }

    bool hasUnsavedScripts() const
    {
        return std::any_of(scriptDocuments_.documents().begin(),
            scriptDocuments_.documents().end(),
            [](const auto& document) {
                return document.dirty();
            });
    }

    void setCodeColumnWidth(float advance)
    {
        if (advance > 0.0f)
            codeColumnWidth_ = advance;
    }

    void setCodeCjkColumnWidth(float advance)
    {
        if (advance > 0.0f)
            codeCjkColumnWidth_ = advance;
    }

    bool saveScript();

    std::uint64_t feedbackVersion() const noexcept { return feedbackVersion_; }

    bool scriptPresentationFailed() const noexcept { return scriptPresentationFailed_; }

    // Refresh current chrome/feedback only; never replay a document operation.
    bool retryScriptPresentation() noexcept;

    bool exitRequested() const noexcept { return exitRequested_; }

    bool sceneDirty() const;

    bool projectDirty() const noexcept { return projectManifestDirty_; }

    const ProjectManifest* projectManifest() const noexcept
    {
        return projectManifest_ ? &*projectManifest_ : nullptr;
    }

    const std::filesystem::path& projectRoot() const { return projectBrowser_.root(); }

    const std::filesystem::path& selectedFilePath() const { return selectedFilePath_; }

    bool filesPresentationFailed() const noexcept { return filesPresentationFailed_; }

    bool retryFileBrowserPresentation() noexcept;

    const std::filesystem::path& scriptPath() const { return scriptDocuments_.activePath(); }

    std::uint64_t activeScriptDocumentId() const;

    const core::pmr::String& scriptText() const { return scriptTextView(); }

    std::size_t scriptSelectionStart() const
    {
        return std::min(scriptDocuments_.activeAnchor(), scriptDocuments_.activeCursor());
    }

    std::size_t scriptSelectionEnd() const
    {
        return std::max(scriptDocuments_.activeAnchor(), scriptDocuments_.activeCursor());
    }

    bool scriptDirty() const
    {
        const auto* document = scriptDocuments_.active();
        return document && document->dirty();
    }

    std::size_t fileEntryCount() const { return projectBrowser_.entries().size(); }

    std::size_t visibleFileEntryCount() const
    {
        return projectBrowser_.visibleEntryIndices().size();
    }

    std::size_t fileScrollOffset() const { return fileScrollOffset_; }

    std::size_t scriptScrollLine() const { return scriptDocuments_.activeScrollLine(); }

    ui::UiControlRuntime& runtime() { return loaded_.runtime(); }

    const ui::UiControlRuntime& runtime() const { return loaded_.runtime(); }

    const ui::UiDocument& uiDocument() const { return loaded_.document(); }

    ui::WidgetId id(const core::pmr::String& source) const
    {
        return loaded_.widgetIdForSourceNodeId(source);
    }

    const core::pmr::String& selection() const { return presentationState_.selection(); }

    const core::pmr::String& workspace() const { return presentationState_.workspace(); }

    EditorSceneDocument& sceneDocument() noexcept { return sceneDocument_; }

    const EditorSceneDocument& sceneDocument() const noexcept { return sceneDocument_; }

    resource::ResourceManager& projectResources() noexcept { return projectResources_.manager(); }

    core::SharedPtr<resource::ResourceManager> retainProjectResourceManager() const noexcept
    {
        return projectResources_.retainManager();
    }

    const resource::ResourceManager& projectResources() const noexcept
    {
        return projectResources_.manager();
    }

    const EditorProjectResourceLoadResult& projectResourceLoad() const noexcept
    {
        return projectResources_.lastLoad();
    }

    const EditorSceneViewportCamera& sceneViewportCamera() const noexcept
    {
        return sceneViewportCamera_;
    }

    bool sceneViewportObjectSelected(scene::GameObjectId objectId) const noexcept;
    EditorSceneViewState::Observation sceneObjectView(scene::GameObjectId) const noexcept;
    bool sceneObjectEditable(scene::GameObjectId, bool subtree = false) const noexcept;
    bool sceneObjectVisible(scene::GameObjectId) const noexcept;
    bool sceneObjectPickable(scene::GameObjectId) const noexcept;
    bool sceneSelectionGizmoVisible() const noexcept;
    std::optional<scene::SceneTraversalSnapshot> visibleSceneSnapshot(
        const scene::SceneTraversalSnapshot&, const char*& diagnostic) const noexcept;
    using SceneTransformTool = EditorSceneTransformTool;

    SceneTransformTool sceneTransformTool() const noexcept { return sceneTransformTool_; }

    EditorSceneTransformSettings sceneTransformSettings() const noexcept;

    EditorSceneTransformSpace sceneTransformSpace() const noexcept { return sceneTransformSpace_; }

    const EditorSceneTransformSnap& sceneTransformSnap() const noexcept
    {
        return sceneTransformSnap_;
    }

    EditorSceneTransformPivot sceneTransformPivot() const noexcept { return sceneTransformPivot_; }

    std::uint64_t activeSceneSelectionTransformEditId() const noexcept;
    bool sceneSelectionTransformEditCurrent(std::uint64_t gestureId) const noexcept;
    const char* sceneTransformSelectionView(core::Vec3&, EditorSceneTransformFrame&);

    void selectSceneObject(std::uint64_t objectId)
    {
        (void)submitCommand(SelectSceneObjectCommand{objectId});
    }

    void activate(const core::pmr::String& source);
    void entryChanged(const core::pmr::String& source, const core::pmr::String& value);
    void colorEntryChanged(const core::pmr::String& source, const ui::UiControlColorValue& value);
    enum class InspectorState { Pending, Failed, Ready };

    InspectorState inspectorState() const noexcept { return sceneInspectorState_; }

    const char* inspectorFailure() const noexcept { return sceneInspectorFailure_; }

    bool retrySceneInspector();
    // Widget identity rejects callbacks from retired or partially built trees.
    bool acceptsInspectorInput(std::string_view source, ui::WidgetId widget = {}) const noexcept;
    void inspectorScrollChanged(float offset);
    void dragInspectorThumb(float top);
    float inspectorScrollOffset() const;
    renderer::ui::UiDrawPlanLowerResult drawCommands(
        std::string_view viewportTextureResourceId = {});
    ui::UiInputFrameDispatchResult scenario();

    bool outputOpen() const { return presentationState_.outputOpen(); }

    void reportViewportDiagnostic(std::string_view diagnostic);

    const ui::UiDiagnostic& diagnostic() const { return diagnostic_; }

private:
    friend class EditorEnumDropdown;
    friend class EditorRecoveryAdmission;
    friend class EditorProjectPanel;
    friend class EditorScenePanel;
    friend class EditorSceneTransformPanel;
    friend class EditorScriptFilePanel;
    friend class EditorScriptInspector;
    friend class EditorExitPanel;
    friend class EditorModalSession;
    static constexpr std::size_t kMaxSceneHistoryEntries = 256u;
    core::UniquePtr<PreparedSceneReplacement> prepareSceneReplacement(
        EditorSceneDocument&, core::pmr::String&, EditorSceneDocument::ServicePolicy) const;
    // Shared workspace state stays owned by this facade. Implementations are
    // grouped into coordination, project, document, input and presentation TUs.
    static bool containsIgnoringCase(const core::pmr::String&, const core::pmr::String&);
    static bool isArpPath(const std::filesystem::path&);
    static std::size_t scriptLineCount(const core::pmr::String&);
    static float scriptTabWidth(const std::filesystem::path&);
    static core::pmr::String scriptTabTitle(
        const std::filesystem::path&, float availableWidth, bool dirty);
    static bool isWideCodepoint(std::uint32_t);
    static float utf8AdvancePixels(std::string_view, std::size_t, float, float);
    static std::size_t utf8OffsetAtAdvance(std::string_view, float, float, float);
    const core::pmr::String& scriptTextView() const;
    core::pmr::String& scriptTextView();
    void applyActivation(const core::pmr::String& source);
    void applyEntryChanged(const core::pmr::String& source, const core::pmr::String& value);
    EditorCommandOutcome applyPlatformInput(const EditorInputCommand& command);
    EditorCommandOutcome applyScriptKeyboardInput(const EditorInputCommand& command);
    EditorCommandOutcome applySceneKeyboardInput(const EditorInputCommand& command);
    std::optional<EditorCommandOutcome> applyScriptPointerInput(const EditorInputCommand& command);
    EditorScriptInputState captureScriptInputState() const noexcept;
    EditorCommandOutcome scriptInputOutcome(const EditorScriptInputState& before) const noexcept;
    void cancelScriptPointerCapture() noexcept;
    bool handleSceneReferenceDrag(const EditorInputCommand&);
    bool handleSceneViewPointer(const EditorInputCommand&);
    bool sceneHierarchyPointerHit(const ui::CanvasLayoutResult&, float x, float y) const;
    bool sceneViewPointerTarget(float x, float y, EditorSceneViewPointer::Target&) const;
    void reportSceneReferenceFailure();
    bool sceneReferenceDragCurrent() const;
    const char* prepareSceneReferenceDrop(
        float x, float y, EditorSceneReferenceAssignment::Intent&, ui::WidgetId&);
    void appendSceneReferenceDrag(
        const ui::CanvasLayoutResult&, core::pmr::Vector<renderer::ui::UiDrawCommand>&) const;
    void applyInspectorScrollChanged(float offset);
    void applyDragInspectorThumb(float top);
    void applyGamePreviewState(bool running, bool starting, const core::pmr::String& status);
    EditorCommandOutcome applyScriptHistory(std::uint64_t documentId, bool undo);
    EditorCommandOutcome saveScriptDocument();
    EditorCommandOutcome openScriptDocument(const OpenScriptCommand&);
    void appendScriptSnapshot(EditorStateSnapshot&) const;
    EditorCommandOutcome reloadScriptDocument(bool discardChanges);
    EditorCommandOutcome reloadSourceScript(bool discardChanges);
    EditorCommandOutcome applySceneHistory(bool undo);
    EditorCommandOutcome applySceneBatchHistory(bool undo);
    EditorCommandOutcome transformSceneSelection(const TransformSceneSelectionCommand&);
    bool sceneBatchTransformBusy(bool allowOwnGesture = false) const noexcept;
    const char* captureSceneTransformBatch(EditorSceneTransformBatch&, EditorSceneTransformPivot);
    void publishSceneTransformHistory(PreparedSceneTransform&) noexcept;
    EditorCommandOutcome beginSceneSelectionTransformEdit(const TransformSceneSelectionCommand&);
    EditorCommandOutcome updateSceneSelectionTransformEdit(std::uint64_t gestureId, double amount);
    EditorCommandOutcome endSceneSelectionTransformEdit(std::uint64_t gestureId);
    void invalidateSceneTransformInspector() noexcept;
    EditorCommandOutcome applySceneTransform(
        scene::GameObjectId objectId, const scene::Transform& transform);
    EditorCommandOutcome beginSingleSceneTransformEdit(std::uint64_t objectId);
    EditorCommandOutcome endSingleSceneTransformEdit(std::uint64_t objectId) noexcept;
    bool singleSceneTransformEditCurrent() const noexcept;
    struct SceneHistoryEntry;
    EditorCommandOutcome createSceneObjectWithHistory(const CreateSceneObjectCommand&);
    EditorCommandOutcome destroySceneObjectWithHistory();
    EditorCommandOutcome addSceneComponentWithHistory(const AddSceneComponentCommand&);
    EditorCommandOutcome removeSceneComponentWithHistory(const RemoveSceneComponentCommand&);
    void requireSceneScriptContext() const;
    EditorCommandOutcome addSceneScriptComponentWithHistory(const AddSceneScriptComponentCommand&);
    EditorCommandOutcome setSceneScriptFieldsWithHistory(const SetSceneScriptFieldsCommand&);
    EditorCommandOutcome applyScriptFieldHistory(const SceneHistoryEntry&, bool undo);
    EditorCommandOutcome applyPresenceHistory(const SceneHistoryEntry&, bool undo);
    void publishPreparedSceneHistory(core::pmr::Vector<SceneHistoryEntry>& preparedUndo,
        core::pmr::Vector<SceneHistoryEntry>& retiredRedo,
        SceneHistoryEntry&& entry) noexcept;
    void resetSceneDocumentInteraction();
    EditorCommandOutcome focusSceneSelection();
    EditorCommandOutcome sceneNavigationGate() const;
    EditorCommandOutcome setSceneObjectView(const SetSceneObjectViewCommand&);
    EditorCommandOutcome sceneObjectWriteGate(std::uint64_t objectId, bool subtree) const;
    EditorCommandOutcome focusSceneObjects(
        scene::GameObjectId primary, core::ArrayView<scene::GameObjectId> additional);
    EditorCommandOutcome focusSceneReference(const FocusSceneReferenceCommand&);
    EditorCommandOutcome locateSceneResource(const LocateSceneResourceCommand&);
    EditorCommandOutcome assignSceneReference(const AssignSceneReferenceCommand&);
    bool activateSceneResourceLocate(std::string_view source);
    bool activateSceneReferenceFocus(std::string_view source);
    void text(const core::pmr::String& source, core::pmr::String value);
    void visible(const core::pmr::String& source, bool value);
    void filter(const core::pmr::String& prefix, const core::pmr::String& query);
    core::pmr::Vector<SceneHierarchyEntry> visibleSceneHierarchyEntries() const;
    bool iconSourceRect(std::string_view resourceId, ui::LayoutRect& sourceRect) const;
    void refreshFileSystem();
    bool showTerrainBrowser(bool visible);
    void updateTerrainBrowserDetails();
    ui::WidgetId terrainBrowserWidget(std::string_view name) const;
    EditorCommandOutcome openTerrainPreview(const OpenTerrainPreviewCommand& command);
    void updateFileSlots();
    bool navigateTo(const std::filesystem::path& path);
    bool updateFilePathText();
    bool initializeFileBrowserPresentation();
    void setFileBrowserPresentationFailed(bool failed) noexcept;
    void setResourceNavigationRejected(bool rejected) noexcept;
    void configureGodotDockLayout();
    void updateWorkspaceChrome();
    void activateScriptDocument(std::size_t index);
    EditorCommandOutcome closeScriptDocument(std::uint64_t documentId, bool discardChanges);
    void rebuildScriptTabs(core::pmr::Vector<renderer::ui::UiDrawCommand>& commands) const;
    void rebuildScriptCommands(const ui::CanvasLayoutResult& layout,
        core::pmr::Vector<renderer::ui::UiDrawCommand>& commands) const;
    void rebuildSceneCommands(const ui::CanvasLayoutResult& layout,
        core::pmr::Vector<renderer::ui::UiDrawCommand>& commands,
        std::string_view viewportTextureResourceId);
    void refreshSceneInspector();
    core::pmr::Vector<SceneInspectorProperty> sceneInspectorProperties(scene::GameObjectId) const;
    EditorSceneInspectorSelection::Snapshot inspectorSelectionSnapshot() const;
    void rebuildSceneInspector();
    ui::UiControlRuntimeResult createInspectorControl(ui::UiControlDesc, ui::WidgetId);
    ui::UiControlRuntimeResult createInspectorControl(
        core::UniquePtr<ui::Control>, ui::UiControlDesc, ui::WidgetId);
    void refreshSceneInspectorHeader();
    void finishSceneInspectorLayout(float rowY, bool add);
    void clearSceneInspectorWidgets();
    void synchronizeInspectorAvailability() noexcept;
    void failSceneInspector(const char* diagnostic) noexcept;
    EditorCommandOutcome openProject(const OpenProjectCommand& command);
    EditorCommandOutcome openSourceProject(const OpenProjectCommand&);
    EditorCommandOutcome acceptProjectIoTask(
        core::UniquePtr<EditorProjectIoTask>, std::uint64_t id);
    EditorCommandOutcome navigateSourceProject(const NavigateProjectSourceCommand&);
    EditorCommandOutcome advanceProjectIo(std::uint64_t taskId);
    EditorCommandOutcome cancelProjectIo(std::uint64_t taskId);
    bool projectIoPending() const noexcept;
    EditorCommandOutcome createProject(const CreateProjectCommand& command);
    EditorCommandOutcome closeProject(bool discardChanges);
    EditorCommandOutcome requestEditorExit(bool discardChanges);
    EditorCommandOutcome saveProject();
    EditorCommandOutcome completeProjectSave();
    void pollProjectSave();
    EditorCommandOutcome renameProject(std::string_view name);
    EditorCommandOutcome setProjectSettings(const SetProjectSettingsCommand&);
    EditorCommandOutcome projectReplacementGate(bool discardChanges) const;
    EditorCommandOutcome sceneReplacementGate(bool discardChanges) const;
    EditorCommandOutcome setSceneTransformSettings(const EditorSceneTransformSettings& settings,
        const EditorSceneTransformPanel* authority = nullptr);
    EditorCommandOutcome setSceneTransformSpace(EditorSceneTransformSpace space);
    EditorCommandOutcome setSceneTransformSnap(const EditorSceneTransformSnap& settings);
    EditorCommandOutcome setSceneTransformPivot(EditorSceneTransformPivot pivot);
    EditorCommandOutcome sceneTransformSettingsGate(
        const EditorSceneTransformPanel* authority = nullptr) const;
    void captureSceneTransformGesture(const scene::GameObject& object);
    bool beginSceneTransformGesture(const scene::GameObject& object, unsigned axis);
    bool sceneTransformGestureCurrent() const;
    EditorCommandOutcome finishSceneTransformGesture(bool endActive = false);
    bool modalBlocksActivation(std::string_view source) const;

    bool authoringUiBlocked() const
    {
        return projectIoPending() || projectPanel_.active() || scenePanel_.active()
            || scriptFilePanel_.active() || exitPanel_.active() || sceneTransformPanel_.active()
            || presentationState_.menuOpen();
    }

    EditorCommandOutcome newScene(bool discardChanges);
    EditorCommandOutcome duplicateScene();
    EditorCommandOutcome exportScene(std::string_view relativeDirectory);
    EditorCommandOutcome sceneExportTaskSnapshot(std::uint64_t taskId) const;
    EditorCommandOutcome completeSceneExport();
    static bool prepareProjectScene(
        EditorProjectResources&, EditorSceneDocument&, core::pmr::String& diagnostic);
    bool publishProject(EditorProjectResources&& resources, ProjectBrowserModel&& browser);
    void appendProjectSnapshot(EditorStateSnapshot& snapshot) const;
    void pollScenePersistence();
    bool observeSceneHash(core::pmr::String&, core::pmr::String&) const;
    EditorCommandOutcome completeScenePersistence(std::uint64_t taskId);
    bool sceneFileContextCurrent(const std::filesystem::path&,
        std::string_view projectModel,
        std::string_view manifestBytes,
        core::pmr::String& diagnostic) const;
    EditorCommandOutcome scenePersistenceTaskSnapshot(std::uint64_t taskId) const;
    EditorCommandOutcome beginScenePersistence(bool loading,
        std::string_view relativePath,
        bool discardChanges = false,
        bool overwriteExisting = false);
    void moveScriptCursorVertical(int direction);
    EditorCommandOutcome insertScriptText(const char* text, std::size_t size);
    EditorCommandOutcome replaceScriptRange(
        std::size_t start, std::size_t length, const char* text, std::size_t size);
    void moveScriptCursor(std::size_t cursor, bool extendSelection);
    std::size_t scriptOffsetAt(float x, float y, const ui::LayoutRect& viewport) const noexcept;
    void clampScriptScroll();
    std::size_t scriptVisibleRows() const;
    void clampScriptScroll(std::size_t visibleRows) noexcept;
    EditorCommandOutcome replaceScriptText(const ReplaceScriptTextCommand& command);
    bool initializeScriptPresentation();
    bool publishScriptPresentation(core::pmr::String message, bool chrome = false) noexcept;
    void setScriptPresentationFailed(bool failed) noexcept;
    void feedback(core::pmr::String message);
    ui::UiArpuiRuntimeLoadResult loaded_;
    core::LifetimeEpoch recoveryOwnerEpoch_;
    core::LifetimeEpoch sceneHistoryEpoch_;
    EditorCommandDispatcher commandDispatcher_;
    std::uint64_t nextCommandSequence_ = 1;
    ProjectBrowserModel projectBrowser_;
    ui::LayoutRect gameViewportRect_;
    std::size_t fileScrollOffset_ = 0;
    bool filesPresentationFailed_ = false;
    std::array<ui::WidgetId, layout::kFileSlotCount * 3 + 3> filePresentationWidgets_{};
    std::size_t filePresentationWidgetCount_ = 0;
    ui::WidgetId filePresentationFailure_, filePresentationRetry_;
    std::array<ui::WidgetId, 2> resourceNavigationNotice_{};
    std::filesystem::path selectedFilePath_;
    std::filesystem::path lastClickedScriptPath_;
    std::chrono::steady_clock::time_point lastScriptClickTime_{};
    core::pmr::String emptyScriptText_{&core::defaultCpuMemoryResource()};
    using ScriptDocument = ScriptDocumentSession::Document;
    ScriptDocumentSession scriptDocuments_;
    bool scriptFocused_ = false;
    EditorModifierState modifiers_;
    bool scriptDraggingSelection_ = false;
    bool fileDraggingScrollbar_ = false;
    bool scriptDraggingScrollbar_ = false;
    bool scriptPointerSuppressRelease_ = false;
    bool sceneHierarchyDraggingScrollbar_ = false;
    float fileScrollbarGrabOffset_ = 0.0f;
    float scriptScrollbarGrabOffset_ = 0.0f;
    float sceneHierarchyScrollbarGrabOffset_ = 0.0f;
    std::size_t lastScriptClickOffset_ = 0;
    std::chrono::steady_clock::time_point lastEditorClickTime_{};
    EditorPresentationState presentationState_;
    EditorProjectPanel projectPanel_;
    EditorScenePanel scenePanel_;
    EditorSceneTransformPanel sceneTransformPanel_;
    EditorScriptFilePanel scriptFilePanel_;
    core::pmr::String pendingScriptFeedback_{&core::defaultCpuMemoryResource()};
    bool scriptChromePending_ = false;
    bool scriptPresentationFailed_ = false;
    ui::WidgetId scriptPresentationNotice_, scriptPresentationRetry_;
    ui::WidgetId scriptPresentationStatus_, scriptPresentationOutput_;
    EditorExitPanel exitPanel_;
    bool exitRequested_ = false;
    std::uint64_t feedbackVersion_ = 0;
    core::pmr::String sceneHierarchySearch_{&core::defaultCpuMemoryResource()};
    core::pmr::String sceneDockTab_{"Scene", &core::defaultCpuMemoryResource()};
    core::pmr::Vector<scene::GameObjectId> sceneHierarchyCollapsedObjects_{
        &core::defaultCpuMemoryResource()};
    std::size_t sceneHierarchyScrollOffset_ = 0u;
    EditorProjectResources projectResources_;
    core::io::FileService* projectSourceFiles_ = nullptr;
    core::PoolHandle projectSourceMount_;
    core::pmr::String projectSourceAlias_{&core::defaultCpuMemoryResource()};
    EditorProjectIoBudget projectIoBudget_;
    core::UniquePtr<EditorProjectIoTask> projectIo_;
    core::MonotonicIdSequence projectIoIds_;
    std::uint64_t projectIoTaskId_ = 0;
    std::uint64_t projectIoSceneEpoch_ = 0;
    std::uint64_t projectIoResourceRevision_ = 0;
    core::LifetimeEpoch::Ticket projectIoScripts_;
    core::pmr::String projectIoSceneHash_{&core::defaultCpuMemoryResource()};
    ProjectBrowserModel::SourceNavigation projectIoNavigation_ =
        ProjectBrowserModel::SourceNavigation::Navigate;
    core::SharedPtr<EditorSceneServiceSession> sceneServiceSession_;
    std::optional<ProjectManifest> projectManifest_;
    bool projectManifestDirty_ = false;
    core::pmr::String projectDiagnostic_;
    core::io::AtomicFileService projectFiles_{1, EditorProjectResources::maxManifestBytes};
    core::PoolHandle projectSaveRequest_;
    std::optional<core::io::AtomicFileResult> projectSaveResult_;
    std::optional<ProjectManifest> projectSaveManifest_;
    core::pmr::String projectSaveSnapshot_;
    std::uint32_t sceneAuthoringVersion_ = EditorSceneAuthoringCodec::contractVersion;
    core::pmr::String savedSceneHash_;
    core::pmr::String activeScenePath_;
    std::filesystem::path savedSceneFileTarget_;
    core::pmr::Vector<std::uint8_t> savedSceneFileBytes_;
    std::uint64_t sceneDocumentEpoch_ = 1;
    std::uint64_t resourceRevision_ = 1;
    EditorTerrainBuild terrainBuild_;
    EditorSceneExport sceneExport_;
    EditorTerrainBrowser terrainBrowser_;
    std::size_t terrainBrowserSelection_ = 0;
    core::pmr::Vector<ui::WidgetId> terrainBrowserWidgets_;
    // Resource-backed scene components are destroyed before their ResourceManager owner.
    EditorSceneDocument sceneDocument_;
    EditorSceneViewState sceneViewState_;

    struct ScenePersistenceTaskRecord {
        ScenePersistenceTaskRecord() : diagnostic(&core::defaultCpuMemoryResource()) {}

        std::uint64_t id = 0;
        EditorScenePersistence::RequestHandle request;
        ScenePersistenceTaskStatus status = ScenePersistenceTaskStatus::Unknown;
        bool loading = false;
        bool checkingFile = false;
        bool committed = false;
        std::uint32_t version = EditorSceneAuthoringCodec::contractVersion;
        core::LifetimeEpoch::Ticket owner;
        std::filesystem::path projectRoot;
        core::pmr::String projectModel, manifestBytes;

        std::uint64_t documentEpoch = 0;
        core::pmr::String contentHash;
        core::pmr::String relativePath;
        std::filesystem::path resolvedTarget;
        core::pmr::String saveYaml;
        core::pmr::String diagnostic;
        std::optional<core::io::AtomicFileResult> result;
    };

    EditorScenePersistence scenePersistence_;
    std::array<ScenePersistenceTaskRecord, 32> scenePersistenceTasks_;
    std::uint64_t nextScenePersistenceTaskId_ = 1;
    enum class SceneViewportGesture : std::uint8_t {
        None,
        Orbit,
        Look,
        Pan,
        BoxSelect,
        TransformAxis
    };
    enum class SceneGizmoAxis : std::uint8_t { None, X, Y, Z };
    EditorSceneViewportCamera sceneViewportCamera_;
    SceneViewportGesture sceneViewportGesture_ = SceneViewportGesture::None;
    core::pmr::Vector<scene::GameObjectId> sceneViewportAdditionalSelection_{
        &core::defaultCpuMemoryResource()};
    bool sceneViewportRightButtonHeld_ = false;
    core::Vec3 sceneViewportOrbitPivot_{};
    float sceneViewportBoxStartX_ = 0.0f;
    float sceneViewportBoxStartY_ = 0.0f;
    float sceneViewportBoxEndX_ = 0.0f;
    float sceneViewportBoxEndY_ = 0.0f;
    SceneTransformTool sceneTransformTool_ = SceneTransformTool::Select;
    EditorSceneTransformSpace sceneTransformSpace_ = EditorSceneTransformSpace::World;
    EditorSceneTransformSnap sceneTransformSnap_;
    EditorSceneTransformPivot sceneTransformPivot_ = EditorSceneTransformPivot::Active;
    bool sceneTransformGizmoViewValid_ = false;
    core::Vec3 sceneTransformGizmoOrigin_;
    EditorSceneTransformFrame sceneTransformGizmoFrame_;
    const char* sceneTransformGizmoDiagnostic_ = nullptr;
    SceneGizmoAxis sceneGizmoAxis_ = SceneGizmoAxis::None;
    scene::GameObjectId sceneGizmoObject_;
    scene::Transform sceneGizmoStartLocal_, sceneGizmoParentWorld_, sceneGizmoLastLocal_;
    scene::GameObjectId sceneGizmoParentId_;
    std::uint64_t sceneGizmoDocumentEpoch_ = 0;
    core::LifetimeEpoch::Ticket sceneGizmoHistoryEpoch_;
    std::uint64_t sceneGizmoBatchEditId_ = 0;
    core::Vec3 sceneGizmoScreenAxis_{};
    EditorSceneGestureAccumulator sceneGizmoAccumulator_;
    float sceneGizmoScreenOrientationSign_ = 1.0f;
    scene::GameObjectId hierarchyDragObject_;
    scene::GameObjectId hierarchyDragTarget_;
    float hierarchyDragStartX_ = 0.0f;
    float hierarchyDragStartY_ = 0.0f;
    bool hierarchyDragging_ = false;
    EditorSceneReferenceDrag sceneReferenceDrag_;
    EditorSceneViewPointer sceneViewPointer_;
    bool platformPointerConsumed_ = false;
    core::pmr::Vector<ui::WidgetId> sceneInspectorWidgetIds_{&core::defaultCpuMemoryResource()};
    EditorScriptInspector scriptInspector_;
    core::pmr::Vector<ui::WidgetId> staticInspectorWidgetIds_{&core::defaultCpuMemoryResource()};

    struct CollapsedInspectorComponent {
        std::uint64_t objectId = 0u;
        std::uint64_t componentId = 0u;
    };

    core::pmr::Vector<CollapsedInspectorComponent> collapsedInspectorComponents_{
        &core::defaultCpuMemoryResource()};
    scene::GameObjectId collapsedInspectorOwner_;
    scene::GameObjectId sceneInspectorWidgetOwner_;
    InspectorState sceneInspectorState_ = InspectorState::Pending;
    const char* sceneInspectorFailure_ = "";
    scene::GameObjectId sceneInspectorAttemptOwner_;

    // Fixed authoring-budget keys remain publishable even under persistent OOM.
    struct InspectorSelectionKey {
        scene::GameObjectId primary;
        std::array<scene::GameObjectId, EditorSceneAuthoringCodec::maxObjects> additional{};
        std::size_t count = 0;

        bool capture(scene::GameObjectId object, core::ArrayView<scene::GameObjectId> rest) noexcept
        {
            primary = object;
            count = rest.size();
            if (rest.size() >= additional.size()) {
                primary = {};
                return false;
            }
            if (!rest.empty())
                std::copy(rest.begin(), rest.end(), additional.begin());
            return true;
        }

        bool matches(
            scene::GameObjectId object, core::ArrayView<scene::GameObjectId> rest) const noexcept
        {
            return primary == object && count == rest.size() && count < additional.size()
                && (rest.empty() || std::equal(rest.begin(), rest.end(), additional.begin()));
        }

        bool matches(const InspectorSelectionKey& other) const noexcept
        {
            return matches(other.primary, {other.additional.data(), other.count});
        }
    };

    InspectorSelectionKey sceneInspectorSelectionKey_, sceneInspectorAttemptKey_;
    core::LifetimeEpoch::Ticket sceneInspectorSceneTicket_;
    // Bounded view feedback, scoped to the selected object and exact Scene epoch.
    scene::GameObjectId sceneInspectorReferenceFailureOwner_;
    core::LifetimeEpoch::Ticket sceneInspectorReferenceFailureTicket_;
    ui::WidgetId sceneInspectorContent_, sceneInspectorPending_, sceneInspectorFailed_,
        sceneInspectorRetry_, sceneInspectorAdd_;
    ui::UiDiagnostic diagnostic_;
    GamePreviewModel gamePreview_;

    struct SceneHistoryEntry {
        SceneHistoryEntry() : beforeProperty(""), afterProperty("") {}

        EditorScenePresence::Cost charge() const noexcept;

        enum class Kind : std::uint8_t {
            Transform,
            TransformBatch,
            Reparent,
            Property,
            ComponentPresence,
            ObjectPresence,
            ScriptFields,
            Name,
            Active
        };
        Kind kind = Kind::Transform;
        core::pmr::Vector<EditorSceneTransformBatch::Change> transforms{
            &core::defaultCpuMemoryResource()};
        core::LifetimeEpoch::Ticket transformSceneOwner;
        std::uint64_t objectId = 0;
        std::uint64_t componentId = 0;
        bool componentPresentBefore = false;
        bool componentPresentAfter = false;
        core::pmr::String componentType{&core::defaultCpuMemoryResource()};
        core::pmr::String beforeName{&core::defaultCpuMemoryResource()};
        core::pmr::String afterName{&core::defaultCpuMemoryResource()};
        bool beforeActive = true;
        bool afterActive = true;
        core::SharedPtr<const EditorScenePresence::Component> componentSnapshot;
        core::SharedPtr<const EditorScenePresence::Subtree> subtree;
        core::SharedPtr<const EditorScriptComponentState> beforeScriptFields, afterScriptFields;
        bool objectPresentBefore = false, objectPresentAfter = false;
        std::uint64_t selectionBefore = 0, selectionAfter = 0;
        core::pmr::Vector<scene::GameObjectId> additionalSelectionBefore, additionalSelectionAfter;
        EditorScenePresence::Cost cost{sizeof(SceneHistoryEntry)};
        scene::Transform beforeTransform;
        scene::Transform afterTransform;
        std::uint64_t beforeParentId = 0;
        std::uint64_t afterParentId = 0;
        std::uint32_t beforeChildIndex = 0;
        std::uint32_t afterChildIndex = 0;
        SetSceneComponentPropertyCommand beforeProperty;
        SetSceneComponentPropertyCommand afterProperty;
    };

    core::pmr::Vector<SceneHistoryEntry> sceneUndoHistory_{&core::defaultCpuMemoryResource()};
    const std::thread::id sceneAuthoringThread_ = std::this_thread::get_id();
    core::pmr::Vector<SceneHistoryEntry> sceneRedoHistory_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<PreparedSceneTransform> activeSceneBatchTransform_;
    core::MonotonicIdSequence sceneBatchTransformIds_;

    // Prepared before publishing Begin. No history owner or capacity is created
    // after a live pose update; the private token never enters engine Scene.
    struct SingleSceneTransformEdit {
        SceneHistoryEntry entry;
        core::pmr::Vector<SceneHistoryEntry> undo{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<SceneHistoryEntry> retiredRedo{&core::defaultCpuMemoryResource()};
        core::LifetimeEpoch::Ticket sceneOwner, historyOwner;
        scene::GameObjectId parent;
        scene::Transform parentWorld;
        std::uint64_t id = 0, documentEpoch = 0;
    };

    core::UniquePtr<SingleSceneTransformEdit> activeSingleSceneTransform_;
    core::MonotonicIdSequence sceneSingleTransformIds_;
    std::uint64_t sceneGizmoSingleEditId_ = 0;
    std::uint64_t activeTransformEditObjectId_ = 0;
    bool applyingSceneHistory_ = false;
    float inspectorContentHeight_ = 0.0f;
    float codeColumnWidth_ = 9.0f;
    float codeCjkColumnWidth_ = 18.0f;
};
} // namespace arp::editor
