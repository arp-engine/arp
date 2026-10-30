#pragma once

#include "core/io/file_service.h"
#include "editor/project/project_input_snapshot.h"

namespace arp::editor {

struct ProjectSourceLocation {
    core::PoolHandle mount;
    core::pmr::String root{&core::defaultCpuMemoryResource()};
    std::uint64_t epoch = 0;
};

// Immutable acquired bytes. Binding freshness means mount/epoch admission,
// not an atomic filesystem snapshot or proof that backing files never changed.
class ProjectSourceSnapshot final {
public:
    struct Entry {
        core::pmr::String relative{&core::defaultCpuMemoryResource()};
        core::io::FileReadRequest binding;
        core::io::FileReadResult receipt;
    };

    ProjectSourceSnapshot(ProjectSourceLocation location, ProjectInputBudget budget);
    const Entry* find(std::string_view relative) const noexcept;

    const Entry* entryAt(std::size_t index) const noexcept
    {
        return sealed_ && index < entries_.size() ? &entries_[index] : nullptr;
    }

    bool validate(
        core::io::FileService&, std::uint64_t currentEpoch, const char*& diagnostic) const noexcept;

    const ProjectSourceLocation& location() const noexcept { return location_; }

    const core::pmr::Vector<ProjectInputBinding>& identities() const noexcept
    {
        return identities_.bindings();
    }

    std::size_t size() const noexcept { return entries_.size(); }

    std::size_t totalBytes() const noexcept { return totalBytes_; }

private:
    friend class ProjectSourceSession;
    bool append(std::string_view relative,
        const core::io::FileReadRequest& binding,
        core::io::FileReadResult receipt,
        const char*& diagnostic) noexcept;
    const Entry* lookup(std::string_view relative) const noexcept;
    ProjectSourceLocation location_;
    ProjectInputBudget budget_;
    ProjectInputSnapshot identities_;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    std::size_t totalBytes_ = 0;
    bool sealed_ = false;
};

// Explicit owner-thread source input. Service/queue outlive this context and all
// documents/retired catalogs retaining it. Validation does not read backing files.
struct ProjectSourceContext {
    core::SharedPtr<const ProjectSourceSnapshot> snapshot;
    core::io::FileService* files = nullptr;
    std::uint64_t epoch = 0;

    bool validate(const char*& diagnostic) const noexcept;
};

enum class ProjectSourceState { Ready, Pending, Failed, Cancelled, Sealed };

// Owner-thread incremental acquisition: one accepted read at a time; the host
// dispatches FileService completions. Service/queue outlive accepted callbacks.
// Destruction cancels pending work; callbacks retain only private candidate state.
class ProjectSourceSession final {
public:
    ProjectSourceSession(core::io::FileService&, ProjectSourceLocation, ProjectInputBudget);
    ~ProjectSourceSession();
    ProjectSourceSession(const ProjectSourceSession&) = delete;
    ProjectSourceSession& operator=(const ProjectSourceSession&) = delete;

    core::io::FileIoStatus read(std::string_view relative,
        std::size_t maxBytes,
        core::io::FileVersionToken expected = {},
        std::uint64_t maxVersionBytes = 0) noexcept;
    // Planning-only view, valid until the next read/dispatch or session destruction.
    const ProjectSourceSnapshot::Entry* acquired(std::string_view relative) const noexcept;
    core::SharedPtr<const ProjectSourceSnapshot> finish(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    ProjectSourceState state() const noexcept;
    const char* diagnostic() const noexcept;

private:
    struct State;
    struct Pending;
    static void complete(void*, core::io::FileReadResult) noexcept;
    core::io::FileService& service_;
    core::SharedPtr<State> state_;
};

} // namespace arp::editor
