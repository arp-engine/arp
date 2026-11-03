#pragma once

#include "editor/project/project_source_session.h"

namespace arp::editor {

enum class SceneSourceState { Pending, Ready, Failed, Cancelled };

// Owner-thread acquisition only. IO callbacks belong to ProjectSourceSession;
// this class neither reads host paths nor modifies Scene/Shell objects.
class EditorSceneSourceTask final {
public:
    EditorSceneSourceTask(ProjectSourceContext,
        std::string_view relative,
        ProjectInputBudget,
        std::size_t maxSceneBytes);
    ~EditorSceneSourceTask();
    EditorSceneSourceTask(const EditorSceneSourceTask&) = delete;
    EditorSceneSourceTask& operator=(const EditorSceneSourceTask&) = delete;

    void advance() noexcept;
    void cancel() noexcept;
    SceneSourceState state() const noexcept;
    const char* diagnostic() const noexcept;
    const ProjectSourceContext& context() const noexcept;
    std::string_view text() const noexcept;
    const ProjectSourceSnapshot::Entry& scene() const;

private:
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
