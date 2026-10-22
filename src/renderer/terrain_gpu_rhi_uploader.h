#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/runtime/gpu_resource_retire_queue.h"
#include "renderer/terrain_gpu_runtime.h"
#include "rhi/gpu_device.h"

#include <cstdint>
#include <memory>

namespace arp::renderer {

/// Backend-visible outcome for terrain page payload creation. Null validates
/// the complete L2 upload sequence but is never a visual backend claim.
enum class TerrainGpuPageBackendStatus : std::uint8_t {
    Uninitialized,
    NullValidation,
    Native,
    Degraded,
    DiagnosticFailure,
};

struct TerrainGpuRhiUploaderDesc final {
    std::uint32_t samplesPerEdge = 0u;
    std::uint32_t maxTilePages = 0u;
    std::uint32_t maxPageTableSnapshots = 0u;
    core::pmr::String label{"TerrainAssetPages", &core::defaultCpuMemoryResource()};

    bool valid() const noexcept
    {
        return samplesPerEdge >= 2u && maxTilePages > 0u && maxPageTableSnapshots >= 2u;
    }
};

struct TerrainGpuRhiUploaderResult final {
    bool success = false;
    TerrainGpuPageBackendStatus status = TerrainGpuPageBackendStatus::Uninitialized;
    TerrainGpuTileBackendPayload pages;
    std::uint64_t pageTableGeneration = 0u;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

/// Renderer-owned RHI lowerer for TerrainAsset texture-array pages. It owns
/// only RHI objects; scene, PCG and resource systems never receive these
/// handles. The caller supplies the selected array layer from its streaming
/// residency policy, so capacity and eviction remain explicit.
class TerrainGpuRhiUploader final {
public:
    struct PageTableSnapshot final {
        std::uint64_t generation = 0u;
        const rhi::GpuBuffer* entries = nullptr;
        const rhi::GpuBuffer* params = nullptr;

        bool valid() const noexcept
        {
            return generation != 0u && entries != nullptr && params != nullptr;
        }
    };

    TerrainGpuRhiUploader() = default;

    TerrainGpuRhiUploader(const TerrainGpuRhiUploader&) = delete;
    TerrainGpuRhiUploader& operator=(const TerrainGpuRhiUploader&) = delete;

    TerrainGpuRhiUploaderResult create(
        rhi::GpuDevice& device, const TerrainGpuRhiUploaderDesc& desc);

    /// Upload a fully validated tile to one explicit texture-array layer.
    /// The operation creates no placeholder data and publishes no page
    /// identity until all three texture uploads have been submitted.
    TerrainGpuRhiUploaderResult upload(const TerrainGpuTilePayload& tile,
        std::uint32_t arrayLayer,
        std::uint64_t submissionSerial);

    /// Writes a complete active page-table snapshot and its explicit count.
    /// All entries must refer to this uploader's page arrays. Generation is
    /// preserved as two shader-visible u32 lanes; no resource generation is
    /// truncated merely to satisfy the page-table ABI.
    TerrainGpuRhiUploaderResult writePageTable(
        const core::pmr::Vector<TerrainGpuPageTableEntry>& entries, std::uint64_t submissionSerial);

    bool setRetirementCapacityBudget(std::shared_ptr<core::AtomicCapacityBudget> budget) noexcept
    {
        return retiredPageTables_.setCapacityBudget(std::move(budget));
    }

    /// Releases immutable page-table generations only after the strict
    /// completion boundary. Descriptors captured from an older snapshot stay
    /// valid until this method reports their submission complete.
    std::size_t reapCompletedPageTableSnapshots(std::uint64_t completedSubmissionSerial)
    {
        return retiredPageTables_.reapCompleted(completedSubmissionSerial);
    }

    std::size_t retiredPageTableSnapshotCount() const noexcept { return retiredPageTables_.size(); }

    core::runtime::GpuResourceRetireQueueStats retirementStats() const noexcept
    {
        return retiredPageTables_.stats();
    }

    /// A planned publication replaces exactly one active immutable snapshot,
    /// if present. Callers use this neutral demand before starting a
    /// multi-owner residency transaction so every shared retirement lease is
    /// admitted before published state can change.
    std::size_t retirementDemandForPageTablePublication() const noexcept
    {
        return activePageTable_.generation == 0u ? 0u : 1u;
    }

    void clear();

    bool ready() const noexcept { return device_ != nullptr; }

    TerrainGpuPageBackendStatus backendStatus() const noexcept { return status_; }

    const rhi::GpuTextureView* heightPagesView() const noexcept { return heightPagesView_.get(); }

    const rhi::GpuTextureView* weightPageAView() const noexcept { return weightPageAView_.get(); }

    const rhi::GpuTextureView* weightPageBView() const noexcept { return weightPageBView_.get(); }

    PageTableSnapshot pageTableSnapshot() const noexcept;

    const rhi::GpuBuffer* pageTableBuffer() const noexcept
    {
        return activePageTable_.entries.get();
    }

    const rhi::GpuBuffer* pageTableParamsBuffer() const noexcept
    {
        return activePageTable_.params.get();
    }

    std::uint32_t maxTilePages() const noexcept { return desc_.maxTilePages; }

private:
    TerrainGpuRhiUploaderResult failure(
        TerrainGpuPageBackendStatus status, const char* message) const;
    bool backendCanExecute(core::pmr::String& diagnostics) const;

    struct OwnedPageTableSnapshot final {
        std::uint64_t generation = 0u;
        core::UniquePtr<rhi::GpuBuffer> entries;
        core::UniquePtr<rhi::GpuBuffer> params;
    };

    rhi::GpuDevice* device_ = nullptr;
    TerrainGpuRhiUploaderDesc desc_;
    TerrainGpuPageBackendStatus status_ = TerrainGpuPageBackendStatus::Uninitialized;
    core::UniquePtr<rhi::GpuTexture> heightPages_;
    core::UniquePtr<rhi::GpuTexture> weightPageA_;
    core::UniquePtr<rhi::GpuTexture> weightPageB_;
    core::UniquePtr<rhi::GpuTextureView> heightPagesView_;
    core::UniquePtr<rhi::GpuTextureView> weightPageAView_;
    core::UniquePtr<rhi::GpuTextureView> weightPageBView_;
    OwnedPageTableSnapshot activePageTable_;
    core::runtime::GpuResourceRetireQueue<OwnedPageTableSnapshot> retiredPageTables_;
    std::uint64_t nextPageTableGeneration_ = 1u;
};

} // namespace arp::renderer
