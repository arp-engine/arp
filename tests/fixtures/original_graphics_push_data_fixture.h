#pragma once
#include "original_graphics_matrix_data_fixture.h"

namespace arp::test {
class OriginalGraphicsPushDataFixture final {
public:
    struct Block {
        std::array<core::Mat4, 3u> matrices;
        core::Vec4 flags;
        std::uint32_t cascade, faultHandle, pad1, pad2;
    };

    static_assert(sizeof(Block) == 224u);

    static Block block(bool fault, std::uint32_t update = 0u)
    {
        assert(update < 2u);
        Block result{};
        result.matrices = OriginalGraphicsMatrixDataFixture::matrices().palette;
        result.matrices[2] = {{1, 0, 0, -0.125f, 0, 1, 0, -0.125f, 0, 0, 1, 0, 0, 0, 0, 1}};
        result.flags = update ? core::Vec4{0.125f, 0.25f, 0.125f, 0.25f} : core::Vec4{2, 4, 8, 16};
        result.cascade = 2u;
        result.faultHandle = fault ? 1u : 0u;
        return result;
    }

    static core::Vec4 expected(bool fault, std::uint32_t update = 0u)
    {
        const auto data = block(fault, update);
        return fault ? core::Vec4{-999, -999, -999, -999}
                     : OriginalGraphicsMatrixDataFixture::transform(data.matrices[2], data.flags)
                + data.flags;
    }
};
} // namespace arp::test
