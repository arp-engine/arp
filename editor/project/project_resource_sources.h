#pragma once

#include "core/io/file_io.h"
#include "editor/project/project_source_session.h"
#include "resource/internal_asset_package.h"

namespace arp::editor {

// Cold, bounded projection for existing resource parsers. Source bytes are copied
// once into their owned ResourceSourceSet contract; no new parser or IO path.
class ProjectResourceSources final {
public:
    bool capture(core::SharedPtr<const ProjectSourceSnapshot>,
        core::io::FileService&,
        std::uint64_t epoch,
        std::size_t maxBytes,
        core::pmr::String& diagnostic);
    bool validate(
        core::io::FileService&, std::uint64_t epoch, const char*& diagnostic) const noexcept;

    const ProjectSourceSnapshot& snapshot() const { return *snapshot_; }

    resource::AssetPackageSourceContext context(std::size_t maxDecodedBytes) const;

    const std::filesystem::path& root() const noexcept { return root_; }

    bool path(std::string_view relative, std::filesystem::path& output) const;
    const resource::ResourceSourceBuffer* find(std::string_view relative) const;
    core::TextFileReadResult text(std::string_view relative, std::size_t maxBytes) const;

private:
    std::filesystem::path root_;
    resource::ResourceFileOrigin origin_;
    core::UniquePtr<resource::ResourceSourceSet> sources_;
    core::SharedPtr<const ProjectSourceSnapshot> snapshot_;
};

} // namespace arp::editor
