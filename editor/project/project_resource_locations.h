#pragma once

#include "editor/project/project_input_snapshot.h"
#include "editor/project/project_source_session.h"
#include "resource/resource_manager.h"

namespace arp::resource {
struct InternalAssetPackageEntry;
}

namespace arp::editor {
class ProjectResourceSources;

// Locations describe resource definitions, never inferred original source files.
// Built only from validated explicit package entries and retained owner metadata.
class ProjectResourceLocations final {
public:
    static constexpr ProjectInputBudget budget{};

    struct Definition {
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        core::pmr::String resourceType{&core::defaultCpuMemoryResource()};

        void swap(Definition& other) noexcept
        {
            resourceId.swap(other.resourceId);
            resourceType.swap(other.resourceType);
        }
    };

    static const char* capture(const std::filesystem::path& root,
        const core::pmr::Vector<core::pmr::String>& packages,
        core::SharedPtr<resource::ResourceManager> owner,
        ProjectResourceLocations& destination) noexcept;
    static const char* captureSources(const ProjectResourceSources&,
        core::io::FileService&,
        std::uint64_t epoch,
        const core::pmr::Vector<core::pmr::String>& packages,
        core::SharedPtr<resource::ResourceManager>,
        ProjectResourceLocations&) noexcept;
    const char* resolveSource(std::string_view id,
        std::string_view type,
        const resource::ResourceManager&,
        const ProjectResourceSources&,
        core::io::FileService&,
        std::uint64_t epoch,
        core::io::FileReadRequest& destination) const noexcept;
    const char* identifySource(const core::io::FileReadRequest&,
        const resource::ResourceManager&,
        const ProjectResourceSources&,
        core::io::FileService&,
        std::uint64_t epoch,
        Definition& destination) const noexcept;

    bool contains(std::string_view id, std::string_view type) const noexcept;
    // Exact canonical envelope identity; no directory scan, extension inference
    // or resource load. As with resolve(), failure preserves the destination.
    const char* identify(const std::filesystem::path& envelope,
        const resource::ResourceManager& owner,
        Definition& destination) const noexcept;
    const char* resolve(std::string_view id,
        std::string_view type,
        const resource::ResourceManager& owner,
        std::filesystem::path& destination) const noexcept;

    std::size_t size() const noexcept { return entries_.size(); }

private:
    const char* addDefinition(const resource::InternalAssetPackageEntry&,
        core::pmr::String manifest,
        core::pmr::String envelope);
    void swap(ProjectResourceLocations&) noexcept;

    struct Entry {
        core::pmr::String id{&core::defaultCpuMemoryResource()};
        core::pmr::String type{&core::defaultCpuMemoryResource()};
        core::pmr::String manifestPath{&core::defaultCpuMemoryResource()};
        core::pmr::String envelopePath{&core::defaultCpuMemoryResource()};
        resource::ResourceGeneration generation = 0;
        std::uint32_t contentHash = 0;
    };

    std::filesystem::path root_;
    core::SharedPtr<resource::ResourceManager> owner_;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ProjectInputBinding> inputs_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<ProjectSourceLocation> location_;
};

} // namespace arp::editor
