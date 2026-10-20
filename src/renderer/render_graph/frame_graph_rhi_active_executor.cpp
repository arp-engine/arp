#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/frame_graph_uniform_ring_buffer.h"
#include "renderer/render_graph/rhi_frame_plan.h"
#include "renderer/render_graph/transient_resource_allocator.h"
#include "renderer/shader_package_reflection.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_pipeline.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace arp::render_graph {

namespace {

void mergeQueryExecutionStatus(std::pmr::vector<rhi::QueryExecutionStatus>& statuses,
    const rhi::QueryExecutionStatus& replacement)
{
    for (auto& status : statuses) {
        if (status.type == replacement.type) {
            status = replacement;
            return;
        }
    }
    statuses.push_back(replacement);
}

struct StatusTelemetryFinalizer {
    FrameGraphRhiCommandExecutionReport& report;

    ~StatusTelemetryFinalizer()
    {
        report.statusTelemetry = rhi::summarizeExecutionStatuses(
            report.executionStatuses, report.queryExecutionStatuses);
        report.diagnosticsReport.plan.statusTelemetry = report.statusTelemetry;
    }
};

FrameGraphRhiNullSmokeSummary summarizeNullSmoke(const rhi::NullHiddenFrameSmokeReport& smoke)
{
    FrameGraphRhiNullSmokeSummary summary;
    summary.executed = true;
    summary.success = smoke.success;
    summary.diagnostics = smoke.diagnostics;
    summary.swapchainImageCount = smoke.swapchainImageCount;
    summary.maxFramesInFlight = smoke.maxFramesInFlight;
    summary.submittedFrameCount = smoke.submittedFrameCount;
    summary.totalSubmittedCommandCount = smoke.totalSubmittedCommandCount;
    summary.logicalFrameSlots = smoke.resourceStats.logicalFrameSlots;
    summary.descriptorCompletenessValidated = smoke.descriptorCompletenessValidated;
    summary.transientResourceCount = smoke.resourceStats.transientResourceCount;
    summary.pendingDestroyCount = smoke.resourceStats.pendingDestroyCount;
    summary.frames.reserve(smoke.frames.size());

    for (const auto& frame : smoke.frames) {
        summary.frames.push_back({
            frame.frameSlot,
            frame.frameSerial,
            frame.submittedCommandCount,
            frame.presentSucceeded,
            frame.releaseStats.releasedCount,
            frame.releaseStats.releasedBytes,
            frame.diagnostics,
        });
    }

    return summary;
}

bool hasBackendConsumableBarrierPlan(const CompiledFrameGraph& compiled)
{
    for (const auto& barrier : compiled.barrierPlan()) {
        if (barrier.resourceName.empty()
            || ((barrier.before == barrier.after) != barrier.memoryOnly)) {
            return false;
        }
    }
    return true;
}

bool commandSucceeded(const rhi::CommandValidationResult& result, std::pmr::string& diagnostics)
{
    if (result.success) {
        return true;
    }
    arp::core::appendDiagnostic(diagnostics, result.diagnostics);
    return false;
}

const rhi::semantics::GpuTimingScopeSlot* timingSlotForPass(
    const rhi::semantics::GpuTimingLoweringPlan* plan, const std::pmr::string& passName)
{
    if (plan == nullptr)
        return nullptr;
    for (std::size_t i = 0; i < plan->scopes.size() && i < plan->slots.size(); ++i) {
        if (plan->scopes[i].label == passName)
            return &plan->slots[i];
    }
    return nullptr;
}

const rhi::ShaderEntrypoint* uniqueEntrypointForStage(
    const rhi::ShaderPackageDesc& package, rhi::ShaderStage stage, std::pmr::string& diagnostics)
{
    const rhi::ShaderEntrypoint* found = nullptr;
    for (const auto& entrypoint : package.entrypoints) {
        if (entrypoint.stage != stage) {
            continue;
        }
        if (found != nullptr) {
            arp::core::appendDiagnostic(diagnostics, "RHI-RG-SHADER-ENTRYPOINT-AMBIGUOUS");
            return nullptr;
        }
        found = &entrypoint;
    }
    if (found == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-SHADER-ENTRYPOINT-MISSING");
    }
    return found;
}

const rhi::ShaderPackageDesc* packageForPass(const RhiPassRecord& pass,
    const FrameGraphRhiShaderPackageCatalog* catalog,
    std::pmr::string& diagnostics)
{
    const auto* package = catalog != nullptr ? catalog->find(pass.name) : nullptr;
    if (package == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-SHADER-PACKAGE-MISSING");
        return nullptr;
    }
    const auto validation = rhi::validateShaderPackage(*package);
    if (!validation.success) {
        arp::core::appendDiagnostic(diagnostics, validation.diagnostics);
        return nullptr;
    }
    return package;
}

struct FrameGraphRhiPassResources {
    const rhi::GpuTexture* colorTarget = nullptr;
    const rhi::GpuBuffer* transferSource = nullptr;
    const rhi::GpuBuffer* transferDestination = nullptr;
    std::pmr::vector<FrameGraphRhiDescriptorSetBinding> descriptorSets;
    const rhi::DescriptorTable* descriptorTable = nullptr;
    rhi::PipelineLayout* pipelineLayout = nullptr;
    rhi::ShaderPackageDesc shaderPackage;
    rhi::RenderPipeline* renderPipeline = nullptr;
    rhi::ComputePipeline* computePipeline = nullptr;
    const void* pushConstantData = nullptr;
    std::uint32_t pushConstantDataSize = 0;
};

bool prepareGraphicsResources(const RhiPassRecord& pass,
    rhi::GpuDevice& device,
    FrameGraphRhiPassResources& resources,
    std::pmr::string& diagnostics,
    std::uint32_t frameSlot,
    FrameGraphUniformRingBuffer* ringBuffer,
    const FrameGraphRhiShaderPackageCatalog* shaderPackages)
{
    (void)frameSlot;
    (void)ringBuffer;
    const auto* binding =
        shaderPackages != nullptr ? shaderPackages->findBinding(pass.name) : nullptr;
    if (binding == nullptr || binding->package == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-SHADER-PACKAGE-MISSING");
        return false;
    }
    if (binding->pipelineLayout == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-PIPELINE-LAYOUT-MISSING");
        return false;
    }
    if (binding->colorTarget == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-COLOR-TARGET-MISSING");
        return false;
    }
    const auto* shaderPackage = binding->package;
    if (!shaderPackage->generatedReflectionHydrated) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-SHADER-REFLECTION-UNHYDRATED");
        return false;
    }
    const auto packageValidation = rhi::validateShaderPackage(*shaderPackage);
    if (!packageValidation.success) {
        arp::core::appendDiagnostic(diagnostics, packageValidation.diagnostics);
        return false;
    }
    const auto* vertexEntrypoint =
        uniqueEntrypointForStage(*shaderPackage, rhi::ShaderStage::Vertex, diagnostics);
    const auto* fragmentEntrypoint =
        uniqueEntrypointForStage(*shaderPackage, rhi::ShaderStage::Fragment, diagnostics);
    if (vertexEntrypoint == nullptr || fragmentEntrypoint == nullptr) {
        return false;
    }

    const auto& pipelineLayoutDesc = *binding->pipelineLayout;

    const auto layoutValidation = rhi::validatePipelineLayoutAgainstShaderReflection(
        pipelineLayoutDesc, shaderPackage->reflection);
    if (!layoutValidation.success) {
        arp::core::appendDiagnostic(diagnostics, layoutValidation.diagnostics);
        return false;
    }

    resources.pipelineLayout = device.pipelineCache().acquirePipelineLayout(pipelineLayoutDesc);
    if (!resources.pipelineLayout) {
        arp::core::appendDiagnostic(
            diagnostics, "FrameGraph RHI graphics pass pipeline layout creation failed");
        return false;
    }
    resources.shaderPackage = *shaderPackage;

    resources.renderPipeline = device.pipelineCache().acquireRenderPipeline({
        pass.name + "GraphPipeline",
        resources.pipelineLayout,
        &resources.shaderPackage,
        {
            vertexEntrypoint->modulePath,
            vertexEntrypoint->entryPoint,
            vertexEntrypoint->stage,
            vertexEntrypoint->sourceLanguage,
            vertexEntrypoint->computeWorkgroupSizeX,
            vertexEntrypoint->computeWorkgroupSizeY,
            vertexEntrypoint->computeWorkgroupSizeZ,
            vertexEntrypoint->writesRenderTargetArrayIndex,
        },
        {
            fragmentEntrypoint->modulePath,
            fragmentEntrypoint->entryPoint,
            fragmentEntrypoint->stage,
            fragmentEntrypoint->sourceLanguage,
            fragmentEntrypoint->computeWorkgroupSizeX,
            fragmentEntrypoint->computeWorkgroupSizeY,
            fragmentEntrypoint->computeWorkgroupSizeZ,
            fragmentEntrypoint->writesRenderTargetArrayIndex,
        },
        resources.colorTarget->desc().format,
    });
    if (!resources.renderPipeline) {
        arp::core::appendDiagnostic(
            diagnostics, "FrameGraph RHI graphics pass render pipeline creation failed");
        return false;
    }

    if (binding->descriptorSets.size() != pass.descriptorSetCount) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-DESCRIPTOR-SET-BINDINGS-MISMATCH");
        return false;
    }
    if (pass.pushConstantSizeBytes > 0
        && (binding->pushConstantData == nullptr
            || binding->pushConstantDataSize != pass.pushConstantSizeBytes)) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-PUSH-CONSTANT-DATA-MISMATCH");
        return false;
    }
    resources.colorTarget = binding->colorTarget;
    resources.descriptorSets = binding->descriptorSets;
    resources.pushConstantData = binding->pushConstantData;
    resources.pushConstantDataSize = binding->pushConstantDataSize;
    return true;
}

bool prepareComputeResources(const RhiPassRecord& pass,
    rhi::GpuDevice& device,
    FrameGraphRhiPassResources& resources,
    std::pmr::string& diagnostics,
    std::uint32_t frameSlot,
    FrameGraphUniformRingBuffer* ringBuffer,
    const FrameGraphRhiShaderPackageCatalog* shaderPackages)
{
    (void)frameSlot;
    (void)ringBuffer;
    const auto* binding =
        shaderPackages != nullptr ? shaderPackages->findBinding(pass.name) : nullptr;
    if (binding == nullptr || binding->package == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-SHADER-PACKAGE-MISSING");
        return false;
    }
    if (binding->pipelineLayout == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-PIPELINE-LAYOUT-MISSING");
        return false;
    }
    const auto* shaderPackage = binding->package;
    if (!shaderPackage->generatedReflectionHydrated) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-SHADER-REFLECTION-UNHYDRATED");
        return false;
    }
    const auto packageValidation = rhi::validateShaderPackage(*shaderPackage);
    if (!packageValidation.success) {
        arp::core::appendDiagnostic(diagnostics, packageValidation.diagnostics);
        return false;
    }
    const auto* computeEntrypoint =
        uniqueEntrypointForStage(*shaderPackage, rhi::ShaderStage::Compute, diagnostics);
    if (computeEntrypoint == nullptr) {
        return false;
    }
    const auto& pipelineLayoutDesc = *binding->pipelineLayout;

    const auto layoutValidation = rhi::validatePipelineLayoutAgainstShaderReflection(
        pipelineLayoutDesc, shaderPackage->reflection);
    if (!layoutValidation.success) {
        arp::core::appendDiagnostic(diagnostics, layoutValidation.diagnostics);
        return false;
    }

    resources.pipelineLayout = device.pipelineCache().acquirePipelineLayout(pipelineLayoutDesc);
    if (!resources.pipelineLayout) {
        arp::core::appendDiagnostic(
            diagnostics, "FrameGraph RHI compute pass pipeline layout creation failed");
        return false;
    }
    resources.shaderPackage = *shaderPackage;
    resources.computePipeline = device.pipelineCache().acquireComputePipeline({
        pass.name + "GraphComputePipeline",
        resources.pipelineLayout,
        &resources.shaderPackage,
        {
            computeEntrypoint->modulePath,
            computeEntrypoint->entryPoint,
            computeEntrypoint->stage,
            computeEntrypoint->sourceLanguage,
            computeEntrypoint->computeWorkgroupSizeX,
            computeEntrypoint->computeWorkgroupSizeY,
            computeEntrypoint->computeWorkgroupSizeZ,
            computeEntrypoint->writesRenderTargetArrayIndex,
        },
    });
    if (!resources.computePipeline) {
        arp::core::appendDiagnostic(
            diagnostics, "FrameGraph RHI compute pass pipeline creation failed");
        return false;
    }

    if ((pass.descriptorTableCount > 0) != (binding->descriptorTable != nullptr)) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-DESCRIPTOR-TABLE-BINDING-MISMATCH");
        return false;
    }
    if (pass.pushConstantSizeBytes > 0
        && (binding->pushConstantData == nullptr
            || binding->pushConstantDataSize != pass.pushConstantSizeBytes)) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-PUSH-CONSTANT-DATA-MISMATCH");
        return false;
    }
    resources.descriptorTable = binding->descriptorTable;
    resources.pushConstantData = binding->pushConstantData;
    resources.pushConstantDataSize = binding->pushConstantDataSize;
    return true;
}

bool prepareTransferResources(const RhiPassRecord& pass,
    rhi::GpuDevice& device,
    FrameGraphRhiPassResources& resources,
    std::pmr::string& diagnostics,
    std::uint32_t frameSlot,
    FrameGraphUniformRingBuffer* ringBuffer,
    const FrameGraphRhiShaderPackageCatalog* shaderPackages)
{
    (void)device;
    (void)frameSlot;
    (void)ringBuffer;
    const auto* binding =
        shaderPackages != nullptr ? shaderPackages->findBinding(pass.name) : nullptr;
    if (binding == nullptr || binding->transferSource == nullptr
        || binding->transferDestination == nullptr) {
        arp::core::appendDiagnostic(diagnostics, "RHI-RG-TRANSFER-RESOURCES-MISSING");
        return false;
    }
    resources.transferSource = binding->transferSource;
    resources.transferDestination = binding->transferDestination;
    return true;
}

/// Emit pre-pass barriers for a pass by calling CommandEncoder::transition
bool emitPassBarriers(const RhiPassRecord& pass,
    const RhiFramePlan& plan,
    rhi::CommandEncoder& encoder,
    const FrameGraphRhiShaderPackageCatalog* shaderPackages,
    FrameGraphRhiCommandExecutionReport& report)
{
    bool success = true;
    for (const auto& barrier : plan.barriers) {
        if (barrier.passName != pass.name) {
            continue;
        }
        if (barrier.resourceKind == ResourceKind::Buffer) {
            auto* buffer =
                shaderPackages != nullptr ? shaderPackages->findBuffer(barrier.handleId) : nullptr;
            if (buffer == nullptr) {
                arp::core::appendDiagnostic(report.diagnostics, "RHI-RG-BARRIER-BUFFER-MISSING");
                success = false;
                continue;
            }
            const auto result = barrier.memoryOnly
                ? encoder.memoryBarrier(*buffer,
                      rhi::ResourceMemoryBarrierDesc{buffer->id(),
                          barrier.sourceStages,
                          barrier.destinationStages,
                          barrier.sourceAccess,
                          barrier.destinationAccess})
                : encoder.transition(*buffer,
                      rhi::ResourceBarrierDesc{buffer->id(),
                          {},
                          barrier.beforeState,
                          barrier.afterState,
                          barrier.sourceStages,
                          barrier.destinationStages,
                          barrier.sourceAccess,
                          barrier.destinationAccess});
            if (!result.success) {
                arp::core::appendDiagnostic(report.diagnostics, result.diagnostics);
                success = false;
                continue;
            }
            ++report.barrierEmissionCount;
            continue;
        }

        auto* texture =
            shaderPackages != nullptr ? shaderPackages->findTexture(barrier.handleId) : nullptr;
        if (!texture) {
            arp::core::appendDiagnostic(report.diagnostics, "RHI-RG-BARRIER-TEXTURE-MISSING");
            success = false;
            continue;
        }

        const auto result = barrier.memoryOnly
            ? encoder.memoryBarrier(*texture,
                  rhi::ResourceMemoryBarrierDesc{texture->id(),
                      barrier.sourceStages,
                      barrier.destinationStages,
                      barrier.sourceAccess,
                      barrier.destinationAccess})
            : encoder.transition(*texture,
                  rhi::ResourceBarrierDesc{texture->id(),
                      rhi::defaultTextureSubresourceRange(texture->desc()),
                      barrier.beforeState,
                      barrier.afterState,
                      barrier.sourceStages,
                      barrier.destinationStages,
                      barrier.sourceAccess,
                      barrier.destinationAccess});
        if (!result.success) {
            arp::core::appendDiagnostic(report.diagnostics,
                std::pmr::string("barrier transition failed for pass '") + pass.name
                    + "' resource '" + barrier.resourceName + "': " + result.diagnostics);
            success = false;
            continue;
        }
        ++report.barrierEmissionCount;
    }
    return success;
}

bool recordPass(const RhiPassRecord& pass,
    rhi::GpuDevice& device,
    rhi::CommandEncoder& encoder,
    FrameGraphRhiCommandExecutionReport& report,
    std::uint32_t frameSlot,
    FrameGraphUniformRingBuffer* ringBuffer,
    const FrameGraphRhiShaderPackageCatalog* shaderPackages,
    const rhi::QueryPool* timingPool = nullptr,
    const rhi::semantics::GpuTimingScopeSlot* timingSlot = nullptr)
{
    FrameGraphRhiRecordedPassReport passReport;
    passReport.name = pass.name;
    passReport.kind = pass.kind;
    passReport.queue = pass.queue;
    FrameGraphRhiPassResources resources;
    const auto fail = [&]() {
        report.recordedPasses.push_back(passReport);
        return false;
    };

    switch (pass.kind) {
    case PassKind::Graphics: {
        if (pass.drawCommandCount > 0
            && !prepareGraphicsResources(pass,
                device,
                resources,
                report.diagnostics,
                frameSlot,
                ringBuffer,
                shaderPackages)) {
            report.recordedPasses.push_back(passReport);
            return false;
        }
        rhi::RenderPassDesc desc;
        desc.colorTargetName = pass.name;
        desc.label = pass.name;
        if (resources.colorTarget != nullptr) {
            desc.colorAttachments.push_back({
                pass.name + "Color",
                resources.colorTarget->id(),
                resources.colorTarget->desc().format,
                rhi::LoadOp::Clear,
                rhi::StoreOp::Store,
            });
        }
        auto renderPass = encoder.beginRenderPass(desc);
        if (!renderPass) {
            arp::core::appendDiagnostic(report.diagnostics, encoder.lastValidation().diagnostics);
            return fail();
        }
        if (timingPool && timingSlot) {
            if (!commandSucceeded(renderPass->writeTimestamp(*timingPool, timingSlot->beginQuery),
                    report.diagnostics)) {
                renderPass->end();
                return fail();
            }
            ++report.gpuTimingWriteCount;
        }
        if (pass.drawCommandCount > 0) {
            if (!commandSucceeded(
                    renderPass->setPipeline(*resources.renderPipeline), report.diagnostics)) {
                renderPass->end();
                return fail();
            }
            for (std::uint32_t set = 0; set < resources.descriptorSets.size(); ++set) {
                const auto& descriptorBinding = resources.descriptorSets[set];
                if (descriptorBinding.descriptorSet == nullptr) {
                    arp::core::appendDiagnostic(
                        report.diagnostics, "RHI-RG-DESCRIPTOR-SET-MISSING");
                    renderPass->end();
                    return fail();
                }
                if (!commandSucceeded(renderPass->bindDescriptorSet(set,
                                          *descriptorBinding.descriptorSet,
                                          descriptorBinding.dynamicOffsets,
                                          descriptorBinding.dynamicOffsetCount),
                        report.diagnostics)) {
                    renderPass->end();
                    return fail();
                }
            }
            for (const auto& draw : pass.drawCommands) {
                if (pass.pushConstantSizeBytes > 0) {
                    if (!commandSucceeded(renderPass->pushConstants(
                                              rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                                              0,
                                              pass.pushConstantSizeBytes,
                                              resources.pushConstantData),
                            report.diagnostics)) {
                        renderPass->end();
                        return fail();
                    }
                    ++passReport.pushConstantCommandCount;
                    ++report.pushConstantCommandCount;
                }
                if (!commandSucceeded(renderPass->drawIndexed(draw.indexCount,
                                          draw.instanceCount,
                                          draw.firstIndex,
                                          draw.vertexOffset,
                                          draw.firstInstance),
                        report.diagnostics)) {
                    renderPass->end();
                    return fail();
                }
                ++passReport.drawCommandCount;
                ++report.drawCommandCount;
            }
        }
        if (timingPool && timingSlot) {
            if (!commandSucceeded(renderPass->writeTimestamp(*timingPool, timingSlot->endQuery),
                    report.diagnostics)) {
                renderPass->end();
                return fail();
            }
            ++report.gpuTimingWriteCount;
        }
        const auto ended = renderPass->end();
        if (!ended.success) {
            arp::core::appendDiagnostic(report.diagnostics, ended.diagnostics);
            return fail();
        }
        ++report.renderPassCount;
        passReport.recordedThroughCommandEncoder = true;
        break;
    }
    case PassKind::Compute: {
        if (pass.dispatchCount > 0
            && !prepareComputeResources(pass,
                device,
                resources,
                report.diagnostics,
                frameSlot,
                ringBuffer,
                shaderPackages)) {
            return fail();
        }
        auto computePass = encoder.beginComputePass({pass.name});
        if (!computePass) {
            arp::core::appendDiagnostic(report.diagnostics, encoder.lastValidation().diagnostics);
            return fail();
        }
        if (timingPool && timingSlot) {
            if (!commandSucceeded(computePass->writeTimestamp(*timingPool, timingSlot->beginQuery),
                    report.diagnostics)) {
                computePass->end();
                return fail();
            }
            ++report.gpuTimingWriteCount;
        }
        if (pass.dispatchCount > 0) {
            if (!commandSucceeded(
                    computePass->setPipeline(*resources.computePipeline), report.diagnostics)) {
                computePass->end();
                return fail();
            }
            if (resources.descriptorTable) {
                if (!commandSucceeded(
                        computePass->bindDescriptorTable(0, *resources.descriptorTable),
                        report.diagnostics)) {
                    computePass->end();
                    return fail();
                }
            }
            for (const auto& dispatch : pass.dispatchCommands) {
                if (pass.pushConstantSizeBytes > 0) {
                    if (!commandSucceeded(computePass->pushConstants(rhi::ShaderStage::Compute,
                                              0,
                                              pass.pushConstantSizeBytes,
                                              resources.pushConstantData),
                            report.diagnostics)) {
                        computePass->end();
                        return fail();
                    }
                    ++passReport.pushConstantCommandCount;
                    ++report.pushConstantCommandCount;
                }
                if (!commandSucceeded(
                        computePass->dispatch(
                            dispatch.groupCountX, dispatch.groupCountY, dispatch.groupCountZ),
                        report.diagnostics)) {
                    computePass->end();
                    return fail();
                }
                ++passReport.dispatchCommandCount;
                ++report.dispatchCommandCount;
            }
        }
        if (timingPool && timingSlot) {
            if (!commandSucceeded(computePass->writeTimestamp(*timingPool, timingSlot->endQuery),
                    report.diagnostics)) {
                computePass->end();
                return fail();
            }
            ++report.gpuTimingWriteCount;
        }
        const auto ended = computePass->end();
        if (!ended.success) {
            arp::core::appendDiagnostic(report.diagnostics, ended.diagnostics);
            return fail();
        }
        ++report.computePassCount;
        passReport.recordedThroughCommandEncoder = true;
        break;
    }
    case PassKind::Copy:
    case PassKind::Present: {
        if (pass.transferCommandCount > 0
            && !prepareTransferResources(pass,
                device,
                resources,
                report.diagnostics,
                frameSlot,
                ringBuffer,
                shaderPackages)) {
            return fail();
        }
        auto transferPass = encoder.beginTransferPass({pass.name});
        if (!transferPass) {
            arp::core::appendDiagnostic(report.diagnostics, encoder.lastValidation().diagnostics);
            return fail();
        }
        for (const auto& transfer : pass.transferCommands) {
            if (!commandSucceeded(transferPass->uploadBuffer({
                                      pass.name + "Upload",
                                      resources.transferSource->id(),
                                      resources.transferDestination->id(),
                                      transfer.sizeBytes,
                                      transfer.stagingIntent,
                                      transfer.backendOwnedStaging,
                                      transfer.frameSerial,
                                  }),
                    report.diagnostics)) {
                transferPass->end();
                return fail();
            }
            ++passReport.transferCommandCount;
            ++report.transferCommandCount;
        }
        const auto ended = transferPass->end();
        if (!ended.success) {
            arp::core::appendDiagnostic(report.diagnostics, ended.diagnostics);
            return fail();
        }
        ++report.transferPassCount;
        passReport.recordedThroughCommandEncoder = true;
        break;
    }
    }

    if (pass.drawCommandCount > 0 || pass.dispatchCount > 0 || pass.transferCommandCount > 0) {
        report.backendOwnedPassResources = true;
    }
    report.recordedPasses.push_back(passReport);
    return true;
}

} // namespace

FrameGraphRhiCommandExecutor::FrameGraphRhiCommandExecutor(
    const FrameGraphRhiCommandExecutorDesc& desc)
    : desc_(desc)
{}

FrameGraphRhiCommandExecutionReport FrameGraphRhiCommandExecutor::execute(
    const CompiledFrameGraph& compiled, std::uint32_t frameSlot, rhi::GpuDevice& device) const
{
    FrameGraphRhiCommandExecutionReport report;
    StatusTelemetryFinalizer telemetryFinalizer{report};
    report.frameSlot = frameSlot;
    report.barrierCount = static_cast<std::uint32_t>(compiled.barrierPlan().size());
    report.loadStorePlanCount = static_cast<std::uint32_t>(compiled.loadStorePlan().size());
    report.backendConsumableBarrierPlan = hasBackendConsumableBarrierPlan(compiled);
    const auto capabilities = device.capabilities();
    report.executionStatuses = capabilities.executionStatuses;
    report.queryExecutionStatuses = capabilities.queryExecutionStatuses;
    report.statusTelemetry = capabilities.statusTelemetry;

    for (const auto& resource : compiled.externalResources()) {
        if (resource.externalProtected) {
            ++report.externalProtectedResourceCount;
        }
    }
    for (const auto& attachment : compiled.loadStorePlan()) {
        report.mobileTileHintsLowered = report.mobileTileHintsLowered
            || attachment.tileHints.discardAfterPass || attachment.tileHints.avoidStore
            || attachment.tileHints.preserveOnlyWhenSampledLater;
        report.memorylessAttachmentIntentLowered =
            report.memorylessAttachmentIntentLowered || attachment.tileHints.memorylessAttachment;
    }

    FrameGraphRhiDiagnosticsAdapter diagnosticsAdapter({
        desc_.logicalFrameSlots,
        desc_.maxFeatureRequirements,
        desc_.maxBarriers,
        desc_.maxPasses,
    });
    report.diagnosticsReport = diagnosticsAdapter.build(compiled, frameSlot);
    if (!report.diagnosticsReport.success) {
        arp::core::appendDiagnostic(report.diagnostics, report.diagnosticsReport.diagnostics);
        return report;
    }
    report.diagnosticsReport.plan.executionStatuses = report.executionStatuses;
    report.diagnosticsReport.plan.queryExecutionStatuses = report.queryExecutionStatuses;
    report.recordedPasses.reserve(report.diagnosticsReport.plan.passes.size());

    auto encoder = device.createCommandEncoder({"FrameGraphRhiCommandExecution"});
    if (!encoder) {
        arp::core::appendDiagnostic(report.diagnostics, "RHI command encoder creation failed");
        return report;
    }
    const auto* timingPlan = desc_.gpuTimingPlan;
    arp::core::UniquePtr<rhi::QueryPool> timingPool;
    std::vector<std::uint64_t> timingScopeIds;
    if (timingPlan && rhi::semantics::gpuTimingPlanValid(*timingPlan)
        && timingPlan->outcome == rhi::semantics::GpuTimingLoweringOutcome::Native) {
        for (const auto& slot : timingPlan->slots)
            timingScopeIds.push_back(slot.scopeId);
        rhi::QueryPoolDesc timingDesc;
        timingDesc.label = "FrameGraphGpuTiming";
        timingDesc.type = rhi::QueryType::Timestamp;
        timingDesc.count = timingPlan->queryCount;
        timingDesc.timingScopeIds = timingScopeIds.data();
        timingDesc.timingScopeIdCount = static_cast<std::uint32_t>(timingScopeIds.size());
        timingPool = device.createQueryPool(timingDesc);
        if (timingPool) {
            report.gpuTimingScopeCount = static_cast<std::uint32_t>(timingPlan->slots.size());
            mergeQueryExecutionStatus(report.queryExecutionStatuses, timingPool->executionStatus());
            report.diagnosticsReport.plan.queryExecutionStatuses = report.queryExecutionStatuses;
        }
    }
    report.passRecordsConsumed = true;
    // Barrier resources are supplied by the explicit FrameGraph catalog.

    // Async-eligible passes may use separate queue segments. Cross-queue
    // transitions require executable L2 TimelineSync and its real primitive;
    // a host counter cannot provide ordering or completion. Missing lowering
    // or primitive creation fails before submission. The explicit no-compute-
    // queue branch below records the workload on the graphics queue instead.
    bool hasAsyncCompute = false;
    for (const auto& pass : report.diagnosticsReport.plan.passes) {
        if (pass.kind == PassKind::Compute) {
            ++report.asyncComputePassCount;
        }
        for (const auto& qr : report.diagnosticsReport.plan.queueRecords) {
            if (qr.passName == pass.name && qr.asyncComputeEligible) {
                hasAsyncCompute = true;
                break;
            }
        }
    }

    if (hasAsyncCompute) {
        // A VkQueryPool is reset by the command buffer that first uses it.
        // Sharing one timing pool across independently submitted graphics and
        // compute segments would therefore race resets and invalidate samples.
        // Segment-owned timing batches are lowered separately; until then the
        // safe contract is to disable this optional timing plan for multi-queue
        // execution rather than publish incorrect scope data.
        if (timingPool) {
            timingPool.reset();
            report.gpuTimingScopeCount = 0u;
        }
        auto computeQueue = device.computeQueue();
        if (!computeQueue) {
            // Backend has no async compute queue; fall back to single-queue
            // execution with passes serialized on the graphics queue.
            ++report.asyncComputeDegradationCount;
            arp::core::appendDiagnostic(report.diagnostics,
                "FrameGraph RHI async compute degraded: backend has no compute queue");
            for (const auto& pass : report.diagnosticsReport.plan.passes) {
                if (!emitPassBarriers(
                        pass, report.diagnosticsReport.plan, *encoder, desc_.shaderPackages, report)
                    || !recordPass(pass,
                        device,
                        *encoder,
                        report,
                        frameSlot,
                        nullptr,
                        desc_.shaderPackages,
                        timingPool.get(),
                        timingSlotForPass(timingPlan, pass.name))) {
                    return report;
                }
                ++report.executedPassCount;
            }
        } else {
            // True multi-queue execution. Passes are split into contiguous
            // same-queue segments following the compiled execution order;
            // each queue transition gets a timeline semaphore signal/wait
            // pair so arbitrary interleavings (graphics → compute → graphics,
            // etc.) are correctly ordered. This replaces the previous fixed
            // "all-compute-then-all-graphics" split which could not express a
            // graphics producer feeding a compute consumer.
            report.asyncComputeSplitExecuted = true;

            const auto& plan = report.diagnosticsReport.plan;
            auto* graphicsQ = &device.queue();

            // Resolve the async-compute eligibility of a plan pass.
            const auto isAsyncPass = [&plan](const RhiPassRecord& pass) {
                for (const auto& qr : plan.queueRecords) {
                    if (qr.passName == pass.name && qr.asyncComputeEligible) {
                        return true;
                    }
                }
                return false;
            };

            // Build contiguous same-queue segments following executionOrder.
            struct Segment {
                rhi::GpuQueue* queue;
                std::pmr::vector<const RhiPassRecord*> passes;
                bool isCompute;
            };

            std::pmr::vector<Segment> segments;
            for (const auto passIdx : compiled.executionOrder()) {
                if (passIdx >= plan.passes.size()) {
                    continue;
                }
                const auto& pass = plan.passes[passIdx];
                auto* targetQueue = isAsyncPass(pass) ? computeQueue : graphicsQ;
                const bool isCompute = (targetQueue == computeQueue);
                if (segments.empty() || segments.back().queue != targetQueue) {
                    segments.push_back({targetQueue, {}, isCompute});
                }
                segments.back().passes.push_back(&pass);
            }

            // Allocate segment encoders before accepting any work. Dependent
            // segments record only after their predecessor is accepted; they
            // must never borrow another recorder's provisional resource state.
            struct SubmitItem {
                rhi::GpuQueue* queue;
                rhi::CommandEncoder* encoder;
                bool isCompute;
            };

            std::pmr::vector<arp::core::UniquePtr<rhi::CommandEncoder>> segmentEncoders;
            std::pmr::vector<SubmitItem> submits;
            segmentEncoders.reserve(segments.size());
            submits.reserve(segments.size());
            for (const auto& seg : segments) {
                auto enc = device.createCommandEncoder(
                    {seg.isCompute ? "FrameGraphRhiAsyncCompute" : "FrameGraphRhiGraphics"});
                if (!enc) {
                    arp::core::appendDiagnostic(
                        report.diagnostics, "FrameGraph RHI multi-queue encoder creation failed");
                    return report;
                }
                submits.push_back({seg.queue, enc.get(), seg.isCompute});
                segmentEncoders.push_back(std::move(enc));
            }

            core::pmr::Vector<core::UniquePtr<rhi::CommandBuffer>> finishedCommands;
            finishedCommands.reserve(submits.size());

            // Submit segments with a single timeline semaphore: each segment
            // (except the last) signals timeline=++value; each segment (except
            // the first) waits on the previous segment's signal value.
            // Submit signature: (cmd, waitSem, waitVal, signalSem, signalVal, fence).
            // Consume the L2 decision before asking the backend for the
            // lowering artifact. A null factory result is a second defensive
            // gate, not the capability negotiation mechanism.
            const bool timelineSupported =
                rhi::isFeatureSupported(capabilities, rhi::EngineFeature::TimelineSync);
            if (submits.size() >= 2u && !timelineSupported) {
                arp::core::appendDiagnostic(report.diagnostics,
                    "FrameGraph RHI async compute failed: TimelineSync lowering is unavailable");
                report.success = false;
                return report;
            }
            arp::core::UniquePtr<rhi::GpuTimelineSemaphore> timelineSem;
            if (submits.size() >= 2u) {
                timelineSem = device.createTimelineSemaphore(0);
            }
            if (!timelineSem && submits.size() >= 2u) {
                // Multi-segment async execution needs a timeline semaphore to
                // order the segments; without one the wait/signal pairs below
                // would be no-ops and segments could execute out of order.
                // Fail-closed with a diagnostic instead of silently proceeding.
                arp::core::appendDiagnostic(report.diagnostics,
                    "FrameGraph RHI async compute failed: createTimelineSemaphore returned null");
                report.success = false;
                return report;
            }
            report.asyncComputeTimelineUsed = submits.size() >= 2u;
            std::uint64_t timelineValue = 0;
            std::uint32_t submittedSegmentCount = 0;
            report.submitSegmentCount = static_cast<std::uint32_t>(submits.size());
            report.submitSegmentQueueRoles.reserve(submits.size());
            for (const auto& s : submits)
                report.submitSegmentQueueRoles.push_back(s.isCompute ? 'C' : 'G');
            for (std::size_t i = 0; i < submits.size(); ++i) {
                auto& segmentEncoder = *submits[i].encoder;
                for (auto* pass : segments[i].passes) {
                    if (!emitPassBarriers(*pass, plan, segmentEncoder, desc_.shaderPackages, report)
                        || !recordPass(*pass,
                            device,
                            segmentEncoder,
                            report,
                            frameSlot,
                            nullptr,
                            desc_.shaderPackages,
                            timingPool.get(),
                            timingSlotForPass(timingPlan, pass->name))) {
                        return report; // Already accepted segments remain accepted.
                    }
                    ++report.executedPassCount;
                }
                auto commandBuffer = segmentEncoder.finish();
                if (!commandBuffer) {
                    core::appendDiagnostic(report.diagnostics,
                        "FrameGraph RHI multi-queue failed: segment encoder finish returned null");
                    return report;
                }
                finishedCommands.push_back(std::move(commandBuffer));
                rhi::GpuTimelineSemaphore* waitSem = (i > 0) ? timelineSem.get() : nullptr;
                const std::uint64_t waitVal = (i > 0) ? timelineValue : 0;
                rhi::GpuTimelineSemaphore* sigSem =
                    (i + 1 < submits.size()) ? timelineSem.get() : nullptr;
                const std::uint64_t sigVal = (i + 1 < submits.size()) ? (timelineValue + 1) : 0;

                const auto result = submits[i].queue->submit(
                    *finishedCommands[i], waitSem, waitVal, sigSem, sigVal);
                if (result.hasAcceptedWork()) {
                    ++submittedSegmentCount;
                    report.submittedCommandCount += result.submittedCommandCount;
                    if (i + 1 < submits.size()) {
                        timelineValue = sigVal;
                    }
                    // Record the compute segment's signal value and the
                    // graphics segment that immediately follows it waits on.
                    if (submits[i].isCompute && sigSem) {
                        report.asyncComputeSignalValue = sigVal;
                    }
                    if (!submits[i].isCompute && i > 0 && submits[i - 1].isCompute) {
                        report.asyncComputeWaitValue = waitVal;
                    }
                    ++report.commandBufferCount;
                }
                if (!result.success) {
                    arp::core::appendDiagnostic(report.diagnostics,
                        result.diagnostics.empty()
                            ? std::pmr::string("FrameGraph RHI multi-queue submission failed")
                            : result.diagnostics);
                    break;
                }
            }
            // Both queues submitted; the timeline semaphore ensures ordering.
            // Skip the normal single-queue submit path below.
            // multiQueueExecuted is honest: true only when every segment
            // submitted successfully. A skipped/failed segment must NOT be
            // reported as real multi-queue execution (that would mask a
            // partial run as success).
            report.multiQueueExecuted =
                !submits.empty() && (submittedSegmentCount == submits.size());
            report.recordedThroughRhiCommandEncoder = report.multiQueueExecuted;
            if (timingPool) {
                mergeQueryExecutionStatus(
                    report.queryExecutionStatuses, timingPool->executionStatus());
                report.diagnosticsReport.plan.queryExecutionStatuses =
                    report.queryExecutionStatuses;
            }
            report.success = report.backendConsumableBarrierPlan
                && report.recordedThroughRhiCommandEncoder && report.passRecordsConsumed
                && report.backendFallbacksHiddenFromUpperLayer
                && !report.nativeHandleExposedToUpperLayer && report.commandBufferCount >= 1
                && report.diagnostics.empty();
            if (report.success) {
                device.pipelineCache().onFrameComplete();
            }
            return report;
        }
    } else {
        for (const auto& pass : report.diagnosticsReport.plan.passes) {
            if (!emitPassBarriers(
                    pass, report.diagnosticsReport.plan, *encoder, desc_.shaderPackages, report)
                || !recordPass(pass,
                    device,
                    *encoder,
                    report,
                    frameSlot,
                    nullptr,
                    desc_.shaderPackages,
                    timingPool.get(),
                    timingSlotForPass(timingPlan, pass.name))) {
                return report;
            }
            ++report.executedPassCount;
        }
    }

    if (!report.diagnostics.empty()) {
        return report;
    }

    auto commandBuffer = encoder->finish();
    if (!commandBuffer) {
        arp::core::appendDiagnostic(report.diagnostics, encoder->lastValidation().diagnostics);
        return report;
    }
    const auto submitted = device.queue().submit(*commandBuffer);
    if (!submitted.success) {
        arp::core::appendDiagnostic(report.diagnostics, submitted.diagnostics);
        return report;
    }
    ++report.commandBufferCount;

    report.recordedThroughRhiCommandEncoder = true;
    report.submittedCommandCount = submitted.submittedCommandCount;
    if (timingPool) {
        mergeQueryExecutionStatus(report.queryExecutionStatuses, timingPool->executionStatus());
        report.diagnosticsReport.plan.queryExecutionStatuses = report.queryExecutionStatuses;
    }
    report.backendFallbacksHiddenFromUpperLayer = true;
    report.nativeHandleExposedToUpperLayer = false;
    report.success = report.backendConsumableBarrierPlan && report.recordedThroughRhiCommandEncoder
        && report.passRecordsConsumed && report.backendFallbacksHiddenFromUpperLayer
        && !report.nativeHandleExposedToUpperLayer;
    if (report.success) {
        device.pipelineCache().onFrameComplete();
        report.pipelineCacheHitCount = device.pipelineCache().renderPipelineHitCount()
            + device.pipelineCache().computePipelineHitCount()
            + device.pipelineCache().layoutHitCount();
        report.pipelineCacheMissCount = device.pipelineCache().renderPipelineMissCount()
            + device.pipelineCache().computePipelineMissCount()
            + device.pipelineCache().layoutMissCount();
        report.pipelineCacheEntryCount = device.pipelineCache().entryCount();
    }
    if (!report.success && report.diagnostics.empty()) {
        arp::core::appendDiagnostic(
            report.diagnostics, "FrameGraph RHI command execution failed validation");
    }
    return report;
}

FrameGraphRhiCommandExecutionReport FrameGraphRhiCommandExecutor::execute(
    const CompiledFrameGraph& compiled,
    std::uint32_t frameSlot,
    rhi::GpuDevice& device,
    FrameGraphUniformRingBuffer& uniformRingBuffer) const
{
    (void)compiled;
    (void)device;
    (void)uniformRingBuffer;
    FrameGraphRhiCommandExecutionReport report;
    report.frameSlot = frameSlot;
    report.diagnostics = "RHI-FRAME-GRAPH-UNIFORM-RING-COMPLETION-SOURCE-UNAVAILABLE";
    return report;
}

FrameGraphRhiActiveExecutor::FrameGraphRhiActiveExecutor(
    const FrameGraphRhiActiveExecutorDesc& desc)
    : desc_(desc)
{}

FrameGraphRhiActiveExecutionReport FrameGraphRhiActiveExecutor::execute(
    const CompiledFrameGraph& compiled,
    std::uint32_t frameSlot,
    rhi::GpuDevice& device,
    const FrameGraphRhiExecutionGate& gate) const
{
    FrameGraphRhiActiveExecutionReport report;
    report.explicitGateEnabled = gate.enabled;

    FrameGraphRhiDiagnosticsAdapter diagnosticsAdapter(desc_.diagnostics);
    report.diagnosticsReport = diagnosticsAdapter.build(compiled, frameSlot);
    report.backendFallbacksHiddenFromUpperLayer =
        report.diagnosticsReport.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer =
        report.diagnosticsReport.nativeHandleExposedToUpperLayer;

    if (!gate.enabled) {
        arp::core::appendDiagnostic(
            report.diagnostics, "FrameGraph RHI active execution requires an explicit gate");
        return report;
    }

    if (!report.diagnosticsReport.success) {
        arp::core::appendDiagnostic(
            report.diagnostics, "FrameGraph RHI active execution requires green diagnostics");
        arp::core::appendDiagnostic(report.diagnostics, report.diagnosticsReport.diagnostics);
        return report;
    }

    if (!gate.runNullHiddenFrameSmoke) {
        arp::core::appendDiagnostic(report.diagnostics,
            "FrameGraph RHI active execution requires the Null hidden-frame smoke gate");
        return report;
    }

    report.diagnosticsOnly = false;
    report.activeExecutionPath = true;

    FrameGraphTransientResourceAllocator transientAllocator;
    report.transientAllocationExecuted = true;
    report.transientAllocation = transientAllocator.allocate(compiled);
    report.transientAllocationSucceeded = report.transientAllocation.success;
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && report.transientAllocation.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
        || report.transientAllocation.nativeHandleExposedToUpperLayer;
    if (!compiled.aliasPlan().empty()
        && (!report.transientAllocation.lifetimeProofValidated
            || !report.transientAllocation.sizeMetadataValidated
            || !report.transientAllocation.budgetProofValidated
            || (report.transientAllocation.aliasedMemory
                && !report.transientAllocation.completionGatedRetirementRequired))) {
        report.transientAllocationSucceeded = false;
        arp::core::appendDiagnostic(report.diagnostics, "RG-ALIAS-EXECUTION-PROOF-INCOMPLETE");
        return report;
    }
    if (!report.transientAllocation.success) {
        arp::core::appendDiagnostic(report.diagnostics, report.transientAllocation.diagnostics);
        return report;
    }

    rhi::NullHiddenFrameSmokeRunner runner(device);
    const auto smoke = runner.run({
        gate.smokeSurfaceName,
        gate.smokeExtent,
        gate.smokeFormat,
        gate.smokeFrameCount,
    });
    report.nullSmoke = summarizeNullSmoke(smoke);
    report.backendFallbacksHiddenFromUpperLayer =
        report.backendFallbacksHiddenFromUpperLayer && smoke.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || smoke.nativeHandleExposedToUpperLayer;
    arp::core::appendDiagnostic(report.diagnostics, smoke.diagnostics);
    report.realRhiCommandSubmission =
        smoke.submittedFrameCount > 0u && smoke.totalSubmittedCommandCount > 0u;

    // Phase 4: Execute real graph passes through the command executor.
    // This closes the lowering gap: the active executor no longer relies
    // solely on the Null smoke; it also records and submits the graph's
    // passes with real barrier emission.
    FrameGraphRhiCommandExecutor commandExecutor({
        desc_.diagnostics.logicalFrameSlots,
        desc_.diagnostics.maxFeatureRequirements,
        desc_.diagnostics.maxBarriers,
        desc_.diagnostics.maxPasses,
        nullptr,
        desc_.shaderPackages,
    });
    report.graphPassExecution = commandExecutor.execute(compiled, frameSlot, device);
    report.graphPassExecutionRan = true;
    if (report.graphPassExecution.success && report.graphPassExecution.submittedCommandCount > 0u) {
        report.realRhiCommandSubmission = true;
    }
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && report.graphPassExecution.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
        || report.graphPassExecution.nativeHandleExposedToUpperLayer;
    if (!report.graphPassExecution.success) {
        arp::core::appendDiagnostic(report.diagnostics, report.graphPassExecution.diagnostics);
    }

    report.success = smoke.success && report.realRhiCommandSubmission
        && report.transientAllocationSucceeded && report.graphPassExecution.success
        && report.backendFallbacksHiddenFromUpperLayer && !report.nativeHandleExposedToUpperLayer
        && report.v1ExecutionUnaffected;
    if (!report.success && report.diagnostics.empty()) {
        arp::core::appendDiagnostic(report.diagnostics, "FrameGraph RHI active execution failed");
    }

    return report;
}

} // namespace arp::render_graph
