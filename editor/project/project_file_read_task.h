#pragma once

#include "editor/project/project_source_session.h"

namespace arp::editor {
enum class ProjectFileReadState { Pending, Complete, Failed, Cancelled, Consumed };

// One optional logical file observation. MissingFile is an actual completion,
// distinct from admission failure. No successful source snapshot is manufactured.
// Owner thread advances; FileService/queue outlive accepted callbacks. Completed
// request tokens remain valid when a consumer retains the observed binding.
class ProjectFileReadTask final {
public:
    ProjectFileReadTask(core::io::FileService&,
        ProjectSourceLocation,
        std::string_view relative,
        std::size_t maxBytes,
        core::io::FileVersionToken expected = {},
        std::uint64_t maxVersionBytes = 0);
    ~ProjectFileReadTask();
    ProjectFileReadTask(const ProjectFileReadTask&) = delete;
    ProjectFileReadTask& operator=(const ProjectFileReadTask&) = delete;

    void advance(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    ProjectFileReadState state() const noexcept;
    const char* diagnostic() const noexcept;
    const core::io::FileReadRequest& binding() const noexcept;
    const core::io::FileReadResult& result() const noexcept;
    core::io::FileReadResult takeResult() noexcept;

private:
    struct State;
    static void completed(void*, core::io::FileReadResult) noexcept;
    core::SharedPtr<State> state_;
};
} // namespace arp::editor
