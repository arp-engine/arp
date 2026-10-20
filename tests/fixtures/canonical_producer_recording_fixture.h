#pragma once
#include "bindless_package_request_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/bindless_recording_validation.h"
#include "rhi/gpu_extensions.h"
#include "zero_base_indirect_source_fixture.h"

#include <cassert>

namespace arp::test {
// Actual Null logical resources, never a native completion fixture.
class CanonicalProducerRecordingFixture final {
public:
    using Contract = rhi::CanonicalZeroBaseIndirectProducerRecordingContract;
    using Role = Contract::Role;

    CanonicalProducerRecordingFixture(std::shared_ptr<const rhi::BindlessShaderPackage> producer,
        std::shared_ptr<const rhi::BindlessShaderPackage> consumer,
        ZeroBaseIndirectSourceFixture::Order order,
        bool suppressed,
        bool uploadImages = false)
        : device_(rhi::createNullDevice())
    {
        ZeroBaseIndirectSourceFixture fixture(order);
        if (suppressed)
            fixture.put({UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX});
        inputs.producerPackage = std::move(producer);
        inputs.consumerPackage = std::move(consumer);
        inputs.source =
            core::makeShared<rhi::ZeroBaseIndirectSourceConsumptionContract>(fixture.make());
        const auto* extension = static_cast<const rhi::BindlessCrossExtension*>(
            device_->queryInterface(rhi::kExtensionBindlessCross));
        assert(extension);
        const auto request = BindlessPackageRequestFixture::make(
            inputs.consumerPackage->artifact(), extension->capabilities().deviceIdentity);
        rhi::BindlessIndirectPacketPool packets(*device_,
            {1u,
                rhi::BindlessIndirectPacketStorageContract::minimumBytes,
                rhi::BindlessIndirectPacketStorageContract::minimumBytes});
        core::pmr::String diagnostic;
        inputs.packet = packets.prepareShared(
            request, inputs.consumerPackage->artifact(), inputs.source->parameters(), diagnostic);
        assert(inputs.packet);
        inputs.producerFault = rhi::BindlessOwnedFaultDomain::create(*device_,
            {1u, "CanonicalProducer", rhi::BindlessOwnedFaultDomain::minimumBytes},
            diagnostic);
        inputs.consumerFault = rhi::BindlessOwnedFaultDomain::create(*device_,
            {1u, "CanonicalConsumer", rhi::BindlessOwnedFaultDomain::minimumBytes},
            diagnostic);
        assert(inputs.producerFault && inputs.consumerFault);
        const auto create = [&](Role role,
                                std::uint64_t bytes,
                                rhi::ResourceUsage usage,
                                rhi::MemoryUsage memory,
                                rhi::ResourceState state,
                                bool allSeals) {
            auto buffer = core::shareUnique(
                device_->createBuffer({"CanonicalRecordingValue", bytes, usage, memory, state}));
            assert(buffer);
            if (role == Role::Source)
                assert(device_->writeBuffer(*buffer, inputs.source->sourceBytes().data(), bytes)
                        .success);
            if (allSeals)
                buffer->sealWrites();
            else
                buffer->sealHostWrites();
            inputs.buffers[static_cast<std::size_t>(role)] = std::move(buffer);
        };
        create(Role::Source,
            inputs.source->sourceBytes().size(),
            rhi::ResourceUsage::StorageBuffer,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead,
            true);
        create(Role::Metadata,
            sizeof(Contract::Cold::Bytes),
            rhi::ResourceUsage::StorageBuffer,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead,
            true);
        const auto scratch = rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
            | rhi::ResourceUsage::CopyDst;
        create(Role::Arguments,
            sizeof(rhi::DrawIndexedIndirectCommand),
            scratch,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderWrite,
            true);
        create(Role::Mapping,
            sizeof(rhi::IndirectInstanceBase),
            scratch,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderWrite,
            true);
        create(Role::ArgumentsReadback,
            sizeof(rhi::DrawIndexedIndirectCommand),
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::CopyDst,
            false);
        create(Role::MappingReadback,
            sizeof(rhi::IndirectInstanceBase),
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu,
            rhi::ResourceState::CopyDst,
            false);
        inputs.buffers[static_cast<std::size_t>(Role::Latch)] = {
            inputs.producerFault, inputs.producerFault->faultLatchBuffer()};
        inputs.buffers[static_cast<std::size_t>(Role::FaultReadback)] = {
            inputs.producerFault, inputs.producerFault->faultReadbackBuffer()};
        inputs.buffers[static_cast<std::size_t>(Role::Parameters)] = {
            inputs.packet, inputs.packet->producerParameters()};
        for (std::size_t i = 0u; i < inputs.images.size(); ++i) {
            textures_[i] = device_->createTexture({"CanonicalImage",
                {4u, 2u, 1u},
                rhi::TextureFormat::Rgba8Unorm,
                uploadImages ? rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst
                             : rhi::TextureUsage::Sampled,
                rhi::MemoryUsage::GpuOnly,
                rhi::ResourceState::ShaderRead});
            assert(textures_[i]);
            views_[i] = device_->createTextureView(*textures_[i],
                {"CanonicalView", rhi::TextureFormat::Rgba8Unorm, rhi::TextureUsage::Sampled});
            assert(views_[i]);
            assert(!rhi::BindlessTextureInventoryResource::capture(
                *textures_[i], *views_[i], inputs.images[i]));
        }
    }

    void transitionOutputs(rhi::ResourceState before, rhi::ResourceState after)
    {
        auto encoder = device_->createCommandEncoder({"CanonicalLogicalPhase"});
        assert(encoder);
        for (const auto role : {Role::Arguments, Role::Mapping, Role::Latch}) {
            auto& buffer =
                const_cast<rhi::GpuBuffer&>(*inputs.buffers[static_cast<std::size_t>(role)]);
            rhi::ResourceBarrierDesc intent;
            intent.resource = buffer.id();
            intent.before = before;
            intent.after = after;
            const bool toReadback = after == rhi::ResourceState::CopySrc;
            intent.sourceStages =
                toReadback ? rhi::PipelineStage::ComputeShader : rhi::PipelineStage::Transfer;
            intent.sourceAccess =
                toReadback ? rhi::ResourceAccess::ShaderWrite : rhi::ResourceAccess::TransferRead;
            intent.destinationStages =
                toReadback ? rhi::PipelineStage::Transfer : rhi::PipelineStage::ComputeShader;
            intent.destinationAccess =
                toReadback ? rhi::ResourceAccess::TransferRead : rhi::ResourceAccess::ShaderWrite;
            assert(encoder->transition(buffer, intent).success);
        }
        auto command = encoder->finish();
        assert(command);
        // Null commits only logical journal values; it proves no GPU writes.
        assert(device_->queue().submit(*command).success);
    }

    static Contract::Budget budget() noexcept
    {
        return {
            1u, true, Contract::commandUnits, Contract::readbackBytes, Contract::referenceSlots};
    }

    rhi::GpuDevice& device() noexcept { return *device_; }

    rhi::GpuTexture& texture(std::size_t index) noexcept { return *textures_[index]; }

    Contract::Inputs inputs;

private:
    core::UniquePtr<rhi::GpuDevice> device_;
    std::array<core::UniquePtr<rhi::GpuTexture>, Contract::Consumer::Consumer::capacity> textures_;
    std::array<core::UniquePtr<rhi::GpuTextureView>, Contract::Consumer::Consumer::capacity> views_;
};
} // namespace arp::test
