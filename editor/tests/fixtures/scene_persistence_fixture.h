#pragma once

#include "editor/workspace/editor_shell.h"

#include <cassert>
#include <chrono>
#include <thread>

namespace arp::editor::tests {

// Poll the exact accepted task; ordinary editor.state does not identify a
// completed operation when another request has since been submitted.
class ScenePersistenceFixture final {
public:
    static EditorStateSnapshot finish(EditorShell& shell, const EditorResponse& accepted)
    {
        assert(accepted.status == EditorResponseStatus::Completed && accepted.hasSnapshot);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            shell.pollTasks();
            const auto result = shell.submitCommand(
                GetScenePersistenceTaskQuery{accepted.snapshot.scenePersistenceTaskId});
            assert(result.hasSnapshot);
            if (result.snapshot.scenePersistenceTaskStatus != ScenePersistenceTaskStatus::Pending)
                return result.snapshot;
            std::this_thread::yield();
        }
        assert(false && "scene IO completion timeout");
        return {};
    }
};

} // namespace arp::editor::tests
