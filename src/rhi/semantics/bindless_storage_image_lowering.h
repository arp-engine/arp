#pragma once
#include "rhi/frontend/bindless_storage_image_domain.h"
#include "rhi/gpu_bindless.h"

namespace arp::rhi::semantics {
// Target declaration mappings only. Device format queries, public command
// admission and genuine completion are independent gates. No backend can use
// compiler acceptance to opt into an unvalidated public storage-image path.
class BindlessStorageImageDeclarationLowering final {
public:
    struct Decision {
        const char* target = nullptr;
        bool requiresStorageFormatSupport = false;
        bool grantsExecution = false;
        const char* diagnostic = "RHI-BINDLESS-STORAGE-IMAGE-DECLARATION-BACKEND";
    };

    static constexpr Decision lowering(
        Backend backend, TextureFormat format, std::uint32_t version) noexcept
    {
        if (!BindlessStorageImageDomain::declaration(format, version).floatLanes)
            return {nullptr, false, false, "RHI-BINDLESS-STORAGE-IMAGE-DECLARATION-DOMAIN"};
        switch (backend) {
        case Backend::Null:
            return {"Validator", false, false, nullptr};
        case Backend::Vulkan:
            return {"SPIR-V OpTypeImage", true, false, nullptr};
        case Backend::Metal:
            return {format == TextureFormat::Rgba16Float
                    ? "MSL texture2d<float, access::write>"
                    : "MSL texture2d<float, access::read_write>",
                true,
                false,
                nullptr};
        case Backend::WebGPU:
            return {"WGSL texture_storage_2d", true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
