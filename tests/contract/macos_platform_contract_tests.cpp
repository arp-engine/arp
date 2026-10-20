#include "platform/render_system.h"
#include "platform/window.h"

#include <cassert>

int main()
{
#if defined(__APPLE__)
    using namespace arp::platform;
    using namespace arp::rhi;

    assert(BackendPreference::Metal != BackendPreference::Vulkan);
    WindowDesc desc;
    desc.title = "arp-macos-platform-contract";
    desc.width = 64;
    desc.height = 64;
    desc.hidden = true;
    desc.gpuSurface = true; // The Apple window source attaches a CAMetalLayer for GPU surfaces.

    WindowSystem platform;
    const auto initialized = platform.initialize();
    if (!initialized.success) {
        return 0; // Headless CI may not expose a video driver.
    }
    auto window = platform.createWindow(desc);
    if (!window.valid()) {
        return 0;
    }
    const auto token = window.surfaceToken();
    assert(token != nullptr);

    SwapchainDesc swapchain;
    swapchain.surfaceName = "macos-contract";
    swapchain.extent = {64u, 64u};
    swapchain.format = TextureFormat::Bgra8Unorm;
    swapchain.platformSurface = token;
    assert(swapchain.platformSurface == token);
#endif
    return 0;
}
