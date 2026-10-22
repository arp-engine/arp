#pragma once
#include "core/memory/cpu_allocator.h"

namespace arp::platform {
// Optional developer capture hook; no dependency on a RenderDoc installation
// at runtime. Failure to inject is observable, never claimed as capture proof.
class RenderDocCaptureScope final {
public:
    RenderDocCaptureScope() = default;
    ~RenderDocCaptureScope();
    bool begin(const core::pmr::String& path);
    bool end();
    RenderDocCaptureScope(const RenderDocCaptureScope&) = delete;
    RenderDocCaptureScope& operator=(const RenderDocCaptureScope&) = delete;

private:
    void* api_ = nullptr;
    bool active_ = false;
};
} // namespace arp::platform
