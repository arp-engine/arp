#pragma once

#include "editor/project/project_source_session.h"

namespace arp::editor {

// Directory payload budgets, independent of project source byte budgets. A
// backend additionally reserves its declared scan/result scratch storage.
struct ProjectDirectoryBudget {
    std::uint32_t maxEntries = 4096;
    std::uint32_t pageSize = 128;
    std::uint64_t maxNameBytes = 256 * 1024;
    std::uint64_t maxPageBytes = 256 * 1024;
};

class ProjectDirectorySnapshot final {
public:
    bool validate(
        core::io::FileService&, std::uint64_t epoch, const char*& diagnostic) const noexcept;

    const core::SharedPtr<const ProjectSourceSnapshot>& project() const noexcept
    {
        return project_;
    }

    const core::pmr::String& directory() const noexcept { return directory_; }

    const core::pmr::Vector<core::io::FileDirectoryEntry>& entries() const noexcept
    {
        return entries_;
    }

    const core::io::FileVersionToken& version() const noexcept { return version_; }

private:
    friend class ProjectDirectorySession;
    core::SharedPtr<const ProjectSourceSnapshot> project_;
    core::pmr::String directory_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::io::FileDirectoryEntry> entries_{&core::defaultCpuMemoryResource()};
    core::io::FileVersionToken version_;
    bool sealed_ = false;
};

enum class ProjectDirectoryState { Ready, Pending, Complete, Sealed, Failed, Cancelled };

// Owner-thread paged acquisition. Only private candidate state reaches callbacks;
// FileService and its completion queue outlive accepted callbacks. A directory
// token checks consistency between pages, not permanent filesystem freshness.
class ProjectDirectorySession final {
public:
    static bool validBudget(ProjectDirectoryBudget) noexcept;
    ProjectDirectorySession(core::io::FileService&,
        core::SharedPtr<const ProjectSourceSnapshot>,
        std::string_view relativeDirectory,
        ProjectDirectoryBudget);
    ~ProjectDirectorySession();
    ProjectDirectorySession(const ProjectDirectorySession&) = delete;
    ProjectDirectorySession& operator=(const ProjectDirectorySession&) = delete;

    core::io::FileIoStatus next(std::uint64_t currentEpoch) noexcept;
    core::SharedPtr<const ProjectDirectorySnapshot> finish(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    ProjectDirectoryState state() const noexcept;
    const char* diagnostic() const noexcept;

private:
    struct State;
    struct Pending;
    static void complete(void*, core::io::FileQueryResult) noexcept;
    core::io::FileService& files_;
    core::SharedPtr<State> state_;
};

} // namespace arp::editor
