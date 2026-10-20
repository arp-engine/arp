#include "rhi/frontend/resource_cache_accounting.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>

int main()
{
    using namespace arp::rhi;
    using Domain = ResourceCacheAccountingDomain;
    const ResourceCacheAccountingRequest logical{1u, Domain::LogicalDescriptor};
    TextureDesc texture;
    texture.extent = {8u, 4u, 1u};
    texture.format = TextureFormat::Rgba8Unorm;
    texture.mipLevelCount = 2u;
    texture.arrayLayerCount = 2u;
    BufferDesc buffer;
    buffer.sizeBytes = 13u;
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (const auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        const auto result = ResourceCacheAccounting::query(backend, texture, logical);
        assert(result.success() && result.logicalBytes == (8u * 4u + 4u * 2u) * 4u * 2u);
        assert(result.nativePhysicalObservation.state
            == semantics::PhysicalBudgetObservationState::Unknown);
        assert(semantics::validPhysicalBudgetObservation(result.nativePhysicalObservation));
        assert(ResourceCacheAccounting::query(backend, buffer, logical).logicalBytes == 13u);
        for (const auto request : {ResourceCacheAccountingRequest{},
                 ResourceCacheAccountingRequest{0u, Domain::LogicalDescriptor},
                 ResourceCacheAccountingRequest{2u, Domain::LogicalDescriptor},
                 ResourceCacheAccountingRequest{1u, Domain::NativePhysical},
                 ResourceCacheAccountingRequest{1u, static_cast<Domain>(99)}}) {
            const auto rejected = ResourceCacheAccounting::query(backend, texture, request);
            assert(!rejected.success() && !rejected.logicalBytes && rejected.diagnostic);
            assert(rejected.nativePhysicalObservation.state
                == semantics::PhysicalBudgetObservationState::Unknown);
            assert(!ResourceCacheAccounting::query(backend, buffer, request).success());
        }
        const auto physical =
            ResourceCacheAccounting::query(backend, texture, {1u, Domain::NativePhysical});
        assert(std::string_view(physical.diagnostic)
            == "RHI-CACHE-ACCOUNTING-NATIVE-PHYSICAL-UNAVAILABLE");
    }
    assert(!ResourceCacheAccounting::query(static_cast<Backend>(99), texture, logical).success());
    assert(!ResourceCacheAccounting::query(static_cast<Backend>(99), buffer, logical).success());
    auto samples = texture;
    samples.mipLevelCount = 1u;
    samples.sampleCount = 4u;
    assert(ResourceCacheAccounting::query(Backend::Null, samples, logical).logicalBytes
        == 8u * 4u * 4u * 2u * 4u);
    samples.sampleCount = 3u;
    assert(!ResourceCacheAccounting::query(Backend::Null, samples, logical).success());
    auto blocks = texture;
    blocks.extent = {5u, 5u, 1u};
    blocks.format = TextureFormat::Bc7Unorm;
    assert(ResourceCacheAccounting::query(Backend::Null, blocks, logical).logicalBytes
        == (2u * 2u + 1u) * 16u * 2u);
    for (const auto count : {0u, 100u}) {
        auto invalid = texture;
        invalid.mipLevelCount = count;
        assert(!ResourceCacheAccounting::query(Backend::Null, invalid, logical).success());
    }
    auto invalid = texture;
    invalid.extent.width = 0u;
    assert(!ResourceCacheAccounting::query(Backend::Null, invalid, logical).success());
    invalid = texture;
    invalid.format = TextureFormat::Unknown;
    assert(!ResourceCacheAccounting::query(Backend::Null, invalid, logical).success());
    invalid = texture;
    invalid.extent = {
        std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max(), 1u};
    assert(!ResourceCacheAccounting::query(Backend::Null, invalid, logical).success());
    invalid = texture;
    invalid.extent = {1u << 31u, 1u << 31u, 1u};
    invalid.format = TextureFormat::R8Unorm;
    invalid.mipLevelCount = 1u;
    invalid.arrayLayerCount = 1u;
    invalid.sampleCount = 4u;
    const auto sampleOverflow = ResourceCacheAccounting::query(Backend::Null, invalid, logical);
    assert(!sampleOverflow.success() && !sampleOverflow.logicalBytes);
    assert(std::string_view(sampleOverflow.diagnostic) == "RHI-CACHE-ACCOUNTING-BYTE-OVERFLOW");
    buffer.sizeBytes = 0u;
    assert(!ResourceCacheAccounting::query(Backend::Null, buffer, logical).success());
    std::uint64_t sum = std::numeric_limits<std::uint64_t>::max() - 1u;
    assert(!ResourceCacheAccounting::accumulate(sum, 1u));
    assert(ResourceCacheAccounting::accumulate(sum, 1u));
    assert(sum == std::numeric_limits<std::uint64_t>::max());
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
    std::puts("ResourceCacheAccounting/1 logical query, physical Unknown, five backends: PASS");
}
