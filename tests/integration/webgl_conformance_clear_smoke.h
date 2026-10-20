#pragma once

#include "core/file_io.h"
#include "platform/render_system_ordered_graphics.h"
#include "rhi/conformance/clear_workload.h"
#include "rhi/conformance/matrix.h"
#include "webgl_conformance_observation_faults.h"

namespace arp::rhi::webgl::test {
EM_JS(int, conformanceClearMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('conformanceClearOnly') === '1';
    // clang-format on
});
EM_JS(void,
    reportConformanceClear,
    (int success,
        const char* diagnostic,
        const char* hash,
        int frames,
        int pixels,
        int sealed,
        int accepted,
        int retired),
    {
        // clang-format off
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'ConformanceClearExecution',contractVersion:1,success:!!success,
        diagnostic:UTF8ToString(diagnostic),workloadSha256:UTF8ToString(hash),frames,
        verifiedPixels:pixels,sealedOutputValidated:!!sealed,rejectedAfterSubmit:!!accepted,retiredAuditRejected:!!retired,validatorOnly:false,webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        gpuCompletionClaimed:false,productionReady:false,readinessPromoted:false})});
    Module.arpWorkloadFinished?.();
        // clang-format on
    });

class ConformanceClearFrameWorkload final : public platform::OrderedGraphicsFrameWorkload {
public:
    std::array<std::shared_ptr<RenderPipeline>, 1> native;
    ConformanceClearFrame frame;

    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines() const noexcept override
    {
        return native;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle canvas) override
    {
        const auto& desc = graph.textureDesc(canvas);
        if (desc.extent.width != frame.extent.width || desc.extent.height != frame.extent.height)
            throw core::DiagnosticError("RHI-CONFORMANCE-SURFACE-EXTENT");
        graph.addPass("ConformanceClearWorkload/1")
            .writeAttachment(canvas,
                ResourceState::ColorAttachment,
                LoadOp::Clear,
                StoreOp::Store,
                {},
                frame.clearColor())
            .executeRenderPass([](render_graph::RenderPassContext&) {});
    }
};

class ConformanceClearSmoke final {
public:
    core::pmr::String diagnostic, hash;
    int frames = 0, verifiedPixels = 0;
    bool sealedOutputValidated = false, rejectedAfterSubmit = false, retiredAuditRejected = false;

    bool run(core::UniquePtr<WebGlDevice> device,
        const std::shared_ptr<RenderPipeline>& original,
        double (*nextFrame)())
    {
        try {
            constexpr ConformanceWorkloadBudget budget{8192, 4096, 3};
            const auto source = core::readTextFile(
                "/tests/fixtures/rhi_conformance_clear.yaml", budget.maxYamlBytes);
            if (!source.success)
                throw core::DiagnosticError(source.diagnostics);
            const char* id = nullptr;
            if (ConformanceClearWorkload::parse(source.text + "\n---\nignored: true\n", budget, id))
                throw core::DiagnosticError("CONFORMANCE-CLEAR-MULTIPLE-DOCUMENTS-ACCEPTED");
            auto fixtureOwner = ConformanceClearWorkload::parse(source.text, budget, id);
            const auto* fixture = fixtureOwner.get();
            if (!fixture)
                throw core::DiagnosticError(id);
            hash = fixture->sourceSha256();
            auto state = RenderPipelineStateCopy::copy(
                dynamic_cast<WebGlRenderPipeline*>(original.get())->impl->definition);
            state.depthFormat = state.depthStencil.format = TextureFormat::Unknown;
            state.shaderPackage = nullptr;
            ConformanceClearFrameWorkload workload;
            workload.native[0] = core::shareUnique(device->createRenderPipeline(state));
            if (!workload.native[0])
                throw core::DiagnosticError(device->diagnostic());
            if (const auto* error = device->enableSurfaceLifecycle({1, true, budget.maxFrames}))
                throw core::DiagnosticError(error);
            OrderedGraphicsExecutionRequest request;
            request.contractVersion = 1;
            request.prototypeOptIn = true;
            request.maxSubmissions = budget.maxFrames;
            request.maxCpuMetadataBytes = 256 * 1024;
            request.requirements = {1, 1, 64, 64, 4, 32, 32, 0, 2};
            auto executionQuota =
                core::makeShared<core::CpuMemoryQuota>(request.maxCpuMetadataBytes);
            auto execution = core::shareUnique(
                device->createOrderedGraphicsExecution(request, executionQuota, id));
            if (!execution)
                throw core::DiagnosticError(id);
            const auto observation = ConformanceObservationFaults::request();
            auto observationQuota =
                core::makeShared<core::CpuMemoryQuota>(observation.maxCpuMetadataBytes);
            if (const auto* error = execution->beginConformance(
                    std::move(fixtureOwner), {}, observation, observationQuota))
                throw core::DiagnosticError(error);
            if (RhiConformanceRunner::auditOriginal(execution->conformanceRun())
                    .originalOutputValidated)
                throw core::DiagnosticError("CONFORMANCE-PREMATURE-AUDIT");
            platform::RenderSystemOrderedGraphicsDesc desc;
            desc.contractVersion = 1;
            desc.prototypeOptIn = true;
            desc.surface = {"#canvas", fixture->frames()[0].extent, TextureFormat::Rgba8Unorm, 0};
            desc.commands = {3, true, 1, 160, 3, {1, true, 5, 32, 24, 256 * 1024, 32}, 1, 2, 2, 16};
            desc.maxFrameCpuMetadataBytes = 4 * 1024 * 1024;
            desc.frameQuota = core::makeShared<core::CpuMemoryQuota>(desc.maxFrameCpuMetadataBytes);
            desc.commandQuota =
                core::makeShared<core::CpuMemoryQuota>(desc.commands.issue.maxCpuMetadataBytes);
            auto path = platform::RenderSystem::createOrderedGraphicsPath(
                std::move(device), execution, desc, id);
            if (!path)
                throw core::DiagnosticError(id);
            for (const auto& frame : fixture->frames()) {
                core::requireSuccessfulResult(
                    path->resize(frame.extent), "CONFORMANCE-CLEAR-RESIZE");
                if (!nextFrame())
                    throw core::DiagnosticError("CONFORMANCE-CLEAR-RAF");
                workload.frame = frame;
                if (conformanceObservationFault() == 1)
                    ++workload.frame.rgba8[0];
                const auto result = path->renderFrame(workload);
                if (!result.success) {
                    rejectedAfterSubmit = result.workAccepted;
                    throw core::DiagnosticError(result.diagnostics);
                }
                if (!result.workAccepted || !result.presented || !result.recycled
                    || result.gpuCompletionClaimed || result.executedPasses != 1)
                    throw core::DiagnosticError("CONFORMANCE-CLEAR-EXECUTION");
                // Cold test observation, never production L2 readback or a completion receipt.
                const auto* sealed = execution->conformanceRun();
                const auto pixels = sealed->pixels(std::size_t(frames));
                const auto count = frame.extent.width * frame.extent.height;
                if (pixels.size() != count * 4
                    || sealed->observedFrames() != std::size_t(frames + 1))
                    throw core::DiagnosticError("CONFORMANCE-ORIGINAL-SEALED-OUTPUT");
                verifiedPixels += int(count);
                ++frames;
            }
            const auto audit = RhiConformanceRunner::auditOriginal(execution->conformanceRun());
            if (!audit.originalOutputValidated || audit.frames != 3
                || audit.actualOutputBytes != 9728 * 4 || audit.backendReadinessPromoted
                || audit.platformValidated || audit.productionReady)
                throw core::DiagnosticError(
                    audit.diagnostic ? audit.diagnostic : "CONFORMANCE-ORIGINAL-AUDIT");
            sealedOutputValidated = true;
            path.reset();
            const auto retired = RhiConformanceRunner::auditOriginal(execution->conformanceRun());
            retiredAuditRejected = !retired.originalOutputValidated && retired.diagnostic
                && core::StringView(retired.diagnostic) == "RHI-CONFORMANCE-ORIGINAL-OWNER-RETIRED";
            if (!retiredAuditRejected)
                throw core::DiagnosticError("CONFORMANCE-RETIRED-AUDIT");
            return true;
        } catch (const std::exception& error) {
            diagnostic = error.what();
            return false;
        }
    }
};
} // namespace arp::rhi::webgl::test
