#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace arp::rhi {

class GpuFence;

// Presentation policy requested by the client. Backends must report a clear
// fallback when the requested mode is unavailable on the active surface.
enum class PresentationMode : std::uint8_t {
    Auto,       // Prefer low-latency mailbox, then immediate, then FIFO.
    VSync,      // Tear-free FIFO pacing.
    LowLatency, // Mailbox when available, otherwise immediate.
    Immediate,  // Request unsynchronised presentation.
};

struct SwapchainDesc {
    std::pmr::string surfaceName;
    Extent2D extent;
    TextureFormat format = TextureFormat::Unknown;
    std::uint32_t imageCount = 2;
    PresentationMode presentationMode = PresentationMode::Auto;
    /// Opaque platform-owned surface token. The RHI may consume it during
    /// swapchain creation but must not expose or reinterpret it above the
    /// backend boundary. Null/Vulkan/WebGPU ignore this field.
    std::shared_ptr<void> platformSurface;
};

enum class AcquireStatus {
    Unknown,
    Acquired,
    Suboptimal,
    OutOfDate,
    SurfaceLost,
    Failed,
    NotReady
};

enum class PresentStatus { Unknown, Presented, Suboptimal, OutOfDate, SurfaceLost, Failed };

struct AcquiredFrame {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint64_t frameSerial = 0;
    std::uint32_t frameIndex = 0;
    AcquireStatus status = AcquireStatus::Unknown;
    bool recoverable = false;
    bool swapchainRebuildRequired = false;
    bool surfaceLost = false;
};

struct PresentResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint64_t frameSerial = 0;
    std::uint32_t frameIndex = 0;
    PresentStatus status = PresentStatus::Unknown;
    bool recoverable = false;
    bool swapchainRebuildRequired = false;
    bool surfaceLost = false;
};

enum class FrameDiscardStatus {
    Unknown,
    Discarded,
    SurfaceLost,
    Failed,
    AlreadyInvalidated,
};

struct FrameDiscardResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint64_t frameSerial = 0;
    std::uint32_t frameIndex = 0;
    FrameDiscardStatus status = FrameDiscardStatus::Unknown;
    bool recoverable = false;
    bool swapchainRebuildRequired = false;
    bool surfaceLost = false;
};

inline bool acquireStatusIsRecoverable(const AcquireStatus status)
{
    return status == AcquireStatus::Suboptimal || status == AcquireStatus::OutOfDate
        || status == AcquireStatus::SurfaceLost || status == AcquireStatus::NotReady;
}

inline bool presentStatusIsRecoverable(const PresentStatus status)
{
    return status == PresentStatus::Suboptimal || status == PresentStatus::OutOfDate
        || status == PresentStatus::SurfaceLost;
}

inline bool acquireStatusRequiresSwapchainRebuild(const AcquireStatus status)
{
    return status != AcquireStatus::NotReady && acquireStatusIsRecoverable(status);
}

inline bool presentStatusRequiresSwapchainRebuild(const PresentStatus status)
{
    return presentStatusIsRecoverable(status);
}

inline AcquiredFrame makeAcquireSuccessResult(const std::uint64_t frameSerial,
    const std::uint32_t frameIndex,
    const AcquireStatus status = AcquireStatus::Acquired)
{
    AcquiredFrame result;
    result.success = true;
    result.frameSerial = frameSerial;
    result.frameIndex = frameIndex;
    result.status = status;
    result.recoverable = acquireStatusIsRecoverable(status);
    result.swapchainRebuildRequired = acquireStatusRequiresSwapchainRebuild(status);
    result.surfaceLost = status == AcquireStatus::SurfaceLost;
    return result;
}

inline AcquiredFrame makeAcquireFailureResult(std::pmr::string diagnostics,
    const std::uint64_t frameSerial,
    const std::uint32_t frameIndex,
    const AcquireStatus status = AcquireStatus::Failed)
{
    AcquiredFrame result;
    result.success = false;
    result.diagnostics = std::move(diagnostics);
    result.frameSerial = frameSerial;
    result.frameIndex = frameIndex;
    result.status = status;
    result.recoverable = acquireStatusIsRecoverable(status);
    result.swapchainRebuildRequired = acquireStatusRequiresSwapchainRebuild(status);
    result.surfaceLost = status == AcquireStatus::SurfaceLost;
    return result;
}

inline PresentResult makePresentSuccessResult(const std::uint64_t frameSerial,
    const std::uint32_t frameIndex,
    const PresentStatus status = PresentStatus::Presented)
{
    PresentResult result;
    result.success = true;
    result.frameSerial = frameSerial;
    result.frameIndex = frameIndex;
    result.status = status;
    result.recoverable = presentStatusIsRecoverable(status);
    result.swapchainRebuildRequired = presentStatusRequiresSwapchainRebuild(status);
    result.surfaceLost = status == PresentStatus::SurfaceLost;
    return result;
}

inline PresentResult makePresentFailureResult(std::pmr::string diagnostics,
    const std::uint64_t frameSerial,
    const std::uint32_t frameIndex,
    const PresentStatus status = PresentStatus::Failed)
{
    PresentResult result;
    result.success = false;
    result.diagnostics = std::move(diagnostics);
    result.frameSerial = frameSerial;
    result.frameIndex = frameIndex;
    result.status = status;
    result.recoverable = presentStatusIsRecoverable(status);
    result.swapchainRebuildRequired = presentStatusRequiresSwapchainRebuild(status);
    result.surfaceLost = status == PresentStatus::SurfaceLost;
    return result;
}

inline FrameDiscardResult makeFrameDiscardSuccessResult(
    const std::uint64_t frameSerial, const std::uint32_t frameIndex)
{
    FrameDiscardResult result;
    result.success = true;
    result.frameSerial = frameSerial;
    result.frameIndex = frameIndex;
    result.status = FrameDiscardStatus::Discarded;
    return result;
}

inline FrameDiscardResult makeFrameDiscardFailureResult(std::pmr::string diagnostics,
    const std::uint64_t frameSerial,
    const std::uint32_t frameIndex,
    const FrameDiscardStatus status = FrameDiscardStatus::Failed)
{
    FrameDiscardResult result;
    result.success = false;
    result.diagnostics = std::move(diagnostics);
    result.frameSerial = frameSerial;
    result.frameIndex = frameIndex;
    result.status = status;
    result.recoverable = status == FrameDiscardStatus::SurfaceLost;
    result.swapchainRebuildRequired = result.recoverable;
    result.surfaceLost = status == FrameDiscardStatus::SurfaceLost;
    return result;
}

struct SwapchainImageRef {
    GpuResourceId image;
    std::uint32_t imageIndex = 0;
    std::uint64_t frameSerial = 0;
    Extent2D extent;
    TextureFormat format = TextureFormat::Unknown;
    ResourceState state = ResourceState::Present;
};

struct AcquiredSwapchainImage {
    bool success = false;
    std::pmr::string diagnostics;
    SwapchainImageRef ref;
    arp::core::UniquePtr<GpuTexture> texture;
};

struct SwapchainLifecycleResult {
    bool success = false;
    std::pmr::string diagnostics;
    bool acquireAllowed = false;
    bool presentAllowed = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    Extent2D extent;
};

// A backend-neutral update to the swapchain creation contract.  The active
// backend owns lowering it to native presentation objects and returns the
// lifecycle impact instead of exposing backend handles to callers.
struct SwapchainReconfigureRequest {
    SwapchainDesc desc;
};

// Result of backend lowering. `selectedBackendMode` intentionally preserves
// native vocabulary (for example FIFO or MAILBOX) for diagnostics while the
// policy enum remains portable for application decisions.
struct SwapchainPresentationStatus {
    PresentationMode requested = PresentationMode::Auto;
    PresentationMode selected = PresentationMode::Auto;
    bool fallback = false;
    std::pmr::string selectedBackendMode;
};

/// Read-only completion for the exact graphics submission that rendered an
/// acquired frame. This is neither a presentation callback nor a CPU serial.
struct FramePresentationCompletion {
    std::shared_ptr<const GpuFence> renderCompletion;
};

class ARP_API Swapchain {
public:
    virtual ~Swapchain() = default;
    virtual std::uint32_t imageCount() const = 0;
    virtual std::uint32_t maxFramesInFlight() const = 0;
    virtual AcquiredFrame acquireNextFrame() = 0;
    /// AcquiredFrameValidity/1: same-owner, serialized, allocation-free query.
    /// Acquisition authority is distinct from native validity/GPU completion.
    virtual bool isFrameAcquired(const AcquiredFrame& frame) const noexcept = 0;
    virtual AcquiredSwapchainImage acquiredImage(const AcquiredFrame& frame) = 0;
    /// Releases an acquired frame when command recording or submission fails.
    /// The backend must release the native surface image/drawable and its
    /// in-flight slot without claiming that a valid render frame was
    /// presented. Callers must treat a failed discard as a lifecycle failure.
    virtual FrameDiscardResult discardFrame(const AcquiredFrame& frame) = 0;
    virtual PresentResult present(
        const AcquiredFrame& frame, const FramePresentationCompletion& completion = {}) = 0;
    virtual SwapchainLifecycleResult setSurfaceAvailable(bool available) = 0;
    virtual SwapchainLifecycleResult resize(const Extent2D& extent) = 0;
    virtual SwapchainLifecycleResult reconfigure(const SwapchainReconfigureRequest& request) = 0;
    virtual SwapchainPresentationStatus presentationStatus() const = 0;
};

struct RhiFrameContractValidationResult {
    bool success = false;
    std::pmr::string diagnostics;
};

struct RhiFrameContractValidatorDesc {
    std::uint32_t logicalFrameSlots = 2;
};

struct RenderTargetDesc {
    std::pmr::string label;
    GpuResourceId target;
    Extent2D extent;
    TextureFormat format = TextureFormat::Unknown;
    ResourceState finalState = ResourceState::Present;
};

struct DepthTargetDesc {
    std::pmr::string label;
    GpuResourceId target;
    Extent2D extent;
    TextureFormat format = TextureFormat::Unknown;
    ResourceState finalState = ResourceState::DepthAttachment;
};

struct RenderOutputDesc {
    RenderTargetDesc color;
    bool hasDepth = false;
    DepthTargetDesc depth;
    TextureFormat presentFormat = TextureFormat::Unknown;
};

enum class TextureUploadLayoutSource { Unknown, ExplicitMetadata };

struct TextureUploadMetadata {
    std::pmr::string label;
    GpuResourceId destinationTexture;
    Extent3D extent;
    TextureFormat format = TextureFormat::Unknown;
    std::uint32_t bytesPerRow = 0;
    std::uint32_t rowsPerImage = 0;
    TextureSubresourceRange subresources;
    StagingIntent stagingIntent = StagingIntent::Upload;
    TextureUploadLayoutSource layoutSource = TextureUploadLayoutSource::Unknown;
    bool backendOwnedStaging = true;
};

struct MeshBufferUploadIntent {
    std::pmr::string label;
    GpuResourceId vertexBuffer;
    GpuResourceId indexBuffer;
    std::uint64_t vertexBytes = 0;
    std::uint64_t indexBytes = 0;
    std::uint32_t vertexStrideBytes = 0;
    std::uint32_t indexFormatBytes = 0;
    StagingIntent stagingIntent = StagingIntent::Upload;
    bool backendOwnedStaging = true;
};

struct MaterialTextureBindingIntent {
    std::pmr::string materialName;
    std::uint32_t binding = 0;
    GpuResourceId texture;
    GpuResourceId sampler;
    TextureFormat format = TextureFormat::Unknown;
    TextureUsage textureUsage = TextureUsage::None;
    bool descriptorBacked = false;
};

enum class RendererInputKind {
    Unknown,
    GuiDraw,
    ParticleDraw,
    Fxaa,
    ScenePass,
    ShadowPass,
    GiCompute
};

struct RendererInputContract {
    RendererInputKind kind = RendererInputKind::Unknown;
    std::pmr::string label;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t dispatchCount = 0;
    std::uint32_t vertexBytes = 0;
    std::uint32_t indexBytes = 0;
    std::uint32_t particleCount = 0;
    std::uint32_t meshCount = 0;
    std::uint32_t materialBindingCount = 0;
    std::uint32_t sampledTextureCount = 0;
    std::uint32_t storageBufferCount = 0;
    std::uint32_t storageTextureCount = 0;
    std::uint32_t shadowCasterCount = 0;
    bool hasDepthTarget = false;
    bool descriptorBacked = false;
    bool pipelineLayoutKnown = false;
    bool explicitResourceStates = false;
    bool backendOwnedStaging = true;
};

class RhiFrameContractValidator final {
public:
    explicit RhiFrameContractValidator(const RhiFrameContractValidatorDesc& desc);

    RhiFrameContractValidationResult validateAcquireResult(const AcquiredFrame& frame) const;
    RhiFrameContractValidationResult validatePresentResult(
        const AcquiredFrame& acquired, const PresentResult& present) const;
    RhiFrameContractValidationResult validateSwapchainImage(const SwapchainImageRef& image) const;
    RhiFrameContractValidationResult validateRenderOutput(const RenderOutputDesc& output) const;
    RhiFrameContractValidationResult validateTextureUploadMetadata(
        const TextureUploadMetadata& upload) const;
    RhiFrameContractValidationResult validateMeshBufferUploadIntent(
        const MeshBufferUploadIntent& intent) const;
    RhiFrameContractValidationResult validateMaterialTextureBindingIntent(
        const MaterialTextureBindingIntent& intent) const;
    RhiFrameContractValidationResult validateRendererInput(
        const RendererInputContract& input) const;

private:
    RhiFrameContractValidatorDesc desc_;
};

} // namespace arp::rhi
