#pragma once

#include "rhi/semantics/webgl_runtime_profile.h"

namespace arp::rhi::semantics {
struct WebGlExtensionExecutionRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    WebGlRuntimeExtensions required;
};

struct WebGlExtensionExecutionDecision {
    bool executable = false;
    WebGlRuntimeExtensions native;
    const char* diagnostic = nullptr;
};

class WebGlExtensionExecutionContract final {
public:
    static WebGlExtensionExecutionDecision lower(Backend backend,
        const WebGlExtensionExecutionRequest& request,
        const WebGlRuntimeProfile& profile,
        const WebGlExtensionOptIn& originalOptIn) noexcept
    {
        switch (backend) {
        case Backend::WebGL:
            break;
        case Backend::Null:
            return {false, {}, "RHI-WEBGL1-EXTENSION-EXECUTION-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, {}, "RHI-WEBGL1-EXTENSION-EXECUTION-NOT-APPLICABLE"};
        default:
            return {false, {}, "RHI-WEBGL1-EXTENSION-EXECUTION-BACKEND-UNKNOWN"};
        }
        if (!profile.valid())
            return {false, {}, profile.diagnostic()};
        if (request.contractVersion == 0 && !request.prototypeOptIn && !request.required.any())
            return {true, {}, nullptr};
        if (request.contractVersion != 1)
            return {false, {}, "RHI-WEBGL1-EXTENSION-EXECUTION-VERSION"};
        if (!request.prototypeOptIn || !request.required.any())
            return {false, {}, "RHI-WEBGL1-EXTENSION-EXECUTION-OPT-IN"};
        WebGlGraphicsRequirements requirements;
        requirements.uint32Indices = request.required.uint32Indices;
        requirements.instancing = request.required.instancing;
        requirements.vertexArrayObjects = request.required.vertexArrayObjects;
        requirements.depthTextures = request.required.depthTextures;
        requirements.derivatives = request.required.derivatives;
        const auto admission = profile.admit(requirements, originalOptIn);
        if (admission.compatible && request.required.depthTextures) {
            const auto format = profile.admitFormat(
                {1, TextureFormat::Depth24Stencil8, WebGlFormatRole::DepthStencilTexture, 1});
            if (!format.compatible)
                return {false, {}, format.diagnostic};
        }
        return admission.compatible
            ? WebGlExtensionExecutionDecision{true, request.required, nullptr}
            : WebGlExtensionExecutionDecision{false, {}, admission.diagnostic};
    }
};
} // namespace arp::rhi::semantics
