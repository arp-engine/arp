#include "platform/render_backend_preference.h"

#include <array>
#include <cassert>
#include <cstring>

#ifdef NDEBUG
#error Backend selection contract tests require assertions
#endif

using arp::platform::BackendPreference;
using arp::platform::RenderSystemBackendSelection;
using arp::rhi::Backend;

int main()
{
    static_assert(static_cast<int>(BackendPreference::Default) == 0);
    static_assert(static_cast<int>(BackendPreference::Vulkan) == 1);
    static_assert(static_cast<int>(BackendPreference::Metal) == 2);
    static_assert(static_cast<int>(BackendPreference::WebGPU) == 3);
    static_assert(static_cast<int>(BackendPreference::Null) == 4);
    static_assert(static_cast<int>(BackendPreference::WebGL) == 5);
    static_assert(static_cast<int>(Backend::Null) == 0);
    static_assert(static_cast<int>(Backend::Vulkan) == 1);
    static_assert(static_cast<int>(Backend::Metal) == 2);
    static_assert(static_cast<int>(Backend::WebGPU) == 3);
    static_assert(static_cast<int>(Backend::WebGL) == 4);
    static_assert(RenderSystemBackendSelection::contractVersion == 1u);

    constexpr std::array backends{
        Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL};
    constexpr std::array preferences{BackendPreference::Null,
        BackendPreference::Vulkan,
        BackendPreference::Metal,
        BackendPreference::WebGPU,
        BackendPreference::WebGL};
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (auto compiled : backends) {
        const bool hasFactory = compiled == Backend::Vulkan || compiled == Backend::Metal
            || compiled == Backend::WebGPU;
        for (std::size_t i = 0; i < backends.size(); ++i) {
            const auto route = RenderSystemBackendSelection::forBuild(preferences[i], compiled);
            assert(route.requestedBackend == backends[i]);
            assert(route.routeToFactory == (hasFactory && backends[i] == compiled));
            assert((route.diagnostic == nullptr) == route.routeToFactory);
            if (compiled == Backend::WebGL || backends[i] == Backend::WebGL)
                assert(std::strcmp(route.diagnostic, "RHI-WEBGL1-RENDERSYSTEM-UNAVAILABLE") == 0);
        }
        const auto defaults =
            RenderSystemBackendSelection::forBuild(BackendPreference::Default, compiled);
        assert(defaults.requestedBackend == compiled && defaults.routeToFactory == hasFactory);
        for (auto unknown :
            {static_cast<BackendPreference>(-1), static_cast<BackendPreference>(255)}) {
            const auto route = RenderSystemBackendSelection::forBuild(unknown, compiled);
            assert(!route.routeToFactory);
            assert(std::strcmp(route.diagnostic, "RHI-PLATFORM-BACKEND-PREFERENCE-UNKNOWN") == 0);
        }
    }
    for (auto preference : {BackendPreference::Default,
             BackendPreference::Null,
             BackendPreference::Vulkan,
             BackendPreference::Metal,
             BackendPreference::WebGPU,
             BackendPreference::WebGL}) {
        const auto unknown =
            RenderSystemBackendSelection::forBuild(preference, static_cast<Backend>(255));
        assert(!unknown.routeToFactory);
        assert(std::strcmp(unknown.diagnostic, "RHI-PLATFORM-BACKEND-BUILD-UNKNOWN") == 0);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
}
