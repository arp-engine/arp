// Shared engine lifecycle anchor. Public exports live on the concrete engine
// interfaces declared in src/* headers via ARP_API.

#include "core/cpu_allocator.h"

#if defined(_WIN32)
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved)
{
    // On process termination Windows has already torn down main-thread TLS
    // before MinGW runs this DLL's C++ static destructors. Avoid calling into
    // mimalloc from those late destructors; the OS owns final process cleanup.
    // Explicit FreeLibrary has reserved == nullptr and keeps normal cleanup.
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr) {
        arp::core::notifyCpuAllocatorProcessShutdown();
    }
    return TRUE;
}
#endif
