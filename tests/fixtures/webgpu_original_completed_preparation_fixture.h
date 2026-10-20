#pragma once
#include "original_completed_sampled_input_fixture.h"
#include "webgpu_original_sampled_preparation_fixture.h"

namespace arp::test {
// The existing public cold Root now consumes genuine uploads. It still grants
// no shader execution and never changes CopyDst into ShaderRead on the CPU.
struct WebGpuOriginalCompletedPreparationRequest : WebGpuOriginalSampledPreparationRequest {
    static constexpr bool completedTextures = true;

    static constexpr std::uint32_t completedTexturePublicationVersion() noexcept { return 2u; }

    static constexpr std::uint64_t bytes = 65536u;
    void (*initializeTexturePixels)(
        std::uint32_t, std::uint32_t, core::pmr::Vector<std::uint8_t>&) = nullptr;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    void prepareOriginalDevice(rhi::GpuDevice& device) const
    {
        assert(!device.prepareOriginalDeviceNativeLoss({{1u, true, 1u, 1u, bytes}, 1u}, quota()));
    }

    void configureRegistration(rhi::OriginalResourceRegistrationBudget& registration) const
    {
        registration.contractVersion = 9u;
        ++registration.maxBuffers; // One actual sequential upload staging buffer.
        registration.maxRetainedOwners +=
            rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
    }

    void prepareRegistration(rhi::GpuDevice& device,
        const rhi::OriginalResourceRegistrationBudget& registration,
        const std::shared_ptr<core::CpuMemoryQuota>& owner) const
    {
        const char* id = nullptr;
        const auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        assert(registration.samplers.requested());
        assert(!device.prepareOriginalDescriptorNativeLossRegistration(
            {3u, true}, registration, owner, *loss));
    }

    void configureSampler(rhi::SamplerDesc& desc, std::uint32_t index) const
    {
        desc.minFilter = desc.magFilter =
            index ? rhi::SamplerFilter::Linear : rhi::SamplerFilter::Nearest;
    }

    std::shared_ptr<const rhi::OriginalCommandTextureLease> createCompletedTexture(
        rhi::GpuDevice& device,
        rhi::TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        std::uint32_t index) const
    {
        OriginalCompletedSampledInputFixture::Context context(
            device, sourceQuota, quota(), desc.arrayLayerCount, desc.format, desc.cubeCompatible);
        const auto parent = context.binding->originalDestinationLease();
        if (index || initializeTexturePixels) {
            context.buffer.reset();
            context.prepareBinding(parent,
                static_cast<std::uint8_t>(index * 32u),
                true,
                initializeTexturePixels,
                index);
        }
        OriginalCompletedSampledInputFixture::Quotas quotas;
        const auto initialization = OriginalCompletedSampledInputFixture::prepare(context, quotas);
        const auto content = parent->originalObject()->observeOriginalContent();
        assert(content && content->controlVersion() == 1u && content->contentGeneration() == 2u
            && parent->originalObject()->state() == rhi::ResourceState::CopyDst);
        desc = parent->originalObject()->desc();
        return parent;
    }
};
} // namespace arp::test
