#pragma once

#include "editor/command/editor_command.h"
#include "editor/viewport/editor_scene_viewport_camera.h"
#include "editor/workspace/editor_presentation_state.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_persistence.h"
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
        bool useCommandValue = false)
        : shell_(shell), source_(std::move(source)), options_(std::move(options)),
          useCommandValue_(useCommandValue)
    {}

    ui::UiResult applyDefaultAction(const ui::UiEvent& event, ui::UiEventContext& context) override;
    ui::UiResult applyInteraction(
        const ui::UiControlInteraction& interaction, ui::UiEventContext& context) override;
    void choose(std::uint32_t selected);

protected:
    void onSelectionChanged(std::uint32_t, std::uint32_t selected) override;

private:
    friend class EditorShell;
    void openMenu();
    void closeMenu();
    EditorShell& shell_;
    core::pmr::String source_;
    core::pmr::Vector<SceneInspectorEnumOption> options_;
    bool useCommandValue_ = false;
    core::pmr::Vector<ui::WidgetId> optionWidgets_{&core::defaultCpuMemoryResource()};
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
    ui::UiResult load(const std::filesystem::path& path, std::filesystem::path projectRoot = {});
    EditorResponse submitCommand(EditorCommandPayload payload,
        EditorCommandOrigin origin = EditorCommandOrigin::LocalUi,
        std::uint64_t correlationId = 0);
    EditorResponse submitCommand(EditorCommandEnvelope command);

    std::uint64_t stateVersion() const noexcept { return commandDispatcher_.stateVersion(); }

    EditorCommandOutcome execute(const EditorCommandPayload& payload) override;
    void handlePlatformInput(const platform::WindowInputEvent& event);
    GamePreviewModel::Request consumeGamePreviewCommand();
    void setGamePreviewState(bool running, bool starting, const char* status);

    bool gamePreviewRunning() const { return gamePreview_.running(); }

    bool scriptEditorCapturesKeyboard() const
    {
        return presentationState_.workspace() == "Script" && !scriptDocuments_.activePath().empty();
    }

    bool openScriptFile(const std::filesystem::path& path);

    std::size_t openScriptCount() const { return scriptDocuments_.size(); }

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

    const std::filesystem::path& projectRoot() const { return projectBrowser_.root(); }

    const std::filesystem::path& selectedFilePath() const { return selectedFilePath_; }

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

    ui::WidgetId id(const core::pmr::String& source) const
    {
        return loaded_.widgetIdForSourceNodeId(source);
    }

    const core::pmr::String& selection() const { return presentationState_.selection(); }

    const core::pmr::String& workspace() const { return presentationState_.workspace(); }

    EditorSceneDocument& sceneDocument() noexcept { return sceneDocument_; }

    const EditorSceneDocument& sceneDocument() const noexcept { return sceneDocument_; }

    resource::ResourceManager& projectResources() noexcept { return projectResources_.manager(); }

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
    enum class SceneTransformTool : std::uint8_t { Select, Move, Rotate, Scale };

    SceneTransformTool sceneTransformTool() const noexcept { return sceneTransformTool_; }

    void selectSceneObject(std::uint64_t objectId)
    {
        (void)submitCommand(SelectSceneObjectCommand{objectId});
    }

    void activate(const core::pmr::String& source);
    void entryChanged(const core::pmr::String& source, const core::pmr::String& value);
    void colorEntryChanged(const core::pmr::String& source, const ui::UiControlColorValue& value);
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
    static constexpr std::size_t kMaxSceneHistoryEntries = 256u;
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
    void applyPlatformInput(const EditorInputCommand& command);
    void applyInspectorScrollChanged(float offset);
    void applyDragInspectorThumb(float top);
    void applyGamePreviewState(bool running, bool starting, const core::pmr::String& status);
    EditorCommandOutcome applyScriptHistory(std::uint64_t documentId, bool undo);
    EditorCommandOutcome applySceneHistory(bool undo);
    struct SceneHistoryEntry;
    void recordSceneHistory(const SceneHistoryEntry& entry);
    void text(const core::pmr::String& source, core::pmr::String value);
    void visible(const core::pmr::String& source, bool value);
    void filter(const core::pmr::String& prefix, const core::pmr::String& query);
    core::pmr::Vector<SceneHierarchyEntry> visibleSceneHierarchyEntries() const;
    void refreshFileSystem();
    void updateFileSlots();
    bool navigateTo(const std::filesystem::path& path);
    void updateFilePathText();
    void configureGodotDockLayout();
    void updateWorkspaceChrome();
    void activateScriptDocument(std::size_t index);
    bool closeScriptDocument(std::size_t index);
    void rebuildScriptTabs(core::pmr::Vector<renderer::ui::UiDrawCommand>& commands) const;
    void rebuildScriptCommands(const ui::CanvasLayoutResult& layout,
        core::pmr::Vector<renderer::ui::UiDrawCommand>& commands) const;
    void rebuildSceneCommands(const ui::CanvasLayoutResult& layout,
        core::pmr::Vector<renderer::ui::UiDrawCommand>& commands,
        std::string_view viewportTextureResourceId) const;
    void refreshSceneInspector();
    void pollScenePersistence();
    EditorCommandOutcome scenePersistenceTaskSnapshot(std::uint64_t taskId) const;
    EditorCommandOutcome beginScenePersistence(bool loading, std::string_view relativePath);
    void moveScriptCursorVertical(int direction);
    void insertScriptText(const char* text, std::size_t size);
    void replaceScriptRange(
        std::size_t start, std::size_t length, const char* text, std::size_t size);
    void moveScriptCursor(std::size_t cursor, bool extendSelection);
    std::size_t scriptOffsetAt(float x, float y) const;
    void clampScriptScroll();
    void feedback(core::pmr::String message);
    ui::UiArpuiRuntimeLoadResult loaded_;
    EditorCommandDispatcher commandDispatcher_;
    std::uint64_t nextCommandSequence_ = 1;
    ProjectBrowserModel projectBrowser_;
    ui::LayoutRect gameViewportRect_;
    std::size_t fileScrollOffset_ = 0;
    std::filesystem::path selectedFilePath_;
    std::filesystem::path lastClickedScriptPath_;
    std::chrono::steady_clock::time_point lastScriptClickTime_{};
    core::pmr::String emptyScriptText_{&core::defaultCpuMemoryResource()};
    using ScriptDocument = ScriptDocumentSession::Document;
    ScriptDocumentSession scriptDocuments_;
    bool scriptFocused_ = false;
    bool controlHeld_ = false;
    bool shiftHeld_ = false;
    bool scriptDraggingSelection_ = false;
    bool fileDraggingScrollbar_ = false;
    bool scriptDraggingScrollbar_ = false;
    bool sceneHierarchyDraggingScrollbar_ = false;
    float fileScrollbarGrabOffset_ = 0.0f;
    float scriptScrollbarGrabOffset_ = 0.0f;
    float sceneHierarchyScrollbarGrabOffset_ = 0.0f;
    std::size_t lastScriptClickOffset_ = 0;
    std::chrono::steady_clock::time_point lastEditorClickTime_{};
    EditorPresentationState presentationState_;
    core::pmr::String sceneHierarchySearch_{&core::defaultCpuMemoryResource()};
    core::pmr::String sceneDockTab_{"Scene", &core::defaultCpuMemoryResource()};
    core::pmr::Vector<scene::GameObjectId> sceneHierarchyCollapsedObjects_{
        &core::defaultCpuMemoryResource()};
    std::size_t sceneHierarchyScrollOffset_ = 0u;
    EditorProjectResources projectResources_;
    // Resource-backed scene components are destroyed before their ResourceManager owner.
    EditorSceneDocument sceneDocument_;

    struct ScenePersistenceTaskRecord {
        ScenePersistenceTaskRecord() : diagnostic(&core::defaultCpuMemoryResource()) {}

        std::uint64_t id = 0;
        EditorScenePersistence::RequestHandle request;
        ScenePersistenceTaskStatus status = ScenePersistenceTaskStatus::Unknown;
        bool loading = false;
        core::pmr::String diagnostic;
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
    SceneGizmoAxis sceneGizmoAxis_ = SceneGizmoAxis::None;
    scene::GameObjectId sceneGizmoObject_;
    scene::Transform sceneGizmoStartTransform_;
    core::Vec3 sceneGizmoWorldAxis_{};
    core::Vec3 sceneGizmoScreenAxis_{};
    core::Vec3 sceneGizmoLastPointer_{};
    float sceneGizmoScreenOrientationSign_ = 1.0f;
    scene::GameObjectId hierarchyDragObject_;
    scene::GameObjectId hierarchyDragTarget_;
    float hierarchyDragStartX_ = 0.0f;
    float hierarchyDragStartY_ = 0.0f;
    bool hierarchyDragging_ = false;
    core::pmr::Vector<ui::WidgetId> sceneInspectorWidgetIds_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ui::WidgetId> staticInspectorWidgetIds_{&core::defaultCpuMemoryResource()};

    struct CollapsedInspectorComponent {
        std::uint64_t objectId = 0u;
        std::uint64_t componentId = 0u;
    };

    core::pmr::Vector<CollapsedInspectorComponent> collapsedInspectorComponents_{
        &core::defaultCpuMemoryResource()};
    scene::GameObjectId collapsedInspectorOwner_;
    scene::GameObjectId sceneInspectorWidgetOwner_;
    ui::UiDiagnostic diagnostic_;
    GamePreviewModel gamePreview_;

    struct SceneHistoryEntry {
        SceneHistoryEntry() : beforeProperty(""), afterProperty("") {}

        enum class Kind : std::uint8_t { Transform, Reparent, Property, ComponentPresence };
        Kind kind = Kind::Transform;
        std::uint64_t objectId = 0;
        std::uint64_t componentId = 0;
        bool componentPresentBefore = false;
        bool componentPresentAfter = false;
        core::pmr::String componentType{&core::defaultCpuMemoryResource()};
        reflection::object beforeComponentProperties;
        reflection::object afterComponentProperties;
        reflection::object beforeComponentAuthoringProperties;
        reflection::object afterComponentAuthoringProperties;
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
    core::pmr::Vector<SceneHistoryEntry> sceneRedoHistory_{&core::defaultCpuMemoryResource()};
    std::uint64_t activeTransformEditObjectId_ = 0;
    scene::Transform activeTransformEditStart_;
    bool applyingSceneHistory_ = false;
    float inspectorContentHeight_ = 0.0f;
    float codeColumnWidth_ = 9.0f;
    float codeCjkColumnWidth_ = 18.0f;
};
} // namespace arp::editor
