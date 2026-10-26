#pragma once

#include "editor/workspace/editor_shell.h"

namespace arp::editor {

// Prepared Scene + owner-thread history/view handoff. Native documents,
// resource owners and GPU consumers remain the caller's retirement duty.
class EditorShell::PreparedSceneReplacement final {
public:
    static constexpr std::size_t maxPreparedLabelBytes = 512u;
    PreparedSceneReplacement();
    ~PreparedSceneReplacement();
    PreparedSceneReplacement(const PreparedSceneReplacement&) = delete;
    PreparedSceneReplacement& operator=(const PreparedSceneReplacement&) = delete;
    PreparedSceneReplacement(PreparedSceneReplacement&&) = delete;
    PreparedSceneReplacement& operator=(PreparedSceneReplacement&&) = delete;

    bool committed() const noexcept;
    std::size_t retiredUndoCount() const noexcept;
    std::size_t retiredRedoCount() const noexcept;
    std::size_t retiredSelectionCount() const noexcept;

private:
    friend class EditorShell;
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
