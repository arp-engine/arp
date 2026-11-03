#include "editor/workspace/editor_scene_source_task.h"

#include "core/text/text_decoder.h"
#include "editor/project/project_source_verification.h"

#include <algorithm>

namespace arp::editor {
namespace {
// Keep the established Scene task's diagnostics while sharing the project's
// verification mechanism. Backend/source diagnostic IDs pass through unchanged.
const char* sceneVerificationDiagnostic(const char* issue) noexcept
{
    if (issue && std::string_view(issue) == "PROJECT-SOURCE-VERIFY-BUDGET")
        return "EDITOR-SCENE-SOURCE-DEPENDENCY-OR-BUDGET";
    if (issue && std::string_view(issue) == "PROJECT-SOURCE-VERIFY-PREPARATION")
        return "EDITOR-SCENE-SOURCE-PREPARATION";
    return issue;
}
} // namespace

struct EditorSceneSourceTask::State {
    ProjectSourceContext context;
    std::size_t sceneLimit = 0;
    SceneSourceState phase = SceneSourceState::Pending;
    const char* diagnostic = nullptr;
    core::pmr::String relative, decoded;
    core::UniquePtr<ProjectSourceVerification> verification;
    core::UniquePtr<ProjectSourceSession> pending;
    core::SharedPtr<const ProjectSourceSnapshot> scene;

    void fail(const char* issue) noexcept
    {
        phase = SceneSourceState::Failed;
        diagnostic = issue ? issue : "EDITOR-SCENE-SOURCE-INPUT";
        pending.reset();
        verification.reset();
        scene.reset();
        core::pmr::String{}.swap(decoded);
    }
};

EditorSceneSourceTask::EditorSceneSourceTask(ProjectSourceContext context,
    std::string_view relative,
    ProjectInputBudget budget,
    std::size_t maxSceneBytes)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.context = std::move(context);
    const char* issue = nullptr;
    if (!state.context.validate(issue) || !ProjectInputSnapshot::validBudget(budget)
        || !ProjectInputSnapshot::validLogicalPath(relative) || !maxSceneBytes) {
        state.fail(issue ? issue : "EDITOR-SCENE-SOURCE-INPUT");
        return;
    }
    const auto& scope = *state.context.snapshot;
    if (scope.size() >= budget.maxFiles || scope.totalBytes() >= budget.maxTotalBytes) {
        state.fail("EDITOR-SCENE-SOURCE-BUDGET");
        return;
    }
    state.relative = relative;
    state.sceneLimit =
        std::min({maxSceneBytes, budget.maxFileBytes, budget.maxTotalBytes - scope.totalBytes()});
    if (scope.find(relative)) {
        state.fail("EDITOR-SCENE-SOURCE-DEPENDENCY-OR-BUDGET");
        return;
    }
    state.verification = core::makeUnique<ProjectSourceVerification>(state.context, budget);
    if (state.verification->state() != ProjectSourceVerificationState::Pending)
        state.fail(sceneVerificationDiagnostic(state.verification->diagnostic()));
}

EditorSceneSourceTask::~EditorSceneSourceTask() = default;

SceneSourceState EditorSceneSourceTask::state() const noexcept
{
    return state_->phase;
}

const char* EditorSceneSourceTask::diagnostic() const noexcept
{
    return state_->diagnostic;
}

const ProjectSourceContext& EditorSceneSourceTask::context() const noexcept
{
    return state_->context;
}

std::string_view EditorSceneSourceTask::text() const noexcept
{
    return state_->decoded;
}

const ProjectSourceSnapshot::Entry& EditorSceneSourceTask::scene() const
{
    return *state_->scene->entryAt(0);
}

void EditorSceneSourceTask::cancel() noexcept
{
    if (state_->phase == SceneSourceState::Pending) {
        state_->fail("EDITOR-SCENE-SOURCE-CANCELLED");
        state_->phase = SceneSourceState::Cancelled;
    }
}

void EditorSceneSourceTask::advance() noexcept
{
    auto& state = *state_;
    if (state.phase != SceneSourceState::Pending)
        return;
    try {
        const char* issue = nullptr;
        if (!state.context.validate(issue)) {
            state.fail(issue);
            return;
        }
        if (state.verification) {
            state.verification->advance();
            if (state.verification->state() == ProjectSourceVerificationState::Pending)
                return;
            if (state.verification->state() != ProjectSourceVerificationState::Verified) {
                state.fail(sceneVerificationDiagnostic(state.verification->diagnostic()));
                return;
            }
            state.verification.reset();
        }
        if (state.pending) {
            if (state.pending->state() == ProjectSourceState::Pending)
                return;
            if (state.pending->state() != ProjectSourceState::Ready) {
                state.fail(state.pending->diagnostic());
                return;
            }
            auto receipt = state.pending->finish(state.context.epoch);
            if (!receipt) {
                state.fail(state.pending->diagnostic());
                return;
            }
            state.pending.reset();
            core::TextDecodeDesc text;
            text.bom = core::TextBomPolicy::Strip;
            text.nul = core::TextNulPolicy::Reject;
            text.maxOutputBytes = state.sceneLimit;
            const auto& bytes = receipt->entryAt(0)->receipt.bytes;
            auto decoded = core::TextDecoder::decode(bytes.empty()
                    ? core::StringView{}
                    : core::StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                text);
            if (decoded.status != core::TextDecodeStatus::Success) {
                state.fail(decoded.diagnosticId);
                return;
            }
            state.decoded.swap(decoded.text);
            state.scene = std::move(receipt);
            state.phase = SceneSourceState::Ready;
            return;
        }
        state.pending = core::makeUnique<ProjectSourceSession>(*state.context.files,
            state.context.snapshot->location(),
            ProjectInputBudget{1, state.sceneLimit, state.sceneLimit});
        if (state.pending->read(state.relative, state.sceneLimit)
            != core::io::FileIoStatus::Accepted)
            state.fail(state.pending->diagnostic());
    } catch (...) {
        state.fail("EDITOR-SCENE-SOURCE-PREPARATION");
    }
}
} // namespace arp::editor
