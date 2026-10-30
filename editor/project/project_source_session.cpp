#include "editor/project/project_source_session.h"

#include "core/hash/sha256.h"
#include "core/io/file_read_contract.h"

#include <algorithm>
#include <type_traits>

namespace arp::editor {
namespace {
using namespace core::io;

bool validLocation(const ProjectSourceLocation& location)
{
    return location.mount && location.epoch
        && (location.root.empty() || ProjectInputSnapshot::validLogicalPath(location.root));
}

core::pmr::String sourcePath(const ProjectSourceLocation& location, std::string_view relative)
{
    core::pmr::String path(location.root, &core::defaultCpuMemoryResource());
    if (!path.empty())
        path += '/';
    path.append(relative.data(), relative.size());
    return path;
}

const char* statusDiagnostic(FileIoStatus status) noexcept
{
    switch (status) {
    case FileIoStatus::StaleLocation:
        return "PROJECT-SOURCE-STALE-MOUNT";
    case FileIoStatus::ServiceClosed:
        return "PROJECT-SOURCE-SERVICE-CLOSED";
    case FileIoStatus::Cancelled:
        return "PROJECT-SOURCE-CANCELLED";
    case FileIoStatus::VersionMismatch:
        return "PROJECT-SOURCE-VERSION";
    case FileIoStatus::BudgetExceeded:
        return "PROJECT-SOURCE-BUDGET";
    case FileIoStatus::MissingFile:
        return "PROJECT-SOURCE-MISSING";
    case FileIoStatus::Unsupported:
        return "PROJECT-SOURCE-UNSUPPORTED";
    case FileIoStatus::Timeout:
        return "PROJECT-SOURCE-TIMEOUT";
    case FileIoStatus::QueueFull:
        return "PROJECT-SOURCE-QUEUE-FULL";
    default:
        return "PROJECT-SOURCE-READ-FAILED";
    }
}

const char* ioDiagnostic(const FileReadResult& result) noexcept
{
    return result.diagnosticId ? result.diagnosticId : statusDiagnostic(result.status);
}
} // namespace

ProjectSourceSnapshot::ProjectSourceSnapshot(
    ProjectSourceLocation location, ProjectInputBudget budget)
    : location_(std::move(location)), budget_(budget), identities_({}, budget)
{
    if (ProjectInputSnapshot::validBudget(budget_))
        entries_.reserve(budget_.maxFiles);
}

const ProjectSourceSnapshot::Entry* ProjectSourceSnapshot::lookup(
    std::string_view relative) const noexcept
{
    for (const auto& entry : entries_)
        if (entry.relative == relative)
            return &entry;
    return nullptr;
}

const ProjectSourceSnapshot::Entry* ProjectSourceSnapshot::find(
    std::string_view relative) const noexcept
{
    return sealed_ ? lookup(relative) : nullptr;
}

bool ProjectSourceSnapshot::append(std::string_view relative,
    const FileReadRequest& binding,
    FileReadResult receipt,
    const char*& diagnostic) noexcept
{
    diagnostic = nullptr;
    const auto fail = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    try {
        const auto validation = FileReadContract::validate(binding);
        if (validation.status != FileIoStatus::Success)
            return fail(validation.diagnostic);
        if (sealed_ || !validLocation(location_) || !ProjectInputSnapshot::validBudget(budget_))
            return fail("PROJECT-SOURCE-CONFIG");
        if (!ProjectInputSnapshot::validLogicalPath(relative)
            || binding.path != sourcePath(location_, relative)
            || !(binding.mount == location_.mount)
            || binding.pathScope != FilePathScope::MountedResource || binding.contractVersion != 1
            || binding.range.mode != FileReadRangeMode::WholeFile
            || binding.partialPolicy != FilePartialReadPolicy::DenyPartial)
            return fail("PROJECT-SOURCE-BINDING");
        if (binding.cancellation.cancelled())
            return fail("PROJECT-SOURCE-CANCELLED");
        if (receipt.status != FileIoStatus::Success)
            return fail(ioDiagnostic(receipt));
        if (lookup(relative))
            return fail("PROJECT-SOURCE-DUPLICATE");
        if (!receipt.version.valid()
            || receipt.version.kind == FileVersionKind::DirectorySnapshotSha256
            || (binding.expectedVersion.kind != FileVersionKind::None
                && binding.expectedVersion != receipt.version))
            return fail("PROJECT-SOURCE-VERSION");
        if (entries_.size() >= budget_.maxFiles || receipt.bytes.size() > binding.maxBytes
            || receipt.bytes.size() > budget_.maxFileBytes
            || receipt.bytes.size() > budget_.maxTotalBytes - totalBytes_)
            return fail("PROJECT-SOURCE-BUDGET");
        const core::StringView bytes(
            receipt.bytes.empty() ? "" : reinterpret_cast<const char*>(receipt.bytes.data()),
            receipt.bytes.size());
        if (receipt.version.kind == FileVersionKind::ContentSha256) {
            const auto digest = core::Sha256::digest(bytes);
            if (!std::equal(digest.begin(),
                    digest.end(),
                    receipt.version.digest.begin(),
                    receipt.version.digest.end()))
                return fail("PROJECT-SOURCE-INTEGRITY");
        }
        Entry entry;
        entry.relative = relative;
        entry.binding = binding;
        entry.receipt = std::move(receipt);
        const core::StringView ownedBytes(entry.receipt.bytes.empty()
                ? ""
                : reinterpret_cast<const char*>(entry.receipt.bytes.data()),
            entry.receipt.bytes.size());
        entries_.reserve(entries_.size() + 1);
        core::pmr::String issue{&core::defaultCpuMemoryResource()};
        if (!identities_.recordLogical(relative, ownedBytes, issue))
            return fail("PROJECT-SOURCE-IDENTITY");
        static_assert(std::is_nothrow_move_constructible_v<Entry>);
        totalBytes_ += entry.receipt.bytes.size();
        entries_.push_back(std::move(entry));
        return true;
    } catch (...) {
        return fail("PROJECT-SOURCE-ALLOCATION");
    }
}

bool ProjectSourceSnapshot::validate(
    FileService& service, std::uint64_t epoch, const char*& diagnostic) const noexcept
{
    diagnostic = nullptr;
    if (!sealed_ || entries_.empty() || epoch != location_.epoch) {
        diagnostic = "PROJECT-SOURCE-EPOCH-OR-UNSEALED";
        return false;
    }
    try {
        for (const auto& entry : entries_) {
            if (entry.binding.cancellation.cancelled()) {
                diagnostic = "PROJECT-SOURCE-CANCELLED";
                return false;
            }
            const auto validation = service.validateRead(entry.binding);
            if (validation.status != FileIoStatus::Success) {
                diagnostic = ioDiagnostic(validation);
                return false;
            }
        }
        return true;
    } catch (...) {
        diagnostic = "PROJECT-SOURCE-ALLOCATION";
        return false;
    }
}

bool ProjectSourceContext::validate(const char*& diagnostic) const noexcept
{
    if (!snapshot || !files || !epoch) {
        diagnostic = "PROJECT-SOURCE-CONTEXT";
        return false;
    }
    return snapshot->validate(*files, epoch, diagnostic);
}

struct ProjectSourceSession::State {
    core::SharedPtr<ProjectSourceSnapshot> snapshot;
    core::runtime::CancellationSource cancellation;
    ProjectSourceState phase = ProjectSourceState::Ready;
    const char* diagnostic = nullptr;
};

struct ProjectSourceSession::Pending {
    core::SharedPtr<State> state;
    FileService* service = nullptr;
    core::pmr::String relative{&core::defaultCpuMemoryResource()};
    FileReadRequest binding;
};

ProjectSourceSession::ProjectSourceSession(
    FileService& service, ProjectSourceLocation location, ProjectInputBudget budget)
    : service_(service), state_(core::makeShared<State>())
{
    if (!validLocation(location) || !ProjectInputSnapshot::validBudget(budget)) {
        state_->phase = ProjectSourceState::Failed;
        state_->diagnostic = "PROJECT-SOURCE-CONFIG";
        return;
    }
    state_->snapshot = core::makeShared<ProjectSourceSnapshot>(std::move(location), budget);
}

ProjectSourceSession::~ProjectSourceSession()
{
    if (state_->phase != ProjectSourceState::Sealed)
        cancel();
}

void ProjectSourceSession::cancel() noexcept
{
    state_->cancellation.cancel();
    state_->phase = ProjectSourceState::Cancelled;
    state_->diagnostic = "PROJECT-SOURCE-CANCELLED";
}

ProjectSourceState ProjectSourceSession::state() const noexcept
{
    return state_->phase;
}

const char* ProjectSourceSession::diagnostic() const noexcept
{
    return state_->diagnostic;
}

const ProjectSourceSnapshot::Entry* ProjectSourceSession::acquired(
    std::string_view relative) const noexcept
{
    return state_->snapshot
            && (state_->phase == ProjectSourceState::Ready
                || state_->phase == ProjectSourceState::Sealed)
        ? state_->snapshot->lookup(relative)
        : nullptr;
}

FileIoStatus ProjectSourceSession::read(std::string_view relative,
    std::size_t maxBytes,
    FileVersionToken expected,
    std::uint64_t maxVersionBytes) noexcept
{
    if (state_->phase != ProjectSourceState::Ready)
        return state_->phase == ProjectSourceState::Pending ? FileIoStatus::QueueFull
                                                            : FileIoStatus::InvalidRequest;
    try {
        auto& inputs = *state_->snapshot;
        const auto reject = [&](const char* id, FileIoStatus status) {
            state_->diagnostic = id;
            state_->phase = ProjectSourceState::Failed;
            return status;
        };
        if (!ProjectInputSnapshot::validLogicalPath(relative))
            return reject("PROJECT-SOURCE-PATH", FileIoStatus::InvalidRequest);
        if (inputs.lookup(relative))
            return reject("PROJECT-SOURCE-DUPLICATE", FileIoStatus::InvalidRequest);
        if (!maxBytes || maxBytes > inputs.budget_.maxFileBytes
            || inputs.size() >= inputs.budget_.maxFiles
            || maxBytes > inputs.budget_.maxTotalBytes - inputs.totalBytes_)
            return reject("PROJECT-SOURCE-BUDGET", FileIoStatus::BudgetExceeded);
        auto pending = core::makeUnique<Pending>();
        pending->state = state_;
        pending->service = &service_;
        pending->relative = relative;
        auto& binding = pending->binding;
        binding.contractVersion = 1;
        binding.pathScope = FilePathScope::MountedResource;
        binding.path = sourcePath(inputs.location_, relative);
        binding.mount = inputs.location_.mount;
        binding.maxBytes = maxBytes;
        binding.expectedVersion = expected;
        binding.maxVersionBytes = maxVersionBytes;
        binding.cancellation = state_->cancellation.token();
        const auto validation = service_.validateRead(binding);
        if (validation.status != FileIoStatus::Success)
            return reject(ioDiagnostic(validation), validation.status);
        const auto submitted = service_.read(binding, {complete, pending.get()});
        if (submitted != FileIoStatus::Accepted)
            return reject(statusDiagnostic(submitted), submitted);
        state_->phase = ProjectSourceState::Pending;
        pending.release();
        return submitted;
    } catch (...) {
        state_->phase = ProjectSourceState::Failed;
        state_->diagnostic = "PROJECT-SOURCE-ALLOCATION";
        return FileIoStatus::BudgetExceeded;
    }
}

void ProjectSourceSession::complete(void* context, FileReadResult receipt) noexcept
{
    core::UniquePtr<Pending> pending{static_cast<Pending*>(context),
        core::CpuAllocatedDeleter<Pending>{context, &core::destroyCpuAllocatedUnique<Pending>}};
    auto& state = *pending->state;
    if (state.cancellation.token().cancelled())
        return;
    try {
        const auto validation = pending->service->validateRead(pending->binding);
        if (validation.status != FileIoStatus::Success) {
            state.diagnostic = ioDiagnostic(validation);
            state.phase = ProjectSourceState::Failed;
            return;
        }
        state.phase = state.snapshot->append(
                          pending->relative, pending->binding, std::move(receipt), state.diagnostic)
            ? ProjectSourceState::Ready
            : ProjectSourceState::Failed;
    } catch (...) {
        state.phase = ProjectSourceState::Failed;
        state.diagnostic = "PROJECT-SOURCE-ALLOCATION";
    }
}

core::SharedPtr<const ProjectSourceSnapshot> ProjectSourceSession::finish(
    std::uint64_t epoch) noexcept
{
    if (state_->phase != ProjectSourceState::Ready && state_->phase != ProjectSourceState::Sealed)
        return {};
    auto& snapshot = *state_->snapshot;
    snapshot.sealed_ = true;
    if (!snapshot.validate(service_, epoch, state_->diagnostic)) {
        state_->phase = ProjectSourceState::Failed;
        return {};
    }
    state_->phase = ProjectSourceState::Sealed;
    return state_->snapshot;
}

} // namespace arp::editor
