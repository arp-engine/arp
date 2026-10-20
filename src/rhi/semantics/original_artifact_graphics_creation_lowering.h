#pragma once
#include "rhi/semantics/original_artifact_creation_lowering.h"

namespace arp::rhi::semantics {
class OriginalArtifactGraphicsCreationLowering final {
public:
    using Facts = OriginalArtifactCreationLowering::Facts;
    using Decision = OriginalArtifactCreationLowering::Decision;

    static constexpr Decision decide(Backend backend, Facts facts) noexcept
    {
        return OriginalArtifactCreationLowering::decide(backend,
            facts,
            {"RHI-GRAPHICS-CREATION-BACKEND",
                "RHI-GRAPHICS-CREATION-NULL-VALIDATOR-ONLY",
                "RHI-GRAPHICS-CREATION-VULKAN-PAYLOAD-MISSING",
                "RHI-GRAPHICS-CREATION-METAL-PAYLOAD-MISSING",
                "RHI-GRAPHICS-CREATION-WEBGPU-DEVICE",
                "RHI-GRAPHICS-CREATION-WEBGPU-API"});
    }
};
} // namespace arp::rhi::semantics
