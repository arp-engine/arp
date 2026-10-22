#pragma once
#include "core/algorithm/strided_index_range.h"
#include "core/encoding/unsigned_element_span.h"
#include "core/math/unsigned_range.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_indirect_command_schema.h"
#include "rhi/frontend/descriptor_validation.h"
#include "rhi/frontend/resource_access_scope.h"
#include "rhi/frontend/resource_memory_barrier.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/command_draw_lowering.h"
#include "rhi/semantics/indirect_instance_lowering.h"

#include <array>
#include <optional>

// region vertex_input_consumption
namespace arp::rhi {

// Cold immutable L2 layout, independent of native stream limits and slots.
class VertexInputConsumptionContract final : public core::SharedObjectIdentity {
public:
    // A moved value establishes a fresh identity, never copies its old owner.
    VertexInputConsumptionContract(VertexInputConsumptionContract&& other) noexcept
        : streams_(std::move(other.streams_)), attributes_(std::move(other.attributes_)),
          diagnostic_(other.diagnostic_)
    {}

    struct Stream {
        std::uint32_t slot, strideBytes;
        VertexInputRate rate;
        std::size_t firstAttribute, attributeCount;
        std::uint64_t fetchBytes;
    };

    struct Attribute {
        std::uint32_t slot;
        VertexAttributeDesc layout;
    };

    // Allocation-free inspection of an already retained pipeline stream. The
    // cold owner and positional validators consume the same shape calculation.
    static const char* inspectStream(const VertexBufferLayoutDesc& input, Stream& output) noexcept
    {
        if (!input.strideBytes || input.attributes.empty())
            return "RHI-VERTEX-INPUT-LAYOUT-INVALID";
        if (input.inputRate != VertexInputRate::Vertex
            && input.inputRate != VertexInputRate::Instance)
            return "RHI-VERTEX-INPUT-RATE-UNKNOWN";
        std::uint64_t footprint = 0u;
        for (const auto& attribute : input.attributes) {
            const auto bytes = formatBytes(attribute.format);
            if (!bytes)
                return "RHI-VERTEX-INPUT-FORMAT-UNKNOWN";
            footprint = std::max(footprint, std::uint64_t{attribute.offsetBytes} + bytes);
        }
        output = {
            input.slot, input.strideBytes, input.inputRate, 0u, input.attributes.size(), footprint};
        return nullptr;
    }

    explicit VertexInputConsumptionContract(const core::pmr::Vector<VertexBufferLayoutDesc>& source)
    {
        streams_.reserve(source.size());
        for (const auto& input : source) {
            Stream stream{};
            if ((diagnostic_ = inspectStream(input, stream))) {
                return;
            }
            if (findStream(input.slot)) {
                diagnostic_ = "RHI-VERTEX-INPUT-SLOT-DUPLICATE";
                return;
            }
            const auto first = attributes_.size();
            for (const auto& attribute : input.attributes) {
                if (findAttribute(attribute.shaderLocation)) {
                    diagnostic_ = "RHI-VERTEX-INPUT-LOCATION-DUPLICATE";
                    return;
                }
                attributes_.push_back({input.slot, attribute});
            }
            stream.firstAttribute = first;
            streams_.push_back(stream);
        }
    }

    VertexInputConsumptionContract(const VertexInputConsumptionContract&) = delete;
    VertexInputConsumptionContract& operator=(const VertexInputConsumptionContract&) = delete;

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& streams() const noexcept { return streams_; }

    const auto& attributes() const noexcept { return attributes_; }

    const Stream* findStream(std::uint32_t slot) const noexcept
    {
        const auto found = std::find_if(streams_.begin(), streams_.end(), [&](const auto& s) {
            return s.slot == slot;
        });
        return found == streams_.end() ? nullptr : &*found;
    }

    const Attribute* findAttribute(std::uint32_t location) const noexcept
    {
        const auto found = std::find_if(attributes_.begin(), attributes_.end(), [&](const auto& a) {
            return a.layout.shaderLocation == location;
        });
        return found == attributes_.end() ? nullptr : &*found;
    }

    static constexpr std::uint64_t formatBytes(VertexFormat format) noexcept
    {
        // All existing formats explicitly consist of 32-bit float scalars.
        constexpr auto scalarBytes = 32u / 8u;
        switch (format) {
        case VertexFormat::Float32x1:
            return scalarBytes;
        case VertexFormat::Float32x2:
            return 2u * scalarBytes;
        case VertexFormat::Float32x3:
            return 3u * scalarBytes;
        case VertexFormat::Float32x4:
            return 4u * scalarBytes;
        }
        return 0u;
    }

    bool matches(const core::pmr::Vector<VertexBufferLayoutDesc>& source) const noexcept
    {
        if (diagnostic_ || source.size() != streams_.size())
            return false;
        for (std::size_t i = 0; i < source.size(); ++i) {
            const auto& a = source[i];
            const auto& b = streams_[i];
            if (a.slot != b.slot || a.strideBytes != b.strideBytes || a.inputRate != b.rate
                || a.attributes.size() != b.attributeCount)
                return false;
            for (std::size_t j = 0; j < b.attributeCount; ++j) {
                const auto& x = a.attributes[j];
                const auto& y = attributes_[b.firstAttribute + j].layout;
                if (x.shaderLocation != y.shaderLocation || x.offsetBytes != y.offsetBytes
                    || x.format != y.format)
                    return false;
            }
        }
        return true;
    }

    const char* validateRange(std::uint32_t slot,
        std::uint64_t bufferBytes,
        std::uint64_t bindingOffset,
        std::uint64_t vertexCount,
        std::uint64_t firstVertex,
        std::uint64_t instanceCount,
        std::uint64_t firstInstance) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        const auto* stream = findStream(slot);
        if (!stream)
            return "RHI-VERTEX-INPUT-SLOT-UNDECLARED";
        return validateStreamRange(*stream,
            bufferBytes,
            bindingOffset,
            vertexCount,
            firstVertex,
            instanceCount,
            firstInstance);
    }

    // Borrow the already inspected immutable stream shape. Original draw
    // projection reuses this calculation without allocating a second layout.
    static const char* validateStreamRange(const Stream& stream,
        std::uint64_t bufferBytes,
        std::uint64_t bindingOffset,
        std::uint64_t vertexCount,
        std::uint64_t firstVertex,
        std::uint64_t instanceCount,
        std::uint64_t firstInstance) noexcept
    {
        if (!stream.strideBytes || !stream.fetchBytes
            || (stream.rate != VertexInputRate::Vertex && stream.rate != VertexInputRate::Instance))
            return "RHI-VERTEX-INPUT-LAYOUT-INVALID";
        if (!vertexCount || !instanceCount)
            return "RHI-VERTEX-INPUT-DRAW-EMPTY";
        if (!core::containsUnsignedRange(
                std::uint64_t{0u}, bufferBytes, bindingOffset, stream.fetchBytes))
            return "RHI-VERTEX-INPUT-BUFFER-RANGE";
        const auto elements =
            (bufferBytes - bindingOffset - stream.fetchBytes) / stream.strideBytes + 1u;
        const auto first = stream.rate == VertexInputRate::Vertex ? firstVertex : firstInstance;
        const auto count = stream.rate == VertexInputRate::Vertex ? vertexCount : instanceCount;
        return core::containsUnsignedRange(std::uint64_t{0u}, elements, first, count)
            ? nullptr
            : "RHI-VERTEX-INPUT-FETCH-RANGE";
    }

private:
    core::pmr::Vector<Stream> streams_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Attribute> attributes_{&core::defaultCpuMemoryResource()};
    const char* diagnostic_ = nullptr;
};

// endregion vertex_input_consumption

// region index_input_consumption

// Immutable content and cold analysis only. A range is never native authority.
class IndexInputConsumptionContract final {
public:
    enum class PrimitiveRestart { Disabled, Enabled };

    struct DrawRange {
        std::uint64_t firstIndex = 0, indexCount = 0, firstVertex = 0, vertexCount = 0;
        std::int32_t baseVertex = 0;
    };

    IndexInputConsumptionContract(const void* source,
        std::size_t bytes,
        IndexFormat format,
        core::UnsignedElementSpan::ByteOrder order,
        PrimitiveRestart restart)
        : format_(format)
    {
        if (restart != PrimitiveRestart::Disabled) {
            diagnostic_ = "RHI-INDEX-INPUT-RESTART-UNSUPPORTED";
            return;
        }
        const auto width = elementWidth(format);
        if (!width) {
            diagnostic_ = "RHI-INDEX-INPUT-FORMAT-UNKNOWN";
            return;
        }
        const core::UnsignedElementSpan input(source, bytes, *width, order);
        diagnostic_ = input.copyLittleEndian(bytes_);
    }

    IndexInputConsumptionContract(const IndexInputConsumptionContract&) = delete;
    IndexInputConsumptionContract& operator=(const IndexInputConsumptionContract&) = delete;
    IndexInputConsumptionContract(IndexInputConsumptionContract&&) noexcept = default;

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& bytes() const noexcept { return bytes_; }

    auto format() const noexcept { return format_; }

    std::size_t elementBytes() const noexcept { return elementBytes(format_); }

    static constexpr std::size_t elementBytes(IndexFormat format) noexcept
    {
        const auto width = elementWidth(format);
        return !width                                            ? 0u
            : *width == core::UnsignedElementSpan::Width::Uint16 ? sizeof(std::uint16_t)
                                                                 : sizeof(std::uint32_t);
    }

    std::uint64_t elementCount() const noexcept
    {
        return diagnostic_ || !elementBytes() ? 0u : bytes_.size() / elementBytes();
    }

    const char* analyzeDraw(std::uint64_t firstIndex,
        std::uint64_t count,
        std::int32_t baseVertex,
        DrawRange& output) const noexcept
    {
        output = {};
        if (diagnostic_)
            return diagnostic_;
        return analyzeLittleEndianBytes(bytes_.data(),
            bytes_.size(),
            format_,
            0u,
            firstIndex,
            count,
            baseVertex,
            elementCount(),
            output);
    }

    // Borrowed immutable byte validation, shared by cold owned snapshots and
    // initialized native-source actors. This value helper grants no GPU proof.
    static const char* analyzeLittleEndianBytes(const void* bytes,
        std::size_t byteCount,
        IndexFormat format,
        std::uint64_t bindingOffset,
        std::uint64_t firstIndex,
        std::uint64_t count,
        std::int32_t baseVertex,
        std::uint64_t maxAnalysisIndices,
        DrawRange& output) noexcept
    {
        output = {};
        const auto width = elementWidth(format);
        if (!width)
            return "RHI-INDEX-INPUT-FORMAT-UNKNOWN";
        const core::UnsignedElementSpan full(
            bytes, byteCount, *width, core::UnsignedElementSpan::ByteOrder::LittleEndian);
        if (const auto* id = full.diagnostic())
            return id;
        if (bindingOffset >= byteCount || bindingOffset % elementBytes(format))
            return "RHI-INDEX-INPUT-BINDING-OFFSET";
        const core::UnsignedElementSpan input(
            static_cast<const std::uint8_t*>(bytes) + bindingOffset,
            byteCount - static_cast<std::size_t>(bindingOffset),
            *width,
            core::UnsignedElementSpan::ByteOrder::LittleEndian);
        if (!count
            || !core::containsUnsignedRange(
                std::uint64_t{0u}, std::uint64_t{input.size()}, firstIndex, count))
            return "CORE-UNSIGNED-SPAN-RANGE-INVALID";
        if (!maxAnalysisIndices || count > maxAnalysisIndices)
            return "RHI-INDEX-INPUT-ANALYSIS-CAPACITY";
        core::UnsignedElementSpan::Extrema indices;
        if (const auto* id = input.extrema(firstIndex, count, indices))
            return id;
        // uint32 indices + int32 base are representable exactly by int64.
        const auto minimum = std::int64_t{indices.minimum} + baseVertex;
        const auto maximum = std::int64_t{indices.maximum} + baseVertex;
        if (minimum < 0)
            return "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE";
        output = {firstIndex,
            count,
            static_cast<std::uint64_t>(minimum),
            static_cast<std::uint64_t>(maximum - minimum) + 1u,
            baseVertex};
        return nullptr;
    }

private:
    static constexpr std::optional<core::UnsignedElementSpan::Width> elementWidth(
        IndexFormat format) noexcept
    {
        switch (format) {
        case IndexFormat::Uint16:
            return core::UnsignedElementSpan::Width::Uint16;
        case IndexFormat::Uint32:
            return core::UnsignedElementSpan::Width::Uint32;
        }
        return std::nullopt;
    }

    IndexFormat format_;
    core::ByteBuffer bytes_{&core::defaultCpuMemoryResource()};
    const char* diagnostic_ = nullptr;
};

// endregion index_input_consumption

// region render_pass_reference_sequence

// Shared CPU positional field sequence only. The callback must bind the exact
// original typed view/parent and perform its independent domain validation.
class RenderPassReferenceSequenceContract final {
public:
    template <class ViewAt>
    static const char* validate(const RenderPassDesc& desc,
        std::size_t referenceCount,
        ViewAt&& viewAt,
        const char* referenceDiagnostic) noexcept
    {
        std::size_t consumed = 0u;
        const auto view =
            [&](GpuResourceId target, TextureFormat format, bool depth) -> const char* {
            if (consumed == referenceCount)
                return referenceDiagnostic;
            return viewAt(consumed++, target, format, depth);
        };
        for (const auto& color : desc.colorAttachments) {
            if (const auto* id = view(color.target, color.format, false))
                return id;
            if (color.hasResolveTarget != color.resolveTarget.isValid())
                return referenceDiagnostic;
            if (color.hasResolveTarget)
                if (const auto* id = view(color.resolveTarget, color.format, false))
                    return id;
        }
        if (desc.hasDepthStencilAttachment)
            if (const auto* id = view(
                    desc.depthStencilAttachment.target, desc.depthStencilAttachment.format, true))
                return id;
        return consumed == referenceCount ? nullptr : referenceDiagnostic;
    }
};

// endregion render_pass_reference_sequence

// region indirect_argument_consumption

inline constexpr std::uint32_t kIndirectArgumentConsumptionContractVersion = 1u;

// Cold immutable content/range projection only. A backend-issued prepared root
// must separately prove exact uploaded arguments/index content and all owners.
class IndirectArgumentConsumptionContract final {
public:
    enum class Purpose { ImmutableHostUpload, GpuOutputExpectation };

    // Numeric expectation only. These words are never evidence of actual GPU
    // content, a selected producer proof, or host-upload authority.
    struct ExpectedGpuWords {
        std::array<std::uint32_t, 5u> words;
    };
    enum class Origin { Unknown, HostAuthoredImmutable, ApprovedGpuProduced };

    struct Source {
        const void* data = nullptr;
        std::size_t byteExtent = 0u;
        std::uint64_t offsetBytes = 0u;
        std::uint32_t drawCount = 0u;
        std::uint32_t strideBytes = 0u;
        core::UnsignedElementSpan::ByteOrder order = core::UnsignedElementSpan::ByteOrder::Unknown;
        Origin origin = Origin::Unknown;
        std::uint32_t contractVersion = kIndirectArgumentConsumptionContractVersion;
    };

    struct Budget {
        std::uint32_t maxRecords = 0u;
        std::uint64_t maxSourceBytes = 0u;
        std::uint64_t maxSnapshotBytes = 0u;
    };

    struct DrawObservation {
        DrawIndexedIndirectCommand logical{};
        IndexInputConsumptionContract::DrawRange fetch;
        bool active = false;
    };

    static constexpr std::size_t recordBytes = sizeof(DrawIndexedIndirectCommand);

    IndirectArgumentConsumptionContract(
        Source source, Budget budget, const IndexInputConsumptionContract& indices)
        : IndirectArgumentConsumptionContract(source, budget, indices, Purpose::ImmutableHostUpload)
    {}

    IndirectArgumentConsumptionContract(
        ExpectedGpuWords expected, Budget budget, const IndexInputConsumptionContract& indices)
    {
        std::array<std::uint8_t, recordBytes> bytes{};
        IndexedIndirectCommandSchema::encodeLittleEndian(
            expected.words, bytes.data(), bytes.size());
        *this = IndirectArgumentConsumptionContract(
            {bytes.data(),
                bytes.size(),
                0u,
                1u,
                recordBytes,
                core::UnsignedElementSpan::ByteOrder::LittleEndian,
                Origin::HostAuthoredImmutable},
            budget,
            indices,
            Purpose::GpuOutputExpectation);
    }

private:
    IndirectArgumentConsumptionContract(
        Source source, Budget budget, const IndexInputConsumptionContract& indices, Purpose purpose)
        : purpose_(purpose)
    {
        if (source.contractVersion != kIndirectArgumentConsumptionContractVersion) {
            diagnostic_ = "RHI-INDIRECT-ARGUMENT-VERSION";
            return;
        }
        if (source.origin == Origin::ApprovedGpuProduced) {
            diagnostic_ = "RHI-INDIRECT-ARGUMENT-GPU-PROOF-REQUIRED";
            return;
        }
        if (source.origin != Origin::HostAuthoredImmutable) {
            diagnostic_ = "RHI-INDIRECT-ARGUMENT-ORIGIN-UNKNOWN";
            return;
        }
        constexpr auto wordBytes = sizeof(std::uint32_t);
        if (!source.data || source.byteExtent < recordBytes || source.offsetBytes % wordBytes
            || source.strideBytes < recordBytes || source.strideBytes % wordBytes) {
            diagnostic_ = "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE";
            return;
        }
        const core::StridedIndexRange records(source.offsetBytes,
            source.drawCount,
            source.strideBytes,
            source.byteExtent - recordBytes);
        if (!records.valid()) {
            diagnostic_ = "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE";
            return;
        }
        if (source.order != core::UnsignedElementSpan::ByteOrder::LittleEndian
            && source.order != core::UnsignedElementSpan::ByteOrder::BigEndian) {
            diagnostic_ = "CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN";
            return;
        }
        if (!budget.maxRecords || !budget.maxSourceBytes || !budget.maxSnapshotBytes) {
            diagnostic_ = "RHI-INDIRECT-ARGUMENT-BUDGET";
            return;
        }
        // uint32 record count times the fixed schema/observation size fits uint64.
        const auto argumentBytes = std::uint64_t{source.drawCount} * recordBytes;
        const auto observationBytes = std::uint64_t{source.drawCount} * sizeof(DrawObservation);
        if (source.drawCount > budget.maxRecords || source.byteExtent > budget.maxSourceBytes
            || argumentBytes + observationBytes > budget.maxSnapshotBytes
            || argumentBytes > std::numeric_limits<std::size_t>::max()
            || observationBytes > std::numeric_limits<std::size_t>::max()) {
            diagnostic_ = "RHI-INDIRECT-ARGUMENT-BUDGET-EXCEEDED";
            return;
        }
        if (indices.diagnostic()) {
            diagnostic_ = indices.diagnostic();
            return;
        }
        // Local candidates publish together only after every record is proven.
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<DrawObservation> observations{&core::defaultCpuMemoryResource()};
        bytes.resize(static_cast<std::size_t>(argumentBytes));
        observations.resize(source.drawCount);
        for (std::size_t i = 0u; i < source.drawCount; ++i) {
            std::uint64_t offset = 0u;
            if (!records.index(i, offset)) {
                diagnostic_ = "RHI-INDIRECT-ARGUMENT-SOURCE-RANGE";
                return;
            }
            const core::UnsignedElementSpan words(
                static_cast<const std::uint8_t*>(source.data) + offset,
                recordBytes,
                core::UnsignedElementSpan::Width::Uint32,
                source.order);
            auto& observation = observations[i];
            auto& draw = observation.logical;
            if (const auto* id = IndexedIndirectCommandSchema::decode(
                    static_cast<const std::uint8_t*>(source.data) + offset,
                    recordBytes,
                    source.order,
                    draw)) {
                diagnostic_ = id;
                return;
            }
            observation.active = draw.indexCount && draw.instanceCount;
            if (observation.active) {
                if (std::uint64_t{draw.firstInstance} + draw.instanceCount
                    > std::uint64_t{UINT32_MAX} + 1u) {
                    diagnostic_ = "RHI-INDIRECT-ARGUMENT-INSTANCE-RANGE";
                    return;
                }
                if (const auto* id = indices.analyzeDraw(
                        draw.firstIndex, draw.indexCount, draw.vertexOffset, observation.fetch)) {
                    diagnostic_ = id;
                    return;
                }
            }
            for (std::size_t word = 0u; word < recordBytes / wordBytes; ++word) {
                if (!words.copyElementLittleEndian(
                        word, bytes.data() + i * recordBytes + word * wordBytes, wordBytes)) {
                    diagnostic_ = "RHI-INDIRECT-ARGUMENT-SCHEMA";
                    return;
                }
            }
        }
        bytes_ = std::move(bytes);
        observations_ = std::move(observations);
    }

public:
    IndirectArgumentConsumptionContract(const IndirectArgumentConsumptionContract&) = delete;
    IndirectArgumentConsumptionContract& operator=(
        const IndirectArgumentConsumptionContract&) = delete;
    IndirectArgumentConsumptionContract(IndirectArgumentConsumptionContract&&) noexcept = default;
    IndirectArgumentConsumptionContract& operator=(
        IndirectArgumentConsumptionContract&&) noexcept = default;

    Purpose purpose() const noexcept { return purpose_; }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& bytes() const noexcept { return bytes_; }

    std::size_t drawCount() const noexcept { return observations_.size(); }

    std::uint64_t retainedBytes() const noexcept
    {
        return bytes_.size() + std::uint64_t{observations_.size()} * sizeof(DrawObservation);
    }

    const char* observe(std::uint64_t ordinal, DrawObservation& output) const noexcept
    {
        output = {};
        if (diagnostic_)
            return diagnostic_;
        if (ordinal >= observations_.size())
            return "RHI-INDIRECT-ARGUMENT-ORDINAL";
        output = observations_[static_cast<std::size_t>(ordinal)];
        return nullptr;
    }

    const char* validateStream(std::uint64_t ordinal,
        const VertexInputConsumptionContract& vertex,
        std::uint32_t slot,
        std::uint64_t bufferBytes,
        std::uint64_t bindingOffset) const noexcept
    {
        DrawObservation draw;
        if (const auto* id = observe(ordinal, draw))
            return id;
        if (vertex.diagnostic())
            return vertex.diagnostic();
        if (!vertex.findStream(slot))
            return "RHI-VERTEX-INPUT-SLOT-UNDECLARED";
        if (!draw.active)
            return nullptr;
        return vertex.validateRange(slot,
            bufferBytes,
            bindingOffset,
            draw.fetch.vertexCount,
            draw.fetch.firstVertex,
            draw.logical.instanceCount,
            draw.logical.firstInstance);
    }

    BindlessResourceAccess requiredAccess(GpuResourceId resource) const noexcept
    {
        if (diagnostic_ || bytes_.empty() || !resource.isValid())
            return {};
        BindlessResourceAccess result;
        result.resource = resource;
        result.kind = BindlessAccessKind::Buffer;
        result.state = ResourceState::ShaderRead;
        result.pipelineStages = PipelineStage::DrawIndirect;
        result.access = ResourceAccess::IndirectCommandRead;
        result.bufferSize = bytes_.size();
        return result;
    }

    const char* validateConsumerScope(
        GpuResourceId resource, const BindlessResourceAccess& access) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        const auto required = requiredAccess(resource);
        if (!required.resource.isValid() || access.resource != resource
            || access.kind != required.kind)
            return "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE";
        if (const auto* id = semantics::validateResourceAccessScope(
                {access.state, access.pipelineStages, access.access}))
            return id;
        return access.state == required.state && access.pipelineStages == required.pipelineStages
                && access.access == required.access && access.stages == required.stages
                && !access.bufferOffset && access.bufferSize == required.bufferSize
            ? nullptr
            : "RHI-INDIRECT-ARGUMENT-CONSUMER-SCOPE";
    }

private:
    core::ByteBuffer bytes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<DrawObservation> observations_{&core::defaultCpuMemoryResource()};
    const char* diagnostic_ = nullptr;
    Purpose purpose_ = Purpose::ImmutableHostUpload;
};

namespace semantics {
struct IndirectArgumentLoweringPlan {
    std::uint32_t contractVersion = kIndirectArgumentConsumptionContractVersion;
    bool validatorOnly = false;
    bool executable = false;
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    const char* diagnosticId = "RHI-INDIRECT-ARGUMENT-PAYLOAD-MISSING";
};

// Current versioned payload facts, not caller-inferred native capabilities.
class IndirectArgumentLowering final {
public:
    static constexpr IndirectArgumentLoweringPlan current(Backend backend) noexcept
    {
        return semantics::CommandDrawLowering::IndirectArgumentLowering_current<
            IndirectArgumentLoweringPlan>(backend, kIndirectArgumentConsumptionContractVersion);
    }
};
} // namespace semantics

// endregion indirect_argument_consumption

// region zero_base_indirect_source

// Cold upload prerequisite; independent of producer/consumer execution gates.
struct ZeroBaseIndirectSourcePreparationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxSourceBytes = 0u, maxParameterBytes = 0u;
    std::uint32_t maxChildOwners = 0u;
};

// Cold immutable source/range intent. Requires independently proven canonical
// binary, native source-byte correspondence, issuer/lease and consumer ABI.
class ZeroBaseIndirectSourceConsumptionContract final : public core::SharedObjectIdentity {
public:
    static constexpr std::uint32_t version = 1u;
    using Arguments = IndirectArgumentConsumptionContract;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxSourceBytes = 0u, maxSnapshotBytes = 0u;
    };
    enum class Outcome { Produced, FaultSuppressed };

    struct Facts {
        std::uint32_t contractVersion = 0u;
        DrawIndexedIndirectCommand logical{}, physical{};
        IndirectInstanceBase mapping;
        Outcome outcome = Outcome::Produced;
    };

    static constexpr std::uint64_t physicalSnapshotBytes =
        Arguments::recordBytes + sizeof(Arguments::DrawObservation);

    ZeroBaseIndirectSourceConsumptionContract(Arguments::Source source,
        ZeroBaseIndirectProducerParams parameters,
        BindlessIndirectInstanceConsumerLayout consumer,
        Budget budget,
        const IndexInputConsumptionContract& indices)
    {
        const auto reject = [&](const char* id) {
            diagnostic_ = id;
        };
        if (budget.contractVersion != version || !budget.optIn || !budget.maxSourceBytes
            || !budget.maxSnapshotBytes) {
            reject("RHI-ZERO-BASE-SOURCE-BUDGET");
            return;
        }
        if (source.contractVersion != kIndirectArgumentConsumptionContractVersion) {
            reject("RHI-INDIRECT-ARGUMENT-VERSION");
            return;
        }
        if (source.origin != Arguments::Origin::HostAuthoredImmutable) {
            reject("RHI-ZERO-BASE-SOURCE-IMMUTABLE-PROOF-REQUIRED");
            return;
        }
        if (parameters.contractVersion != version || parameters.reserved
            || !parameters.instanceCapacity) {
            reject("RHI-ZERO-BASE-SOURCE-PARAMETERS");
            return;
        }
        if (consumer.contractVersion != version || consumer.recordIndex
            || consumer.instanceCapacity != parameters.instanceCapacity) {
            reject("RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING");
            return;
        }
        const core::UnsignedElementSpan input(
            source.data, source.byteExtent, core::UnsignedElementSpan::Width::Uint32, source.order);
        if (const auto* id = input.diagnostic()) {
            reject(id);
            return;
        }
        if (input.size() > UINT32_MAX || source.drawCount != 1u
            || source.strideBytes != Arguments::recordBytes
            || source.offsetBytes
                != std::uint64_t{parameters.sourceWordOffset} * sizeof(std::uint32_t)
            || !core::containsUnsignedRange(std::uint64_t{0u},
                input.size(),
                std::uint64_t{parameters.sourceWordOffset},
                std::uint64_t{IndexedIndirectCommandSchema::wordCount})) {
            reject("RHI-ZERO-BASE-SOURCE-RANGE");
            return;
        }
        if (source.byteExtent > budget.maxSourceBytes || source.byteExtent > budget.maxSnapshotBytes
            || physicalSnapshotBytes > budget.maxSnapshotBytes - source.byteExtent) {
            reject("RHI-ZERO-BASE-SOURCE-BUDGET-EXCEEDED");
            return;
        }
        // Publish all snapshots/facts together; failures leave the object empty.
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
        if (const auto* id = input.copyLittleEndian(bytes)) {
            reject(id);
            return;
        }
        DrawIndexedIndirectCommand logical;
        if (const auto* id = IndexedIndirectCommandSchema::decode(bytes.data() + source.offsetBytes,
                Arguments::recordBytes,
                core::UnsignedElementSpan::ByteOrder::LittleEndian,
                logical)) {
            reject(id);
            return;
        }
        const auto lowered =
            semantics::IndirectInstanceBaseLowering::encode(logical, parameters.instanceCapacity);
        Arguments physical(
            Arguments::ExpectedGpuWords{IndexedIndirectCommandSchema::words(lowered.physical)},
            {1u, Arguments::recordBytes, budget.maxSnapshotBytes - source.byteExtent},
            indices);
        if (const auto* id = physical.diagnostic()) {
            reject(id);
            return;
        }
        source_ = std::move(bytes);
        physical_.emplace(std::move(physical));
        parameters_ = parameters;
        consumer_ = consumer;
        facts_ = {version,
            logical,
            lowered.physical,
            lowered.mapping,
            lowered.valid() ? Outcome::Produced : Outcome::FaultSuppressed};
    }

    ZeroBaseIndirectSourceConsumptionContract(
        const ZeroBaseIndirectSourceConsumptionContract&) = delete;
    ZeroBaseIndirectSourceConsumptionContract& operator=(
        const ZeroBaseIndirectSourceConsumptionContract&) = delete;

    ZeroBaseIndirectSourceConsumptionContract(
        ZeroBaseIndirectSourceConsumptionContract&& other) noexcept
        : source_(std::move(other.source_)), physical_(std::move(other.physical_)),
          parameters_(other.parameters_), consumer_(other.consumer_), facts_(other.facts_),
          diagnostic_(other.diagnostic_)
    {
        other.facts_ = {};
        other.diagnostic_ = "RHI-ZERO-BASE-SOURCE-MOVED";
    }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& sourceBytes() const noexcept { return source_; }

    const auto& parameters() const noexcept { return parameters_; }

    const auto& consumer() const noexcept { return consumer_; }

    std::uint64_t retainedBytes() const noexcept
    {
        return source_.size() + (physical_ ? physical_->retainedBytes() : 0u);
    }

    const char* observe(Facts& output) const noexcept
    {
        output = {};
        if (diagnostic_)
            return diagnostic_;
        output = facts_;
        return nullptr;
    }

    const char* validateConsumer(
        const BindlessIndirectInstanceConsumerLayout& consumer) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        return consumer.contractVersion == consumer_.contractVersion
                && consumer.binding == consumer_.binding
                && consumer.recordIndex == consumer_.recordIndex
                && consumer.instanceCapacity == consumer_.instanceCapacity
            ? nullptr
            : "RHI-ZERO-BASE-SOURCE-CONSUMER-MAPPING";
    }

    const char* validateParameters(const ZeroBaseIndirectProducerParams& parameters) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        return parameters.sourceWordOffset == parameters_.sourceWordOffset
                && parameters.instanceCapacity == parameters_.instanceCapacity
                && parameters.reserved == parameters_.reserved
                && parameters.contractVersion == parameters_.contractVersion
            ? nullptr
            : "RHI-ZERO-BASE-SOURCE-PARAMETERS";
    }

    const char* validateStream(const VertexInputConsumptionContract& vertex,
        std::uint32_t slot,
        std::uint64_t bytes,
        std::uint64_t offset) const noexcept
    {
        return diagnostic_ ? diagnostic_
                           : physical_->validateStream(0u, vertex, slot, bytes, offset);
    }

    // Shape/seal check only. Equal size does not establish byte/issuer identity.
    const char* validateSourceStorage(const GpuBuffer& buffer) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        if (!buffer.hostWritesSealed() || !buffer.gpuWritesSealed()
            || !buffer.gpuTransferWritesSealed())
            return "RHI-ZERO-BASE-SOURCE-WRITES-NOT-SEALED";
        return buffer.id().isValid() && buffer.desc().sizeBytes == source_.size()
                && buffer.desc().usage == ResourceUsage::StorageBuffer
                && buffer.state() == ResourceState::ShaderRead
            ? nullptr
            : "RHI-ZERO-BASE-SOURCE-STORAGE-DOMAIN";
    }

private:
    core::ByteBuffer source_{&core::defaultCpuMemoryResource()};
    std::optional<Arguments> physical_;
    ZeroBaseIndirectProducerParams parameters_;
    BindlessIndirectInstanceConsumerLayout consumer_;
    Facts facts_;
    const char* diagnostic_ = nullptr;
};

namespace semantics {
class ZeroBaseIndirectSourceLowering final {
public:
    static constexpr IndirectArgumentLoweringPlan current(Backend backend) noexcept
    {
        auto plan = IndirectArgumentLowering::current(backend);
        plan.contractVersion = ZeroBaseIndirectSourceConsumptionContract::version;
        if (backend != Backend::Null && backend != Backend::Vulkan && backend != Backend::Metal
            && backend != Backend::WebGPU) {
            plan.diagnosticId = "RHI-ZERO-BASE-SOURCE-BACKEND-UNKNOWN";
            return plan;
        }
        plan.diagnosticId = plan.validatorOnly ? "RHI-ZERO-BASE-SOURCE-VALIDATOR-ONLY"
                                               : "RHI-ZERO-BASE-SOURCE-PAYLOAD-MISSING";
        return plan;
    }
};
} // namespace semantics

// endregion zero_base_indirect_source

// region gpu_indirect_producer

inline constexpr std::uint32_t kGpuIndirectProducerContractVersion = 1u;

// L2 intent only. Selected binary proof must independently establish all words;
// neither this value nor packet/processor leases authorize GPU-produced bytes.
struct GpuIndirectProducerFootprint {
    enum class Operation { ComputeFixedIndexedRecord };
    std::uint32_t contractVersion = kGpuIndirectProducerContractVersion;
    Operation operation = Operation::ComputeFixedIndexedRecord;
    std::uint32_t set = 0u, binding = 0u;
    std::array<std::uint32_t, sizeof(DrawIndexedIndirectCommand) / sizeof(std::uint32_t)> words{};
};

// ProtectedIndirectOutputPreparation/1. Cold storage/expectation intent only;
// no writer, lease, consumer instance ABI or execution capability is granted.
struct GpuIndirectOutputPreparationBudget {
    std::uint32_t contractVersion = 0u;
    bool optIn = false;
    std::uint64_t maxOutputBytes = 0u;
    IndirectArgumentConsumptionContract::Budget expectation;
    std::uint32_t maxStreamRoots = 0u, maxChildOwners = 0u;

    const char* validate(std::size_t streamCount) const noexcept
    {
        if (contractVersion != 1u || !optIn || !maxOutputBytes || !expectation.maxRecords
            || !expectation.maxSourceBytes || !expectation.maxSnapshotBytes || !maxChildOwners)
            return "RHI-GPU-INDIRECT-OUTPUT-BUDGET";
        if (maxOutputBytes < sizeof(DrawIndexedIndirectCommand) || expectation.maxRecords < 1u
            || expectation.maxSourceBytes < sizeof(DrawIndexedIndirectCommand)
            || expectation.maxSnapshotBytes < sizeof(DrawIndexedIndirectCommand)
                    + sizeof(IndirectArgumentConsumptionContract::DrawObservation)
            || streamCount > maxStreamRoots)
            return "RHI-GPU-INDIRECT-OUTPUT-BUDGET-EXCEEDED";
        return nullptr;
    }
};

class GpuIndirectOutputPreparationContract final {
public:
    static constexpr std::uint32_t version = 1u;

    static ResourceUsage usage() noexcept
    {
        return ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer;
    }

    static const char* validateStorage(const BufferDesc& desc) noexcept
    {
        return desc.sizeBytes == sizeof(DrawIndexedIndirectCommand) && desc.usage == usage()
                && desc.memory == MemoryUsage::CpuToGpu
                && desc.initialState == ResourceState::ShaderWrite
            ? nullptr
            : "RHI-GPU-INDIRECT-OUTPUT-STORAGE-DOMAIN";
    }

    static const char* validateFetchSource(const GpuBuffer& buffer) noexcept
    {
        if (!buffer.hostWritesSealed() || !buffer.gpuWritesSealed()
            || !buffer.gpuTransferWritesSealed())
            return "RHI-GPU-INDIRECT-OUTPUT-FETCH-WRITES-NOT-SEALED";
        return buffer.desc().usage == ResourceUsage::VertexBuffer
                && buffer.state() == ResourceState::ShaderRead
            ? nullptr
            : "RHI-GPU-INDIRECT-OUTPUT-FETCH-DOMAIN";
    }
};

class GpuIndirectOutputRecordingContract final {
public:
    static constexpr std::uint32_t version = 1u;

    static const char* validateIdentity(std::uint64_t generation,
        std::uint64_t recording,
        std::uint64_t expectedGeneration,
        std::uint64_t expectedRecording) noexcept
    {
        return generation && recording && generation == expectedGeneration
                && recording == expectedRecording
            ? nullptr
            : "RHI-GPU-INDIRECT-OUTPUT-RECORDING-MISMATCH";
    }
};

class GpuIndirectProducerContract final {
public:
    static constexpr auto footprintBytes = sizeof(DrawIndexedIndirectCommand);

    GpuIndirectProducerContract(const PipelineLayoutDesc& layout,
        const ShaderReflectionDesc& reflection,
        GpuIndirectProducerFootprint footprint)
        : descriptors_(layout, reflection), footprint_(footprint)
    {
        if (footprint.contractVersion != kGpuIndirectProducerContractVersion) {
            diagnostic_ = "RHI-GPU-INDIRECT-PRODUCER-VERSION";
            return;
        }
        if (footprint.operation
            != GpuIndirectProducerFootprint::Operation::ComputeFixedIndexedRecord) {
            diagnostic_ = "RHI-GPU-INDIRECT-PRODUCER-OPERATION";
            return;
        }
        if (reflection.pushConstantByteCount || descriptors_.bindings().size() != 1u
            || !descriptors_.declaresSet(footprint.set)
            || descriptors_.declaresTable(footprint.set)) {
            diagnostic_ = "RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN";
            return;
        }
        const auto& selected = descriptors_.bindings().front();
        if (selected.set != footprint.set || selected.binding != footprint.binding
            || selected.type != DescriptorType::StorageBuffer || selected.arrayCount != 1u
            || selected.stages != ShaderStage::Compute) {
            diagnostic_ = "RHI-GPU-INDIRECT-PRODUCER-BINDING-DOMAIN";
            return;
        }
        if (!validatePipelineLayoutAgainstShaderReflection(layout, reflection).success) {
            diagnostic_ = "RHI-GPU-INDIRECT-PRODUCER-LAYOUT";
            return;
        }
        for (const auto& slot : layout.descriptorSets)
            if (slot.set == selected.set) {
                const auto* set = findPipelineDescriptorLayout(layout, slot.layoutName);
                const auto* binding = set ? findDescriptorBinding(*set, selected) : nullptr;
                if (!binding || binding->dynamicOffset || binding->tableBacked
                    || binding->stages != ShaderStage::Compute) {
                    diagnostic_ = "RHI-GPU-INDIRECT-PRODUCER-LAYOUT-DOMAIN";
                    return;
                }
            }
    }

    GpuIndirectProducerContract(const GpuIndirectProducerContract&) = delete;

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& footprint() const noexcept { return footprint_; }

    const char* validateBindingRange(
        std::uint64_t bytes, std::uint64_t offset, bool dynamic) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        return bytes == footprintBytes && !offset && !dynamic
            ? nullptr
            : "RHI-GPU-INDIRECT-PRODUCER-BUFFER-RANGE";
    }

    const char* validateDispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        return x == 1u && y == 1u && z == 1u ? nullptr : "RHI-GPU-INDIRECT-PRODUCER-DISPATCH";
    }

    BindlessResourceAccess producerAccess(GpuResourceId resource) const noexcept
    {
        if (diagnostic_ || !resource.isValid())
            return {};
        BindlessResourceAccess result;
        result.resource = resource;
        result.kind = BindlessAccessKind::Buffer;
        result.state = ResourceState::ShaderWrite;
        result.stages = ShaderStage::Compute;
        result.pipelineStages = PipelineStage::ComputeShader;
        result.access = ResourceAccess::ShaderWrite;
        result.bufferSize = footprintBytes;
        return result;
    }

    BindlessResourceAccess consumerAccess(GpuResourceId resource) const noexcept
    {
        auto result = producerAccess(resource);
        if (!result.resource.isValid())
            return {};
        result.state = ResourceState::ShaderRead;
        result.stages = ShaderStage::None;
        result.pipelineStages = PipelineStage::DrawIndirect;
        result.access = ResourceAccess::IndirectCommandRead;
        return result;
    }

    const char* validateScope(
        GpuResourceId resource, const BindlessResourceAccess& scope, bool producer) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        const auto required = producer ? producerAccess(resource) : consumerAccess(resource);
        if (!required.resource.isValid() || scope.resource != required.resource
            || scope.kind != required.kind || scope.state != required.state
            || scope.stages != required.stages || scope.pipelineStages != required.pipelineStages
            || scope.access != required.access || scope.bufferOffset
            || scope.bufferSize != required.bufferSize)
            return "RHI-GPU-INDIRECT-PRODUCER-SCOPE";
        return semantics::validateResourceAccessScope(
            {scope.state, scope.pipelineStages, scope.access});
    }

    // Structural intent only; issuer must prove real encoder/order/generation.
    const char* validateDependency(GpuResourceId resource,
        const ResourceBarrierDesc& barrier,
        const BindlessResourceAccess& consumer,
        bool producerEnded) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        if (!producerEnded)
            return "RHI-GPU-INDIRECT-PRODUCER-OPEN";
        if (const auto* id = validateScope(resource, consumer, false))
            return id;
        const auto producer = producerAccess(resource);
        ResourceMemoryBarrierDesc memory;
        memory.resource = barrier.resource;
        memory.sourceStages = barrier.sourceStages;
        memory.destinationStages = barrier.destinationStages;
        memory.sourceAccess = barrier.sourceAccess;
        memory.destinationAccess = barrier.destinationAccess;
        if (const auto* id = semantics::validateResourceMemoryBarrierDesc(memory, resource))
            return id;
        return barrier.before == producer.state && barrier.after == consumer.state
                && barrier.sourceStages == producer.pipelineStages
                && barrier.sourceAccess == producer.access
                && barrier.destinationStages == consumer.pipelineStages
                && barrier.destinationAccess == consumer.access
            ? nullptr
            : "RHI-GPU-INDIRECT-PRODUCER-DEPENDENCY";
    }

private:
    DescriptorConsumptionContract descriptors_;
    GpuIndirectProducerFootprint footprint_;
    const char* diagnostic_ = nullptr;
};

namespace semantics {
struct GpuIndirectProducerLoweringPlan {
    std::uint32_t contractVersion = kGpuIndirectProducerContractVersion;
    bool validatorOnly = false, executable = false;
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    const char* diagnosticId = "RHI-GPU-INDIRECT-PRODUCER-PAYLOAD-MISSING";
};

class GpuIndirectProducerLowering final {
public:
    static constexpr GpuIndirectProducerLoweringPlan current(Backend backend) noexcept
    {
        return semantics::CommandDrawLowering::GpuIndirectProducerLowering_current<
            GpuIndirectProducerLoweringPlan>(backend, kGpuIndirectProducerContractVersion);
    }
};
} // namespace semantics

// endregion gpu_indirect_producer

// region graphics_storage_write

inline constexpr std::uint32_t kGraphicsStorageWriteContractVersion = 1u;

// Describes intent, never shader proof or device authorization. A subsequent
// selected-entry binary verifier must establish every field independently.
struct GraphicsStorageWriteFootprint {
    enum class Operation { FragmentFixedUint32AtomicExchange };
    std::uint32_t contractVersion = kGraphicsStorageWriteContractVersion;
    Operation operation = Operation::FragmentFixedUint32AtomicExchange;
    std::uint32_t set = 0u;
    std::uint32_t binding = 0u;
    std::uint32_t replacement = 0u;
};

// Cold immutable L2 consumption projection. The initial domain is exactly one
// fragment-only scalar storage descriptor, atomically replacing uint element0
// with a literal constant. Atomic results cannot influence any shader output.
// Concurrent/repeated fragment invocations therefore have the same final value.
class GraphicsStorageWriteContract final {
public:
    static constexpr std::uint64_t footprintBytes = sizeof(std::uint32_t);

    GraphicsStorageWriteContract(const PipelineLayoutDesc& layout,
        const ShaderReflectionDesc& reflection,
        GraphicsStorageWriteFootprint footprint)
        : descriptors_(layout, reflection), footprint_(footprint)
    {
        if (footprint_.contractVersion != kGraphicsStorageWriteContractVersion) {
            diagnostic_ = "RHI-GRAPHICS-STORAGE-WRITE-VERSION";
            return;
        }
        if (footprint_.operation
            != GraphicsStorageWriteFootprint::Operation::FragmentFixedUint32AtomicExchange) {
            diagnostic_ = "RHI-GRAPHICS-STORAGE-WRITE-DOMAIN";
            return;
        }
        const ShaderReflectionBinding* selected = nullptr;
        for (const auto& binding : descriptors_.bindings()) {
            if (binding.set == footprint_.set && binding.binding == footprint_.binding) {
                if (selected) {
                    diagnostic_ = "RHI-GRAPHICS-STORAGE-WRITE-BINDING-DUPLICATE";
                    return;
                }
                selected = &binding;
            }
            if ((binding.type == DescriptorType::StorageBuffer
                    || binding.type == DescriptorType::StorageTexture)
                && (binding.set != footprint_.set || binding.binding != footprint_.binding)) {
                diagnostic_ = "RHI-GRAPHICS-STORAGE-WRITE-OTHER-WRITER";
                return;
            }
        }
        if (!selected || !descriptors_.declaresSet(footprint_.set)
            || descriptors_.declaresTable(footprint_.set)
            || selected->type != DescriptorType::StorageBuffer || selected->arrayCount != 1u
            || selected->stages != ShaderStage::Fragment) {
            diagnostic_ = "RHI-GRAPHICS-STORAGE-WRITE-BINDING-DOMAIN";
            return;
        }
        if (!validatePipelineLayoutAgainstShaderReflection(layout, reflection).success) {
            diagnostic_ = "RHI-GRAPHICS-STORAGE-WRITE-LAYOUT-MISMATCH";
            return;
        }
        for (const auto& slot : layout.descriptorSets) {
            if (slot.set != footprint_.set)
                continue;
            const auto* set = findPipelineDescriptorLayout(layout, slot.layoutName);
            const auto* binding = set ? findDescriptorBinding(*set, *selected) : nullptr;
            if (!binding || binding->dynamicOffset || binding->tableBacked
                || binding->stages != ShaderStage::Fragment) {
                diagnostic_ = "RHI-GRAPHICS-STORAGE-WRITE-LAYOUT-DOMAIN";
                return;
            }
        }
    }

    GraphicsStorageWriteContract(const GraphicsStorageWriteContract&) = delete;
    GraphicsStorageWriteContract& operator=(const GraphicsStorageWriteContract&) = delete;
    GraphicsStorageWriteContract(GraphicsStorageWriteContract&&) noexcept = default;

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& footprint() const noexcept { return footprint_; }

    const auto& descriptors() const noexcept { return descriptors_; }

    const char* validateBindingRange(
        std::uint64_t bufferBytes, std::uint64_t bindingOffset, bool dynamicOffset) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        if (bindingOffset || dynamicOffset)
            return "RHI-GRAPHICS-STORAGE-WRITE-OFFSET-DOMAIN";
        return core::containsUnsignedRange(
                   std::uint64_t{0}, bufferBytes, std::uint64_t{0}, footprintBytes)
            ? nullptr
            : "RHI-GRAPHICS-STORAGE-WRITE-BUFFER-RANGE";
    }

    const char* validateDraw(std::uint64_t vertexCount,
        std::uint64_t instanceCount,
        bool indexed,
        bool indirect) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        return vertexCount && instanceCount == 1u && !indexed && !indirect
            ? nullptr
            : "RHI-GRAPHICS-STORAGE-WRITE-DRAW-DOMAIN";
    }

    BindlessResourceAccess requiredAccess(GpuResourceId resource) const noexcept
    {
        if (diagnostic_ || !resource.isValid())
            return {};
        BindlessResourceAccess result;
        result.resource = resource;
        result.kind = BindlessAccessKind::Buffer;
        result.stages = ShaderStage::Fragment;
        result.pipelineStages = PipelineStage::FragmentShader;
        // Exchange is read-modify-write even though its result is unused.
        result.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        result.state = ResourceState::ShaderWrite;
        result.bufferSize = footprintBytes;
        return result;
    }

    ValidationResult validateProducerScope(
        GpuResourceId resource, const BindlessAccessDeclarations& declarations) const
    {
        if (diagnostic_)
            return {false, diagnostic_};
        const auto required = requiredAccess(resource);
        const auto covered = validateBindlessBufferAccess(required, declarations);
        if (!covered.success)
            return covered;
        std::size_t matches = 0u;
        for (std::size_t i = 0; i < declarations.size(); ++i) {
            BindlessResourceAccess access;
            if (!declarations.read(i, access))
                return {false, "RHI-BINDLESS-ACCESS-SET-INVALID"};
            if (access.resource != resource)
                continue;
            if (++matches != 1u)
                return {false, "RHI-GRAPHICS-STORAGE-WRITE-ALIAS"};
            if (const auto* id = semantics::validateResourceAccessScope(
                    {access.state, access.pipelineStages, access.access}))
                return {false, id};
            if (access.kind != required.kind || access.stages != required.stages
                || access.pipelineStages != required.pipelineStages
                || access.access != required.access || access.state != required.state
                || access.bufferOffset || access.bufferSize != footprintBytes)
                return {false, "RHI-GRAPHICS-STORAGE-WRITE-SCOPE-MISMATCH"};
        }
        return {true, {}};
    }

    // Value validation only: native recording must also prove producer encoder
    // ended, order, resource state/identity, payload and completion-owned owners.
    const char* validateDependency(GpuResourceId resource,
        const ResourceBarrierDesc& barrier,
        const BindlessResourceAccess& consumer,
        bool producerEnded) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        if (!producerEnded)
            return "RHI-GRAPHICS-STORAGE-WRITE-PRODUCER-OPEN";
        if (!resource.isValid() || consumer.resource != resource
            || consumer.kind != BindlessAccessKind::Buffer || consumer.bufferOffset
            || consumer.bufferSize != footprintBytes)
            return "RHI-GRAPHICS-STORAGE-WRITE-CONSUMER-RANGE";
        if (const auto* id = semantics::validateResourceAccessScope(
                {consumer.state, consumer.pipelineStages, consumer.access}))
            return id;
        const auto shaderStages =
            ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute;
        if ((consumer.stages & shaderStages) != consumer.stages)
            return "RHI-GRAPHICS-STORAGE-WRITE-CONSUMER-DOMAIN";
        // v1 supports a subsequent shader read or transfer read only. Neither
        // an indirect consumer nor another writer is authorized.
        if (!((consumer.state == ResourceState::CopySrc
                  && consumer.pipelineStages == PipelineStage::Transfer
                  && consumer.access == ResourceAccess::TransferRead
                  && consumer.stages == ShaderStage::None)
                || (consumer.state == ResourceState::ShaderRead
                    && consumer.access == ResourceAccess::ShaderRead
                    && consumer.stages != ShaderStage::None
                    && consumer.pipelineStages
                        == bindlessPipelineStagesForShader(consumer.stages))))
            return "RHI-GRAPHICS-STORAGE-WRITE-CONSUMER-DOMAIN";
        ResourceMemoryBarrierDesc dependency;
        dependency.resource = barrier.resource;
        dependency.sourceStages = barrier.sourceStages;
        dependency.destinationStages = barrier.destinationStages;
        dependency.sourceAccess = barrier.sourceAccess;
        dependency.destinationAccess = barrier.destinationAccess;
        if (const auto* id = semantics::validateResourceMemoryBarrierDesc(dependency, resource))
            return id;
        const auto producer = requiredAccess(resource);
        return barrier.before == producer.state && barrier.after == consumer.state
                && barrier.sourceStages == producer.pipelineStages
                && barrier.sourceAccess == producer.access
                && barrier.destinationStages == consumer.pipelineStages
                && barrier.destinationAccess == consumer.access
            ? nullptr
            : "RHI-GRAPHICS-STORAGE-WRITE-DEPENDENCY-MISMATCH";
    }

private:
    DescriptorConsumptionContract descriptors_;
    GraphicsStorageWriteFootprint footprint_;
    const char* diagnostic_ = nullptr;
};

namespace semantics {

struct GraphicsStorageWriteLoweringPlan {
    bool validatorOnly = false;
    bool executable = false;
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    const char* diagnosticId = "RHI-GRAPHICS-STORAGE-WRITE-PAYLOAD-MISSING";
};

// Versioned current implementation conclusions, never inferred native support.
// No capability boolean or caller-provided sizeof(T) can open execution.
class GraphicsStorageWriteLowering final {
public:
    static GraphicsStorageWriteLoweringPlan current(Backend backend) noexcept
    {
        return semantics::CommandDrawLowering::GraphicsStorageWriteLowering_current<
            GraphicsStorageWriteLoweringPlan>(backend);
    }
};

} // namespace semantics

// endregion graphics_storage_write

// region instance_attribute_fetch

// InstanceAttributeFetch/1: the draw's firstInstance is the first ordinal of
// every Instance-rate location stream; one element advances per instance.
struct InstanceAttributeFetchIntent {
    std::uint32_t contractVersion = 0u;
    bool optIn = false;
    enum class InputAbi { LocationFloatOnly };
    InputAbi inputAbi = InputAbi::LocationFloatOnly;
    std::uint32_t stepRate = 1u;
};

class InstanceAttributeFetchContract final {
public:
    static const char* validate(const InstanceAttributeFetchIntent& intent,
        const VertexInputConsumptionContract& input) noexcept
    {
        if (intent.contractVersion != 1u || !intent.optIn)
            return "RHI-INSTANCE-ATTRIBUTE-FETCH-VERSION-OPT-IN";
        if (intent.inputAbi != InstanceAttributeFetchIntent::InputAbi::LocationFloatOnly
            || intent.stepRate != 1u)
            return "RHI-INSTANCE-ATTRIBUTE-FETCH-ABI-DOMAIN";
        return input.diagnostic();
    }
};

namespace semantics {
struct InstanceAttributeFetchLoweringPlan {
    std::uint32_t contractVersion = 1u;
    bool validatorOnly = false, executable = false;
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    const char* diagnosticId = "RHI-INSTANCE-ATTRIBUTE-FETCH-PAYLOAD-MISSING";
};

class InstanceAttributeFetchLowering final {
public:
    static constexpr InstanceAttributeFetchLoweringPlan current(Backend backend) noexcept
    {
        return semantics::CommandDrawLowering::InstanceAttributeFetchLowering_current<
            InstanceAttributeFetchLoweringPlan>(backend);
    }
};
} // namespace semantics
} // namespace arp::rhi

// endregion instance_attribute_fetch
