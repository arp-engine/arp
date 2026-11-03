#pragma once

#include "core/memory/cpu_allocator.h"
#include "editor/project/project_directory_session.h"

#include <filesystem>
#include <string_view>

namespace arp::editor {
class ProjectDirectorySnapshot;
}

namespace arp::core::io {
class FileService;
}

namespace arp::editor {
class ProjectBrowserModel final {
public:
    class PreparedProjectRebind;

    struct Entry {
        std::filesystem::path path;
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        bool directory = false;
    };

    enum class NavigationResult { Navigated, Unchanged, Rejected };
    enum class SourceNavigation { Navigate, Refresh, Back, Forward };

    // Prepare a complete source-backed directory without observing host paths.
    // Source navigation is published only after its corresponding acquisition.
    const char* prepareSources(core::SharedPtr<const ProjectDirectorySnapshot>,
        core::io::FileService&,
        std::uint64_t epoch,
        SourceNavigation,
        ProjectBrowserModel& destination,
        std::size_t maxHistoryEntries = 256) const noexcept;
    // Explicit owner-thread handoff after source-refresh admission. Normal
    // directory navigation still requires the identical project snapshot.
    const char* prepareReloadedSources(const core::SharedPtr<const ProjectSourceSnapshot>& expected,
        core::SharedPtr<const ProjectDirectorySnapshot>,
        core::io::FileService&,
        std::uint64_t epoch,
        ProjectBrowserModel& destination,
        std::size_t maxHistoryEntries) const noexcept;
    bool sourceTarget(
        SourceNavigation, std::string_view relative, core::pmr::String& destination) const noexcept;

    const core::SharedPtr<const ProjectDirectorySnapshot>& sourceSnapshot() const noexcept
    {
        return source_;
    }

    core::UniquePtr<PreparedProjectRebind> prepareProjectRebind(
        const ProjectSourceSnapshot::PreparedFileSave&, const char*& diagnostic) const noexcept;

    void setRoot(std::filesystem::path root);
    bool refresh(std::size_t entryBudget = 0);
    NavigationResult navigate(const std::filesystem::path& path, bool remember = true);
    bool back();
    bool forward();
    void setSearch(const core::pmr::String& query);

    // Cold, bounded preparation; failure preserves both output values.
    const char* prepareReveal(const std::filesystem::path& file,
        ProjectBrowserModel& candidate,
        std::size_t& visibleIndex) const noexcept;
    void swap(ProjectBrowserModel& other) noexcept;

    const std::filesystem::path& root() const noexcept { return root_; }

    const std::filesystem::path& currentDirectory() const noexcept { return currentDirectory_; }

    const core::pmr::Vector<Entry>& entries() const noexcept { return entries_; }

    const core::pmr::Vector<std::size_t>& visibleEntryIndices() const noexcept
    {
        return visibleEntries_;
    }

    const core::pmr::String& search() const noexcept { return search_; }

private:
    static bool containsIgnoringCase(
        const core::pmr::String& value, const core::pmr::String& query);
    NavigationResult navigateResolved(const std::filesystem::path& candidate, bool remember);
    std::filesystem::path root_;
    std::filesystem::path currentDirectory_;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::size_t> visibleEntries_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::filesystem::path> backHistory_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::filesystem::path> forwardHistory_{&core::defaultCpuMemoryResource()};
    core::pmr::String search_{&core::defaultCpuMemoryResource()};
    core::SharedPtr<const ProjectDirectorySnapshot> source_;
    std::size_t sourceHistoryBudget_ = 0;
};

// A settings save keeps the exact directory/search/history state. The live
// owner must still match its prepared state when its new scope is published.
class ProjectBrowserModel::PreparedProjectRebind final {
public:
    PreparedProjectRebind() = default;
    PreparedProjectRebind(const PreparedProjectRebind&) = delete;
    PreparedProjectRebind& operator=(const PreparedProjectRebind&) = delete;
    bool canFinish(const ProjectSourceSnapshot::PreparedFileSave&,
        const core::SharedPtr<const ProjectSourceSnapshot>&,
        const ProjectBrowserModel&) const noexcept;
    bool finish(const ProjectSourceSnapshot::PreparedFileSave&,
        core::SharedPtr<const ProjectSourceSnapshot>,
        ProjectBrowserModel&) noexcept;

private:
    friend class ProjectBrowserModel;
    const ProjectBrowserModel* owner_ = nullptr;
    ProjectBrowserModel candidate_;
    core::UniquePtr<ProjectDirectorySnapshot::PreparedProjectRebind> directory_;
    bool consumed_ = false;
};
} // namespace arp::editor
