#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {

// GraphicsWorkloadRequirements/1 describes one caller-selected pipeline. It
// never selects a visually different pipeline or grants device execution.
struct GraphicsWorkloadRequirements {
    std::uint32_t contractVersion = 0;
    std::uint32_t colorAttachments = 0;
    std::uint32_t textureWidth = 0, textureHeight = 0;
    std::uint32_t vertexAttributes = 0;
    std::uint32_t vertexUniformVectors = 0, fragmentUniformVectors = 0;
    std::uint32_t vertexTextureUnits = 0, fragmentTextureUnits = 0;
    bool compute = false, storageBuffers = false, storageTextures = false;
    bool hdrAttachments = false, inputAttachments = false, aliasHeaps = false;
    bool timelineSync = false, gpuCompletion = false, readback = false;
    bool externalSharing = false;
};

// Observation only. Executable owners must independently consume original
// device queries, artifact owners and actual backend payloads.
struct GraphicsWorkloadFacts {
    std::uint32_t colorAttachments = 0;
    std::uint32_t textureWidth = 0, textureHeight = 0;
    std::uint32_t vertexAttributes = 0;
    std::uint32_t vertexUniformVectors = 0, fragmentUniformVectors = 0;
    std::uint32_t vertexTextureUnits = 0, fragmentTextureUnits = 0;
    std::uint32_t combinedTextureUnits = 0;
};

class GraphicsWorkloadRequirementsContract final {
public:
    static const char* validate(const GraphicsWorkloadRequirements& r) noexcept
    {
        if (r.contractVersion != 1)
            return "RHI-GRAPHICS-WORKLOAD-VERSION";
        if (!r.colorAttachments || !r.textureWidth || !r.textureHeight || !r.vertexAttributes)
            return "RHI-GRAPHICS-WORKLOAD-EMPTY";
        return nullptr;
    }

    static const char* orderedSubset(const GraphicsWorkloadRequirements& r) noexcept
    {
        if (const auto* id = validate(r))
            return id;
        if (r.compute || r.storageBuffers || r.storageTextures || r.hdrAttachments
            || r.inputAttachments || r.aliasHeaps || r.timelineSync || r.gpuCompletion || r.readback
            || r.externalSharing)
            return "RHI-ORDERED-GRAPHICS-WORKLOAD-UNSUPPORTED";
        return nullptr;
    }

    static const char* assessLimits(
        const GraphicsWorkloadRequirements& r, const GraphicsWorkloadFacts& f) noexcept
    {
        if (const auto* id = validate(r))
            return id;
        if (r.colorAttachments > f.colorAttachments || r.textureWidth > f.textureWidth
            || r.textureHeight > f.textureHeight || r.vertexAttributes > f.vertexAttributes
            || r.vertexUniformVectors > f.vertexUniformVectors
            || r.fragmentUniformVectors > f.fragmentUniformVectors
            || r.vertexTextureUnits > f.vertexTextureUnits
            || r.fragmentTextureUnits > f.fragmentTextureUnits
            || std::uint64_t{r.vertexTextureUnits} + r.fragmentTextureUnits
                > f.combinedTextureUnits)
            return "RHI-GRAPHICS-WORKLOAD-LIMIT";
        return nullptr;
    }

    static const char* orderedLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return "RHI-ORDERED-GRAPHICS-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-ORDERED-GRAPHICS-COMPLETION-PATH-REQUIRED";
        case Backend::WebGL:
            return nullptr; // Eligibility only; original native custody is mandatory.
        }
        return "RHI-ORDERED-GRAPHICS-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
