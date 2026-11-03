#include "editor/workspace/editor_source_recovery_workspace.h"

#include "editor/project/project_source_verification.h"
#include "editor/workspace/editor_recovery_digest.h"
#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_service_session.h"

namespace arp::editor {
struct EditorSourceRecoveryWorkspace::State {
    enum class Step { Resources, Documents, Directory, Inputs, Files, DirectoryCheck };
    core::io::FileService* files = nullptr;
    ProjectSourceLocation location;
    ProjectInputBudget inputs;
    ProjectDirectoryBudget directories;
    EditorRecoveryBudget records;
    core::pmr::String alias, detail;
    const char* issue = nullptr;
    SourceRecoveryWorkspaceState phase = SourceRecoveryWorkspaceState::Pending;
    Step step = Step::Resources;
    core::UniquePtr<EditorSourceRecoveryPreparation> acquisition;
    core::UniquePtr<EditorSourceRecoveryPreparation::Candidate> recovery;
    core::UniquePtr<EditorRecoveryPreparation::WorkspaceState> savedState;
    core::UniquePtr<EditorSourceRecoveryDocuments> documents;
    core::UniquePtr<ProjectDirectorySession> directory;
    core::UniquePtr<ProjectSourceVerification> verification;
    core::UniquePtr<EditorSourceRecoveryFiles> observation;
    ProjectBrowserModel browser;
    core::LifetimeEpoch::Ticket scene, services;
    core::ByteBuffer fingerprint;

    void fail(const char* diagnostic) noexcept
    {
        issue = diagnostic ? diagnostic
                           : (detail.empty() ? "EDITOR-RECOVERY-SOURCE-WORKSPACE" : nullptr);
        phase = SourceRecoveryWorkspaceState::Failed;
        acquisition.reset();
        verification.reset();
        observation.reset();
        directory.reset();
        documents.reset();
        savedState.reset();
        recovery.reset();
        ProjectBrowserModel empty;
        browser.swap(empty);
        core::ByteBuffer{}.swap(fingerprint);
    }

    bool owners(std::uint64_t epoch, const char*& diagnostic) const noexcept
    {
        diagnostic = "EDITOR-RECOVERY-SOURCE-WORKSPACE-OWNER-OR-EPOCH";
        if (!recovery || !recovery->document || !savedState || epoch != location.epoch
            || !scene.sameCurrentEpoch(recovery->document->ownerTicket()))
            return false;
        const auto session = recovery->document->sceneServiceSession();
        if (!session || !services.sameCurrentEpoch(session->ownerTicket())
            || session->resources() != &recovery->resources.manager()
            || session->physics() != recovery->resources.physicsWorld2D()
            || recovery->sources.snapshot != recovery->resources.sourceSnapshot()
            || !recovery->sources.validate(diagnostic))
            return false;
        return true;
    }

    bool observe(core::ByteBuffer& destination)
    {
        const auto root = std::filesystem::u8path(location.root);
        core::pmr::String yaml;
        const auto version = savedState->sceneAuthoringVersion();
        if (version == EditorSceneAuthoringCodec::contractVersion) {
            if (!EditorSceneAuthoringCodec::encode(*recovery->document, yaml, detail))
                return false;
        } else {
            auto policy = EditorSceneScriptPolicy::inputs(root, recovery->draft);
            policy.sources = &recovery->sources;
            if (!EditorSceneScriptCodec::observeSnapshot(*recovery->document, policy, yaml, detail))
                return false;
        }
        detail::RecoveryDigest digest(records.maxPayloadBytes);
        digest.field("arp.editor-source-recovery-workspace/1");
        digest.field(yaml);
        ProjectDiagnostic diagnostic;
        if (!ProjectManifestCodec::encode(recovery->draft, yaml, diagnostic)) {
            detail = diagnostic.code;
            return false;
        }
        digest.field(yaml);
        if (!digest.valid()) {
            detail = "EDITOR-RECOVERY-SOURCE-WORKSPACE-OBSERVATION-BUDGET";
            return false;
        }
        destination = digest.finish();
        return true;
    }

    bool current(std::uint64_t epoch, const char*& diagnostic)
    {
        if (!owners(epoch, diagnostic))
            return false;
        const auto& listing = browser.sourceSnapshot();
        if (!listing || listing->project() != recovery->sources.snapshot
            || !listing->validate(*files, epoch, diagnostic)
            || !documents->documents(epoch, diagnostic))
            return false;
        core::ByteBuffer bytes;
        if (!observe(bytes)) {
            diagnostic = "EDITOR-RECOVERY-SOURCE-WORKSPACE-OBSERVATION";
            return false;
        }
        if (bytes != fingerprint) {
            diagnostic = "EDITOR-RECOVERY-SOURCE-WORKSPACE-CONTENT-CHANGED";
            return false;
        }
        diagnostic = nullptr;
        return true;
    }

    bool matchingFiles(const core::pmr::Vector<EditorRecoverySourceFile>& currentFiles)
    {
        const char* diagnostic = nullptr;
        const auto* previous = documents->observations(location.epoch, diagnostic);
        if (!previous || previous->size() != currentFiles.size())
            return false;
        for (std::size_t i = 0; i < previous->size(); ++i) {
            const auto& before = (*previous)[i];
            const auto& now = currentFiles[i];
            if (before.relativePath != now.relativePath || before.conflict != now.conflict
                || before.observation.status != now.observation.status
                || before.observation.bytes != now.observation.bytes)
                return false;
        }
        return true;
    }
};

EditorSourceRecoveryWorkspace::EditorSourceRecoveryWorkspace(core::io::FileService& files,
    const EditorRecoverySnapshot& record,
    std::string_view alias,
    ProjectSourceLocation location,
    ProjectInputBudget inputs,
    ProjectDirectoryBudget directories,
    EditorRecoveryBudget records)
    : state_(core::makeUnique<State>())
{
    auto& s = *state_;
    s.files = &files;
    s.location = std::move(location);
    s.inputs = inputs;
    s.directories = directories;
    s.records = records;
    s.alias = alias;
    if (!ProjectDirectorySession::validBudget(directories)) {
        s.fail("EDITOR-RECOVERY-SOURCE-WORKSPACE-DIRECTORY-BUDGET");
        return;
    }
    s.acquisition = core::makeUnique<EditorSourceRecoveryPreparation>(
        files, record, alias, s.location, inputs, records);
}

EditorSourceRecoveryWorkspace::~EditorSourceRecoveryWorkspace() = default;

SourceRecoveryWorkspaceState EditorSourceRecoveryWorkspace::state() const noexcept
{
    return state_->phase;
}

std::string_view EditorSourceRecoveryWorkspace::diagnostic() const noexcept
{
    return state_->issue ? std::string_view(state_->issue) : std::string_view(state_->detail);
}

std::optional<EditorSourceRecoveryWorkspace::View> EditorSourceRecoveryWorkspace::inspect(
    std::uint64_t epoch, const char*& diagnostic) const noexcept
{
    auto& s = *state_;
    diagnostic = "EDITOR-RECOVERY-SOURCE-WORKSPACE-NOT-READY";
    try {
        if (s.phase != SourceRecoveryWorkspaceState::Ready || !s.current(epoch, diagnostic))
            return {};
        const auto* docs = s.documents->documents(epoch, diagnostic);
        const auto* observed = s.documents->observations(epoch, diagnostic);
        if (!docs || !observed)
            return {};
        return View{*s.recovery, *s.savedState, s.browser, *docs, *observed};
    } catch (...) {
        diagnostic = "EDITOR-RECOVERY-SOURCE-WORKSPACE-INSPECTION";
        return {};
    }
}

core::UniquePtr<ScriptDocumentSession::PreparedRecovery>
EditorSourceRecoveryWorkspace::prepareForSession(
    const ScriptDocumentSession& session, std::uint64_t epoch, core::pmr::String& diagnostic) const
{
    const char* issue = nullptr;
    if (!inspect(epoch, issue)) {
        diagnostic = issue;
        return {};
    }
    return state_->documents->prepareForSession(session, epoch, diagnostic);
}

bool EditorSourceRecoveryWorkspace::revalidate(std::uint64_t epoch) noexcept
{
    auto& s = *state_;
    if (s.phase != SourceRecoveryWorkspaceState::Ready)
        return false;
    try {
        const char* issue = nullptr;
        if (!s.current(epoch, issue)) {
            s.fail(issue);
            return false;
        }
        auto verification =
            core::makeUnique<ProjectSourceVerification>(s.recovery->sources, s.inputs);
        s.verification = std::move(verification);
        s.step = State::Step::Inputs;
        s.phase = SourceRecoveryWorkspaceState::Revalidating;
        return true;
    } catch (...) {
        s.fail("EDITOR-RECOVERY-SOURCE-WORKSPACE-REVALIDATION");
        return false;
    }
}

void EditorSourceRecoveryWorkspace::cancel() noexcept
{
    if (state_->phase == SourceRecoveryWorkspaceState::Pending
        || state_->phase == SourceRecoveryWorkspaceState::Revalidating) {
        state_->fail("EDITOR-RECOVERY-SOURCE-WORKSPACE-CANCELLED");
        state_->phase = SourceRecoveryWorkspaceState::Cancelled;
    }
}

void EditorSourceRecoveryWorkspace::advance(std::uint64_t epoch) noexcept
{
    auto& s = *state_;
    if (s.phase != SourceRecoveryWorkspaceState::Pending
        && s.phase != SourceRecoveryWorkspaceState::Revalidating)
        return;
    try {
        if (epoch != s.location.epoch) {
            s.fail("EDITOR-RECOVERY-SOURCE-WORKSPACE-EPOCH");
            return;
        }
        const char* issue = nullptr;
        if (s.step != State::Step::Resources && !s.recovery->sources.validate(issue)) {
            s.fail(issue);
            return;
        }
        switch (s.step) {
        case State::Step::Resources:
            s.acquisition->advance(epoch);
            if (s.acquisition->state() == SourceRecoveryState::Pending)
                return;
            if (s.acquisition->state() != SourceRecoveryState::Ready) {
                s.detail = s.acquisition->diagnostic();
                s.fail(nullptr);
                return;
            }
            s.recovery = s.acquisition->takeCandidate(epoch, issue);
            if (!s.recovery) {
                s.detail = s.acquisition->diagnosticDetail();
                s.fail(issue);
                return;
            }
            s.acquisition.reset();
            s.savedState = EditorRecoveryPreparation::prepareWorkspaceState(s.recovery->submitted,
                s.recovery->submitted.scene.path.empty()
                    ? std::filesystem::path{}
                    : std::filesystem::u8path(s.location.pathFor(s.recovery->submitted.scene.path)),
                s.detail,
                s.records);
            if (!s.savedState) {
                s.fail("EDITOR-RECOVERY-SOURCE-WORKSPACE-SAVEPOINT");
                return;
            }
            s.scene = s.recovery->document->ownerTicket();
            s.services = s.recovery->document->sceneServiceSession()->ownerTicket();
            s.documents = core::makeUnique<EditorSourceRecoveryDocuments>(
                s.recovery->sources, s.recovery->submitted, s.alias, s.inputs, s.records);
            s.step = State::Step::Documents;
            return;
        case State::Step::Documents:
            s.documents->advance(epoch);
            if (s.documents->state() == RecoverySourceDocumentsState::Pending)
                return;
            if (s.documents->state() != RecoverySourceDocumentsState::Ready) {
                s.fail(s.documents->diagnostic());
                return;
            }
            s.directory = core::makeUnique<ProjectDirectorySession>(
                *s.files, s.recovery->sources.snapshot, "", s.directories);
            s.step = State::Step::Directory;
            return;
        case State::Step::Directory:
        case State::Step::DirectoryCheck: {
            if (s.directory->state() == ProjectDirectoryState::Ready)
                s.directory->next(epoch);
            if (s.directory->state() == ProjectDirectoryState::Pending
                || s.directory->state() == ProjectDirectoryState::Ready)
                return;
            auto listing = s.directory->finish(epoch);
            if (!listing) {
                s.fail(s.directory->diagnostic());
                return;
            }
            s.directory.reset();
            if (s.step == State::Step::DirectoryCheck) {
                if (listing->version() != s.browser.sourceSnapshot()->version()
                    || !s.current(epoch, issue)) {
                    s.fail(issue ? issue : "EDITOR-RECOVERY-SOURCE-WORKSPACE-DIRECTORY-CHANGED");
                    return;
                }
                s.phase = SourceRecoveryWorkspaceState::Ready;
                return;
            }
            ProjectBrowserModel empty;
            if (const auto diagnostic = empty.prepareSources(std::move(listing),
                    *s.files,
                    epoch,
                    ProjectBrowserModel::SourceNavigation::Navigate,
                    s.browser)) {
                s.fail(diagnostic);
                return;
            }
            if (!s.observe(s.fingerprint)) {
                s.fail("EDITOR-RECOVERY-SOURCE-WORKSPACE-OBSERVATION");
                return;
            }
            s.verification =
                core::makeUnique<ProjectSourceVerification>(s.recovery->sources, s.inputs);
            s.step = State::Step::Inputs;
            return;
        }
        case State::Step::Inputs:
            s.verification->advance();
            if (s.verification->state() == ProjectSourceVerificationState::Pending)
                return;
            if (s.verification->state() != ProjectSourceVerificationState::Verified) {
                s.fail(s.verification->diagnostic());
                return;
            }
            s.verification.reset();
            s.observation = core::makeUnique<EditorSourceRecoveryFiles>(
                s.recovery->sources, s.recovery->submitted, s.alias, s.inputs, s.records);
            s.step = State::Step::Files;
            return;
        case State::Step::Files: {
            s.observation->advance(epoch);
            if (s.observation->state() == RecoverySourceFilesState::Pending)
                return;
            const auto* observed = s.observation->observations(epoch, issue);
            if (!observed || !s.matchingFiles(*observed)) {
                s.fail(issue ? issue : "EDITOR-RECOVERY-SOURCE-WORKSPACE-FILE-CHANGED");
                return;
            }
            s.observation.reset();
            s.directory = core::makeUnique<ProjectDirectorySession>(
                *s.files, s.recovery->sources.snapshot, "", s.directories);
            s.step = State::Step::DirectoryCheck;
            return;
        }
        }
    } catch (...) {
        s.fail("EDITOR-RECOVERY-SOURCE-WORKSPACE-PREPARATION");
    }
}
} // namespace arp::editor
