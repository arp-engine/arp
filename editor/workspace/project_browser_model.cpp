#include "editor/workspace/project_browser_model.h"

#include "core/io/filesystem_path.h"
#include "core/text/string_utils.h"

#include <algorithm>
#include <new>
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

bool ProjectBrowserModel::refresh(std::size_t entryBudget)
{
    entries_.clear();
    visibleEntries_.clear();
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        currentDirectory_, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    if (error)
        return false;
    std::size_t visited = 0;
    for (; iterator != end; iterator.increment(error)) {
        if (entryBudget && (++visited > entryBudget || error))
            return false;
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
    if (entryBudget && error)
        return false;
    std::sort(entries_.begin(), entries_.end(), [](const Entry& lhs, const Entry& rhs) {
        if (lhs.directory != rhs.directory)
            return lhs.directory > rhs.directory;
        return lessIgnoringCase(lhs.name, rhs.name);
    });
    setSearch(search_);
    return true;
}

const char* ProjectBrowserModel::prepareReveal(const std::filesystem::path& file,
    ProjectBrowserModel& destination,
    std::size_t& visibleIndex) const noexcept
{
    constexpr std::size_t maxEntries = 4096;
    try {
        std::error_code error;
        const auto canonical = std::filesystem::canonical(file, error);
        if (error || canonical != file || !core::FilePath::contains(root_, canonical)
            || !std::filesystem::is_regular_file(canonical, error) || error)
            return "EDITOR-RESOURCE-NAVIGATION-FILE";
        if (backHistory_.size() >= maxEntries || forwardHistory_.size() >= maxEntries
            || entries_.size() > maxEntries)
            return "EDITOR-RESOURCE-NAVIGATION-BROWSER-BUDGET";
        auto candidate = *this;
        if (candidate.navigate(canonical.parent_path()) == NavigationResult::Rejected
            || !candidate.refresh(maxEntries))
            return "EDITOR-RESOURCE-NAVIGATION-DIRECTORY-OR-BUDGET";
        candidate.setSearch("");
        const auto found = std::find_if(
            candidate.entries_.begin(), candidate.entries_.end(), [&](const auto& entry) {
                return !entry.directory && entry.path == canonical;
            });
        if (found == candidate.entries_.end())
            return "EDITOR-RESOURCE-NAVIGATION-ROW-MISSING";
        const auto index = static_cast<std::size_t>(found - candidate.entries_.begin());
        destination.swap(candidate);
        visibleIndex = index;
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-RESOURCE-NAVIGATION-ALLOCATION-FAILED";
    } catch (const std::filesystem::filesystem_error&) {
        return "EDITOR-RESOURCE-NAVIGATION-FILE";
    }
}

void ProjectBrowserModel::swap(ProjectBrowserModel& other) noexcept
{
    root_.swap(other.root_);
    currentDirectory_.swap(other.currentDirectory_);
    entries_.swap(other.entries_);
    visibleEntries_.swap(other.visibleEntries_);
    backHistory_.swap(other.backHistory_);
    forwardHistory_.swap(other.forwardHistory_);
    search_.swap(other.search_);
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
