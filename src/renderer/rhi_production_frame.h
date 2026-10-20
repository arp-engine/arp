#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/gpu_device.h"

#include <memory>
#include <string>

namespace arp::renderer {

struct RendererRhiProductionFramePathDesc {
    std::pmr::string label = "RendererRhiProductionFrame";
    rhi::Extent3D colorExtent{320, 180, 1};
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Bgra8Unorm;
    RendererRhiDrawPlanBridgeDesc framePlanBridge{2, 6, 4, 4};
    std::uint32_t maxDrawCommandsPerPass = 1024;
    bool useExternalColorTarget = false;
    std::pmr::string shaderArtifactRoot;
};

struct RendererRhiProductionFramePrepareReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool productionRendererPath = true;
    bool usedRhiGpuDevice = false;
    bool rendererFramePlanAccepted = false;
    bool rhiResourceCreated = false;
    bool rhiPipelineCreated = false;
    bool rhiDescriptorCreated = false;
    bool rhiDescriptorWritten = false;
    bool shaderPackageValidated = false;
    bool fullSurfaceViewportConfigured = false;
    bool fullSurfaceScissorConfigured = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool removedModuleRouteUsed = false;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t resourceCount = 0;
    std::uint32_t renderPipelineCount = 0;
    std::uint32_t descriptorSetCount = 0;
    RendererRhiFramePlanBuildResult framePlan;
};

struct RendererRhiProductionFrameSubmitReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool productionRendererPath = true;
    bool usedRhiGpuDevice = false;
    bool resourcesPreparedBeforeSubmit = false;
    bool pipelinePreparedBeforeSubmit = false;
    bool descriptorsPreparedBeforeSubmit = false;
    bool resourceAllocatedDuringSubmit = false;
    bool pipelineCreatedDuringSubmit = false;
    bool rhiCommandEncoded = false;
    bool rhiQueueSubmitted = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool removedModuleRouteUsed = false;
    bool nativeHandleExposedToUpperLayer = false;
    bool proceduralSceneRuntime3DObjectDrawn = false;
    bool proceduralSceneRuntime2DSpriteDrawn = false;
    bool proceduralSceneRuntimeUiDrawn = false;
    std::uint32_t renderPassCount = 0;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t submittedCommandCount = 0;
};

class ARP_API RendererRhiProductionFramePath final {
public:
    RendererRhiProductionFramePath();
    explicit RendererRhiProductionFramePath(const RendererRhiProductionFramePathDesc& desc);
    ~RendererRhiProductionFramePath();

    RendererRhiProductionFramePrepareReport prepare(
        const RendererRhiFramePlanDesc& frame, rhi::GpuDevice& device);
    RendererRhiProductionFrameSubmitReport submit(rhi::GpuDevice& device);
    RendererRhiProductionFrameSubmitReport submit(
        rhi::GpuDevice& device, rhi::GpuTexture& externalColorTarget);

private:
    void resetPreparedState();
    RendererRhiProductionFrameSubmitReport submitToTarget(
        rhi::GpuDevice& device, rhi::GpuTexture& colorTarget, bool externalColorTarget);

    RendererRhiProductionFramePathDesc desc_;
    RendererRhiFramePlanBuildResult framePlan_;
    rhi::ShaderPackageDesc shaderPackage_;
    arp::core::UniquePtr<rhi::GpuTexture> colorTarget_;
    arp::core::UniquePtr<rhi::GpuBuffer> frameConstants_;
    arp::core::UniquePtr<rhi::PipelineLayout> pipelineLayout_;
    arp::core::UniquePtr<rhi::RenderPipeline> renderPipeline_;
    arp::core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    arp::core::UniquePtr<rhi::DescriptorSet> frameDescriptorSet_;
    bool prepared_ = false;
};

} // namespace arp::renderer
