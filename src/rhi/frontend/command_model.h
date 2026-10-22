#pragma once

#include "core/algorithm/ordered_span_cursor.h"
#include "rhi/gpu_common.h"

#include <array>
#include <cstddef>
#include <cstdint>

// region Command usage validation
// region command_usage
namespace arp::rhi {

// CommandUsage/1 describes ownership usage, independently of native lowering,
// scalar command validation and completion. Passing it never grants execution.
enum class CommandReferenceDomain : std::uint32_t {
    Buffer,
    Texture,
    TextureView,
    Sampler,
    Pipeline,
    Descriptor,
    BindlessRoot,
    Upload,
    Readback,
    Query,
    RenderBundle,
    IndirectProducer,
    IndirectProcessor,
    DescriptorTable,
    SecondaryCommandBuffer,
    Count
};
enum class CommandUsageKind : std::uint32_t {
    BufferBarrier,
    TextureBarrier,
    BufferCopy,
    BufferToTextureCopy,
    TextureToBufferCopy,
    TextureToTextureCopy,
    TextureUpload,
    TextureReadback,
    RetainIndexedPipeline,
    RetainIndexedSnapshot,
    ClearFaultBuffer,
    ClearSnapshotFault,
    RecordIndirectProducer,
    RecordIndirectProcessor,
    IndirectProcessorDraw,
    BufferFaultReadback,
    SnapshotFaultReadback,
    ProcessorFaultReadback,
    BeginRenderPass,
    EndRenderPass,
    BindGraphicsPipeline,
    ExecuteRenderBundle,
    SetViewport,
    SetScissor,
    BindRenderDescriptor,
    BindVertexBuffer,
    BindIndexBuffer,
    Draw,
    DrawIndexed,
    DrawIndexedIndirect,
    DrawIndexedIndirectCount,
    Timestamp,
    BeginComputePass,
    EndComputePass,
    BindComputePipeline,
    BindComputeDescriptor,
    PushConstants,
    Dispatch,
    DispatchIndirect,
    PushDebugGroup,
    PopDebugGroup,
    InsertDebugMarker,
    RetainTexture,
    Finish,
    // Append new operations without renumbering the original ownership vocabulary.
    BeginTransferPass,
    EndTransferPass,
    BufferMemoryBarrier,
    TextureMemoryBarrier,
    AliasBarrier,
    BufferUpload,
    LegacySizeOnlyBufferCopy,
    BindTessellationFactorBuffer,
    BindRenderDescriptorTable,
    BindComputeDescriptorTable,
    ExecuteSecondaryCommandBuffer,
    PipelineStatistics,
    NextSubpass,
    PrepareBindlessGraphicsPass,
    BindPreparedComputeBinding,
    BindPreparedGraphicsBinding,
    Count
};

struct CommandUsage {
    CommandUsageKind kind = CommandUsageKind::Finish;
    std::uint32_t firstReference = 0u, referenceCount = 0u;
};

class CommandUsageContract final {
public:
    // Attachment references are explicit views in color/optional-resolve order,
    // followed by the optional depth/stencil view. Descriptor references name
    // the original complete binding closure. Draw/dispatch consume previously
    // bound state: their raw explicit references follow the signature below;
    // the executor must separately prove pass/state/scalar semantics.
    template <class DomainAt>
    static bool validReferences(
        CommandUsageKind kind, std::size_t count, DomainAt&& domainAt) noexcept
    {
        using D = CommandReferenceDomain;
        const auto none = [&] {
            return count == 0u;
        };
        const auto one = [&](D a) {
            return count == 1u && domainAt(0u) == a;
        };
        const auto two = [&](D a, D b) {
            return count == 2u && domainAt(0u) == a && domainAt(1u) == b;
        };
        switch (kind) {
        case CommandUsageKind::BufferBarrier:
        case CommandUsageKind::BufferMemoryBarrier:
        case CommandUsageKind::BindTessellationFactorBuffer:
        case CommandUsageKind::BindVertexBuffer:
        case CommandUsageKind::BindIndexBuffer:
        case CommandUsageKind::DrawIndexedIndirect:
        case CommandUsageKind::DispatchIndirect:
            return one(D::Buffer);
        case CommandUsageKind::TextureBarrier:
        case CommandUsageKind::TextureMemoryBarrier:
        case CommandUsageKind::RetainTexture:
            return one(D::Texture);
        case CommandUsageKind::BufferCopy:
        case CommandUsageKind::DrawIndexedIndirectCount:
            return two(D::Buffer, D::Buffer);
        case CommandUsageKind::BufferToTextureCopy:
            return two(D::Buffer, D::Texture);
        case CommandUsageKind::TextureToBufferCopy:
            return two(D::Texture, D::Buffer);
        case CommandUsageKind::TextureToTextureCopy:
            return two(D::Texture, D::Texture);
        case CommandUsageKind::TextureUpload:
            return two(D::Texture, D::Upload);
        case CommandUsageKind::BufferUpload:
            return count == 3u && domainAt(0u) == D::Buffer && domainAt(1u) == D::Buffer
                && domainAt(2u) == D::Upload;
        case CommandUsageKind::AliasBarrier:
            return count == 2u && (domainAt(0u) == D::Buffer || domainAt(0u) == D::Texture)
                && (domainAt(1u) == D::Buffer || domainAt(1u) == D::Texture);
        case CommandUsageKind::TextureReadback:
            return two(D::Texture, D::Readback);
        case CommandUsageKind::RetainIndexedPipeline:
            return two(D::Pipeline, D::BindlessRoot);
        case CommandUsageKind::RetainIndexedSnapshot:
        case CommandUsageKind::ClearSnapshotFault:
            return one(D::BindlessRoot);
        case CommandUsageKind::ClearFaultBuffer:
            return two(D::Buffer, D::BindlessRoot);
        case CommandUsageKind::RecordIndirectProducer:
            return one(D::IndirectProducer);
        case CommandUsageKind::RecordIndirectProcessor:
            return one(D::IndirectProcessor);
        case CommandUsageKind::IndirectProcessorDraw:
            return two(D::IndirectProcessor, D::BindlessRoot);
        case CommandUsageKind::BufferFaultReadback:
            return count == 3u && domainAt(0u) == D::Buffer && domainAt(1u) == D::BindlessRoot
                && domainAt(2u) == D::Readback;
        case CommandUsageKind::SnapshotFaultReadback:
            return two(D::BindlessRoot, D::Readback);
        case CommandUsageKind::ProcessorFaultReadback:
            return two(D::IndirectProcessor, D::Readback);
        case CommandUsageKind::BeginRenderPass:
            if (!count)
                return false;
            for (std::size_t i = 0u; i < count; ++i)
                if (domainAt(i) != D::TextureView)
                    return false;
            return true;
        case CommandUsageKind::BindGraphicsPipeline:
        case CommandUsageKind::BindComputePipeline:
            return one(D::Pipeline);
        case CommandUsageKind::ExecuteRenderBundle:
            return one(D::RenderBundle);
        case CommandUsageKind::ExecuteSecondaryCommandBuffer:
            return one(D::SecondaryCommandBuffer);
        case CommandUsageKind::BindRenderDescriptorTable:
        case CommandUsageKind::BindComputeDescriptorTable:
            return one(D::DescriptorTable);
        case CommandUsageKind::PrepareBindlessGraphicsPass:
        case CommandUsageKind::BindPreparedComputeBinding:
        case CommandUsageKind::BindPreparedGraphicsBinding:
            return one(D::BindlessRoot);
        case CommandUsageKind::BindRenderDescriptor:
        case CommandUsageKind::BindComputeDescriptor:
            return one(D::Descriptor);
        case CommandUsageKind::Timestamp:
        case CommandUsageKind::PipelineStatistics:
            return one(D::Query);
        case CommandUsageKind::PushConstants:
            // Ordinary encoders retain an Upload source. Original Root streams
            // authenticate the bound Root and borrow their frozen argument bytes.
            return one(D::Upload) || one(D::BindlessRoot);
        case CommandUsageKind::EndRenderPass:
        case CommandUsageKind::SetViewport:
        case CommandUsageKind::SetScissor:
        case CommandUsageKind::Draw:
        case CommandUsageKind::DrawIndexed:
        case CommandUsageKind::BeginComputePass:
        case CommandUsageKind::EndComputePass:
        case CommandUsageKind::BeginTransferPass:
        case CommandUsageKind::EndTransferPass:
        case CommandUsageKind::NextSubpass:
        case CommandUsageKind::Dispatch:
        case CommandUsageKind::PushDebugGroup:
        case CommandUsageKind::PopDebugGroup:
        case CommandUsageKind::InsertDebugMarker:
        case CommandUsageKind::Finish:
            return none();
        // This legacy overload has no original source/destination identity.
        case CommandUsageKind::LegacySizeOnlyBufferCopy:
            return false;
        case CommandUsageKind::Count:
            return false;
        }
        return false;
    }
};

// endregion command_usage

// region command_input

// Positional input family only. Scalar classification does not prove original
// source ownership, legality, backend recording or completion for any command.
class CommandInputContract final {
public:
    enum class Family {
        Scalar,
        Pass,
        Copy,
        Dependency,
        Unsupported,
        BufferUpload,
        TextureUpload,
        TextureReadback
    };

    // Consume sparse descriptors in command order. Scalar/pass routing remains
    // the caller's explicit contract; no backend or source semantics are inferred.
    template <class Descriptor>
    static const char* consumeDescriptor(std::size_t command,
        CommandUsageKind kind,
        const Descriptor* descriptors,
        std::size_t count,
        std::size_t& consumed,
        const char* orderDiagnostic,
        const char* kindDiagnostic) noexcept
    {
        if (consumed >= count || !descriptors || descriptors[consumed].commandIndex != command)
            return orderDiagnostic;
        if (descriptors[consumed].kind() != kind)
            return kindDiagnostic;
        ++consumed;
        return nullptr;
    }

    static constexpr Family family(CommandUsageKind kind) noexcept
    {
        using K = CommandUsageKind;
        switch (kind) {
        case K::BeginRenderPass:
            return Family::Pass;
        case K::BufferCopy:
        case K::BufferToTextureCopy:
        case K::TextureToBufferCopy:
        case K::TextureToTextureCopy:
            return Family::Copy;
        case K::BufferBarrier:
        case K::TextureBarrier:
        case K::BufferMemoryBarrier:
        case K::TextureMemoryBarrier:
        case K::AliasBarrier:
            return Family::Dependency;
        case K::BufferUpload:
            return Family::BufferUpload;
        case K::TextureUpload:
            return Family::TextureUpload;
        case K::TextureReadback:
            return Family::TextureReadback;
        case K::LegacySizeOnlyBufferCopy:
        case K::Count:
            return Family::Unsupported;
        case K::BeginComputePass:
        case K::BeginTransferPass:
        case K::EndRenderPass:
        case K::EndComputePass:
        case K::EndTransferPass:
        case K::BindGraphicsPipeline:
        case K::BindComputePipeline:
        case K::BindRenderDescriptor:
        case K::BindComputeDescriptor:
        case K::BindRenderDescriptorTable:
        case K::BindComputeDescriptorTable:
        case K::BindVertexBuffer:
        case K::BindIndexBuffer:
        case K::BindTessellationFactorBuffer:
        case K::SetViewport:
        case K::SetScissor:
        case K::Draw:
        case K::DrawIndexed:
        case K::DrawIndexedIndirect:
        case K::DrawIndexedIndirectCount:
        case K::Dispatch:
        case K::DispatchIndirect:
        case K::Timestamp:
        case K::PipelineStatistics:
        case K::PushConstants:
        case K::ExecuteRenderBundle:
        case K::ExecuteSecondaryCommandBuffer:
        case K::NextSubpass:
        case K::PushDebugGroup:
        case K::PopDebugGroup:
        case K::InsertDebugMarker:
        case K::Finish:
        case K::RetainTexture:
        case K::RetainIndexedPipeline:
        case K::RetainIndexedSnapshot:
        case K::ClearFaultBuffer:
        case K::ClearSnapshotFault:
        case K::RecordIndirectProducer:
        case K::RecordIndirectProcessor:
        case K::IndirectProcessorDraw:
        case K::BufferFaultReadback:
        case K::SnapshotFaultReadback:
        case K::ProcessorFaultReadback:
        case K::PrepareBindlessGraphicsPass:
        case K::BindPreparedComputeBinding:
        case K::BindPreparedGraphicsBinding:
            return Family::Scalar;
        }
        return Family::Unsupported;
    }
};

// endregion command_input

// region command_usage_sequence

// Frozen positional ownership sequence only. Scope, scalar/input, resource
// state, original object/native binding and completion proof are independent.
class CommandUsageSequenceContract final {
public:
    struct Diagnostics {
        const char* input = "RHI-COMMAND-USAGE-INPUT";
        const char* finish = "RHI-COMMAND-USAGE-FINISH";
        const char* span = "RHI-COMMAND-USAGE-SPAN";
        const char* signature = "RHI-COMMAND-USAGE-SIGNATURE";
        const char* tail = "RHI-COMMAND-USAGE-REFERENCE-TAIL";
    };

    template <class DomainAt>
    static const char* validate(const CommandUsage* commands,
        std::size_t count,
        std::size_t references,
        DomainAt&& domainAt,
        const Diagnostics& diagnostics) noexcept
    {
        if (!commands || !count)
            return diagnostics.input;
        core::OrderedSpanCursor cursor(count, references);
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& command = commands[i];
            if ((command.kind == CommandUsageKind::Finish) != (i == count - 1u))
                return diagnostics.finish;
            const core::OrderedSpanCursor::Span span{
                command.firstReference, command.referenceCount};
            if (!cursor.matchesNext(span))
                return diagnostics.span;
            if (!CommandUsageContract::validReferences(
                    command.kind, command.referenceCount, [&](std::size_t offset) {
                        return domainAt(span.first + offset);
                    }))
                return diagnostics.signature;
            const auto ticket = cursor.preflight(span, [](std::size_t) {
                return true;
            });
            if (!cursor.commit(ticket))
                return diagnostics.span;
        }
        return cursor.complete() ? nullptr : diagnostics.tail;
    }
};

// endregion command_usage_sequence
// endregion Command usage validation

// region Command input admission validation
// region original_command_descriptor_consumption

// Independently opted-in CPU projection intent, never state/native authority.
class OriginalCommandDescriptorConsumptionContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t finiteFootprintVersion = 0u, wholeFootprintVersion = 0u;
        bool finiteFullBoundViewOptIn = false, wholeFullBoundViewOptIn = false;
        std::uint64_t maxConsumptions = 0u, maxLogicalAccesses = 0u;
        std::uint32_t depthTableFootprintVersion = 0u;
        bool depthTableFullBoundViewOptIn = false;
        std::uint64_t maxTextureAccesses = 0u;
        // OriginalIndexedDrawConsumption/1: shader footprints only. Index and
        // vertex content, recording and completion retain independent gates.
        std::uint32_t indexedDrawConsumptionVersion = 0u;
        bool indexedDrawConsumptionOptIn = false;
        // OriginalVertexStreamShaderConsumption/1 projects shader resources
        // alongside indexed immutable geometry. Geometry binding/content/state
        // and execution remain independently validated; no implicit opt-in.
        std::uint32_t vertexStreamConsumptionVersion = 0u;
        bool vertexStreamConsumptionOptIn = false;
        std::uint32_t maxVertexStreams = 0u;
        std::uint32_t generalShaderResourcesVersion = 0u;
        bool generalShaderResourcesOptIn = false;
    };

    static constexpr bool isConsumption(CommandUsageKind kind, const Budget& budget) noexcept
    {
        return kind == CommandUsageKind::Draw || kind == CommandUsageKind::Dispatch
            || (kind == CommandUsageKind::DrawIndexed && budget.contractVersion == 3u
                && budget.indexedDrawConsumptionVersion == 1u
                && budget.indexedDrawConsumptionOptIn);
    }

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u
            && b.contractVersion != 4u)
            return "RHI-ORIGINAL-CONSUMPTION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-CONSUMPTION-OPT-IN";
        if (const auto* id = validateGeneralResources(
                b.contractVersion, b.generalShaderResourcesVersion, b.generalShaderResourcesOptIn))
            return id;
        if (b.vertexStreamConsumptionVersion || b.vertexStreamConsumptionOptIn
            || b.maxVertexStreams) {
            if (b.contractVersion != 3u || b.indexedDrawConsumptionVersion != 1u
                || !b.indexedDrawConsumptionOptIn)
                return "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-PROFILE";
            if (b.vertexStreamConsumptionVersion != 1u)
                return "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-VERSION";
            if (!b.vertexStreamConsumptionOptIn)
                return "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-OPT-IN";
            if (!b.maxVertexStreams)
                return "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-BUDGET";
        }
        if (b.indexedDrawConsumptionVersion || b.indexedDrawConsumptionOptIn) {
            if (b.contractVersion != 3u)
                return "RHI-ORIGINAL-CONSUMPTION-INDEXED-PROFILE";
            if (b.indexedDrawConsumptionVersion != 1u)
                return "RHI-ORIGINAL-CONSUMPTION-INDEXED-VERSION";
            if (!b.indexedDrawConsumptionOptIn)
                return "RHI-ORIGINAL-CONSUMPTION-INDEXED-OPT-IN";
        }
        if (b.contractVersion == 1u
            && (b.depthTableFootprintVersion || b.depthTableFullBoundViewOptIn
                || b.maxTextureAccesses))
            return "RHI-ORIGINAL-CONSUMPTION-LEGACY-SCHEMA";
        if (b.contractVersion == 4u) {
            if (b.depthTableFootprintVersion || b.depthTableFullBoundViewOptIn)
                return "RHI-ORIGINAL-CONSUMPTION-GENERAL-FINITE-SCHEMA";
            if (b.maxTextureAccesses > b.maxLogicalAccesses)
                return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-BUDGET";
        } else if (b.contractVersion >= 2u) {
            if (b.depthTableFootprintVersion != 1u)
                return "RHI-ORIGINAL-CONSUMPTION-TABLE-FOOTPRINT-VERSION";
            if (!b.depthTableFullBoundViewOptIn)
                return "RHI-ORIGINAL-CONSUMPTION-TABLE-FOOTPRINT-OPT-IN";
            if (!b.maxTextureAccesses || b.maxTextureAccesses > b.maxLogicalAccesses)
                return "RHI-ORIGINAL-CONSUMPTION-TEXTURE-BUDGET";
        }
        if (b.contractVersion == 1u || b.finiteFootprintVersion || b.wholeFootprintVersion
            || b.finiteFullBoundViewOptIn || b.wholeFullBoundViewOptIn)
            if (const auto* id = validateFootprints(b.finiteFootprintVersion,
                    b.wholeFootprintVersion,
                    b.finiteFullBoundViewOptIn,
                    b.wholeFullBoundViewOptIn))
                return id;
        return b.maxConsumptions && b.maxLogicalAccesses ? nullptr
                                                         : "RHI-ORIGINAL-CONSUMPTION-BUDGET";
    }

    // Reuse the existing footprint opt-in semantics in independently bounded
    // projections. This helper does not validate a request version or counters.
    static constexpr const char* validateFootprints(std::uint32_t finiteVersion,
        std::uint32_t wholeVersion,
        bool finiteOptIn,
        bool wholeOptIn) noexcept
    {
        if ((finiteOptIn ? finiteVersion != 1u : finiteVersion != 0u)
            || (wholeOptIn ? wholeVersion != 1u : wholeVersion != 0u))
            return "RHI-ORIGINAL-CONSUMPTION-FOOTPRINT-VERSION";
        return finiteOptIn || wholeOptIn ? nullptr : "RHI-ORIGINAL-CONSUMPTION-FOOTPRINT-OPT-IN";
    }

    static constexpr const char* validateGeneralResources(
        std::uint32_t profile, std::uint32_t version, bool optedIn) noexcept
    {
        if (profile == 4u) {
            if (version != 2u)
                return "RHI-ORIGINAL-CONSUMPTION-GENERAL-VERSION";
            return optedIn ? nullptr : "RHI-ORIGINAL-CONSUMPTION-GENERAL-OPT-IN";
        }
        if (!version && !optedIn)
            return nullptr;
        if (profile != 2u && profile != 3u)
            return "RHI-ORIGINAL-CONSUMPTION-GENERAL-PROFILE";
        if (version != 1u)
            return "RHI-ORIGINAL-CONSUMPTION-GENERAL-VERSION";
        return optedIn ? nullptr : "RHI-ORIGINAL-CONSUMPTION-GENERAL-OPT-IN";
    }

    // Value-only join shared by Consumption and RenderUsage, including their
    // captured walkers. Inputs validates its own table/geometry requirements.
    // GeneralResources/1 permits ordinary sets alongside the unchanged table
    // profile; geometry still requires Inputs5. This grants no native authority.
    static constexpr bool inputsProfileMatches(std::uint32_t footprintVersion,
        std::uint32_t generalVersion,
        bool generalOptIn,
        std::uint32_t inputsVersion) noexcept
    {
        if (footprintVersion < 1u || footprintVersion > 4u
            || validateGeneralResources(footprintVersion, generalVersion, generalOptIn))
            return false;
        if (footprintVersion == 4u)
            return inputsVersion == 6u;
        if (!generalVersion)
            return inputsVersion == footprintVersion + 2u;
        return (footprintVersion == 2u && (inputsVersion == 3u || inputsVersion == 4u))
            || (footprintVersion == 3u && inputsVersion == 5u);
    }

    static constexpr bool empty(const Budget& b) noexcept
    {
        return !b.contractVersion && !b.prototypeOptIn && !b.finiteFootprintVersion
            && !b.wholeFootprintVersion && !b.finiteFullBoundViewOptIn && !b.wholeFullBoundViewOptIn
            && !b.maxConsumptions && !b.maxLogicalAccesses && !b.depthTableFootprintVersion
            && !b.depthTableFullBoundViewOptIn && !b.maxTextureAccesses
            && !b.indexedDrawConsumptionVersion && !b.indexedDrawConsumptionOptIn
            && !b.vertexStreamConsumptionVersion && !b.vertexStreamConsumptionOptIn
            && !b.maxVertexStreams && !b.generalShaderResourcesVersion
            && !b.generalShaderResourcesOptIn;
    }

    static constexpr bool matches(const Budget& a, const Budget& b) noexcept
    {
        return a.contractVersion == b.contractVersion && a.prototypeOptIn == b.prototypeOptIn
            && a.finiteFootprintVersion == b.finiteFootprintVersion
            && a.wholeFootprintVersion == b.wholeFootprintVersion
            && a.finiteFullBoundViewOptIn == b.finiteFullBoundViewOptIn
            && a.wholeFullBoundViewOptIn == b.wholeFullBoundViewOptIn
            && a.maxConsumptions == b.maxConsumptions
            && a.maxLogicalAccesses == b.maxLogicalAccesses
            && a.depthTableFootprintVersion == b.depthTableFootprintVersion
            && a.depthTableFullBoundViewOptIn == b.depthTableFullBoundViewOptIn
            && a.maxTextureAccesses == b.maxTextureAccesses
            && a.indexedDrawConsumptionVersion == b.indexedDrawConsumptionVersion
            && a.indexedDrawConsumptionOptIn == b.indexedDrawConsumptionOptIn
            && a.vertexStreamConsumptionVersion == b.vertexStreamConsumptionVersion
            && a.vertexStreamConsumptionOptIn == b.vertexStreamConsumptionOptIn
            && a.maxVertexStreams == b.maxVertexStreams
            && a.generalShaderResourcesVersion == b.generalShaderResourcesVersion
            && a.generalShaderResourcesOptIn == b.generalShaderResourcesOptIn;
    }

    static constexpr const char* validateVertexStreams(const Budget& budget,
        std::uint32_t inputsVersion,
        CommandUsageKind kind,
        std::size_t streams) noexcept
    {
        if (!streams)
            return nullptr;
        if (budget.vertexStreamConsumptionVersion != 1u || !budget.vertexStreamConsumptionOptIn)
            return "RHI-ORIGINAL-CONSUMPTION-VERTEX-PROFILE";
        if (inputsVersion != 5u || kind != CommandUsageKind::DrawIndexed)
            return "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-INPUTS";
        return streams <= budget.maxVertexStreams
            ? nullptr
            : "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-CAPACITY";
    }
};

// endregion original_command_descriptor_consumption

// region original_command_admission

// GenericOriginalCommandAdmission/1. Value intent only; no executable facade.
// Independent limits must be charged to the corresponding original owner.
struct OriginalCommandAdmissionBudget {
    enum class CpuDomain : std::uint32_t {
        Source,
        Graph,
        Recording,
        Submission,
        Callback,
        Result,
        Count
    };
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::array<std::uint32_t, static_cast<std::size_t>(CommandReferenceDomain::Count)>
        maxSourceOwners{};
    std::uint32_t maxCommands = 0u, maxReferences = 0u, maxGraphOwners = 0u,
                  maxInputDescriptors = 0u;
    std::uint32_t maxPassColorAttachments = 0u, maxPassAttachmentReferences = 0u,
                  maxPassSubpasses = 0u;
    std::uint32_t maxPassAttachmentIndices = 0u, maxPassDependencies = 0u;
    std::uint32_t maxStateObservations = 0u, maxStateTransactions = 0u, maxDebugDepth = 0u,
                  maxDynamicOffsets = 0u;
    std::uint32_t maxMipMappings = 0u, maxPendingSubmissions = 0u, maxCallbackTickets = 0u,
                  maxCallbackRegistrations = 0u;
    std::uint32_t maxNativePayloadObjects = 0u;
    std::uint64_t maxLabelBytes = 0u, maxPushBytes = 0u, maxRetainedUploadBytes = 0u;
    std::uint64_t maxUnpublishedResultBytes = 0u, maxNativeStagingBytes = 0u,
                  maxNativeTransientBytes = 0u;
    std::array<std::uint64_t, static_cast<std::size_t>(CpuDomain::Count)> maxCpuBytes{};
    std::uint32_t maxTextureRanges = 0u;
    // CanonicalOriginalTypedSources/1: authored native API call ceiling. Zero
    // closes typed native lowering; it is never derived from scalar count.
    std::uint64_t maxTypedNativeCommandCalls = 0u;
};

class OriginalCommandAdmissionContract final {
public:
    // All domains have explicit L2 ownership requirements, including domains
    // whose original source admission has not yet been implemented.
    enum class Ownership {
        UniqueObject,
        SharedOriginal,
        FrozenInput,
        PreparedOriginal,
        Unsupported
    };

    static constexpr Ownership ownership(CommandReferenceDomain domain) noexcept
    {
        using D = CommandReferenceDomain;
        switch (domain) {
        case D::Buffer:
        case D::Texture:
        case D::TextureView:
        case D::Sampler:
        case D::Descriptor:
        case D::DescriptorTable:
        case D::RenderBundle:
        case D::SecondaryCommandBuffer:
            return Ownership::UniqueObject;
        case D::Pipeline:
            return Ownership::SharedOriginal;
        case D::Upload:
            return Ownership::FrozenInput;
        case D::Readback:
        case D::Query:
        case D::BindlessRoot:
        case D::IndirectProducer:
        case D::IndirectProcessor:
            return Ownership::PreparedOriginal;
        case D::Count:
            return Ownership::Unsupported;
        }
        return Ownership::Unsupported;
    }

    static constexpr const char* validateIntent(const OriginalCommandAdmissionBudget& b) noexcept
    {
        using C = OriginalCommandAdmissionBudget::CpuDomain;
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-COMMAND-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-COMMAND-OPT-IN";
        if (!b.maxCommands || !b.maxGraphOwners
            || !b.maxCpuBytes[static_cast<std::size_t>(C::Graph)])
            return "RHI-ORIGINAL-COMMAND-GRAPH-BUDGET";
        bool sources = false;
        for (const auto n : b.maxSourceOwners)
            sources = sources || n != 0u;
        if (sources && !b.maxCpuBytes[static_cast<std::size_t>(C::Source)])
            return "RHI-ORIGINAL-COMMAND-SOURCE-BUDGET";
        if ((b.maxStateTransactions || b.maxNativePayloadObjects)
            && !b.maxCpuBytes[static_cast<std::size_t>(C::Recording)])
            return "RHI-ORIGINAL-COMMAND-RECORDING-BUDGET";
        if (b.maxTextureRanges && !b.maxStateObservations)
            return "RHI-ORIGINAL-COMMAND-RANGE-OBSERVATION-BUDGET";
        if (b.maxPendingSubmissions && !b.maxCpuBytes[static_cast<std::size_t>(C::Submission)])
            return "RHI-ORIGINAL-COMMAND-SUBMISSION-BUDGET";
        if ((b.maxCallbackTickets || b.maxCallbackRegistrations)
            && !b.maxCpuBytes[static_cast<std::size_t>(C::Callback)])
            return "RHI-ORIGINAL-COMMAND-CALLBACK-BUDGET";
        if (b.maxUnpublishedResultBytes && !b.maxCpuBytes[static_cast<std::size_t>(C::Result)])
            return "RHI-ORIGINAL-COMMAND-RESULT-BUDGET";
        return nullptr;
    }
};

// endregion original_command_admission
// endregion Command input admission validation

// region Command state validation
// region original_command_state_admission

// Intent limits only. No caller-created observation, control, device-domain or
// registration record is an original issuer. Device-owned construction follows
// this contract before the complete generic execution gate may be changed.
// Shared value intent for render-binding footprints. State capture and the dry
// walker consume the same schema; command/binding ceilings remain independent
// of Draw/Dispatch consumption and of state-observation admission.
struct OriginalRenderDescriptorUsageBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t finiteFootprintVersion = 0u, wholeFootprintVersion = 0u;
    bool finiteFullBoundViewOptIn = false, wholeFullBoundViewOptIn = false;
    std::uint64_t maxCommands = 0u, maxBindings = 0u, maxLogicalAccesses = 0u;
    std::uint32_t depthTableFootprintVersion = 0u;
    bool depthTableFullBoundViewOptIn = false;
    std::uint64_t maxTextureAccesses = 0u;
    std::uint32_t generalShaderResourcesVersion = 0u;
    bool generalShaderResourcesOptIn = false;

    constexpr bool matches(const OriginalRenderDescriptorUsageBudget& other) const noexcept
    {
        return contractVersion == other.contractVersion && prototypeOptIn == other.prototypeOptIn
            && finiteFootprintVersion == other.finiteFootprintVersion
            && wholeFootprintVersion == other.wholeFootprintVersion
            && finiteFullBoundViewOptIn == other.finiteFullBoundViewOptIn
            && wholeFullBoundViewOptIn == other.wholeFullBoundViewOptIn
            && maxCommands == other.maxCommands && maxBindings == other.maxBindings
            && maxLogicalAccesses == other.maxLogicalAccesses
            && depthTableFootprintVersion == other.depthTableFootprintVersion
            && depthTableFullBoundViewOptIn == other.depthTableFullBoundViewOptIn
            && maxTextureAccesses == other.maxTextureAccesses
            && generalShaderResourcesVersion == other.generalShaderResourcesVersion
            && generalShaderResourcesOptIn == other.generalShaderResourcesOptIn;
    }

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !prototypeOptIn && !finiteFootprintVersion
            && !wholeFootprintVersion && !finiteFullBoundViewOptIn && !wholeFullBoundViewOptIn
            && !maxCommands && !maxBindings && !maxLogicalAccesses && !depthTableFootprintVersion
            && !depthTableFullBoundViewOptIn && !maxTextureAccesses
            && !generalShaderResourcesVersion && !generalShaderResourcesOptIn;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u && contractVersion != 2u && contractVersion != 3u
            && contractVersion != 4u)
            return "RHI-ORIGINAL-RENDER-USAGE-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-RENDER-USAGE-OPT-IN";
        if (const auto* id = OriginalCommandDescriptorConsumptionContract::validateGeneralResources(
                contractVersion, generalShaderResourcesVersion, generalShaderResourcesOptIn))
            return id;
        if (contractVersion == 1u) {
            if (depthTableFootprintVersion || depthTableFullBoundViewOptIn || maxTextureAccesses)
                return "RHI-ORIGINAL-RENDER-USAGE-LEGACY-SCHEMA";
            if (const auto* id = OriginalCommandDescriptorConsumptionContract::validateFootprints(
                    finiteFootprintVersion,
                    wholeFootprintVersion,
                    finiteFullBoundViewOptIn,
                    wholeFullBoundViewOptIn))
                return id;
        } else {
            OriginalCommandDescriptorConsumptionContract::Budget shared{contractVersion,
                prototypeOptIn,
                finiteFootprintVersion,
                wholeFootprintVersion,
                finiteFullBoundViewOptIn,
                wholeFullBoundViewOptIn,
                1u,
                maxLogicalAccesses,
                depthTableFootprintVersion,
                depthTableFullBoundViewOptIn,
                maxTextureAccesses};
            shared.generalShaderResourcesVersion = generalShaderResourcesVersion;
            shared.generalShaderResourcesOptIn = generalShaderResourcesOptIn;
            if (const auto* id =
                    OriginalCommandDescriptorConsumptionContract::validateBudget(shared))
                return id;
        }
        return maxCommands && maxBindings && maxLogicalAccesses
            ? nullptr
            : "RHI-ORIGINAL-RENDER-USAGE-BUDGET";
    }
};

// GeometryInputAccess/1 intent shared by Inputs5, State4 and Stream4. It is
// independent from descriptor consumption and grants no native or fetch proof.
struct OriginalGeometryUsageBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCommands = 0u, maxBindings = 0u, maxPairChecks = 0u;

    // Independent GeometryDrawConsumption/1 intent. The old binding footprint
    // remains unchanged when every field below is absent.
    std::uint32_t drawConsumptionContractVersion = 0u;
    bool drawConsumptionOptIn = false;
    std::uint64_t maxDraws = 0u, maxDrawAccesses = 0u, maxBindingScans = 0u;

    constexpr bool drawConsumptionEmpty() const noexcept
    {
        return !drawConsumptionContractVersion && !drawConsumptionOptIn && !maxDraws
            && !maxDrawAccesses && !maxBindingScans;
    }

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !prototypeOptIn && !maxCommands && !maxBindings && !maxPairChecks
            && drawConsumptionEmpty();
    }

    constexpr bool matches(const OriginalGeometryUsageBudget& b) const noexcept
    {
        return contractVersion == b.contractVersion && prototypeOptIn == b.prototypeOptIn
            && maxCommands == b.maxCommands && maxBindings == b.maxBindings
            && maxPairChecks == b.maxPairChecks
            && drawConsumptionContractVersion == b.drawConsumptionContractVersion
            && drawConsumptionOptIn == b.drawConsumptionOptIn && maxDraws == b.maxDraws
            && maxDrawAccesses == b.maxDrawAccesses && maxBindingScans == b.maxBindingScans;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-GEOMETRY-ACCESS-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-GEOMETRY-ACCESS-OPT-IN";
        if (!drawConsumptionEmpty()) {
            if (drawConsumptionContractVersion != 1u)
                return "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-VERSION";
            if (!drawConsumptionOptIn)
                return "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-OPT-IN";
            if (!maxDraws || !maxDrawAccesses || !maxBindingScans)
                return "RHI-ORIGINAL-GEOMETRY-CONSUMPTION-BUDGET";
        }
        return maxCommands && maxBindings ? nullptr : "RHI-ORIGINAL-GEOMETRY-ACCESS-BUDGET";
    }
};

// Independently budgeted frozen AtomicFullBlockPush consumption. This is CPU
// preflight. Recording needs the separate explicit intent and backend capability;
// neither intent grants submission or completion authority.
struct OriginalCommandPushConsumptionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxUpdates = 0u, maxBytes = 0u;
    std::uint32_t recordingContractVersion = 0u;
    bool recordingOptIn = false;

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !prototypeOptIn && !maxUpdates && !maxBytes
            && !recordingContractVersion && !recordingOptIn;
    }

    constexpr bool matches(const OriginalCommandPushConsumptionBudget& other) const noexcept
    {
        return contractVersion == other.contractVersion && prototypeOptIn == other.prototypeOptIn
            && maxUpdates == other.maxUpdates && maxBytes == other.maxBytes
            && recordingContractVersion == other.recordingContractVersion
            && recordingOptIn == other.recordingOptIn;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-PUSH-CONSUMPTION-OPT-IN";
        if (recordingContractVersion || recordingOptIn) {
            if (recordingContractVersion != 1u)
                return "RHI-ORIGINAL-PUSH-RECORDING-VERSION";
            if (!recordingOptIn)
                return "RHI-ORIGINAL-PUSH-RECORDING-OPT-IN";
        }
        return maxUpdates && maxBytes ? nullptr : "RHI-ORIGINAL-PUSH-CONSUMPTION-BUDGET";
    }
};

// Privately owned upload storage is independent of logical frozen push bytes.
// The resident budget includes alignment gaps and the complete dynamic binding
// span. Counts cover additional native payloads/owners, not the existing Root.
// This request grants no recording, submission or completion authority.
struct OriginalCommandPushUploadBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxUpdates = 0u, maxPayloadBytes = 0u, maxResidentBytes = 0u;
    std::uint32_t maxNativePayloadObjects = 0u, maxRetainedOwners = 0u;

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !prototypeOptIn && !maxUpdates && !maxPayloadBytes
            && !maxResidentBytes && !maxNativePayloadObjects && !maxRetainedOwners;
    }

    constexpr bool matches(const OriginalCommandPushUploadBudget& other) const noexcept
    {
        return contractVersion == other.contractVersion && prototypeOptIn == other.prototypeOptIn
            && maxUpdates == other.maxUpdates && maxPayloadBytes == other.maxPayloadBytes
            && maxResidentBytes == other.maxResidentBytes
            && maxNativePayloadObjects == other.maxNativePayloadObjects
            && maxRetainedOwners == other.maxRetainedOwners;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-ORIGINAL-PUSH-UPLOAD-VERSION";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-PUSH-UPLOAD-OPT-IN";
        return maxUpdates && maxPayloadBytes && maxResidentBytes && maxNativePayloadObjects
                && maxRetainedOwners
            ? nullptr
            : "RHI-ORIGINAL-PUSH-UPLOAD-BUDGET";
    }
};

// Independent CPU-only join of Root /1 declarations into the canonical state
// graph. Retention does not consume a shader or confer execution authority.
// DirectResultCopy/1 extends Root5 only, under a separate explicit ceiling.
// It authenticates declared buffer copies; it grants no native execution.
struct OriginalBindlessDirectResultCopyIntent {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCopies = 0u, maxDependencyVisits = 0u;

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !prototypeOptIn && !maxCopies && !maxDependencyVisits;
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (empty())
            return nullptr;
        if (contractVersion != 1u)
            return "RHI-BINDLESS-DIRECT-RESULT-COPY-VERSION";
        if (!prototypeOptIn)
            return "RHI-BINDLESS-DIRECT-RESULT-COPY-OPT-IN";
        return maxCopies && maxDependencyVisits ? nullptr
                                                : "RHI-BINDLESS-DIRECT-RESULT-COPY-BUDGET";
    }

    constexpr bool matches(const OriginalBindlessDirectResultCopyIntent& other) const noexcept
    {
        return contractVersion == other.contractVersion && prototypeOptIn == other.prototypeOptIn
            && maxCopies == other.maxCopies && maxDependencyVisits == other.maxDependencyVisits;
    }
};

struct OriginalBindlessRootStateBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxRoots = 0u, maxAccesses = 0u, maxSourceVisits = 0u;
    std::uint64_t maxFaultClears = 0u;
    std::uint64_t maxFaultCopies = 0u;
    OriginalCommandPushConsumptionBudget pushConsumption;
    // RootState6 joins finite indirect argument reads to the same original
    // inventory/journal. This is an address/access prerequisite, never proof
    // of GPU-generated argument values or native execution authority.
    std::uint64_t maxIndirectCalls = 0u, maxIndirectRecords = 0u;
    std::uint64_t maxDependencyVisits = 0u;
    OriginalBindlessDirectResultCopyIntent directResultCopies;

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !prototypeOptIn && !maxRoots && !maxAccesses && !maxSourceVisits
            && !maxFaultClears && !maxFaultCopies && pushConsumption.empty() && !maxIndirectCalls
            && !maxIndirectRecords && !maxDependencyVisits && directResultCopies.empty();
    }

    constexpr bool geometryJoined() const noexcept
    {
        return contractVersion == 5u || contractVersion == 6u;
    }

    constexpr bool matches(const OriginalBindlessRootStateBudget& other) const noexcept
    {
        return contractVersion == other.contractVersion && prototypeOptIn == other.prototypeOptIn
            && maxRoots == other.maxRoots && maxAccesses == other.maxAccesses
            && maxSourceVisits == other.maxSourceVisits && maxFaultClears == other.maxFaultClears
            && maxFaultCopies == other.maxFaultCopies
            && pushConsumption.matches(other.pushConsumption)
            && maxIndirectCalls == other.maxIndirectCalls
            && maxIndirectRecords == other.maxIndirectRecords
            && maxDependencyVisits == other.maxDependencyVisits
            && directResultCopies.matches(other.directResultCopies);
    }

    constexpr const char* diagnostic() const noexcept
    {
        if (contractVersion < 1u || contractVersion > 6u)
            return "RHI-ORIGINAL-BINDLESS-STATE-VERSION";
        if (const auto* id = directResultCopies.diagnostic())
            return id;
        if (!directResultCopies.empty() && contractVersion != 5u)
            return "RHI-BINDLESS-DIRECT-RESULT-COPY-ROOT-PROFILE";
        if (contractVersion != 6u
            && (maxIndirectCalls || maxIndirectRecords || maxDependencyVisits))
            return "RHI-ORIGINAL-BINDLESS-INDIRECT-LEGACY-SCHEMA";
        if (contractVersion == 6u && (!maxIndirectCalls || !maxIndirectRecords))
            return "RHI-ORIGINAL-BINDLESS-INDIRECT-BUDGET";
        if (contractVersion == 6u && !maxDependencyVisits)
            return "RHI-ORIGINAL-BINDLESS-DEPENDENCY-BUDGET";
        if (!prototypeOptIn)
            return "RHI-ORIGINAL-BINDLESS-STATE-OPT-IN";
        if (!pushConsumption.empty()) {
            if (contractVersion < 2u)
                return "RHI-ORIGINAL-BINDLESS-STATE-PUSH-PROFILE";
            if (const auto* id = pushConsumption.diagnostic())
                return id;
        }
        if (contractVersion < 3u && maxFaultClears)
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-LEGACY-SCHEMA";
        if (contractVersion < 4u && maxFaultCopies)
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-LEGACY-SCHEMA";
        if (contractVersion == 3u && !maxFaultClears)
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-BUDGET";
        if (contractVersion >= 4u && (!maxFaultClears || !maxFaultCopies))
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-BUDGET";
        if (!maxRoots || !maxAccesses || !maxSourceVisits)
            return "RHI-ORIGINAL-BINDLESS-STATE-BUDGET";
        return nullptr;
    }
};

struct OriginalCommandStateAdmissionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxDeviceDomains = 0u, maxResourceRegistrations = 0u, maxStateObservations = 0u,
                  maxTextureRanges = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    OriginalCommandDescriptorConsumptionContract::Budget descriptorConsumption;
    OriginalRenderDescriptorUsageBudget renderDescriptorUsage;
    // TypedReadbackState/1 adds retained Source2 only to State3. Source counts,
    // nested owners and exact parent/destination correspondence remain authored
    // in the original Sources/Inputs contract; this grants CPU capture only.
    std::uint32_t retainedReadbackStateVersion = 0u;
    bool retainedReadbackStateOptIn = false;
    OriginalGeometryUsageBudget geometryUsage;
    OriginalBindlessRootStateBudget bindlessRoots;
};

class OriginalCommandStateAdmissionContract final {
public:
    enum class Requirement {
        BufferCommittedRevision,
        TextureRangeCommittedRevision,
        CanonicalParentRange,
        StateFreeRegisteredLifetime,
        CanonicalRegisteredClosure,
        Unsupported
    };

    static constexpr Requirement requirement(CommandReferenceDomain domain) noexcept
    {
        using D = CommandReferenceDomain;
        switch (domain) {
        case D::Buffer:
            return Requirement::BufferCommittedRevision;
        case D::Texture:
            return Requirement::TextureRangeCommittedRevision;
        case D::TextureView:
            return Requirement::CanonicalParentRange;
        case D::Sampler:
        case D::Pipeline:
            return Requirement::StateFreeRegisteredLifetime;
        case D::Descriptor:
        case D::DescriptorTable:
        case D::Upload:
        case D::Readback:
        case D::Query:
        case D::RenderBundle:
        case D::SecondaryCommandBuffer:
        case D::BindlessRoot:
        case D::IndirectProducer:
        case D::IndirectProcessor:
            return Requirement::CanonicalRegisteredClosure;
        case D::Count:
            return Requirement::Unsupported;
        }
        return Requirement::Unsupported;
    }

    static constexpr const char* scalarAuthorityDiagnostic(CommandReferenceDomain domain) noexcept
    {
        return requirement(domain) == Requirement::Unsupported
            ? "RHI-ORIGINAL-STATE-SOURCE-UNSUPPORTED"
            : "RHI-ORIGINAL-STATE-REGISTRATION-UNPROVEN";
    }

    static constexpr const char* validateIntent(
        const OriginalCommandStateAdmissionBudget& budget) noexcept
    {
        if (!budget.bindlessRoots.empty()) {
            if (budget.contractVersion != 1u)
                return "RHI-ORIGINAL-BINDLESS-STATE-PROFILE";
            if (const auto* id = budget.bindlessRoots.diagnostic())
                return id;
        }
        if (budget.contractVersion != 1u && budget.contractVersion != 2u
            && budget.contractVersion != 3u && budget.contractVersion != 4u
            && budget.contractVersion != 5u)
            return "RHI-ORIGINAL-STATE-VERSION";
        if (budget.contractVersion == 1u
            && !OriginalCommandDescriptorConsumptionContract::empty(budget.descriptorConsumption))
            return "RHI-ORIGINAL-STATE-LEGACY-SCHEMA";
        if (budget.contractVersion < 3u && !budget.renderDescriptorUsage.empty())
            return "RHI-ORIGINAL-STATE-RENDER-LEGACY-SCHEMA";
        const bool rootGeometry =
            budget.contractVersion == 1u && budget.bindlessRoots.geometryJoined();
        if (budget.contractVersion != 4u && !rootGeometry && !budget.geometryUsage.empty())
            return "RHI-ORIGINAL-STATE-GEOMETRY-LEGACY-SCHEMA";
        if (budget.contractVersion == 4u || rootGeometry)
            if (const auto* id = budget.geometryUsage.diagnostic())
                return id;
        if (rootGeometry && budget.geometryUsage.drawConsumptionEmpty())
            return "RHI-ORIGINAL-BINDLESS-GEOMETRY-CONSUMPTION-REQUIRED";
        if (budget.retainedReadbackStateVersion || budget.retainedReadbackStateOptIn) {
            if (budget.contractVersion != 3u && budget.contractVersion != 4u)
                return "RHI-ORIGINAL-STATE-READBACK-PROFILE";
            if (budget.retainedReadbackStateVersion != 1u)
                return "RHI-ORIGINAL-STATE-READBACK-VERSION";
            if (!budget.retainedReadbackStateOptIn)
                return "RHI-ORIGINAL-STATE-READBACK-OPT-IN";
        }
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-STATE-OPT-IN";
        if (budget.contractVersion == 2u) {
            // State2's descriptor visitors are buffer-only. The dry texture
            // footprint cannot authorize their buffer indexing or transactions.
            if (budget.descriptorConsumption.contractVersion != 1u)
                return "RHI-ORIGINAL-STATE-CONSUMPTION-PROFILE";
            if (const auto* id = OriginalCommandDescriptorConsumptionContract::validateBudget(
                    budget.descriptorConsumption))
                return id;
        }
        if (budget.contractVersion >= 3u) {
            if (budget.descriptorConsumption.contractVersion != budget.contractVersion - 1u)
                return "RHI-ORIGINAL-STATE-CONSUMPTION-PROFILE";
            if (const auto* id = OriginalCommandDescriptorConsumptionContract::validateBudget(
                    budget.descriptorConsumption))
                return id;
            if (budget.renderDescriptorUsage.contractVersion != budget.contractVersion - 1u)
                return "RHI-ORIGINAL-STATE-RENDER-PROFILE";
            if (const auto* id = budget.renderDescriptorUsage.diagnostic())
                return id;
        }
        // One complete original recording belongs to one exact original device.
        if (budget.maxDeviceDomains != 1u)
            return "RHI-ORIGINAL-STATE-DEVICE-DOMAIN";
        if (!budget.maxResourceRegistrations)
            return "RHI-ORIGINAL-STATE-REGISTRATION-BUDGET";
        if (!budget.maxRetainedOwners || !budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-STATE-OWNER-CPU-BUDGET";
        if (budget.maxTextureRanges && !budget.maxStateObservations)
            return "RHI-ORIGINAL-STATE-RANGE-OBSERVATION-BUDGET";
        return nullptr;
    }
};

// endregion original_command_state_admission

// region original_command_state_sequence

// /1-3 are immutable CPU state/access prerequisites, not execution plans.
// Cells count every expanded original observation (duplicate ranges included).
// Steps count every declared per-cell access/transition, plus scalar Buffer
// operands. /3 charges shader and render-binding logical contributions together,
// while captured texture cells already account for their timeline steps. Only
// implicit buffer contributions add steps; texture steps must not be doubled.
// No capacity is inferred from Source observation/range limits.
struct OriginalCommandStateSequenceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxDebugDepth = 0u;
    std::uint64_t maxStateCells = 0u, maxSteps = 0u;
    std::uint64_t maxImplicitAccesses = 0u;
    std::uint32_t bindlessRootStateVersion = 0u;
    bool bindlessRootStateOptIn = false;
};

class OriginalCommandStateSequenceContract final {
public:
    static constexpr const char* validateIntent(
        const OriginalCommandStateSequenceBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u && budget.contractVersion != 2u
            && budget.contractVersion != 3u && budget.contractVersion != 4u
            && budget.contractVersion != 5u)
            return "RHI-ORIGINAL-SEQUENCE-VERSION";
        if (budget.bindlessRootStateVersion || budget.bindlessRootStateOptIn) {
            if (budget.contractVersion != 1u
                || (budget.bindlessRootStateVersion != 1u && budget.bindlessRootStateVersion != 2u
                    && budget.bindlessRootStateVersion != 3u
                    && budget.bindlessRootStateVersion != 4u
                    && budget.bindlessRootStateVersion != 5u
                    && budget.bindlessRootStateVersion != 6u))
                return "RHI-ORIGINAL-BINDLESS-SEQUENCE-PROFILE";
            if (!budget.bindlessRootStateOptIn)
                return "RHI-ORIGINAL-BINDLESS-SEQUENCE-OPT-IN";
            if (!budget.maxImplicitAccesses)
                return "RHI-ORIGINAL-BINDLESS-SEQUENCE-BUDGET";
        }
        if (budget.contractVersion == 1u && budget.maxImplicitAccesses
            && !budget.bindlessRootStateVersion)
            return "RHI-ORIGINAL-SEQUENCE-LEGACY-SCHEMA";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-SEQUENCE-OPT-IN";
        if (!budget.maxCommands || !budget.maxStateCells || !budget.maxSteps)
            return "RHI-ORIGINAL-SEQUENCE-BUDGET";
        if (budget.contractVersion >= 2u && !budget.maxImplicitAccesses)
            return "RHI-ORIGINAL-SEQUENCE-IMPLICIT-BUDGET";
        return nullptr;
    }
};

// endregion original_command_state_sequence

// region original_command_access

// PortableOriginalCommandAccess/1 is an explicitly opted-in CPU prerequisite.
// Buffer copies align source/destination/size to four bytes (WebGPU buffer-copy
// specification); buffer/texture row pitch uses the existing versioned transfer
// contract kTextureUploadRowPitchAlignment. No alignment is inferred from OS or
// backend names and no native capability is granted by validation.
class OriginalCommandAccessContract final {
public:
    static constexpr std::uint32_t version = 1u;
    static constexpr std::uint64_t bufferCopyAlignment = 4u;
};

// endregion original_command_access

// region original_command_state_plan

// OriginalCpuStatePlan/1: explicit Prototype CPU declaration only. The exact
// canonical transaction root owns immutable ordered access/transition intent,
// unique final cells and sealed observations under its Recording quota. Access
// preflight and all-controls (writes + one rollback revision) admission precede
// allocation/enlistment/writes; all operations require external serialization.
// Expanded cells and steps use the independently authored sequence budget.
// Overlapping normalized ranges share canonical mip/layer/aspect final cells;
// each barrier writes its declared range once, not once per represented cell.
// Uniform controls accept only their exact default range; Depth24Stencil8
// uniform plans fail because one scalar cannot prove independent aspect state.
// Commit declares CPU states only, never GPU completion/visibility/order.
// Null: ValidatorOnly with closed native execution/issuer. Vulkan/Metal/WebGPU:
// DiagnosticFailure for full native execution/issuer;
// none may substitute CPU replay for a native payload. Loss refuses commit and
// retained pending ownership lasts until abandon/destruction; rollback never
// revives an invalidated original domain. No retry of an abandoned root.
class OriginalCommandStatePlanContract final {
public:
    static constexpr std::uint32_t version = 1u;
};
} // namespace arp::rhi

// endregion original_command_state_plan
// endregion Command state validation

// region original_initialized_buffer_pending
namespace arp::rhi {
// Initialized bytes retain their original completion capture. Pending reads
// require the sole original recording seal/journal; these values grant no issuer.
enum class OriginalInitializedBufferPendingUse {
    CopySource1,
    ImmutableGeometryRead1,
    ImmutableUniformRead2,
    ImmutableShaderBufferRead3,
    ImmutableGraphicsShaderBufferRead4,
    ImmutableBindlessGeometryRead5,
    ImmutableBindlessShaderRead6
};

class OriginalInitializedBufferPendingContract final {
public:
    using Use = OriginalInitializedBufferPendingUse;

    static constexpr const char* validateUse(std::uint32_t version, Use use) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u)
            return "RHI-ORIGINAL-INITIALIZED-BUFFER-PENDING-VERSION";
        return (version == 1u && (use == Use::CopySource1 || use == Use::ImmutableGeometryRead1))
                || (version == 2u && use == Use::ImmutableUniformRead2)
                || (version == 3u && use == Use::ImmutableShaderBufferRead3)
                || (version == 4u && use == Use::ImmutableGraphicsShaderBufferRead4)
                || (version == 5u && use == Use::ImmutableBindlessGeometryRead5)
                || (version == 6u && use == Use::ImmutableBindlessShaderRead6)
            ? nullptr
            : "RHI-ORIGINAL-INITIALIZED-BUFFER-PENDING-USE";
    }

    static constexpr ResourceState state(Use use) noexcept
    {
        return use == Use::CopySource1 ? ResourceState::CopySrc
            : use == Use::ImmutableGeometryRead1 || use == Use::ImmutableUniformRead2
                || use == Use::ImmutableShaderBufferRead3
                || use == Use::ImmutableGraphicsShaderBufferRead4
                || use == Use::ImmutableBindlessGeometryRead5
                || use == Use::ImmutableBindlessShaderRead6
            ? ResourceState::ShaderRead
            : ResourceState::Undefined;
    }

    static constexpr const char* validateRootProfile(
        Use use, bool originalGeneral, bool finiteGeneral, std::uint32_t contentVersion) noexcept
    {
        if (use == Use::ImmutableBindlessShaderRead6)
            return !originalGeneral && !finiteGeneral
                    && (contentVersion == 0u || contentVersion == 8u || contentVersion == 9u
                        || (contentVersion >= 11u && contentVersion <= 16u) || contentVersion == 18u
                        || contentVersion == 19u)
                ? nullptr
                : "RHI-ORIGINAL-BINDLESS-SHADER-PENDING-CONTENT-PROFILE";
        if (use == Use::ImmutableBindlessGeometryRead5)
            return !originalGeneral && !finiteGeneral && contentVersion == 9u
                ? nullptr
                : "RHI-ORIGINAL-BINDLESS-GEOMETRY-PENDING-CONTENT-PROFILE";
        if (use == Use::ImmutableGraphicsShaderBufferRead4)
            return finiteGeneral && !originalGeneral && contentVersion == 6u
                ? nullptr
                : "RHI-ORIGINAL-INITIALIZED-GRAPHICS-BUFFER-PENDING-PROFILE";
        if (use == Use::ImmutableUniformRead2)
            return originalGeneral && !finiteGeneral && contentVersion == 4u
                ? nullptr
                : "RHI-ORIGINAL-INITIALIZED-UNIFORM-PENDING-PROFILE";
        if (use == Use::ImmutableShaderBufferRead3)
            return finiteGeneral && !originalGeneral && contentVersion == 5u
                ? nullptr
                : "RHI-ORIGINAL-INITIALIZED-SHADER-BUFFER-PENDING-PROFILE";
        return nullptr;
    }

    static constexpr const char* validateStates(Use use,
        ResourceState captured,
        ResourceState initial,
        ResourceState final,
        ResourceState pending,
        std::uint32_t version = 1u) noexcept
    {
        if (const auto* id = validateUse(version, use))
            return id;
        const auto expected = state(use);
        return captured == expected && initial == expected && final == expected
                && pending == expected
            ? nullptr
            : "RHI-ORIGINAL-INITIALIZED-BUFFER-PENDING-READ-STATE";
    }
};
} // namespace arp::rhi

// endregion original_initialized_buffer_pending

namespace arp::rhi {
// Native completion consumes the same choice frozen by the original recording
// issuer. A range or completed stamp cannot infer this submission profile.
class OriginalGeneralSubmissionCompletionContract final {
public:
    static constexpr const char* validateProfile(std::uint32_t submissionVersion,
        bool wholeGeneral,
        bool finiteGeneral,
        std::uint32_t contentVersion,
        bool bindlessRoot = false,
        bool producedGeometry = false) noexcept
    {
        if (producedGeometry || submissionVersion == 15u)
            return producedGeometry && submissionVersion == 15u && contentVersion == 9u
                    && bindlessRoot && !wholeGeneral && !finiteGeneral
                ? nullptr
                : "RHI-BINDLESS-PRODUCED-GEOMETRY-COMPLETION-PROFILE";
        if (contentVersion == 19u || submissionVersion == 14u)
            return submissionVersion == 14u && contentVersion == 19u && bindlessRoot
                    && !wholeGeneral && !finiteGeneral
                ? nullptr
                : "RHI-CANONICAL-GRAPHICS-DEPTH-SUBMISSION-COMPLETION-PROFILE";
        if (contentVersion == 18u || submissionVersion == 13u)
            return submissionVersion == 13u && contentVersion == 18u && bindlessRoot
                    && !wholeGeneral && !finiteGeneral
                ? nullptr
                : "RHI-GRAPHICS-DEPTH-SUBMISSION-COMPLETION-PROFILE";
        if (contentVersion == 15u || contentVersion == 16u || submissionVersion == 11u
            || submissionVersion == 12u)
            return bindlessRoot && !wholeGeneral && !finiteGeneral
                    && ((contentVersion == 15u && submissionVersion == 11u)
                        || (contentVersion == 16u && submissionVersion == 12u))
                ? nullptr
                : "RHI-MIXED-ROOT-SUBMISSION-COMPLETION-PROFILE";
        if (contentVersion == 14u || submissionVersion == 10u)
            return submissionVersion == 10u && bindlessRoot && !wholeGeneral && !finiteGeneral
                    && contentVersion == 14u
                ? nullptr
                : "RHI-STORAGE-ROOT-CALLBACK-SUBMISSION-COMPLETION-PROFILE";
        if (contentVersion == 13u || submissionVersion == 9u)
            return submissionVersion == 9u && bindlessRoot && !wholeGeneral && !finiteGeneral
                    && contentVersion == 13u
                ? nullptr
                : "RHI-STORAGE-ROOT-NATIVE-SUBMISSION-COMPLETION-PROFILE";
        if (contentVersion == 12u || submissionVersion == 8u)
            return submissionVersion == 8u && bindlessRoot && !wholeGeneral && !finiteGeneral
                    && contentVersion == 12u
                ? nullptr
                : "RHI-DEPTH-ROOT-NATIVE-SUBMISSION-COMPLETION-PROFILE";
        if (contentVersion == 11u || submissionVersion == 7u)
            return submissionVersion == 7u && bindlessRoot && !wholeGeneral && !finiteGeneral
                    && contentVersion == 11u
                ? nullptr
                : "RHI-DEPTH-ROOT-SUBMISSION-COMPLETION-PROFILE";
        if (contentVersion == 9u || submissionVersion == 6u)
            return submissionVersion == 6u && bindlessRoot && !wholeGeneral && !finiteGeneral
                    && contentVersion == 9u
                ? nullptr
                : "RHI-BINDLESS-GRAPHICS-SUBMISSION-COMPLETION-PROFILE";
        if (bindlessRoot || contentVersion == 8u)
            return submissionVersion == 5u && bindlessRoot && !wholeGeneral && !finiteGeneral
                    && contentVersion == 8u
                ? nullptr
                : "RHI-BINDLESS-COMPLETED-TEXTURE-COMPLETION-PROFILE";
        return (!submissionVersion && !wholeGeneral && !finiteGeneral && contentVersion <= 3u)
                || (submissionVersion == 1u && wholeGeneral && !finiteGeneral
                    && contentVersion == 4u)
                || (submissionVersion == 2u && finiteGeneral && !wholeGeneral
                    && contentVersion == 5u)
                || (submissionVersion == 3u && finiteGeneral && !wholeGeneral
                    && contentVersion == 6u)
                || (submissionVersion == 4u && !finiteGeneral && !wholeGeneral
                    && contentVersion == 7u)
            ? nullptr
            : "RHI-GENERAL-SUBMISSION-COMPLETION-PROFILE";
    }
};
} // namespace arp::rhi
