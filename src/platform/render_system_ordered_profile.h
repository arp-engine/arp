#pragma once

#include "core/ownership/shared_owner_projection.h"
#include "platform/render_system_ordered_graphics.h"
#include "rhi/frontend/memory_cache_budget.h"
#include "shader/single_entry_shader_artifact.h"

namespace arp::platform {

// OrderedRenderSystem/1 is an explicit Prototype execution profile. These
// budgets are logical/CPU limits, never claims about physical driver memory.
struct OrderedRenderSystemBudgets {
    std::uint64_t contextMetadataBytes = 0, resourceMetadataBytes = 0, uploadBytes = 0;
    std::uint64_t logicalBufferBytes = 0, logicalTextureBytes = 0, resourceObjects = 0;
    std::uint64_t deviceMetadataBytes = 0;
    std::uint32_t identityFieldBytes = 0, surfaceMutations = 0;
    rhi::semantics::CacheBudget pipelines, descriptors;
};

struct RenderSystemOrderedProfile {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    bool sampledDepthOptIn = false;
    OrderedRenderSystemBudgets budgets;
    rhi::OrderedGraphicsExecutionRequest execution;
    RenderSystemOrderedGraphicsDesc frames;
};

class RenderSystemOrderedProfileContract final {
public:
    static const char* validate(rhi::Backend backend, const RenderSystemOrderedProfile& p)
    {
        if (p.contractVersion != 1)
            return "PLATFORM-ORDERED-PROFILE-VERSION";
        if (!p.prototypeOptIn || !p.execution.prototypeOptIn || !p.frames.prototypeOptIn)
            return "PLATFORM-ORDERED-PROFILE-OPT-IN";
        const auto& b = p.budgets;
        if (!b.contextMetadataBytes || !b.resourceMetadataBytes || !b.uploadBytes
            || !b.logicalBufferBytes || !b.logicalTextureBytes || !b.resourceObjects
            || b.resourceObjects > UINT32_MAX || !b.deviceMetadataBytes || !b.identityFieldBytes
            || b.identityFieldBytes > INT32_MAX || !b.surfaceMutations || !b.pipelines.valid()
            || !b.descriptors.valid())
            return "PLATFORM-ORDERED-PROFILE-BUDGET";
        if (p.execution.contractVersion != 1 || !p.execution.maxSubmissions
            || !p.execution.maxCpuMetadataBytes || p.frames.contractVersion != 1)
            return "PLATFORM-ORDERED-PROFILE-EXECUTION";
        if (const auto* id = rhi::semantics::GraphicsWorkloadRequirementsContract::orderedSubset(
                p.execution.requirements))
            return id;
        if (const auto* id =
                rhi::semantics::PreparedGraphicsCommandsContract::validate(p.frames.commands))
            return id;
        if (!p.frames.maxFrameCpuMetadataBytes || !p.frames.frameQuota || !p.frames.commandQuota
            || core::SharedOwnerProjection::overlapsOwner(
                p.frames.frameQuota, p.frames.commandQuota)
            || p.frames.frameQuota->limitBytes() != p.frames.maxFrameCpuMetadataBytes
            || p.frames.commandQuota->limitBytes() != p.frames.commands.issue.maxCpuMetadataBytes)
            return "PLATFORM-ORDERED-CPU-QUOTA";
        const auto& s = p.frames.surface;
        if (s.surfaceName.empty() || s.surfaceName.find('\0') != core::pmr::String::npos
            || !s.extent.width || !s.extent.height || s.format != rhi::TextureFormat::Rgba8Unorm
            || s.imageCount != 0 || s.presentationMode != rhi::PresentationMode::Auto
            || s.platformSurface)
            return "PLATFORM-ORDERED-PROFILE-SURFACE";
        switch (backend) {
        case rhi::Backend::WebGL:
            return nullptr;
        case rhi::Backend::Null:
            return "PLATFORM-ORDERED-PROFILE-VALIDATOR-ONLY";
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
        case rhi::Backend::WebGPU:
            return "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE";
        }
        return "PLATFORM-ORDERED-PROFILE-BACKEND-UNKNOWN";
    }
};

struct OrderedPipelineLoadDesc {
    core::pmr::String artifactPath{&core::defaultCpuMemoryResource()};
    shader::SingleEntryShaderLoadBudget artifactBudget;
    std::uint32_t maxUniformLeaves = 0, maxInterfaces = 0, maxTypeNodes = 0;
    std::uint32_t maxInterfaceNameBytes = 0, maxDiagnosticBytes = 0;
    std::optional<std::uint32_t> stencilReference;
    bool useReflectedLayout = true;
    // Reflected mode admits only explicit additional empty descriptor sets.
    rhi::PipelineLayoutDesc layout;
    rhi::RenderPipelineDesc pipeline;
};

struct OrderedRenderSystemCapabilities {
    bool prototype = false, orderedGraphics = false, surfaceLifecycle = false;
    bool gpuCompletion = false;
    // Sampling an attachment and reading pixels follow an explicit coordinate contract.
    bool attachmentSampleFlipY = false;
    bool readbackReverseRows = false;
};
} // namespace arp::platform
