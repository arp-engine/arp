#include "platform/renderdoc_capture_scope.h"
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
#include <renderdoc_app.h>
#include <windows.h>
#endif
namespace arp::platform {
RenderDocCaptureScope::~RenderDocCaptureScope()
{
    end();
}

bool RenderDocCaptureScope::begin(const core::pmr::String& path)
{
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
    if (active_ || path.empty())
        return false;
    const auto module = GetModuleHandleA("renderdoc.dll");
    const auto get = module
        ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"))
        : nullptr;
    RENDERDOC_API_1_4_0* api = nullptr;
    if (!get || get(eRENDERDOC_API_Version_1_4_0, reinterpret_cast<void**>(&api)) != 1 || !api)
        return false;
    api_ = api;
    api->SetCaptureFilePathTemplate(path.c_str());
    api->MaskOverlayBits(0, 0);
    api->StartFrameCapture(nullptr, nullptr);
    active_ = api->IsFrameCapturing() != 0;
    return active_;
#else
    (void)path;
    return false;
#endif
}

bool RenderDocCaptureScope::end()
{
#if defined(ARP_RENDERDOC_APP_AVAILABLE) && defined(_WIN32)
    if (active_) {
        active_ = false;
        return static_cast<RENDERDOC_API_1_4_0*>(api_)->EndFrameCapture(nullptr, nullptr) != 0;
    }
#endif
    return false;
}
} // namespace arp::platform
