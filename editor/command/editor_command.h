#pragma once
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "editor/project/project_manifest.h"
#include "editor/viewport/editor_scene_transform_batch.h"
#include "editor/viewport/editor_scene_transform_frame.h"
#include "editor/viewport/editor_scene_transform_settings.h"
#include "editor/viewport/editor_scene_transform_snap.h"
#include "editor/workspace/editor_scene_export.h"
#include "editor/workspace/editor_scene_inspector.h"
#include "editor/workspace/editor_scene_inspector_selection.h"
#include "editor/workspace/editor_scene_reference_assignment.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

namespace arp::editor {
enum class EditorCommandOrigin : std::uint8_t { LocalUi, PlatformInput, Mcp, HttpRest, Internal };

struct ActivateControlCommand {
    explicit ActivateControlCommand(std::string_view source,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : source(source, resource)
    {}

    core::pmr::String source;
};

struct SetTextValueCommand {
    SetTextValueCommand(std::string_view source,
        std::string_view value,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : source(source, resource), value(value, resource)
    {}

    core::pmr::String source;
    core::pmr::String value;
};

enum class EditorInputKind : std::uint8_t {
    KeyDown,
    KeyUp,
    PointerDown,
    PointerUp,
    PointerMoved,
    PointerDelta,
    Wheel,
    Text,
    FocusLost
};
enum class EditorPointerButton : std::uint8_t { None, Left, Middle, Right, Auxiliary };
enum class EditorInputKey : std::uint8_t {
    Other,
    A,
    C,
    D,
    E,
    Q,
    R,
    S,
    V,
    W,
    X,
    Y,
    Z,
    Backspace,
    Delete,
    Enter,
    NumpadEnter,
    Tab,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    Home,
    End,
    PageUp,
    PageDown,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    F,
    Escape
};

struct EditorInputCommand {
    EditorInputKind kind = EditorInputKind::PointerDelta;
    EditorInputKey key = EditorInputKey::Other;
    EditorPointerButton button = EditorPointerButton::None;
    bool primaryButton = false;
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    float wheelDelta = 0.0f;
    core::pmr::String text{&core::defaultCpuMemoryResource()};
};

struct SetInspectorScrollCommand {
    float offset = 0.0f;
};

struct DragInspectorThumbCommand {
    float top = 0.0f;
};

struct SetGamePreviewStateCommand {
    bool running = false;
    bool starting = false;
    core::pmr::String status{&core::defaultCpuMemoryResource()};

    explicit SetGamePreviewStateCommand(std::string_view statusText,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : status(statusText, resource)
    {}
};

struct SelectSceneObjectCommand {
    std::uint64_t objectId = 0;
};

// Full local flags, scoped to the exact authoring document. Inheritance is
// observed from current parents; setting false never overrides an ancestor.
struct SetSceneObjectViewCommand {
    std::uint64_t objectId = 0;
    std::uint64_t documentEpoch = 0;
    bool hidden = false;
    bool locked = false;
};

struct FocusSceneSelectionCommand {};

// Source identity is document-local; resolve the current field on the owner thread.
struct FocusSceneReferenceCommand {
    std::uint64_t objectId = 0;
    std::uint64_t componentId = 0;
    std::uint64_t documentEpoch = 0;
    core::pmr::String property{&core::defaultCpuMemoryResource()};
};

struct LocateSceneResourceCommand {
    std::uint64_t objectId = 0;
    std::uint64_t componentId = 0;
    std::uint64_t documentEpoch = 0;
    std::uint64_t resourceRevision = 0;
    core::pmr::String property{&core::defaultCpuMemoryResource()};
};

// Explicit single-field assignment; source and target are document-local values.
struct AssignSceneReferenceCommand {
    EditorSceneReferenceAssignment::Intent intent;
};

struct SetSceneTransformSpaceCommand {
    EditorSceneTransformSpace space = EditorSceneTransformSpace::World;
};

struct SetSceneTransformSnapCommand {
    EditorSceneTransformSnap settings;
};

struct SetSceneTransformPivotCommand {
    EditorSceneTransformPivot pivot = EditorSceneTransformPivot::Active;
};

struct SetSceneTransformSettingsCommand {
    EditorSceneTransformSettings settings;
};

struct GetSceneTransformSettingsQuery {};

// One relative operation on the complete current selection, one history entry.
// Amount is a world distance, radians, or the explicit scale delta (B4 contract).
struct TransformSceneSelectionCommand {
    EditorSceneTransformTool tool = EditorSceneTransformTool::Move;
    EditorSceneTransformSpace space = EditorSceneTransformSpace::World;
    EditorSceneTransformPivot pivot = EditorSceneTransformPivot::Active;
    unsigned axis = 0;
    double amount = 0;
};

// Internal authoring gesture commands. Begin requires amount=0; Update amount
// is measured from that frozen start, never from the last accepted pose.
struct BeginSceneSelectionTransformEditCommand {
    TransformSceneSelectionCommand operation;
};

struct UpdateSceneSelectionTransformEditCommand {
    std::uint64_t gestureId = 0;
    double amount = 0;
};

struct EndSceneSelectionTransformEditCommand {
    std::uint64_t gestureId = 0;
};

struct SelectSceneObjectsCommand {
    explicit SelectSceneObjectsCommand(
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : objectIds(resource)
    {}

    core::pmr::Vector<std::uint64_t> objectIds;
    std::uint64_t primaryObjectId = 0;
};

struct ReparentSceneObjectCommand {
    std::uint64_t objectId = 0;
    std::uint64_t parentId = 0;
    std::uint32_t childIndex = 0;
};

enum class SceneViewportCameraOperation : std::uint8_t {
    Orbit,
    OrbitAround,
    Look,
    Pan,
    Move,
    Zoom
};

struct SceneViewportCameraCommand {
    SceneViewportCameraOperation operation = SceneViewportCameraOperation::Orbit;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    core::Vec3 pivot{};
};

struct CreateSceneObjectCommand {
    explicit CreateSceneObjectCommand(std::string_view value,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : name(value, resource)
    {}

    core::pmr::String name;
};

struct DestroySceneObjectCommand {};

struct SetSceneObjectNameCommand {
    explicit SetSceneObjectNameCommand(std::string_view value,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : name(value, resource)
    {}

    core::pmr::String name;
    // Zero preserves Inspector selection targeting; remote edits require an ID.
    std::uint64_t objectId = 0;
};

struct SetSceneObjectTranslationCommand {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct SetSceneObjectActiveCommand {
    std::uint64_t objectId = 0;
    bool active = true;
};

struct SetSceneObjectTransformCommand {
    // Zero preserves the UI/REST compatibility behavior of targeting selection.
    std::uint64_t objectId = 0;
    core::Vec3 translation{};
    core::Quat rotation{};
    core::Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct BeginSceneTransformEditCommand {
    std::uint64_t objectId = 0;
};

struct EndSceneTransformEditCommand {
    std::uint64_t objectId = 0;
};

struct UndoSceneCommand {};

struct RedoSceneCommand {};

struct RemoveSceneComponentCommand {
    // Zero IDs retain the UI behavior of removing the last removable component
    // from the current selection. Remote commands must provide both IDs.
    std::uint64_t objectId = 0;
    std::uint64_t componentId = 0;
};

enum class ScenePropertyValueKind : std::uint8_t {
    Boolean,
    Integer,
    UnsignedInteger,
    Number,
    Enumeration,
    Vector2,
    Vector2List,
    SceneObjectLinkList,
    Vector3,
    Color4,
    String,
    ResourceReference,
    SceneObjectReference
};

struct SetSceneComponentPropertyCommand {
    explicit SetSceneComponentPropertyCommand(std::string_view propertyName,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : name(propertyName, resource), stringValue(resource), vector2ListValue(resource),
          sceneObjectLinkListValue(resource)
    {}

    std::uint64_t objectId = 0;
    std::uint64_t componentId = 0;
    core::pmr::String name;
    core::pmr::String stringValue;
    ScenePropertyValueKind valueKind = ScenePropertyValueKind::Number;
    bool booleanValue = false;
    int integerValue = 0;
    std::uint64_t unsignedIntegerValue = 0;
    std::uint64_t sceneObjectReferenceValue = 0;
    double numberValue = 0.0f;
    core::Vec2 vector2Value{};
    core::pmr::Vector<core::Vec2> vector2ListValue{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneInspectorObjectLink> sceneObjectLinkListValue{
        &core::defaultCpuMemoryResource()};
    core::Vec3 vector3Value{};
    core::Vec4 color4Value{};
};

struct AddSceneComponentCommand {
    explicit AddSceneComponentCommand(std::string_view value,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : type(value, resource), initialProperties(resource)
    {}

    core::pmr::String type;
    // Zero retains the UI behavior of targeting the current selection.
    std::uint64_t objectId = 0;
    // Optional typed properties are applied before the add command commits.
    core::pmr::Vector<SetSceneComponentPropertyCommand> initialProperties;
};

struct UndoScriptCommand {
    std::uint64_t documentId = 0;
};

struct RedoScriptCommand {
    std::uint64_t documentId = 0;
};

struct ReplaceScriptTextCommand {
    ReplaceScriptTextCommand(std::uint64_t document,
        std::string_view value,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : documentId(document), text(value, resource)
    {}

    std::uint64_t documentId = 0;
    core::pmr::String text;
};

struct SaveScriptCommand {
    std::uint64_t documentId = 0;
};

// Explicit disposal of one document; never writes or deletes the source file.
struct CloseScriptCommand {
    std::uint64_t documentId = 0;
    bool discardChanges = false;
};

// Exit acceptance is terminal for this Editor session; no files are written.
struct RequestEditorExitCommand {
    bool discardChanges = false;
};

struct ReloadScriptCommand {
    std::uint64_t documentId = 0;
    bool discardChanges = false;
};

// Explicit authoring identity; creation never invents engine compatibility.
struct CreateProjectCommand {
    core::pmr::String root;
    ProjectManifest manifest;
    bool discardChanges = false;
    ProjectTemplateKind templateKind = ProjectTemplateKind::Empty;
};

struct OpenProjectCommand {
    core::pmr::String root;
    bool discardChanges = false;
};

struct CloseProjectCommand {
    bool discardChanges = false;
};

struct SaveProjectCommand {};

struct CompleteProjectSaveCommand {};

struct RenameProjectCommand {
    core::pmr::String name;
};

// One atomic authoring draft edit; resource/physics ownership remains unchanged.
struct SetProjectSettingsCommand {
    core::pmr::String name;
    core::pmr::String entryScene;
};

struct NewSceneCommand {
    bool discardChanges = false;
};

// Adopt the entire current draft as a new, unsaved scene resource. No IO.
struct DuplicateSceneCommand {};

struct ExportSceneCommand {
    core::pmr::String relativeDirectory;
};

struct GetSceneExportTaskQuery {
    std::uint64_t taskId = 0;
};

// Internal completion cannot be forged without an actual completed worker job.
struct CompleteSceneExportCommand {};

struct SaveSceneCommand {
    explicit SaveSceneCommand(std::string_view path,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : relativePath(path, resource)
    {}

    core::pmr::String relativePath;
    // Saving to the active path is already explicit; other existing targets need consent.
    bool overwriteExisting = false;
};

struct LoadSceneCommand {
    explicit LoadSceneCommand(std::string_view path,
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : relativePath(path, resource)
    {}

    core::pmr::String relativePath;
    bool discardChanges = false;
};

enum class ScenePersistenceTaskStatus : std::uint8_t { Unknown, Pending, Succeeded, Failed };

struct GetScenePersistenceTaskQuery {
    std::uint64_t taskId = 0;
};

// Only an actual private IO result may complete the referenced task.
struct CompleteScenePersistenceCommand {
    std::uint64_t taskId = 0;
};

struct GetEditorStateQuery {};

struct GetSceneInspectorQuery {
    // Zero means the current scene selection.
    std::uint64_t objectId = 0;
};

struct GetSceneHierarchyQuery {};

// Independently observes the complete current selection; never edits primary.
struct GetSceneInspectorSelectionQuery {};

struct GetSceneComponentCatalogQuery {};

struct SceneHierarchySnapshotEntry {
    std::uint64_t objectId = 0;
    std::uint64_t parentId = 0;
    std::uint32_t depth = 0;
    std::uint32_t childIndex = 0;
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    bool hidden = false, locked = false;
    bool effectiveHidden = false, effectiveLocked = false;
};

struct SceneHierarchySnapshot {
    std::uint64_t documentEpoch = 0;
    std::uint64_t selectedObjectId = 0;
    core::pmr::Vector<SceneHierarchySnapshotEntry> entries{&core::defaultCpuMemoryResource()};
};

struct SceneComponentCatalogSnapshotEntry {
    core::pmr::String typeName{&core::defaultCpuMemoryResource()};
    core::pmr::String displayName{&core::defaultCpuMemoryResource()};
};

struct SceneComponentCatalogSnapshot {
    core::pmr::Vector<SceneComponentCatalogSnapshotEntry> entries{
        &core::defaultCpuMemoryResource()};
};

// Project-scoped offline preparation; no scene Undo entry. Completion publishes
// a complete candidate only if the project manifest has not changed.
struct BuildTerrainResourceCommand {
    core::pmr::String authoringPath{&core::defaultCpuMemoryResource()};
    core::pmr::String packagePath{&core::defaultCpuMemoryResource()};
};

struct CompleteTerrainResourceBuildCommand {};

struct ShowTerrainBrowserCommand {
    bool visible = true;
};

struct SelectTerrainPreviewCommand {
    core::pmr::String previewId;
    std::uint64_t expectedGeneration = 0;
};

struct OpenTerrainPreviewCommand {
    core::pmr::String previewId;
    std::uint64_t expectedGeneration = 0;
};

using EditorCommandPayload = std::variant<BuildTerrainResourceCommand,
    CompleteTerrainResourceBuildCommand,
    ShowTerrainBrowserCommand,
    SelectTerrainPreviewCommand,
    OpenTerrainPreviewCommand,
    ActivateControlCommand,
    SetTextValueCommand,
    EditorInputCommand,
    SetInspectorScrollCommand,
    DragInspectorThumbCommand,
    SetGamePreviewStateCommand,
    SelectSceneObjectCommand,
    SetSceneObjectViewCommand,
    SelectSceneObjectsCommand,
    FocusSceneSelectionCommand,
    FocusSceneReferenceCommand,
    LocateSceneResourceCommand,
    AssignSceneReferenceCommand,
    SetSceneTransformSpaceCommand,
    SetSceneTransformSnapCommand,
    SetSceneTransformPivotCommand,
    SetSceneTransformSettingsCommand,
    GetSceneTransformSettingsQuery,
    TransformSceneSelectionCommand,
    BeginSceneSelectionTransformEditCommand,
    UpdateSceneSelectionTransformEditCommand,
    EndSceneSelectionTransformEditCommand,
    ReparentSceneObjectCommand,
    SceneViewportCameraCommand,
    CreateSceneObjectCommand,
    DestroySceneObjectCommand,
    SetSceneObjectNameCommand,
    SetSceneObjectActiveCommand,
    SetSceneObjectTranslationCommand,
    SetSceneObjectTransformCommand,
    BeginSceneTransformEditCommand,
    EndSceneTransformEditCommand,
    UndoSceneCommand,
    RedoSceneCommand,
    AddSceneComponentCommand,
    RemoveSceneComponentCommand,
    SetSceneComponentPropertyCommand,
    UndoScriptCommand,
    RedoScriptCommand,
    ReplaceScriptTextCommand,
    SaveScriptCommand,
    CloseScriptCommand,
    ReloadScriptCommand,
    RequestEditorExitCommand,
    CreateProjectCommand,
    OpenProjectCommand,
    CloseProjectCommand,
    SaveProjectCommand,
    CompleteProjectSaveCommand,
    RenameProjectCommand,
    SetProjectSettingsCommand,
    NewSceneCommand,
    DuplicateSceneCommand,
    ExportSceneCommand,
    GetSceneExportTaskQuery,
    CompleteSceneExportCommand,
    SaveSceneCommand,
    LoadSceneCommand,
    GetScenePersistenceTaskQuery,
    CompleteScenePersistenceCommand,
    GetEditorStateQuery,
    GetSceneInspectorQuery,
    GetSceneInspectorSelectionQuery,
    GetSceneHierarchyQuery,
    GetSceneComponentCatalogQuery>;

struct EditorCommandEnvelope {
    static constexpr std::uint16_t currentVersion = 1;

    explicit EditorCommandEnvelope(EditorCommandPayload payload) : payload(std::move(payload)) {}

    std::uint16_t schemaVersion = currentVersion;
    std::uint64_t commandId = 0;
    std::uint64_t correlationId = 0;
    std::uint64_t sourceSequence = 0;
    EditorCommandOrigin origin = EditorCommandOrigin::Internal;
    bool hasExpectedStateVersion = false;
    std::uint64_t expectedStateVersion = 0;
    core::pmr::String idempotencyKey{&core::defaultCpuMemoryResource()};
    core::pmr::String idempotencyFingerprint{&core::defaultCpuMemoryResource()};
    EditorCommandPayload payload;
};

enum class EditorResponseStatus : std::uint8_t { Completed, Rejected, Failed };

enum class EditorDiagnosticCode : std::uint16_t {
    None,
    UnsupportedCommandVersion,
    WrongOwnerThread,
    StaleStateVersion,
    StaleDocumentTarget,
    UnsupportedIdempotency,
    IdempotencyKeyConflict,
    IdempotencyCapacityExceeded,
    InvalidCommandPayload,
    WorkspaceBusy,
    UnsavedChanges,
    ProjectOperationFailed,
    SceneOverwriteRequired,
    SceneRootActiveUnsupported,
    ScriptExternalConflict,
    ScriptFileOperationFailed,
    CommandPreparationFailed,
    ResponsePayloadUnavailable,
    SceneReferenceUnavailable,
    ResourceDefinitionUnavailable,
    ResourceNavigationPresentationFailed,
    SceneObjectLocked,
    ScriptPresentationFailed,
    ScriptModalPresentationFailed,
    SceneModalPresentationFailed,
    SceneHistoryRestoreRejected,
    SceneHistoryBudgetExceeded
};

struct EditorStateSnapshot {
    EditorSceneTransformSettings sceneTransformSettings;
    core::Vec3 sceneCameraTarget{};
    float sceneCameraYaw = 0;
    float sceneCameraPitch = 0;
    float sceneCameraDistance = 0;
    bool exitRequested = false;
    bool hasProject = false;
    bool projectDirty = false;
    bool sceneDirty = false;
    bool projectSavePending = false;
    bool sceneIoPending = false;
    bool sceneExportPending = false;
    SceneExportResult sceneExportTask;
    core::pmr::String scenePath;
    core::pmr::String sceneRuntimeId;
    core::pmr::String projectId;
    core::pmr::String projectName;
    core::pmr::String projectEntryScene;
    core::pmr::String projectRoot;
    core::pmr::String projectDiagnostic;

    core::pmr::String workspace{&core::defaultCpuMemoryResource()};
    core::pmr::String scene{&core::defaultCpuMemoryResource()};
    core::pmr::String selection{&core::defaultCpuMemoryResource()};
    core::pmr::String scriptText{&core::defaultCpuMemoryResource()};
    core::pmr::String scriptRelativePath{&core::defaultCpuMemoryResource()};
    std::uint64_t activeDocumentId = 0;
    std::uint32_t openDocumentCount = 0;
    bool hasActiveDocument = false;
    bool scriptDirty = false;
    bool scriptPresentationFailed = false;
    std::uint64_t scenePersistenceTaskId = 0;
    ScenePersistenceTaskStatus scenePersistenceTaskStatus = ScenePersistenceTaskStatus::Unknown;
    core::pmr::String scenePersistenceTaskDiagnostic{&core::defaultCpuMemoryResource()};
};

struct EditorCommandOutcome {
    bool success = true;
    bool stateChanged = true;
    EditorDiagnosticCode diagnostic = EditorDiagnosticCode::None;
    bool hasSnapshot = false;
    EditorStateSnapshot snapshot;
    bool hasSceneTransformSettings = false;
    EditorSceneTransformSettings sceneTransformSettings;
    bool hasSceneInspector = false;
    EditorSceneInspectorSnapshot sceneInspector;
    bool hasSceneInspectorSelection = false;
    EditorSceneInspectorSelection::Snapshot sceneInspectorSelection;
    bool hasSceneHierarchy = false;
    SceneHierarchySnapshot sceneHierarchy;
    bool hasSceneComponentCatalog = false;
    SceneComponentCatalogSnapshot sceneComponentCatalog;
};

struct EditorResponse {
    // Preserve a nested dispatch payload without owning a second version change.
    EditorCommandOutcome intoNestedOutcome() && noexcept;

    std::uint16_t schemaVersion = 1;
    std::uint64_t commandId = 0;
    std::uint64_t correlationId = 0;
    std::uint64_t stateVersion = 0;
    bool hasStateVersion = false;
    EditorResponseStatus status = EditorResponseStatus::Failed;
    EditorDiagnosticCode diagnostic = EditorDiagnosticCode::None;
    bool hasSnapshot = false;
    EditorStateSnapshot snapshot;
    bool hasSceneTransformSettings = false;
    EditorSceneTransformSettings sceneTransformSettings;
    bool hasSceneInspector = false;
    EditorSceneInspectorSnapshot sceneInspector;
    bool hasSceneInspectorSelection = false;
    EditorSceneInspectorSelection::Snapshot sceneInspectorSelection;
    bool hasSceneHierarchy = false;
    SceneHierarchySnapshot sceneHierarchy;
    bool hasSceneComponentCatalog = false;
    SceneComponentCatalogSnapshot sceneComponentCatalog;
};

class IEditorCommandExecutor {
public:
    virtual ~IEditorCommandExecutor() = default;
    virtual EditorCommandOutcome execute(const EditorCommandPayload& payload) = 0;
};

class EditorCommandDispatcher final {
public:
    explicit EditorCommandDispatcher(std::size_t idempotencyCapacity = 128);
    EditorResponse dispatch(const EditorCommandEnvelope& command, IEditorCommandExecutor& executor);

    std::uint64_t stateVersion() const noexcept { return stateVersion_; }

private:
    struct IdempotencyRecord {
        explicit IdempotencyRecord(std::pmr::memory_resource* resource)
            : key(resource), fingerprint(resource)
        {}

        core::pmr::String key;
        core::pmr::String fingerprint;
        std::optional<EditorResponse> response;
    };

    std::thread::id ownerThread_ = std::this_thread::get_id();
    std::atomic<std::uint64_t> nextCommandId_{1};
    std::uint64_t stateVersion_ = 0;
    std::size_t idempotencyCapacity_ = 0;
    core::pmr::Vector<core::UniquePtr<IdempotencyRecord>> idempotencyRecords_{
        &core::defaultCpuMemoryResource()};
};
} // namespace arp::editor
