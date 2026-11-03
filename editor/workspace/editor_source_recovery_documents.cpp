#include "editor/workspace/editor_source_recovery_documents.h"

#include <algorithm>

namespace arp::editor {
using namespace core::io;

struct EditorSourceRecoveryDocuments::State {
    ProjectSourceContext context;
    EditorRecoverySnapshot record;
    ProjectInputBudget budget;
    core::UniquePtr<EditorSourceRecoveryFiles> observation;
    core::UniquePtr<ProjectSourceSession> read;
    core::pmr::Vector<ScriptDocumentSession::Document> documents;
    std::size_t next = 0, acquiredBytes = 0;
    RecoverySourceDocumentsState phase = RecoverySourceDocumentsState::Pending;
    const char* diagnostic = nullptr;

    void fail(const char* issue) noexcept
    {
        phase = RecoverySourceDocumentsState::Failed;
        diagnostic = issue ? issue : "EDITOR-RECOVERY-SOURCE-DOCUMENT-INPUT";
        read.reset();
        observation.reset();
        core::pmr::Vector<ScriptDocumentSession::Document>{}.swap(documents);
        record = {};
        context = {};
    }

    void append(core::UniquePtr<ScriptDocumentSession::Document> document)
    {
        const auto& source = record.scripts[next];
        document->text = source.text;
        document->savedText = source.savedText;
        document->savedFileBytes = source.savedFileBytes;
        document->newline = source.newline;
        document->cursor = source.cursor;
        document->anchor = source.anchor;
        document->scrollLine = source.scrollLine;
        documents.push_back(std::move(*document));
        ++next;
    }
};

EditorSourceRecoveryDocuments::EditorSourceRecoveryDocuments(ProjectSourceContext context,
    const EditorRecoverySnapshot& record,
    std::string_view admittedAlias,
    ProjectInputBudget budget,
    EditorRecoveryBudget recovery)
    : state_(core::makeUnique<State>())
{
    auto& s = *state_;
    s.context = std::move(context);
    s.budget = budget;
    s.observation = core::makeUnique<EditorSourceRecoveryFiles>(
        s.context, record, admittedAlias, budget, recovery);
    if (s.observation->state() != RecoverySourceFilesState::Pending) {
        s.fail(s.observation->diagnostic());
        return;
    }
    s.record = record;
    s.documents.reserve(record.scripts.size());
}

EditorSourceRecoveryDocuments::~EditorSourceRecoveryDocuments() = default;

RecoverySourceDocumentsState EditorSourceRecoveryDocuments::state() const noexcept
{
    return state_->phase;
}

const char* EditorSourceRecoveryDocuments::diagnostic() const noexcept
{
    return state_->diagnostic;
}

const ProjectSourceContext& EditorSourceRecoveryDocuments::context() const noexcept
{
    return state_->context;
}

const core::pmr::Vector<ScriptDocumentSession::Document>* EditorSourceRecoveryDocuments::documents(
    std::uint64_t currentEpoch, const char*& diagnostic) const noexcept
{
    diagnostic = "EDITOR-RECOVERY-SOURCE-DOCUMENTS-NOT-READY-OR-EPOCH";
    if (state_->phase != RecoverySourceDocumentsState::Ready
        || currentEpoch != state_->context.epoch || !state_->context.validate(diagnostic))
        return nullptr;
    for (const auto& document : state_->documents)
        if (!document.validateSource(*state_->context.files, currentEpoch, diagnostic))
            return nullptr;
    diagnostic = nullptr;
    return &state_->documents;
}

const core::pmr::Vector<EditorRecoverySourceFile>* EditorSourceRecoveryDocuments::observations(
    std::uint64_t currentEpoch, const char*& diagnostic) const noexcept
{
    return documents(currentEpoch, diagnostic)
        ? state_->observation->observations(currentEpoch, diagnostic)
        : nullptr;
}

core::UniquePtr<ScriptDocumentSession::PreparedRecovery>
EditorSourceRecoveryDocuments::prepareForSession(const ScriptDocumentSession& session,
    std::uint64_t currentEpoch,
    core::pmr::String& diagnostic) const
{
    const char* issue = nullptr;
    const auto* docs = documents(currentEpoch, issue);
    if (!docs) {
        diagnostic = issue;
        return {};
    }
    return session.prepareRecoveredSourceDocuments(*docs,
        state_->record.activeScript.value_or(ScriptDocumentSession::noActiveDocument),
        state_->context,
        diagnostic);
}

void EditorSourceRecoveryDocuments::cancel() noexcept
{
    if (state_->phase == RecoverySourceDocumentsState::Pending) {
        state_->fail("EDITOR-RECOVERY-SOURCE-DOCUMENTS-CANCELLED");
        state_->phase = RecoverySourceDocumentsState::Cancelled;
    }
}

void EditorSourceRecoveryDocuments::advance(std::uint64_t currentEpoch) noexcept
{
    auto& s = *state_;
    if (s.phase != RecoverySourceDocumentsState::Pending)
        return;
    try {
        const char* issue = nullptr;
        if (currentEpoch != s.context.epoch || !s.context.validate(issue)) {
            s.fail(issue ? issue : "EDITOR-RECOVERY-SOURCE-DOCUMENT-EPOCH");
            return;
        }
        s.observation->advance(currentEpoch);
        if (s.observation->state() == RecoverySourceFilesState::Pending)
            return;
        const auto* files = s.observation->observations(currentEpoch, issue);
        if (!files) {
            s.fail(s.observation->diagnostic() ? s.observation->diagnostic() : issue);
            return;
        }
        if (s.next == s.record.scripts.size()) {
            s.phase = RecoverySourceDocumentsState::Ready;
            return;
        }
        const auto& script = s.record.scripts[s.next];
        const auto& file = (*files)[s.next + (s.record.scene.path.empty() ? 0u : 1u)];
        if (file.relativePath != script.path) {
            s.fail("EDITOR-RECOVERY-SOURCE-DOCUMENT-OBSERVATION");
            return;
        }
        core::UniquePtr<ScriptDocumentSession::Document> document;
        if (file.conflict == RecoverySourceFileConflict::Missing) {
            if (!ScriptDocumentSession::prepareMissingSourceFile(
                    s.context, script.path, file.binding, file.observation, document, issue)) {
                s.fail(issue);
                return;
            }
            s.append(std::move(document));
            return;
        }
        if (!s.read) {
            const auto bytes = std::max<std::size_t>(1, file.observation.bytes.size());
            if (bytes > s.budget.maxFileBytes || bytes > s.budget.maxTotalBytes - s.acquiredBytes) {
                s.fail("EDITOR-RECOVERY-SOURCE-DOCUMENT-BUDGET");
                return;
            }
            core::Sha256 hash;
            const auto& raw = file.observation.bytes;
            hash.update({raw.empty() ? "" : reinterpret_cast<const char*>(raw.data()), raw.size()});
            const auto version = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
            s.read = core::makeUnique<ProjectSourceSession>(*s.context.files,
                s.context.snapshot->location(),
                ProjectInputBudget{1, bytes, bytes});
            if (s.read->read(script.path, bytes, version, bytes) != FileIoStatus::Accepted)
                s.fail(s.read->diagnostic());
            return;
        }
        if (s.read->state() == ProjectSourceState::Pending)
            return;
        const auto acquired = s.read->finish(currentEpoch);
        if (!acquired) {
            s.fail(s.read->diagnostic());
            return;
        }
        const auto* entry = acquired->find(script.path);
        if (!entry || entry->receipt.bytes != file.observation.bytes
            || entry->receipt.bytes.capacity() > s.budget.maxTotalBytes - s.acquiredBytes) {
            s.fail("EDITOR-RECOVERY-SOURCE-DOCUMENT-OBSERVATION-STALE");
            return;
        }
        s.acquiredBytes += entry->receipt.bytes.capacity();
        document = core::makeUnique<ScriptDocumentSession::Document>();
        document->path = std::filesystem::u8path(entry->binding.path);
        document->sourceSnapshot = acquired;
        s.append(std::move(document));
        s.read.reset();
    } catch (...) {
        s.fail("EDITOR-RECOVERY-SOURCE-DOCUMENT-PREPARATION");
    }
}
} // namespace arp::editor
