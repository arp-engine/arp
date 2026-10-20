#pragma once

#include "core/cpu_allocator.h"
#include "physics2d/physics_world_2d.h"
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
    EditorProjectResources();

    EditorProjectResourceLoadResult load(const std::filesystem::path& projectRoot);

    resource::ResourceManager& manager() noexcept { return *manager_; }

    const resource::ResourceManager& manager() const noexcept { return *manager_; }

    const EditorProjectResourceLoadResult& lastLoad() const noexcept { return lastLoad_; }

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
};

} // namespace arp::editor
