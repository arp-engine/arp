#pragma once

#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_recovery_codec.h"
#include "editor/workspace/editor_scene_document.h"

namespace arp::editor {

enum class SourceRecoveryState { Pending, Ready, Failed, Cancelled, Consumed };

// Cold preparation only. The host explicitly admits alias/root and supplies a
// fresh mount/epoch; a serialized alias cannot open or authorize a workspace.
// Accepted callbacks outlive this task safely, but FileService/queue must live.
class EditorSourceRecoveryPreparation final {
public:
    // This is not a workspace-admission ticket. Script/Scene disk conflicts,
    // fresh live IDs, history publication and GPU retirement remain separate.
    // Historical submitted script bytes are never forged into acquired receipts.
    struct Candidate {
        EditorRecoverySnapshot submitted;
        ProjectSourceContext sources;
        ProjectManifest draft;
        // Retired in reverse order; callbacks also retain the exact services.
        EditorProjectResources resources;
        core::UniquePtr<EditorSceneDocument> document;
    };

    EditorSourceRecoveryPreparation(core::io::FileService&,
        const EditorRecoverySnapshot&,
        std::string_view admittedAlias,
        ProjectSourceLocation,
        ProjectInputBudget = {},
        EditorRecoveryBudget = {});
    ~EditorSourceRecoveryPreparation();
    EditorSourceRecoveryPreparation(const EditorSourceRecoveryPreparation&) = delete;
    EditorSourceRecoveryPreparation& operator=(const EditorSourceRecoveryPreparation&) = delete;

    // At most one asynchronous file request per call; no waiting or host file IO.
    // Resource parsing and Passive scene restoration run once after acquisition.
    void advance(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    SourceRecoveryState state() const noexcept;
    std::string_view diagnostic() const noexcept;
    std::string_view diagnosticDetail() const noexcept;
    const Candidate* candidate(std::uint64_t currentEpoch, const char*& diagnostic) const noexcept;
    // Single ownership transfer to a larger owner-thread preparation. This
    // grants no live workspace, resource inventory or GPU publication authority.
    core::UniquePtr<Candidate> takeCandidate(
        std::uint64_t currentEpoch, const char*& diagnostic) noexcept;

private:
    struct State;
    core::UniquePtr<State> state_;
};

} // namespace arp::editor
