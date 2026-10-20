#pragma once

#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class CanonicalInitializedBufferStagingContractFixture final {
public:
    using Contract = rhi::OriginalCommandInitializedBufferStagingContract;

    static Contract::Budget budget()
    {
        return {1u,
            true,
            1u,
            Contract::callbackCount,
            Contract::retainedOwnerBound,
            16u,
            65536u,
            65536u,
            65536u,
            65536u,
            65536u};
    }

    static void verify()
    {
        auto b = budget();
        assert(!Contract::validateIntent(b));
        auto shader = b;
        shader.contractVersion = 4u;
        shader.shaderContent = {1u, true, rhi::DescriptorType::UniformBuffer, 16u};
        assert(!Contract::validateIntent(shader));
        assert(Contract::expectedUsage(shader)
                == (rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopySrc
                    | rhi::ResourceUsage::CopyDst)
            && Contract::expectedState(shader) == rhi::ResourceState::ShaderRead);
        assert(!Contract::validateShaderPayload(shader, 16u));
        assert(Contract::validateShaderPayload(shader, 12u));
        auto readonlyShader = shader;
        readonlyShader.shaderContent.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
        assert(!Contract::validateIntent(readonlyShader)
            && Contract::expectedUsage(readonlyShader)
                == (rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
                    | rhi::ResourceUsage::CopyDst));
        for (const auto version : {1u, 2u, 3u}) {
            auto legacy = shader;
            legacy.contractVersion = version;
            if (version == 2u)
                legacy.indexContent = {1u,
                    true,
                    rhi::IndexFormat::Uint16,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian,
                    8u};
            if (version == 3u)
                legacy.vertexContent = {1u,
                    true,
                    8u,
                    rhi::VertexInputRate::Vertex,
                    2u,
                    core::UnsignedElementSpan::ByteOrder::LittleEndian};
            assert(Contract::validateIntent(legacy));
        }
        for (const auto type : {rhi::DescriptorType::StorageBuffer,
                 rhi::DescriptorType::SampledTexture,
                 rhi::DescriptorType::Sampler}) {
            auto invalidShader = shader;
            invalidShader.shaderContent.type = type;
            assert(Contract::validateIntent(invalidShader));
        }
        auto invalidShader = shader;
        invalidShader.shaderContent.prototypeOptIn = false;
        assert(Contract::validateIntent(invalidShader));
        invalidShader = shader;
        invalidShader.shaderContent.contractVersion = 0u;
        assert(Contract::validateIntent(invalidShader));
        invalidShader = shader;
        invalidShader.shaderContent.bindingBytes = 0u;
        assert(Contract::validateIntent(invalidShader));
        invalidShader = shader;
        invalidShader.shaderContent.bindingBytes = UINT64_MAX;
        assert(Contract::validateIntent(invalidShader));
        invalidShader = shader;
        invalidShader.contractVersion = 5u;
        assert(Contract::validateIntent(invalidShader));
        auto invalid = b;
        invalid.contractVersion = 0u;
        assert(Contract::validateIntent(invalid));
        invalid = b;
        invalid.prototypeOptIn = false;
        assert(Contract::validateIntent(invalid));
        invalid = b;
        --invalid.maxCallbacks;
        assert(Contract::validateIntent(invalid));
        invalid = b;
        --invalid.maxRetainedOwners;
        assert(Contract::validateIntent(invalid));
        invalid = b;
        invalid.maxNativeBuffers = 2u;
        assert(Contract::validateIntent(invalid));
        auto index = b;
        index.contractVersion = 2u;
        index.indexContent = {1u,
            true,
            rhi::IndexFormat::Uint16,
            core::UnsignedElementSpan::ByteOrder::LittleEndian,
            8u};
        assert(!Contract::validateIntent(index));
        const auto indexUsage = rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopySrc
            | rhi::ResourceUsage::CopyDst;
        assert(Contract::expectedUsage(index) == indexUsage && !Contract::shaderReadEnabled(index));
        auto readonly = index;
        readonly.indexContent.shaderReadContractVersion = 1u;
        readonly.indexContent.shaderReadOptIn = true;
        assert(!Contract::validateIntent(readonly) && Contract::shaderReadEnabled(readonly)
            && Contract::expectedUsage(readonly)
                == (indexUsage | rhi::ResourceUsage::StorageBuffer));
        invalid = readonly;
        invalid.indexContent.shaderReadOptIn = false;
        assert(std::string_view(Contract::validateIntent(invalid))
            == "RHI-INITIALIZED-INDEX-SHADER-READ-OPT-IN");
        invalid = readonly;
        invalid.indexContent.shaderReadContractVersion = 0u;
        assert(std::string_view(Contract::validateIntent(invalid))
            == "RHI-INITIALIZED-INDEX-SHADER-READ-VERSION");
        invalid = readonly;
        invalid.indexContent.shaderReadContractVersion = 2u;
        assert(std::string_view(Contract::validateIntent(invalid))
            == "RHI-INITIALIZED-INDEX-SHADER-READ-VERSION");
        invalid = readonly;
        invalid.contractVersion = 1u;
        assert(std::string_view(Contract::validateIntent(invalid))
            == "RHI-INITIALIZED-CANONICAL-STAGING-INDEX-SCHEMA");
        invalid = index;
        invalid.indexContent.prototypeOptIn = false;
        assert(Contract::validateIntent(invalid));
        invalid = index;
        invalid.indexContent.contractVersion = 0u;
        assert(Contract::validateIntent(invalid));
        invalid = index;
        invalid.indexContent.format = static_cast<rhi::IndexFormat>(255u);
        assert(Contract::validateIntent(invalid));
        invalid = index;
        invalid.indexContent.byteOrder = core::UnsignedElementSpan::ByteOrder::BigEndian;
        assert(Contract::validateIntent(invalid));
        invalid = index;
        invalid.indexContent.maxAnalysisIndices = 0u;
        assert(Contract::validateIntent(invalid));
        invalid = index;
        invalid.contractVersion = 1u;
        assert(Contract::validateIntent(invalid));
        invalid = index;
        invalid.contractVersion = 3u;
        assert(Contract::validateIntent(invalid));
        auto vertex = b;
        vertex.contractVersion = 3u;
        vertex.vertexContent = {1u,
            true,
            8u,
            rhi::VertexInputRate::Vertex,
            2u,
            core::UnsignedElementSpan::ByteOrder::LittleEndian};
        assert(!Contract::validateIntent(vertex));
        assert(Contract::expectedUsage(vertex)
                == (rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopySrc
                    | rhi::ResourceUsage::CopyDst)
            && Contract::expectedState(vertex) == rhi::ResourceState::ShaderRead
            && !Contract::shaderReadEnabled(vertex));
        assert(!Contract::validateVertexPayload(vertex, 16u));
        assert(std::string_view(Contract::validateVertexPayload(vertex, 12u))
            == "RHI-INITIALIZED-VERTEX-CONTENT-SIZE");
        const auto refuseVertex = [&](const Contract::Budget& value, const char* id) {
            assert(std::string_view(Contract::validateIntent(value)) == id);
        };
        invalid = vertex;
        invalid.contractVersion = 1u;
        refuseVertex(invalid, "RHI-INITIALIZED-CANONICAL-STAGING-VERTEX-SCHEMA");
        invalid = vertex;
        invalid.contractVersion = 2u;
        invalid.indexContent = index.indexContent;
        refuseVertex(invalid, "RHI-INITIALIZED-CANONICAL-STAGING-VERTEX-SCHEMA");
        invalid = vertex;
        invalid.indexContent = index.indexContent;
        refuseVertex(invalid, "RHI-INITIALIZED-CANONICAL-STAGING-INDEX-SCHEMA");
        invalid = vertex;
        invalid.vertexContent.contractVersion = 0u;
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-CONTENT-VERSION");
        invalid = vertex;
        invalid.vertexContent.prototypeOptIn = false;
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-CONTENT-OPT-IN");
        invalid = vertex;
        invalid.vertexContent.strideBytes = 0u;
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-CONTENT-STRIDE");
        invalid = vertex;
        invalid.vertexContent.strideBytes = 6u;
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-CONTENT-STRIDE");
        invalid = vertex;
        invalid.vertexContent.rate = static_cast<rhi::VertexInputRate>(255u);
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-CONTENT-RATE");
        invalid = vertex;
        invalid.vertexContent.elementCount = 0u;
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-CONTENT-COUNT");
        invalid = vertex;
        invalid.vertexContent.elementCount = UINT64_MAX;
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-CONTENT-COUNT");
        invalid = vertex;
        invalid.vertexContent.byteOrder = core::UnsignedElementSpan::ByteOrder::BigEndian;
        refuseVertex(invalid, "RHI-INITIALIZED-VERTEX-BYTE-ORDER");
        auto instance = vertex;
        instance.vertexContent.rate = rhi::VertexInputRate::Instance;
        assert(!Contract::validateIntent(instance));
        rhi::VertexInputConsumptionContract::Stream stream{
            3u, 8u, rhi::VertexInputRate::Vertex, 0u, 1u, 8u};
        assert(!Contract::validateVertexStream(vertex, stream));
        assert(!Contract::validateVertexRange(vertex, 16u, stream, 0u, 2u, 0u, 1u, 0u));
        assert(Contract::validateVertexRange(vertex, 16u, stream, 0u, 3u, 0u, 1u, 0u));
        assert(Contract::validateVertexRange(vertex, 16u, stream, 0u, 1u, UINT64_MAX, 1u, 0u));
        assert(Contract::validateVertexRange(vertex, 16u, stream, 1u, 1u, 0u, 1u, 0u));
        assert(Contract::validateVertexRange(vertex, 16u, stream, 16u, 1u, 0u, 1u, 0u));
        assert(Contract::validateVertexRange(vertex, 16u, stream, 0u, 0u, 0u, 1u, 0u));
        auto instanceStream = stream;
        instanceStream.rate = rhi::VertexInputRate::Instance;
        assert(!Contract::validateVertexRange(instance, 16u, instanceStream, 0u, 1u, 0u, 2u, 0u));
        assert(Contract::validateVertexRange(instance, 16u, instanceStream, 0u, 1u, 0u, 3u, 0u));
        assert(Contract::validateVertexRange(
            instance, 16u, instanceStream, 0u, 1u, 0u, 1u, UINT64_MAX));
        ++stream.strideBytes;
        assert(Contract::validateVertexStream(vertex, stream));
        --stream.strideBytes;
        stream.rate = rhi::VertexInputRate::Instance;
        assert(Contract::validateVertexStream(vertex, stream));
        stream.rate = rhi::VertexInputRate::Vertex;
        stream.fetchBytes = 12u;
        assert(Contract::validateVertexStream(vertex, stream));
        const std::array<const char*, 4> ids{
            "RHI-INITIALIZED-CANONICAL-STAGING-NULL-VALIDATOR-ONLY",
            "RHI-INITIALIZED-CANONICAL-STAGING-VULKAN-PAYLOAD-MISSING",
            "RHI-INITIALIZED-CANONICAL-STAGING-METAL-PAYLOAD-MISSING",
            "RHI-INITIALIZED-CANONICAL-STAGING-WEBGPU-PRIVATE-PROFILE"};
        std::size_t i = 0u;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                    backend);
            assert(std::string_view(d.diagnostic) == ids[i]);
            assert(d.privateInitializationAvailable == (i == 3u));
            ++i;
            const auto initializedIndex =
                rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                    backend, 2u);
            assert(
                initializedIndex.privateInitializationAvailable == (backend == rhi::Backend::WebGPU)
                && initializedIndex.diagnostic && initializedIndex.disposition == d.disposition);
            const auto initializedVertex =
                rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                    backend, 3u);
            assert(initializedVertex.privateInitializationAvailable
                    == (backend == rhi::Backend::WebGPU)
                && initializedVertex.diagnostic && initializedVertex.disposition == d.disposition);
            const auto initializedShader =
                rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                    backend, 4u);
            assert(initializedShader.privateInitializationAvailable
                    == (backend == rhi::Backend::WebGPU)
                && initializedShader.diagnostic && initializedShader.disposition == d.disposition);
            assert(!rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                backend, 5u)
                    .privateInitializationAvailable);
            const auto shaderRead = rhi::semantics::
                OriginalCommandInitializedBufferStagingLowering::indexShaderReadLowering(backend);
            assert(shaderRead.disposition == initializedIndex.disposition
                && shaderRead.privateInitializationAvailable
                    == initializedIndex.privateInitializationAvailable
                && std::string_view(shaderRead.diagnostic) == initializedIndex.diagnostic);
            assert(!rhi::semantics::OriginalCommandInitializedBufferStagingLowering::
                    indexShaderReadLowering(backend, 2u)
                        .privateInitializationAvailable);
        }
        assert(!arp::rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
            static_cast<rhi::Backend>(255u))
                .privateInitializationAvailable);
        std::puts(
            "BINDLESS_RHI_CANONICAL_INITIALIZED_STAGING_CONTRACT_OK buffer1 scopes3 queue1 own-loss1 existing-loss1 retained-bytes exact-canonical-lease five-independent-cpu-domains four-outcomes generic-execution-closed");
    }
};
} // namespace arp::test
