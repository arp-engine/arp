#pragma once

#include "core/ownership/lifetime_epoch.h"
#include "rhi/semantics/webgl_context_ownership.h"
#include "rhi/semantics/webgl_context_recovery.h"
#include "rhi/semantics/webgl_surface_lifecycle.h"

namespace arp::shader {
struct WebGlShaderLimits;
}

namespace arp::rhi::webgl {

class WebGlDevice;
class WebGlResourceDomain;
class WebGlResources;
class WebGlCanvas;
class WebGlCanvasPayload;

// Original context custody. The public profile and epoch ticket are observations,
// never native handles or standalone permission to execute a command.
class WebGlRuntime final {
    class ConstructionKey {
        friend class WebGlRuntime;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Impl;
    explicit WebGlRuntime(ConstructionKey, core::UniquePtr<Impl>);
    ~WebGlRuntime();
    WebGlRuntime(const WebGlRuntime&) = delete;
    WebGlRuntime& operator=(const WebGlRuntime&) = delete;

    static core::UniquePtr<WebGlRuntime> create(const semantics::WebGlContextOwnershipRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);

    const semantics::WebGlRuntimeProfile& profile() const noexcept;
    const char* currentDiagnostic() const noexcept;
    core::LifetimeEpoch::Ticket epoch() const noexcept;
    const char* drawingBufferExtent(Extent2D&) const noexcept;
    std::string_view canvasSelector() const noexcept;
    semantics::WebGlContextPhase phase() const noexcept;
    const char* enableRecovery(const semantics::WebGlContextRecoveryRequest&) noexcept;
    const char* enableSurfaceLifecycle(const semantics::WebGlSurfaceLifecycleRequest&) noexcept;

private:
    friend class WebGlDevice;
    friend class WebGlResourceDomain;
    friend class WebGlResources;
    friend class WebGlCanvas;
    friend class WebGlCanvasPayload;
    static std::shared_ptr<WebGlRuntime> shareOriginal(core::UniquePtr<WebGlRuntime>);
    std::shared_ptr<const semantics::WebGlRuntimeProfile> profileSnapshot() const noexcept;
    semantics::WebGlExtensionOptIn extensionOptIn() const noexcept;
    const char* restore() noexcept;
    const char* surfaceDiagnostic() const noexcept;
    core::LifetimeEpoch::Ticket surfaceEpoch() const noexcept;
    const char* setSurfaceAvailable(bool) noexcept;
    const char* resizeSurface(Extent2D) noexcept;
    shader::WebGlShaderLimits shaderLimits() const noexcept;
    int beginNativeDestruction() noexcept;
    void endNativeDestruction(int previous) noexcept;
    core::UniquePtr<Impl> impl_;
};

} // namespace arp::rhi::webgl
