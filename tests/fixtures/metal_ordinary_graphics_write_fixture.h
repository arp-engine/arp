#pragma once
#include "metal_ordinary_graphics_fixture.h"
#include "rhi/backends/metal/metal_ordinary_graphics_write_execution.h"

namespace arp::test {
class OrdinaryGraphicsWriteFixture {
public:
    enum class Consumer { Shader, Transfer, UniformShader };

    static rhi::DescriptorLayoutDesc layout()
    {
        rhi::DescriptorLayoutDesc result;
        result.name = "AtomicWritePreparation";
        result.bindings = {{3u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Fragment}};
        return result;
    }

    OrdinaryGraphicsWriteFixture(
        const char* vs, const char* fs, Consumer selection = Consumer::Shader)
        : pass(vs, fs, false, layout())
    {
        using namespace rhi;
        auto& f = pass.graphics;
        {
            core::CpuMemoryQuotaScope scope(f.quota);
            buffer = core::shareUnique(f.device->createBuffer({"OwnedWriter",
                sizeof(initial),
                ResourceUsage::StorageBuffer | ResourceUsage::CopySrc
                    | (selection == Consumer::UniformShader ? ResourceUsage::UniformBuffer
                                                            : ResourceUsage::None),
                MemoryUsage::CpuToGpu,
                ResourceState::ShaderWrite}));
        }
        assert(buffer && f.device->writeBuffer(*buffer, initial.data(), sizeof(initial)).success);
        assert(f.set
                ->write({3u,
                    DescriptorType::StorageBuffer,
                    buffer->id(),
                    DescriptorResourceKind::Buffer})
                .success);
        binding = f.prepare();
        const auto& artifact = f.provider->artifacts()[1];
        const auto& bytes = artifact->bytes(shader::ShaderArtifactRole::Spirv);
        shader::FragmentAtomicWriteProof::Facts proof;
        assert(!shader::FragmentAtomicWriteProof::verify(
            bytes.data(), bytes.size(), artifact->entry(), proof));
        PipelineLayoutDesc authored;
        authored.descriptorLayouts = {f.inputs};
        authored.descriptorSets = {{proof.set, f.inputs.name}};
        GraphicsStorageWriteFootprint footprint;
        footprint.set = proof.set;
        footprint.binding = proof.binding;
        footprint.replacement = proof.replacement;
        GraphicsStorageWriteContract contract(
            authored, f.provider->interface().reflection, footprint);
        assert(!contract.diagnostic());
        producer = contract.requiredAccess(buffer->id());
        consumer = producer;
        if (selection != Consumer::Transfer) {
            consumer.state = ResourceState::ShaderRead;
            consumer.access = ResourceAccess::ShaderRead;
        } else {
            consumer.state = ResourceState::CopySrc;
            consumer.access = ResourceAccess::TransferRead;
            consumer.stages = ShaderStage::None;
            consumer.pipelineStages = PipelineStage::Transfer;
        }
        dependency.resource = buffer->id();
        dependency.before = producer.state;
        dependency.after = consumer.state;
        dependency.sourceStages = producer.pipelineStages;
        dependency.sourceAccess = producer.access;
        dependency.destinationStages = consumer.pipelineStages;
        dependency.destinationAccess = consumer.access;
    }

    auto prepare()
    {
        rhi::BindlessAccessArray declarations(&producer, 1u);
        return rhi_metal::MetalRhiOrdinaryPreparedGraphicsWrite::prepare(
            binding, buffer, declarations, dependency, consumer, pass.graphics.diagnostic);
    }

    auto command(std::uint32_t owners, std::uint32_t draws = 1u)
    {
        auto& f = pass.graphics;
        return rhi_metal::MetalRhiOrdinaryWriteCommand::prepare({1u, true, draws},
            core::pmr::String{"OrdinaryWritePrototype"},
            f.registry,
            {1u, true, owners, 0u, 0u, 0u, f.cpuBytes},
            f.quota,
            f.diagnostic);
    }

    void dropInputs()
    {
        binding.reset();
        buffer.reset();
        auto& f = pass.graphics;
        f.set.reset();
        f.arena.reset();
        f.provider.reset();
        f.view.reset();
        f.texture.reset();
        f.sampler.reset();
    }

    OrdinaryGraphicsPassFixture pass;
    std::shared_ptr<rhi::GpuBuffer> buffer;
    std::shared_ptr<const rhi_metal::MetalRhiOrdinaryPreparedGraphicsBinding> binding;
    const std::array<std::uint32_t, 4> initial{0u, 0x91abcdefu, 0x82736455u, 0xfeedfaceu};
    rhi::BindlessResourceAccess producer, consumer;
    rhi::ResourceBarrierDesc dependency;
};
} // namespace arp::test
