#pragma once

#include "3d/bvh.h"
#include "3d/lod/lod_profile.h"
#include "3d/lod/lod_transition.h"
#include "assets/mesh_section_grouping.h"
#include "core/math/frustum.h"
#include "core/runtime/work_service.h"
#include "renderer/foliage_deformation_envelope.h"
#include "renderer/mesh/immutable_mesh_upload.h"
#include "renderer/runtime/gpu_resource_retire_queue.h"
#include "renderer/terrain_vegetation_runtime.h"
#include "rhi/gpu_device.h"

#include <filesystem>

namespace arp::renderer {

// Resolve the explicit, hash-verified material/static mesh resource declared
// by VegetationScene/v1. Game cooks may retain the authoring bank separately.
bool resolveVegetationStaticScene(const std::filesystem::path& manifest,
    std::filesystem::path& staticScene,
    core::pmr::String& diagnostics);
bool resolveVegetationStaticScene(const std::filesystem::path& manifest,
    std::filesystem::path& staticScene,
    assets::MeshSectionSpatialPartition& partition,
    core::pmr::String& diagnostics);

// VegetationScene/v1 is independent of terrain tile resolution. Prototype
// geometry is shared; only asynchronous placement pages become resident.
class VegetationSceneRuntime final {
public:
    struct Section {
        std::uint32_t firstIndex = 0, indexCount = 0, materialSlot = 0, sourceMaterialSlot = 0;
    };

    struct SceneCluster {
        std::uint32_t sectionIndex = 0, firstIndex = 0, indexCount = 0;
        scene3d::Aabb bounds;
    };

    struct Model {
        core::UniquePtr<ImmutableMeshUpload> immutableUpload;
        core::UniquePtr<rhi::GpuBuffer> vertices, indices;
        core::pmr::Vector<Section> sections{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<SceneCluster> sceneClusters{&core::defaultCpuMemoryResource()};
        scene3d::Aabb bounds;
    };

    struct Prototype {
        FoliageDeformationEnvelope deformationEnvelope;
        std::uint32_t id = 0;
        bool castsShadow = false;
        lod::LodProfile profile;
        core::ScreenDiameterLod screenProfile;
        std::array<std::uint32_t, 4> shadowLods{};
        core::pmr::Vector<Model> models{&core::defaultCpuMemoryResource()};
    };

    struct Group {
        std::uint32_t prototypeIndex = 0, lod = 0;
        bool sceneVisible = true;
        std::uint64_t stableId = 0, createdSerial = 0;
        scene3d::Aabb bounds;
        core::pmr::Vector<vegetation::InstanceDataGpu> instances{&core::defaultCpuMemoryResource()};
        core::UniquePtr<rhi::GpuBuffer> buffer;
    };

    struct Telemetry {
        std::size_t residentChunks = 0, residentInstances = 0, pendingChunks = 0;
        std::size_t loaded = 0, unloaded = 0, bufferReplacements = 0;
        std::size_t visibleInstances = 0, frustumCulled = 0, distanceCulled = 0;
        std::size_t lodInstances[4]{};
        std::size_t transitions = 0, starts = 0;
        std::uint64_t extraTriangles = 0;
    };

    VegetationSceneRuntime() = default;
    ~VegetationSceneRuntime();
    bool initialize(const std::filesystem::path& manifest,
        rhi::GpuDevice& device,
        core::pmr::String& diagnostics);

    // All limits are aggregate for this scene, including copy command count.
    // Model count additionally bounds ticket/command/fence ownership. Source
    // generation is supplied by the caller's hash-verified resource identity.
    struct PrototypeResidencyBudget {
        ImmutableMeshUpload::Budget total;
        std::uint32_t maxModels = 0;
        std::uint64_t sourceGeneration = 0;
    };

    struct PrototypeUploadStats {
        std::uint64_t destinationBytes = 0, metadataBytes = 0;
        std::uint32_t models = 0, copyCommands = 0;
    };

    bool initialize(const std::filesystem::path& manifest,
        rhi::GpuDevice& device,
        core::pmr::String& diagnostics,
        const PrototypeResidencyBudget& budget);
    // Cold operation, once, after ALL models and consumer bindings validate.
    // A partial submission failure retains all tickets until drain/loss.
    bool submitPrototypeUploads(core::pmr::String& diagnostics);
    // Render-owner observations; no IO, wait, or allocation. Every consuming
    // pipeline must gate on this predicate, not merely ready().
    bool prototypeGeometryReady() const;
    std::size_t reapPrototypeUploads();
    bool prototypeUploadsRequireDrain() const noexcept;
    std::uint64_t retainedPrototypeUploadBytes() const noexcept;

    const PrototypeUploadStats& prototypeUploadStats() const noexcept
    {
        return prototypeUploadStats_;
    }

    bool clearAfterDeviceDrain(const rhi::semantics::ShutdownDrainReport& report);
    void clearAfterDeviceLoss();
    bool update(rhi::GpuDevice& device,
        core::Vec3 camera,
        const core::Frustum& frustum,
        std::uint64_t serial,
        core::pmr::String& diagnostics);

    struct View {
        float projectionY = 1.7320508f;
        core::Vec3 forward{0, 0, -1};
        float deltaSeconds = 1.f / 60.f;
        float absoluteSeconds = -1;
    };

    bool update(rhi::GpuDevice& device,
        core::Vec3 camera,
        const core::Frustum& frustum,
        std::uint64_t serial,
        core::pmr::String& diagnostics,
        const View& view);
    // Only after GPU drain; also joins the IO worker. Accepted prototype upload
    // ownership additionally requires explicit clearAfterDeviceDrain/Loss.
    void clear();

    bool ready() const noexcept { return ready_; }

    const auto& prototypes() const noexcept { return prototypes_; }

    const Telemetry& telemetry() const noexcept { return telemetry_; }

    template <class Fn> void forEachGroup(Fn&& fn) const
    {
        for (const auto& chunk : chunks_)
            for (const auto& group : chunk.groups)
                fn(group);
    }

    bool setRetirementCapacityBudget(std::shared_ptr<core::AtomicCapacityBudget> budget)
    {
        return retired_.setCapacityBudget(std::move(budget));
    }

    std::size_t reapCompleted(std::uint64_t serial) { return retired_.reapCompleted(serial); }

    std::size_t reapAll() { return retired_.reapAll(); }

    auto retirementStats() const { return retired_.stats(); }

private:
    bool initializeImpl(const std::filesystem::path& manifest,
        rhi::GpuDevice& device,
        core::pmr::String& diagnostics,
        const PrototypeResidencyBudget* budget);
    enum class PageState { Unloaded, Loading, Loaded, Failed };

    struct Placement {
        vegetation::InstanceDataGpu gpu;
        std::uint32_t prototypeIndex = 0;
        scene3d::Aabb bounds;
        float screenDiameter = 0;
        int lod = -1;
        core::LodTransition transition;
        std::uint64_t transitionCost = 0;
        bool distanceVisible = true;
    };

    struct Chunk {
        VegetationSceneRuntime* owner = nullptr;
        int x = 0, z = 0;
        std::uint32_t count = 0;
        std::filesystem::path path;
        core::pmr::String sha256{&core::defaultCpuMemoryResource()};
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Placement> placements{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Group> groups{&core::defaultCpuMemoryResource()};
        PageState state = PageState::Unloaded;
        bool workerSuccess = false;
    };

    static core::runtime::WorkStatus readPage(void* context);
    static void pageCompleted(void* context, core::runtime::WorkResult result);
    bool refreshGroups(Chunk& chunk,
        rhi::GpuDevice& device,
        core::Vec3 camera,
        const core::Frustum& frustum,
        std::uint64_t serial,
        core::pmr::String& diagnostics);
    core::pmr::Vector<Prototype> prototypes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Chunk> chunks_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<core::runtime::CompletionQueue> completions_;
    core::UniquePtr<core::runtime::WorkService> work_;
    core::runtime::GpuResourceRetireQueue<core::pmr::Vector<Group>> retired_;
    float chunkSize_ = 0, loadRadius_ = 0, unloadRadius_ = 0;
    std::size_t requestsPerFrame_ = 0, uploadsPerFrame_ = 0, maxResidentChunks_ = 0;
    std::size_t refreshCursor_ = 0;
    Telemetry telemetry_;
    View view_;
    float time_ = 0, transitionSeconds_ = 0;
    core::LodTransitionBudget transitionBudget_;
    bool ready_ = false;
    bool prototypeResidencyEnabled_ = false, prototypeSubmissionAttempted_ = false;
    bool prototypeSubmissionSucceeded_ = false;
    PrototypeUploadStats prototypeUploadStats_;
};
} // namespace arp::renderer
