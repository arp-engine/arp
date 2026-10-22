#pragma once

#include "core/memory/cpu_allocator.h"
#include "physics/2d/physics_world_2d.h"
#include "resource/resource_manager.h"

#include <filesystem>

namespace arp::editor {

struct EditorProjectResourceLoadResult {
    bool success = false;
    bool manifestFound = false;
    std::size_t packageCount = 0;
    bool physicsWorld2DReady = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class EditorProjectResources final {
public:
    static constexpr std::size_t maxManifestBytes = 1024u * 1024u;
    static constexpr std::size_t maxAssetPackages = 256u;

    EditorProjectResources();

    EditorProjectResourceLoadResult load(const std::filesystem::path& projectRoot);

    // Preparation entry for a validated candidate project manifest. Publication
    // remains with the command owner after its on-disk transaction succeeds.
    EditorProjectResourceLoadResult loadManifest(
        const std::filesystem::path& projectRoot, std::string_view yaml);

    // Asset-package publication does not replace the live physics world. The
    // caller must verify that the project configuration has not changed.
    void publishAssetPackages(EditorProjectResources&& candidate);

    resource::ResourceManager& manager() noexcept { return *manager_; }

    const resource::ResourceManager& manager() const noexcept { return *manager_; }

    const EditorProjectResourceLoadResult& lastLoad() const noexcept { return lastLoad_; }

    std::string_view manifestSnapshot() const noexcept { return manifestSnapshot_; }

    core::SharedPtr<physics2d::PhysicsWorld2D> physicsWorld2D() const noexcept
    {
        return physicsWorld2D_;
    }

    bool packageRootForResource(
        resource::NameView resourceId, std::filesystem::path& packageRoot) const;

private:
    struct PackageResourceRoot {
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        std::filesystem::path packageRoot;
    };

    core::UniquePtr<resource::ResourceManager> manager_;
    core::SharedPtr<physics2d::PhysicsWorld2D> physicsWorld2D_;
    core::pmr::Vector<PackageResourceRoot> packageRoots_{&core::defaultCpuMemoryResource()};
    EditorProjectResourceLoadResult lastLoad_;
    core::pmr::String manifestSnapshot_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::editor
