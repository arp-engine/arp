#pragma once
#include <cstdlib>

#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
#include <Windows.h>
#include <renderdoc_app.h>
// Windows' ANSI/Wide function alias collides with the RHI capability type.
// Do not leak it into headers included after this optional capture fixture.
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif
#endif

namespace arp::tests {

class RenderDocCaptureScope final {
public:
    explicit RenderDocCaptureScope(const char* explicitPath = nullptr)
    {
        (void)explicitPath;
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
        const auto module = GetModuleHandleA("renderdoc.dll");
        const auto getApi = module
            ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"))
            : nullptr;
        if (!getApi || getApi(eRENDERDOC_API_Version_1_4_0, reinterpret_cast<void**>(&api_)) != 1
            || !api_) {
            return;
        }
        if (const auto* path =
                explicitPath ? explicitPath : std::getenv("ARP_RENDERDOC_CAPTURE_PATH")) {
            api_->SetCaptureFilePathTemplate(path);
        }
        // The RenderDoc overlay records its own swapchain image transitions
        // around present. Keeping it enabled in this resource-lifecycle smoke
        // contaminates API-validation state with overlay-owned GENERAL layout
        // transitions, so capture only the application command stream.
        api_->MaskOverlayBits(0u, 0u);
        api_->StartFrameCapture(nullptr, nullptr);
        active_ = true;
#endif
    }

    ~RenderDocCaptureScope() { end(); }

    void end()
    {
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
        if (active_) {
            api_->EndFrameCapture(nullptr, nullptr);
            active_ = false;
        }
#endif
    }

private:
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
    RENDERDOC_API_1_4_0* api_ = nullptr;
#endif
    bool active_ = false;
};

} // namespace arp::tests
