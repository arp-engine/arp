#include "editor/project/project_source_verification.h"

#include "core/hash/sha256.h"

#include <algorithm>

namespace arp::editor {
struct ProjectSourceVerification::State {
    ProjectSourceContext context;
    core::pmr::Vector<core::io::FileVersionToken> versions;
    core::UniquePtr<ProjectSourceSession> pending;
    std::size_t next = 0;
    ProjectSourceVerificationState phase = ProjectSourceVerificationState::Pending;
    const char* diagnostic = nullptr;

    void fail(const char* issue) noexcept
    {
        phase = ProjectSourceVerificationState::Failed;
        diagnostic = issue ? issue : "PROJECT-SOURCE-VERIFY-INPUT";
        pending.reset();
    }
};

ProjectSourceVerification::ProjectSourceVerification(
    ProjectSourceContext context, ProjectInputBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.context = std::move(context);
    const char* issue = nullptr;
    if (!state.context.validate(issue) || !ProjectInputSnapshot::validBudget(budget)) {
        state.fail(issue);
        return;
    }
    const auto& scope = *state.context.snapshot;
    if (scope.size() > budget.maxFiles || scope.totalBytes() > budget.maxTotalBytes) {
        state.fail("PROJECT-SOURCE-VERIFY-BUDGET");
        return;
    }
    state.versions.reserve(scope.size());
    for (std::size_t i = 0; i < scope.size(); ++i) {
        const auto& entry = *scope.entryAt(i);
        if (entry.receipt.bytes.size() > budget.maxFileBytes) {
            state.fail("PROJECT-SOURCE-VERIFY-BUDGET");
            return;
        }
        auto version = entry.receipt.version;
        if (version.kind != core::io::FileVersionKind::ContentSha256) {
            const auto& bytes = entry.receipt.bytes;
            core::Sha256 hash;
            hash.update(bytes.empty()
                    ? core::StringView{}
                    : core::StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            version = core::io::FileVersionEncoding::finish(
                hash, core::io::FileVersionKind::ContentSha256);
        }
        state.versions.push_back(version);
    }
}

ProjectSourceVerification::~ProjectSourceVerification() = default;

ProjectSourceVerificationState ProjectSourceVerification::state() const noexcept
{
    return state_->phase;
}

const char* ProjectSourceVerification::diagnostic() const noexcept
{
    return state_->diagnostic;
}

const ProjectSourceContext& ProjectSourceVerification::context() const noexcept
{
    return state_->context;
}

void ProjectSourceVerification::cancel() noexcept
{
    if (state_->phase == ProjectSourceVerificationState::Pending) {
        state_->fail("PROJECT-SOURCE-VERIFY-CANCELLED");
        state_->phase = ProjectSourceVerificationState::Cancelled;
    }
}

void ProjectSourceVerification::advance() noexcept
{
    auto& state = *state_;
    if (state.phase != ProjectSourceVerificationState::Pending)
        return;
    try {
        const char* issue = nullptr;
        if (!state.context.validate(issue)) {
            state.fail(issue);
            return;
        }
        if (state.pending) {
            if (state.pending->state() == ProjectSourceState::Pending)
                return;
            if (state.pending->state() != ProjectSourceState::Ready) {
                state.fail(state.pending->diagnostic());
                return;
            }
            auto verified = state.pending->finish(state.context.epoch);
            if (!verified) {
                state.fail(state.pending->diagnostic());
                return;
            }
            state.pending.reset();
            ++state.next;
        }
        if (state.next == state.versions.size()) {
            state.phase = ProjectSourceVerificationState::Verified;
            return;
        }
        const auto& entry = *state.context.snapshot->entryAt(state.next);
        const auto bytes = std::max<std::size_t>(1, entry.receipt.bytes.size());
        state.pending = core::makeUnique<ProjectSourceSession>(*state.context.files,
            state.context.snapshot->location(),
            ProjectInputBudget{1, bytes, bytes});
        if (state.pending->read(entry.relative, bytes, state.versions[state.next], bytes)
            != core::io::FileIoStatus::Accepted)
            state.fail(state.pending->diagnostic());
    } catch (...) {
        state.fail("PROJECT-SOURCE-VERIFY-PREPARATION");
    }
}
} // namespace arp::editor
