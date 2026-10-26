#pragma once

#include "core/math/unsigned_range.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_resource_ownership.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/semantics/metal_vertex_stream_lowering.h"

#include <cstdint>

// region Metal buffer binding facts
namespace arp::rhi_metal {

class MetalRhiOrdinaryGraphicsResourceDomain final {
public:
    static bool isReadOnlyBuffer(rhi::DescriptorType type) noexcept
    {
        return type == rhi::DescriptorType::UniformBuffer
            || type == rhi::DescriptorType::ReadOnlyStorageBuffer;
    }

    static bool isReadOnlyBufferArray(rhi::DescriptorType type, std::uint32_t count) noexcept
    {
        return isReadOnlyBuffer(type) && count > 1u;
    }

    static bool allowsDescriptor(rhi::DescriptorType type, std::uint32_t count, bool depth) noexcept
    {
        return !depth && count
            && (type == rhi::DescriptorType::SampledTexture || type == rhi::DescriptorType::Sampler
                || isReadOnlyBuffer(type));
    }
};

// Actual Metal pipeline-reflected buffer arguments; zero means unknown.
struct MetalRhiBufferBindingFact {
    std::uint32_t slot = UINT32_MAX;
    std::uint64_t alignment = 0u;
    std::uint64_t requiredBytes = 0u;
    std::uint32_t access = UINT32_MAX;
    std::uint32_t elementType = UINT32_MAX;
    // Actual MTLArgument.isActive query: 0/1; UINT32_MAX is unobserved.
    std::uint32_t active = UINT32_MAX;

    bool matches(const MetalRhiBufferBindingFact& other) const noexcept
    {
        return slot == other.slot && alignment == other.alignment
            && requiredBytes == other.requiredBytes && access == other.access
            && elementType == other.elementType && active == other.active;
    }
};

struct MetalRhiStageBufferBindingFacts {
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    std::uint32_t version = 0u;
    core::pmr::Vector<MetalRhiBufferBindingFact> entries{&core::defaultCpuMemoryResource()};
};

// Actual pointer member ABI, independent of the outer argument-buffer layout.
// Numeric access/type values are retained backend-private; the collector proves
// readonly, non-argument-buffer pointees before publishing version1.
struct MetalRhiBufferPointeeFact {
    // Specified MTLBindingAccessReadOnly value, independently checked so a
    // normalized boolean cannot let writable access masquerade as readonly.
    static constexpr std::uint32_t readOnlyAccess = 0u;
    std::uint32_t slot = UINT32_MAX, argumentIndex = UINT32_MAX;
    std::uint32_t arrayCount = 0u, argumentIndexStride = 0u, element = UINT32_MAX;
    std::uint32_t access = UINT32_MAX, elementType = UINT32_MAX;
    std::uint64_t alignment = 0u, requiredBytes = 0u;
    bool readOnly = false, argumentBuffer = false;

    bool matches(const MetalRhiBufferPointeeFact& other) const noexcept
    {
        return slot == other.slot && argumentIndex == other.argumentIndex
            && arrayCount == other.arrayCount && argumentIndexStride == other.argumentIndexStride
            && element == other.element && access == other.access
            && elementType == other.elementType && alignment == other.alignment
            && requiredBytes == other.requiredBytes && readOnly == other.readOnly
            && argumentBuffer == other.argumentBuffer;
    }
};

struct MetalRhiStageBufferPointeeFacts {
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    std::uint32_t version = 0u;
    const char* queryDiagnostic = "RHI-METAL-ORDINARY-POINTEE-FACTS-MISSING";
    core::pmr::Vector<MetalRhiBufferPointeeFact> entries{&core::defaultCpuMemoryResource()};
};

struct MetalRhiStageBufferPointeeRange {
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    MetalRhiBufferPointeeFact fact;
    std::uint64_t offset = 0u;
    MetalRhiNativeResourceObservation nativeIdentity;
};

class MetalRhiStageBufferPointeeRangeValidator final {
public:
    static const char* capture(const MetalRhiStageBufferPointeeFacts& facts,
        rhi::ShaderStage stage,
        std::uint32_t slot,
        std::uint32_t index,
        std::uint32_t count,
        std::uint32_t element,
        std::uint64_t offset,
        std::uint64_t bytes,
        MetalRhiStageBufferPointeeRange& result) noexcept
    {
        result = {};
        if (facts.stage != stage
            || (stage != rhi::ShaderStage::Vertex && stage != rhi::ShaderStage::Fragment))
            return "RHI-METAL-ORDINARY-POINTEE-STAGE-MISMATCH";
        if (facts.version != 1u)
            return facts.queryDiagnostic ? facts.queryDiagnostic
                                         : "RHI-METAL-ORDINARY-POINTEE-FACTS-MISSING";
        const MetalRhiBufferPointeeFact* found = nullptr;
        for (const auto& fact : facts.entries)
            if (fact.slot == slot && fact.argumentIndex == index) {
                if (found)
                    return "RHI-METAL-ORDINARY-POINTEE-FACTS-DUPLICATE";
                found = &fact;
            }
        if (!found || !found->alignment || !found->requiredBytes || found->elementType == UINT32_MAX
            || found->access == UINT32_MAX)
            return "RHI-METAL-ORDINARY-POINTEE-FACTS-MISSING";
        if (!found->readOnly || found->access != MetalRhiBufferPointeeFact::readOnlyAccess
            || found->argumentBuffer)
            return "RHI-METAL-ORDINARY-POINTEE-DOMAIN-UNSUPPORTED";
        if (!count || element >= count || found->arrayCount != count || found->element != element
            || found->argumentIndexStride != 1u)
            return "RHI-METAL-ORDINARY-POINTEE-MAP-MISMATCH";
        // Current sealed ordinary maps enumerate contiguous argument indices.
        // A different reflected stride is explicit failure, never normalization.
        if (offset % found->alignment)
            return "RHI-METAL-ORDINARY-POINTEE-ALIGNMENT";
        if (!core::containsUnsignedRange(std::uint64_t{0u}, bytes, offset, found->requiredBytes))
            return "RHI-METAL-ORDINARY-POINTEE-RANGE";
        result = {stage, *found, offset};
        return nullptr;
    }

    static const char* validate(const MetalRhiStageBufferPointeeFacts& facts,
        rhi::ShaderStage stage,
        std::uint32_t slot,
        std::uint32_t index,
        std::uint32_t count,
        std::uint32_t element,
        std::uint64_t offset,
        std::uint64_t bytes,
        const MetalRhiStageBufferPointeeRange& captured) noexcept
    {
        MetalRhiStageBufferPointeeRange current;
        if (const auto* id =
                capture(facts, stage, slot, index, count, element, offset, bytes, current))
            return id;
        return captured.stage == current.stage && captured.fact.matches(current.fact)
                && captured.offset == offset
            ? nullptr
            : "RHI-METAL-ORDINARY-POINTEE-CAPTURE-MISMATCH";
    }
};

struct MetalRhiStageBufferRange {
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    MetalRhiBufferBindingFact fact;
    std::uint64_t offset = 0u;
    MetalRhiNativeResourceObservation nativeIdentity;
};

inline const char* validateMetalRhiBufferOffset(
    const core::pmr::Vector<MetalRhiBufferBindingFact>& facts,
    std::uint32_t version,
    std::uint32_t slot,
    std::uint64_t offset,
    std::uint64_t bufferBytes,
    const MetalRhiBufferBindingFact*& resolved) noexcept
{
    resolved = nullptr;
    if (version != 1u)
        return "RHI-METAL-ORDINARY-OFFSET-FACTS-MISSING";
    for (const auto& fact : facts) {
        if (fact.slot != slot)
            continue;
        if (resolved) {
            resolved = nullptr;
            return "RHI-METAL-ORDINARY-OFFSET-FACTS-DUPLICATE";
        }
        resolved = &fact;
    }
    if (!resolved || !resolved->alignment || !resolved->requiredBytes)
        return "RHI-METAL-ORDINARY-OFFSET-FACTS-MISSING";
    if (offset % resolved->alignment)
        return "RHI-METAL-ORDINARY-OFFSET-ALIGNMENT";
    if (!core::containsUnsignedRange(
            std::uint64_t{0u}, bufferBytes, offset, resolved->requiredBytes))
        return "RHI-METAL-ORDINARY-OFFSET-RANGE";
    return nullptr;
}

// Independently queried Graphics stage ABI footprints for direct buffers.
class MetalRhiStageBufferRangeValidator final {
public:
    static const char* capture(const MetalRhiStageBufferBindingFacts& facts,
        rhi::ShaderStage stage,
        std::uint32_t slot,
        std::uint64_t offset,
        std::uint64_t bufferBytes,
        MetalRhiStageBufferRange& result) noexcept
    {
        if (facts.stage != stage
            || (stage != rhi::ShaderStage::Vertex && stage != rhi::ShaderStage::Fragment))
            return "RHI-METAL-ORDINARY-OFFSET-FACTS-STAGE-MISMATCH";
        const MetalRhiBufferBindingFact* fact = nullptr;
        if (const auto* id = validateMetalRhiBufferOffset(
                facts.entries, facts.version, slot, offset, bufferBytes, fact))
            return id;
        result = {stage, *fact, offset};
        return nullptr;
    }

    static const char* validate(const MetalRhiStageBufferBindingFacts& facts,
        rhi::ShaderStage stage,
        std::uint32_t slot,
        std::uint64_t offset,
        std::uint64_t bufferBytes,
        const MetalRhiStageBufferRange& captured) noexcept
    {
        MetalRhiStageBufferRange current;
        if (const auto* id = capture(facts, stage, slot, offset, bufferBytes, current))
            return id;
        if (captured.stage != current.stage || captured.fact.slot != current.fact.slot
            || captured.fact.alignment != current.fact.alignment
            || captured.fact.requiredBytes != current.fact.requiredBytes
            || captured.fact.access != current.fact.access
            || captured.fact.elementType != current.fact.elementType
            || captured.fact.active != current.fact.active || captured.offset != current.offset)
            return "RHI-METAL-ORDINARY-OFFSET-CAPTURE-MISMATCH";
        return nullptr;
    }
};

// Exact queried scalar ABI; callers supply their approved numeric native ABI.
class MetalRhiScalarBufferAbiValidator final {
public:
    static const char* validate(const core::pmr::Vector<MetalRhiBufferBindingFact>& facts,
        std::uint32_t version,
        std::uint32_t slot,
        std::uint64_t bytes,
        std::uint32_t access,
        std::uint32_t elementType,
        std::uint64_t minimumBytes,
        const MetalRhiBufferBindingFact*& resolved,
        const char* domainDiagnostic) noexcept
    {
        resolved = nullptr;
        if (const auto* id =
                validateMetalRhiBufferOffset(facts, version, slot, 0u, bytes, resolved)) {
            resolved = nullptr;
            return id;
        }
        if (resolved->access != access || resolved->elementType != elementType
            || resolved->requiredBytes != minimumBytes) {
            resolved = nullptr;
            return domainDiagnostic;
        }
        return nullptr;
    }
};

// Query-only acceptance for the proved direct uint32 fragment atomic domain.
// Constants are specified MTLBindingAccessReadWrite and MTLDataTypeUInt values.
// ABI facts are not a shader footprint, hazard proof or execution capability.
class MetalRhiWritableUint32BufferAbiValidator final {
public:
    static constexpr std::uint32_t readWriteAccess = 1u, uintElementType = 33u;

    static const char* validate(const core::pmr::Vector<MetalRhiBufferBindingFact>& facts,
        std::uint32_t version,
        std::uint32_t slot,
        std::uint64_t bufferBytes,
        const MetalRhiBufferBindingFact*& resolved,
        const char* domainDiagnostic = "RHI-METAL-WRITABLE-UINT32-BUFFER-ABI-DOMAIN") noexcept
    {
        return MetalRhiScalarBufferAbiValidator::validate(facts,
            version,
            slot,
            bufferBytes,
            readWriteAccess,
            uintElementType,
            sizeof(std::uint32_t),
            resolved,
            domainDiagnostic);
    }
};

class MetalRhiAtomicBufferAbiValidator final {
public:
    static constexpr auto readWriteAccess =
        MetalRhiWritableUint32BufferAbiValidator::readWriteAccess;
    static constexpr auto uintElementType =
        MetalRhiWritableUint32BufferAbiValidator::uintElementType;

    static const char* validate(const MetalRhiStageBufferBindingFacts& facts,
        std::uint32_t slot,
        std::uint64_t bufferBytes,
        const MetalRhiBufferBindingFact*& resolved) noexcept
    {
        resolved = nullptr;
        if (facts.stage != rhi::ShaderStage::Fragment)
            return "RHI-METAL-ATOMIC-BUFFER-ABI-STAGE";
        return MetalRhiWritableUint32BufferAbiValidator::validate(facts.entries,
            facts.version,
            slot,
            bufferBytes,
            resolved,
            "RHI-METAL-ATOMIC-BUFFER-ABI-DOMAIN");
    }
};

// endregion Metal buffer binding facts

// region Metal texture binding facts

// Backend-private values copied from actual render/compute pipeline reflection.
// UINT32_MAX argumentIndex denotes a direct texture slot, not an array member.
struct MetalRhiTextureBindingFact {
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    std::uint32_t slot = UINT32_MAX, argumentIndex = UINT32_MAX;
    std::uint32_t textureType = UINT32_MAX, scalarDataType = UINT32_MAX;
    bool depth = false;
    std::uint32_t access = UINT32_MAX, active = UINT32_MAX;

    bool matches(const MetalRhiTextureBindingFact& other) const noexcept
    {
        return stage == other.stage && slot == other.slot && argumentIndex == other.argumentIndex
            && textureType == other.textureType && scalarDataType == other.scalarDataType
            && depth == other.depth && access == other.access && active == other.active;
    }
};

// MetalOriginalSampledTexture2DFloatABI/1. Numeric values are specified Metal
// ABI and independently checked against SDK enums by actual native tests.
// This checks declaration ABI only, never original resource/view identity.
class MetalRhiOriginalSampledTexture2DFloatAbi final {
public:
    static const char* inspect(const MetalRhiTextureBindingFact& fact, bool depth) noexcept
    {
        if (fact.argumentIndex != UINT32_MAX || fact.textureType != 2u || fact.scalarDataType != 3u
            || fact.access != 0u || fact.active > 1u)
            return "RHI-METAL-ORIGINAL-SAMPLED-TEXTURE-ABI";
        return fact.depth == depth ? nullptr : "RHI-METAL-ORIGINAL-SAMPLED-TEXTURE-DEPTH-ABI";
    }
};

// endregion Metal texture binding facts

// region Metal vertex input facts

// Descriptor-creation and function-input query facts, not driver queries on a
// finished pipeline. SDK enum values are versioned and asserted in native tests.
class MetalRhiVertexInputFacts final {
public:
    // Metal vertex descriptor arrays expose indices 0..30 (SDK contract).
    static constexpr std::uint32_t version = 1u, perVertex = 1u, perInstance = 2u,
                                   attributeCapacity = 31u;

    struct Stream {
        std::uint32_t slot = UINT32_MAX, physicalSlot = UINT32_MAX, stepFunction = 0u;
        std::uint64_t stride = 0u, stepRate = 0u;

        bool operator==(const Stream& b) const noexcept
        {
            return slot == b.slot && physicalSlot == b.physicalSlot
                && stepFunction == b.stepFunction && stride == b.stride && stepRate == b.stepRate;
        }
    };

    struct Attribute {
        std::uint32_t location = UINT32_MAX, physicalSlot = UINT32_MAX, format = 0u;
        std::uint64_t offset = 0u;

        bool operator==(const Attribute& b) const noexcept
        {
            return location == b.location && physicalSlot == b.physicalSlot && format == b.format
                && offset == b.offset;
        }
    };

    struct ShaderInput {
        std::uint32_t location = UINT32_MAX, type = 0u;
        bool active = false;

        bool operator==(const ShaderInput& b) const noexcept
        {
            return location == b.location && type == b.type && active == b.active;
        }
    };

    std::uint32_t contractVersion = 0u;
    core::pmr::Vector<Stream> streams{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Attribute> attributes{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ShaderInput> shaderInputs{&core::defaultCpuMemoryResource()};
    MetalRhiVertexInputFacts() = default;

    MetalRhiVertexInputFacts(const MetalRhiVertexInputFacts& source)
        : contractVersion(source.contractVersion),
          streams(source.streams, &core::defaultCpuMemoryResource()),
          attributes(source.attributes, &core::defaultCpuMemoryResource()),
          shaderInputs(source.shaderInputs, &core::defaultCpuMemoryResource())
    {}

    MetalRhiVertexInputFacts& operator=(const MetalRhiVertexInputFacts&) = default;
    MetalRhiVertexInputFacts(MetalRhiVertexInputFacts&&) noexcept = default;
    MetalRhiVertexInputFacts& operator=(MetalRhiVertexInputFacts&&) noexcept = default;

    bool operator==(const MetalRhiVertexInputFacts& b) const noexcept
    {
        return contractVersion == b.contractVersion && streams == b.streams
            && attributes == b.attributes && shaderInputs == b.shaderInputs;
    }

    static std::uint32_t nativeFormat(rhi::VertexFormat format) noexcept
    {
        switch (format) {
        case rhi::VertexFormat::Float32x1:
            return 28u;
        case rhi::VertexFormat::Float32x2:
            return 29u;
        case rhi::VertexFormat::Float32x3:
            return 30u;
        case rhi::VertexFormat::Float32x4:
            return 31u;
        }
        return 0u;
    }

    static std::uint32_t nativeShaderType(rhi::VertexFormat format) noexcept
    {
        switch (format) {
        case rhi::VertexFormat::Float32x1:
            return 3u;
        case rhi::VertexFormat::Float32x2:
            return 4u;
        case rhi::VertexFormat::Float32x3:
            return 5u;
        case rhi::VertexFormat::Float32x4:
            return 6u;
        }
        return 0u;
    }

    const char* validate(const rhi::VertexInputConsumptionContract& input,
        const rhi::semantics::MetalVertexStreamMapping& mapping) const noexcept
    {
        if (contractVersion != version)
            return "RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-MISSING";
        if (input.diagnostic())
            return input.diagnostic();
        if (!mapping.success || streams.size() != input.streams().size()
            || attributes.size() != input.attributes().size()
            || shaderInputs.size() != attributes.size())
            return "RHI-METAL-INSTANCE-ATTRIBUTE-FACTS-COUNT-MAPPING";
        for (std::size_t i = 0u; i < streams.size(); ++i) {
            const auto& fact = streams[i];
            const auto* stream = input.findStream(fact.slot);
            if (!stream || fact.slot >= mapping.capacity
                || mapping.slots[fact.slot] != fact.physicalSlot
                || fact.physicalSlot >= mapping.capacity || fact.stride != stream->strideBytes
                || fact.stepRate != 1u
                || fact.stepFunction
                    != (stream->rate == rhi::VertexInputRate::Instance ? perInstance : perVertex))
                return "RHI-METAL-INSTANCE-ATTRIBUTE-STREAM-ABI";
            for (std::size_t j = 0u; j < i; ++j)
                if (streams[j].slot == fact.slot || streams[j].physicalSlot == fact.physicalSlot)
                    return "RHI-METAL-INSTANCE-ATTRIBUTE-STREAM-DUPLICATE";
        }
        for (std::size_t i = 0u; i < attributes.size(); ++i) {
            const auto& fact = attributes[i];
            const auto* attribute = input.findAttribute(fact.location);
            if (!attribute || attribute->slot >= mapping.capacity
                || fact.physicalSlot != mapping.slots[attribute->slot]
                || fact.offset != attribute->layout.offsetBytes
                || fact.format != nativeFormat(attribute->layout.format))
                return "RHI-METAL-INSTANCE-ATTRIBUTE-ATTRIBUTE-ABI";
            for (std::size_t j = 0u; j < i; ++j)
                if (attributes[j].location == fact.location)
                    return "RHI-METAL-INSTANCE-ATTRIBUTE-ATTRIBUTE-DUPLICATE";
        }
        for (std::size_t i = 0u; i < shaderInputs.size(); ++i) {
            const auto& fact = shaderInputs[i];
            const auto* attribute = input.findAttribute(fact.location);
            if (!attribute || !fact.active
                || fact.type != nativeShaderType(attribute->layout.format))
                return "RHI-METAL-INSTANCE-ATTRIBUTE-SHADER-ABI";
            for (std::size_t j = 0u; j < i; ++j)
                if (shaderInputs[j].location == fact.location)
                    return "RHI-METAL-INSTANCE-ATTRIBUTE-SHADER-DUPLICATE";
        }
        return nullptr;
    }
};
} // namespace arp::rhi_metal

// endregion Metal vertex input facts
