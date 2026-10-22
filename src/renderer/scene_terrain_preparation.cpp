#include "renderer/scene_terrain_preparation.h"

#include <algorithm>

namespace arp::renderer {
namespace {
bool sameTransform(const scene::Transform& a, const scene::Transform& b)
{
    return a.translation.x == b.translation.x && a.translation.y == b.translation.y
        && a.translation.z == b.translation.z && a.scale.x == b.scale.x && a.scale.y == b.scale.y
        && a.scale.z == b.scale.z && a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y
        && a.rotation.z == b.rotation.z && a.rotation.w == b.rotation.w;
}
} // namespace

SceneTerrainPreparation::Result SceneTerrainPreparation::synchronize(scene::Scene& scene,
    const resource::ResourceManager& resources,
    RuntimeScene& runtime,
    const Desc& desc)
{
    return synchronizeWithGate(scene, resources, runtime, desc, nullptr, nullptr);
}

SceneTerrainPreparation::Result SceneTerrainPreparation::synchronizeWithGate(scene::Scene& scene,
    const resource::ResourceManager& resources,
    RuntimeScene& runtime,
    const Desc& desc,
    BeforePublishFn gate,
    void* context)
{
    Result result;
    if (!desc.maxObjects || !desc.maxTerrains || !desc.binding.valid()) {
        result.diagnostics = "TERRAIN-SCENE-PREPARATION-CONFIG";
        return result;
    }
    if (sceneId_.value() && sceneId_ != scene.sceneId()) {
        result.diagnostics = "TERRAIN-SCENE-CLEAR-REQUIRED";
        return result;
    }
    if (sceneId_.value() && mode_ != desc.binding.mode) {
        result.diagnostics = "TERRAIN-SCENE-MODE-CLEAR-REQUIRED";
        return result;
    }
    const auto snapshot = scene.extractTraversalSnapshot({desc.maxObjects});
    if (!snapshot.ok()) {
        result.diagnostics = "TERRAIN-SCENE-SNAPSHOT";
        return result;
    }
    std::size_t count = 0u;
    for (const auto& node : snapshot.nodes()) {
        if (!node.activeInHierarchy)
            continue;
        const auto* object = scene.findObject(node.objectId);
        for (const auto& component : object->components())
            if (component->enabled() && dynamic_cast<scene3d::TerrainComponent*>(component.get()))
                ++count;
    }
    if (count > desc.maxTerrains) {
        result.diagnostics = "TERRAIN-SCENE-BUDGET";
        return result;
    }

    struct Pending final {
        scene3d::TerrainComponent* component = nullptr;
        std::size_t entry = 0u;
        TerrainSceneBinding::Candidate binding;
    };

    SceneTerrainPreparation candidate;
    candidate.sceneId_ = scene.sceneId();
    candidate.mode_ = desc.binding.mode;
    candidate.entries_.reserve(count);
    core::pmr::Vector<Pending> pending{&core::defaultCpuMemoryResource()};
    pending.reserve(count);
    std::size_t registrations = 0u;
    for (const auto& node : snapshot.nodes()) {
        if (!node.activeInHierarchy)
            continue;
        auto* object = scene.findObject(node.objectId);
        for (const auto& component : object->components()) {
            auto* terrain = dynamic_cast<scene3d::TerrainComponent*>(component.get());
            if (!terrain || !terrain->enabled())
                continue;
            const auto found =
                std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
                    return entry.component == terrain->id();
                });
            const auto resource =
                resources.find<resource::TerrainAssetResource>(terrain->resourceId());
            if (found != entries_.end() && resource.ready()
                && found->resourceId == terrain->resourceId()
                && found->resourceGeneration == resource.generation()
                && sameTransform(found->transform, node.worldTransform)
                && (found->queryOnly
                    || (runtime.terrain(found->handle)
                        && terrain->desc().terrain.id == found->handle.id))) {
                auto& reused = candidate.entries_.emplace_back();
                reused.object = found->object;
                reused.component = found->component;
                reused.resourceId = found->resourceId;
                reused.resourceGeneration = found->resourceGeneration;
                reused.transform = found->transform;
                reused.handle = found->handle;
                reused.worldSurface = found->worldSurface;
                reused.queryOnly = found->queryOnly;
                ++result.reused;
                continue;
            }
            auto binding = desc.binding;
            binding.worldTransform = node.worldTransform;
            auto prepared = TerrainSceneBinding().prepare(*terrain, resources, binding);
            if (!prepared.result.success) {
                ++result.failed;
                if (!result.diagnostics.empty())
                    result.diagnostics += '\n';
                result.diagnostics += prepared.result.diagnostics;
                continue;
            }
            Entry entry;
            entry.object = node.objectId;
            entry.component = terrain->id();
            entry.resourceId = terrain->resourceId();
            entry.resourceGeneration = resource.generation();
            entry.transform = node.worldTransform;
            entry.worldSurface = prepared.result.worldSurface;
            entry.queryOnly = prepared.queryOnly;
            if (!prepared.queryOnly && !runtime.terrain(TerrainHandle{terrain->desc().terrain.id}))
                ++registrations;
            pending.push_back({terrain, candidate.entries_.size(), std::move(prepared)});
            candidate.entries_.push_back(std::move(entry));
            ++result.bound;
        }
    }
    if (result.failed) {
        result.bound = result.reused = 0u;
        return result;
    }
    if (!runtime.reserveTerrainRegistrations(registrations)) {
        result.bound = result.reused = 0u;
        result.diagnostics = "TERRAIN-SCENE-REGISTRATION-CAPACITY";
        return result;
    }
    if (gate && !gate(context, candidate, result.diagnostics)) {
        result.bound = result.reused = 0u;
        if (result.diagnostics.empty())
            result.diagnostics = "TERRAIN-SCENE-PUBLICATION-REJECTED";
        return result;
    }
    // From here to return: no allocation, callbacks, resource lookup or IO.
    for (auto& item : pending) {
        TerrainSceneBinding::publish(*item.component, runtime, std::move(item.binding));
        if (!candidate.entries_[item.entry].queryOnly)
            candidate.entries_[item.entry].handle =
                TerrainHandle{item.component->desc().terrain.id};
    }
    for (const auto& entry : entries_) {
        const auto retained = std::find_if(
            candidate.entries_.begin(), candidate.entries_.end(), [&](const auto& next) {
                return next.component == entry.component;
            });
        if (retained == candidate.entries_.end()) {
            release(scene, runtime, entry);
            ++result.released;
        }
    }
    entries_.swap(candidate.entries_);
    sceneId_ = candidate.sceneId_;
    mode_ = candidate.mode_;
    result.success = true;
    return result;
}

std::shared_ptr<const terrain::TerrainSurface> SceneTerrainPreparation::worldSurface(
    scene::ComponentId component) const
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry.component == component;
    });
    return found == entries_.end() ? nullptr : found->worldSurface;
}

void SceneTerrainPreparation::release(
    scene::Scene& scene, RuntimeScene& runtime, const Entry& entry) noexcept
{
    if (entry.queryOnly)
        return;
    runtime.unloadTerrain(entry.handle);
    if (auto* object = scene.findObject(entry.object))
        for (const auto& component : object->components()) {
            if (component->id() == entry.component)
                if (auto* terrain = dynamic_cast<scene3d::TerrainComponent*>(component.get()))
                    terrain->clearResolvedTerrain();
        }
}

void SceneTerrainPreparation::clear(scene::Scene& scene, RuntimeScene& runtime)
{
    if (sceneId_.value() && sceneId_ != scene.sceneId())
        return;
    for (const auto& entry : entries_)
        release(scene, runtime, entry);
    entries_.clear();
    sceneId_ = {};
}
} // namespace arp::renderer
