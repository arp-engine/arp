#include "editor/project/project_file_read_task.h"

namespace arp::editor {
using namespace core::io;

struct ProjectFileReadTask::State {
    FileService* files = nullptr;
    ProjectSourceLocation location;
    FileReadRequest binding;
    FileReadResult result;
    core::runtime::CancellationSource cancellation;
    ProjectFileReadState phase = ProjectFileReadState::Pending;
    const char* diagnostic = nullptr;
    bool submitted = false, done = false;

    State() { result.status = FileIoStatus::BackendUnavailable; }

    void fail(const char* issue) noexcept
    {
        cancellation.cancel();
        phase = ProjectFileReadState::Failed;
        diagnostic = issue ? issue : "PROJECT-FILE-READ-FAILED";
    }
};

ProjectFileReadTask::ProjectFileReadTask(FileService& files,
    ProjectSourceLocation location,
    std::string_view relative,
    std::size_t maxBytes,
    FileVersionToken expected,
    std::uint64_t maxVersionBytes)
    : state_(core::makeShared<State>())
{
    auto& s = *state_;
    s.files = &files;
    s.location = std::move(location);
    try {
        s.binding.contractVersion = 1;
        s.binding.pathScope = FilePathScope::MountedResource;
        s.binding.mount = s.location.mount;
        s.binding.path = s.location.pathFor(relative);
        s.binding.maxBytes = maxBytes;
        s.binding.expectedVersion = expected;
        s.binding.maxVersionBytes = maxVersionBytes;
        s.binding.cancellation = s.cancellation.token();
    } catch (...) {
        s.fail("PROJECT-FILE-READ-LOCATION");
    }
}

ProjectFileReadTask::~ProjectFileReadTask()
{
    cancel();
}

ProjectFileReadState ProjectFileReadTask::state() const noexcept
{
    return state_->phase;
}

const char* ProjectFileReadTask::diagnostic() const noexcept
{
    return state_->diagnostic;
}

const FileReadRequest& ProjectFileReadTask::binding() const noexcept
{
    return state_->binding;
}

const FileReadResult& ProjectFileReadTask::result() const noexcept
{
    return state_->result;
}

FileReadResult ProjectFileReadTask::takeResult() noexcept
{
    if (state_->phase == ProjectFileReadState::Complete) {
        state_->phase = ProjectFileReadState::Consumed;
        return std::move(state_->result);
    }
    FileReadResult result;
    result.status = FileIoStatus::InvalidRequest;
    result.diagnosticId = "PROJECT-FILE-READ-NOT-COMPLETE";
    return result;
}

void ProjectFileReadTask::cancel() noexcept
{
    if (state_->phase == ProjectFileReadState::Pending) {
        state_->fail("PROJECT-FILE-READ-CANCELLED");
        state_->phase = ProjectFileReadState::Cancelled;
    }
}

void ProjectFileReadTask::completed(void* context, FileReadResult result) noexcept
{
    using Retained = core::SharedPtr<State>;
    core::UniquePtr<Retained> retained{static_cast<Retained*>(context),
        core::CpuAllocatedDeleter<Retained>{context, &core::destroyCpuAllocatedUnique<Retained>}};
    auto& s = **retained;
    if (s.phase != ProjectFileReadState::Pending)
        return;
    s.result = std::move(result);
    s.done = true;
}

void ProjectFileReadTask::advance(std::uint64_t currentEpoch) noexcept
{
    auto& s = *state_;
    if (s.phase != ProjectFileReadState::Pending)
        return;
    try {
        if (currentEpoch != s.location.epoch) {
            s.fail("PROJECT-FILE-READ-EPOCH");
            return;
        }
        const auto admission = s.files->validateRead(s.binding);
        if (admission.status != FileIoStatus::Success) {
            s.fail(admission.diagnosticId ? admission.diagnosticId : "PROJECT-FILE-READ-ADMISSION");
            return;
        }
        if (s.done) {
            if (s.result.status != FileIoStatus::Success
                && s.result.status != FileIoStatus::MissingFile)
                s.fail(s.result.diagnosticId);
            else if (s.result.bytes.size() > s.binding.maxBytes
                || (s.result.status == FileIoStatus::MissingFile && !s.result.bytes.empty()))
                s.fail("PROJECT-FILE-READ-RESULT");
            else
                s.phase = ProjectFileReadState::Complete;
            return;
        }
        if (s.submitted)
            return;
        auto retained = core::makeUnique<core::SharedPtr<State>>(state_);
        if (s.files->read(s.binding, {completed, retained.get()}) != FileIoStatus::Accepted) {
            s.fail("PROJECT-FILE-READ-NOT-ACCEPTED");
            return;
        }
        retained.release();
        s.submitted = true;
    } catch (...) {
        s.fail("PROJECT-FILE-READ-PREPARATION");
    }
}
} // namespace arp::editor
