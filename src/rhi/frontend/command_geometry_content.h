#pragma once
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/geometry_content_lowering.h"

#include <cstring>

namespace arp::rhi {
// Backend-issued initialized content, retained by the canonical closure.
// Backend factories must authenticate the concrete actor and its shared owner.
class OriginalInitializedGeometrySource : public core::QuotaBoundSharedObjectIdentity {
public:
    virtual ~OriginalInitializedGeometrySource() = default;
    virtual const std::shared_ptr<const OriginalCommandBufferLease>&
    originalLease() const noexcept = 0;
    virtual std::uint64_t initializedBytes() const noexcept = 0;
};

// OriginalHostGeometryContent/1 freezes and initializes one complete original
// CPU-to-GPU geometry buffer through a real mapped write and publication.
// Vulkan flushes mapped storage, Metal publishes shared/managed writes, and
// WebGPU writes at creation and unmaps before any command can consume it.
// Initialization is host work, not GPU completion or command authority.
class OriginalHostGeometryContentContract final {
public:
    using Staging = OriginalCommandInitializedBufferStagingContract;
    using Definition = std::variant<Staging::IndexContentIntent,
        Staging::VertexContentIntent,
        Staging::ShaderContentIntent>;
    // Source metadata, authentic lease/object, native payload and runtime.
    static constexpr std::uint32_t retainedOwnerBound =
        1u + OriginalCommandBufferLease::retainedOwnerCount + 2u;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxNativeBuffers = 0u, maxRetainedOwners = 0u;
        std::uint64_t maxRetainedBufferBytes = 0u, maxCpuMetadataBytes = 0u,
                      maxCpuPayloadBytes = 0u, maxCpuResourceBytes = 0u, maxCpuLeaseBytes = 0u;
        Definition definition;
    };

    struct Quotas {
        std::shared_ptr<core::CpuMemoryQuota> metadata, payload, resource, lease;
    };

    static const char* validateQuotas(
        GpuDevice& device, const Budget& budget, const Quotas& quotas) noexcept
    {
        const std::array domains{quotas.metadata, quotas.payload, quotas.resource, quotas.lease};
        const std::array limits{budget.maxCpuMetadataBytes,
            budget.maxCpuPayloadBytes,
            budget.maxCpuResourceBytes,
            budget.maxCpuLeaseBytes};
        for (std::size_t i = 0u; i < domains.size(); ++i)
            if (!domains[i] || domains[i]->limitBytes() != limits[i])
                return "RHI-HOST-GEOMETRY-CONTENT-QUOTA";
        if (!core::SharedOwnerProjection::hasIndependentOwners(domains.size(), [&](std::size_t i) {
                return domains[i];
            }))
            return "RHI-HOST-GEOMETRY-CONTENT-QUOTA-DOMAIN";
        for (const auto& quota : domains)
            if (device.overlapsOriginalDeviceCpuOwner(quota))
                return "RHI-HOST-GEOMETRY-CONTENT-QUOTA-DOMAIN";
        return nullptr;
    }

    static const char* analyzeIndexBytes(const Definition& definition,
        const void* bytes,
        std::size_t size,
        IndexFormat format,
        std::uint64_t offset,
        std::uint64_t first,
        std::uint64_t count,
        std::int32_t base,
        IndexInputConsumptionContract::DrawRange& output) noexcept
    {
        output = {};
        const auto* index = std::get_if<Staging::IndexContentIntent>(&definition);
        if (!index || index->format != format)
            return "RHI-HOST-GEOMETRY-CONTENT-INDEX-PROFILE";
        return IndexInputConsumptionContract::analyzeLittleEndianBytes(
            static_cast<const std::uint8_t*>(bytes),
            size,
            format,
            offset,
            first,
            count,
            base,
            index->maxAnalysisIndices,
            output);
    }

    static const char* validateVertexBytes(const Definition& definition,
        std::size_t size,
        const VertexInputConsumptionContract::Stream& stream,
        std::uint64_t offset,
        std::uint64_t vertices,
        std::uint64_t firstVertex,
        std::uint64_t instances,
        std::uint64_t firstInstance) noexcept
    {
        if (const auto* id = validateVertexStream(definition, stream))
            return id;
        if (offset % Staging::VertexContentIntent::scalarBytes)
            return "RHI-INITIALIZED-VERTEX-BINDING-OFFSET";
        return VertexInputConsumptionContract::validateStreamRange(
            stream, size, offset, vertices, firstVertex, instances, firstInstance);
    }

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::OriginalHostGeometryContentLowering::current(backend, version);
    }

    static const char* validateBudget(const Budget& budget) noexcept
    {
        if (budget.contractVersion != 1u && budget.contractVersion != 2u)
            return "RHI-HOST-GEOMETRY-CONTENT-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-HOST-GEOMETRY-CONTENT-OPT-IN";
        const auto* diagnostic = std::visit(
            [](const auto& intent) noexcept {
                return intent.diagnostic();
            },
            budget.definition);
        if (diagnostic)
            return diagnostic;
        if ((budget.contractVersion == 2u)
            != std::holds_alternative<Staging::ShaderContentIntent>(budget.definition))
            return "RHI-HOST-IMMUTABLE-BUFFER-CONTENT-SCHEMA";
        if (const auto* index = std::get_if<Staging::IndexContentIntent>(&budget.definition))
            if (index->shaderReadContractVersion || index->shaderReadOptIn)
                return "RHI-HOST-GEOMETRY-CONTENT-SHADER-SCHEMA";
        if (budget.maxNativeBuffers != 1u || budget.maxRetainedOwners < retainedOwnerBound)
            return "RHI-HOST-GEOMETRY-CONTENT-OWNER-CAPACITY";
        return budget.maxRetainedBufferBytes && budget.maxCpuMetadataBytes
                && budget.maxCpuPayloadBytes && budget.maxCpuResourceBytes
                && budget.maxCpuLeaseBytes
            ? nullptr
            : "RHI-HOST-GEOMETRY-CONTENT-BUDGET";
    }

    static ResourceUsage usage(const Definition& definition) noexcept
    {
        if (const auto* shader = std::get_if<Staging::ShaderContentIntent>(&definition))
            return shader->type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                                 : ResourceUsage::StorageBuffer;
        return std::holds_alternative<Staging::IndexContentIntent>(definition)
            ? ResourceUsage::IndexBuffer
            : ResourceUsage::VertexBuffer;
    }

    static const char* validateDeclaration(
        const BufferDesc& desc, std::uint64_t bytes, const Budget& budget) noexcept
    {
        if (const auto* id = validateBudget(budget))
            return id;
        if (desc.memory != MemoryUsage::CpuToGpu || desc.initialState != ResourceState::ShaderRead
            || desc.usage != usage(budget.definition))
            return "RHI-HOST-GEOMETRY-CONTENT-LAYOUT";
        if (!bytes || bytes != desc.sizeBytes || bytes > SIZE_MAX
            || bytes > budget.maxCpuPayloadBytes || bytes > budget.maxRetainedBufferBytes)
            return "RHI-HOST-GEOMETRY-CONTENT-BYTE-CAPACITY";
        if (const auto* index = std::get_if<Staging::IndexContentIntent>(&budget.definition))
            return bytes % IndexInputConsumptionContract::elementBytes(index->format)
                ? "RHI-HOST-GEOMETRY-CONTENT-INDEX-SIZE"
                : nullptr;
        if (const auto* shader = std::get_if<Staging::ShaderContentIntent>(&budget.definition))
            return bytes == shader->bindingBytes && bytes % sizeof(std::uint32_t) == 0u
                ? nullptr
                : "RHI-HOST-IMMUTABLE-SHADER-CONTENT-SIZE";
        const auto& vertex = std::get<Staging::VertexContentIntent>(budget.definition);
        return bytes == vertex.elementCount * vertex.strideBytes
            ? nullptr
            : "RHI-INITIALIZED-VERTEX-CONTENT-SIZE";
    }

    // Backend actors compare the retained payload actually passed to mapped
    // initialization. An external span or a matching lease alone is not proof.
    static const char* validateShaderBytes(const Definition& definition,
        DescriptorType type,
        const void* actual,
        std::size_t actualBytes,
        const void* expected,
        std::size_t expectedBytes) noexcept
    {
        const auto* shader = std::get_if<Staging::ShaderContentIntent>(&definition);
        if (!shader || shader->diagnostic() || shader->type != type)
            return "RHI-HOST-IMMUTABLE-SHADER-CONTENT-PROFILE";
        if (!actual || !expected || actualBytes != shader->bindingBytes
            || actualBytes != expectedBytes || actualBytes % sizeof(std::uint32_t))
            return "RHI-HOST-IMMUTABLE-SHADER-CONTENT-SIZE";
        return std::memcmp(actual, expected, actualBytes)
            ? "RHI-HOST-IMMUTABLE-SHADER-CONTENT-BYTES"
            : nullptr;
    }

    static const char* validateVertexStream(
        const Definition& definition, const VertexInputConsumptionContract::Stream& stream) noexcept
    {
        const auto* vertex = std::get_if<Staging::VertexContentIntent>(&definition);
        if (!vertex)
            return "RHI-HOST-GEOMETRY-CONTENT-VERTEX-PROFILE";
        if (const auto* id = vertex->diagnostic())
            return id;
        return stream.strideBytes == vertex->strideBytes && stream.rate == vertex->rate
                && stream.attributeCount && stream.fetchBytes
                && stream.fetchBytes <= stream.strideBytes
            ? nullptr
            : "RHI-INITIALIZED-VERTEX-STREAM-JOIN";
    }
};

// Borrowed cold creation request. The device copies bytes into its existing
// immutable initialized-content actor before returning. No native handle,
// shader execution authority, journal or completion issuer crosses this API.
struct OriginalInitializedBufferRequest {
    BufferDesc desc;
    const void* bytes = nullptr;
    std::size_t byteCount = 0u;
    OriginalHostGeometryContentContract::Budget budget;
    OriginalHostGeometryContentContract::Quotas quotas;
};
} // namespace arp::rhi
