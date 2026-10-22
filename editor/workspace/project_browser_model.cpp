#include "editor/workspace/project_browser_model.h"

#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"

#include <algorithm>
#include <system_error>

namespace arp::editor {
namespace {
bool lessIgnoringCase(const core::pmr::String& lhs, const core::pmr::String& rhs)
{
    const auto leftLess = std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char a, char b) {
            return core::toLowerAscii(a) < core::toLowerAscii(b);
        });
    const auto rightLess = std::lexicographical_compare(
        rhs.begin(), rhs.end(), lhs.begin(), lhs.end(), [](char a, char b) {
            return core::toLowerAscii(a) < core::toLowerAscii(b);
        });
    if (!leftLess && !rightLess)
        return lhs < rhs;
    return leftLess;
}
} // namespace

void ProjectBrowserModel::setRoot(std::filesystem::path root)
{
    root_ = std::move(root);
    currentDirectory_ = root_;
    backHistory_.clear();
    forwardHistory_.clear();
    entries_.clear();
    visibleEntries_.clear();
}

bool ProjectBrowserModel::refresh()
{
    entries_.clear();
    visibleEntries_.clear();
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        currentDirectory_, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    if (error)
        return false;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        std::error_code statusError;
        const auto status = iterator->symlink_status(statusError);
        if (statusError || std::filesystem::is_symlink(status))
            continue;
        const bool directory = std::filesystem::is_directory(status);
        if (!directory && !std::filesystem::is_regular_file(status))
            continue;
        auto resolved = std::filesystem::weakly_canonical(iterator->path(), statusError);
        if (statusError || !core::FilePath::contains(root_, resolved))
            continue;
        Entry entry;
        entry.path = std::move(resolved);
        entry.name = core::FilePath::utf8(iterator->path().filename());
        entry.directory = directory;
        entries_.push_back(std::move(entry));
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& lhs, const Entry& rhs) {
        if (lhs.directory != rhs.directory)
            return lhs.directory > rhs.directory;
        return lessIgnoringCase(lhs.name, rhs.name);
    });
    setSearch(search_);
    return true;
}

ProjectBrowserModel::NavigationResult ProjectBrowserModel::navigate(
    const std::filesystem::path& path, bool remember)
{
    std::error_code error;
    const auto candidate = std::filesystem::weakly_canonical(
        path.is_absolute() ? path : currentDirectory_ / path, error);
    if (error || !core::FilePath::contains(root_, candidate)
        || !std::filesystem::is_directory(candidate, error) || error)
        return NavigationResult::Rejected;
    return navigateResolved(candidate, remember);
}

ProjectBrowserModel::NavigationResult ProjectBrowserModel::navigateResolved(
    const std::filesystem::path& candidate, bool remember)
{
    if (candidate == currentDirectory_)
        return NavigationResult::Unchanged;
    if (remember) {
        backHistory_.push_back(currentDirectory_);
        forwardHistory_.clear();
    }
    currentDirectory_ = candidate;
    return NavigationResult::Navigated;
}

bool ProjectBrowserModel::back()
{
    if (backHistory_.empty())
        return false;
    const auto candidate = backHistory_.back();
    const auto previous = currentDirectory_;
    if (navigate(candidate, false) == NavigationResult::Rejected)
        return false;
    backHistory_.pop_back();
    forwardHistory_.push_back(previous);
    return true;
}

bool ProjectBrowserModel::forward()
{
    if (forwardHistory_.empty())
        return false;
    const auto candidate = forwardHistory_.back();
    const auto previous = currentDirectory_;
    if (navigate(candidate, false) == NavigationResult::Rejected)
        return false;
    forwardHistory_.pop_back();
    backHistory_.push_back(previous);
    return true;
}

void ProjectBrowserModel::setSearch(const core::pmr::String& query)
{
    search_ = query;
    visibleEntries_.clear();
    for (std::size_t index = 0; index < entries_.size(); ++index)
        if (search_.empty() || containsIgnoringCase(entries_[index].name, search_))
            visibleEntries_.push_back(index);
}

bool ProjectBrowserModel::containsIgnoringCase(
    const core::pmr::String& value, const core::pmr::String& query)
{
    return std::search(value.begin(), value.end(), query.begin(), query.end(), [](char a, char b) {
        return core::toLowerAscii(a) == core::toLowerAscii(b);
    }) != value.end();
}
} // namespace arp::editor
