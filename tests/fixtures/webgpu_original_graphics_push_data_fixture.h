#pragma once
#include "original_graphics_push_data_fixture.h"
#include "webgpu_original_graphics_matrix_data_fixture.h"

#include <cmath>

namespace arp::test {
// Data-only oracle; original Root execution and completion remain shared.
template <std::uint32_t Updates> class WebGpuOriginalGraphicsPushDataFixtureImpl final {
public:
    static constexpr bool graphicsPush = true;
    static constexpr std::uint32_t pushUpdates = Updates;
    static_assert(Updates == 1u || Updates == 2u);
    static constexpr std::size_t readbackBytes = sizeof(core::Vec4);

    template <class Root, class Sources>
    static void inspectOriginalRoot(const Root&, const Sources&, rhi::GpuDevice*, bool)
    {}

    static void configureRequest(rhi::BindlessRequirements& request)
    {
        assert(request.tables.size() == 1u);
        request.tables.front().resourceCount = 1u;
    }

    static void configureOrdinaryBuffer(
        rhi::BufferDesc&, std::uint32_t, const rhi::DescriptorBindingDesc&)
    {
        assert(false); // This package declares no ordinary descriptors.
    }

    static void initializeOrdinaryBuffer(
        rhi::GpuBuffer&, std::uint32_t, const rhi::DescriptorBindingDesc&)
    {
        assert(false);
    }

    template <class Table> static void configureBuffer(rhi::BufferDesc& desc, const Table& table)
    {
        assert(table.tableId == 1u);
        desc.sizeBytes = readbackBytes;
    }

    template <class Table>
    static void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index)
    {
        assert(table.tableId == 1u && index == 0u);
        const core::Vec4 poison{-999, -999, -999, -999};
        WebGpuOriginalGraphicsMatrixDataFixture::write(buffer, &poison, sizeof(poison));
    }

    static void verify(const std::array<std::uint8_t, 512u + readbackBytes>& actual, bool fault)
    {
        const auto expected = OriginalGraphicsPushDataFixture::expected(fault);
        assert(!std::memcmp(actual.data() + 512u, &expected, sizeof(expected)));
        // First draw writes pixel0's exact float result. For two updates, the
        // right column must use the second slice and an unsaturated result;
        // overwriting slice0 or binding offset0 for draw2 fails this oracle.
        const auto second = OriginalGraphicsPushDataFixture::expected(false, 1u);
        const std::array<float, 4u> secondColor{second.x, second.y, second.z, second.w};
        for (std::size_t y = 0u; y < 2u; ++y)
            for (std::size_t x = 0u; x < 2u; ++x)
                for (std::size_t c = 0u; c < 4u; ++c) {
                    const auto channel = actual[y * 256u + x * 4u + c];
                    if constexpr (Updates == 2u) {
                        if (x == 1u) {
                            assert(std::abs(float(channel) - secondColor[c] * 255.f) <= 0.5f);
                            continue;
                        }
                    }
                    assert(channel == 255u);
                }
        if constexpr (Updates == 2u)
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_MULTI_NUMERIC_OK fault%u lanes4 exact frozen448 updates2 scissor-columns color2x2\n",
                fault ? 1u : 0u);
        else
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_NUMERIC_OK fault%u lanes4 exact frozen224 vertex-fragment color2x2\n",
                fault ? 1u : 0u);
    }
};

using WebGpuOriginalGraphicsPushDataFixture = WebGpuOriginalGraphicsPushDataFixtureImpl<1u>;
using WebGpuOriginalGraphicsPushMultiDataFixture = WebGpuOriginalGraphicsPushDataFixtureImpl<2u>;
} // namespace arp::test
