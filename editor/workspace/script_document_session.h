#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/monotonic_id_sequence.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>

namespace arp::editor {
class ScriptDocumentSession final {
public:
    static constexpr std::size_t maxScriptBytes = 2u * 1024u * 1024u;

    // Identity domains never recycle, including after every document is closed.
    explicit ScriptDocumentSession(std::uint64_t firstDocumentId = 1u)
        : documentIds_(firstDocumentId)
    {}

    ScriptDocumentSession(const ScriptDocumentSession&) = delete;
    ScriptDocumentSession& operator=(const ScriptDocumentSession&) = delete;
    ScriptDocumentSession(ScriptDocumentSession&&) = delete;
    ScriptDocumentSession& operator=(ScriptDocumentSession&&) = delete;

    // Shared file-to-editing-buffer conversion; exact disk bytes remain separate.
    static core::pmr::String normalizeNewlines(std::string_view contents);
    static bool isScriptPath(const std::filesystem::path& path);

    struct Edit {
        core::pmr::String removed{&core::defaultCpuMemoryResource()};
        core::pmr::String inserted{&core::defaultCpuMemoryResource()};
        std::size_t offset = 0;
        std::size_t cursorBefore = 0, anchorBefore = 0;
        std::size_t cursorAfter = 0, anchorAfter = 0;
    };

    struct Document {
        std::uint64_t id = 0;
        std::filesystem::path path;
        core::pmr::String text{&core::defaultCpuMemoryResource()};
        core::pmr::String savedText{&core::defaultCpuMemoryResource()};
        // Exact disk baseline, distinct from the normalized editing buffer.
        core::pmr::String savedFileBytes{&core::defaultCpuMemoryResource()};
        core::pmr::String newline{"\n", &core::defaultCpuMemoryResource()};
        std::size_t cursor = 0;
        std::size_t anchor = 0;
        std::size_t scrollLine = 0;
        core::pmr::Vector<Edit> undo{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Edit> redo{&core::defaultCpuMemoryResource()};

        bool dirty() const noexcept { return text != savedText; }
    };

    enum class FileResultCode {
        Opened,
        AlreadyOpen,
        Saved,
        NoActiveDocument,
        InvalidPath,
        TooLarge,
        ReadFailed,
        WriteFailed,
        ExternalConflict,
        UnsavedChanges,
        IdentityExhausted
    };

    struct FileResult {
        FileResultCode code = FileResultCode::InvalidPath;
        std::size_t documentIndex = std::numeric_limits<std::size_t>::max();

        explicit operator bool() const noexcept
        {
            return code == FileResultCode::Opened || code == FileResultCode::AlreadyOpen
                || code == FileResultCode::Saved;
        }
    };

    enum class EditResult { Applied, Unchanged, Rejected };

    static constexpr std::size_t noActiveDocument = std::numeric_limits<std::size_t>::max();

    enum class RecoveryCommitResult {
        Ready,
        Committed,
        Incomplete,
        Stale,
        Consumed,
        IdentityExhausted
    };

    class PreparedRecovery final {
    public:
        PreparedRecovery() = default;
        PreparedRecovery(const PreparedRecovery&) = delete;
        PreparedRecovery& operator=(const PreparedRecovery&) = delete;
        PreparedRecovery(PreparedRecovery&&) = delete;
        PreparedRecovery& operator=(PreparedRecovery&&) = delete;

        // Provisional documents before commit, retired documents afterwards.
        const core::pmr::Vector<Document>& documents() const noexcept { return documents_; }

        bool committed() const noexcept { return committed_; }

    private:
        friend class ScriptDocumentSession;
        core::pmr::Vector<Document> documents_{&core::defaultCpuMemoryResource()};
        core::LifetimeEpoch::Ticket owner_;
        std::size_t activeIndex_ = noActiveDocument;
        bool prepared_ = false;
        bool committed_ = false;
    };

    const core::pmr::Vector<Document>& documents() const noexcept { return documents_; }

    std::size_t size() const noexcept { return documents_.size(); }

    std::size_t activeIndex() const noexcept { return activeIndex_; }

    // Direct mutable Document access is additionally checked by admission's
    // content digest; successful session operations invalidate older tickets.
    core::LifetimeEpoch::Ticket stateTicket() const noexcept { return stateEpoch_.ticket(); }

    bool setActiveIndex(std::size_t index) noexcept;
    // Returns noActiveDocument when the identity domain is exhausted.
    std::size_t append(Document document);
    // Owner-thread transaction after enclosing workspace authorization/freshness
    // gates. Prepared documents must have id=0 and no history. Success swaps
    // retired documents into prepared; failure preserves both sides and IDs.
    // This does not admit paths, discard dirty edits or authorize GPU retirement.
    bool replaceRecoveredDocuments(core::pmr::Vector<Document>& prepared,
        std::size_t activeIndex,
        core::pmr::String& diagnostic);
    // Fallible owner-thread preflight. A private copy isolates existing aliases
    // into the input vector; preparation preserves that input and consumes no IDs.
    // Abandoning the token leaves both the live session and input intact.
    core::UniquePtr<PreparedRecovery> prepareRecoveredDocuments(
        const core::pmr::Vector<Document>& prepared,
        std::size_t activeIndex,
        core::pmr::String& diagnostic) const;
    // These do not allocate, do IO, normalize text or invoke callbacks. They
    // observe Session epochs, not arbitrary direct Document* writes; enclosing
    // workspace content/authorization gates remain the caller's responsibility.
    RecoveryCommitResult recoveryCommitReadiness(const PreparedRecovery& prepared) const noexcept;
    RecoveryCommitResult commitRecoveredDocuments(PreparedRecovery& prepared) noexcept;
    bool erase(std::size_t index);
    FileResult openFile(
        const std::filesystem::path& projectRoot, const std::filesystem::path& path);
    // Prepare saved baselines before disk replacement; successful publication
    // does not allocate. The existing disk preflight is not a cross-process CAS.
    FileResult saveActiveFile(const std::filesystem::path& projectRoot);
    FileResult reloadActiveFile(const std::filesystem::path& projectRoot, bool discardChanges);
    const std::filesystem::path& activePath() const noexcept;
    std::size_t activeCursor() const noexcept;
    std::size_t activeAnchor() const noexcept;
    std::size_t activeScrollLine() const noexcept;
    void setActiveSelection(std::size_t cursor, std::size_t anchor) noexcept;
    void setActiveScrollLine(std::size_t line) noexcept;
    // Allocation failure preserves text/selection/history/save point and epoch.
    // Candidate text owns input aliases before any live buffer is changed.
    EditResult replaceActiveRange(
        std::size_t start, std::size_t length, std::string_view value, std::size_t maxBytes);
    bool undoActive();
    bool redoActive();
    Document* active() noexcept;
    const Document* active() const noexcept;
    Document* find(std::uint64_t documentId) noexcept;
    const Document* find(const std::filesystem::path& path) const noexcept;
    std::uint64_t activeId() const noexcept;

private:
    const char* validateRecoveredDocuments(
        const core::pmr::Vector<Document>& prepared, std::size_t activeIndex) const;
    core::pmr::Vector<Document> documents_{&core::defaultCpuMemoryResource()};
    std::size_t activeIndex_ = noActiveDocument;
    core::MonotonicIdSequence documentIds_;
    core::LifetimeEpoch stateEpoch_;
};
} // namespace arp::editor
