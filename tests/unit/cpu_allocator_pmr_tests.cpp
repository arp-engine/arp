#include "core/cpu_allocator.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

namespace {

struct CallbackState {
    arp::core::DefaultCpuAllocator backing{"CallbackBackingAllocator"};
    std::uint64_t allocateCalls = 0;
    std::uint64_t deallocateCalls = 0;
    arp::core::CpuAllocationIntent lastIntent = arp::core::CpuAllocationIntent::General;
};

void* allocateFromCallback(void* userData, const arp::core::CpuAllocationDesc& desc)
{
    auto& state = *static_cast<CallbackState*>(userData);
    ++state.allocateCalls;
    state.lastIntent = desc.intent;
    return state.backing.allocate(desc).ptr;
}

void deallocateFromCallback(void* userData, void* ptr, const arp::core::CpuAllocationDesc& desc)
{
    auto& state = *static_cast<CallbackState*>(userData);
    ++state.deallocateCalls;
    state.backing.deallocate(ptr, desc);
}

arp::core::CpuMemoryStats statsFromCallback(void* userData)
{
    auto& state = *static_cast<CallbackState*>(userData);
    return state.backing.stats();
}

struct TrackedObject {
    bool* destroyed = nullptr;
    int value = 0;

    TrackedObject(bool* destroyedValue, int valueValue)
        : destroyed(destroyedValue), value(valueValue)
    {}

    ~TrackedObject()
    {
        if (destroyed != nullptr) {
            *destroyed = true;
        }
    }
};

} // namespace

int main()
{
    using namespace arp::core;

    DefaultCpuAllocator defaultAllocator{"CoreDefaultAllocator"};
    assert(defaultAllocator.backendName() == "mimalloc");
    {
        CpuMemoryResource resource{
            defaultAllocator,
            CpuAllocationIntent::AssetImport,
            "AssetImportVector",
        };
        pmr::Vector<std::uint64_t> values{&resource};
        values.reserve(16);
        values.push_back(42u);

        const auto stats = defaultAllocator.stats();
        assert(stats.allocationCount >= 1u);
        assert(stats.liveBytes > 0u);
        assert(stats.peakBytes >= stats.liveBytes);
        assert(stats.byIntent(CpuAllocationIntent::AssetImport).allocationCount >= 1u);
        assert(stats.byIntent(CpuAllocationIntent::AssetImport).liveBytes == stats.liveBytes);
    }
    const auto releasedStats = defaultAllocator.stats();
    assert(releasedStats.liveBytes == 0u);
    assert(releasedStats.deallocationCount >= 1u);
    assert(releasedStats.byIntent(CpuAllocationIntent::AssetImport).liveBytes == 0u);

    bool destroyed = false;
    const auto beforeUnique = defaultCpuAllocator().stats();
    {
        auto tracked = makeUnique<TrackedObject>(&destroyed, 17);
        assert(tracked != nullptr);
        assert(tracked->value == 17);
        const auto duringUnique = defaultCpuAllocator().stats();
        assert(duringUnique.allocationCount > beforeUnique.allocationCount);
        assert(duringUnique.liveBytes >= beforeUnique.liveBytes + sizeof(TrackedObject));
    }
    const auto afterUnique = defaultCpuAllocator().stats();
    assert(destroyed);
    assert(afterUnique.liveBytes == beforeUnique.liveBytes);

    CallbackState callbackState;
    CpuAllocatorCallbacks callbacks;
    callbacks.userData = &callbackState;
    callbacks.allocate = allocateFromCallback;
    callbacks.deallocate = deallocateFromCallback;
    callbacks.stats = statsFromCallback;

    CallbackCpuAllocator callbackAllocator{"ThirdPartyCallbackAllocator", callbacks};
    {
        CpuMemoryResource resource{
            callbackAllocator,
            CpuAllocationIntent::RhiHostStaging,
            "RhiHostStagingVector",
        };
        pmr::Vector<std::uint32_t> stagingBytes{&resource};
        stagingBytes.resize(64u);
        assert(stagingBytes[8] == 0u);
    }
    assert(callbackState.allocateCalls >= 1u);
    assert(callbackState.deallocateCalls >= 1u);
    assert(callbackState.lastIntent == CpuAllocationIntent::RhiHostStaging);
    assert(callbackAllocator.stats().liveBytes == 0u);

    CallbackCpuAllocator missingCallbacks{"MissingCallbacks", {}};
    CpuMemoryResource failingResource{
        missingCallbacks,
        CpuAllocationIntent::Telemetry,
        "FailingTelemetryVector",
    };
    bool threwBadAlloc = false;
    try {
        pmr::Vector<int> failingVector{&failingResource};
        failingVector.resize(1u);
    } catch (const std::bad_alloc&) {
        threwBadAlloc = true;
    }
    assert(threwBadAlloc);
    assert(missingCallbacks.stats().failureCount == 1u);
    assert(missingCallbacks.lastFailureDiagnostics().find("missing allocate callback")
        != std::pmr::string::npos);

    return 0;
}
