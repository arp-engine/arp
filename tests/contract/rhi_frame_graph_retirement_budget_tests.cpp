#include "core/container/single_use_reservation_pool.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/frame_graph_retirement_budget.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string_view>
#include <tuple>

namespace {
using namespace arp::rhi;
using namespace arp::rhi::semantics;
using Counters = FrameGraphRetirementCounters;
using Preflight = FrameGraphRetirementBudgetPreflight;
using MetadataReservation = arp::core::SingleUseReservationPool<Counters>::Reservation;
static_assert(!std::is_constructible_v<MetadataReservation, FrameGraphRetirementBudgetResult>);
static_assert(!std::is_constructible_v<MetadataReservation, QueueSubmitResult>);
static_assert(!std::is_constructible_v<MetadataReservation, GpuFence*>);
static_assert(!std::is_constructible_v<MetadataReservation, arp::core::PoolHandle>);
constexpr std::array fields{&Counters::logicalTextureBytes,
    &Counters::logicalBufferBytes,
    &Counters::logicalAliasBytes,
    &Counters::textureCount,
    &Counters::bufferCount,
    &Counters::aliasHeapCount,
    &Counters::frameBundleCount,
    &Counters::cpuMetadataBytes,
    &Counters::referenceCount};
constexpr std::array partitions{&FrameGraphRetirementUsage::inUse,
    &FrameGraphRetirementUsage::reserved,
    &FrameGraphRetirementUsage::pending,
    &FrameGraphRetirementUsage::cached};

auto values(const Counters& c)
{
    return std::tie(c.logicalTextureBytes,
        c.logicalBufferBytes,
        c.logicalAliasBytes,
        c.textureCount,
        c.bufferCount,
        c.aliasHeapCount,
        c.frameBundleCount,
        c.cpuMetadataBytes,
        c.referenceCount);
}

void expect(const FrameGraphRetirementBudgetResult& result, const char* diagnostic)
{
    assert(!result.fits() && result.diagnostic);
    assert(std::string_view(result.diagnostic) == diagnostic);
    assert(values(result.projected) == values(Counters{}));
}
} // namespace

int main()
{
    using namespace arp;
    auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    std::size_t cases = 0u;
    {
        core::CpuMemoryQuotaScope scope(quota);
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            FrameGraphRetirementBudget budget{1u, {}};
            FrameGraphRetirementBudgetRequest request{
                1u, ResourceCacheAccountingDomain::LogicalDescriptor, {}};
            assert(Preflight::assess(backend, budget, {}, request).fits());
            for (std::size_t index = 0u; index < fields.size(); ++index) {
                const auto field = fields[index];
                for (const auto partition : partitions) {
                    FrameGraphRetirementUsage usage;
                    budget.capacity = {};
                    request.addition = {};
                    budget.capacity.*field = 7u;
                    (usage.*partition).*field = 4u;
                    request.addition.*field = 2u;
                    const auto usageBefore = usage;
                    auto result = Preflight::assess(backend, budget, usage, request);
                    assert(result.fits() && result.projected.*field == 6u);
                    request.addition.*field = 3u;
                    result = Preflight::assess(backend, budget, usage, request);
                    assert(result.fits() && result.projected.*field == 7u);
                    request.addition.*field = 4u;
                    result = Preflight::assess(backend, budget, usage, request);
                    expect(result, "RG-RETIREMENT-BUDGET-CAPACITY");
                    assert(result.dimension == index);
                    request.addition = {};
                    budget.capacity.*field = 3u;
                    expect(Preflight::assess(backend, budget, usage, request),
                        "RG-RETIREMENT-BUDGET-LIVE-EXCEEDS-CAPACITY");
                    for (const auto part : partitions)
                        assert(values(usage.*part) == values(usageBefore.*part));
                    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
                    budget.capacity.*field = maximum;
                    (usage.*partition).*field = maximum;
                    request.addition.*field = 1u;
                    expect(Preflight::assess(backend, budget, usage, request),
                        "RG-RETIREMENT-BUDGET-OVERFLOW");
                    request.addition = {};
                    const auto other = partition == partitions[0] ? partitions[1] : partitions[0];
                    (usage.*other).*field = 1u;
                    expect(Preflight::assess(backend, budget, usage, request),
                        "RG-RETIREMENT-BUDGET-LIVE-OVERFLOW");
                    usage = {};
                    budget.capacity = {};
                    request.addition.*field = 1u;
                    expect(Preflight::assess(backend, budget, usage, request),
                        "RG-RETIREMENT-BUDGET-CAPACITY");
                    ++cases;
                }
            }
            budget.capacity = {};
            request.addition = {};
            for (const auto field : fields)
                budget.capacity.*field = 11u;
            FrameGraphRetirementUsage usage;
            for (const auto partition : partitions)
                for (const auto field : fields)
                    (usage.*partition).*field = 2u;
            for (const auto field : fields)
                request.addition.*field = 3u;
            const auto result = Preflight::assess(backend, budget, usage, request);
            assert(result.fits() && result.dimension == fields.size());
            assert(values(result.projected) == values(budget.capacity));
            // A late dimension's malformed snapshot wins over an early one
            // that merely lacks room. The result must expose no partial sum.
            auto invalidUsage = usage;
            invalidUsage.cached.referenceCount = UINT64_MAX;
            auto smallBudget = budget;
            smallBudget.capacity.logicalTextureBytes = 0u;
            expect(Preflight::assess(backend, smallBudget, invalidUsage, request),
                "RG-RETIREMENT-BUDGET-LIVE-OVERFLOW");
            invalidUsage = usage;
            invalidUsage.cached.referenceCount = 12u;
            expect(Preflight::assess(backend, smallBudget, invalidUsage, request),
                "RG-RETIREMENT-BUDGET-LIVE-EXCEEDS-CAPACITY");
            for (const auto version : {0u, 2u, UINT32_MAX}) {
                auto invalid = budget;
                invalid.contractVersion = version;
                auto invalidRequest = request;
                invalidRequest.contractVersion = version;
                expect(Preflight::assess(backend, invalid, usage, invalidRequest),
                    "RG-RETIREMENT-BUDGET-VERSION");
                expect(Preflight::assess(backend, budget, usage, invalidRequest),
                    "RG-RETIREMENT-BUDGET-REQUEST-VERSION");
            }
            for (const auto domain : {ResourceCacheAccountingDomain::Unspecified,
                     ResourceCacheAccountingDomain::NativePhysical,
                     static_cast<ResourceCacheAccountingDomain>(99)}) {
                auto invalid = request;
                invalid.domain = domain;
                invalid.requiresNativePhysicalBound = true;
                expect(Preflight::assess(backend, budget, usage, invalid),
                    "RG-RETIREMENT-BUDGET-DOMAIN");
            }
            request.requiresNativePhysicalBound = true;
            expect(Preflight::assess(backend, budget, usage, request),
                "RG-RETIREMENT-BUDGET-PHYSICAL-BOUND-UNAVAILABLE");
            expect(Preflight::assess(static_cast<Backend>(99), budget, usage, request),
                "RG-RETIREMENT-BUDGET-BACKEND");
        }
    }
    assert(cases == 180u && quota->peakBytes() == 0u);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    std::puts(
        "FrameGraphRetirementBudgetPreflight/1: five backends, 9 dimensions x 4 partitions PASS");
}
