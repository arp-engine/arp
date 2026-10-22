#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class BindlessInitializedGraphicsContract final {
public:
    static constexpr std::uint32_t version = 2u;

    struct Profile {
        std::uint32_t contractVersion = 0u, interfaceIdentity = 0u;
        bool prototypeOptIn = false;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !interfaceIdentity && !prototypeOptIn;
        }
    };

    struct Selection {
        bool recognized = false;
        const char* diagnostic = nullptr;
    };

    static constexpr const char* validate(Profile profile) noexcept
    {
        if (profile.empty())
            return nullptr;
        if (profile.contractVersion != version)
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-PROFILE-VERSION";
        return profile.prototypeOptIn && profile.interfaceIdentity
            ? nullptr
            : "RHI-BINDLESS-INITIALIZED-GRAPHICS-PROFILE-INTENT";
    }

    static constexpr Selection select(Profile vertex, Profile fragment) noexcept
    {
        if (const auto* id = validate(vertex))
            return {false, id};
        if (const auto* id = validate(fragment))
            return {false, id};
        if (vertex.empty() && fragment.empty())
            return {};
        if (vertex.empty() || fragment.empty()
            || vertex.interfaceIdentity != fragment.interfaceIdentity)
            return {false, "RHI-BINDLESS-INITIALIZED-GRAPHICS-PROFILE-MISMATCH"};
        return {true, nullptr};
    }

    static constexpr const char* lowering(Backend backend,
        std::uint32_t requested,
        std::uint32_t closureVersion,
        bool initializedGeometry,
        bool initializedShader,
        bool producedAttachments) noexcept
    {
        if (requested != version)
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-VERSION";
        if (!initializedGeometry || !initializedShader)
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-INITIALIZED-CLOSURE";
        switch (backend) {
        case Backend::Null:
            return "RHI-BINDLESS-INITIALIZED-GRAPHICS-NULL-VALIDATOR";
        case Backend::Vulkan:
        case Backend::Metal:
            return closureVersion == 3u && !producedAttachments
                ? nullptr
                : "RHI-BINDLESS-INITIALIZED-GRAPHICS-NATIVE-CLOSURE";
        case Backend::WebGPU:
            return closureVersion >= 4u && closureVersion <= 6u && producedAttachments
                ? nullptr
                : "RHI-BINDLESS-INITIALIZED-GRAPHICS-EMULATED-CLOSURE";
        case Backend::WebGL:
            return "RHI-WEBGL1-BINDLESS-INITIALIZED-GRAPHICS-UNSUPPORTED";
        }
        return "RHI-BINDLESS-INITIALIZED-GRAPHICS-BACKEND";
    }
};
} // namespace arp::rhi::semantics
