#pragma once
#include "canonical_producer_recording_fixture.h"
#include "rhi/frontend/bindless_recording_validation.h"

namespace arp::test {
class CanonicalClosedConsumerRecordingFixture final {
public:
    using Contract = rhi::CanonicalClosedConsumerRecordingContract;
    using Content = Contract::Content;
    static constexpr std::size_t imageBytes = rhi::kTextureUploadRowPitchAlignment * 2u;

    CanonicalClosedConsumerRecordingFixture(
        std::shared_ptr<const rhi::BindlessShaderPackage> producer,
        std::shared_ptr<const rhi::BindlessShaderPackage> consumer,
        ZeroBaseIndirectSourceFixture::Order order,
        bool suppressed)
        : original(std::move(producer), std::move(consumer), order, suppressed, true)
    {
        inputs.producer = original.inputs;
        core::pmr::Vector<rhi::VertexBufferLayoutDesc> layout;
        rhi::VertexBufferLayoutDesc stream;
        stream.strideBytes = Contract::Geometry::strideBytes;
        stream.attributes = {{0u, 0u, rhi::VertexFormat::Float32x2}};
        layout.push_back(std::move(stream));
        inputs.vertex = core::makeShared<rhi::VertexInputConsumptionContract>(layout);
        const std::array<std::uint32_t, 8u> bits{0xbf800000u,
            0xbf800000u,
            0x3f800000u,
            0xbf800000u,
            0u,
            0x3f800000u,
            0x3f000000u,
            0x3f000000u};
        std::array<std::uint8_t, sizeof(bits)> positions;
        core::UnsignedElementSpan::encodeWordsLittleEndian(bits, positions);
        inputs.geometry = core::makeShared<Contract::Geometry>(*inputs.producer.consumerPackage,
            *inputs.producer.source,
            *inputs.vertex,
            Contract::Geometry::Input{positions.data(),
                positions.size(),
                Contract::Geometry::Encoding::Ieee754Binary32,
                Contract::Geometry::Order::LittleEndian},
            Contract::Geometry::Budget{1u,
                true,
                positions.size(),
                positions.size() + inputs.producer.source->sourceBytes().size()});
        assert(!inputs.geometry->diagnostic());
        auto geometry = original.device().createBuffer({"ConsumerGeometry",
            positions.size(),
            rhi::ResourceUsage::VertexBuffer,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        assert(geometry);
        assert(
            original.device().writeBuffer(*geometry, positions.data(), positions.size()).success);
        geometry->sealWrites();
        inputs.geometryBuffer = core::shareUnique(std::move(geometry));
        Contract::Consumer::Facts consumerFacts;
        assert(!Contract::Consumer::verify(*inputs.producer.consumerPackage,
            *inputs.producer.source,
            *inputs.producer.packet,
            *inputs.producer.packet->instanceMappings(),
            inputs.producer.images,
            inputs.producer.producerFault,
            inputs.producer.consumerFault,
            consumerFacts));
        auto metadata = original.device().createBuffer({"ConsumerMetadata",
            sizeof(consumerFacts.bytes),
            rhi::ResourceUsage::StorageBuffer,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        assert(metadata);
        assert(original.device()
                .writeBuffer(*metadata, consumerFacts.bytes.data(), consumerFacts.bytes.size())
                .success);
        metadata->sealWrites();
        inputs.metadata = core::shareUnique(std::move(metadata));
        for (std::size_t i = 0u; i < inputs.contents.size(); ++i) {
            // Explicit authored fixture payload, including patterned row padding.
            std::array<std::uint8_t, imageBytes> bytes;
            for (std::size_t offset = 0u; offset < bytes.size(); ++offset)
                bytes[offset] = static_cast<std::uint8_t>(i + offset);
            inputs.contents[i] = core::makeShared<Content>(Content::Input{inputs.producer.images[i],
                                                               {4u, 2u, 1u},
                                                               rhi::kTextureUploadRowPitchAlignment,
                                                               2u,
                                                               bytes.data(),
                                                               bytes.size()},
                Content::Budget{1u, true, bytes.size(), bytes.size()});
            assert(!inputs.contents[i]->diagnostic());
            inputs.revisions[i] = {
                1u, inputs.producer.images[i].view, inputs.producer.images[i].parent, 1u};
        }
    }

    void recordLogicalImageUploads()
    {
        auto encoder = original.device().createCommandEncoder({"ConsumerLogicalImageUpload"});
        assert(encoder);
        for (std::size_t i = 0u; i < inputs.contents.size(); ++i) {
            auto& texture = original.texture(i);
            const auto& content = *inputs.contents[i];
            rhi::ResourceBarrierDesc barrier;
            barrier.resource = texture.id();
            barrier.textureSubresources = content.image().range;
            barrier.before = rhi::ResourceState::ShaderRead;
            barrier.after = rhi::ResourceState::CopyDst;
            barrier.sourceStages = rhi::PipelineStage::FragmentShader;
            barrier.sourceAccess = rhi::ResourceAccess::ShaderRead;
            barrier.destinationStages = rhi::PipelineStage::Transfer;
            barrier.destinationAccess = rhi::ResourceAccess::TransferWrite;
            assert(encoder->transition(texture, barrier).success);
        }
        auto pass = encoder->beginTransferPass({"ConsumerUpload"});
        assert(pass);
        for (std::size_t i = 0u; i < inputs.contents.size(); ++i) {
            rhi::UploadTextureDesc upload;
            assert(!inputs.contents[i]->describeUpload(inputs.revisions[i], 1u, upload));
            assert(pass->uploadTexture(upload).success);
        }
        assert(pass->end().success);
        for (std::size_t i = 0u; i < inputs.contents.size(); ++i) {
            auto& texture = original.texture(i);
            rhi::ResourceBarrierDesc barrier;
            barrier.resource = texture.id();
            barrier.textureSubresources = inputs.contents[i]->image().range;
            barrier.before = rhi::ResourceState::CopyDst;
            barrier.after = rhi::ResourceState::ShaderRead;
            barrier.sourceStages = rhi::PipelineStage::Transfer;
            barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
            barrier.destinationStages = rhi::PipelineStage::FragmentShader;
            barrier.destinationAccess = rhi::ResourceAccess::ShaderRead;
            assert(encoder->transition(texture, barrier).success);
        }
        auto command = encoder->finish();
        assert(command);
        assert(original.device().queue().submit(*command).success);
        // Null records intent/state only; never assert actual uploaded texels.
        for (std::size_t i = 0u; i < inputs.contents.size(); ++i)
            assert(original.texture(i).subresourceState(inputs.producer.images[i].range)
                == rhi::ResourceState::ShaderRead);
    }

    // One real Null journal transition per selected original resource. This
    // proves only logical state, not packet contents, encoding or completion.
    void transitionConsumerBuffer(std::size_t role, bool ready)
    {
        using namespace rhi;
        assert(role < 3u);
        const std::array<const GpuBuffer*, 3u> buffers{inputs.producer.packet->physicalArgs(),
            inputs.producer.packet->instanceMappings(),
            inputs.producer.consumerFault->faultLatchBuffer()};
        auto& buffer = const_cast<GpuBuffer&>(*buffers[role]);
        const auto finalState = role < 2u ? ResourceState::ShaderRead : ResourceState::CopySrc;
        ResourceBarrierDesc barrier;
        barrier.resource = buffer.id();
        barrier.before = ready ? ResourceState::ShaderWrite : finalState;
        barrier.after = ready ? finalState : ResourceState::ShaderWrite;
        const auto consumerStage = role == 0u ? PipelineStage::DrawIndirect
            : role == 1u                      ? PipelineStage::VertexShader
                                              : PipelineStage::Transfer;
        const auto consumerAccess = role == 0u ? ResourceAccess::IndirectCommandRead
            : role == 1u                       ? ResourceAccess::ShaderRead
                                               : ResourceAccess::TransferRead;
        barrier.sourceStages = ready
            ? (role < 2u ? PipelineStage::Transfer : PipelineStage::FragmentShader)
            : consumerStage;
        barrier.sourceAccess = ready
            ? (role < 2u ? ResourceAccess::TransferWrite : ResourceAccess::ShaderWrite)
            : consumerAccess;
        barrier.destinationStages = ready ? consumerStage : PipelineStage::ComputeShader;
        barrier.destinationAccess = ready ? consumerAccess : ResourceAccess::ShaderWrite;
        auto encoder = original.device().createCommandEncoder({"ClosedConsumerLogicalPhase"});
        assert(encoder);
        if (ready && role < 2u) {
            auto prepare = barrier;
            prepare.after = ResourceState::CopyDst;
            prepare.sourceStages = PipelineStage::ComputeShader;
            prepare.sourceAccess = ResourceAccess::ShaderWrite;
            prepare.destinationStages = PipelineStage::Transfer;
            prepare.destinationAccess = ResourceAccess::TransferWrite;
            assert(encoder->transition(buffer, prepare).success);
            barrier.before = ResourceState::CopyDst;
        }
        assert(encoder->transition(buffer, barrier).success);
        auto command = encoder->finish();
        assert(command);
        assert(original.device().queue().submit(*command).success);
    }

    static Contract::Budget budget() noexcept
    {
        return {
            1u, true, Contract::commandUnits, Contract::readbackBytes, Contract::referenceSlots};
    }

    CanonicalProducerRecordingFixture original;
    Contract::Inputs inputs;
};
} // namespace arp::test
