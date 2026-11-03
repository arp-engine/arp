#pragma once

#include "editor/workspace/editor_source_recovery_files.h"
#include "editor/workspace/script_document_session.h"

namespace arp::editor {
enum class RecoverySourceDocumentsState { Pending, Ready, Failed, Cancelled };

// Cold script tabs only. Successful files are reacquired through the existing
// ProjectSourceSession, pinned to observed bytes. Missing observations retain an
// explicit source binding. No historical bytes become successful IO receipts.
class EditorSourceRecoveryDocuments final {
public:
    EditorSourceRecoveryDocuments(ProjectSourceContext,
        const EditorRecoverySnapshot&,
        std::string_view admittedAlias,
        ProjectInputBudget = {},
        EditorRecoveryBudget = {});
    ~EditorSourceRecoveryDocuments();
    EditorSourceRecoveryDocuments(const EditorSourceRecoveryDocuments&) = delete;
    EditorSourceRecoveryDocuments& operator=(const EditorSourceRecoveryDocuments&) = delete;

    void advance(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    RecoverySourceDocumentsState state() const noexcept;
    const char* diagnostic() const noexcept;
    const ProjectSourceContext& context() const noexcept;
    const core::pmr::Vector<ScriptDocumentSession::Document>* documents(
        std::uint64_t currentEpoch, const char*& diagnostic) const noexcept;
    const core::pmr::Vector<EditorRecoverySourceFile>* observations(
        std::uint64_t currentEpoch, const char*& diagnostic) const noexcept;
    core::UniquePtr<ScriptDocumentSession::PreparedRecovery> prepareForSession(
        const ScriptDocumentSession&,
        std::uint64_t currentEpoch,
        core::pmr::String& diagnostic) const;

private:
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
