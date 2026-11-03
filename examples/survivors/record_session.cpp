#include "record_session.h"

#include "core/io/file_save_contract.h"

namespace arp::survivors {
using namespace core::io;

struct RecordSession::State {
    RecordPhase phase = RecordPhase::Empty;
    Record current, candidate;
    FileVersionToken baseline, candidateVersion;
    core::runtime::CancellationSource cancellation;
    core::pmr::String detail;
    const char* diagnostic = nullptr;
    FileSaveResult saveResult;

    void fail(const char* id) noexcept
    {
        diagnostic = id;
        phase = RecordPhase::Failed;
    }

    using Retained = core::SharedPtr<State>;

    static core::UniquePtr<Retained> consume(void* context) noexcept
    {
        return {static_cast<Retained*>(context),
            core::CpuAllocatedDeleter<Retained>{
                context, &core::destroyCpuAllocatedUnique<Retained>}};
    }

    static void loaded(void* context, FileTextReadResult input) noexcept
    {
        const auto retained = consume(context);
        auto& state = **retained;
        if (input.source.status == FileIoStatus::MissingFile) {
            state.current = {};
            state.baseline = {};
            state.phase = RecordPhase::Ready;
            return;
        }
        if (input.status() != FileIoStatus::Success) {
            state.fail(input.diagnosticId() ? input.diagnosticId() : "SURVIVORS-RECORD-READ");
            return;
        }
        try {
            Record candidate;
            if (!RecordSchema::prepare(input.decoded.text, candidate, state.detail)) {
                state.fail("SURVIVORS-RECORD-SCHEMA");
                return;
            }
            // The optimistic baseline is the exact whole-file byte snapshot,
            // never reserialized YAML or an opaque metadata/HTTP validator.
            core::Sha256 hash;
            hash.update({reinterpret_cast<const char*>(input.source.bytes.data()),
                input.source.bytes.size()});
            const auto baseline = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
            if (input.source.version.kind == FileVersionKind::ContentSha256
                && input.source.version != baseline) {
                state.fail("SURVIVORS-RECORD-CONTENT-VERSION");
                return;
            }
            state.current = candidate;
            state.baseline = baseline;
            state.phase = RecordPhase::Ready;
        } catch (...) {
            state.fail("SURVIVORS-RECORD-ALLOCATION");
        }
    }

    static void saved(void* context, FileSaveResult result) noexcept
    {
        const auto retained = consume(context);
        auto& state = **retained;
        state.saveResult = result;
        if (result.status != FileIoStatus::Success || result.deliveryStatus != FileIoStatus::Success
            || result.commitState != FileCommitState::Committed) {
            state.fail(result.diagnosticId ? result.diagnosticId : "SURVIVORS-RECORD-SAVE");
            return;
        }
        if (result.receipt.valid() || result.committedVersion != state.candidateVersion) {
            state.fail("SURVIVORS-RECORD-COMMIT-IDENTITY");
            return;
        }
        state.current = state.candidate;
        state.baseline = result.committedVersion;
        state.phase = RecordPhase::Ready;
    }
};

RecordSession::RecordSession(FileService& files, core::PoolHandle mount)
    : files_(files), mount_(mount), state_(core::makeShared<State>())
{}

RecordSession::~RecordSession()
{
    state_->cancellation.cancel();
}

bool RecordSession::load() noexcept
{
    auto& state = *state_;
    if (state.phase != RecordPhase::Empty)
        return false;
    try {
        FileTextReadRequest request;
        request.file.contractVersion = 1;
        request.file.pathScope = FilePathScope::MountedResource;
        request.file.mount = mount_;
        request.file.path = "record.yaml";
        request.file.maxBytes = maxFileBytes;
        request.file.cancellation = state.cancellation.token();
        request.text.maxOutputBytes = maxFileBytes;
        auto retained = core::makeUnique<State::Retained>(state_);
        state.phase = RecordPhase::Loading;
        const auto status = files_.readText(request, {State::loaded, retained.get()});
        if (status == FileIoStatus::Accepted) {
            retained.release();
            return true;
        }
        state.fail("SURVIVORS-RECORD-READ-NOT-ACCEPTED");
    } catch (...) {
        state.fail("SURVIVORS-RECORD-ALLOCATION");
    }
    return false;
}

bool RecordSession::save(const Record& candidate) noexcept
{
    auto& state = *state_;
    if (state.phase != RecordPhase::Ready)
        return false;
    try {
        const auto capability = files_.saveCapability(mount_);
        if (!capability.observedBaselineReplace || capability.operationReceipts) {
            state.saveResult.fail(
                FileIoStatus::Unsupported, "SURVIVORS-RECORD-SAVE-PROFILE-UNSUPPORTED");
            state.fail("SURVIVORS-RECORD-SAVE-PROFILE-UNSUPPORTED");
            return false;
        }
        const auto text = RecordSchema::encode(candidate);
        Record checked;
        state.detail.clear();
        if (!RecordSchema::prepare(text, checked, state.detail)) {
            state.fail("SURVIVORS-RECORD-SCHEMA");
            return false;
        }
        if (text.size() > maxFileBytes) {
            state.fail("SURVIVORS-RECORD-BYTE-BUDGET");
            return false;
        }
        FileSaveRequest request;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = mount_;
        request.path = "record.yaml";
        request.bytes.assign(text.begin(), text.end());
        request.maxWriteBytes = maxFileBytes;
        if (state.baseline.kind == FileVersionKind::ContentSha256) {
            request.precondition = FileSavePrecondition::ContentSha256;
            request.expectedVersion = state.baseline;
            request.maxBaselineBytes = maxFileBytes;
        }
        state.candidateVersion = FileSaveContract::contentVersion(request);
        request.cancellation = state.cancellation.token();
        auto retained = core::makeUnique<State::Retained>(state_);
        state.candidate = checked;
        state.phase = RecordPhase::Saving;
        const auto status = files_.save(std::move(request), {State::saved, retained.get()});
        if (status == FileIoStatus::Accepted) {
            retained.release();
            return true;
        }
        state.saveResult.fail(status, "SURVIVORS-RECORD-SAVE-NOT-ACCEPTED");
        state.fail("SURVIVORS-RECORD-SAVE-NOT-ACCEPTED");
    } catch (...) {
        state.fail("SURVIVORS-RECORD-ALLOCATION");
    }
    return false;
}

RecordPhase RecordSession::phase() const noexcept
{
    return state_->phase;
}

const Record& RecordSession::record() const noexcept
{
    return state_->current;
}

const char* RecordSession::diagnostic() const noexcept
{
    return state_->detail.empty() ? state_->diagnostic : state_->detail.c_str();
}

const FileSaveResult& RecordSession::saveResult() const noexcept
{
    return state_->saveResult;
}
} // namespace arp::survivors
