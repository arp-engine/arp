#include "../fixtures/original_graphics_terrain27_data_fixture.h"

#ifdef NDEBUG
#error Terrain27 data tests require assertions
#endif

int main()
{
    using Data = arp::test::OriginalGraphicsTerrain27DataFixture;
    auto expected = Data::expected();
    assert(Data::matches(expected));
    for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
        float sum = 0.f;
        for (std::size_t i = 8u; i < 14u; ++i)
            sum += expected[pixel * 32u + i];
        assert(std::abs(sum - 1.f) < 1e-6f);
        assert(expected[pixel * 32u + 28u] == 1.f + pixel % 2u
            && expected[pixel * 32u + 29u] == pixel / 2u);
    }
    for (std::size_t i = 0u; i < expected.size(); ++i) {
        auto bad = expected;
        bad[i] += 0.01f;
        assert(!Data::matches(bad));
    }
    assert(expected[16] != expected[48] && expected[0] != expected[64]);
    for (std::uint32_t count : {1u, 2u})
        for (std::uint32_t image = 0u; image < (count == 1u ? 25u : 2u); ++image) {
            arp::core::pmr::Vector<std::uint8_t> data(20u * count);
            Data::fillTexture(image, count, data);
            for (std::uint32_t layer = 0u; layer < count; ++layer)
                for (std::size_t channel = 0u; channel < 4u; ++channel)
                    assert(data[layer * 16u + channel] + data[16u * count + layer * 4u + channel]
                        == 255u);
        }
    std::puts("BINDLESS_TERRAIN27_DATA_OK ABI32 pages2 layers2 mip2 normalized128 hostile128");
}
