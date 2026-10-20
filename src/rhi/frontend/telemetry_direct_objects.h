#pragma once

#include "rhi/gpu_common.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arp::rhi::semantics {

// DirectObjectTelemetry/1 is an observation-only L2 contract for objects
// created directly through GpuDevice rather than through an ARP cache. It is
// deliberately separate from PhysicalBudgetLedger: an observed peak is not a
// workload-owned capacity and must not be presented as one.
inline constexpr std::uint32_t kDirectObjectTelemetryContractVersion = 1u;

enum class DirectObjectTelemetryState : std::uint8_t {
    Unsupported,
    Observed,
    DiagnosticFailure,
};

enum class DirectObjectKind : std::uint8_t {
    PipelineLayout,
    RenderPipeline,
    ComputePipeline,
    DescriptorArena,
    Count,
};

struct DirectObjectTelemetryCounters {
    std::uint64_t liveObjects = 0u;
    std::uint64_t peakObjects = 0u;
    std::uint64_t createdObjects = 0u;
    // Descriptor arenas are charged by their declared maxDescriptors. Other
    // kinds always use one object unit and leave these fields at zero.
    std::uint64_t liveDescriptorCapacity = 0u;
    std::uint64_t peakDescriptorCapacity = 0u;
};

struct DirectObjectTelemetryReport {
    std::uint32_t contractVersion = kDirectObjectTelemetryContractVersion;
    rhi::Backend backend = rhi::Backend::Null;
    DirectObjectTelemetryState state = DirectObjectTelemetryState::Unsupported;
    DirectObjectTelemetryCounters pipelineLayouts;
    DirectObjectTelemetryCounters renderPipelines;
    DirectObjectTelemetryCounters computePipelines;
    DirectObjectTelemetryCounters descriptorArenas;
    const char* diagnosticId = "ARP-RHI-DIRECT-OBJECT-TELEMETRY-UNSUPPORTED";

    bool observed() const noexcept { return state == DirectObjectTelemetryState::Observed; }

    bool valid() const noexcept
    {
        if (contractVersion != kDirectObjectTelemetryContractVersion)
            return false;
        if (state == DirectObjectTelemetryState::Observed)
            return diagnosticId == nullptr;
        return diagnosticId != nullptr;
    }
};

class DirectObjectTelemetryTracker;

// The lease is held by the concrete L2 object. This makes telemetry follow
// real object ownership instead of the returning factory call.
class DirectObjectTelemetryLease final {
public:
    DirectObjectTelemetryLease() noexcept = default;
    DirectObjectTelemetryLease(std::shared_ptr<DirectObjectTelemetryTracker> tracker,
        DirectObjectKind kind,
        std::uint64_t descriptorCapacity) noexcept;
    ~DirectObjectTelemetryLease();

    DirectObjectTelemetryLease(const DirectObjectTelemetryLease&) = delete;
    DirectObjectTelemetryLease& operator=(const DirectObjectTelemetryLease&) = delete;
    DirectObjectTelemetryLease(DirectObjectTelemetryLease&& other) noexcept;
    DirectObjectTelemetryLease& operator=(DirectObjectTelemetryLease&& other) noexcept;

private:
    void release() noexcept;

    std::shared_ptr<DirectObjectTelemetryTracker> tracker_;
    DirectObjectKind kind_ = DirectObjectKind::Count;
    std::uint64_t descriptorCapacity_ = 0u;
};

class DirectObjectTelemetryTracker final
    : public std::enable_shared_from_this<DirectObjectTelemetryTracker> {
public:
    DirectObjectTelemetryLease acquire(
        DirectObjectKind kind, std::uint64_t descriptorCapacity = 0u);
    DirectObjectTelemetryReport report(rhi::Backend backend) const noexcept;

private:
    friend class DirectObjectTelemetryLease;

    struct Counter {
        std::atomic<std::uint64_t> liveObjects{0u};
        std::atomic<std::uint64_t> peakObjects{0u};
        std::atomic<std::uint64_t> createdObjects{0u};
        std::atomic<std::uint64_t> liveDescriptorCapacity{0u};
        std::atomic<std::uint64_t> peakDescriptorCapacity{0u};
    };

    static constexpr std::size_t index(DirectObjectKind kind) noexcept
    {
        return static_cast<std::size_t>(kind);
    }

    static void updatePeak(std::atomic<std::uint64_t>& peak, std::uint64_t observed) noexcept;
    void release(DirectObjectKind kind, std::uint64_t descriptorCapacity) noexcept;

    static DirectObjectTelemetryCounters countersFor(const Counter& counter) noexcept;

    Counter counters_[static_cast<std::size_t>(DirectObjectKind::Count)]{};
};

inline void DirectObjectTelemetryTracker::updatePeak(
    std::atomic<std::uint64_t>& peak, const std::uint64_t observed) noexcept
{
    auto current = peak.load(std::memory_order_relaxed);
    while (current < observed
        && !peak.compare_exchange_weak(
            current, observed, std::memory_order_release, std::memory_order_relaxed)) {
    }
}

inline DirectObjectTelemetryLease DirectObjectTelemetryTracker::acquire(
    const DirectObjectKind kind, const std::uint64_t descriptorCapacity)
{
    if (kind == DirectObjectKind::Count
        || (kind == DirectObjectKind::DescriptorArena && descriptorCapacity == 0u)) {
        return {};
    }
    auto& counter = counters_[index(kind)];
    const auto live = counter.liveObjects.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    counter.createdObjects.fetch_add(1u, std::memory_order_relaxed);
    updatePeak(counter.peakObjects, live);
    if (descriptorCapacity != 0u) {
        const auto capacity =
            counter.liveDescriptorCapacity.fetch_add(descriptorCapacity, std::memory_order_acq_rel)
            + descriptorCapacity;
        updatePeak(counter.peakDescriptorCapacity, capacity);
    }
    return DirectObjectTelemetryLease(shared_from_this(), kind, descriptorCapacity);
}

inline void DirectObjectTelemetryTracker::release(
    const DirectObjectKind kind, const std::uint64_t descriptorCapacity) noexcept
{
    auto& counter = counters_[index(kind)];
    counter.liveObjects.fetch_sub(1u, std::memory_order_acq_rel);
    if (descriptorCapacity != 0u) {
        counter.liveDescriptorCapacity.fetch_sub(descriptorCapacity, std::memory_order_acq_rel);
    }
}

inline DirectObjectTelemetryCounters DirectObjectTelemetryTracker::countersFor(
    const DirectObjectTelemetryTracker::Counter& counter) noexcept
{
    return {
        counter.liveObjects.load(std::memory_order_acquire),
        counter.peakObjects.load(std::memory_order_acquire),
        counter.createdObjects.load(std::memory_order_acquire),
        counter.liveDescriptorCapacity.load(std::memory_order_acquire),
        counter.peakDescriptorCapacity.load(std::memory_order_acquire),
    };
}

inline DirectObjectTelemetryReport DirectObjectTelemetryTracker::report(
    const rhi::Backend backend) const noexcept
{
    DirectObjectTelemetryReport result;
    result.backend = backend;
    result.state = DirectObjectTelemetryState::Observed;
    result.pipelineLayouts = countersFor(counters_[index(DirectObjectKind::PipelineLayout)]);
    result.renderPipelines = countersFor(counters_[index(DirectObjectKind::RenderPipeline)]);
    result.computePipelines = countersFor(counters_[index(DirectObjectKind::ComputePipeline)]);
    result.descriptorArenas = countersFor(counters_[index(DirectObjectKind::DescriptorArena)]);
    result.diagnosticId = nullptr;
    return result;
}

inline DirectObjectTelemetryLease::DirectObjectTelemetryLease(
    std::shared_ptr<DirectObjectTelemetryTracker> tracker,
    const DirectObjectKind kind,
    const std::uint64_t descriptorCapacity) noexcept
    : tracker_(std::move(tracker)), kind_(kind), descriptorCapacity_(descriptorCapacity)
{}

inline DirectObjectTelemetryLease::~DirectObjectTelemetryLease()
{
    release();
}

inline DirectObjectTelemetryLease::DirectObjectTelemetryLease(
    DirectObjectTelemetryLease&& other) noexcept
    : tracker_(std::move(other.tracker_)), kind_(other.kind_),
      descriptorCapacity_(other.descriptorCapacity_)
{
    other.kind_ = DirectObjectKind::Count;
    other.descriptorCapacity_ = 0u;
}

inline DirectObjectTelemetryLease& DirectObjectTelemetryLease::operator=(
    DirectObjectTelemetryLease&& other) noexcept
{
    if (this != &other) {
        release();
        tracker_ = std::move(other.tracker_);
        kind_ = other.kind_;
        descriptorCapacity_ = other.descriptorCapacity_;
        other.kind_ = DirectObjectKind::Count;
        other.descriptorCapacity_ = 0u;
    }
    return *this;
}

inline void DirectObjectTelemetryLease::release() noexcept
{
    if (tracker_ && kind_ != DirectObjectKind::Count) {
        tracker_->release(kind_, descriptorCapacity_);
    }
    tracker_.reset();
    kind_ = DirectObjectKind::Count;
    descriptorCapacity_ = 0u;
}

} // namespace arp::rhi::semantics
