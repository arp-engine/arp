#pragma once

#include "editor/project/project_source_session.h"

namespace arp::editor {

enum class ProjectSourceVerificationState { Pending, Verified, Failed, Cancelled };

// A finite owner-thread admission observation of an existing sealed scope.
// Re-read one dependency at a time, pinned to the retained exact-byte SHA256;
// discard temporary bytes and reuse the original scope. This is not a global
// filesystem transaction, a watcher or a replacement project resource bundle.
class ProjectSourceVerification final {
public:
    ProjectSourceVerification(ProjectSourceContext, ProjectInputBudget);
    ~ProjectSourceVerification();
    ProjectSourceVerification(const ProjectSourceVerification&) = delete;
    ProjectSourceVerification& operator=(const ProjectSourceVerification&) = delete;

    void advance() noexcept;
    void cancel() noexcept;
    ProjectSourceVerificationState state() const noexcept;
    const char* diagnostic() const noexcept;
    const ProjectSourceContext& context() const noexcept;

private:
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
