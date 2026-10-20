#pragma once

#include "core/cpu_allocator.h"
#include "renderer/terrain_runtime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::assets {
struct MeshAsset;
}

namespace arp::renderer {

template <typename Tag> struct RuntimeHandle {
    uint32_t id = 0;

    bool valid() const { return id != 0; }

    bool operator==(RuntimeHandle other) const { return id == other.id; }
};

struct MeshHandleTag;
struct TextureHandleTag;
struct MaterialHandleTag;
struct LightHandleTag;
struct InstanceHandleTag;
struct TerrainHandleTag;

using MeshHandle = RuntimeHandle<MeshHandleTag>;
using TextureHandle = RuntimeHandle<TextureHandleTag>;
using MaterialHandle = RuntimeHandle<MaterialHandleTag>;
using LightHandle = RuntimeHandle<LightHandleTag>;
using InstanceHandle = RuntimeHandle<InstanceHandleTag>;
using TerrainHandle = RuntimeHandle<TerrainHandleTag>;

enum class LightKind { Directional, Point, Spot };

struct RuntimeMaterialDesc {
    std::pmr::string name;
};

/// CPU-side runtime asset binding used by scene extraction and renderer
/// adapters. Ownership remains with the asset/resource system; the runtime
/// scene only records the current resident address behind a stable handle.
struct RuntimeMeshAsset {
    std::pmr::string name;
    const arp::assets::MeshAsset* asset = nullptr;
    bool resident = false;

    RuntimeMeshAsset(const std::pmr::string& meshName, const arp::assets::MeshAsset* meshAsset)
        : name(meshName, meshName.get_allocator().resource()), asset(meshAsset),
          resident(meshAsset != nullptr)
    {}
};

struct RuntimeLightDesc {
    LightKind kind = LightKind::Directional;
    float intensity = 1.0f;
};

struct RuntimeRenderableDesc {
    MeshHandle mesh;
    MaterialHandle material;
    uint32_t transformIndex = 0;
};

struct RenderableBatch {
    MaterialHandle material;
    std::pmr::vector<InstanceHandle> instances;
};

class RuntimeScene {
public:
    /// Registers a CPU mesh without transferring ownership. Handles are never
    /// recycled, so an unloaded handle remains stale even if another mesh is
    /// registered later.
    MeshHandle registerMesh(
        const std::pmr::string& name, const arp::assets::MeshAsset* asset = nullptr)
    {
        meshes_.emplace_back(name, asset);
        return MeshHandle{static_cast<uint32_t>(meshes_.size())};
    }

    MeshHandle addMesh(const std::pmr::string& name) { return registerMesh(name); }

    /// Rebinds a resident mesh handle to a newly produced CPU asset. The
    /// renderer observes the new pointer on the next frame and owns any GPU
    /// buffer replacement/retirement.
    bool updateMesh(MeshHandle handle, const arp::assets::MeshAsset* asset)
    {
        auto* entry = mutableMesh(handle);
        if (entry == nullptr) {
            return false;
        }
        entry->asset = asset;
        entry->resident = asset != nullptr;
        return true;
    }

    /// Removes the CPU binding while keeping the slot reserved so stale
    /// handles cannot accidentally resolve to a different mesh.
    bool unloadMesh(MeshHandle handle) { return updateMesh(handle, nullptr); }

    const RuntimeMeshAsset* mesh(MeshHandle handle) const
    {
        if (!handle.valid() || handle.id > meshes_.size()) {
            return nullptr;
        }
        const auto& entry = meshes_[handle.id - 1];
        return entry.resident ? &entry : nullptr;
    }

    const std::pmr::vector<RuntimeMeshAsset>& meshes() const { return meshes_; }

    const arp::assets::MeshAsset* meshAsset(MeshHandle handle) const
    {
        const auto* entry = mesh(handle);
        return entry != nullptr ? entry->asset : nullptr;
    }

    TerrainHandle registerTerrain(std::shared_ptr<TerrainRuntime> runtime)
    {
        terrainRuntimes_.push_back(std::move(runtime));
        return TerrainHandle{static_cast<uint32_t>(terrainRuntimes_.size())};
    }

    bool updateTerrain(TerrainHandle handle, std::shared_ptr<TerrainRuntime> runtime)
    {
        auto* entry = mutableTerrain(handle);
        if (entry == nullptr)
            return false;
        *entry = std::move(runtime);
        return true;
    }

    bool unloadTerrain(TerrainHandle handle) { return updateTerrain(handle, nullptr); }

    const std::shared_ptr<TerrainRuntime>* terrain(TerrainHandle handle) const
    {
        if (!handle.valid() || handle.id > terrainRuntimes_.size()) {
            return nullptr;
        }
        const auto& entry = terrainRuntimes_[handle.id - 1];
        return entry != nullptr ? &entry : nullptr;
    }

    TextureHandle addTexture(const std::pmr::string& name)
    {
        textures_.push_back(name);
        return TextureHandle{static_cast<uint32_t>(textures_.size())};
    }

    MaterialHandle addMaterial(const std::pmr::string& name)
    {
        materials_.push_back(RuntimeMaterialDesc{name});
        return MaterialHandle{static_cast<uint32_t>(materials_.size())};
    }

    LightHandle addLight(RuntimeLightDesc desc)
    {
        lights_.push_back(desc);
        return LightHandle{static_cast<uint32_t>(lights_.size())};
    }

    InstanceHandle addInstance(RuntimeRenderableDesc desc)
    {
        instances_.push_back(desc);
        return InstanceHandle{static_cast<uint32_t>(instances_.size())};
    }

    RenderableBatch buildRenderableBatch(MaterialHandle material) const
    {
        RenderableBatch batch;
        batch.material = material;
        for (size_t index = 0; index < instances_.size(); ++index) {
            if (instances_[index].material == material) {
                batch.instances.push_back(InstanceHandle{static_cast<uint32_t>(index + 1)});
            }
        }
        return batch;
    }

    size_t meshCount() const { return meshes_.size(); }

    size_t textureCount() const { return textures_.size(); }

    size_t materialCount() const { return materials_.size(); }

    size_t lightCount() const { return lights_.size(); }

    size_t instanceCount() const { return instances_.size(); }

private:
    RuntimeMeshAsset* mutableMesh(MeshHandle handle)
    {
        if (!handle.valid() || handle.id > meshes_.size()) {
            return nullptr;
        }
        auto& entry = meshes_[handle.id - 1];
        return entry.resident ? &entry : nullptr;
    }

    std::shared_ptr<TerrainRuntime>* mutableTerrain(TerrainHandle handle)
    {
        if (!handle.valid() || handle.id > terrainRuntimes_.size()) {
            return nullptr;
        }
        auto& entry = terrainRuntimes_[handle.id - 1];
        return entry != nullptr ? &entry : nullptr;
    }

    std::pmr::vector<RuntimeMeshAsset> meshes_;
    std::pmr::vector<std::shared_ptr<TerrainRuntime>> terrainRuntimes_;
    std::pmr::vector<std::pmr::string> textures_;
    std::pmr::vector<RuntimeMaterialDesc> materials_;
    std::pmr::vector<RuntimeLightDesc> lights_;
    std::pmr::vector<RuntimeRenderableDesc> instances_;
};

} // namespace arp::renderer
