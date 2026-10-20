#include "platform/window.h"
#include "platform/windows_application_icon.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cassert>
#include <cstdio>
#include <windows.h>

namespace {
void assertIconBinding(HWND window, int classSlot, int width, int height)
{
    const auto actual = reinterpret_cast<HICON>(GetClassLongPtrW(window, classSlot));
    assert(actual != nullptr);
    const auto expected = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(ARP_APPLICATION_ICON_RESOURCE_ID),
        IMAGE_ICON,
        width,
        height,
        LR_SHARED));
    // LR_SHARED returns the module's cached icon for this resource and size.
    // This verifies the actual window binding, not just the EXE's PE payload.
    assert(actual == expected);
    ICONINFO info{};
    assert(GetIconInfo(actual, &info));
    BITMAP bitmap{};
    assert(GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap));
    assert(bitmap.bmWidth == width && bitmap.bmHeight == height);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
}
} // namespace

int main()
{
    arp::platform::WindowSystem system;
    const auto initialized = system.initialize();
    assert(initialized.success);
    auto window = system.createWindow({"ARP Window Icon Test", 320, 200, true, false});
    assert(window.valid());
    const HWND native = FindWindowW(L"ArpWindowSystemWindowClass", L"ARP Window Icon Test");
    assert(native != nullptr);
    assertIconBinding(native, GCLP_HICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    assertIconBinding(
        native, GCLP_HICONSM, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    window = {};
    // Shared icons must survive individual window wrapper destruction.
    auto second = system.createWindow({"ARP Window Icon Second Test", 320, 200, true, false});
    assert(second.valid());
    const HWND secondNative =
        FindWindowW(L"ArpWindowSystemWindowClass", L"ARP Window Icon Second Test");
    assertIconBinding(
        secondNative, GCLP_HICONSM, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    std::puts("PASS: executable resource bound to small and large hidden-window class icons");
}
