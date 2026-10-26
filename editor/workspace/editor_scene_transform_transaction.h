#pragma once

#include "editor/workspace/editor_shell.h"

namespace arp::editor {

// Owner-thread cold transaction; retired reflection history never crosses a
// worker boundary. Numeric preparation is reusable independently in viewport/.
class EditorShell::PreparedSceneTransform final {
public:
    PreparedSceneTransform();
    ~PreparedSceneTransform();
    PreparedSceneTransform(const PreparedSceneTransform&) = delete;
    PreparedSceneTransform& operator=(const PreparedSceneTransform&) = delete;
    bool committed() const noexcept;
    std::size_t changeCount() const noexcept;
    std::size_t retiredRedoCount() const noexcept;

private:
    friend class EditorShell;
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
