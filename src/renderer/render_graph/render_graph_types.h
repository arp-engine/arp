#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_commands.h"

#include <cstdint>
#include <string>

namespace arp::render_graph {

struct TextureHandle {
    std::uint32_t id = 0;
};

struct BufferHandle {
    std::uint32_t id = 0;
};

enum class ResourceKind { Texture, Buffer };

enum class ResourceAccess { Read, Write };

enum class PassKind { Graphics, Compute, Copy, Present };

enum class QueueRole { Graphics, Compute, Transfer, Present };

/// Compiler-owned resource dependency classification. This is part of the
/// RenderGraph topology contract consumed by diagnostics and execution plans.
enum class TopologyDependencyKind { Raw, War, Waw };

/// Phase 6: Subgraph pin usage. Describes how a subgraph exposes a resource
/// at its boundary — either consuming it as an attachment or sharing it as
/// a sampled/storage resource.
enum class PinUsage {
    ColorAttachment,
    DepthAttachment,
    SampledTexture,
    StorageTexture,
    StorageBuffer,
    CopySource,
    CopyDestination
};

/// Phase 6: Subgraph pin declaration. Each pin describes a single resource
/// that the subgraph reads or writes across its boundary. Pins are the
/// contract between a subgraph and its parent FrameGraph.
struct SubgraphPinDecl {
    std::pmr::string name;
    PinUsage usage = PinUsage::SampledTexture;
    ResourceKind kind = ResourceKind::Texture;
    rhi::LoadOp expectedLoadOp = rhi::LoadOp::Load;
    rhi::StoreOp expectedStoreOp = rhi::StoreOp::Store;
};

/// Phase 6: Subgraph alias scope policy. Controls whether the subgraph's
/// transient resources are isolated from or shared with the parent graph's
/// alias pool.
enum class AliasScopePolicy {
    /// Pack transient resources within this subgraph's scope only.
    Isolated,
    /// Allow cross-scope aliasing with the parent graph's global pool.
    AllowCrossScope
};

struct MobileTileHints {
    bool discardAfterPass = false;
    bool memorylessAttachment = false;
    bool avoidStore = false;
    bool preserveOnlyWhenSampledLater = false;
};

/// Explicit command arguments crossing the renderer-to-RHI frame-plan
/// boundary. Counts alone are not executable metadata and must never be
/// expanded into backend commands by pass-name heuristics.
struct RhiDrawIndexedCommand {
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstIndex = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t firstInstance = 0;
};

struct RhiDispatchCommand {
    std::uint32_t groupCountX = 0;
    std::uint32_t groupCountY = 0;
    std::uint32_t groupCountZ = 0;
};

struct RhiTransferCommand {
    std::uint64_t sizeBytes = 0;
    rhi::StagingIntent stagingIntent = rhi::StagingIntent::Upload;
    bool backendOwnedStaging = true;
    std::uint64_t frameSerial = 0;
};

/// SubpassGraph: attachment role within a subpass group.
enum class AttachmentRole { ColorAttachment, DepthAttachment };

/// SubpassGraph: opaque handle to an attachment declared on a SubpassGraph.
/// Used to reference attachments when authoring subpasses.
struct SubpassAttachment {
    std::uint32_t index = 0;
};

/// SubpassGraph: attachment declaration stored on the SubpassGraph.
struct SubpassAttachmentDecl {
    TextureHandle handle;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    AttachmentRole role = AttachmentRole::ColorAttachment;
    MobileTileHints tileHints;
};

struct ResourceTraceRecord {
    std::pmr::string passName;
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    ResourceAccess access = ResourceAccess::Read;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
};

struct PassTraceRecord {
    std::pmr::string name;
    PassKind kind = PassKind::Graphics;
    QueueRole queue = QueueRole::Graphics;
    bool asyncComputeEligible = false;
    bool culled = false;
};

struct AttachmentTraceRecord {
    std::pmr::string passName;
    std::pmr::string resourceName;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    rhi::LoadOp loadOp = rhi::LoadOp::Load;
    rhi::StoreOp storeOp = rhi::StoreOp::Store;
    MobileTileHints tileHints;
    rhi::Color clearColor;
    bool transient = false;
    bool imported = false;
    bool externalProtected = false;
};

struct AliasTraceRecord {
    std::pmr::string resourceName;
    TextureHandle handle;
    std::uint32_t aliasGroup = 0;
};

struct BarrierTraceRecord {
    std::pmr::string passName;
    std::pmr::string resourceName;
    ResourceKind kind = ResourceKind::Texture;
    rhi::ResourceState before = rhi::ResourceState::Undefined;
    rhi::ResourceState after = rhi::ResourceState::Undefined;
    rhi::PipelineStage srcStage = rhi::PipelineStage::None;
    rhi::PipelineStage dstStage = rhi::PipelineStage::None;
    rhi::ResourceAccess srcAccess = rhi::ResourceAccess::None;
    rhi::ResourceAccess dstAccess = rhi::ResourceAccess::None;
    std::pmr::string oldLayout;
    std::pmr::string newLayout;
    rhi::TextureAspect aspect = rhi::TextureAspect::Color;
    std::uint32_t handleId = 0;
    bool memoryOnly = false;
    rhi::PipelineStage replaySourceStages = rhi::PipelineStage::None;
    rhi::ResourceAccess replaySourceAccess = rhi::ResourceAccess::None;
};

} // namespace arp::render_graph
