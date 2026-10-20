#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/render_graph/rhi_frame_plan.h"
#include "rhi/gpu_swapchain.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::renderer {

struct RendererRhiDrawPlanBridgeDesc {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxFeatureRequirements = 0;
    std::uint32_t maxBarriers = 0;
    std::uint32_t maxPasses = 0;
};

enum class RendererRhiWorkKind { Scene, Gui, Postprocess, Particle, Shadow, Compute, Transfer };

struct RendererRhiDrawPlanDesc {
    std::pmr::string name;
    RendererRhiWorkKind workKind = RendererRhiWorkKind::Scene;
    std::uint32_t frameSlot = 0;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t dispatchCount = 0;
    std::uint32_t transferCommandCount = 0;
    std::pmr::vector<render_graph::RhiDrawIndexedCommand> drawCommands;
    std::pmr::vector<render_graph::RhiDispatchCommand> dispatchCommands;
    std::pmr::vector<render_graph::RhiTransferCommand> transferCommands;
    std::uint32_t pushConstantSizeBytes = 0;
    std::uint32_t pipelinePushConstantCapacityBytes = 0;
    std::pmr::string pipelineLayoutName;
    std::pmr::string expectedPipelineLayoutName;
    std::uint32_t descriptorLayoutNameCount = 0;
    std::array<std::pmr::string, render_graph::kMaxRhiPassDescriptorLayoutNames>
        descriptorLayoutNames{};
    std::uint32_t descriptorSetCount = 0;
    std::uint32_t descriptorTableCount = 0;
    std::uint32_t requiredMaterialResourceCount = 0;
    std::uint32_t materialResourceCount = 0;
    bool allowSoftSecondaryCommands = false;
    bool usesTransientTargets = false;
    bool usesComputeDispatch = false;
    bool usesStorageTextureBinding = false;
    bool hasDepthTarget = false;
    bool requiresBackendOwnedVertexUpload = false;
    std::uint32_t maxUploadBytes = 0;
    bool hasInputContract = false;
    rhi::RendererInputContract inputContract;
};

struct RendererRhiFramePlanBuildResult {
    bool success = false;
    std::pmr::string diagnostics;
    render_graph::RhiFramePlan plan;
    const render_graph::FrameGraphRhiShaderPackageCatalog* shaderPackages = nullptr;
    bool backendSpecificBranchingRequired = false;
    bool nativeHandleExposedToUpperLayer = false;
};

class ARP_API RendererRhiDrawPlanBridge {
public:
    explicit RendererRhiDrawPlanBridge(const RendererRhiDrawPlanBridgeDesc& desc);

    RendererRhiFramePlanBuildResult buildFramePlan(const RendererRhiDrawPlanDesc& drawPlan) const;

private:
    RendererRhiDrawPlanBridgeDesc desc_;
};

ARP_API RendererRhiFramePlanBuildResult buildRendererRhiFramePlan(
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc, const RendererRhiDrawPlanDesc& drawPlan);

struct RendererRhiFramePlanDesc {
    std::uint32_t frameSlot = 0;
    std::pmr::vector<RendererRhiDrawPlanDesc> drawPlans;
    const render_graph::FrameGraphRhiShaderPackageCatalog* shaderPackages = nullptr;
};

struct RendererRhiFramePlanBuilderDesc {
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t maxDrawPlans = 0;
    std::uint32_t maxFeatureRequirements = 0;
    std::uint32_t maxBarriers = 0;
    std::uint32_t maxPasses = 0;
    const render_graph::FrameGraphRhiShaderPackageCatalog* shaderPackages = nullptr;
};

class ARP_API RendererRhiFramePlanBridge {
public:
    explicit RendererRhiFramePlanBridge(const RendererRhiDrawPlanBridgeDesc& desc);

    RendererRhiFramePlanBuildResult buildFramePlan(const RendererRhiFramePlanDesc& framePlan) const;

private:
    RendererRhiDrawPlanBridgeDesc desc_;
};

ARP_API RendererRhiFramePlanBuildResult buildRendererRhiFramePlan(
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc, const RendererRhiFramePlanDesc& framePlan);

class ARP_API RendererRhiFramePlanBuilder {
public:
    explicit RendererRhiFramePlanBuilder(const RendererRhiFramePlanBuilderDesc& desc);

    RendererRhiFramePlanBuilder& setFrameSlot(std::uint32_t frameSlot);
    RendererRhiFramePlanBuilder& addDrawPlan(const RendererRhiDrawPlanDesc& drawPlan);
    RendererRhiFramePlanBuildResult build() const;

private:
    void appendFailure(const std::pmr::string& diagnostics);

    RendererRhiFramePlanBuilderDesc desc_;
    RendererRhiFramePlanDesc framePlan_;
    bool failed_ = false;
    std::pmr::string diagnostics_;
};

} // namespace arp::renderer
