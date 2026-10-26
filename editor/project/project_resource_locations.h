#pragma once

#include "editor/project/project_input_snapshot.h"
#include "resource/resource_manager.h"

namespace arp::editor {

// Locations describe resource definitions, never inferred original source files.
// Built only from validated explicit package entries and retained owner metadata.
class ProjectResourceLocations final {
public:
    static constexpr ProjectInputBudget budget{};

    struct Definition {
        core::pmr::String resourceId, resourceType;

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
    struct Entry {
        core::pmr::String id, type, manifestPath, envelopePath;
        resource::ResourceGeneration generation = 0;
        std::uint32_t contentHash = 0;
    };

    std::filesystem::path root_;
    core::SharedPtr<resource::ResourceManager> owner_;
    core::pmr::Vector<Entry> entries_;
    core::pmr::Vector<ProjectInputBinding> inputs_;
};

} // namespace arp::editor
