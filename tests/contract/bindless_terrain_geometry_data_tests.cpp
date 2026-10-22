#include "core/memory/cpu_allocator.h"
#include "renderer/bindless/bindless_terrain_geometry.h"

#include <cassert>
#include <limits>
#ifdef NDEBUG
#error Terrain geometry checks require assertions
#endif
int main()
{
    using namespace arp;
    using C = renderer::BindlessTerrainGeometryContract;
    renderer::BindlessTerrainGeometryParameters64 value;
    value.pages = {0u, 2u, 2u, 0u};
    value.foamParameters = {0.f, 0.75f, 1.f, 2.f};
    value.weather[0u] = 0.3f;
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(!C::validateParameters(value, 1u, 2u, 1u, 4u));
    assert(C::validateParameters(value, 1u, 2u, 1u, 3u));
    assert(C::validateParameters(value, 1u, 1u, 1u, 4u));
    assert(C::validateParameters(value, 0u, 2u, 1u, 4u));
    assert(C::validateParameters(value, 1u, 2u, 0u, 4u));
    value.pages[2u] = 1u;
    assert(C::validateParameters(value, 1u, 2u, 1u, 4u));
    value.pages[2u] = 2u;
    value.foamParameters[3u] = -1.f;
    assert(C::validateParameters(value, 1u, 2u, 1u, 4u));
    value.foamParameters[3u] = 4294967040.f;
    assert(C::validateParameters(value, 1u, 2u, 1u, UINT64_MAX));
    value.foamParameters[3u] = 2.49f;
    assert(!C::validateParameters(value, 1u, 2u, 1u, 4u));
    value.foamParameters[3u] = 2.5f;
    assert(C::validateParameters(value, 1u, 2u, 1u, 8u));
    assert(!C::validateParameters(value, 1u, 2u, 1u, 9u));
    value.foamParameters[2u] = 0.f;
    assert(!C::validateParameters(value, 1u, 2u, 1u, 0u));
    value.weather[1u] = 1.f;
    assert(C::validateParameters(value, 1u, 2u, 1u, 0u));
    value.weather[1u] = 0.f;
    for (auto* vector : {&value.foamOriginExtent, &value.foamParameters, &value.weather})
        for (auto& lane : *vector) {
            const auto saved = lane;
            lane = std::numeric_limits<float>::infinity();
            assert(C::validateParameters(value, 1u, 2u, 1u, 0u));
            lane = saved;
        }
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}
