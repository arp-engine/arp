#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/strided_image_statistics.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace arp::rhi {

class CommandEncoder;
class GpuTexture;

/// TextureReadbackBudget/1: explicit diagnostic workload limits. maxBytes
/// covers all pending staging buffers plus their CPU read copies; maxPending
/// includes completed-but-unpolled requests. Zero disables new scheduling.
struct TextureReadbackBudget {
    std::uint64_t maxBytes = 0;
    std::uint32_t maxPending = 0;
};

// Scalar, backend-owned accounting for TextureReadbackBudget/1. `chargedBytes`
// covers the pending native staging buffer plus its CPU copy as declared by
// the extension; it can therefore overlap the device resource-allocation
// ledger and must never be blindly added to it.
struct TextureReadbackBudgetStats {
    bool configured = false;
    std::uint64_t capacityBytes = 0;
    std::uint64_t chargedBytes = 0;
    std::uint64_t peakChargedBytes = 0;
    std::uint64_t budgetRejectionCount = 0;
    std::uint32_t pendingRequestCount = 0;
    std::uint32_t maxPendingRequestCount = 0;
};

/// TextureReadbackIdentity/1: caller-owned correlation, echoed unchanged even
/// on GPU failure/abandonment. A resource generation names the immutable source
/// snapshot (for frame-local graph textures, the graph's frame serial).
/// Zero fields are invalid. Identities must be unique within a pending batch.
/// TextureReadbackBatchIsolation/1 assigns runtime-unique opaque request IDs
/// across batches; consumers must not interpret requestId as an entry index.
struct TextureReadbackIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t frameSerial = 0;
    std::uint64_t resourceGeneration = 0;
};

inline bool operator==(const TextureReadbackIdentity& a, const TextureReadbackIdentity& b)
{
    return a.requestId == b.requestId && a.frameSerial == b.frameSerial
        && a.resourceGeneration == b.resourceGeneration;
}

enum class TextureReadbackCompletion { Unavailable, Completed, Failed, Abandoned };

/// Sanitized asynchronous GPU texture readback.  Backends may expose this
/// optional extension when they can map a backend-owned staging buffer; no
/// native resource handle crosses this interface.  For Rgba8/Bgra8 textures,
/// the numeric statistics use the RHI canonical Rec.709 luminance and are
/// independent of storage byte order.  Scalar and floating-point formats keep
/// their single-channel interpretation.
struct TextureReadbackSample {
    std::string label;
    std::string format;
    std::string diagnostic;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double minValue = 0.0;
    double maxValue = 0.0;
    double meanValue = 0.0;
    double centerValue = 0.0;
    // Row-major coordinates of the first finite extrema. These are diagnostic
    // only; they do not expose backend resources or affect acceptance.
    std::uint32_t minX = 0;
    std::uint32_t minY = 0;
    std::uint32_t maxX = 0;
    std::uint32_t maxY = 0;
    // FNV-1a over unpadded row-major pixel bytes for deterministic comparison.
    std::uint64_t sampleHash = 0u;
    std::uint32_t finiteCount = 0;
    std::uint32_t sampleCount = 0;
    bool valid = false;
    /// TextureReadbackChannels/1 adds raw numerical channels without changing
    /// the legacy scalar/luminance fields above. R/RG/RGBA order (BGRA storage
    /// is reordered); sRGB channel statistics are encoded normalized values,
    /// not linear luminance. Depth has one channel. Padding is excluded.
    /// Version zero means unavailable; it must not satisfy all-channel gates.
    /// Metal bounded Legacy and WebGPU owned snapshots share the canonical
    /// decoder. Borrowed Vulkan/WebGPU execution is retired by
    /// BorrowedTextureReadbackRetirement/1; Null has no native readback proof.
    std::uint32_t channelStatisticsVersion = 0;
    core::ImageChannelStatistics channelStatistics;
    TextureReadbackIdentity identity;
    GpuResourceId source;
    TextureFormat textureFormat = TextureFormat::Unknown;
    TextureReadbackCompletion completion = TextureReadbackCompletion::Unavailable;
};

class ARP_API TextureReadbackExtension {
public:
    virtual ~TextureReadbackExtension() = default;

    /// BorrowedTextureReadbackRetirement/1: Vulkan/WebGPU keep this ABI
    /// discoverable but reject schedule with a stable retired diagnostic and
    /// preserve poll output. Metal requires its explicit bounded Legacy budget.
    /// This interface never infers owned-source preparation or full graph quotas.
    /// Opt-in bounded mode. Reconfiguration with pending work must be rejected.
    /// Unsupported backends fail closed; the legacy interface does not imply
    /// this contract. Metal implements it with native completion polling;
    /// Null/Vulkan/WebGPU must decline until their bounded mode is implemented.
    virtual bool configureBudget(TextureReadbackBudget, core::pmr::String& diagnostic)
    {
        diagnostic = "RHI-READBACK-BOUNDED-MODE-UNSUPPORTED";
        return false;
    }

    /// Returns a scalar snapshot only. Unsupported extensions return the
    /// default `configured=false` record rather than pretending their budget
    /// is zero.
    virtual TextureReadbackBudgetStats budgetStats() const { return {}; }

    virtual bool schedule(CommandEncoder& commands,
        const GpuTexture& texture,
        std::string_view label,
        std::string& diagnostic) = 0;
    virtual bool poll(TextureReadbackSample& sample) = 0;

    /// Explicit opt-in identity + completion contract; never infer support
    /// from backend identity or the existence of the legacy extension.
    /// Metal: native completion-backed tracked requests. Null: no extension.
    /// Vulkan/WebGPU: DiagnosticFailure until bounded per-device ownership and
    /// identity payloads are implemented; no legacy/untagged fallback allowed.
    virtual bool supportsTrackedRequests() const { return false; }

    virtual bool scheduleTracked(CommandEncoder&,
        const GpuTexture&,
        std::string_view,
        TextureReadbackIdentity,
        core::pmr::String& diagnostic)
    {
        diagnostic = "RHI-READBACK-TRACKED-MODE-UNSUPPORTED";
        return false;
    }
};

/// Extension IDs for GpuDevice::queryInterface().
/// Each backend may implement zero or more optional extension interfaces.
/// Core rendering paths must not depend on extensions; query capabilities()
/// for the guaranteed baseline.

inline constexpr std::string_view kExtensionMultiQueue = "arp.rhi.MultiQueue";
inline constexpr std::string_view kExtensionBindlessCross = "arp.rhi.BindlessCross";
inline constexpr std::string_view kExtensionTimelineSync = "arp.rhi.Timeline";
inline constexpr std::string_view kExtensionTextureReadback = "arp.rhi.TextureReadback";

} // namespace arp::rhi
