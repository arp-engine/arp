#pragma once

#include "core/memory/cpu_allocator.h"

#include <filesystem>

namespace arp::resource {
class AssetPackageCodecRegistry;
struct AssetPackageSourceContext;
} // namespace arp::resource

namespace arp::editor {

struct ProjectPackageBinding {
    core::pmr::String directory;
    core::pmr::String fingerprint;
};

// Editor authoring provenance, independent of runtime resource interpretation.
// Uses the existing explicit resource-package closure, never directory discovery.
class ProjectPackageSnapshot final {
public:
    static constexpr std::size_t maxPackageFiles = 4096u;
    static constexpr std::size_t maxPackageFileBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t maxPackageBytes = 256u * 1024u * 1024u;
    static constexpr std::size_t maxAllPackageBytes = 512u * 1024u * 1024u;
    static constexpr std::size_t maxAllDecodedBytes = 512u * 1024u * 1024u;

    // The v1 binding schema and diagnostics are preserved from recovery.
    // Failure preserves destination and does not write any resource file.
    static bool capture(const std::filesystem::path& root,
        const core::pmr::Vector<core::pmr::String>& directories,
        core::SharedPtr<const resource::AssetPackageCodecRegistry> codecs,
        core::pmr::Vector<ProjectPackageBinding>& destination,
        core::pmr::String& diagnostic);
    static bool equal(const core::pmr::Vector<ProjectPackageBinding>& left,
        const core::pmr::Vector<ProjectPackageBinding>& right) noexcept;
    static bool captureSources(const std::filesystem::path& root,
        const core::pmr::Vector<core::pmr::String>& directories,
        core::SharedPtr<const resource::AssetPackageCodecRegistry> codecs,
        const resource::AssetPackageSourceContext&,
        core::pmr::Vector<ProjectPackageBinding>& destination,
        core::pmr::String& diagnostic);

private:
    static bool captureImpl(const std::filesystem::path&,
        const core::pmr::Vector<core::pmr::String>&,
        core::SharedPtr<const resource::AssetPackageCodecRegistry>,
        core::pmr::Vector<ProjectPackageBinding>&,
        core::pmr::String&,
        const resource::AssetPackageSourceContext*);
};

} // namespace arp::editor
