#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/resource_texture_state_range.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalTextureStateRangeFixture final {
    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verify()
    {
        using Grid = core::RowMajorIndexGrid;
        using Geometry = rhi::semantics::OriginalTextureStateRange;
        using A = rhi::TextureAspect;
        using R = rhi::TextureSubresourceRange;
        assert(!Grid(0u, 1u).valid() && !Grid(1u, 0u).valid());
        assert(!Grid(UINT64_MAX, 2u).valid() && Grid(1u, UINT64_MAX).size() == UINT64_MAX);
        std::uint64_t index = 0u, row = 0u, column = 0u;
        assert(Grid(1u, UINT64_MAX).index(0u, UINT64_MAX - 1u, index) && index == UINT64_MAX - 1u);
        assert(!Grid(1u, UINT64_MAX).index(0u, UINT64_MAX, index) && index == 0u);
        assert(Grid(3u, 4u, 12u).coordinates(11u, row, column) && row == 2u && column == 3u);
        assert(!Grid(3u, 4u, 11u).valid() && !Grid(3u, 4u).coordinates(12u, row, column));
        rhi::TextureDesc desc;
        desc.extent = {8u, 8u, 1u};
        desc.mipLevelCount = 3u;
        desc.arrayLayerCount = 4u;
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        const char* diagnostic = nullptr;
        const auto make = [&](std::uint64_t capacity) {
            return Geometry::capture(desc, capacity, diagnostic);
        };
        auto geometry = make(12u);
        assert(geometry && geometry->cellCount() == 12u && geometry->matches(desc));
        assert(!make(11u));
        is(diagnostic, "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
        const R whole{0u, 3u, 0u, 4u, A::Color}, part{1u, 2u, 1u, 2u, A::Color},
            overlap{2u, 1u, 2u, 1u, A::Color};
        assert(geometry->isWholeAspect(whole) && !geometry->isWholeAspect(part)
            && geometry->rangeCellCount(part) == 4u);
        const std::array<std::uint64_t, 4> expected{5u, 6u, 9u, 10u};
        for (std::size_t i = 0u; i < expected.size(); ++i)
            assert(geometry->cellIndex(part, i, index) && index == expected[i]);
        assert(geometry->cellIndex(overlap, 0u, index) && index == 10u
            && !geometry->cellIndex(part, 4u, index));
        for (const auto invalid : std::array<R, 4>{{{UINT32_MAX, 2u, 0u, 1u, A::Color},
                 {0u, 0u, 0u, 1u, A::Color},
                 {0u, 1u, UINT32_MAX, 2u, A::Color},
                 {0u, 1u, 0u, 0u, A::Color}}})
            is(geometry->validate(invalid), "RHI-ORIGINAL-TEXTURE-RANGE-BOUNDS");
        is(geometry->validate({0u, 1u, 0u, 1u, A::Depth}), "RHI-ORIGINAL-TEXTURE-RANGE-ASPECT");
        is(geometry->validate({0u, 1u, 0u, 1u, static_cast<A>(UINT32_MAX)}),
            "RHI-ORIGINAL-TEXTURE-RANGE-ASPECT");
        desc.format = rhi::TextureFormat::Depth24Stencil8;
        const auto depthStencil = make(24u);
        assert(depthStencil && depthStencil->cellCount() == 24u);
        assert(depthStencil->cellIndex({2u, 1u, 2u, 1u, A::Depth}, 0u, index) && index == 10u);
        assert(depthStencil->cellIndex({2u, 1u, 2u, 1u, A::Stencil}, 0u, index) && index == 22u);
        R reversed;
        for (std::uint64_t cell = 0u; cell < depthStencil->cellCount(); ++cell) {
            assert(depthStencil->cellRange(cell, reversed));
            assert(depthStencil->cellIndex(reversed, 0u, index) && index == cell);
            assert(depthStencil->containsCell(reversed, cell));
            const auto opposite = cell < 12u ? cell + 12u : cell - 12u;
            assert(!depthStencil->containsCell(reversed, opposite));
        }
        assert(!depthStencil->cellRange(24u, reversed));
        assert(geometry->containsCell(part, 5u) && geometry->containsCell(part, 10u)
            && !geometry->containsCell(part, 4u) && !geometry->containsCell(part, 11u));
        assert(!make(23u));
        is(diagnostic, "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
        desc.mipLevelCount = desc.arrayLayerCount = UINT32_MAX;
        assert(!make(UINT64_MAX));
        is(diagnostic, "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
        desc.mipLevelCount = 3u;
        desc.arrayLayerCount = 1u;
        desc.format = rhi::TextureFormat::D32Float;
        const auto depth = make(3u);
        assert(depth && depth->cellCount() == 3u);
        is(depth->validate({0u, 1u, 0u, 1u, A::Stencil}), "RHI-ORIGINAL-TEXTURE-RANGE-ASPECT");
        desc.format = rhi::TextureFormat::Bc7Unorm;
        desc.arrayLayerCount = 6u;
        desc.cubeCompatible = true;
        const auto cube = make(18u);
        assert(cube && cube->cellCount() == 18u);
        desc.cubeCompatible = false;
        desc.extent.depth = 8u;
        desc.arrayLayerCount = 1u;
        const auto volume = make(3u);
        assert(volume && volume->cellCount() == 3u);
        desc.arrayLayerCount = 2u;
        assert(!make(6u));
        is(diagnostic, "RHI-ORIGINAL-TEXTURE-RANGE-DIMENSION");
        desc.arrayLayerCount = 1u;
        desc.format = static_cast<rhi::TextureFormat>(UINT32_MAX);
        assert(!make(3u));
        is(diagnostic, "RHI-ORIGINAL-TEXTURE-RANGE-FORMAT");
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.extent.depth = 1u;
        desc.arrayLayerCount = 4u;
        const auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            const auto current = make(12u);
            if (!current || !current->cellIndex(part, 3u, index) || index != 10u)
                return "TEST-RANGE-MAPPING";
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                if (current->originalRangeIssuerAvailable()
                    || Geometry::lowering(backend).originalIssuerAvailable
                    || Geometry::lowering(backend).executionAvailable)
                    return "TEST-RANGE-ISSUER-OPEN";
            return static_cast<const char*>(nullptr);
        }));
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TEXTURE_STATE_RANGE_OK exact-mip-layer-aspect overlap-canonical overflow-capacity aspect-separation cube3D no-partial-widening hot-allocation0 issuers4-closed");
    }
};
} // namespace arp::test
