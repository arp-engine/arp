#include "editor/workspace/editor_source_recovery_store.h"

#include "resource/resource_manager.h"

namespace arp::editor {
using namespace core::io;

bool EditorSourceRecoveryWrite::submitted() const noexcept
{
    return operation_ && operation_->submitted();
}

bool EditorSourceRecoveryWrite::terminal() const noexcept
{
    return operation_ && operation_->terminal();
}

const FileSaveResult& EditorSourceRecoveryWrite::result() const noexcept
{
    return operation_ ? operation_->result() : empty_;
}

struct EditorSourceRecoveryStore::State {
    struct Slot {
        core::pmr::Vector<std::uint8_t> bytes;
        FileVersionToken version;
        std::uint64_t sequence = 0, epoch = 0;
        bool present = false;
    };

    FileService* files = nullptr;
    EditorSourceRecoveryStoreDesc desc;
    SourceRecoveryStoreState phase = SourceRecoveryStoreState::Opening;
    std::array<Slot, 2> slots;
    core::UniquePtr<ProjectFileReadTask> read;
    core::UniquePtr<ProjectSourceVerification> verification;
    core::SharedPtr<EditorSourceRecoveryWrite> write;
    core::pmr::Vector<std::uint8_t> pending;
    core::pmr::String detail;
    const char* diagnostic = nullptr;
    std::size_t nextRead = 0, target = 0;
    std::size_t payloadCapacity = 0;
    std::uint64_t nextSequence = 1;
    FileVersionToken pendingVersion;
    std::optional<TimePoint> lastSubmission;
    TimePoint pendingTime;

    std::size_t resident() const noexcept
    {
        return slots[0].bytes.capacity() + slots[1].bytes.capacity() + pending.capacity();
    }

    std::size_t latest() const noexcept { return slots[1].sequence > slots[0].sequence ? 1u : 0u; }

    bool current(std::uint64_t epoch, const char*& issue) const noexcept
    {
        issue = "EDITOR-RECOVERY-STORE-EPOCH";
        if (epoch != desc.storage.epoch)
            return false;
        try {
            FileReadRequest probe;
            probe.contractVersion = 1;
            probe.pathScope = FilePathScope::MountedResource;
            probe.mount = desc.storage.mount;
            probe.path = desc.storage.pathFor(desc.slots[0]);
            probe.maxBytes = desc.records.maxEnvelopeBytes;
            const auto admission = files->validateRead(probe);
            if (admission.status != FileIoStatus::Success) {
                issue = admission.diagnosticId ? admission.diagnosticId
                                               : "EDITOR-RECOVERY-STORE-LOCATION";
                return false;
            }
            issue = nullptr;
            return true;
        } catch (...) {
            issue = "EDITOR-RECOVERY-STORE-ADMISSION";
            return false;
        }
    }

    void fail(const char* issue) noexcept
    {
        diagnostic = issue ? issue : "EDITOR-RECOVERY-STORE-FAILED";
        phase = SourceRecoveryStoreState::Failed;
        read.reset();
        verification.reset();
        if (write && !write->operation_->terminal())
            write->operation_->reject(FileIoStatus::InvalidRequest, diagnostic);
        core::pmr::Vector<std::uint8_t>{}.swap(pending);
        payloadCapacity = 0;
    }

    bool identity(const EditorRecoverySnapshot& record) const noexcept
    {
        return record.source && record.project && record.sessionId == desc.sessionId
            && record.project->id == desc.projectId && record.source->alias == desc.sourceAlias
            && record.source->root == desc.sourceRoot;
    }

    bool acceptRead()
    {
        auto result = read->takeResult();
        read.reset();
        auto& slot = slots[nextRead];
        const bool present = result.status == FileIoStatus::Success;
        if (result.bytes.capacity() > desc.maxRawResidentBytes - resident() - payloadCapacity) {
            diagnostic = "EDITOR-RECOVERY-STORE-RESIDENT-BUDGET";
            return false;
        }
        if (phase == SourceRecoveryStoreState::Pending) {
            if (present != slot.present || result.bytes != slot.bytes) {
                diagnostic = "EDITOR-RECOVERY-STORE-SLOT-CHANGED";
                return false;
            }
            return true;
        }
        if (present && desc.mode == SourceRecoveryStoreMode::Fresh) {
            diagnostic = "EDITOR-RECOVERY-STORE-ALREADY-EXISTS";
            return false;
        }
        if (!present)
            return true;
        EditorRecoverySnapshot record;
        if (!EditorRecoveryCodec::decode(
                {reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()},
                record,
                detail,
                desc.records)
            || !identity(record) || (record.sequence - 1u) % slots.size() != nextRead) {
            diagnostic = "EDITOR-RECOVERY-STORE-SLOT-RECORD";
            return false;
        }
        core::Sha256 hash;
        hash.update({reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()});
        slot.version = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
        slot.bytes.swap(result.bytes);
        slot.sequence = record.sequence;
        slot.epoch = record.workspaceEpoch;
        slot.present = true;
        return true;
    }
};

EditorSourceRecoveryStore::EditorSourceRecoveryStore(
    FileService& files, EditorSourceRecoveryStoreDesc desc)
    : state_(core::makeUnique<State>())
{
    auto& s = *state_;
    s.files = &files;
    s.desc = std::move(desc);
    const auto& configured = s.desc;
    if (!EditorRecoveryCodec::validateBudget(configured.records, s.detail)
        || !ProjectInputSnapshot::validBudget(configured.inputs)
        || (configured.mode != SourceRecoveryStoreMode::Fresh
            && configured.mode != SourceRecoveryStoreMode::Resume
            && configured.mode != SourceRecoveryStoreMode::Inspect)
        || !resource::isResourceUuid(configured.sessionId)
        || !resource::isResourceUuid(configured.projectId)
        || !ProjectInputSnapshot::validLogicalPath(configured.sourceAlias)
        || configured.sourceAlias.find('/') != core::pmr::String::npos
        || configured.sourceAlias.size() > 128u
        || (!configured.sourceRoot.empty()
            && !ProjectInputSnapshot::validLogicalPath(configured.sourceRoot))
        || !configured.storage.mount || !configured.storage.epoch
        || !ProjectInputSnapshot::validLogicalPath(configured.slots[0])
        || !ProjectInputSnapshot::validLogicalPath(configured.slots[1])
        || configured.slots[0] == configured.slots[1]
        || configured.maxRawResidentBytes < configured.records.maxEnvelopeBytes
        || configured.records.maxEnvelopeBytes > configured.maxRawResidentBytes / 5u) {
        s.fail("EDITOR-RECOVERY-STORE-DESCRIPTOR");
        return;
    }
    const char* issue = nullptr;
    try {
        (void)configured.storage.pathFor(configured.slots[1]);
    } catch (...) {
        s.fail("EDITOR-RECOVERY-STORE-DESCRIPTOR");
        return;
    }
    if (!s.current(configured.storage.epoch, issue))
        s.fail(issue);
    else if (configured.mode != SourceRecoveryStoreMode::Inspect
        && !files.saveCapability(configured.storage.mount).observedBaselineReplace)
        s.fail("EDITOR-RECOVERY-STORE-WRITE-CAPABILITY");
}

EditorSourceRecoveryStore::~EditorSourceRecoveryStore()
{
    cancel();
}

SourceRecoveryStoreState EditorSourceRecoveryStore::state() const noexcept
{
    return state_->phase;
}

std::string_view EditorSourceRecoveryStore::diagnostic() const noexcept
{
    return state_->diagnostic ? std::string_view(state_->diagnostic)
                              : std::string_view(state_->detail);
}

std::uint64_t EditorSourceRecoveryStore::nextSequence() const noexcept
{
    return state_->nextSequence;
}

core::SharedPtr<const EditorSourceRecoveryWrite> EditorSourceRecoveryStore::receipt() const noexcept
{
    return state_->write;
}

SourceRecoveryStoreSubmit EditorSourceRecoveryStore::submit(
    const EditorSourceRecoveryCapture& capture, std::uint64_t projectEpoch, TimePoint now)
{
    auto& s = *state_;
    if (s.phase == SourceRecoveryStoreState::Opening
        || s.phase == SourceRecoveryStoreState::Pending)
        return SourceRecoveryStoreSubmit::Busy;
    if (s.phase != SourceRecoveryStoreState::Ready
        || s.desc.mode == SourceRecoveryStoreMode::Inspect) {
        s.diagnostic = "EDITOR-RECOVERY-STORE-NOT-WRITABLE-OR-RECONCILE";
        return SourceRecoveryStoreSubmit::Rejected;
    }
    const char* issue = nullptr;
    const auto* record = capture.snapshot(projectEpoch, issue);
    if (!record || !s.identity(*record) || record->sequence != s.nextSequence
        || s.nextSequence > INT64_MAX || record->workspaceEpoch < s.slots[s.latest()].epoch
        || !s.current(s.desc.storage.epoch, issue)) {
        s.diagnostic = issue ? issue : "EDITOR-RECOVERY-STORE-SUBMISSION-IDENTITY";
        return SourceRecoveryStoreSubmit::Rejected;
    }
    const auto interval = std::chrono::seconds(s.desc.records.snapshotIntervalSeconds);
    if (s.lastSubmission && now < *s.lastSubmission) {
        s.diagnostic = "EDITOR-RECOVERY-STORE-CLOCK-REGRESSED";
        return SourceRecoveryStoreSubmit::Rejected;
    }
    if (s.lastSubmission
        && (*s.lastSubmission > TimePoint::max() - interval || now < *s.lastSubmission + interval))
        return SourceRecoveryStoreSubmit::NotDue;
    try {
        core::pmr::String yaml;
        if (!EditorRecoveryCodec::encode(*record, yaml, s.detail, s.desc.records))
            return SourceRecoveryStoreSubmit::Rejected;
        if (yaml.capacity() > s.desc.maxRawResidentBytes - s.resident()
            || yaml.size() > (s.desc.maxRawResidentBytes - s.resident()) / 2u) {
            s.diagnostic = "EDITOR-RECOVERY-STORE-RESIDENT-BUDGET";
            return SourceRecoveryStoreSubmit::Rejected;
        }
        core::pmr::Vector<std::uint8_t> candidate(yaml.begin(), yaml.end());
        core::pmr::String{}.swap(yaml);
        FileSaveRequest request;
        s.target = static_cast<std::size_t>((record->sequence - 1u) % s.slots.size());
        const auto& baseline = s.slots[s.target];
        request.pathScope = FilePathScope::MountedResource;
        request.mount = s.desc.storage.mount;
        request.path = s.desc.storage.pathFor(s.desc.slots[s.target]);
        request.maxWriteBytes = s.desc.records.maxEnvelopeBytes;
        request.precondition = baseline.present ? FileSavePrecondition::ContentSha256
                                                : FileSavePrecondition::MustBeMissing;
        request.expectedVersion = baseline.version;
        request.maxBaselineBytes = baseline.present ? s.desc.records.maxEnvelopeBytes : 0;
        request.bytes = candidate;
        const auto available = s.desc.maxRawResidentBytes - s.resident();
        if (candidate.capacity() > available
            || request.bytes.capacity() > available - candidate.capacity()
            || s.desc.records.maxEnvelopeBytes
                > available - candidate.capacity() - request.bytes.capacity()) {
            s.diagnostic = "EDITOR-RECOVERY-STORE-RESIDENT-BUDGET";
            return SourceRecoveryStoreSubmit::Rejected;
        }
        const auto payloadCapacity = request.bytes.capacity();
        auto write = core::makeShared<EditorSourceRecoveryWrite>();
        write->sequence_ = record->sequence;
        write->workspaceEpoch_ = record->workspaceEpoch;
        write->operation_ = core::makeUnique<ProjectFileSaveTask>(*s.files, std::move(request));
        auto verification =
            core::makeUnique<ProjectSourceVerification>(capture.context(), s.desc.inputs);
        core::Sha256 hash;
        hash.update({reinterpret_cast<const char*>(candidate.data()), candidate.size()});
        s.pendingVersion = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
        s.pending.swap(candidate);
        s.payloadCapacity = payloadCapacity;
        s.verification = std::move(verification);
        s.write = std::move(write);
        s.pendingTime = now;
        s.nextRead = 0;
        s.diagnostic = nullptr;
        s.detail.clear();
        s.phase = SourceRecoveryStoreState::Pending;
        return SourceRecoveryStoreSubmit::Accepted;
    } catch (...) {
        s.diagnostic = "EDITOR-RECOVERY-STORE-PREPARATION";
        return SourceRecoveryStoreSubmit::Rejected;
    }
}

void EditorSourceRecoveryStore::advance(std::uint64_t storageEpoch) noexcept
{
    auto& s = *state_;
    if (s.phase != SourceRecoveryStoreState::Opening
        && s.phase != SourceRecoveryStoreState::Pending)
        return;
    try {
        const char* issue = nullptr;
        if (!s.current(storageEpoch, issue)) {
            // An accepted save retains its real terminal outcome; do not turn
            // a stale delivery into an invented NotCommitted result.
            if (s.write && s.write->submitted()) {
                s.write->operation_->cancel();
                if (!s.write->terminal())
                    return;
                s.phase = SourceRecoveryStoreState::ReconcileRequired;
                s.diagnostic = issue;
                core::pmr::Vector<std::uint8_t>{}.swap(s.pending);
                s.payloadCapacity = 0;
                return;
            }
            s.fail(issue);
            return;
        }
        if (s.verification) {
            s.verification->advance();
            if (s.verification->state() == ProjectSourceVerificationState::Pending)
                return;
            if (s.verification->state() != ProjectSourceVerificationState::Verified) {
                s.fail(s.verification->diagnostic());
                return;
            }
            s.verification.reset();
        }
        if (s.read) {
            s.read->advance(storageEpoch);
            if (s.read->state() == ProjectFileReadState::Pending)
                return;
            if (s.read->state() != ProjectFileReadState::Complete || !s.acceptRead()) {
                s.fail(s.read ? s.read->diagnostic() : s.diagnostic);
                return;
            }
            ++s.nextRead;
        }
        if (s.nextRead < s.slots.size()) {
            const auto expected = s.phase == SourceRecoveryStoreState::Pending
                ? s.slots[s.nextRead].version
                : FileVersionToken{};
            s.read = core::makeUnique<ProjectFileReadTask>(*s.files,
                s.desc.storage,
                s.desc.slots[s.nextRead],
                s.desc.records.maxEnvelopeBytes,
                expected,
                expected.kind == FileVersionKind::None ? 0 : s.desc.records.maxEnvelopeBytes);
            s.read->advance(storageEpoch);
            return;
        }
        if (s.phase == SourceRecoveryStoreState::Opening) {
            const auto& latest = s.slots[s.latest()];
            const auto& previous = s.slots[1u - s.latest()];
            if (latest.present && previous.present && latest.epoch < previous.epoch) {
                s.fail("EDITOR-RECOVERY-STORE-SLOT-EPOCH-ORDER");
                return;
            }
            if (s.desc.mode == SourceRecoveryStoreMode::Resume && !latest.present) {
                s.fail("EDITOR-RECOVERY-STORE-NO-SNAPSHOT");
                return;
            }
            s.nextSequence = latest.present ? latest.sequence + 1u : 1u;
            s.phase = SourceRecoveryStoreState::Ready;
            return;
        }
        auto& write = *s.write;
        write.operation_->advance();
        if (write.submitted() && !write.consumedSequence_) {
            ++s.nextSequence;
            write.consumedSequence_ = true;
            s.lastSubmission = s.pendingTime;
        }
        if (!write.terminal())
            return;
        s.payloadCapacity = 0;
        const auto& result = write.result();
        if (result.commitState == FileCommitState::OutcomeUnknown
            || (result.commitState == FileCommitState::Committed
                && (result.status != FileIoStatus::Success
                    || result.deliveryStatus != FileIoStatus::Success
                    || result.committedVersion != s.pendingVersion))) {
            s.phase = SourceRecoveryStoreState::ReconcileRequired;
            s.diagnostic = "EDITOR-RECOVERY-STORE-RECONCILE-REQUIRED";
        } else if (result.status != FileIoStatus::Success
            || result.commitState != FileCommitState::Committed) {
            s.diagnostic =
                result.diagnosticId ? result.diagnosticId : "EDITOR-RECOVERY-STORE-NOT-COMMITTED";
            s.phase = SourceRecoveryStoreState::Ready;
        } else {
            auto& slot = s.slots[s.target];
            slot.bytes.swap(s.pending);
            slot.version = result.committedVersion;
            slot.sequence = write.sequence_;
            slot.epoch = write.workspaceEpoch_;
            slot.present = true;
            write.published_ = true;
            s.phase = SourceRecoveryStoreState::Ready;
        }
        core::pmr::Vector<std::uint8_t>{}.swap(s.pending);
    } catch (...) {
        if (s.write && s.write->submitted()) {
            s.write->operation_->cancel();
            s.phase = SourceRecoveryStoreState::ReconcileRequired;
            s.diagnostic = "EDITOR-RECOVERY-STORE-PUBLICATION-FAILED";
            core::pmr::Vector<std::uint8_t>{}.swap(s.pending);
            s.payloadCapacity = 0;
        } else
            s.fail("EDITOR-RECOVERY-STORE-PREPARATION");
    }
}

void EditorSourceRecoveryStore::cancel() noexcept
{
    auto& s = *state_;
    if (s.phase == SourceRecoveryStoreState::Opening
        || s.phase == SourceRecoveryStoreState::Pending) {
        s.read.reset();
        s.verification.reset();
        if (s.write) {
            s.write->operation_->cancel();
            if (!s.write->submitted() && !s.write->terminal())
                s.write->operation_->reject(
                    FileIoStatus::Cancelled, "EDITOR-RECOVERY-STORE-CANCELLED");
        }
        core::pmr::Vector<std::uint8_t>{}.swap(s.pending);
        s.payloadCapacity = 0;
        s.phase = SourceRecoveryStoreState::Cancelled;
        s.diagnostic = "EDITOR-RECOVERY-STORE-CANCELLED";
    }
}

bool EditorSourceRecoveryStore::decodeLatest(std::uint64_t storageEpoch,
    EditorRecoverySnapshot& destination,
    core::pmr::String& diagnostic) const
{
    const auto& s = *state_;
    const char* issue = nullptr;
    if (s.phase != SourceRecoveryStoreState::Ready || !s.current(storageEpoch, issue)) {
        diagnostic = issue ? issue : "EDITOR-RECOVERY-STORE-NOT-READY";
        return false;
    }
    const auto& latest = s.slots[s.latest()];
    if (!latest.present) {
        diagnostic = "EDITOR-RECOVERY-STORE-EMPTY";
        return false;
    }
    return EditorRecoveryCodec::decode(
        {reinterpret_cast<const char*>(latest.bytes.data()), latest.bytes.size()},
        destination,
        diagnostic,
        s.desc.records);
}
} // namespace arp::editor
