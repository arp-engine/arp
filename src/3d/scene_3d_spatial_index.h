#pragma once

#include "3d/dynamic_aabb_tree.h"
#include "core/container/array_view.h"
#include "core/diagnostics/diagnostic_error.h"
#include "scene/ids.h"
#include "scene/render_extract.h"

#include <array>

namespace arp::scene {
class Scene;
class Component;
class RenderExtractSink;
} // namespace arp::scene

namespace arp::scene3d {
enum class SpatialMobility { Static, Dynamic };

struct SceneSpatialHandle {
    std::uint32_t slot = UINT32_MAX, generation = 0;

    bool valid() const { return generation != 0; }

    friend bool operator==(SceneSpatialHandle a, SceneSpatialHandle b)
    {
        return a.slot == b.slot && a.generation == b.generation;
    }

    friend bool operator!=(SceneSpatialHandle a, SceneSpatialHandle b) { return !(a == b); }
};

struct SceneSpatialOwner {
    scene::GameObjectId object;
    scene::ComponentId component;
    bool extractOutsideView = false;
};

struct SceneSpatialTelemetry {
    std::size_t objects = 0, visible = 0, nodeTests = 0, candidateLeaves = 0;
    std::uint64_t staticBuilds = 0, dynamicUpdates = 0, queries = 0, cacheHits = 0;
    std::uint64_t contextResolves = 0, contextCacheHits = 0, extractionDispatches = 0;
};

// Scene-3D-owned persistent renderable registry. Providers supply coarse world
// bounds on admission/transform changes, never a per-frame skinned mesh.
// Object/component IDs are borrowed identities: erase before their retirement.
class Scene3DSpatialIndex final {
public:
    void reserve(std::size_t objects);
    SceneSpatialHandle insert(SceneSpatialOwner owner, const Aabb& world, SpatialMobility mobility);
    bool update(SceneSpatialHandle handle, const Aabb& world);
    void erase(SceneSpatialHandle handle);
    // Canonical row-major shader view-projection, depth [0,w]. Null disables
    // filtering. Identical camera and bounds revision reuse the visible set.
    void query(const std::array<float, 16>* viewProjection);
    bool visible(SceneSpatialHandle handle) const;
    SceneSpatialOwner owner(SceneSpatialHandle handle) const;
    // Dispatch only the registered components selected by the last query.
    // Extraction order follows stable object/component identity. Persistent
    // emitters may opt in to extraction outside the mesh's view bounds.
    void extractVisible(const scene::Scene& scene, scene::RenderExtractSink& sink) const;
    // Additional unbounded components have no invented proxy bounds. They must
    // be sorted, unique, and disjoint from this index; dispatch merges both
    // streams in the same stable identity order as ordinary scene extraction.
    void extractVisible(const scene::Scene& scene,
        scene::RenderExtractSink& sink,
        core::ArrayView<SceneSpatialOwner> unbounded) const;

    const auto& visibleHandles() const { return visible_; }

    const auto& changedHandles() const { return changed_; }

    const auto& telemetry() const { return telemetry_; }

private:
    struct ExtractionBinding {
        const scene::GameObject* object = nullptr;
        const scene::Component* component = nullptr;
        scene::Transform world;
        std::uint64_t revision = 0;
        bool active = false;
    };

    struct UnboundedBinding {
        SceneSpatialOwner owner;
        ExtractionBinding binding;
    };

    struct Entry {
        SceneSpatialOwner owner;
        Aabb bounds;
        SpatialMobility mobility = SpatialMobility::Static;
        std::uint32_t generation = 1, proxy = UINT32_MAX;
        bool active = false, visible = true;
        std::uint64_t stamp = 0;
        mutable ExtractionBinding binding;
    };
    class Sink;
    bool live(SceneSpatialHandle handle) const;
    Entry& require(SceneSpatialHandle handle);
    const Entry& require(SceneSpatialHandle handle) const;
    void accept(std::uint32_t slot);
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> free_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneSpatialHandle> visible_{&core::defaultCpuMemoryResource()},
        previous_{&core::defaultCpuMemoryResource()}, changed_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneSpatialHandle> alwaysExtract_{&core::defaultCpuMemoryResource()},
        extraction_{&core::defaultCpuMemoryResource()};
    mutable core::SharedPtr<const scene::RenderContextEpoch> extractionIdentity_;
    mutable bool extracting_ = false;
    mutable core::pmr::Vector<UnboundedBinding> unboundedBindings_{
        &core::defaultCpuMemoryResource()};
    Bvh static_;
    DynamicAabbTree dynamic_;
    std::array<float, 16> camera_{};
    std::uint64_t revision_ = 0, queriedRevision_ = UINT64_MAX, stamp_ = 0;
    bool staticDirty_ = false, enabled_ = false;
    mutable SceneSpatialTelemetry telemetry_;
};
} // namespace arp::scene3d
