#pragma once
#include "webgpu_ordinary_color_execution_data_fixture.h"
#include "wocc_ibl_frozen_inputs_fixture.h"

namespace arp::test {
// Same frozen inputs and public ordinary-color execution as Native. Only the
// test's authored resources and diagnostic numeric readback differ.
template <bool Reference>
class WebGpuWoccIblExecutionDataFixture final
    : public WebGpuOrdinaryColorExecutionDataFixture<OriginalGraphicsWoccIblDataFixture, true> {
    using Data = OriginalGraphicsWoccIblDataFixture;
    using Base = WebGpuOrdinaryColorExecutionDataFixture<Data, true>;

public:
    static constexpr auto originalRootCpuBytes = Data::originalRootCpuBytes;
    static constexpr std::uint32_t ordinaryResultBinding = Reference ? 4u : UINT32_MAX;
    static constexpr const char* sampledReceipt = Reference ? "WOCC_IBL_REFERENCE" : "WOCC_IBL";

    static void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        if (set == 0u && binding.binding == 4u) {
            assert(Reference);
            desc.sizeBytes = sizeof(Data::Numeric);
        } else
            Base::configureOrdinaryBuffer(desc, set, binding);
    }

    static void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        if (set == 0u && binding.binding == 4u) {
            assert(Reference);
            Base::initializeBuffer(buffer, binding, 0u);
        } else
            Base::initializeOrdinaryBuffer(buffer, set, binding);
    }

    static auto createCompletedOrdinaryTexture(rhi::GpuDevice& device,
        rhi::TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const rhi::DescriptorBindingDesc& binding)
    {
        assert(set == 2u && Data::inputs);
        auto upload = Data::textureUpload(*Data::inputs, desc, binding);
        OriginalCompletedSampledInputFixture::Context context(device,
            quota,
            WebGpuOriginalCompletedPreparationRequest::quota(),
            desc,
            std::move(upload));
        const auto parent = context.binding->originalDestinationLease();
        OriginalCompletedSampledInputFixture::Quotas quotas;
        const auto completed = OriginalCompletedSampledInputFixture::prepare(context, quotas);
        const auto content = parent->originalObject()->observeOriginalContent();
        assert(content && content->controlVersion() == 1u && content->contentGeneration() == 2u
            && parent->originalObject()->state() == rhi::ResourceState::CopyDst);
        desc = parent->originalObject()->desc();
        return parent;
    }

    static void verify(
        const std::array<std::uint8_t, 512u + Base::readbackBytes>& actual, bool fault)
    {
        Data::Numeric numbers;
        std::memcpy(numbers.data(), actual.data() + 512u, sizeof(numbers));
        if constexpr (Reference)
            Data::verifyReference(numbers);
        else
            Data::verify(numbers);
        for (std::size_t y = 0u; y < 2u; ++y)
            for (std::size_t x = 0u; x < 2u; ++x) {
                const auto expected = Data::expectedColor(x);
                for (std::size_t c = 0u; c < expected.size(); ++c)
                    assert(actual[y * 256u + x * 4u + c] == expected[c]);
            }
        EM_ASM(
            {
                // clang-format off
                Module.woccIblReadbacks = Module.woccIblReadbacks || [];
                Module.woccIblReadbacks.push({reference: !!$0, fault: !!$1,
                    words: Array.from(HEAPU32.subarray($2 >>> 2, ($2 >>> 2) + $3))});
                // clang-format on
            },
            Reference,
            fault,
            numbers.data(),
            numbers.size());
        std::printf(
            "BINDLESS_WEBGPU_%s_NUMERIC_OK fault%u lanes424 unfiltered80-exact reference-bitwise color2x2\n",
            sampledReceipt,
            fault ? 1u : 0u);
    }
};
} // namespace arp::test
