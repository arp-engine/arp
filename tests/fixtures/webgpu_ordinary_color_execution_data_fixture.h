#pragma once
#include "webgpu_ordinary_color_closure_fixture.h"
#include "webgpu_original_graphics_matrix_data_fixture.h"

namespace arp::test {
// Data and original upload only; the existing public callback fixture owns
// recording, fault/readback, failure scenarios and retirement.
template <class Data, bool Hdr> class WebGpuOrdinaryColorExecutionDataFixture {
public:
    using DataModel = Data;
    static constexpr std::size_t readbackBytes = sizeof(typename Data::Numeric);
    static constexpr std::uint32_t resultBinding = Data::resultBinding;
    static constexpr const char* sampledReceipt = Hdr ? "ORDINARY_HDR" : "ORDINARY_COLOR";

    template <class... Args> static void inspectOriginalRoot(const Args&...) {}

    static void configureRequest(rhi::BindlessRequirements& request)
    {
        assert(request.tables.size() == 1u);
        request.tables.front().resourceCount = 1u;
    }

    static void configureOrdinaryBuffer(
        rhi::BufferDesc&, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        assert(set == 0u && binding.binding == 3u);
    }

    static void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        assert(set == 0u && binding.binding == 3u);
        const std::array<float, 4u> frame{17.f, 0.f, 0.f, 0.f};
        WebGpuOriginalGraphicsMatrixDataFixture::write(buffer, frame.data(), sizeof(frame));
    }

    template <class Table> static void configureBuffer(rhi::BufferDesc& desc, const Table&)
    {
        desc.sizeBytes = readbackBytes;
    }

    template <class Table>
    static void initializeBuffer(rhi::GpuBuffer& buffer, const Table&, std::uint32_t index)
    {
        assert(index == 0u);
        typename Data::Numeric poison;
        poison.fill(-999.f);
        WebGpuOriginalGraphicsMatrixDataFixture::write(buffer, poison.data(), sizeof(poison));
    }

    static auto createCompletedOrdinaryTexture(rhi::GpuDevice& device,
        rhi::TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const rhi::DescriptorBindingDesc& binding)
    {
        return WebGpuOrdinaryColorResourcesFixture<Data, Hdr>{}.createCompletedOrdinaryTexture(
            device, desc, quota, set, binding);
    }

    static void verify(const std::array<std::uint8_t, 512u + readbackBytes>& actual, bool fault)
    {
        typename Data::Numeric numbers;
        std::memcpy(numbers.data(), actual.data() + 512u, readbackBytes);
        Data::verify(numbers);
        for (std::size_t y = 0u; y < 2u; ++y)
            for (std::size_t x = 0u; x < 2u; ++x) {
                const auto expected = Data::expectedColor(x);
                for (std::size_t c = 0u; c < expected.size(); ++c)
                    assert(std::abs(int(actual[y * 256u + x * 4u + c]) - int(expected[c])) <= 1);
            }
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_NUMERIC_OK fault%u lanes%zu original-upload 2D-array-Cube mips2 vertex-fragment independent-sampler color2x2\n",
            sampledReceipt,
            fault ? 1u : 0u,
            numbers.size());
    }
};
} // namespace arp::test
