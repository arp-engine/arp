#include "core/concurrency/interactive_thread_scope.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <processthreadsapi.h>
#include <windows.h>
#endif

namespace arp::core {
namespace {
#if defined(_WIN32)
using ReadQuality = BOOL(WINAPI*)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);
using WriteQuality = BOOL(WINAPI*)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);

struct Api {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    ReadQuality read =
        reinterpret_cast<ReadQuality>(GetProcAddress(kernel, "GetThreadInformation"));
    WriteQuality write =
        reinterpret_cast<WriteQuality>(GetProcAddress(kernel, "SetThreadInformation"));
};

const Api& api()
{
    static const Api value;
    return value;
}
#endif
} // namespace

InteractiveThreadScope::InteractiveThreadScope() noexcept
{
#if defined(_WIN32)
    const auto& native = api();
    if (!native.read || !native.write)
        return;
    ownerThread_ = GetCurrentThreadId();
    THREAD_POWER_THROTTLING_STATE previous{};
    previous.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    result_.quality = ThreadExecutionQualityStatus::Failed;
    if (native.read(GetCurrentThread(), ThreadPowerThrottling, &previous, sizeof(previous))) {
        priorQuality_ = {previous.Version, previous.ControlMask, previous.StateMask};
        auto requested = previous;
        requested.ControlMask |= THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        requested.StateMask &= ~THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        if (native.write(
                GetCurrentThread(), ThreadPowerThrottling, &requested, sizeof(requested))) {
            applied_ = true;
            result_.quality = ThreadExecutionQualityStatus::Applied;
        }
    }
    if (!applied_)
        result_.nativeError = GetLastError();
#endif
}

InteractiveThreadScope::~InteractiveThreadScope()
{
    (void)restore();
}

bool InteractiveThreadScope::restore() noexcept
{
#if defined(_WIN32)
    if (!applied_)
        return true;
    if (ownerThread_ != GetCurrentThreadId()) {
        result_.nativeError = ERROR_INVALID_THREAD_ID;
        return false;
    }
    THREAD_POWER_THROTTLING_STATE previous{priorQuality_[0], priorQuality_[1], priorQuality_[2]};
    if (!api().write(GetCurrentThread(), ThreadPowerThrottling, &previous, sizeof(previous))) {
        result_.nativeError = GetLastError();
        return false;
    }
    applied_ = false;
#endif
    return true;
}
} // namespace arp::core
