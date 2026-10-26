#pragma once

#include "core/io/atomic_file_service.h"
#include "editor/workspace/editor_recovery_codec.h"

#include <array>
#include <chrono>
#include <filesystem>

namespace arp::editor {

// Enumeration never infers process death from a directory, PID or timestamp.
struct EditorRecoveryRegionEntry {
    core::pmr::String sessionId;
    std::filesystem::path directory;
    std::size_t usedBytes = 0;
    std::size_t temporaryFiles = 0;
};

// Immutable region contract and exclusive directory admission. Each session
// reserves both retained slots and one atomic-write temporary independently.
class EditorRecoveryRegion final {
public:
    bool open(const std::filesystem::path& directory,
        EditorRecoveryBudget budget,
        core::pmr::String& diagnostic);
    bool inspect(core::pmr::Vector<EditorRecoveryRegionEntry>& destination,
        core::pmr::String& diagnostic) const;
    bool claim(EditorRecoveryRegionEntry& destination, core::pmr::String& diagnostic) const;

    const std::filesystem::path& directory() const noexcept { return directory_; }

    const EditorRecoveryBudget& budget() const noexcept { return budget_; }

    std::uint32_t publicationAttempts() const noexcept { return publicationAttempts_; }

    static std::string_view slotName(std::size_t slot) noexcept;

private:
    std::filesystem::path directory_;
    EditorRecoveryBudget budget_;
    core::pmr::String metadataBytes_;
    std::uint32_t publicationAttempts_ = 0;
};

enum class EditorRecoveryWriteStatus { Idle, Pending, Saved, Failed };
enum class EditorRecoverySubmitStatus { Accepted, Busy, NotDue, Unchanged, Rejected };

struct EditorRecoveryWriteResult {
    EditorRecoveryWriteStatus status = EditorRecoveryWriteStatus::Idle;
    std::uint64_t submittedSequence = 0;
    std::uint64_t savedSequence = 0;
    std::uint64_t savedWorkspaceEpoch = 0;
    core::pmr::String diagnostic;
};

// Owner-thread control; the existing file worker only sees immutable bytes.
// Destruction drains already submitted IO and conservatively retains files.
// It does not promise publication of a write whose preflight was not polled.
class EditorRecoverySession final {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    ~EditorRecoverySession();
    EditorRecoverySession() = default;
    EditorRecoverySession(const EditorRecoverySession&) = delete;
    EditorRecoverySession& operator=(const EditorRecoverySession&) = delete;

    bool open(const std::filesystem::path& region,
        core::pmr::String& diagnostic,
        EditorRecoveryBudget budget = {});
    EditorRecoverySubmitStatus submit(const EditorRecoverySnapshot& snapshot, TimePoint now);
    bool poll();
    // Explicit normal-close action. Busy, changed files or unknown contents
    // reject. Only the current, exact owned directory can be removed.
    bool discardOwned(core::pmr::String& diagnostic);

    bool pending() const noexcept { return result_.status == EditorRecoveryWriteStatus::Pending; }

    const core::pmr::String& sessionId() const noexcept { return owned_.sessionId; }

    const std::filesystem::path& directory() const noexcept { return owned_.directory; }

    std::uint64_t nextSequence() const noexcept { return nextSequence_; }

    const EditorRecoveryWriteResult& result() const noexcept { return result_; }

    std::uint32_t regionPublicationAttempts() const noexcept { return regionPublicationAttempts_; }

private:
    bool admitCurrent(std::size_t temporaryBytes, core::pmr::String& diagnostic) const;
    bool beginWrite();
    bool fail(std::string_view diagnostic);

    EditorRecoveryRegion region_;
    EditorRecoveryRegionEntry owned_;
    core::UniquePtr<core::io::AtomicFileService> files_;
    std::array<core::pmr::Vector<std::uint8_t>, 2> slotBytes_;
    core::pmr::Vector<std::uint8_t> pendingBytes_;
    core::PoolHandle request_;
    std::filesystem::path pendingPath_;
    std::size_t pendingSlot_ = 0;
    bool checking_ = false;
    // Once explicit deletion starts, only retrying cleanup is permitted.
    bool discarding_ = false;
    std::uint64_t nextSequence_ = 1;
    std::uint64_t pendingEpoch_ = 0;
    core::pmr::String pendingProjectRoot_;
    core::pmr::String savedProjectRoot_;
    std::optional<TimePoint> lastSubmission_;
    EditorRecoveryWriteResult result_;
    std::uint32_t regionPublicationAttempts_ = 0;
};

} // namespace arp::editor
