#include "../fixtures/original_graphics_pbr_material_data_fixture.h"
#include "renderer/bindless/bindless_pbr_material.h"

#include <cstring>
#include <limits>

#ifdef NDEBUG
#error PBR material tests require assertions
#endif

int main()
{
    {
        using Contract = arp::renderer::BindlessPbrMaterialContract;
        using Record = arp::renderer::BindlessPbrMaterial96;
        Contract::Values values;
        values.baseColorAlpha = {0.2f, 0.3f, 0.4f, 0.75f};
        values.flags = Contract::AlphaMask | Contract::TwoSidedLighting | Contract::AuthoredLeaf;
        values.leafTranslucency = 0.6f;
        values.dielectricF0 = 0.125f;
        values.leafSaturation = 1.25f;
        constexpr auto generation = (std::uint64_t{7u} << 32u) | 11u;
        Contract::Resources resources;
        for (std::size_t i = 0u; i < resources.images.size(); ++i)
            resources.images[i] = {generation, 1u, static_cast<std::uint32_t>(9u - i)};
        resources.sampler = {generation, 2u, 1u};
        std::uint32_t visits = 0u;
        const auto resolve = [&](arp::rhi::BindlessResourceHandle handle, bool sampler) noexcept {
            ++visits;
            return handle.tableId == (sampler ? 2u : 1u) && handle.index < (sampler ? 2u : 10u);
        };
        Record record;
        arp::rhi::BindlessSnapshot unsealed;
        arp::rhi::BindlessShaderArtifact artifact;
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        assert(!Contract::encode(values, resources, generation, resolve, record) && visits == 6u);
        assert(record.baseColorAlpha == values.baseColorAlpha && record.textures[3u] == 6u
            && record.routing[0u] == 5u && record.routing[1u] == 1u
            && record.routing[2u] == values.flags && record.routing[3u] == 1u);
        assert((record.generation == std::array<std::uint32_t, 4u>{11u, 7u, 1u, 2u}));
        assert(record.coverageLeaf[1u] == 0.6f && record.coverageLeaf[2u] == 0.125f);
        const auto encoded = record;
        assert(!Contract::validateRecord(encoded, generation, resolve));
        for (std::size_t lane = 0u; lane < 4u; ++lane) {
            auto invalid = encoded;
            invalid.generation[lane] = 0u;
            assert(Contract::validateRecord(invalid, generation, resolve));
        }
        for (std::size_t lane = 0u; lane < 4u; ++lane) {
            auto invalid = encoded;
            invalid.textures[lane] = 10u;
            assert(Contract::validateRecord(invalid, generation, resolve));
        }
        auto invalidRecord = encoded;
        invalidRecord.routing[3u] = 2u;
        assert(Contract::validateRecord(invalidRecord, generation, resolve));
        invalidRecord = encoded;
        invalidRecord.factors[1u] = std::numeric_limits<float>::quiet_NaN();
        assert(Contract::validateRecord(invalidRecord, generation, resolve));
        const Record empty{};
        const auto reject = [&](const Contract::Values& v, const Contract::Resources& r) {
            std::memset(&record, 0xff, sizeof(record));
            assert(Contract::encode(v, r, generation, resolve, record));
            assert(!std::memcmp(&record, &empty, sizeof(record)));
        };
        auto wrong = resources;
        wrong.images[4u].generation ^= std::uint64_t{1u} << 32u;
        reject(values, wrong);
        wrong = resources;
        wrong.images[1u].tableId = 2u;
        reject(values, wrong);
        wrong = resources;
        wrong.images[2u].index = 10u;
        reject(values, wrong);
        wrong = resources;
        wrong.sampler.generation = 0u;
        reject(values, wrong);
        wrong = resources;
        wrong.sampler.index = 2u;
        reject(values, wrong);
        auto badValues = values;
        badValues.flags = 16u;
        reject(badValues, resources);
        badValues = values;
        badValues.flags &= ~Contract::TwoSidedLighting;
        reject(badValues, resources);
        for (const auto bad : {-1.f,
                 std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::quiet_NaN()}) {
            badValues = values;
            badValues.roughness = bad;
            reject(badValues, resources);
            badValues = values;
            badValues.baseColorAlpha[2u] = bad;
            reject(badValues, resources);
        }
        assert(Contract::encode(unsealed, artifact, values, resources, record));
        assert(!std::memcmp(&record, &empty, sizeof(record)));
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    }
    using Data = arp::test::OriginalGraphicsPbrMaterialDataFixture;
    const auto expected = Data::expected();
    assert(Data::matches(expected));
    // Fixed expected edges distinguish real material semantics from a color-
    // only record: saturation, roughness floor/ceiling, AO clamp and leaf F0.
    assert(expected[8] == 1.f && expected[9] == 0.045f);
    assert(expected[40] > 0.f && expected[40] < 0.1f && expected[41] == 1.f);
    assert(std::abs(expected[10] - 32.f / 255.f) < Data::tolerance && expected[42] == 1.f);
    assert(expected[11] == 1.f && expected[43] == 0.f);
    assert(expected[15] == 0.04f && expected[47] == 0.125f && expected[51] == 0.75f);
    assert(expected[22] > 0.f && expected[86] > 0.f);
    assert(expected[54] > 0.f && expected[118] < 0.f);
    assert(Data::ordinary1()[2] == 1u);
    assert(std::abs(expected[24] - 242.f / 255.f) < Data::tolerance);
    assert(std::abs(expected[25] - 222.875f / 255.f) < Data::tolerance);
    assert(std::abs(expected[26] - 178.f / 255.f) < Data::tolerance);
    for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
        assert(std::abs(expected[pixel * 32u + 24u] - expected[pixel * 32u + 25u]) > 0.01f);
        const auto color = Data::expectedColor(pixel % 2u);
        assert(color[0] == (pixel % 2u ? 0u : 64u) && color[3] == 255u);
    }
    for (const float mode : {0.5f, 1.5f, 1.999f, 3.001f})
        assert(Data::covered(0.f, mode, 0.5f));
    for (const float mode : {0.501f, 1.f, 1.499f, 2.f, 2.125f, 3.f}) {
        assert(!Data::covered(0.499f, mode, 0.5f));
        assert(Data::covered(0.5f, mode, 0.5f));
    }
    // Every output lane is observable; no dropped role/metadata/normal is green.
    for (std::size_t lane = 0u; lane < expected.size(); ++lane) {
        auto corrupt = expected;
        corrupt[lane] += 0.01f;
        assert(!Data::matches(corrupt));
    }
    Data::verify(expected);
    std::puts(
        "BINDLESS_PBR_MATERIAL_DATA_OK transport96 five-roles alpha-boundary leaf backface oracle-refusals");
}
