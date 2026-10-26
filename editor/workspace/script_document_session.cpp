#include "editor/workspace/script_document_session.h"

#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"

#include <algorithm>
#include <system_error>
#include <type_traits>

namespace arp::editor {
bool ScriptDocumentSession::isScriptPath(const std::filesystem::path& path)
{
    auto extension = core::FilePath::utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](char value) {
        return core::toLowerAscii(value);
    });
    return extension == ".arp";
}

core::pmr::String ScriptDocumentSession::normalizeNewlines(std::string_view contents)
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

bool ScriptDocumentSession::setActiveIndex(std::size_t index) noexcept
{
    if (index >= documents_.size())
        return false;
    if (activeIndex_ != index)
        stateEpoch_.advance();
    activeIndex_ = index;
    return true;
}

std::size_t ScriptDocumentSession::append(Document document)
{
    if (!documentIds_.hasRemaining())
        return noActiveDocument;
    // Complete allocation before consuming an identity. The nothrow move keeps
    // vector insertion atomic on allocation failure. All access is owner-thread.
    static_assert(std::is_nothrow_move_constructible_v<Document>);
    document.id = 0;
    documents_.push_back(std::move(document));
    documents_.back().id = documentIds_.tryAcquire();
    stateEpoch_.advance();
    return documents_.size() - 1;
}

const char* ScriptDocumentSession::validateRecoveredDocuments(
    const core::pmr::Vector<Document>& prepared, std::size_t activeIndex) const
{
    if (&prepared == &documents_ || prepared.get_allocator() != documents_.get_allocator())
        return "EDITOR-SCRIPT-RECOVERY-ALLOCATOR-OR-ALIAS";
    if (activeIndex != noActiveDocument && activeIndex >= prepared.size())
        return "EDITOR-SCRIPT-RECOVERY-ACTIVE-INDEX";
    for (std::size_t index = 0; index < prepared.size(); ++index) {
        const auto& document = prepared[index];
        const auto* allocator = &core::defaultCpuMemoryResource();
        if (document.text.get_allocator().resource() != allocator
            || document.savedText.get_allocator().resource() != allocator
            || document.savedFileBytes.get_allocator().resource() != allocator
            || document.newline.get_allocator().resource() != allocator
            || document.undo.get_allocator().resource() != allocator
            || document.redo.get_allocator().resource() != allocator)
            return "EDITOR-SCRIPT-RECOVERY-ALLOCATOR-OR-ALIAS";
        if (document.id != 0 || !document.undo.empty() || !document.redo.empty()
            || !document.path.is_absolute() || document.path != document.path.lexically_normal()
            || !isScriptPath(document.path) || document.text.size() > maxScriptBytes
            || document.savedText.size() > maxScriptBytes
            || document.savedFileBytes.size() > maxScriptBytes
            || document.cursor > document.text.size() || document.anchor > document.text.size()
            || (document.newline != "\n" && document.newline != "\r\n")
            || document.text.find('\r') != core::pmr::String::npos
            || document.savedText != normalizeNewlines(document.savedFileBytes))
            return "EDITOR-SCRIPT-RECOVERY-DOCUMENT";
        for (std::size_t previous = 0; previous < index; ++previous)
            if (prepared[previous].path == document.path)
                return "EDITOR-SCRIPT-RECOVERY-DUPLICATE-PATH";
    }
    if (!documentIds_.hasRemaining(static_cast<std::uint64_t>(prepared.size())))
        return "EDITOR-SCRIPT-IDENTITY-EXHAUSTED";
    return nullptr;
}

core::UniquePtr<ScriptDocumentSession::PreparedRecovery>
ScriptDocumentSession::prepareRecoveredDocuments(const core::pmr::Vector<Document>& prepared,
    std::size_t activeIndex,
    core::pmr::String& diagnostic) const
{
    try {
        if (const auto* issue = validateRecoveredDocuments(prepared, activeIndex)) {
            diagnostic = issue;
            return {};
        }
        const auto owner = stateEpoch_.ticket();
        if (!owner.isCurrent()) {
            diagnostic = "EDITOR-SCRIPT-RECOVERY-OWNER-INACTIVE";
            return {};
        }
        auto result = core::makeUnique<PreparedRecovery>();
        result->documents_.reserve(prepared.size());
        for (const auto& source : prepared) {
            // Assign into explicitly core-allocated fields. Besides preserving
            // allocator ownership, this detaches old mutable input aliases.
            Document copy;
            copy.path = source.path;
            copy.text = source.text;
            copy.savedText = source.savedText;
            copy.savedFileBytes = source.savedFileBytes;
            copy.newline = source.newline;
            copy.cursor = source.cursor;
            copy.anchor = source.anchor;
            copy.scrollLine = source.scrollLine;
            result->documents_.push_back(std::move(copy));
        }
        result->owner_ = owner;
        result->activeIndex_ = activeIndex;
        result->prepared_ = true;
        diagnostic.clear();
        return result;
    } catch (...) {
        diagnostic = "EDITOR-SCRIPT-RECOVERY-PREPARATION-FAILED";
        return {};
    }
}

ScriptDocumentSession::RecoveryCommitResult ScriptDocumentSession::recoveryCommitReadiness(
    const PreparedRecovery& prepared) const noexcept
{
    if (!prepared.prepared_)
        return RecoveryCommitResult::Incomplete;
    if (prepared.committed_)
        return RecoveryCommitResult::Consumed;
    if (!prepared.owner_.sameCurrentEpoch(stateEpoch_.ticket()))
        return RecoveryCommitResult::Stale;
    if (!documentIds_.hasRemaining(static_cast<std::uint64_t>(prepared.documents_.size())))
        return RecoveryCommitResult::IdentityExhausted;
    return RecoveryCommitResult::Ready;
}

ScriptDocumentSession::RecoveryCommitResult ScriptDocumentSession::commitRecoveredDocuments(
    PreparedRecovery& prepared) noexcept
{
    const auto ready = recoveryCommitReadiness(prepared);
    if (ready != RecoveryCommitResult::Ready)
        return ready;
    if (!prepared.documents_.empty() || !documents_.empty()
        || activeIndex_ != prepared.activeIndex_) {
        // The original owner-thread sequence remains authoritative. No callback,
        // allocation, file access or failure point follows this readiness check.
        for (auto& document : prepared.documents_)
            document.id = documentIds_.tryAcquire();
        documents_.swap(prepared.documents_);
        activeIndex_ = prepared.activeIndex_;
        stateEpoch_.advance();
    }
    prepared.committed_ = true;
    return RecoveryCommitResult::Committed;
}

bool ScriptDocumentSession::replaceRecoveredDocuments(
    core::pmr::Vector<Document>& prepared, std::size_t activeIndex, core::pmr::String& diagnostic)
{
    auto transaction = prepareRecoveredDocuments(prepared, activeIndex, diagnostic);
    if (!transaction)
        return false;
    const auto result = commitRecoveredDocuments(*transaction);
    // No external operation can intervene in this combined owner-thread call.
    // A future readiness rejection must still leave the original input intact.
    if (result != RecoveryCommitResult::Committed) {
        diagnostic = "EDITOR-SCRIPT-RECOVERY-COMMIT-STALE";
        return false;
    }
    prepared.swap(transaction->documents_);
    diagnostic.clear();
    return true;
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
    stateEpoch_.advance();
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
    auto read = core::readTextFile(resolved, maxScriptBytes);
    if (!read.success)
        return {FileResultCode::ReadFailed};
    auto contents = std::move(read.text);

    Document document;
    document.path = resolved;
    document.newline = contents.find("\r\n") != core::pmr::String::npos ? "\r\n" : "\n";
    document.text = normalizeNewlines(contents);
    document.savedText = document.text;
    document.savedFileBytes = std::move(contents);
    const auto index = append(std::move(document));
    if (index == noActiveDocument)
        return {FileResultCode::IdentityExhausted};
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

    // Check exact source bytes, including newline-only and same-size changes.
    // This preflight is intentionally not advertised as a cross-process CAS.
    const auto disk = core::readTextFile(resolved, maxScriptBytes);
    if (!disk.success || disk.text != document->savedFileBytes)
        return {FileResultCode::ExternalConflict};
    if (!document->dirty())
        return {FileResultCode::Saved, activeIndex_};

    core::pmr::String encoded(document->savedFileBytes.get_allocator());
    const auto extra = document->newline == "\r\n"
        ? static_cast<std::size_t>(std::count(document->text.begin(), document->text.end(), '\n'))
        : 0u;
    if (document->text.size() > maxScriptBytes || extra > maxScriptBytes - document->text.size())
        return {FileResultCode::TooLarge};
    encoded.reserve(document->text.size() + extra);
    for (char value : document->text) {
        if (value == '\n' && extra)
            encoded.push_back('\r');
        encoded.push_back(value);
    }
    // Both baselines must be ready before the irreversible disk replacement.
    core::pmr::String savedText(document->text, document->savedText.get_allocator());
    core::pmr::Vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
    const auto result = core::io::replaceFileAtomically(core::FilePath::utf8(resolved), bytes);
    if (!result.success || !result.committed)
        return {FileResultCode::WriteFailed};
    document->savedText.swap(savedText);
    document->savedFileBytes.swap(encoded);
    stateEpoch_.advance();
    return {FileResultCode::Saved, activeIndex_};
}

ScriptDocumentSession::FileResult ScriptDocumentSession::reloadActiveFile(
    const std::filesystem::path& projectRoot, bool discardChanges)
{
    auto* document = active();
    if (!document)
        return {FileResultCode::NoActiveDocument};
    if (document->dirty() && !discardChanges)
        return {FileResultCode::UnsavedChanges};
    // Reuse canonical path checks, bounded reads and newline decoding without
    // publishing the candidate until the complete read has succeeded.
    ScriptDocumentSession candidate;
    const auto opened = candidate.openFile(projectRoot, document->path);
    if (!opened)
        return opened;
    candidate.setActiveIndex(opened.documentIndex);
    auto restored = std::move(*candidate.active());
    restored.id = document->id;
    if (document->text == restored.text && document->savedText == restored.savedText
        && document->savedFileBytes == restored.savedFileBytes
        && document->newline == restored.newline && document->cursor == 0 && document->anchor == 0
        && document->scrollLine == 0 && document->undo.empty() && document->redo.empty())
        return {FileResultCode::Opened, activeIndex_};
    *document = std::move(restored);
    stateEpoch_.advance();
    return {FileResultCode::Opened, activeIndex_};
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
    cursor = std::min(cursor, document->text.size());
    anchor = std::min(anchor, document->text.size());
    if (document->cursor != cursor || document->anchor != anchor)
        stateEpoch_.advance();
    document->cursor = cursor;
    document->anchor = anchor;
}

void ScriptDocumentSession::setActiveScrollLine(std::size_t line) noexcept
{
    if (auto* document = active()) {
        if (document->scrollLine != line)
            stateEpoch_.advance();
        document->scrollLine = line;
    }
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
    edit.cursorAfter = start + edit.inserted.size();
    edit.anchorAfter = edit.cursorAfter;
    core::pmr::String text(document->text, document->text.get_allocator());
    text.replace(start, length, edit.inserted.data(), edit.inserted.size());
    // Vector growth may fail; a nothrow Edit move keeps its old history intact.
    // After admission, same-allocator swap and scalar publication cannot fail.
    static_assert(std::is_nothrow_move_constructible_v<Edit>);
    document->undo.push_back(std::move(edit));
    document->text.swap(text);
    document->cursor = document->undo.back().cursorAfter;
    document->anchor = document->cursor;
    document->redo.clear();
    stateEpoch_.advance();
    return EditResult::Applied;
}

bool ScriptDocumentSession::undoActive()
{
    auto* document = active();
    if (!document || document->undo.empty())
        return false;
    auto& edit = document->undo.back();
    core::pmr::String text(document->text, document->text.get_allocator());
    text.replace(edit.offset, edit.inserted.size(), edit.removed.data(), edit.removed.size());
    document->redo.push_back(std::move(edit));
    document->text.swap(text);
    document->cursor = document->redo.back().cursorBefore;
    document->anchor = document->redo.back().anchorBefore;
    document->undo.pop_back();
    stateEpoch_.advance();
    return true;
}

bool ScriptDocumentSession::redoActive()
{
    auto* document = active();
    if (!document || document->redo.empty())
        return false;
    auto& edit = document->redo.back();
    core::pmr::String text(document->text, document->text.get_allocator());
    text.replace(edit.offset, edit.removed.size(), edit.inserted.data(), edit.inserted.size());
    document->undo.push_back(std::move(edit));
    document->text.swap(text);
    document->cursor = document->undo.back().cursorAfter;
    document->anchor = document->undo.back().anchorAfter;
    document->redo.pop_back();
    stateEpoch_.advance();
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
