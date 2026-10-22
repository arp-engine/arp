#include "editor/workspace/script_document_session.h"

#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace arp::editor {
namespace {
bool isScriptPath(const std::filesystem::path& path)
{
    auto extension = core::FilePath::utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](char value) {
        return core::toLowerAscii(value);
    });
    return extension == ".arp";
}

core::pmr::String normalizeNewlines(std::string_view contents)
{
    core::pmr::String normalized(&core::defaultCpuMemoryResource());
    normalized.reserve(contents.size());
    for (std::size_t i = 0; i < contents.size(); ++i) {
        if (contents[i] == '\r') {
            if (i + 1 < contents.size() && contents[i + 1] == '\n')
                ++i;
            normalized.push_back('\n');
        } else
            normalized.push_back(contents[i]);
    }
    return normalized;
}
} // namespace

bool ScriptDocumentSession::setActiveIndex(std::size_t index) noexcept
{
    if (index >= documents_.size())
        return false;
    activeIndex_ = index;
    return true;
}

std::size_t ScriptDocumentSession::append(Document document)
{
    document.id = nextDocumentId_++;
    documents_.push_back(std::move(document));
    return documents_.size() - 1;
}

bool ScriptDocumentSession::erase(std::size_t index)
{
    if (index >= documents_.size())
        return false;
    documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (activeIndex_ == index)
        activeIndex_ = noActiveDocument;
    else if (activeIndex_ != noActiveDocument && index < activeIndex_)
        --activeIndex_;
    return true;
}

ScriptDocumentSession::FileResult ScriptDocumentSession::openFile(
    const std::filesystem::path& projectRoot, const std::filesystem::path& path)
{
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(projectRoot, error);
    if (error || root.empty() || !std::filesystem::is_directory(root, error) || error)
        return {FileResultCode::InvalidPath};
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error || !core::FilePath::contains(root, resolved) || !isScriptPath(resolved)
        || !std::filesystem::is_regular_file(resolved, error) || error)
        return {FileResultCode::InvalidPath};

    const auto existing =
        std::find_if(documents_.begin(), documents_.end(), [&resolved](const Document& document) {
            return document.path == resolved;
        });
    if (existing != documents_.end())
        return {
            FileResultCode::AlreadyOpen, static_cast<std::size_t>(existing - documents_.begin())};

    const auto size = std::filesystem::file_size(resolved, error);
    if (error)
        return {FileResultCode::ReadFailed};
    if (size > maxScriptBytes)
        return {FileResultCode::TooLarge};
    std::ifstream input(resolved, std::ios::binary);
    if (!input)
        return {FileResultCode::ReadFailed};
    core::pmr::String contents(
        static_cast<std::size_t>(size), '\0', &core::defaultCpuMemoryResource());
    if (size > 0)
        input.read(contents.data(), static_cast<std::streamsize>(size));
    if (!input || static_cast<std::size_t>(input.gcount()) != contents.size())
        return {FileResultCode::ReadFailed};

    Document document;
    document.path = resolved;
    document.newline = contents.find("\r\n") != core::pmr::String::npos ? "\r\n" : "\n";
    document.text = normalizeNewlines(contents);
    document.savedText = document.text;
    const auto index = append(std::move(document));
    return {FileResultCode::Opened, index};
}

ScriptDocumentSession::FileResult ScriptDocumentSession::saveActiveFile(
    const std::filesystem::path& projectRoot)
{
    auto* document = active();
    if (!document)
        return {FileResultCode::NoActiveDocument};
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(projectRoot, error);
    const auto resolved = std::filesystem::weakly_canonical(document->path, error);
    if (error || root.empty() || !std::filesystem::is_directory(root, error) || error
        || resolved != document->path || !core::FilePath::contains(root, resolved)
        || !isScriptPath(resolved))
        return {FileResultCode::InvalidPath};

    std::ofstream output(document->path, std::ios::binary | std::ios::trunc);
    if (!output)
        return {FileResultCode::WriteFailed};
    if (document->newline == "\n")
        output.write(document->text.data(), static_cast<std::streamsize>(document->text.size()));
    else {
        std::size_t start = 0;
        while (start < document->text.size()) {
            const auto newline = document->text.find('\n', start);
            if (newline == core::pmr::String::npos) {
                output.write(document->text.data() + start,
                    static_cast<std::streamsize>(document->text.size() - start));
                break;
            }
            output.write(
                document->text.data() + start, static_cast<std::streamsize>(newline - start));
            output.write("\r\n", 2);
            start = newline + 1;
        }
    }
    output.flush();
    if (!output)
        return {FileResultCode::WriteFailed};
    document->savedText = document->text;
    return {FileResultCode::Saved, activeIndex_};
}

const std::filesystem::path& ScriptDocumentSession::activePath() const noexcept
{
    static const std::filesystem::path empty;
    const auto* document = active();
    return document ? document->path : empty;
}

std::size_t ScriptDocumentSession::activeCursor() const noexcept
{
    const auto* document = active();
    return document ? document->cursor : 0;
}

std::size_t ScriptDocumentSession::activeAnchor() const noexcept
{
    const auto* document = active();
    return document ? document->anchor : 0;
}

std::size_t ScriptDocumentSession::activeScrollLine() const noexcept
{
    const auto* document = active();
    return document ? document->scrollLine : 0;
}

void ScriptDocumentSession::setActiveSelection(std::size_t cursor, std::size_t anchor) noexcept
{
    auto* document = active();
    if (!document)
        return;
    document->cursor = std::min(cursor, document->text.size());
    document->anchor = std::min(anchor, document->text.size());
}

void ScriptDocumentSession::setActiveScrollLine(std::size_t line) noexcept
{
    if (auto* document = active())
        document->scrollLine = line;
}

ScriptDocumentSession::EditResult ScriptDocumentSession::replaceActiveRange(
    std::size_t start, std::size_t length, std::string_view value, std::size_t maxBytes)
{
    auto* document = active();
    if (!document || value.size() > maxBytes)
        return EditResult::Rejected;
    start = std::min(start, document->text.size());
    length = std::min(length, document->text.size() - start);
    if (document->text.size() - length > maxBytes - value.size())
        return EditResult::Rejected;
    if (length == 0 && value.empty())
        return EditResult::Unchanged;

    Edit edit;
    edit.offset = start;
    edit.removed.assign(document->text.data() + start, length);
    if (!value.empty())
        edit.inserted.assign(value.data(), value.size());
    edit.cursorBefore = document->cursor;
    edit.anchorBefore = document->anchor;
    document->text.replace(start, length, value.empty() ? "" : value.data(), value.size());
    document->cursor = start + value.size();
    document->anchor = document->cursor;
    edit.cursorAfter = document->cursor;
    edit.anchorAfter = document->anchor;
    document->undo.push_back(std::move(edit));
    document->redo.clear();
    return EditResult::Applied;
}

bool ScriptDocumentSession::undoActive()
{
    auto* document = active();
    if (!document || document->undo.empty())
        return false;
    auto edit = std::move(document->undo.back());
    document->undo.pop_back();
    document->text.replace(
        edit.offset, edit.inserted.size(), edit.removed.data(), edit.removed.size());
    document->cursor = edit.cursorBefore;
    document->anchor = edit.anchorBefore;
    document->redo.push_back(std::move(edit));
    return true;
}

bool ScriptDocumentSession::redoActive()
{
    auto* document = active();
    if (!document || document->redo.empty())
        return false;
    auto edit = std::move(document->redo.back());
    document->redo.pop_back();
    document->text.replace(
        edit.offset, edit.removed.size(), edit.inserted.data(), edit.inserted.size());
    document->cursor = edit.cursorAfter;
    document->anchor = edit.anchorAfter;
    document->undo.push_back(std::move(edit));
    return true;
}

ScriptDocumentSession::Document* ScriptDocumentSession::active() noexcept
{
    return activeIndex_ < documents_.size() ? &documents_[activeIndex_] : nullptr;
}

const ScriptDocumentSession::Document* ScriptDocumentSession::active() const noexcept
{
    return activeIndex_ < documents_.size() ? &documents_[activeIndex_] : nullptr;
}

ScriptDocumentSession::Document* ScriptDocumentSession::find(std::uint64_t documentId) noexcept
{
    const auto found =
        std::find_if(documents_.begin(), documents_.end(), [documentId](const Document& document) {
            return document.id == documentId;
        });
    return found == documents_.end() ? nullptr : &*found;
}

const ScriptDocumentSession::Document* ScriptDocumentSession::find(
    const std::filesystem::path& path) const noexcept
{
    const auto found =
        std::find_if(documents_.begin(), documents_.end(), [&path](const Document& document) {
            return document.path == path;
        });
    return found == documents_.end() ? nullptr : &*found;
}

std::uint64_t ScriptDocumentSession::activeId() const noexcept
{
    const auto* document = active();
    return document ? document->id : 0;
}
} // namespace arp::editor
