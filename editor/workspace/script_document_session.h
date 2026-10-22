#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>

namespace arp::editor {
class ScriptDocumentSession final {
public:
    static constexpr std::size_t maxScriptBytes = 2u * 1024u * 1024u;

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
        WriteFailed
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

    const core::pmr::Vector<Document>& documents() const noexcept { return documents_; }

    std::size_t size() const noexcept { return documents_.size(); }

    std::size_t activeIndex() const noexcept { return activeIndex_; }

    bool setActiveIndex(std::size_t index) noexcept;
    std::size_t append(Document document);
    bool erase(std::size_t index);
    FileResult openFile(
        const std::filesystem::path& projectRoot, const std::filesystem::path& path);
    FileResult saveActiveFile(const std::filesystem::path& projectRoot);
    const std::filesystem::path& activePath() const noexcept;
    std::size_t activeCursor() const noexcept;
    std::size_t activeAnchor() const noexcept;
    std::size_t activeScrollLine() const noexcept;
    void setActiveSelection(std::size_t cursor, std::size_t anchor) noexcept;
    void setActiveScrollLine(std::size_t line) noexcept;
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
    core::pmr::Vector<Document> documents_{&core::defaultCpuMemoryResource()};
    std::size_t activeIndex_ = noActiveDocument;
    std::uint64_t nextDocumentId_ = 1;
};
} // namespace arp::editor
