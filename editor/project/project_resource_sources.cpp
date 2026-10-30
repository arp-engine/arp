#include "editor/project/project_resource_sources.h"

#include "resource/resource_source_io.h"

namespace arp::editor {

bool ProjectResourceSources::capture(core::SharedPtr<const ProjectSourceSnapshot> snapshot,
    core::io::FileService& files,
    std::uint64_t epoch,
    std::size_t maxBytes,
    core::pmr::String& diagnostic)
{
    const char* issue = nullptr;
    if (!snapshot || !maxBytes || maxBytes == SIZE_MAX || snapshot->totalBytes() > maxBytes) {
        diagnostic = "EDITOR-PROJECT-SOURCE-PROJECTION-BUDGET";
        return false;
    }
    if (!snapshot->validate(files, epoch, issue)) {
        diagnostic = issue;
        return false;
    }
    auto candidate = core::makeUnique<resource::ResourceSourceSet>();
    auto root = std::filesystem::u8path(snapshot->location().root);
    for (std::size_t i = 0; i < snapshot->size(); ++i) {
        const auto* entry = snapshot->entryAt(i);
        if (!entry) {
            diagnostic = "EDITOR-PROJECT-SOURCE-NOT-SEALED";
            return false;
        }
        resource::ResourceSourceBuffer source;
        source.path = entry->binding.path;
        source.pathScope = entry->binding.pathScope;
        source.mount = entry->binding.mount;
        source.version = entry->receipt.version;
        source.role = "project-acquired-bytes/v1";
        source.bytes = entry->receipt.bytes;
        if (!candidate->add(std::move(source))) {
            diagnostic = "EDITOR-PROJECT-SOURCE-DUPLICATE";
            return false;
        }
    }
    if (!snapshot->validate(files, epoch, issue)) {
        diagnostic = issue;
        return false;
    }
    root_.swap(root);
    sources_.swap(candidate);
    origin_ = {core::io::FilePathScope::MountedResource, snapshot->location().mount};
    snapshot_ = std::move(snapshot);
    diagnostic.clear();
    return true;
}

bool ProjectResourceSources::validate(
    core::io::FileService& files, std::uint64_t epoch, const char*& diagnostic) const noexcept
{
    if (!snapshot_) {
        diagnostic = "EDITOR-PROJECT-SOURCE-NOT-PREPARED";
        return false;
    }
    return snapshot_->validate(files, epoch, diagnostic);
}

resource::AssetPackageSourceContext ProjectResourceSources::context(
    std::size_t maxDecodedBytes) const
{
    resource::AssetPackageSourceContext result;
    result.origin = origin_;
    result.resources.sources = sources_.get();
    result.resources.requirePreloadedSources = true;
    result.resources.maxDecodedBytes = maxDecodedBytes;
    return result;
}

bool ProjectResourceSources::path(std::string_view relative, std::filesystem::path& output) const
{
    return sources_ && ProjectInputSnapshot::validLogicalPath(relative)
        && resource::resolveResourceSourcePath(root_, relative, origin_, output);
}

const resource::ResourceSourceBuffer* ProjectResourceSources::find(std::string_view relative) const
{
    std::filesystem::path resolved;
    return path(relative, resolved) ? sources_->find(core::FilePath::utf8(resolved)) : nullptr;
}

core::TextFileReadResult ProjectResourceSources::text(
    std::string_view relative, std::size_t maxBytes) const
{
    std::filesystem::path resolved;
    if (!maxBytes || maxBytes == SIZE_MAX || !path(relative, resolved)) {
        core::TextFileReadResult result;
        result.diagnostics = "EDITOR-PROJECT-SOURCE-PATH-OR-BUDGET";
        return result;
    }
    return resource::readResourceText(
        core::FilePath::utf8(resolved), context(0).resources, maxBytes, &origin_);
}

} // namespace arp::editor
