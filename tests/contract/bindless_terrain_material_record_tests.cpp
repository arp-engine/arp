#include "rhi/frontend/bindless_terrain_material.h"

#include <cassert>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Terrain material tests require assertions
#endif

int main()
{
    using namespace arp::rhi;
    using Contract = BindlessTerrainMaterialContract;
    constexpr auto generation = (std::uint64_t{5u} << 32u) | 7u;
    Contract::Values values;
    values.pageGeneration = (std::uint64_t{3u} << 32u) | 9u;
    values.texParams = {0.4f, 0.07f, 0.02f, 0.25f};
    values.weightParams = {1.f, -2.f, 20.f, 0.f};
    values.layerRough012 = {0.5f, 0.6f, 0.7f, 0.8f};
    values.layerRough345 = {0.9f, 1.f, -32.f, 64.f};
    values.brushParams = {-4.f, 2.f, 8.f, 1.f};
    values.brushColor = {0.1f, 0.2f, 0.3f, 1.f};
    Contract::Resources resources;
    for (std::uint32_t layer = 0u; layer < 6u; ++layer)
        for (std::uint32_t role = 0u; role < 4u; ++role)
            resources.layers[layer][role] = {generation, 1u, layer * 4u + role};
    resources.macro = {generation, 1u, 24u};
    resources.weightPages = {{{generation, 2u, 0u}, {generation, 2u, 1u}}};
    resources.sampler = {generation, 3u, 0u};
    std::uint32_t visits = 0u;
    const auto resolve = [&](BindlessResourceHandle handle, std::uint32_t domain) noexcept {
        ++visits;
        return handle.tableId == domain + 1u
            && handle.index < (domain == 0u ? 25u
                       : domain == 1u       ? 2u
                                            : 1u);
    };
    BindlessSnapshot unsealed;
    BindlessShaderArtifact artifact;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    BindlessTerrainMaterial240 record;
    assert(!Contract::encode(values, resources, generation, resolve, record));
    assert(visits == 28u && record.layers[5u][3u] == 23u && record.routing[2u] == 24u);
    assert((record.generation == std::array<std::uint32_t, 4u>{7u, 5u, 9u, 3u}));
    assert(record.texParams == values.texParams && record.layerRough345 == values.layerRough345);
    const auto encoded = record;
    assert(!Contract::validateRecord(encoded, generation, resolve));
    for (std::size_t layer = 0u; layer < 6u; ++layer)
        for (std::size_t role = 0u; role < 4u; ++role) {
            auto invalid = encoded;
            invalid.layers[layer][role] = 25u;
            assert(Contract::validateRecord(invalid, generation, resolve));
        }
    for (const auto lane : {0u, 1u}) {
        auto invalid = encoded;
        invalid.generation[lane] = 0u;
        assert(Contract::validateRecord(invalid, generation, resolve));
    }
    auto invalidRecord = encoded;
    invalidRecord.domains[3u] = 2u;
    assert(Contract::validateRecord(invalidRecord, generation, resolve));
    invalidRecord = encoded;
    invalidRecord.generation[2u] = invalidRecord.generation[3u] = 0u;
    assert(Contract::validateRecord(invalidRecord, generation, resolve));
    const BindlessTerrainMaterial240 empty{};
    const auto reject = [&](const auto& v, const auto& r) {
        std::memset(&record, 0xff, sizeof(record));
        assert(Contract::encode(v, r, generation, resolve, record));
        assert(!std::memcmp(&record, &empty, sizeof(record)));
    };
    // Every authored texture role is checked; no inactive lane can publish.
    for (std::uint32_t layer = 0u; layer < 6u; ++layer)
        for (std::uint32_t role = 0u; role < 4u; ++role) {
            auto wrong = resources;
            wrong.layers[layer][role].generation ^= std::uint64_t{1u} << 32u;
            reject(values, wrong);
            wrong = resources;
            wrong.layers[layer][role].index = 25u;
            reject(values, wrong);
        }
    auto wrong = resources;
    wrong.weightPages[1u].tableId = 1u;
    reject(values, wrong);
    wrong = resources;
    wrong.weightPages[0u].index = 2u;
    reject(values, wrong);
    wrong = resources;
    wrong.sampler.generation = 0u;
    reject(values, wrong);
    for (const auto invalid : {0.f,
             -1.f,
             std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = values;
        bad.texParams[3u] = invalid;
        reject(bad, resources);
    }
    auto bad = values;
    bad.layerRough345[1u] = 1.01f;
    reject(bad, resources);
    bad = values;
    bad.pageGeneration = 0u;
    reject(bad, resources);
    BindlessTerrainPage32 page;
    assert(
        !Contract::encodePage({-16.f, 16.f, -32.f, 0.f}, 3u, 4u, 5u, values.pageGeneration, page));
    assert((page.routing == std::array<std::uint32_t, 4u>{3u, 9u, 3u, 0u}));
    const BindlessTerrainPage32 emptyPage{};
    for (const auto bounds : {std::array<float, 4u>{1.f, 1.f, -2.f, 2.f},
             std::array<float, 4u>{2.f, 1.f, -2.f, 2.f},
             std::array<float, 4u>{0.f, 1.f, 0.f, std::numeric_limits<float>::infinity()}}) {
        assert(Contract::encodePage(bounds, 0u, 4u, 4u, values.pageGeneration, page));
        assert(!std::memcmp(&page, &emptyPage, sizeof(page)));
    }
    assert(Contract::encodePage({0.f, 1.f, 0.f, 1.f}, 3u, 4u, 3u, values.pageGeneration, page));
    assert(!std::memcmp(&page, &emptyPage, sizeof(page)));
    assert(Contract::encodeCandidate(unsealed, artifact, values, resources, record));
    assert(Contract::encode(unsealed, artifact, values, resources, record));
    assert(!std::memcmp(&record, &empty, sizeof(record)));
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    std::puts(
        "BINDLESS_TERRAIN_MATERIAL_RECORD_OK layout240 pages32 all27-roles generation refusal zero-allocation");
}
