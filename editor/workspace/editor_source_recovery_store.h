#pragma once

#include "editor/project/project_file_read_task.h"
#include "editor/project/project_file_save_task.h"
#include "editor/workspace/editor_source_recovery_capture.h"

#include <array>
#include <chrono>

namespace arp::editor {
enum class SourceRecoveryStoreMode { Fresh, Resume, Inspect };
enum class SourceRecoveryStoreState {
    Opening,
    Ready,
    Pending,
    Failed,
    ReconcileRequired,
    Cancelled
};
enum class SourceRecoveryStoreSubmit { Accepted, Busy, NotDue, Rejected };

struct EditorSourceRecoveryStoreDesc {
    // The host admits two private logical keys in an existing storage root.
    // Region quota/namespace admission and deletion are separate contracts.
    ProjectSourceLocation storage;
    std::array<core::pmr::String, 2> slots;
    core::pmr::String sessionId, projectId, sourceAlias, sourceRoot;
    SourceRecoveryStoreMode mode = SourceRecoveryStoreMode::Fresh;
    ProjectInputBudget inputs;
    EditorRecoveryBudget records;
    // Two retained slots, one prepared cache replacement, one IO payload and
    // one preflight read. Codec objects and backend scratch have separate budgets.
    std::size_t maxRawResidentBytes = 5 * EditorRecoveryBudget{}.maxEnvelopeBytes;
};

// A bounded receipt survives store destruction. It retains only the existing
// save operation and scalar outcome, never project sources or cached slot bytes.
class EditorSourceRecoveryWrite final {
public:
    EditorSourceRecoveryWrite() = default;
    bool submitted() const noexcept;
    bool terminal() const noexcept;

    bool published() const noexcept { return published_; }

    std::uint64_t sequence() const noexcept { return sequence_; }

    const core::io::FileSaveResult& result() const noexcept;

private:
    friend class EditorSourceRecoveryStore;
    core::UniquePtr<ProjectFileSaveTask> operation_;
    core::io::FileSaveResult empty_;
    std::uint64_t sequence_ = 0, workspaceEpoch_ = 0;
    bool published_ = false, consumedSequence_ = false;
};

// Per-session two-slot writer/reader. Every open, preflight and write uses the
// mounted FileService. It grants no region lease, cleanup or workspace publish
// authority. Service/queue outlive the store and accepted receipt callbacks.
class EditorSourceRecoveryStore final {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    EditorSourceRecoveryStore(core::io::FileService&, EditorSourceRecoveryStoreDesc);
    ~EditorSourceRecoveryStore();
    EditorSourceRecoveryStore(const EditorSourceRecoveryStore&) = delete;
    EditorSourceRecoveryStore& operator=(const EditorSourceRecoveryStore&) = delete;

    void advance(std::uint64_t storageEpoch) noexcept;
    void cancel() noexcept;
    SourceRecoveryStoreSubmit submit(
        const EditorSourceRecoveryCapture&, std::uint64_t projectEpoch, TimePoint now);
    SourceRecoveryStoreState state() const noexcept;
    std::string_view diagnostic() const noexcept;
    std::uint64_t nextSequence() const noexcept;
    core::SharedPtr<const EditorSourceRecoveryWrite> receipt() const noexcept;
    bool decodeLatest(
        std::uint64_t storageEpoch, EditorRecoverySnapshot&, core::pmr::String& diagnostic) const;

private:
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
