#pragma once

#include "rhi/semantics/webgl_runtime_profile.h"

namespace arp::rhi::semantics {

enum class WebGlCanvasColorSpace { Unspecified, SRgb };

struct WebGlContextOwnershipRequest {
    std::uint32_t contractVersion = 0;
    std::string_view canvasSelector;
    WebGlContextRequest context;
    WebGlExtensionOptIn extensionOptIn;
    WebGlCanvasColorSpace colorSpace = WebGlCanvasColorSpace::Unspecified;
    bool shaderTextureLodOptIn = false;
    std::uint64_t maxCpuMetadataBytes = 0;
    std::uint32_t maxIdentityFieldBytes = 0;
};

class WebGlContextOwnershipContract final {
public:
    // This chooses the declared creation route, never a capability or native
    // payload. Even WebGL must pass its actual owner/query factory afterward.
    static const char* validateForBackend(
        Backend backend, const WebGlContextOwnershipRequest& request) noexcept
    {
        if (const auto* id = validate(request))
            return id;
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-CONTEXT-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-CONTEXT-BACKEND-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-CONTEXT-BACKEND-UNKNOWN";
    }

    static const char* validate(const WebGlContextOwnershipRequest& request) noexcept
    {
        if (request.contractVersion != 1u)
            return "RHI-WEBGL1-CONTEXT-OWNERSHIP-VERSION";
        if (request.canvasSelector.empty()
            || request.canvasSelector.find('\0') != std::string_view::npos)
            return "RHI-WEBGL1-CONTEXT-CANVAS-SELECTOR";
        if (request.context.contractVersion != 1u)
            return "RHI-WEBGL1-CONTEXT-REQUEST-VERSION";
        if (request.colorSpace != WebGlCanvasColorSpace::SRgb)
            return "RHI-WEBGL1-CONTEXT-COLOR-SPACE";
        if (!request.maxCpuMetadataBytes || !request.maxIdentityFieldBytes
            || request.maxIdentityFieldBytes > std::uint32_t{INT32_MAX})
            return "RHI-WEBGL1-CONTEXT-METADATA-BUDGET";
        if (request.extensionOptIn.contractVersion != 0u
            && request.extensionOptIn.contractVersion != 1u)
            return "RHI-WEBGL1-EXTENSION-CONTRACT-VERSION";
        if (request.extensionOptIn.extensions.any() && request.extensionOptIn.contractVersion != 1u)
            return "RHI-WEBGL1-EXTENSION-OPT-IN-REQUIRED";
        return nullptr;
    }
};

} // namespace arp::rhi::semantics
