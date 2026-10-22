#pragma once

#include "renderer/render_graph/render_graph.h"
#include "renderer/terrain_vegetation_gpu_upload.h"

namespace arp::renderer {

// One consuming graph execution. Instance-side ownership only; not a complete
// geometry/material/descriptor draw closure. See TerrainVegetationSubmission/1.
class ARP_API TerrainVegetationSubmission final : public render_graph::PassSubmissionObserver {
public:
    enum class State { Empty, Prepared, Attempted, Accepted, Completed, Cancelled };

    struct Desc final {
        std::uint32_t contractVersion = 1u;
        std::uint64_t maxCpuBytes = 0u;
        std::shared_ptr<core::AtomicCapacityBudget> liveSubmissions;
        bool validatorOptIn = false;
    };

    struct Result final {
        bool success = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    explicit TerrainVegetationSubmission(rhi::GpuDevice& device) : device_(device) {}

    ~TerrainVegetationSubmission() override;
    TerrainVegetationSubmission(const TerrainVegetationSubmission&) = delete;
    TerrainVegetationSubmission& operator=(const TerrainVegetationSubmission&) = delete;

    Result prepare(std::shared_ptr<const TerrainVegetationGpuSnapshot>, const Desc&);
    render_graph::PassSubmissionToken token() const noexcept;
    const char* beginUse(render_graph::RenderContext&, render_graph::PassSubmissionToken) noexcept;
    const char* validateSubmission(render_graph::PassSubmissionToken) const noexcept override;
    void onSubmitted(render_graph::PassSubmissionToken,
        const std::shared_ptr<const rhi::GpuFence>&) noexcept override;
    bool poll() noexcept;
    bool cancel() noexcept;
    Result drainAndClear(const rhi::semantics::ShutdownDrainRequest&);

    State state() const noexcept { return state_; }

    const auto& snapshot() const noexcept { return snapshot_; }

private:
    bool matches(render_graph::PassSubmissionToken) const noexcept;
    void release(State) noexcept;

    rhi::GpuDevice& device_;
    State state_ = State::Empty;
    std::uint64_t recordingIdentity_ = 0u;
    std::shared_ptr<core::AtomicCapacityLease> submissionLease_;
    std::shared_ptr<const TerrainVegetationGpuSnapshot> snapshot_;
    std::shared_ptr<const rhi::GpuFence> completion_;
};

} // namespace arp::renderer
