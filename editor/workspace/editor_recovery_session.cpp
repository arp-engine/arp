#include "editor/workspace/editor_recovery_session.h"

#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"

#include <algorithm>
#include <limits>

namespace arp::editor {

EditorRecoverySession::~EditorRecoverySession() = default;

bool EditorRecoverySession::open(
    const std::filesystem::path& region, core::pmr::String& diagnostic, EditorRecoveryBudget budget)
{
    if (files_ || !owned_.directory.empty()) {
        diagnostic = "EDITOR-RECOVERY-SESSION-ALREADY-OPEN";
        return false;
    }
    EditorRecoveryRegion candidate;
    const bool opened = candidate.open(region, budget, diagnostic);
    regionPublicationAttempts_ = candidate.publicationAttempts();
    if (!opened)
        return false;
    // Prepare the bounded worker before claiming a persistent directory.
    auto files = core::makeUnique<core::io::AtomicFileService>(1u, budget.maxEnvelopeBytes);
    EditorRecoveryRegionEntry entry;
    if (!candidate.claim(entry, diagnostic))
        return false;
    region_ = std::move(candidate);
    owned_ = std::move(entry);
    files_ = std::move(files);
    nextSequence_ = 1;
    lastSubmission_.reset();
    result_ = {};
    diagnostic.clear();
    return true;
}

bool EditorRecoverySession::admitCurrent(
    std::size_t temporaryBytes, core::pmr::String& diagnostic) const
{
    core::pmr::Vector<EditorRecoveryRegionEntry> entries;
    if (!files_ || owned_.directory.empty() || !region_.inspect(entries, diagnostic)) {
        if (diagnostic.empty())
            diagnostic = "EDITOR-RECOVERY-SESSION-NOT-OPEN";
        return false;
    }
    const auto own = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.sessionId == owned_.sessionId && entry.directory == owned_.directory;
    });
    if (own == entries.end() || own->temporaryFiles != 0u) {
        diagnostic = "EDITOR-RECOVERY-SESSION-OWNERSHIP-CHANGED";
        return false;
    }
    const auto& budget = region_.budget();
    const auto reserved = budget.maxEnvelopeBytes * (budget.slotsPerSession + 1u);
    if (temporaryBytes > budget.maxEnvelopeBytes || own->usedBytes > reserved - temporaryBytes) {
        diagnostic = "EDITOR-RECOVERY-SESSION-DISK-LIMIT";
        return false;
    }
    try {
        for (std::size_t i = 0; i < slotBytes_.size(); ++i) {
            const auto path = owned_.directory / std::filesystem::u8path(region_.slotName(i));
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (status.type() == std::filesystem::file_type::not_found
                && error == std::errc::no_such_file_or_directory)
                error.clear();
            const bool expected = !slotBytes_[i].empty();
            if (error || (expected && status.type() != std::filesystem::file_type::regular)
                || (!expected && status.type() != std::filesystem::file_type::not_found)
                || (expected && std::filesystem::file_size(path) != slotBytes_[i].size())) {
                diagnostic = "EDITOR-RECOVERY-SLOT-PATH-CHANGED";
                return false;
            }
        }
    } catch (...) {
        diagnostic = "EDITOR-RECOVERY-SLOT-STAT-FAILED";
        return false;
    }
    diagnostic.clear();
    return true;
}

bool EditorRecoverySession::fail(std::string_view diagnostic)
{
    result_.status = EditorRecoveryWriteStatus::Failed;
    result_.diagnostic.assign(diagnostic.data(), diagnostic.size());
    pendingBytes_.clear();
    pendingProjectRoot_.clear();
    pendingPath_.clear();
    request_ = {};
    checking_ = false;
    return false;
}

EditorRecoverySubmitStatus EditorRecoverySession::submit(
    const EditorRecoverySnapshot& snapshot, TimePoint now)
{
    if (pending())
        return EditorRecoverySubmitStatus::Busy;
    if (discarding_) {
        fail("EDITOR-RECOVERY-DISCARD-IN-PROGRESS");
        return EditorRecoverySubmitStatus::Rejected;
    }
    if (!files_ || snapshot.sessionId != owned_.sessionId || snapshot.sequence != nextSequence_
        || nextSequence_ == std::numeric_limits<std::uint64_t>::max()
        || (result_.savedSequence && snapshot.workspaceEpoch < result_.savedWorkspaceEpoch)) {
        fail("EDITOR-RECOVERY-SUBMISSION-IDENTITY");
        return EditorRecoverySubmitStatus::Rejected;
    }
    if (lastSubmission_ && now < *lastSubmission_) {
        fail("EDITOR-RECOVERY-CLOCK-REGRESSED");
        return EditorRecoverySubmitStatus::Rejected;
    }
    if (result_.savedSequence && snapshot.workspaceEpoch == result_.savedWorkspaceEpoch
        && snapshot.projectRoot == savedProjectRoot_)
        return EditorRecoverySubmitStatus::Unchanged;
    const auto interval = std::chrono::seconds(region_.budget().snapshotIntervalSeconds);
    if (lastSubmission_
        && (*lastSubmission_ > TimePoint::max() - interval || now < *lastSubmission_ + interval))
        return EditorRecoverySubmitStatus::NotDue;

    core::pmr::String yaml, diagnostic;
    if (!EditorRecoveryCodec::encode(snapshot, yaml, diagnostic, region_.budget())
        || !admitCurrent(yaml.size(), diagnostic)) {
        fail(diagnostic);
        return EditorRecoverySubmitStatus::Rejected;
    }
    pendingSlot_ = static_cast<std::size_t>((snapshot.sequence - 1u) % slotBytes_.size());
    pendingBytes_.assign(yaml.begin(), yaml.end());
    pendingPath_ = owned_.directory / std::filesystem::u8path(region_.slotName(pendingSlot_));
    pendingEpoch_ = snapshot.workspaceEpoch;
    pendingProjectRoot_ = snapshot.projectRoot;
    result_.status = EditorRecoveryWriteStatus::Pending;
    result_.submittedSequence = snapshot.sequence;
    result_.diagnostic.clear();
    ++nextSequence_;
    lastSubmission_ = now;
    if (slotBytes_[pendingSlot_].empty()) {
        if (!beginWrite())
            return EditorRecoverySubmitStatus::Rejected;
    } else {
        checking_ = true;
        request_ = files_->read(core::FilePath::utf8(pendingPath_));
        if (!request_) {
            fail(files_->diagnostic());
            return EditorRecoverySubmitStatus::Rejected;
        }
    }
    return EditorRecoverySubmitStatus::Accepted;
}

bool EditorRecoverySession::beginWrite()
{
    core::pmr::String diagnostic;
    if (!admitCurrent(pendingBytes_.size(), diagnostic))
        return fail(diagnostic);
    checking_ = false;
    request_ = files_->replace(core::FilePath::utf8(pendingPath_), std::move(pendingBytes_));
    if (!request_)
        return fail(files_->diagnostic());
    return true;
}

bool EditorRecoverySession::poll()
{
    if (!pending() || !files_)
        return false;
    core::io::AtomicFileResult completed;
    if (!files_->take(request_, completed))
        return false;
    request_ = {};
    if (!completed.success) {
        fail(completed.diagnostic);
        return true;
    }
    if (checking_) {
        if (completed.bytes != slotBytes_[pendingSlot_])
            fail("EDITOR-RECOVERY-SLOT-CONTENT-CHANGED");
        else
            beginWrite();
        return true;
    }
    if (!completed.committed) {
        fail("EDITOR-RECOVERY-SLOT-NOT-COMMITTED");
        return true;
    }
    slotBytes_[pendingSlot_] = std::move(completed.bytes);
    result_.savedSequence = result_.submittedSequence;
    result_.savedWorkspaceEpoch = pendingEpoch_;
    savedProjectRoot_ = std::move(pendingProjectRoot_);
    pendingPath_.clear();
    result_.status = EditorRecoveryWriteStatus::Saved;
    result_.diagnostic.clear();
    return true;
}

bool EditorRecoverySession::discardOwned(core::pmr::String& diagnostic)
{
    if (pending()) {
        diagnostic = "EDITOR-RECOVERY-SESSION-BUSY";
        return false;
    }
    if (!admitCurrent(0, diagnostic))
        return false;
    // Validate every baseline before removing any file. This is an explicit
    // owner-thread close operation, not a cross-process compare-and-swap.
    try {
        for (std::size_t i = 0; i < slotBytes_.size(); ++i) {
            if (slotBytes_[i].empty())
                continue;
            const auto path = owned_.directory / std::filesystem::u8path(region_.slotName(i));
            const auto current = core::readBinaryFile(path, region_.budget().maxEnvelopeBytes);
            if (!current.success || current.bytes != slotBytes_[i]) {
                diagnostic = "EDITOR-RECOVERY-SLOT-CONTENT-CHANGED";
                return false;
            }
        }
        discarding_ = true;
        for (std::size_t i = 0; i < slotBytes_.size(); ++i) {
            if (slotBytes_[i].empty())
                continue;
            std::error_code error;
            if (!std::filesystem::remove(
                    owned_.directory / std::filesystem::u8path(region_.slotName(i)), error)
                || error) {
                diagnostic = "EDITOR-RECOVERY-SLOT-REMOVE-FAILED";
                return false;
            }
            slotBytes_[i].clear();
        }
        std::error_code error;
        if (!std::filesystem::remove(owned_.directory, error) || error) {
            diagnostic = "EDITOR-RECOVERY-SESSION-REMOVE-FAILED";
            return false;
        }
        files_.reset();
        owned_ = {};
        region_ = {};
        pendingBytes_.clear();
        savedProjectRoot_.clear();
        discarding_ = false;
        result_ = {};
        diagnostic.clear();
        return true;
    } catch (...) {
        diagnostic = "EDITOR-RECOVERY-SESSION-DISCARD-FAILED";
        return false;
    }
}

} // namespace arp::editor
