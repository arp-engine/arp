#pragma once

#include "core/memory/atomic_capacity.h"
#include "renderer/terrain_vegetation_draw_preparation.h"
#include "rhi/gpu_device.h"

namespace arp::renderer {

class TerrainVegetationGpuSnapshot final {
public:
    static_assert(sizeof(vegetation::InstanceDataGpu) == 64u);
    static constexpr std::uint32_t contractVersion = 1u;
    static constexpr std::uint32_t instanceStride = sizeof(vegetation::InstanceDataGpu);
    static constexpr const char* layoutIdentity = "TerrainVegetationWorldCull64/1";
    static constexpr auto positionSpace = TerrainVegetationDrawSnapshot::positionSpace;

    struct Prototype final {
        std::uint32_t prototypeId = 0u;
        std::uint32_t instanceCount = 0u;
        core::UniquePtr<rhi::GpuBuffer> buffer;
    };

    struct Tile final {
        terrain::TerrainTileCoord coord;
        core::pmr::Vector<Prototype> prototypes{&core::defaultCpuMemoryResource()};
    };

    const auto& source() const noexcept { return source_; }

    const auto& tiles() const noexcept { return tiles_; }

    bool validatorOnly() const noexcept { return validatorOnly_; }

    bool belongsTo(const rhi::GpuDevice& device) const noexcept { return device_ == &device; }

private:
    friend class TerrainVegetationGpuUpload;
    // Leases outlive buffers, including when external readers outlive the ticket.
    std::shared_ptr<core::AtomicCapacityLease> generationLease_, instanceLease_;
    std::shared_ptr<const TerrainVegetationDrawSnapshot> source_;
    core::pmr::Vector<Tile> tiles_{&core::defaultCpuMemoryResource()};
    bool validatorOnly_ = false;
    const rhi::GpuDevice* device_ = nullptr;
};

// TerrainVegetationGpuUpload/1. Serialized cold preparation; no draw authority.
// Device and ticket must outlive accepted uploads; completed readers must outlive
// their consuming submissions. See docs/terrain_vegetation_gpu_upload_v1.md.
class ARP_API TerrainVegetationGpuUpload final {
public:
    enum class State { Empty, Prepared, Rejected, AcceptedPending, AcceptedError, Completed };

    struct Desc final {
        std::uint32_t contractVersion = 1u;
        std::size_t maxTiles = 0u, maxInstances = 0u, maxPrototypes = 0u;
        std::uint64_t maxCopyBytes = 0u, maxCopyCommands = 0u, maxCpuBytes = 0u;
        std::shared_ptr<core::AtomicCapacityBudget> liveGenerations, liveInstances;
        bool validatorOptIn = false;
    };

    struct Decision final {
        // Device-fact admission only. prepare must additionally obtain the
        // standalone factory's exact completion before creating GPU buffers.
        rhi::CapabilityMode mode = rhi::CapabilityMode::DiagnosticFailure;
        bool executable = false, validatorOnly = false;
        std::uint64_t maxBufferBytes = 0u;
        const char* diagnostic = "VEGETATION-GPU-PROFILE-UNAVAILABLE";
    };

    struct Result final {
        bool success = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    TerrainVegetationGpuUpload() = default;
    ~TerrainVegetationGpuUpload();
    TerrainVegetationGpuUpload(const TerrainVegetationGpuUpload&) = delete;
    TerrainVegetationGpuUpload& operator=(const TerrainVegetationGpuUpload&) = delete;

    static Decision plan(const rhi::DeviceCapabilities&, bool validatorOptIn = false) noexcept;
    Result prepare(
        rhi::GpuDevice&, std::shared_ptr<const TerrainVegetationDrawSnapshot>, const Desc&);
    Result submit();
    // True only for a successfully accepted, completed upload (or no GPU work).
    bool poll();
    // Accepted-error completion permits release, never publication.
    bool cancel();
    // Performs a bounded drain on the exact device, not a caller-supplied report.
    Result drainAndClear(const rhi::semantics::ShutdownDrainRequest&);

    State state() const noexcept { return state_; }

    const Decision& decision() const noexcept { return decision_; }

    std::shared_ptr<const TerrainVegetationGpuSnapshot> snapshot() const noexcept
    {
        return state_ == State::Completed ? candidate_ : nullptr;
    }

private:
    friend class TerrainVegetationGpuPublication;
    Result prepareWithQuota(rhi::GpuDevice&,
        std::shared_ptr<const TerrainVegetationDrawSnapshot>,
        const Desc&,
        std::shared_ptr<core::CpuMemoryQuota>);
    bool outstanding() const noexcept;
    void reset() noexcept;

    State state_ = State::Empty;
    Decision decision_;
    rhi::GpuDevice* device_ = nullptr;
    std::shared_ptr<TerrainVegetationGpuSnapshot> candidate_;
    core::pmr::Vector<core::UniquePtr<rhi::GpuBuffer>> uploads_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<rhi::CommandBuffer> commands_;
    core::UniquePtr<rhi::GpuFence> completion_;
};

} // namespace arp::renderer
