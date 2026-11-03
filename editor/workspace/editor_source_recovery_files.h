#pragma once

#include "editor/project/project_source_session.h"
#include "editor/workspace/editor_recovery_codec.h"

namespace arp::editor {

enum class RecoverySourceFilesState { Pending, Ready, Failed, Cancelled };
enum class RecoverySourceFileConflict { None, Changed, Missing };

// Historical baselines and actual IO observations have different ownership and
// meaning. Missing files retain an actual MissingFile result, never a fabricated
// successful ProjectSourceSnapshot or a path usable by native file fallbacks.
struct EditorRecoverySourceFile {
    core::pmr::String relativePath;
    core::pmr::String savedFileBytes;
    core::io::FileReadRequest binding;
    core::io::FileReadResult observation;
    RecoverySourceFileConflict conflict = RecoverySourceFileConflict::None;
};

// Owner-thread finite read observation, not a write capability, atomic backing
// snapshot, live publication ticket or revalidation of project dependencies.
// Service/queue outlive the task and any accepted callbacks. Budget bounds the
// combined historical baseline bytes and retained observation capacities.
class EditorSourceRecoveryFiles final {
public:
    EditorSourceRecoveryFiles(ProjectSourceContext,
        const EditorRecoverySnapshot&,
        std::string_view admittedAlias,
        ProjectInputBudget = {},
        EditorRecoveryBudget = {});
    ~EditorSourceRecoveryFiles();
    EditorSourceRecoveryFiles(const EditorSourceRecoveryFiles&) = delete;
    EditorSourceRecoveryFiles& operator=(const EditorSourceRecoveryFiles&) = delete;

    void advance(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    RecoverySourceFilesState state() const noexcept;
    const char* diagnostic() const noexcept;
    // Exposed only after all observations complete and mount/epoch validate.
    const core::pmr::Vector<EditorRecoverySourceFile>* observations(
        std::uint64_t currentEpoch, const char*& diagnostic) const noexcept;

private:
    struct State;
    core::SharedPtr<State> state_;
};

} // namespace arp::editor
