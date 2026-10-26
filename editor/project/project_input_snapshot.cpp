#include "editor/project/project_input_snapshot.h"

#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/text/utf8.h"

#include <algorithm>

namespace arp::editor {
namespace {
bool fail(core::pmr::String& diagnostic, const char* code)
{
    diagnostic = code;
    return false;
}

bool validBudget(ProjectInputBudget budget)
{
    const ProjectInputBudget hard;
    return budget.maxFiles && budget.maxFiles <= hard.maxFiles && budget.maxFileBytes
        && budget.maxFileBytes <= hard.maxFileBytes && budget.maxTotalBytes
        && budget.maxTotalBytes <= hard.maxTotalBytes;
}

bool canonicalFile(const std::filesystem::path& root,
    const std::filesystem::path& file,
    std::filesystem::path& path,
    core::pmr::String& relative,
    core::pmr::String& diagnostic)
{
    std::error_code error;
    if (std::filesystem::canonical(root, error) != root || error)
        return fail(diagnostic, "PROJECT-INPUT-ROOT");
    path = std::filesystem::canonical(file, error);
    if (error || !core::FilePath::contains(root, path)
        || !std::filesystem::is_regular_file(path, error) || error)
        return fail(diagnostic, "PROJECT-INPUT-PATH");
    relative = core::FilePath::utf8(path.lexically_relative(root));
    if (!core::FilePath::isSafeRelativePath(relative, ProjectInputBudget::maxPathBytes)
        || core::Utf8Decoder::validate(relative)
        || std::any_of(relative.begin(), relative.end(), [](unsigned char c) {
               return c < 32u;
           }))
        return fail(diagnostic, "PROJECT-INPUT-PATH");
    return true;
}
} // namespace

ProjectInputSnapshot::ProjectInputSnapshot(std::filesystem::path root, ProjectInputBudget budget)
    : root_(std::move(root)), budget_(budget)
{}

bool ProjectInputSnapshot::record(
    const std::filesystem::path& file, std::string_view bytes, core::pmr::String& diagnostic)
{
    if (!validBudget(budget_) || bytes.size() > budget_.maxFileBytes)
        return fail(diagnostic, "PROJECT-INPUT-BUDGET");
    std::filesystem::path path;
    core::pmr::String relative;
    if (!canonicalFile(root_, file, path, relative, diagnostic))
        return false;
    const auto found = std::lower_bound(
        bindings_.begin(), bindings_.end(), relative, [](const auto& binding, const auto& key) {
            return binding.path < key;
        });
    const auto fingerprint = core::Sha256::hexDigest(bytes);
    if (found != bindings_.end() && found->path == relative) {
        if (found->byteCount != bytes.size() || found->fingerprint != fingerprint)
            return fail(diagnostic, "PROJECT-INPUT-CONTENT-CHANGED");
    } else {
        if (bindings_.size() >= budget_.maxFiles
            || bytes.size() > budget_.maxTotalBytes - usedBytes_)
            return fail(diagnostic, "PROJECT-INPUT-BUDGET");
        bindings_.insert(found, {std::move(relative), bytes.size(), fingerprint});
        usedBytes_ += bytes.size();
    }
    diagnostic.clear();
    return true;
}

bool ProjectInputSnapshot::read(const std::filesystem::path& file, core::pmr::String& diagnostic)
{
    if (!validBudget(budget_))
        return fail(diagnostic, "PROJECT-INPUT-BUDGET");
    std::filesystem::path path;
    core::pmr::String relative;
    if (!canonicalFile(root_, file, path, relative, diagnostic))
        return false;
    const auto bytes = core::readTextFile(path, budget_.maxFileBytes);
    if (!bytes.success)
        return fail(diagnostic, "PROJECT-INPUT-READ-OR-BUDGET");
    return record(path, bytes.text, diagnostic);
}

bool ProjectInputSnapshot::validateBindings(const core::pmr::Vector<ProjectInputBinding>& bindings,
    core::pmr::String& diagnostic,
    ProjectInputBudget budget)
{
    if (!validBudget(budget) || bindings.size() > budget.maxFiles)
        return fail(diagnostic, "PROJECT-INPUT-BUDGET");
    std::size_t remaining = budget.maxTotalBytes;
    std::string_view previous;
    for (const auto& binding : bindings) {
        if (!core::FilePath::isSafeRelativePath(binding.path, ProjectInputBudget::maxPathBytes)
            || core::Utf8Decoder::validate(binding.path)
            || std::any_of(binding.path.begin(),
                binding.path.end(),
                [](unsigned char c) {
                    return c < 32u;
                })
            || (!previous.empty() && previous >= std::string_view(binding.path))
            || binding.byteCount > budget.maxFileBytes || binding.byteCount > remaining
            || binding.fingerprint.size() != 71u || binding.fingerprint.substr(0, 7) != "sha256:"
            || !core::Sha256::isHexDigest(std::string_view(binding.fingerprint).substr(7)))
            return fail(diagnostic, "PROJECT-INPUT-BINDING-INVALID");
        previous = binding.path;
        remaining -= binding.byteCount;
    }
    diagnostic.clear();
    return true;
}

bool ProjectInputSnapshot::verify(const std::filesystem::path& root,
    const core::pmr::Vector<ProjectInputBinding>& bindings,
    core::pmr::String& diagnostic,
    ProjectInputBudget budget)
{
    if (!validateBindings(bindings, diagnostic, budget))
        return false;
    for (const auto& binding : bindings) {
        std::filesystem::path path;
        core::pmr::String relative;
        if (!canonicalFile(
                root, root / std::filesystem::u8path(binding.path), path, relative, diagnostic))
            return false;
        if (relative != binding.path)
            return fail(diagnostic, "PROJECT-INPUT-TARGET-CHANGED");
        const auto bytes = core::readTextFile(path, binding.byteCount);
        if (!bytes.success || bytes.text.size() != binding.byteCount
            || core::Sha256::hexDigest(bytes.text) != binding.fingerprint)
            return fail(diagnostic, "PROJECT-INPUT-CONTENT-CHANGED");
    }
    diagnostic.clear();
    return true;
}

bool ProjectInputSnapshot::verify(core::pmr::String& diagnostic) const
{
    return verify(root_, bindings_, diagnostic, budget_);
}

core::pmr::Vector<ProjectInputBinding> ProjectInputSnapshot::takeBindings()
{
    usedBytes_ = 0;
    core::pmr::Vector<ProjectInputBinding> result;
    result.swap(bindings_);
    return result;
}

bool ProjectInputSnapshot::equal(const core::pmr::Vector<ProjectInputBinding>& left,
    const core::pmr::Vector<ProjectInputBinding>& right) noexcept
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b) {
               return a.path == b.path && a.byteCount == b.byteCount
                   && a.fingerprint == b.fingerprint;
           });
}

} // namespace arp::editor
