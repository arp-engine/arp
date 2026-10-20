#pragma once
#include "core/base64.h"
#include "core/shared_owner_projection.h"
#include "core/unsigned_element_span.h"
#include "core/unsigned_range.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/command_draw_validation.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/resource_texture_extent.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"
#include "rhi/semantics/bindless_preparation_lowering.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

// region bindless_image_resource
namespace arp::rhi {
// Versioned logical resource domain, extracted from the existing Metal image
// specialization. Acceptance proves no backend capability or native ownership.
class BindlessScalarImageResourceDomain final {
public:
    static constexpr std::uint32_t version = 1u;

    static bool accepts(DescriptorType type,
        TextureFormat format,
        TextureViewDimension dimension,
        TextureAspect aspect,
        std::uint32_t samples,
        std::uint32_t layers) noexcept
    {
        const auto expected = type == DescriptorType::SampledTexture ? TextureFormat::Rgba8Unorm
            : type == DescriptorType::StorageTexture                 ? TextureFormat::R32Float
                                                                     : TextureFormat::Unknown;
        return expected != TextureFormat::Unknown && format == expected
            && dimension == TextureViewDimension::Texture2D && aspect == TextureAspect::Color
            && samples == 1u && layers == 1u;
    }
};

// Stable synchronous preparation facts. Backends must derive these from actual
// registered resource/view/parent metadata and independently prove liveness and
// native owners; callers cannot turn these values into an execution ticket.
struct BindlessTextureInventoryResource {
    std::uint32_t contractVersion = 0u;
    GpuResourceId view, parent;
    TextureFormat format = TextureFormat::Unknown, parentFormat = TextureFormat::Unknown;
    TextureUsage usage = TextureUsage::None, parentUsage = TextureUsage::None;
    TextureViewDimension dimension = TextureViewDimension::Texture2D;
    TextureSubresourceRange range;
    Extent3D parentExtent{0u, 0u, 0u};
    std::uint32_t parentMipLevelCount = 0u, parentArrayLayerCount = 0u, sampleCount = 0u;
    ResourceState state = ResourceState::Undefined;

    static const char* capture(const GpuTexture& texture,
        const GpuTextureView& textureView,
        BindlessTextureInventoryResource& output) noexcept
    {
        output = {};
        BindlessTextureInventoryResource resource;
        const auto& parent = texture.desc();
        const auto& view = textureView.desc();
        if (textureView.textureId() != texture.id())
            return "RHI-BINDLESS-TEXTURE-INVENTORY-IDENTITY";
        resource.contractVersion = BindlessScalarImageResourceDomain::version;
        resource.view = textureView.id();
        resource.parent = texture.id();
        resource.format = view.format;
        resource.parentFormat = parent.format;
        resource.usage = view.usage;
        resource.parentUsage = parent.usage;
        resource.dimension = view.dimension;
        resource.range = {view.baseMipLevel,
            view.mipLevelCount,
            view.baseArrayLayer,
            view.arrayLayerCount,
            view.aspect};
        resource.parentExtent = parent.extent;
        resource.parentMipLevelCount = parent.mipLevelCount;
        resource.parentArrayLayerCount = parent.arrayLayerCount;
        resource.sampleCount = parent.sampleCount;
        resource.state = texture.subresourceState(resource.range);
        if (const auto* id = resource.diagnostic())
            return id;
        output = resource;
        return nullptr;
    }

    bool sameParent(const BindlessTextureInventoryResource& b) const noexcept
    {
        return parent == b.parent && parentFormat == b.parentFormat && parentUsage == b.parentUsage
            && parentExtent.width == b.parentExtent.width
            && parentExtent.height == b.parentExtent.height
            && parentExtent.depth == b.parentExtent.depth
            && parentMipLevelCount == b.parentMipLevelCount
            && parentArrayLayerCount == b.parentArrayLayerCount && sampleCount == b.sampleCount;
    }

    bool matches(const BindlessTextureInventoryResource& b) const noexcept
    {
        return contractVersion == b.contractVersion && view == b.view && sameParent(b)
            && format == b.format && usage == b.usage && dimension == b.dimension
            && range == b.range && state == b.state;
    }

    const char* diagnostic() const noexcept
    {
        if (contractVersion != BindlessScalarImageResourceDomain::version || !view.isValid()
            || !parent.isValid() || view == parent)
            return "RHI-BINDLESS-TEXTURE-INVENTORY-IDENTITY";
        if (!BindlessScalarImageResourceDomain::accepts(DescriptorType::SampledTexture,
                format,
                dimension,
                range.aspect,
                sampleCount,
                range.arrayLayerCount)
            || parentFormat != format || parentArrayLayerCount != 1u)
            return "RHI-BINDLESS-TEXTURE-INVENTORY-DOMAIN";
        if ((usage & TextureUsage::Sampled) == TextureUsage::None
            || (parentUsage & TextureUsage::Sampled) == TextureUsage::None)
            return "RHI-BINDLESS-TEXTURE-INVENTORY-USAGE";
        if (!parentExtent.width || !parentExtent.height || parentExtent.depth != 1u
            || range.mipLevelCount != 1u
            || !core::containsUnsignedRange(
                std::uint32_t{0u}, parentMipLevelCount, range.baseMipLevel, range.mipLevelCount)
            || !core::containsUnsignedRange(std::uint32_t{0u},
                parentArrayLayerCount,
                range.baseArrayLayer,
                range.arrayLayerCount))
            return "RHI-BINDLESS-TEXTURE-INVENTORY-RANGE";
        return state == ResourceState::ShaderRead ? nullptr
                                                  : "RHI-BINDLESS-TEXTURE-INVENTORY-STATE";
    }
};

// endregion bindless_image_resource

// region bindless_indirect_packet_storage

// Logical protected storage only. Backend registry/issuer/native bytes and
// privileged producer/consumer/completion authority remain independent.
class BindlessIndirectPacketStorageContract final {
public:
    static constexpr std::uint32_t version = 1u;
    // Frozen at construction; never a packet promotion or public draw ticket.
    enum class Issuance { PublicConsumer, PrototypeStorageOnly, PrototypeClosedConsumerCold };

    static constexpr bool knownIssuance(Issuance purpose) noexcept
    {
        return purpose == Issuance::PublicConsumer || purpose == Issuance::PrototypeStorageOnly
            || purpose == Issuance::PrototypeClosedConsumerCold;
    }

    static constexpr const char* publicConsumerDiagnostic(Issuance purpose) noexcept
    {
        switch (purpose) {
        case Issuance::PublicConsumer:
            return nullptr;
        case Issuance::PrototypeStorageOnly:
            return "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-STORAGE-ONLY";
        case Issuance::PrototypeClosedConsumerCold:
            return "RHI-BINDLESS-INDIRECT-PACKET-PROTOTYPE-CLOSED-CONSUMER-COLD";
        }
        return "RHI-BINDLESS-INDIRECT-PACKET-ISSUANCE-UNKNOWN";
    }

    static constexpr const char* closedConsumerStructuralDiagnostic(Issuance purpose) noexcept
    {
        return purpose == Issuance::PrototypeClosedConsumerCold ? nullptr
                                                                : publicConsumerDiagnostic(purpose);
    }

    struct ClosedConsumerLowering {
        bool validatorOnly = false, prototypeColdIntent = false, executable = false;
        const char* diagnosticId = "RHI-CLOSED-CONSUMER-PACKET-ISSUANCE-BACKEND-UNKNOWN";
    };

    // A construction intent only. Private image/pipeline/ownership validation
    // must precede allocation; this value is not native payload evidence.
    static constexpr ClosedConsumerLowering closedConsumerLowering(
        Backend backend, std::uint32_t contractVersion, bool optIn) noexcept
    {
        if (contractVersion != version || !optIn)
            return {false, false, false, "RHI-CLOSED-CONSUMER-PACKET-ISSUANCE-OPT-IN"};
        return semantics::BindlessPreparationLowering::
            BindlessIndirectPacketStorage_closedConsumerLowering<ClosedConsumerLowering>(backend);
    }
    enum class Role { PhysicalArgs, InstanceMapping, Parameters };

    struct Descriptor {
        std::string_view label;
        std::uint64_t sizeBytes = 0u;
        ResourceUsage usage = ResourceUsage::None;
        MemoryUsage memory = MemoryUsage::CpuToGpu;
        ResourceState initialState = ResourceState::Undefined;
    };

    using Buffers = std::array<const GpuBuffer*, 3>;

    struct Facts {
        std::uint32_t contractVersion = 0u;
        std::array<GpuResourceId, 3> resources{};
        std::uint64_t reportedBufferBytes = 0u;
    };

    static constexpr std::array<Role, 3> roles{
        Role::PhysicalArgs, Role::InstanceMapping, Role::Parameters};
    static constexpr std::uint64_t minimumBytes = sizeof(DrawIndexedIndirectCommand)
        + sizeof(IndirectInstanceBase) + sizeof(ZeroBaseIndirectProducerParams);
    using ParameterBytes = std::array<std::uint8_t, sizeof(ZeroBaseIndirectProducerParams)>;

    struct ColdBudget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxPacketBytes = 0u;
        std::uint32_t maxChildOwners = 0u;
    };

    struct ColdLowering {
        bool validatorOnly = false;
        bool prototypeStorage = false;
        bool consumerIssued = false;
        const char* diagnosticId = "RHI-BINDLESS-COLD-PACKET-BACKEND-UNKNOWN";
    };

    static constexpr ColdLowering coldLowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::BindlessIndirectPacketStorage_coldLowering<
            ColdLowering>(backend);
    }

    static const char* encodeParameters(
        const ZeroBaseIndirectProducerParams& parameters, ParameterBytes& bytes) noexcept
    {
        bytes = {};
        if (parameters.contractVersion != version || parameters.reserved
            || !parameters.instanceCapacity)
            return "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS";
        const std::array<std::uint32_t, 4> words{parameters.sourceWordOffset,
            parameters.instanceCapacity,
            parameters.reserved,
            parameters.contractVersion};
        core::UnsignedElementSpan::encodeWordsLittleEndian(words, bytes);
        return nullptr;
    }

    static Descriptor descriptor(Role role) noexcept
    {
        const auto copies = ResourceUsage::CopySrc | ResourceUsage::CopyDst;
        switch (role) {
        case Role::PhysicalArgs:
            return {"BindlessIndirectPhysicalArgs",
                sizeof(DrawIndexedIndirectCommand),
                ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer | copies,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderWrite};
        case Role::InstanceMapping:
            return {"BindlessIndirectInstanceMappings",
                sizeof(IndirectInstanceBase),
                ResourceUsage::StorageBuffer | copies,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderWrite};
        case Role::Parameters:
            return {"BindlessIndirectProducerParameters",
                sizeof(ZeroBaseIndirectProducerParams),
                ResourceUsage::UniformBuffer | copies,
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderRead};
        }
        return {};
    }

    static const char* verify(const Buffers& buffers,
        const ZeroBaseIndirectProducerParams& parameters,
        std::uint64_t reportedBytes,
        std::uint64_t byteCeiling,
        Facts& facts) noexcept
    {
        facts = {};
        if (parameters.contractVersion != version || parameters.reserved
            || !parameters.instanceCapacity)
            return "RHI-BINDLESS-INDIRECT-PACKET-PARAMETERS";
        if (byteCeiling < minimumBytes || reportedBytes > byteCeiling)
            return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING";
        Facts pending;
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            const auto* buffer = buffers[i];
            if (!buffer || !buffer->id().isValid())
                return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-IDENTITY";
            for (std::size_t previous = 0u; previous < i; ++previous)
                if (buffer->id() == pending.resources[previous])
                    return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-IDENTITY";
            const auto expected = descriptor(roles[i]);
            const auto& desc = buffer->desc();
            if (desc.sizeBytes != expected.sizeBytes || desc.usage != expected.usage
                || desc.memory != expected.memory || desc.initialState != expected.initialState)
                return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-DOMAIN";
            if (!buffer->hostWritesSealed() || !buffer->gpuWritesSealed()
                || !buffer->gpuTransferWritesSealed())
                return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-WRITES-NOT-SEALED";
            const auto bytes = buffer->allocation().estimatedSizeBytes;
            if (bytes < expected.sizeBytes || bytes > byteCeiling - pending.reportedBufferBytes)
                return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING";
            pending.resources[i] = buffer->id();
            pending.reportedBufferBytes += bytes;
        }
        if (pending.reportedBufferBytes != reportedBytes)
            return "RHI-BINDLESS-INDIRECT-PACKET-STORAGE-ACCOUNTING";
        pending.contractVersion = version;
        facts = pending;
        return nullptr;
    }
};

// endregion bindless_indirect_packet_storage

// region canonical_zero_base_indirect

// Value binding to compiler-owned canonical packages. BindlessShaderPackage's
// private construction/load boundary proves exact source + complete compiled
// package digests; no handwritten artifact or name-only proof enters this API.
// Native source-byte/ABI/issuer/consumer/completion proof is still required.
class CanonicalZeroBaseIndirectSourceContract final {
public:
    static constexpr std::uint32_t version = 1u;
    static constexpr core::StringView selectedEntry = "arpZeroBaseIndirectMain";
    enum class Role : std::size_t {
        SnapshotMetadata,
        FaultLatch,
        LogicalSource,
        PhysicalArguments,
        InstanceMapping,
        Parameters
    };
    static constexpr std::size_t bindingCount = 6u;

    struct Binding {
        std::uint32_t set = 0u, binding = 0u;
        DescriptorType type = DescriptorType::UniformBuffer;
        std::uint64_t requiredBytes = 0u;
        BindlessBufferLayout elementLayout;
    };

    struct Facts {
        std::uint32_t contractVersion = 0u;
        std::uint64_t producerPackageIdentity = 0u;
        BindlessExecution execution = BindlessExecution::DiagnosticFailure;
        std::array<Binding, bindingCount> bindings{};
        ZeroBaseIndirectSourceConsumptionContract::Facts source;
        ZeroBaseIndirectProducerParams parameters{0u, 0u, 0u, 0u};
        BindlessIndirectInstanceConsumerLayout consumer;
    };

    static const char* verify(const BindlessShaderPackage& package,
        const ZeroBaseIndirectSourceConsumptionContract& source,
        core::StringView entry,
        Facts& output) noexcept
    {
        output = {};
        Facts facts;
        if (const auto* id = source.observe(facts.source))
            return id;
        const auto& artifact = package.artifact();
        if (artifact.zeroBaseIndirectProducerContractVersion != version
            || artifact.indirectInstanceBaseContractVersion != version
            || artifact.faultReportingContractVersion != version
            || artifact.activePrefixContractVersion != 2u
            || artifact.indirectInstanceConsumer.contractVersion || !artifact.shaderPackageIdentity
            || (artifact.execution != BindlessExecution::Native
                && artifact.execution != BindlessExecution::Emulated))
            return "RHI-CANONICAL-INDIRECT-SOURCE-PACKAGE-DOMAIN";
        if (entry != selectedEntry || package.modules().size() != 1u
            || package.modules().front().entry != entry)
            return "RHI-CANONICAL-INDIRECT-SOURCE-ENTRY";
        const auto& reflection = package.modules().front().reflection.reflection;
        if (reflection.entryPoints.size() != 1u || reflection.entryPoints.front().name != entry
            || reflection.entryPoints.front().stage != shader::ShaderStage::Compute
            || reflection.entryPoints.front().computeWorkgroupSizeX != 1u
            || reflection.entryPoints.front().computeWorkgroupSizeY != 1u
            || reflection.entryPoints.front().computeWorkgroupSizeZ != 1u
            || reflection.pushConstantSizeBytes)
            return "RHI-CANONICAL-INDIRECT-SOURCE-ENTRY";
        if (artifact.snapshotMetadata.contractVersion != version
            || !artifact.snapshotMetadata.wordCount
            || artifact.snapshotMetadata.stages != ShaderStage::Compute
            || artifact.faultLatch.contractVersion != version
            || artifact.faultLatch.stages != ShaderStage::Compute || artifact.tables.size() != 4u
            || artifact.physicalLayout.bindings.size() != bindingCount
            || reflection.bindings.size() != bindingCount)
            return "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT";
        const auto bind = [&](Role role,
                              std::uint32_t binding,
                              DescriptorType type,
                              std::uint64_t bytes,
                              BindlessBufferLayout layout = {}) noexcept {
            const auto* physical = findDescriptorSetBinding(artifact.physicalLayout, binding);
            if (!physical || physical->type != type || physical->stages != ShaderStage::Compute
                || physical->arrayCount != 1u || physical->dynamicOffset || !bytes)
                return false;
            const shader::ShaderBinding* reflected = nullptr;
            for (const auto& candidate : reflection.bindings)
                if (candidate.set == artifact.set && candidate.binding == binding) {
                    if (reflected)
                        return false;
                    reflected = &candidate;
                }
            if (!reflected || reflected->arrayCount != 1u
                || reflected->stageMask != static_cast<std::uint32_t>(ShaderStage::Compute)
                || descriptorTypeFromShaderBinding(reflected->type) != type)
                return false;
            for (const auto& captured : facts.bindings)
                if (captured.requiredBytes && captured.set == artifact.set
                    && captured.binding == binding)
                    return false;
            facts.bindings[static_cast<std::size_t>(role)] = {
                artifact.set, binding, type, bytes, layout};
            return true;
        };
        if (!bind(Role::SnapshotMetadata,
                artifact.snapshotMetadata.binding,
                DescriptorType::ReadOnlyStorageBuffer,
                std::uint64_t{artifact.snapshotMetadata.wordCount} * sizeof(std::uint32_t))
            || !bind(Role::FaultLatch,
                artifact.faultLatch.binding,
                DescriptorType::StorageBuffer,
                sizeof(std::uint32_t)))
            return "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT";

        // Table IDs/operations are normative ZeroBaseIndirectProducer/1 roles,
        // from the closed generator schema, never native slot assumptions.
        struct TableRole {
            std::uint32_t id;
            Role role;
            DescriptorType type;
            BindlessOperation operations;
            std::uint64_t bytes;
        };

        const std::array<TableRole, 4u> roles{
            {{1u,
                 Role::LogicalSource,
                 DescriptorType::ReadOnlyStorageBuffer,
                 BindlessOperation::Load | BindlessOperation::Query,
                 source.sourceBytes().size()},
                {2u,
                    Role::PhysicalArguments,
                    DescriptorType::StorageBuffer,
                    BindlessOperation::Store,
                    sizeof(DrawIndexedIndirectCommand)},
                {3u,
                    Role::InstanceMapping,
                    DescriptorType::StorageBuffer,
                    BindlessOperation::Store,
                    sizeof(IndirectInstanceBase)},
                {4u,
                    Role::Parameters,
                    DescriptorType::UniformBuffer,
                    BindlessOperation::Load,
                    sizeof(ZeroBaseIndirectProducerParams)}}};
        for (const auto& expected : roles) {
            const BindlessTableMapping* table = nullptr;
            for (const auto& candidate : artifact.tables)
                if (candidate.tableId == expected.id) {
                    if (table)
                        return "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT";
                    table = &candidate;
                }
            const auto stride = expected.role == Role::Parameters
                ? sizeof(ZeroBaseIndirectProducerParams)
                : sizeof(std::uint32_t);
            if (!table || table->capacity != 1u || table->operations != expected.operations
                || !table->nonUniform || table->samplerTableId || !table->metadataWordOffset
                || !table->bufferLayout.identity || table->bufferLayout.elementStrideBytes != stride
                || table->bufferLayout.elementAlignmentBytes != stride
                || table->bufferLayout.minBindingSizeBytes != stride
                || !bind(expected.role,
                    table->firstBinding,
                    expected.type,
                    expected.bytes,
                    table->bufferLayout))
                return "RHI-CANONICAL-INDIRECT-SOURCE-LAYOUT";
        }
        facts.contractVersion = version;
        facts.producerPackageIdentity = artifact.shaderPackageIdentity;
        facts.execution = artifact.execution;
        facts.parameters = source.parameters();
        facts.consumer = source.consumer();
        output = facts;
        return nullptr;
    }
};

// endregion canonical_zero_base_indirect

// region canonical_zero_base_indirect_consumer

// ClosedImageMappingConsumer/1 value/package correspondence. Exact source and
// complete package provenance are established by the private package loader.
// These facts issue no native consumer generation, snapshot or execution plan.
class CanonicalZeroBaseIndirectConsumerContract final {
public:
    static constexpr std::uint32_t version = 1u, capacity = 3u, mappingBinding = 7u;
    static constexpr core::StringView vertexEntry = "arpZeroBaseIndirectConsumerVertex";
    static constexpr core::StringView fragmentEntry = "arpZeroBaseIndirectConsumerFragment";
    static constexpr std::size_t metadataWordCount = 4u + 4u + capacity;

    struct Binding {
        std::uint32_t set = 0u, binding = 0u, arrayCount = 0u;
        DescriptorType type = DescriptorType::UniformBuffer;
        ShaderStage stages = ShaderStage::None;
        std::uint64_t requiredBytes = 0u;
    };

    struct Facts {
        std::uint32_t contractVersion = 0u, bindingCount = 0u;
        std::uint64_t consumerPackageIdentity = 0u;
        BindlessExecution execution = BindlessExecution::DiagnosticFailure;
        BindlessIndirectInstanceConsumerLayout consumer;
        std::array<Binding, 3u + capacity> bindings{};
    };

    struct Lowering {
        bool validatorOnly = false, executable = false;
        const char* diagnosticId = "RHI-CANONICAL-INDIRECT-CONSUMER-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::CanonicalZeroBaseIndirectConsumer_lowering<
            Lowering>(backend);
    }

    static const char* verify(const BindlessShaderPackage& package,
        const ZeroBaseIndirectSourceConsumptionContract& source,
        Facts& output) noexcept
    {
        output = {};
        Facts facts;
        if (const auto* id = source.diagnostic())
            return id;
        const auto& artifact = package.artifact();
        if (artifact.zeroBaseIndirectConsumerContractVersion != version
            || artifact.zeroBaseIndirectProducerContractVersion
            || artifact.indirectInstanceBaseContractVersion != version
            || artifact.activePrefixContractVersion != 2u
            || artifact.faultReportingContractVersion != version || !artifact.shaderPackageIdentity
            || (artifact.execution != BindlessExecution::Native
                && artifact.execution != BindlessExecution::Emulated))
            return "RHI-CANONICAL-INDIRECT-CONSUMER-PACKAGE-DOMAIN";
        if (const auto* id = source.validateConsumer(artifact.indirectInstanceConsumer))
            return id;
        if (artifact.indirectInstanceConsumer.contractVersion != version
            || artifact.indirectInstanceConsumer.binding != mappingBinding
            || artifact.indirectInstanceConsumer.recordIndex
            || artifact.indirectInstanceConsumer.instanceCapacity != capacity
            || artifact.snapshotBytes
                != metadataWordCount * sizeof(std::uint32_t) + 2u * sizeof(std::uint32_t)
            || artifact.set || artifact.tables.size() != 1u
            || artifact.snapshotMetadata.contractVersion != version
            || artifact.snapshotMetadata.binding != 1u
            || artifact.snapshotMetadata.wordCount != metadataWordCount
            || artifact.snapshotMetadata.stages != ShaderStage::Fragment
            || artifact.faultLatch.contractVersion != version || artifact.faultLatch.binding != 2u
            || artifact.faultLatch.stages != ShaderStage::Fragment)
            return "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT";
        const auto& table = artifact.tables.front();
        if (table.tableId != 1u || table.capacity != capacity || table.firstBinding != 3u
            || table.metadataWordOffset != 4u || table.operations != BindlessOperation::Load
            || !table.nonUniform || table.samplerTableId || table.bufferLayout.identity
            || table.bufferLayout.elementStrideBytes || table.bufferLayout.elementAlignmentBytes
            || table.bufferLayout.minBindingSizeBytes)
            return "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT";
        if (package.modules().size() != 2u)
            return "RHI-CANONICAL-INDIRECT-CONSUMER-ENTRY";
        for (std::size_t i = 0u; i < package.modules().size(); ++i) {
            const auto& module = package.modules()[i];
            const auto& reflection = module.reflection.reflection;
            const auto entry = i == 0u ? vertexEntry : fragmentEntry;
            const auto stage =
                i == 0u ? shader::ShaderStage::Vertex : shader::ShaderStage::Fragment;
            if (module.entry != entry || reflection.entryPoints.size() != 1u
                || reflection.entryPoints.front().name != entry
                || reflection.entryPoints.front().stage != stage
                || reflection.pushConstantSizeBytes)
                return "RHI-CANONICAL-INDIRECT-CONSUMER-ENTRY";
            if ((i == 0u
                    && (reflection.vertexAttributes.size() != 1u
                        || reflection.vertexAttributes.front().location
                        || reflection.vertexAttributes.front().format
                            != shader::VertexAttributeFormat::Float2))
                || (i == 1u && !reflection.vertexAttributes.empty()))
                return "RHI-CANONICAL-INDIRECT-CONSUMER-VERTEX-INTERFACE";
        }
        const bool native = artifact.execution == BindlessExecution::Native;
        const auto count = 3u + (native ? 1u : capacity);
        if (artifact.physicalLayout.bindings.size() != count)
            return "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT";
        const auto bind = [&](std::uint32_t binding,
                              DescriptorType type,
                              ShaderStage stage,
                              std::uint32_t arrayCount,
                              std::uint64_t bytes) noexcept {
            const auto* physical = findDescriptorSetBinding(artifact.physicalLayout, binding);
            if (!physical || physical->type != type || physical->stages != stage
                || physical->arrayCount != arrayCount || physical->dynamicOffset)
                return false;
            const auto& reflection =
                package.modules()[stage == ShaderStage::Vertex ? 0u : 1u].reflection.reflection;
            const shader::ShaderBinding* reflected = nullptr;
            for (const auto& candidate : reflection.bindings)
                if (candidate.set == artifact.set && candidate.binding == binding) {
                    if (reflected)
                        return false;
                    reflected = &candidate;
                }
            if (!reflected || reflected->arrayCount != arrayCount
                || reflected->stageMask != static_cast<std::uint32_t>(stage)
                || descriptorTypeFromShaderBinding(reflected->type) != type)
                return false;
            facts.bindings[facts.bindingCount++] = {
                artifact.set, binding, arrayCount, type, stage, bytes};
            return true;
        };
        if (!bind(mappingBinding,
                DescriptorType::ReadOnlyStorageBuffer,
                ShaderStage::Vertex,
                1u,
                sizeof(IndirectInstanceBase))
            || !bind(1u,
                DescriptorType::ReadOnlyStorageBuffer,
                ShaderStage::Fragment,
                1u,
                metadataWordCount * sizeof(std::uint32_t))
            || !bind(2u,
                DescriptorType::StorageBuffer,
                ShaderStage::Fragment,
                1u,
                sizeof(std::uint32_t)))
            return "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT";
        for (std::uint32_t i = 0u; i < (native ? 1u : capacity); ++i)
            if (!bind(3u + i,
                    DescriptorType::SampledTexture,
                    ShaderStage::Fragment,
                    native ? capacity : 1u,
                    0u))
                return "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT";
        if (package.modules()[0].reflection.reflection.bindings.size() != 1u
            || package.modules()[1].reflection.reflection.bindings.size() != count - 1u)
            return "RHI-CANONICAL-INDIRECT-CONSUMER-LAYOUT";
        facts.contractVersion = version;
        facts.consumerPackageIdentity = artifact.shaderPackageIdentity;
        facts.execution = artifact.execution;
        facts.consumer = artifact.indirectInstanceConsumer;
        output = facts;
        return nullptr;
    }
};

// endregion canonical_zero_base_indirect_consumer

// region canonical_consumer_image_content

// Immutable authoring/upload values; supplied revision IDs are not GPU proof.
// Actual native upload, exclusive content lifetime and completion are separate.
class CanonicalConsumerImageContentContract final : public core::SharedObjectIdentity {
public:
    static constexpr std::uint32_t version = 1u;

    struct Input {
        BindlessTextureInventoryResource image;
        Extent3D extent{0u, 0u, 0u};
        std::uint32_t bytesPerRow = 0u, rowsPerImage = 0u;
        const void* bytes = nullptr;
        std::size_t byteCount = 0u;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxSourceBytes = 0u, maxSnapshotBytes = 0u;
    };

    struct Revision {
        std::uint32_t contractVersion = 0u;
        GpuResourceId view, parent;
        std::uint64_t revision = 0u;

        bool operator==(const Revision& b) const noexcept
        {
            return contractVersion == b.contractVersion && view == b.view && parent == b.parent
                && revision == b.revision;
        }

        bool valid() const noexcept
        {
            return contractVersion == version && view.isValid() && parent.isValid()
                && view != parent && revision;
        }
    };

    struct Lowering {
        bool validatorOnly = false, executable = false;
        const char* diagnosticId = "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::CanonicalConsumerImageContent_lowering<
            Lowering>(backend);
    }

    // Shared explicit full single-slice upload layout; resource identity and
    // format-domain admission remain separate. Never guesses or repacks rows.
    static const char* validatePayload(Extent3D extent,
        TextureFormat format,
        std::uint32_t pitch,
        std::uint32_t rows,
        const void* bytes,
        std::size_t count,
        Budget budget,
        std::uint64_t& outputBytes) noexcept
    {
        outputBytes = 0u;
        if (!extent.width || !extent.height || extent.depth != 1u || !bytesPerPixel(format)
            || rows != extent.height || !pitch || pitch % kTextureUploadRowPitchAlignment
            || std::uint64_t{extent.width} * bytesPerPixel(format) > pitch)
            return "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-LAYOUT";
        const auto size = std::uint64_t{pitch} * rows;
        if (!bytes || !size || size > std::numeric_limits<std::size_t>::max()
            || size > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())
            || size != count)
            return "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTES";
        if (size > budget.maxSourceBytes || size > budget.maxSnapshotBytes)
            return "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BYTE-BUDGET";
        outputBytes = size;
        return nullptr;
    }

    CanonicalConsumerImageContentContract(Input input, Budget budget)
    {
        if (budget.contractVersion != version || !budget.prototypeOptIn || !budget.maxSourceBytes
            || !budget.maxSnapshotBytes) {
            diagnostic_ = "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-BUDGET";
            return;
        }
        if ((diagnostic_ = input.image.diagnostic()))
            return;
        if ((input.image.parentUsage & TextureUsage::CopyDst) == TextureUsage::None) {
            diagnostic_ = "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-COPY-USAGE";
            return;
        }
        Extent3D mip;
        if ((diagnostic_ = semantics::TextureSubresourceExtent::resolve(input.image.parentExtent,
                 input.image.parentMipLevelCount,
                 input.image.range.baseMipLevel,
                 mip)))
            return;
        if (input.extent.width != mip.width || input.extent.height != mip.height
            || input.extent.depth != mip.depth) {
            diagnostic_ = "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-LAYOUT";
            return;
        }
        std::uint64_t size = 0u;
        if ((diagnostic_ = validatePayload(mip,
                 input.image.format,
                 input.bytesPerRow,
                 input.rowsPerImage,
                 input.bytes,
                 input.byteCount,
                 budget,
                 size)))
            return;
        core::ByteBuffer snapshot{&core::defaultCpuMemoryResource()};
        const auto* begin = static_cast<const std::uint8_t*>(input.bytes);
        snapshot.assign(begin, begin + input.byteCount);
        bytes_ = std::move(snapshot);
        image_ = input.image;
        extent_ = mip;
        pitch_ = input.bytesPerRow;
        rows_ = input.rowsPerImage;
    }

    CanonicalConsumerImageContentContract(const CanonicalConsumerImageContentContract&) = delete;
    CanonicalConsumerImageContentContract& operator=(
        const CanonicalConsumerImageContentContract&) = delete;

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& image() const noexcept { return image_; }

    const auto& bytes() const noexcept { return bytes_; }

    auto extent() const noexcept { return extent_; }

    auto bytesPerRow() const noexcept { return pitch_; }

    auto rowsPerImage() const noexcept { return rows_; }

    const char* validate(
        const BindlessTextureInventoryResource& image, Revision revision) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        if (const auto* id = image.diagnostic())
            return id;
        if (!image_.matches(image))
            return "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-ORIGINAL";
        return revision.valid() && revision.view == image_.view && revision.parent == image_.parent
            ? nullptr
            : "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-REVISION";
    }

    bool sameContent(const CanonicalConsumerImageContentContract& other) const noexcept
    {
        return !diagnostic_ && !other.diagnostic_ && image_.matches(other.image_)
            && pitch_ == other.pitch_ && rows_ == other.rows_ && bytes_ == other.bytes_;
    }

    // Exact existing upload descriptor. Resource generation is a structural
    // value here; this method grants no writable resource or native ticket.
    const char* describeUpload(
        Revision revision, std::uint64_t frame, UploadTextureDesc& output) const noexcept
    {
        output = {};
        if (const auto* id = validate(image_, revision))
            return id;
        if (!frame || bytes_.size() > UINT32_MAX)
            return "RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-UPLOAD-RANGE";
        output.destinationTexture = image_.parent;
        output.extent = extent_;
        output.destinationOffset = {0u, 0u, 0u};
        output.format = image_.format;
        output.bytesPerRow = pitch_;
        output.rowsPerImage = rows_;
        output.mipLevel = image_.range.baseMipLevel;
        output.arrayLayer = image_.range.baseArrayLayer;
        output.subresources = image_.range;
        output.pixelData = bytes_.data();
        output.pixelDataSize = static_cast<std::uint32_t>(bytes_.size());
        output.stagingIntent = StagingIntent::Upload;
        output.backendOwnedStaging = true;
        output.frameSerial = frame;
        output.resourceGeneration = revision.revision;
        return nullptr;
    }

private:
    core::ByteBuffer bytes_{&core::defaultCpuMemoryResource()};
    BindlessTextureInventoryResource image_;
    Extent3D extent_{0u, 0u, 0u};
    std::uint32_t pitch_ = 0u, rows_ = 0u;
    const char* diagnostic_ = nullptr;
};

// endregion canonical_consumer_image_content

// region canonical_zero_base_indirect_consumer_geometry

// Immutable bit/range values only. Native upload, original index/pipeline
// ownership, packet generation and execution require independent admission.
class CanonicalZeroBaseIndirectConsumerGeometryContract final : public core::SharedObjectIdentity {
public:
    using Consumer = CanonicalZeroBaseIndirectConsumerContract;
    using Source = ZeroBaseIndirectSourceConsumptionContract;
    using Order = core::UnsignedElementSpan::ByteOrder;
    static constexpr std::uint32_t version = 1u;
    static constexpr std::uint64_t strideBytes = 2u * sizeof(std::uint32_t);
    enum class Encoding { Unknown, Ieee754Binary32 };

    struct Input {
        const void* data = nullptr;
        std::size_t bytes = 0u;
        Encoding encoding = Encoding::Unknown;
        Order order = Order::Unknown;
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxGeometryBytes = 0u, maxSnapshotBytes = 0u;
    };

    struct Lowering {
        bool validatorOnly = false, executable = false;
        const char* diagnosticId = "RHI-CANONICAL-CONSUMER-GEOMETRY-BACKEND-UNKNOWN";
    };

    struct ColdLowering {
        bool validatorOnly = false, prototypeUpload = false;
        const char* diagnosticId = "RHI-CANONICAL-CONSUMER-GEOMETRY-COLD-BACKEND-UNKNOWN";
    };

    static constexpr ColdLowering coldLowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::
            CanonicalZeroBaseIndirectConsumerGeometry_coldLowering<ColdLowering>(backend);
    }

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::
            CanonicalZeroBaseIndirectConsumerGeometry_lowering<Lowering>(backend);
    }

    CanonicalZeroBaseIndirectConsumerGeometryContract(const BindlessShaderPackage& package,
        const Source& source,
        const VertexInputConsumptionContract& vertex,
        Input input,
        Budget budget)
    {
        if (budget.contractVersion != version || !budget.prototypeOptIn || !budget.maxGeometryBytes
            || !budget.maxSnapshotBytes) {
            diagnostic_ = "RHI-CANONICAL-CONSUMER-GEOMETRY-BUDGET";
            return;
        }
        Consumer::Facts consumer;
        if ((diagnostic_ = Consumer::verify(package, source, consumer)))
            return;
        if ((diagnostic_ = validateLayout(vertex)))
            return;
        if (input.encoding != Encoding::Ieee754Binary32 || !input.data || !input.bytes
            || input.bytes % strideBytes) {
            diagnostic_ = "RHI-CANONICAL-CONSUMER-GEOMETRY-ENCODING";
            return;
        }
        const core::UnsignedElementSpan positions(
            input.data, input.bytes, core::UnsignedElementSpan::Width::Uint32, input.order);
        if ((diagnostic_ = positions.diagnostic()))
            return;
        if (input.bytes > budget.maxGeometryBytes || input.bytes > budget.maxSnapshotBytes
            || source.sourceBytes().size() > budget.maxSnapshotBytes - input.bytes) {
            diagnostic_ = "RHI-CANONICAL-CONSUMER-GEOMETRY-SNAPSHOT-BUDGET";
            return;
        }
        const auto slot = vertex.streams().front().slot;
        if ((diagnostic_ = source.validateStream(vertex, slot, input.bytes, 0u)))
            return;
        Source::Facts sourceFacts;
        if ((diagnostic_ = source.observe(sourceFacts)))
            return;
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()},
            sourceBytes{&core::defaultCpuMemoryResource()};
        if ((diagnostic_ = positions.copyLittleEndian(bytes)))
            return;
        sourceBytes = source.sourceBytes();
        positions_ = std::move(bytes);
        sourceBytes_ = std::move(sourceBytes);
        slot_ = slot;
        packageIdentity_ = consumer.consumerPackageIdentity;
        parameters_ = source.parameters();
        sourceFacts_ = sourceFacts;
    }

    CanonicalZeroBaseIndirectConsumerGeometryContract(
        const CanonicalZeroBaseIndirectConsumerGeometryContract&) = delete;
    CanonicalZeroBaseIndirectConsumerGeometryContract& operator=(
        const CanonicalZeroBaseIndirectConsumerGeometryContract&) = delete;

    CanonicalZeroBaseIndirectConsumerGeometryContract(
        CanonicalZeroBaseIndirectConsumerGeometryContract&& other) noexcept
        : positions_(std::move(other.positions_)), sourceBytes_(std::move(other.sourceBytes_)),
          slot_(other.slot_), packageIdentity_(other.packageIdentity_),
          parameters_(other.parameters_), sourceFacts_(other.sourceFacts_),
          diagnostic_(other.diagnostic_)
    {
        other.packageIdentity_ = 0u;
        other.diagnostic_ = "RHI-CANONICAL-CONSUMER-GEOMETRY-MOVED";
    }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& bytes() const noexcept { return positions_; }

    auto slot() const noexcept { return slot_; }

    auto packageIdentity() const noexcept { return packageIdentity_; }

    std::uint64_t retainedBytes() const noexcept
    {
        return positions_.size() + std::uint64_t{sourceBytes_.size()};
    }

    const char* validate(const BindlessShaderPackage& package,
        const Source& source,
        const VertexInputConsumptionContract& vertex) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        Consumer::Facts current;
        if (const auto* id = Consumer::verify(package, source, current))
            return id;
        if (current.consumerPackageIdentity != packageIdentity_
            || source.sourceBytes() != sourceBytes_)
            return "RHI-CANONICAL-CONSUMER-GEOMETRY-CORRESPONDENCE";
        if (const auto* id = source.validateParameters(parameters_))
            return id;
        Source::Facts currentSource;
        if (const auto* id = source.observe(currentSource))
            return id;
        if (currentSource.contractVersion != sourceFacts_.contractVersion
            || currentSource.outcome != sourceFacts_.outcome
            || IndexedIndirectCommandSchema::words(currentSource.logical)
                != IndexedIndirectCommandSchema::words(sourceFacts_.logical)
            || IndexedIndirectCommandSchema::words(currentSource.physical)
                != IndexedIndirectCommandSchema::words(sourceFacts_.physical)
            || currentSource.mapping.firstInstance != sourceFacts_.mapping.firstInstance
            || currentSource.mapping.instanceCount != sourceFacts_.mapping.instanceCount
            || currentSource.mapping.instanceCapacity != sourceFacts_.mapping.instanceCapacity
            || currentSource.mapping.contractVersion != sourceFacts_.mapping.contractVersion)
            return "RHI-CANONICAL-CONSUMER-GEOMETRY-CORRESPONDENCE";
        if (const auto* id = validateLayout(vertex))
            return id;
        if (vertex.streams().front().slot != slot_)
            return "RHI-CANONICAL-CONSUMER-GEOMETRY-CORRESPONDENCE";
        return source.validateStream(vertex, slot_, positions_.size(), 0u);
    }

    const char* validateStorage(const GpuBuffer& buffer) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        if (!buffer.hostWritesSealed() || !buffer.gpuWritesSealed()
            || !buffer.gpuTransferWritesSealed())
            return "RHI-CANONICAL-CONSUMER-GEOMETRY-WRITES-NOT-SEALED";
        const auto& desc = buffer.desc();
        return buffer.id().isValid() && desc.sizeBytes == positions_.size()
                && desc.usage == ResourceUsage::VertexBuffer && desc.memory == MemoryUsage::CpuToGpu
                && desc.initialState == ResourceState::ShaderRead
                && buffer.state() == ResourceState::ShaderRead
            ? nullptr
            : "RHI-CANONICAL-CONSUMER-GEOMETRY-STORAGE";
    }

private:
    static const char* validateLayout(const VertexInputConsumptionContract& vertex) noexcept
    {
        if (const auto* id = vertex.diagnostic())
            return id;
        if (vertex.streams().size() != 1u || vertex.attributes().size() != 1u)
            return "RHI-CANONICAL-CONSUMER-GEOMETRY-LAYOUT";
        const auto& stream = vertex.streams().front();
        const auto& attribute = vertex.attributes().front();
        return stream.strideBytes == strideBytes && stream.rate == VertexInputRate::Vertex
                && stream.fetchBytes == strideBytes && attribute.slot == stream.slot
                && !attribute.layout.shaderLocation && !attribute.layout.offsetBytes
                && attribute.layout.format == VertexFormat::Float32x2
            ? nullptr
            : "RHI-CANONICAL-CONSUMER-GEOMETRY-LAYOUT";
    }

    core::ByteBuffer positions_{&core::defaultCpuMemoryResource()},
        sourceBytes_{&core::defaultCpuMemoryResource()};
    std::uint32_t slot_ = 0u;
    std::uint64_t packageIdentity_ = 0u;
    ZeroBaseIndirectProducerParams parameters_;
    Source::Facts sourceFacts_;
    const char* diagnostic_ = nullptr;
};

// endregion canonical_zero_base_indirect_consumer_geometry

// region canonical_exclusive_consumer_images

// Fresh authoring values only, never a certificate for an external texture.
class CanonicalExclusiveConsumerImagesContract final {
public:
    static constexpr std::uint32_t version = 1u, imageCount = 3u;
    // Three prepare dependencies, begin, three uploads, end, three read deps.
    static constexpr std::uint32_t commandUnits = imageCount + 1u + imageCount + 1u + imageCount;
    using Content = CanonicalConsumerImageContentContract;

    struct Input {
        TextureFormat format = TextureFormat::Unknown;
        Extent3D parentExtent{0u, 0u, 0u}, extent{0u, 0u, 0u};
        std::uint32_t mipCount = 0u, selectedMip = 0u, bytesPerRow = 0u, rowsPerImage = 0u;
        const void* bytes = nullptr;
        std::size_t byteCount = 0u;
    };

    using Inputs = std::array<Input, imageCount>;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxSourceBytes = 0u, maxSnapshotBytes = 0u;
        std::uint32_t maxCommands = 0u;
    };

    struct Facts {
        std::array<std::uint64_t, imageCount> bytes{};
        std::uint64_t totalBytes = 0u;
    };

    struct Lowering {
        bool validatorOnly = false, prototypeCold = false, executable = false;
        const char* diagnosticId = "RHI-EXCLUSIVE-CONSUMER-IMAGES-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::CanonicalExclusiveConsumerImages_lowering<
            Lowering>(backend);
    }

    static const char* verify(const Inputs& inputs, Budget budget, Facts& output) noexcept
    {
        output = {};
        Facts facts;
        if (budget.contractVersion != version || !budget.prototypeOptIn || !budget.maxSourceBytes
            || !budget.maxSnapshotBytes || budget.maxCommands < commandUnits)
            return "RHI-EXCLUSIVE-CONSUMER-IMAGES-BUDGET";
        for (std::size_t i = 0u; i < inputs.size(); ++i) {
            const auto& input = inputs[i];
            if (input.format != TextureFormat::Rgba8Unorm || input.parentExtent.depth != 1u)
                return "RHI-EXCLUSIVE-CONSUMER-IMAGES-DOMAIN";
            Extent3D extent;
            if (const auto* id = semantics::TextureSubresourceExtent::resolve(
                    input.parentExtent, input.mipCount, input.selectedMip, extent))
                return id;
            if (extent.width != input.extent.width || extent.height != input.extent.height
                || extent.depth != input.extent.depth)
                return "RHI-EXCLUSIVE-CONSUMER-IMAGES-SELECTED-EXTENT";
            if (const auto* id = Content::validatePayload(extent,
                    input.format,
                    input.bytesPerRow,
                    input.rowsPerImage,
                    input.bytes,
                    input.byteCount,
                    {version,
                        true,
                        budget.maxSourceBytes - facts.totalBytes,
                        budget.maxSnapshotBytes - facts.totalBytes},
                    facts.bytes[i]))
                return id;
            if (facts.bytes[i] > UINT32_MAX)
                return "RHI-EXCLUSIVE-CONSUMER-IMAGES-UPLOAD-RANGE";
            facts.totalBytes += facts.bytes[i];
        }
        output = facts;
        return nullptr;
    }

    static TextureDesc parentDescriptor(const Input& input)
    {
        TextureDesc desc;
        desc.extent = input.parentExtent;
        desc.format = input.format;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
        desc.memory = MemoryUsage::GpuOnly;
        desc.initialState = ResourceState::ShaderRead;
        desc.mipLevelCount = input.mipCount;
        desc.arrayLayerCount = 1u;
        desc.sampleCount = 1u;
        return desc;
    }

    static TextureViewDesc viewDescriptor(const Input& input)
    {
        TextureViewDesc desc;
        desc.format = input.format;
        desc.usage = TextureUsage::Sampled;
        desc.dimension = TextureViewDimension::Texture2D;
        desc.baseMipLevel = input.selectedMip;
        desc.mipLevelCount = 1u;
        desc.baseArrayLayer = 0u;
        desc.arrayLayerCount = 1u;
        desc.aspect = TextureAspect::Color;
        return desc;
    }
};

// endregion canonical_exclusive_consumer_images

// region canonical_closed_consumer_target

// Explicit private output intent. Target creation never selects guessed dimensions/state.
class CanonicalClosedConsumerTargetContract final {
public:
    static constexpr std::uint32_t version = 1u;

    struct Input {
        Extent3D extent;
        ViewportState viewport;
        ScissorState scissor;
        Color clear;

        bool operator==(const Input& b) const noexcept
        {
            return extent.width == b.extent.width && extent.height == b.extent.height
                && extent.depth == b.extent.depth
                && semantics::viewportStatesEqual(viewport, b.viewport)
                && semantics::scissorStatesEqual(scissor, b.scissor) && clear.r == b.clear.r
                && clear.g == b.clear.g && clear.b == b.clear.b && clear.a == b.clear.a;
        }
    };

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxTargetBytes = 0u;
    };

    struct Facts {
        std::uint64_t targetBytes = 0u;
        Input original;
    };

    struct Lowering {
        bool validatorOnly = false, prototypeColdIntent = false, executable = false;
        const char* diagnosticId = nullptr;
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::BindlessPreparationLowering::CanonicalClosedConsumerTarget_lowering<
            Lowering>(backend);
    }

    static const char* verify(const Input& input, Budget budget, Facts& output) noexcept
    {
        output = {};
        if (budget.contractVersion != version || !budget.prototypeOptIn || !budget.maxTargetBytes)
            return "RHI-CLOSED-CONSUMER-TARGET-BUDGET";
        if (!input.extent.width || !input.extent.height || input.extent.depth != 1u)
            return "RHI-CLOSED-CONSUMER-TARGET-EXTENT";
        if (const auto* id = semantics::viewportCommandDiagnostic(input.viewport))
            return id;
        if (const auto* id = semantics::scissorCommandDiagnostic(input.scissor))
            return id;
        const auto& v = input.viewport;
        const auto& s = input.scissor;
        for (const float value : {v.x,
                 v.y,
                 v.width,
                 v.height,
                 v.minDepth,
                 v.maxDepth,
                 input.clear.r,
                 input.clear.g,
                 input.clear.b,
                 input.clear.a})
            if (!std::isfinite(value))
                return "RHI-CLOSED-CONSUMER-TARGET-NONFINITE";
        if (v.x < 0.0f || v.y < 0.0f || static_cast<double>(v.x) + v.width > input.extent.width
            || static_cast<double>(v.y) + v.height > input.extent.height || v.minDepth < 0.0f
            || v.maxDepth > 1.0f || s.x < 0 || s.y < 0
            || std::uint64_t{static_cast<std::uint32_t>(s.x)} + s.width > input.extent.width
            || std::uint64_t{static_cast<std::uint32_t>(s.y)} + s.height > input.extent.height)
            return "RHI-CLOSED-CONSUMER-TARGET-DYNAMIC-BOUNDS";
        for (const float value : {input.clear.r, input.clear.g, input.clear.b, input.clear.a})
            if (value < 0.0f || value > 1.0f)
                return "RHI-CLOSED-CONSUMER-TARGET-CLEAR-RANGE";
        const auto pixels = std::uint64_t{input.extent.width} * input.extent.height;
        const auto pixelBytes = bytesPerPixel(TextureFormat::Rgba8Unorm);
        if (pixels > budget.maxTargetBytes / pixelBytes)
            return "RHI-CLOSED-CONSUMER-TARGET-BYTE-BUDGET";
        output.targetBytes = pixels * pixelBytes;
        output.original = input;
        return nullptr;
    }

    // Only call after verify; these are the complete versioned output descriptors.
    static TextureDesc textureDescriptor(const Input& input)
    {
        TextureDesc result;
        result.extent = input.extent;
        result.format = TextureFormat::Rgba8Unorm;
        result.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
        result.memory = MemoryUsage::GpuOnly;
        result.initialState = ResourceState::ColorAttachment;
        result.mipLevelCount = 1u;
        result.arrayLayerCount = 1u;
        result.sampleCount = 1u;
        result.cubeCompatible = false;
        return result;
    }

    static RenderPassColorAttachmentDesc attachmentDescriptor(
        const Input& input, GpuResourceId target)
    {
        RenderPassColorAttachmentDesc result;
        result.target = target;
        result.format = TextureFormat::Rgba8Unorm;
        result.loadOp = LoadOp::Clear;
        result.storeOp = StoreOp::Store;
        result.hasResolveTarget = false;
        result.resolveTarget = {};
        result.clearColor = input.clear;
        return result;
    }
};
} // namespace arp::rhi

// endregion canonical_closed_consumer_target
