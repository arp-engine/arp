#pragma once
#include "rhi/semantics/original_artifact_creation_lowering.h"

namespace arp::rhi::semantics {
// Versioned acceptance gate. Facts must be actual backend queries; this pure
// decision neither mints a native receipt nor grants command execution.
class OriginalArtifactComputeCreationLowering final {
public:
    struct Facts {
        bool device = false, shader = false, compute = false, bindGroupLayout = false,
             pipelineLayout = false, pushScope = false, popScope = false;
    };

    using Decision = OriginalArtifactCreationLowering::Decision;

    static constexpr Decision decide(Backend backend, Facts facts) noexcept
    {
        return OriginalArtifactCreationLowering::decide(backend,
            {facts.device,
                facts.shader,
                facts.compute,
                facts.bindGroupLayout,
                facts.pipelineLayout,
                facts.pushScope,
                facts.popScope},
            {"RHI-ARTIFACT-CREATION-BACKEND",
                "RHI-ARTIFACT-CREATION-NULL-VALIDATOR-ONLY",
                "RHI-ARTIFACT-CREATION-VULKAN-PAYLOAD-MISSING",
                "RHI-ARTIFACT-CREATION-METAL-PAYLOAD-MISSING",
                "RHI-ARTIFACT-CREATION-WEBGPU-DEVICE",
                "RHI-ARTIFACT-CREATION-WEBGPU-API"});
    }
};
} // namespace arp::rhi::semantics
