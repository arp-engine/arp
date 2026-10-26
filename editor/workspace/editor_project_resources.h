#pragma once

#include "core/memory/cpu_allocator.h"
#include "editor/project/project_input_snapshot.h"
#include "editor/project/project_metadata.h"
#include "editor/project/project_package_snapshot.h"
#include "editor/project/project_resource_locations.h"
#include "physics/2d/physics_world_2d.h"
#include "resource/resource_manager.h"

#include <filesystem>

namespace arp::editor {

class EditorSceneDocument;

struct EditorProjectResourceLoadResult {
    bool success = false;
    bool manifestFound = false;
    std::size_t packageCount = 0;
    bool physicsWorld2DReady = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class EditorProjectResources final {
public:
    static constexpr std::size_t maxManifestBytes = ProjectManifestBudget{}.maxDocumentBytes;
    static constexpr std::size_t maxAssetPackages = ProjectManifestBudget{}.maxAssetPackages;

    EditorProjectResources();
    EditorProjectResources(const EditorProjectResources&) = delete;
    EditorProjectResources& operator=(const EditorProjectResources&) = delete;
    EditorProjectResources(EditorProjectResources&&) = default;
    EditorProjectResources& operator=(EditorProjectResources&&) = default;

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

    // Editor consumers with borrowed handles/observers retain this owner until
    // they detach. This is CPU ownership, never proof of GPU completion.
    core::SharedPtr<resource::ResourceManager> retainManager() const noexcept { return manager_; }

    // Cold documents only: existing component callbacks are not rebound.
    // Retain these exact owners even if this bundle is moved/reloaded/destroyed.
    // Does not authorize live/GPU publication or prove resource freshness.
    bool bindSceneServices(EditorSceneDocument&, core::pmr::String& diagnostic) const;

    const EditorProjectResourceLoadResult& lastLoad() const noexcept { return lastLoad_; }

    std::string_view manifestSnapshot() const noexcept { return manifestSnapshot_; }

    // Null means directory browsing without an authoring project. No identity or
    // configuration is synthesized for a folder that has no project manifest.
    const ProjectManifest* manifest() const noexcept { return manifest_ ? &*manifest_ : nullptr; }

    const ProjectMetadata& metadata() const noexcept { return metadata_; }

    std::string_view metadataSnapshot() const noexcept { return metadataSnapshot_; }

    const core::pmr::Vector<ProjectInputBinding>& inputBindings() const noexcept
    {
        return inputBindings_;
    }

    // An unbound provenance profile does not reinterpret successfully loaded
    // legacy resources. Recovery must reject it with the exposed diagnostic.
    bool packageProvenanceBound() const noexcept { return packageProvenanceDiagnostic_.empty(); }

    std::string_view packageProvenanceDiagnostic() const noexcept
    {
        return packageProvenanceDiagnostic_;
    }

    const core::pmr::Vector<ProjectPackageBinding>& packageBindings() const noexcept
    {
        return packageBindings_;
    }

    core::SharedPtr<physics2d::PhysicsWorld2D> physicsWorld2D() const noexcept
    {
        return physicsWorld2D_;
    }

    bool packageRootForResource(
        resource::NameView resourceId, std::filesystem::path& packageRoot) const;

    const ProjectResourceLocations& resourceLocations() const noexcept
    {
        return resourceLocations_;
    }

private:
    friend class EditorShell;
    // Only the workspace's validated settings save may update this snapshot.
    // It preserves all resource/physics ownership and the dependency contract.
    void publishSavedSettings(ProjectManifest manifest, core::pmr::String snapshot);

    struct PackageResourceRoot {
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        std::filesystem::path packageRoot;
    };

    core::SharedPtr<resource::ResourceManager> manager_;
    core::SharedPtr<physics2d::PhysicsWorld2D> physicsWorld2D_;
    core::pmr::Vector<PackageResourceRoot> packageRoots_{&core::defaultCpuMemoryResource()};
    ProjectResourceLocations resourceLocations_;
    EditorProjectResourceLoadResult lastLoad_;
    core::pmr::String manifestSnapshot_{&core::defaultCpuMemoryResource()};
    std::optional<ProjectManifest> manifest_;
    ProjectMetadata metadata_;
    core::pmr::String metadataSnapshot_;
    core::pmr::Vector<ProjectInputBinding> inputBindings_;
    core::pmr::Vector<ProjectPackageBinding> packageBindings_;
    core::pmr::String packageProvenanceDiagnostic_{"EDITOR-PROJECT-PACKAGE-PROVENANCE-NOT-BOUND"};
};

} // namespace arp::editor
