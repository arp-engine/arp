#pragma once
#include "metal_completed_texture_root_fixture.h"

namespace arp::test {
class MetalCompletedSampledGraphicsFixture : public MetalCompletedTextureUploadFixture {
public:
    static constexpr bool enabled = false;
    bool retireDevice = false;

    bool originalDeviceLifetimeRetired() const noexcept { return retireDevice; }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        MetalCompletedTextureUploadFixture::configureAdmission(budget);
        budget.maxCallbackTickets = budget.maxCallbackRegistrations = 0u;
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 0u;
    }

    void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t, const rhi::DescriptorBindingDesc&) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
    }

    void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding) const
    {
        std::array<std::uint32_t, 4u> words{};
        if (set == 0u && binding.binding == 3u) {
            const float frame = 32.f;
            std::memcpy(words.data(), &frame, sizeof(frame));
        } else if (set == 2u && binding.binding == 3u)
            words[0u] = 17u;
        else
            assert(set == 2u && binding.binding == 4u);
        core::pmr::String diagnostic;
        assert(buffer.desc().sizeBytes == sizeof(words));
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            words.data(),
            sizeof(words),
            0u,
            sizeof(words),
            diagnostic));
    }
};

void verifyMetalCompletedSampledGraphics2D(
    const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
void verifyMetalCompletedSampledGraphicsArray(
    const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
void verifyMetalInitializedGeometryGraphics(
    const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool);
} // namespace arp::test
