#pragma once

#include "rhi/backends/webgl/webgl_execution.h"
#include "webgl_execution_quality_smoke.h"

namespace arp::rhi::webgl::test {
EM_JS(void,
    reportCommandBudget,
    (int mode,
        int accepted,
        double peak,
        double uniforms,
        double operations,
        const char* diagnostic),
    {
        // clang-format off
    Module.webGlQuality.budgets.push({mode,accepted:!!accepted,cpuPeakBytes:peak,uniformCapacityBytes:uniforms,recordedOperations:operations,diagnostic:UTF8ToString(diagnostic)});
        // clang-format on
    });

// One original workload, one recording recipe. Negative budgets never issue GL.
class CommandBudgetSmoke final {
public:
    WebGlDevice& device;
    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines;
    const std::shared_ptr<WebGlFramebuffer>& target;
    const RenderPassDesc& pass;
    GpuBuffer& vertex;
    GpuBuffer& index;
    DescriptorSet& descriptors;
    core::ArrayView<float> push;

    bool run()
    {
        semantics::WebGlPreparedCommandsRequest exact;
        exact.contractVersion = 2;
        exact.prototypeOptIn = true;
        exact.maxPipelines = 2;
        exact.maxDraws = 2;
        exact.maxUniformBytes = 320;
        exact.maxAnalysisIndices = 3;
        exact.issue = {1, true, 4, 7, 16, 256 * 1024, 15};
        std::uint64_t measuredPeak = 0;
        for (int mode = 0; mode < 10; ++mode) {
            auto request = exact;
            if (mode > 0)
                request.issue.maxCpuMetadataBytes = measuredPeak;
            switch (mode) {
            case 2:
                --request.issue.maxCpuMetadataBytes;
                break;
            case 3:
                --request.maxUniformBytes;
                break;
            case 4:
                --request.issue.maxOperations;
                break;
            case 5:
                --request.issue.maxSnapshotBytes;
                break;
            case 6:
                --request.issue.maxOwnerReferences;
                break;
            case 7:
                --request.issue.maxResourceOwners;
                break;
            case 8:
                --request.maxAnalysisIndices;
                break;
            case 9:
                request.maxDraws = UINT32_MAX;
                request.maxUniformBytes = UINT64_MAX;
                break;
            }
            auto quota = core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
            ExecutionAudit audit(device, "command-budget-original-workload");
            const char* diagnostic = device.prepareCommand(pipelines, target, request, quota);
            auto encoder =
                diagnostic ? core::UniquePtr<CommandEncoder>{} : device.createCommandEncoder({});
            core::UniquePtr<RenderPassEncoder> recording;
            core::UniquePtr<CommandBuffer> commands;
            bool accepted = false;
            if (encoder) {
                recording = encoder->beginRenderPass(pass);
                if (recording && recording->setPipeline(*pipelines[0]).success
                    && recording->setViewport({0, 0, 64, 64, 0, 1}).success
                    && recording->setScissor({0, 0, 64, 64}).success
                    && recording->bindVertexBuffer(3, vertex).success
                    && recording->bindIndexBuffer(index, IndexFormat::Uint16).success
                    && recording->bindDescriptorSet(0, descriptors).success
                    && recording
                        ->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment,
                            0,
                            push.size() * sizeof(float),
                            push.data())
                        .success
                    && recording->drawIndexed(3, 1, 0, 0, 0).success
                    && recording->setPipeline(*pipelines[1]).success
                    && recording->draw(3, 1, 0, 0).success) {
                    // Exact draw capacity is consumed; the next draw cannot grow storage.
                    if (recording->draw(3, 1, 0, 0).success)
                        return false;
                    if (recording->end().success)
                        commands = encoder->finish();
                    accepted = bool(commands);
                }
            }
            const auto budget = commands ? WebGlExecution::commandBudget(*commands)
                                         : WebGlExecution::CommandBudgetReport{};
            core::pmr::String reason{&core::defaultCpuMemoryResource()};
            if (!accepted)
                reason = diagnostic ? diagnostic
                    : encoder       ? encoder->lastValidation().diagnosticId.c_str()
                                    : device.diagnostic();
            if (!audit.finish(true) || accepted != (mode < 2) || (!accepted && reason.empty()))
                return false;
            if (mode == 0)
                measuredPeak = quota->peakBytes();
            if (accepted
                && (!budget.originalPayload || budget.recordedDraws != 2
                    || budget.recordedOperations != 4 || budget.uniformCapacityBytes != 320
                    || budget.uniformRecordedBytes != 320))
                return false;
            reportCommandBudget(mode,
                accepted,
                quota->peakBytes(),
                budget.uniformCapacityBytes,
                budget.recordedOperations,
                reason.c_str());
            commands.reset();
            recording.reset();
            encoder.reset();
            if (quota->liveBytes())
                return false;
        }
        return true;
    }
};
} // namespace arp::rhi::webgl::test
