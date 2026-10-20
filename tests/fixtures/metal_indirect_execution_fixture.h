#pragma once
#include "metal_indirect_producer_pipeline_fixture.h"
#include "metal_ordinary_graphics_vertex_fixture.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"

namespace arp::test {
using namespace rhi;
using namespace rhi_metal;
using Root = MetalRhiIndirectOutputPreparation;

class MetalIndirectExecutionFixture final {
public:
    enum class Inputs { NegativeBase, PositiveBase, ZeroInstanceBase };

    MetalIndirectExecutionFixture(const char* vs,
        const char* fs,
        const char* compute,
        IndexFormat format = IndexFormat::Uint16,
        Inputs inputs = Inputs::NegativeBase)
        : vertex(vs, fs), producer(compute, vertex.pass.graphics.quota)
    {
        auto& f = vertex.pass.graphics;
        pipeline = producer.prepare();
        assert(pipeline);
        const auto width = format == IndexFormat::Uint16 ? 2u : 4u;
        std::array<std::uint8_t, 16u> bytes{};
        // Binding skips one leading vertex; the fixture has a second dummy.
        // Indices3/4/5 plus the proven base-2 fetch logical vertices1/2/3,
        // which correspond exactly to the full-screen triangle in that stream.
        const std::array<std::uint32_t, 4u> words{
            format == IndexFormat::Uint16 ? UINT16_MAX : UINT32_MAX,
            inputs == Inputs::PositiveBase ? 0u : 3u,
            inputs == Inputs::PositiveBase ? 1u : 4u,
            inputs == Inputs::PositiveBase ? 2u : 5u};
        for (std::size_t i = 0u; i < words.size(); ++i)
            assert(core::UnsignedElementSpan::writeLittleEndian(words[i],
                format == IndexFormat::Uint16 ? core::UnsignedElementSpan::Width::Uint16
                                              : core::UnsignedElementSpan::Width::Uint32,
                bytes.data() + i * width,
                width));
        index = MetalRhiOrdinaryPreparedIndexBinding::prepare(f.provider,
            *f.device,
            bytes.data(),
            words.size() * width,
            format,
            core::UnsignedElementSpan::ByteOrder::LittleEndian,
            IndexInputConsumptionContract::PrimitiveRestart::Disabled,
            f.registry,
            f.budget(),
            f.quota,
            f.diagnostic);
        assert(index);
        if (inputs == Inputs::ZeroInstanceBase)
            vertex.prepared[1] = vertex.prepare(
                *vertex.buffers[1], 8u, 2u * sizeof(test::OrdinaryGraphicsVertexFixture::Instance));
        for (std::size_t i = 0u; i < streams.size(); ++i)
            streams[i] = {vertex.prepared[i], vertex.buffers[i]};
    }

    auto prepare()
    {
        auto& f = vertex.pass.graphics;
        return Root::prepare(
            pipeline, index, *f.device, streams.data(), streams.size(), budget, f.diagnostic);
    }

    void seal()
    {
        for (const auto& stream : streams)
            stream.wrapper->sealWrites();
    }

    auto command(std::uint32_t owners = 128u, std::uint32_t commands = 9u)
    {
        auto& f = vertex.pass.graphics;
        const char* id = nullptr;
        auto retention = common::CommandRetentionStorage::prepare(
            {1u, true, owners, 0u, 0u, 0u, f.cpuBytes}, f.quota, id);
        assert(retention);
        return createMetalRhiIndirectOutputNativeCommandPayload({1u, true, commands},
            "ProtectedIndirectExecution",
            f.registry,
            std::move(retention),
            f.diagnostic);
    }

    auto execution(const std::shared_ptr<const Root>& output,
        const std::shared_ptr<MetalRhiNativeCommandPayload>& command)
    {
        auto& f = vertex.pass.graphics;
        auto consumer = MetalRhiPreparedInstanceAttributeConsumer::prepare(
            f.provider, {1u, true}, f.budget(), f.quota, f.diagnostic);
        assert(consumer);
        return MetalRhiIndirectExecutionPreparation::prepare(output,
            consumer,
            f.prepare(),
            vertex.pass.prepare(),
            command,
            {1u, true, 256u},
            f.diagnostic);
    }

    static ResourceBarrierDesc dependency(const Root& output)
    {
        const auto& contract = output.producer()->contract();
        const auto producer = contract.producerAccess(output.resource().id()),
                   consumer = contract.consumerAccess(output.resource().id());
        ResourceBarrierDesc result;
        result.resource = output.resource().id();
        result.before = producer.state;
        result.after = consumer.state;
        result.sourceStages = producer.pipelineStages;
        result.sourceAccess = producer.access;
        result.destinationStages = consumer.pipelineStages;
        result.destinationAccess = consumer.access;
        return result;
    }

    static bool record(const std::shared_ptr<MetalRhiNativeCommandPayload>& command,
        const std::shared_ptr<const MetalRhiIndirectExecutionPreparation>& root,
        core::pmr::String& diagnostic)
    {
        const auto& output = *root->output();
        const auto& contract = output.producer()->contract();
        return command->recordProtectedIndirectExecution(root,
            contract.producerAccess(output.resource().id()),
            dependency(output),
            contract.consumerAccess(output.resource().id()),
            diagnostic);
    }

    test::OrdinaryGraphicsVertexFixture vertex;
    test::MetalIndirectProducerPipelineFixture producer;
    std::shared_ptr<const MetalRhiIndirectProducerPipeline> pipeline;
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index;
    std::array<Root::Stream, 2u> streams;
    GpuIndirectOutputPreparationBudget budget{1u,
        true,
        sizeof(DrawIndexedIndirectCommand),
        {1u,
            sizeof(DrawIndexedIndirectCommand),
            sizeof(DrawIndexedIndirectCommand)
                + sizeof(IndirectArgumentConsumptionContract::DrawObservation)},
        2u,
        100u};
};
} // namespace arp::test
