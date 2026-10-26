#pragma once

#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_recovery_admission.h"
#include "editor/workspace/editor_recovery_codec.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/script_document_session.h"

namespace arp::editor {

// Preparation is read-only and confined to the Scene owner thread. No live
// workspace, persistence save point, viewport or GPU owner is published here.
class EditorRecoveryPreparation final {
public:
    // Explicit initial recovery preparation limits, independent of the YAML
    // record budget. Package closures may declare smaller limits themselves.
    static constexpr std::size_t maxPackageFiles = ProjectPackageSnapshot::maxPackageFiles;
    static constexpr std::size_t maxPackageFileBytes = ProjectPackageSnapshot::maxPackageFileBytes;
    static constexpr std::size_t maxPackageBytes = ProjectPackageSnapshot::maxPackageBytes;
    static constexpr std::size_t maxAllPackageBytes = ProjectPackageSnapshot::maxAllPackageBytes;
    static constexpr std::size_t maxAllDecodedBytes = ProjectPackageSnapshot::maxAllDecodedBytes;

    static constexpr std::size_t maxObservedDocumentBytes =
        EditorSceneAuthoringCodec::maxDocumentBytes
        + EditorRecoveryBudget{}.maxScripts * ScriptDocumentSession::maxScriptBytes;

    enum class ConflictKind { Changed, Missing };

    struct FileConflict {
        core::pmr::String relativePath;
        ConflictKind kind = ConflictKind::Changed;
    };

    struct FormalFileObservation {
        core::pmr::String relativePath;
        std::filesystem::path target;
        // null means missing, distinct from a present empty file.
        std::optional<core::pmr::String> bytes;
    };

    struct Candidate {
        EditorRecoverySnapshot submitted;
        std::filesystem::path root;
        core::pmr::Vector<FileConflict> conflicts;
        core::pmr::Vector<FormalFileObservation> files;
        std::filesystem::path sceneTarget;
        // Normal retirement destroys the document first. Frozen callbacks also
        // retain the exact service owners if the document is moved out.
        EditorProjectResources resources;
        core::UniquePtr<EditorSceneDocument> document;
        // Provisional cold documents have id=0 and no Undo. The eventual live
        // owner must allocate fresh IDs; replacing its sequence is forbidden.
        core::pmr::Vector<ScriptDocumentSession::Document> scripts;
    };

    // Values needed by the future workspace commit are allocated and parsed
    // before any live mutation. Only the original preparation may fill them.
    class WorkspaceState final {
    public:
        const std::optional<ProjectManifest>& projectDraft() const noexcept
        {
            return projectDraft_;
        }

        bool projectDirty() const noexcept { return projectDirty_; }

        const core::pmr::String& projectSavedBytes() const noexcept { return projectSavedBytes_; }

        const core::pmr::String& scenePath() const noexcept { return scenePath_; }

        const std::filesystem::path& sceneTarget() const noexcept { return sceneTarget_; }

        const core::pmr::String& sceneSavedHash() const noexcept { return sceneSavedHash_; }

        const core::pmr::Vector<std::uint8_t>& sceneSavedBytes() const noexcept
        {
            return sceneSavedBytes_;
        }

        std::size_t activeScriptIndex() const noexcept { return activeScriptIndex_; }

        const core::pmr::String& workspace() const noexcept { return workspace_; }

    private:
        friend class EditorRecoveryPreparation;
        std::optional<ProjectManifest> projectDraft_;
        bool projectDirty_ = false;
        core::pmr::String projectSavedBytes_;
        core::pmr::String scenePath_;
        std::filesystem::path sceneTarget_;
        core::pmr::String sceneSavedHash_;
        core::pmr::Vector<std::uint8_t> sceneSavedBytes_;
        std::size_t activeScriptIndex_ = ScriptDocumentSession::noActiveDocument;
        core::pmr::String workspace_;
    };

    class WorkspaceCandidate final {
    public:
        WorkspaceCandidate() = default;
        WorkspaceCandidate(const WorkspaceCandidate&) = delete;
        WorkspaceCandidate& operator=(const WorkspaceCandidate&) = delete;
        WorkspaceCandidate(WorkspaceCandidate&&) = delete;
        WorkspaceCandidate& operator=(WorkspaceCandidate&&) = delete;

        // Inspection/mutation cannot rebind the private admission or reseal
        // prepared content. A changed candidate fails delayed validation.
        core::UniquePtr<Candidate> recovery;

        const WorkspaceState* preparedState() const noexcept { return state_.get(); }

    private:
        friend class EditorRecoveryPreparation;
        core::UniquePtr<EditorRecoveryAdmission::Ticket> admission_;
        core::UniquePtr<WorkspaceState> state_;
        EditorRecoveryBudget budget_;
        core::ByteBuffer fingerprint_;
        core::LifetimeEpoch::Ticket scene_;
        std::weak_ptr<resource::ResourceManager> resources_;
        std::weak_ptr<physics2d::PhysicsWorld2D> physics_;
        bool hasPhysics_ = false;
    };

    // Shared producer/verifier of v1 package binding fingerprints. Only files
    // explicitly admitted by the existing package closure service are read.
    static bool capturePackages(const std::filesystem::path& admittedRoot,
        std::string_view manifestYaml,
        core::pmr::Vector<EditorRecoveryPackage>& destination,
        core::pmr::String& diagnostic);
    // Capture only the inputs of an already-loaded owner. If a module source
    // has since been saved, the owner must explicitly reload before recovery;
    // current files may not manufacture provenance for old resource contents.
    static bool captureInputs(const std::filesystem::path& admittedRoot,
        const EditorProjectResources& resources,
        EditorRecoveryProject& destination,
        core::pmr::String& diagnostic);
    // Read-only precommit file freshness, not a live workspace/history/identity
    // or GPU retirement gate. Successful comparisons are not a filesystem CAS.
    static bool validateFiles(const Candidate& candidate,
        const std::filesystem::path& currentRoot,
        core::pmr::String& diagnostic);
    static core::UniquePtr<Candidate> prepare(const EditorRecoverySnapshot& snapshot,
        const std::filesystem::path& admittedRoot,
        core::pmr::String& diagnostic,
        EditorRecoveryBudget budget = {});
    // Combined Editor-state and cold-file preparation; still no workspace/GPU
    // publication or complete ResourceManager inventory proof. A delayed
    // consumer must revalidate both observations through this entry.
    static core::UniquePtr<WorkspaceCandidate> prepareForWorkspace(
        const EditorRecoverySnapshot& snapshot,
        const EditorShell& shell,
        bool discardChanges,
        core::pmr::String& diagnostic,
        EditorRecoveryBudget budget = {},
        EditorRecoveryAdmissionBudget admissionBudget = {});
    static bool validateForWorkspace(const WorkspaceCandidate& candidate,
        const EditorShell& shell,
        core::pmr::String& diagnostic);

private:
    static core::UniquePtr<WorkspaceState> prepareState(
        const Candidate& candidate, core::pmr::String& diagnostic);
    static bool candidateFingerprint(const Candidate& candidate,
        EditorRecoveryBudget budget,
        core::ByteBuffer& destination,
        core::pmr::String& diagnostic);
};

} // namespace arp::editor
