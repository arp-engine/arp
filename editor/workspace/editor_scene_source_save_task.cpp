#include "editor/workspace/editor_scene_source_save_task.h"

#include "core/io/file_query_contract.h"
#include "core/io/file_save_contract.h"
#include "editor/project/project_source_verification.h"

#include <algorithm>

namespace arp::editor {
using namespace core::io;

struct EditorSceneSourceSaveTask::State {
    ProjectSourceContext context;
    core::pmr::String relative;
    core::pmr::Vector<std::uint8_t> baseline;
    core::UniquePtr<ProjectSourceVerification> verification;
    core::UniquePtr<ProjectSourceSession> read;
    core::UniquePtr<ProjectFileSaveTask> save;
    core::runtime::CancellationSource cancellation;
    FileQueryResult query;
    FileSaveResult failure;
    FileVersionToken version;
    std::size_t maxBytes = 0;
    bool bound = false, overwrite = false, existed = false;
    bool querySubmitted = false, queryDone = false, ready = false, failed = false,
         cancelled = false;
    const char* diagnostic = nullptr;
};

EditorSceneSourceSaveTask::EditorSceneSourceSaveTask(ProjectSourceContext context,
    std::string_view relative,
    ProjectInputBudget budget,
    std::size_t maxBytes,
    bool bound,
    core::pmr::Vector<std::uint8_t> baseline,
    bool overwrite)
    : state_(core::makeShared<State>())
{
    auto& s = *state_;
    s.context = std::move(context);
    s.relative = relative;
    s.maxBytes = std::min(maxBytes, budget.maxFileBytes);
    s.bound = bound;
    s.existed = bound;
    s.overwrite = overwrite;
    s.baseline.swap(baseline);
    const char* issue = nullptr;
    if (!s.context.validate(issue) || !s.maxBytes || s.baseline.capacity() > s.maxBytes
        || (!bound && !s.baseline.empty()) || !ProjectInputSnapshot::validLogicalPath(relative)
        || s.context.snapshot->find(relative)
        || !s.context.files->saveCapability(s.context.snapshot->location().mount)
            .observedBaselineReplace) {
        fail(issue ? issue : "EDITOR-SCENE-SOURCE-SAVE-INPUT");
        return;
    }
    (void)s.context.snapshot->location().pathFor(relative);
    s.verification = core::makeUnique<ProjectSourceVerification>(s.context, budget);
}

EditorSceneSourceSaveTask::~EditorSceneSourceSaveTask()
{
    cancel();
}

void EditorSceneSourceSaveTask::queried(void* context, FileQueryResult result) noexcept
{
    using Retained = core::SharedPtr<State>;
    core::UniquePtr<Retained> retained{static_cast<Retained*>(context),
        core::CpuAllocatedDeleter<Retained>{context, &core::destroyCpuAllocatedUnique<Retained>}};
    (*retained)->query = std::move(result);
    (*retained)->queryDone = true;
}

void EditorSceneSourceSaveTask::fail(const char* diagnostic) noexcept
{
    auto& s = *state_;
    if (s.save && s.save->submitted() && !s.save->terminal()) {
        cancel();
        return;
    }
    s.diagnostic = diagnostic ? diagnostic : "EDITOR-SCENE-SOURCE-SAVE-FAILED";
    s.failed = true;
    s.ready = false;
    s.failure.fail(FileIoStatus::InvalidRequest, s.diagnostic);
    s.verification.reset();
    s.read.reset();
}

void EditorSceneSourceSaveTask::cancel() noexcept
{
    auto& s = *state_;
    s.cancelled = true;
    s.cancellation.cancel();
    if (s.save) {
        s.save->cancel();
        s.save->advance();
    } else if (!s.failed) {
        fail("EDITOR-SCENE-SOURCE-SAVE-CANCELLED");
        s.failure.status = FileIoStatus::Cancelled;
    }
}

void EditorSceneSourceSaveTask::advance() noexcept
{
    auto& s = *state_;
    if (s.failed || s.ready)
        return;
    if (s.save) {
        s.save->advance();
        return;
    }
    try {
        const char* issue = nullptr;
        if (!s.context.validate(issue)) {
            fail(issue);
            return;
        }
        if (s.verification) {
            s.verification->advance();
            if (s.verification->state() == ProjectSourceVerificationState::Pending)
                return;
            if (s.verification->state() != ProjectSourceVerificationState::Verified) {
                fail(s.verification->diagnostic());
                return;
            }
            s.verification.reset();
        }
        if (s.bound) {
            s.ready = true;
            return;
        }
        if (!s.querySubmitted) {
            FileQueryRequest query;
            query.mount = s.context.snapshot->location().mount;
            query.pathScope = FilePathScope::MountedResource;
            query.path = s.context.snapshot->location().pathFor(s.relative);
            query.maxResultBytes = FileQueryContract::resultBytes(FileQueryResult{});
            query.versionPolicy = FileVersionPolicy::ContentSha256;
            query.maxVersionBytes = s.maxBytes;
            query.cancellation = s.cancellation.token();
            auto retained = core::makeUnique<core::SharedPtr<State>>(state_);
            const auto status = s.context.files->query(query, {queried, retained.get()});
            if (status != FileIoStatus::Accepted) {
                fail("EDITOR-SCENE-SOURCE-TARGET-QUERY-REJECTED");
                return;
            }
            retained.release();
            s.querySubmitted = true;
            return;
        }
        if (!s.queryDone)
            return;
        if (s.query.status == FileIoStatus::MissingFile) {
            s.ready = true;
            return;
        }
        if (s.query.status != FileIoStatus::Success) {
            fail(s.query.diagnosticId);
            return;
        }
        if (s.query.metadata.kind != FileEntryKind::RegularFile) {
            fail("EDITOR-SCENE-SOURCE-TARGET-NOT-FILE");
            return;
        }
        if (s.query.metadata.version.kind != FileVersionKind::ContentSha256) {
            fail("EDITOR-SCENE-SOURCE-TARGET-VERSION-REQUIRED");
            return;
        }
        if (!s.overwrite) {
            fail("EDITOR-SCENE-SOURCE-OVERWRITE-REQUIRED");
            return;
        }
        if (!s.read) {
            s.read = core::makeUnique<ProjectSourceSession>(*s.context.files,
                s.context.snapshot->location(),
                ProjectInputBudget{1, s.maxBytes, s.maxBytes});
            if (s.read->read(s.relative, s.maxBytes, s.query.metadata.version, s.maxBytes)
                != FileIoStatus::Accepted)
                fail(s.read->diagnostic());
            return;
        }
        if (s.read->state() == ProjectSourceState::Pending)
            return;
        const auto snapshot = s.read->finish(s.context.epoch);
        if (!snapshot) {
            fail(s.read->diagnostic());
            return;
        }
        s.baseline = snapshot->entryAt(0)->receipt.bytes;
        s.read.reset();
        s.existed = true;
        s.ready = true;
    } catch (...) {
        fail("EDITOR-SCENE-SOURCE-SAVE-PREPARATION");
    }
}

bool EditorSceneSourceSaveTask::write(core::pmr::Vector<std::uint8_t> bytes) noexcept
{
    auto& s = *state_;
    if (!s.ready || s.failed || s.save || s.cancelled)
        return false;
    try {
        FileSaveRequest request;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = s.context.snapshot->location().mount;
        request.path = s.context.snapshot->location().pathFor(s.relative);
        request.bytes.swap(bytes);
        request.maxWriteBytes = s.maxBytes;
        if (s.existed) {
            FileSaveRequest previous;
            previous.bytes.swap(s.baseline);
            request.expectedVersion = FileSaveContract::contentVersion(previous);
            s.baseline.swap(previous.bytes);
            request.precondition = FileSavePrecondition::ContentSha256;
            request.maxBaselineBytes = s.maxBytes;
        }
        const auto validation = FileSaveContract::validate(request);
        if (validation.diagnostic) {
            fail(validation.diagnostic);
            return false;
        }
        s.version = FileSaveContract::contentVersion(request);
        s.save = core::makeUnique<ProjectFileSaveTask>(*s.context.files, std::move(request));
        s.ready = false;
        return true;
    } catch (...) {
        fail("EDITOR-SCENE-SOURCE-SAVE-PREPARATION");
        return false;
    }
}

ProjectFileReceiptObservation EditorSceneSourceSaveTask::receiptObservation() const noexcept
{
    return state_->save ? state_->save->receiptObservation() : ProjectFileReceiptObservation{};
}

bool EditorSceneSourceSaveTask::ready() const noexcept
{
    return state_->ready;
}

bool EditorSceneSourceSaveTask::terminal() const noexcept
{
    return state_->failed || (state_->save && state_->save->terminal());
}

bool EditorSceneSourceSaveTask::submitted() const noexcept
{
    return state_->save && state_->save->submitted();
}

bool EditorSceneSourceSaveTask::cancelled() const noexcept
{
    return state_->cancelled;
}

bool EditorSceneSourceSaveTask::targetExisted() const noexcept
{
    return state_->existed;
}

const char* EditorSceneSourceSaveTask::diagnostic() const noexcept
{
    return state_->diagnostic;
}

const ProjectSourceContext& EditorSceneSourceSaveTask::context() const noexcept
{
    return state_->context;
}

const core::pmr::Vector<std::uint8_t>& EditorSceneSourceSaveTask::previousBytes() const noexcept
{
    return state_->baseline;
}

const FileSaveResult& EditorSceneSourceSaveTask::result() const noexcept
{
    return state_->save ? state_->save->result() : state_->failure;
}

FileVersionToken EditorSceneSourceSaveTask::intendedVersion() const noexcept
{
    return state_->version;
}
} // namespace arp::editor
