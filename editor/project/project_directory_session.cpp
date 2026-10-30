#include "editor/project/project_directory_session.h"

#include "core/io/file_query_contract.h"

#include <algorithm>

namespace arp::editor {
namespace {
using namespace core::io;

bool validDirectoryBudget(ProjectDirectoryBudget budget)
{
    const ProjectDirectoryBudget hard;
    return budget.maxEntries && budget.maxEntries <= hard.maxEntries && budget.pageSize
        && budget.pageSize <= budget.maxEntries && budget.maxNameBytes
        && budget.maxNameBytes <= hard.maxNameBytes && budget.maxPageBytes
        && budget.maxPageBytes <= hard.maxPageBytes;
}
} // namespace

bool ProjectDirectorySession::validBudget(ProjectDirectoryBudget budget) noexcept
{
    return validDirectoryBudget(budget);
}

bool ProjectDirectorySnapshot::validate(
    FileService& files, std::uint64_t epoch, const char*& diagnostic) const noexcept
{
    if (!sealed_ || !project_) {
        diagnostic = "PROJECT-DIRECTORY-UNSEALED";
        return false;
    }
    return project_->validate(files, epoch, diagnostic);
}

struct ProjectDirectorySession::State {
    core::SharedPtr<ProjectDirectorySnapshot> snapshot;
    core::runtime::CancellationSource cancellation;
    ProjectDirectoryBudget budget;
    FileDirectoryCursor cursor;
    std::uint64_t totalEntries = 0;
    std::uint64_t nameBytes = 0;
    ProjectDirectoryState phase = ProjectDirectoryState::Ready;
    const char* diagnostic = nullptr;
};

struct ProjectDirectorySession::Pending {
    core::SharedPtr<State> state;
    FileService* files = nullptr;
    FileQueryRequest binding;
};

ProjectDirectorySession::ProjectDirectorySession(FileService& files,
    core::SharedPtr<const ProjectSourceSnapshot> project,
    std::string_view relative,
    ProjectDirectoryBudget budget)
    : files_(files), state_(core::makeShared<State>())
{
    state_->budget = budget;
    const char* issue = nullptr;
    if (!project || !validBudget(budget)
        || (!relative.empty() && !ProjectInputSnapshot::validLogicalPath(relative))) {
        state_->phase = ProjectDirectoryState::Failed;
        state_->diagnostic = "PROJECT-DIRECTORY-CONFIG";
        return;
    }
    if (!project->validate(files, project->location().epoch, issue)) {
        state_->phase = ProjectDirectoryState::Failed;
        state_->diagnostic = issue;
        return;
    }
    auto candidate = core::makeShared<ProjectDirectorySnapshot>();
    candidate->project_ = std::move(project);
    candidate->directory_ = relative;
    candidate->entries_.reserve(budget.maxEntries);
    state_->snapshot = std::move(candidate);
}

ProjectDirectorySession::~ProjectDirectorySession()
{
    if (state_->phase != ProjectDirectoryState::Sealed)
        cancel();
}

void ProjectDirectorySession::cancel() noexcept
{
    state_->cancellation.cancel();
    state_->phase = ProjectDirectoryState::Cancelled;
    state_->diagnostic = "PROJECT-DIRECTORY-CANCELLED";
}

ProjectDirectoryState ProjectDirectorySession::state() const noexcept
{
    return state_->phase;
}

const char* ProjectDirectorySession::diagnostic() const noexcept
{
    return state_->diagnostic;
}

FileIoStatus ProjectDirectorySession::next(std::uint64_t epoch) noexcept
{
    if (state_->phase != ProjectDirectoryState::Ready)
        return state_->phase == ProjectDirectoryState::Pending ? FileIoStatus::QueueFull
                                                               : FileIoStatus::InvalidRequest;
    const auto fail = [&](const char* diagnostic, FileIoStatus status) {
        state_->phase = ProjectDirectoryState::Failed;
        state_->diagnostic = diagnostic;
        return status;
    };
    try {
        const char* issue = nullptr;
        if (!state_->snapshot->project_->validate(files_, epoch, issue))
            return fail(issue, FileIoStatus::StaleLocation);
        auto pending = core::makeUnique<Pending>();
        pending->state = state_;
        pending->files = &files_;
        auto& request = pending->binding;
        request.operation = FileQueryOperation::Enumerate;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = state_->snapshot->project_->location().mount;
        request.path = state_->snapshot->project_->location().root;
        if (!state_->snapshot->directory_.empty()) {
            if (!request.path.empty())
                request.path += '/';
            request.path += state_->snapshot->directory_;
        }
        request.cancellation = state_->cancellation.token();
        request.maxResultBytes = state_->budget.maxPageBytes;
        request.pageSize = state_->budget.pageSize;
        request.maxScanEntries = state_->budget.maxEntries;
        request.maxScanNameBytes = state_->budget.maxNameBytes;
        request.cursor = state_->cursor;
        const auto status = files_.query(request, {complete, pending.get()});
        if (status != FileIoStatus::Accepted)
            return fail(status == FileIoStatus::Unsupported  ? "PROJECT-DIRECTORY-UNSUPPORTED"
                    : status == FileIoStatus::QueueFull      ? "PROJECT-DIRECTORY-QUEUE-FULL"
                    : status == FileIoStatus::BudgetExceeded ? "PROJECT-DIRECTORY-BUDGET"
                                                             : "PROJECT-DIRECTORY-QUERY-REJECTED",
                status);
        state_->phase = ProjectDirectoryState::Pending;
        pending.release();
        return status;
    } catch (...) {
        return fail("PROJECT-DIRECTORY-ALLOCATION", FileIoStatus::BudgetExceeded);
    }
}

void ProjectDirectorySession::complete(void* context, FileQueryResult result) noexcept
{
    core::UniquePtr<Pending> pending{static_cast<Pending*>(context),
        core::CpuAllocatedDeleter<Pending>{context, &core::destroyCpuAllocatedUnique<Pending>}};
    auto& state = *pending->state;
    if (state.phase != ProjectDirectoryState::Pending || pending->binding.cancellation.cancelled())
        return;
    const auto fail = [&](const char* diagnostic) {
        state.phase = ProjectDirectoryState::Failed;
        state.diagnostic = diagnostic;
    };
    try {
        if (result.status != FileIoStatus::Success) {
            fail(result.diagnosticId ? result.diagnosticId : "PROJECT-DIRECTORY-QUERY-FAILED");
            return;
        }
        auto& snapshot = *state.snapshot;
        const char* issue = nullptr;
        if (!snapshot.project_->validate(
                *pending->files, snapshot.project_->location().epoch, issue)) {
            fail(issue);
            return;
        }
        const auto& request = pending->binding;
        const auto offset = request.cursor.offset;
        if (FileQueryContract::resultBytes(result) > request.maxResultBytes
            || result.totalEntries > state.budget.maxEntries || offset > result.totalEntries
            || offset != snapshot.entries_.size()
            || result.metadata.kind != FileEntryKind::Directory
            || result.metadata.version.kind != FileVersionKind::DirectorySnapshotSha256
            || !result.metadata.version.valid()
            || result.entries.size()
                != std::min<std::uint64_t>(request.pageSize, result.totalEntries - offset)
            || (offset
                && (result.totalEntries != state.totalEntries
                    || result.metadata.version != snapshot.version_))) {
            fail("PROJECT-DIRECTORY-RECEIPT");
            return;
        }
        const auto end = offset + result.entries.size();
        const bool more = end < result.totalEntries;
        if (!result.next.snapshot.valid()
            || (more
                    ? (result.next.offset != end || result.next.snapshot != result.metadata.version)
                    : (result.next.offset || result.next.snapshot.kind != FileVersionKind::None))) {
            fail("PROJECT-DIRECTORY-CURSOR");
            return;
        }
        auto nameBytes = state.nameBytes;
        std::string_view previous =
            snapshot.entries_.empty() ? std::string_view{} : snapshot.entries_.back().name;
        for (const auto& entry : result.entries) {
            if (!ProjectInputSnapshot::validLogicalPath(entry.name)
                || entry.name.find('/') != core::pmr::String::npos
                || (!previous.empty() && previous >= std::string_view(entry.name))
                || !entry.metadata.version.valid()
                || (entry.metadata.kind != FileEntryKind::RegularFile
                    && entry.metadata.kind != FileEntryKind::Directory
                    && entry.metadata.kind != FileEntryKind::Link)
                || entry.name.size() > state.budget.maxNameBytes - nameBytes) {
                fail("PROJECT-DIRECTORY-ENTRY-OR-BUDGET");
                return;
            }
            nameBytes += entry.name.size();
            previous = entry.name;
        }
        // Capacity was prepared before any request. Whole-page validation must
        // finish before moving its entries into the private candidate.
        for (auto& entry : result.entries)
            snapshot.entries_.push_back(std::move(entry));
        snapshot.version_ = result.metadata.version;
        state.totalEntries = result.totalEntries;
        state.nameBytes = nameBytes;
        state.cursor = result.next;
        state.phase = more ? ProjectDirectoryState::Ready : ProjectDirectoryState::Complete;
    } catch (...) {
        fail("PROJECT-DIRECTORY-ALLOCATION");
    }
}

core::SharedPtr<const ProjectDirectorySnapshot> ProjectDirectorySession::finish(
    std::uint64_t epoch) noexcept
{
    if (state_->phase != ProjectDirectoryState::Complete)
        return {};
    const char* issue = nullptr;
    if (!state_->snapshot->project_->validate(files_, epoch, issue)) {
        state_->phase = ProjectDirectoryState::Failed;
        state_->diagnostic = issue;
        return {};
    }
    state_->snapshot->sealed_ = true;
    state_->phase = ProjectDirectoryState::Sealed;
    return state_->snapshot;
}

} // namespace arp::editor
