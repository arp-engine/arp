#include "core/concurrency/interactive_thread_scope.h"
#include "core/memory/cpu_allocator.h"

#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <processthreadsapi.h>
#include <windows.h>
#endif

namespace {
void require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
#if defined(_WIN32)
struct Observed {
    THREAD_POWER_THROTTLING_STATE quality{};
    arp::core::pmr::Vector<ULONG> sets{&arp::core::defaultCpuMemoryResource()};
    GROUP_AFFINITY affinity{};
    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    int priority = 0;
};

Observed observe()
{
    Observed result;
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto getQuality =
        reinterpret_cast<BOOL(WINAPI*)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD)>(
            GetProcAddress(kernel, "GetThreadInformation"));
    const auto getSets = reinterpret_cast<BOOL(WINAPI*)(HANDLE, PULONG, ULONG, PULONG)>(
        GetProcAddress(kernel, "GetThreadSelectedCpuSets"));
    require(getQuality && getSets, "Windows scope test needs the queried CPU-set/quality APIs");
    result.quality.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    require(getQuality(
                GetCurrentThread(), ThreadPowerThrottling, &result.quality, sizeof(result.quality)),
        "Cannot query native thread quality");
    ULONG count = 0;
    require(getSets(GetCurrentThread(), nullptr, 0, &count)
            || GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "Cannot query selected CPU-set extent");
    result.sets.resize(count);
    require(getSets(GetCurrentThread(), result.sets.data(), count, &count),
        "Cannot query native selected CPU sets");
    require(GetThreadGroupAffinity(GetCurrentThread(), &result.affinity),
        "Cannot query hard thread affinity");
    require(GetProcessAffinityMask(
                GetCurrentProcess(), &result.processAffinity, &result.systemAffinity),
        "Cannot query process affinity");
    result.priority = GetThreadPriority(GetCurrentThread());
    return result;
}

bool same(const Observed& a, const Observed& b)
{
    return a.quality.Version == b.quality.Version && a.quality.ControlMask == b.quality.ControlMask
        && a.quality.StateMask == b.quality.StateMask && a.sets == b.sets
        && a.affinity.Group == b.affinity.Group && a.affinity.Mask == b.affinity.Mask
        && a.processAffinity == b.processAffinity && a.priority == b.priority;
}
#endif
} // namespace

int main()
{
    try {
#if defined(_WIN32)
        const auto before = observe();
        arp::core::InteractiveThreadScope outer;
        const auto active = observe();
        require(outer.result().quality == arp::core::ThreadExecutionQualityStatus::Applied,
            "Interactive quality hint was not admitted on this Windows host");
        require((active.quality.ControlMask & THREAD_POWER_THROTTLING_EXECUTION_SPEED)
                && !(active.quality.StateMask & THREAD_POWER_THROTTLING_EXECUTION_SPEED),
            "Native interactive hint differs from the reported result");
        require(before.priority == active.priority
                && before.processAffinity == active.processAffinity
                && before.affinity.Group == active.affinity.Group
                && before.affinity.Mask == active.affinity.Mask,
            "Interactive scope changed priority or hard affinity");
        require(before.sets == active.sets, "Quality hint changed CPU-set selection");
        {
            arp::core::InteractiveThreadScope inner;
            require(inner.restore(), "Nested scope restoration failed");
            require(same(active, observe()), "Nested scope changed the outer native policy");
        }
        require(outer.restore() && outer.restore(), "Scope restoration was not idempotent");
        require(same(before, observe()), "Scope did not restore the exact prior native policy");
        std::cout
            << "Interactive thread native scope: restored=true priority-unchanged=true affinity-and-cpu-sets-unchanged=true\n";
#else
        arp::core::InteractiveThreadScope scope;
        require(scope.result().quality == arp::core::ThreadExecutionQualityStatus::Unsupported
                && scope.restore(),
            "Unsupported platform advertised a native placement change");
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
