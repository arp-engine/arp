#include "core/cpu_allocator.h"
#include "renderer/rhi_production_frame.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <string>

namespace {

arp::renderer::RendererRhiDrawPlanDesc makeMinimalScenePass()
{
    arp::renderer::RendererRhiDrawPlanDesc pass;
    pass.name = "MinimalProductionScene";
    pass.workKind = arp::renderer::RendererRhiWorkKind::Scene;
    pass.frameSlot = 1;
    pass.drawCommandCount = 2;
    pass.pushConstantSizeBytes = 64;
    pass.pipelinePushConstantCapacityBytes = 128;
    pass.pipelineLayoutName = "MinimalProductionPipelineLayout";
    pass.expectedPipelineLayoutName = "MinimalProductionPipelineLayout";
    pass.descriptorSetCount = 1;
    pass.descriptorLayoutNameCount = 1;
    pass.descriptorLayoutNames[0] = "MinimalProductionFrameSet";
    pass.requiredMaterialResourceCount = 1;
    pass.materialResourceCount = 1;
    return pass;
}

arp::renderer::RendererRhiFramePlanDesc makeMinimalProductionFrame()
{
    arp::renderer::RendererRhiFramePlanDesc frame;
    frame.frameSlot = 1;
    frame.drawPlans.push_back(makeMinimalScenePass());
    return frame;
}

} // namespace

int main()
{
    using namespace arp::renderer;
    using namespace arp::rhi;

    auto device = createNullDevice();
    assert(device);

    RendererRhiProductionFramePath path({
        "MinimalProductionRendererPath",
        {640, 360, 1},
        TextureFormat::Bgra8Unorm,
        {2, 4, 4, 2},
        8,
    });

    const auto prepared = path.prepare(makeMinimalProductionFrame(), *device);
    assert(prepared.success);
    assert(prepared.productionRendererPath);
    assert(prepared.usedRhiGpuDevice);
    assert(prepared.rendererFramePlanAccepted);
    assert(prepared.rhiResourceCreated);
    assert(prepared.rhiPipelineCreated);
    assert(prepared.rhiDescriptorCreated);
    assert(prepared.rhiDescriptorWritten);
    assert(prepared.shaderPackageValidated);
    assert(!prepared.removedModuleRouteUsed);
    assert(!prepared.nativeHandleExposedToUpperLayer);

    const auto submitted = path.submit(*device);
    assert(submitted.success);
    assert(submitted.productionRendererPath);
    assert(submitted.usedRhiGpuDevice);
    assert(submitted.resourcesPreparedBeforeSubmit);
    assert(submitted.pipelinePreparedBeforeSubmit);
    assert(submitted.descriptorsPreparedBeforeSubmit);
    assert(!submitted.pipelineCreatedDuringSubmit);
    assert(!submitted.resourceAllocatedDuringSubmit);
    assert(submitted.rhiCommandEncoded);
    assert(submitted.rhiQueueSubmitted);
    assert(!submitted.removedModuleRouteUsed);
    assert(!submitted.nativeHandleExposedToUpperLayer);
    assert(submitted.renderPassCount == 1u);
    assert(submitted.drawCommandCount == 2u);
    assert(submitted.submittedCommandCount >= 6u);

    RendererRhiProductionFramePath unpreparedPath;
    const auto unprepared = unpreparedPath.submit(*device);
    assert(!unprepared.success);
    assert(unprepared.diagnostics.find("prepared") != std::pmr::string::npos);
    assert(!unprepared.removedModuleRouteUsed);

    return 0;
}
