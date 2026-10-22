#pragma once

#include "3d/terrain/terrain_physics_binding.h"
#include "renderer/scene_terrain_preparation.h"
#include "renderer/terrain_vegetation_binding.h"
#include "renderer/terrain_vegetation_gpu_publication.h"

namespace arp::engine {

// Terrain scene integration; the namespace is retained for public API/ABI stability.
// CPU host orchestration. Resource/runtime/world and the attached scene must
// outlive this session; call clear before destroying an attached scene.
class ARP_API TerrainSceneSession final {
public:
    enum class FailureStage {
        None,
        Configuration,
        Preparation,
        VegetationBinding,
        PhysicsBinding,
        FixedUpdate,
        PhysicsStep,
        VegetationGpuBinding
    };

    struct Result final {
        bool success = false;
        bool fixedUpdateCompleted = false;
        bool stepped = false;
        FailureStage failureStage = FailureStage::None;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
        bool pending = false;
    };

    struct Desc final {
        std::size_t maxObjects = 0u;
        std::size_t maxTerrains = 0u;
        renderer::TerrainSceneBinding::Desc binding;
        renderer::TerrainVegetationBinding::Desc vegetation;
    };

    TerrainSceneSession(const resource::ResourceManager& resources,
        renderer::RuntimeScene& runtime,
        physics::PhysicsWorld& world);
    // Explicit instance-GPU participant; no draw authority. The device outlives
    // session and readers. Drain accepted work before destruction.
    TerrainSceneSession(const resource::ResourceManager& resources,
        renderer::RuntimeScene& runtime,
        physics::PhysicsWorld& world,
        rhi::GpuDevice& device,
        const renderer::TerrainVegetationGpuUpload::Desc& upload);
    ~TerrainSceneSession();
    TerrainSceneSession(const TerrainSceneSession&) = delete;
    TerrainSceneSession& operator=(const TerrainSceneSession&) = delete;

    Result synchronize(scene::Scene& scene, const Desc& desc);
    // Host dispatches callbacks and stops on failure. This boundary only
    // synchronizes authored state and steps physics; zero only synchronizes.
    Result hostStep(scene::Scene& scene, float deltaSeconds, const Desc& desc);
    Result fixedStep(
        scene::Scene& scene, const scene::SceneUpdateOptions& options, const Desc& desc);
    Result clear();
    Result drainAndClear(const rhi::semantics::ShutdownDrainRequest&);

    bool synchronized() const noexcept
    {
        return synchronized_
            && (preparation_.terrainCount() == 0u ? !world_.terrainHeightFieldHandle().valid()
                                                  : collider_.ready());
    }

    std::shared_ptr<const terrain::TerrainSurface> worldSurface() const
    {
        return synchronized() ? collider_.surface() : nullptr;
    }

    std::shared_ptr<const renderer::TerrainVegetationSnapshot> vegetationSnapshot() const
    {
        return synchronized() ? vegetation_.snapshot() : nullptr;
    }

    std::shared_ptr<const renderer::TerrainVegetationGpuSnapshot> vegetationGpuSnapshot() const
    {
        return synchronized() && gpu_ ? gpu_->snapshot() : nullptr;
    }

private:
    struct PublicationContext final {
        TerrainSceneSession& session;
        const Desc& desc;
        FailureStage failureStage = FailureStage::None;
        bool pending = false;
    };

    static bool publishConsumers(void* context,
        const renderer::SceneTerrainPreparation& candidate,
        core::pmr::String& diagnostics);

    const resource::ResourceManager& resources_;
    renderer::RuntimeScene& runtime_;
    physics::PhysicsWorld& world_;
    renderer::SceneTerrainPreparation preparation_;
    physics::TerrainPhysicsBinding collider_;
    renderer::TerrainVegetationBinding vegetation_;
    core::UniquePtr<renderer::TerrainVegetationGpuPublication> gpu_;
    scene::Scene* scene_ = nullptr;
    bool synchronized_ = false;
};
} // namespace arp::engine
