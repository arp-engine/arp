#pragma once

#include "core/file_io.h"
#include "platform/render_system_ordered_graphics.h"
#include "renderer/render_graph/conformance_sampled_recorder.h"
#include "renderer/render_graph/conformance_storage_mrt_recorder.h"
#include "rhi/conformance/matrix.h"
#include "webgl_conformance_observation_faults.h"

namespace arp::rhi::webgl::test {
EM_JS(int, conformanceSampledMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('conformanceSampledOnly') === '1';
    // clang-format on
});
EM_JS(int, conformanceSampledVersion, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('conformanceSampledVersion') === '2' ? 2 : 1;
    // clang-format on
});
EM_JS(int, conformanceSampledBudgetFault, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('conformanceSampledBudgetFault') === '1';
    // clang-format on
});
EM_JS(void,
    reportConformanceSampled,
    (int success,
        const char* diagnostic,
        const char* hash,
        int frames,
        int pixels,
        int sealed,
        int accepted,
        int retired,
        int lost),
    {
        // clang-format off
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'ConformanceSampledExecution',contractVersion:1,success:!!success,
        storageMrtGate:Module.arpConformanceStorageGate,
        diagnostic:UTF8ToString(diagnostic),workloadSha256:UTF8ToString(hash),frames,
        verifiedPixels:pixels,sealedOutputValidated:!!sealed,rejectedAfterSubmit:!!accepted,retiredAuditRejected:!!retired,contextLossAuditRejected:!!lost,validatorOnly:false,webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        gpuCompletionClaimed:false,productionReady:false,readinessPromoted:false})});
    Module.arpWorkloadFinished?.();
        // clang-format on
    });

EM_JS(void, reportConformanceStorageGate, (const char* hash, const char* diagnostic), {
    // clang-format off
    Module.arpConformanceStorageGate = {workloadSha256:UTF8ToString(hash),diagnostic:UTF8ToString(diagnostic),
        resourceCreationAttempted:false,readinessPromoted:false};
    // clang-format on
});

class ConformanceSampledFrameWorkload final : public platform::OrderedGraphicsFrameWorkload {
public:
    std::array<std::shared_ptr<RenderPipeline>, 1> native;
    render_graph::ConformanceSampledRecorder* recorder = nullptr;
    std::size_t frameIndex = 0;

    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines() const noexcept override
    {
        return native;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle target) override
    {
        recorder->record(frameIndex, graph, target);
    }
};

class ConformanceSampledSmoke final {
public:
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()},
        hash{&core::defaultCpuMemoryResource()};
    int frames = 0, verifiedPixels = 0;
    bool sealedOutputValidated = false, rejectedAfterSubmit = false, retiredAuditRejected = false,
         contextLossAuditRejected = false;

    bool run(core::UniquePtr<WebGlDevice> device,
        const std::shared_ptr<RenderPipeline>& original,
        const ShaderPackageDesc& package,
        std::uint32_t vertexVectors,
        std::uint32_t fragmentVectors,
        double (*nextFrame)())
    {
        try {
            const auto storageSource =
                core::readTextFile("/tests/fixtures/rhi_conformance_storage_mrt.yaml", 16384);
            const char* storageId = nullptr;
            const auto storage = storageSource.success
                ? ConformanceStorageMrtWorkload::parse(
                      storageSource.text, ConformanceWorkloadBudget{16384, 4096, 3}, storageId)
                : nullptr;
            if (!storage)
                throw core::DiagnosticError(storageId ? storageId : "CONFORMANCE-STORAGE-FIXTURE");
            const auto beforeStorage = device->memoryStats().allocatedBytes;
            render_graph::ConformanceRecorderDesc storageConfig{1, 3, true, 1024 * 1024};
            storageConfig.quota =
                core::makeShared<core::CpuMemoryQuota>(storageConfig.maxCpuMetadataBytes);
            if (render_graph::ConformanceStorageMrtRecorder::prepare(
                    *device, {}, {}, *storage, storageConfig, diagnostic)
                || diagnostic != "RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-UNAVAILABLE"
                || storageConfig.quota->liveBytes()
                || device->memoryStats().allocatedBytes != beforeStorage)
                throw core::DiagnosticError("CONFORMANCE-STORAGE-WEBGL-GATE");
            const auto storageHash = core::pmr::String(storage->sourceSha256());
            reportConformanceStorageGate(storageHash.c_str(), diagnostic.c_str());
            using Recorder = render_graph::ConformanceSampledRecorder;
            constexpr ConformanceSampledWorkloadBudget budget{16384, 4096, 3, 4};
            const auto source = core::readTextFile(conformanceSampledVersion() == 2
                    ? "/tests/fixtures/rhi_conformance_pingpong.yaml"
                    : "/tests/fixtures/rhi_conformance_sampled.yaml",
                budget.maxYamlBytes);
            if (!source.success)
                throw core::DiagnosticError(source.diagnostics);
            const char* id = nullptr;
            if (ConformanceSampledWorkload::parse(
                    source.text + "\n---\nignored: true\n", budget, id))
                throw core::DiagnosticError("CONFORMANCE-SAMPLED-MULTIPLE-DOCUMENTS");
            auto fixtureOwner = ConformanceSampledWorkload::parse(
                ConformanceObservationFaults::issuerSource(source.text), budget, id);
            const auto* fixture = fixtureOwner.get();
            if (!fixture)
                throw core::DiagnosticError(id);
            hash = fixture->sourceSha256();
            auto state = Recorder::pipelineDefinition(
                *original->layout(), package, TextureFormat::Rgba8Unorm);
            state.shaderPackage = nullptr;
            if (conformanceObservationFault() == 8)
                state.colorTargets[0].writeMask = ColorWriteMask::Red;
            ConformanceSampledFrameWorkload workload;
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
            request.requirements = {1, 1, 64, 64, 3, vertexVectors, fragmentVectors, 0, 2};
            auto executionQuota =
                core::makeShared<core::CpuMemoryQuota>(request.maxCpuMetadataBytes);
            auto execution = core::shareUnique(
                device->createOrderedGraphicsExecution(request, executionQuota, id));
            if (!execution)
                throw core::DiagnosticError(id);
            render_graph::ConformanceSampledRecorderDesc config{
                1, budget.maxFrames, true, 1024 * 1024};
            config.quota = core::makeShared<core::CpuMemoryQuota>(config.maxCpuMetadataBytes);
            const auto recorderFixture = ConformanceSampledWorkload::parse(
                ConformanceObservationFaults::recorderSource(source.text), budget, id);
            if (!recorderFixture)
                throw core::DiagnosticError(id);
            auto recorder = Recorder::prepare(
                *device, workload.native[0], *recorderFixture, config, diagnostic);
            if (!recorder) {
                if (const auto* reason = device->diagnostic()) {
                    diagnostic += ": ";
                    diagnostic += reason;
                }
                throw core::DiagnosticError(diagnostic);
            }
            workload.recorder = recorder.get();
            const auto observation = ConformanceObservationFaults::request();
            auto observationQuota = conformanceObservationFault() == 14
                ? executionQuota
                : core::makeShared<core::CpuMemoryQuota>(observation.maxCpuMetadataBytes);
            if (const auto* error = execution->beginConformance(
                    {}, std::move(fixtureOwner), observation, observationQuota))
                throw core::DiagnosticError(error);
            if (RhiConformanceRunner::auditOriginal(execution->conformanceRun())
                    .originalOutputValidated)
                throw core::DiagnosticError("CONFORMANCE-PREMATURE-AUDIT");
            platform::RenderSystemOrderedGraphicsDesc desc;
            desc.contractVersion = 1;
            desc.prototypeOptIn = true;
            desc.surface = {"#canvas", fixture->frames()[0].extent, TextureFormat::Rgba8Unorm, 0};
            const auto draws = static_cast<std::uint32_t>(fixture->frames()[0].draws.size());
            const auto passes = draws + 2;
            desc.commands = {3,
                true,
                draws,
                256 * draws,
                3,
                {1, true, draws + 2 * passes, 128, 96, 512 * 1024, 256},
                1,
                draws == 1 ? 3u : 5u,
                passes,
                32};
            desc.maxFrameCpuMetadataBytes = 4 * 1024 * 1024;
            desc.frameQuota = core::makeShared<core::CpuMemoryQuota>(desc.maxFrameCpuMetadataBytes);
            desc.commandQuota = conformanceObservationFault() == 15
                ? observationQuota
                : core::makeShared<core::CpuMemoryQuota>(desc.commands.issue.maxCpuMetadataBytes);
            if (conformanceObservationFault() == 15)
                desc.commands.issue.maxCpuMetadataBytes = observation.maxCpuMetadataBytes;
            auto path = platform::RenderSystem::createOrderedGraphicsPath(
                std::move(device), execution, desc, id);
            if (!path)
                throw core::DiagnosticError(id);
            for (std::size_t i = 0; i < fixture->frames().size(); ++i) {
                const auto& frame = fixture->frames()[i];
                core::requireSuccessfulResult(
                    path->resize(frame.extent), "CONFORMANCE-SAMPLED-RESIZE");
                if (!nextFrame())
                    throw core::DiagnosticError("CONFORMANCE-SAMPLED-RAF");
                workload.frameIndex = i;
                const auto report = path->renderFrame(workload);
                if (!report.success) {
                    rejectedAfterSubmit = report.workAccepted;
                    throw core::DiagnosticError(report.diagnostics);
                }
                if (!report.workAccepted || !report.presented || !report.recycled
                    || report.gpuCompletionClaimed
                    || report.executedPasses != frame.draws.size() + 2)
                    throw core::DiagnosticError("CONFORMANCE-SAMPLED-EXECUTION");
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
            if (conformanceObservationFault() == 17) {
                if (!conformanceLoseOriginalContext())
                    throw core::DiagnosticError("CONFORMANCE-LOSS-INJECTION-UNAVAILABLE");
                const auto lost = RhiConformanceRunner::auditOriginal(execution->conformanceRun());
                contextLossAuditRejected = !lost.originalOutputValidated && lost.diagnostic
                    && (core::StringView(lost.diagnostic) == "RHI-WEBGL1-CONTEXT-LOST"
                        || core::StringView(lost.diagnostic)
                            == "RHI-CONFORMANCE-ORIGINAL-OWNER-RETIRED");
                if (!contextLossAuditRejected)
                    throw core::DiagnosticError("CONFORMANCE-LOST-AUDIT");
            }
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
