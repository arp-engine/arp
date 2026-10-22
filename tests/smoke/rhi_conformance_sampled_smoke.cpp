#include "../fixtures/conformance_sampled_native_negatives.h"
#include "../fixtures/renderdoc_capture.h"
#include "core/diagnostics/diagnostic_error.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "platform/render_system.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/conformance_sampled_recorder.h"
#include "rhi/backends/null/null_backend.h"
#include "shader/graphics_shader_module_artifact.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv)
{
    using namespace arp;
    using Recorder = render_graph::ConformanceSampledRecorder;
    assert(argc == 4);
    const bool validator = std::strcmp(argv[1], "--null") == 0;
    const bool negatives = std::strcmp(argv[1], "--vulkan-original-negative") == 0;
    const bool certified = negatives || std::strcmp(argv[1], "--vulkan-original") == 0;
    assert(validator || certified || std::strcmp(argv[1], "--vulkan") == 0);
    constexpr rhi::ConformanceSampledWorkloadBudget budget{16384, 4096, 3, 4};
    const auto source = core::readTextFile(argv[2], budget.maxYamlBytes);
    assert(source.success);
    const char* id = nullptr;
    const auto workload = rhi::ConformanceSampledWorkload::parse(source.text, budget, id);
    assert(workload && !id);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    const shader::GraphicsShaderModuleLoadBudget load{
        4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32, 32, 4096, 32};
    auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(load.maxCpuMetadataBytes);
    const auto artifact =
        shader::GraphicsShaderModuleArtifact::load(argv[3], load, artifactQuota, diagnostic);
    if (!artifact) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 1;
    }
    rhi::ShaderPackageDesc package;
    core::requireSuccessfulResult(rhi::populateGraphicsModuleShaderInterface(*artifact, package),
        "CONFORMANCE-SHADER-INTERFACE");
    const auto artifactDirectory = std::filesystem::path(argv[3]).parent_path();
    for (auto* path : {&package.slangModulePath,
             &package.artifacts.spirvPath,
             &package.artifacts.mslPath,
             &package.artifacts.wgslPath,
             &package.artifacts.reflectionPath})
        *path = (artifactDirectory / path->c_str()).generic_string().c_str();
    for (auto& entry : package.entrypoints)
        entry.modulePath = package.slangModulePath;
    auto definition = rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection);
    if (!definition.pushConstantRanges.empty())
        definition.pushConstantRanges[0].stages =
            *rhi::parseShaderReflectionPushConstantStages(package.reflection.pushConstantStages);
    platform::WindowSystem windows;
    platform::PlatformWindow window;
    platform::RenderSystem system;
    core::UniquePtr<rhi::GpuDevice> nullDevice;
    if (validator)
        nullDevice = rhi::createNullDevice();
    else {
        if (!windows.initialize().success)
            return 77;
        const auto first = workload->frames()[0].extent;
        window = windows.createWindow({"ConformanceSampledWorkload/1",
            int(first.width),
            int(first.height),
            true,
            true,
            true,
            false});
        if (!window.valid())
            return 77;
        system =
            platform::createDefaultRenderSystem(windows, window, "ConformanceSampledWorkload/1");
        if (!system.valid() || system.backend() != rhi::Backend::Vulkan)
            return 77;
    }
    auto& device = validator ? *nullDevice : *system.gpuDevice();
    const auto layout = device.createPipelineLayout(definition);
    assert(layout);
    const auto state = Recorder::pipelineDefinition(
        *layout, package, validator ? rhi::TextureFormat::Rgba8Unorm : system.colorFormat());
    const auto validation = device.validateRenderPipeline(state);
    if (!validation.success) {
        std::fprintf(stderr, "%s\n", validation.diagnostics.c_str());
        return 1;
    }
    auto domainQuota = core::makeShared<core::CpuMemoryQuota>(65536);
    auto lossQuota = core::makeShared<core::CpuMemoryQuota>(65536);
    auto outputQuota = core::makeShared<core::CpuMemoryQuota>(4 * 1024 * 1024);
    core::UniquePtr<rhi::OriginalConformanceSampledExecution> execution;
    if (certified) {
        assert(!device.prepareOriginalDeviceLifetime({1, true, 1, 2, 65536}, domainQuota));
        assert(!device.prepareOriginalDeviceNativeResultLoss({1, true, 1, 2, 65536}, lossQuota));
        if (negatives) {
            tests::ConformanceSampledNativeNegatives::verify(
                device, *system.swapchain(), source.text, state, artifact);
            std::printf("nativeOriginalSampledNegatives: 21\nreadinessPromoted: false\n");
            return 0;
        }
        auto original = rhi::ConformanceSampledWorkload::parse(source.text, budget, id);
        execution = device.createConformanceSampledExecution(std::move(original),
            {{{1, 3, true, 4096 * 4 * 3, outputQuota->limitBytes()}, 1024 * 1024, 5000000000},
                1024 * 1024},
            outputQuota,
            id);
        if (id)
            std::fprintf(stderr, "[sampled-issuer] %s\n", id);
        assert(execution && !id);
    }
    auto pipeline = execution ? execution->preparePipeline(state, artifact, id)
                              : core::shareUnique(device.createRenderPipeline(state));
    if (id)
        std::fprintf(stderr, "[sampled-pipeline] %s\n", id);
    assert(pipeline);
    assert(pipeline->shaderReflection().sourceSha256 == package.reflection.sourceSha256);
    assert(pipeline->shaderReflection().reflectionContractVersion
        == package.reflection.reflectionContractVersion);
    std::shared_ptr<rhi::RenderPipeline> intermediate = pipeline;
    bool needsIntermediate = false;
    for (const auto& frame : workload->frames())
        for (const auto& draw : frame.draws)
            needsIntermediate =
                needsIntermediate || draw.target != rhi::ConformanceSampledResource::Output;
    if (needsIntermediate && state.colorFormat != rhi::TextureFormat::Rgba8Unorm) {
        auto intermediateState =
            Recorder::pipelineDefinition(*layout, package, rhi::TextureFormat::Rgba8Unorm);
        intermediate = execution
            ? execution->preparePipeline(intermediateState, artifact, id)
            : core::shareUnique(device.createRenderPipeline(intermediateState));
        assert(intermediate && !id);
    }
    render_graph::ConformanceSampledRecorderDesc config{1, budget.maxFrames, true, 1024 * 1024};
    config.quota = core::makeShared<core::CpuMemoryQuota>(config.maxCpuMetadataBytes);
    if (needsIntermediate && state.colorFormat != rhi::TextureFormat::Rgba8Unorm) {
        assert(!Recorder::prepare(device, pipeline, *workload, config, diagnostic));
        assert(diagnostic == "RHI-CONFORMANCE-SAMPLED-INTERMEDIATE-PIPELINE-FORMAT");
    }
    auto invalid = config;
    invalid.prototypeOptIn = false;
    assert(!Recorder::prepare(device, pipeline, *workload, invalid, diagnostic));
    invalid = config;
    invalid.maxPreparedFrames = 2;
    assert(!Recorder::prepare(device, pipeline, *workload, invalid, diagnostic));
    invalid = config;
    invalid.maxCpuMetadataBytes = 1;
    invalid.quota = core::makeShared<core::CpuMemoryQuota>(1);
    assert(!Recorder::prepare(device, pipeline, *workload, invalid, diagnostic));
    auto changedSource = source.text;
    changedSource[changedSource.find("7f32928")] = '0';
    const auto changed = rhi::ConformanceSampledWorkload::parse(changedSource, budget, id);
    assert(changed && !Recorder::prepare(device, pipeline, *changed, config, diagnostic));
    assert(diagnostic == "RHI-CONFORMANCE-SAMPLED-SHADER-IDENTITY");
    auto recorder = Recorder::prepare(
        device, pipeline, *workload, config, diagnostic, execution.get(), intermediate);
    if (!recorder) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 1;
    }
    tests::RenderDocCaptureScope capture;
    for (std::size_t i = 0; i < workload->frames().size(); ++i) {
        const auto& frame = workload->frames()[i];
        core::UniquePtr<rhi::Swapchain> nullSurface;
        if (validator)
            nullSurface = device.createSwapchain(
                {"Conformance.Sampled", frame.extent, rhi::TextureFormat::Rgba8Unorm, 2});
        else {
            window.resize(int(frame.extent.width), int(frame.extent.height));
            assert(system.resize(frame.extent).success);
        }
        auto& surface = validator ? *nullSurface : *system.swapchain();
        const auto acquired = surface.acquireNextFrame();
        assert(acquired.success);
        auto image = surface.acquiredImage(acquired);
        assert(image.success && image.texture);
        render_graph::FrameGraph graph(device);
        const auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
        recorder->record(i, graph, target);
        if (i + 1 == workload->frames().size())
            recorder.reset(); // The graph must retain every original resource until execution ends.
        graph.compile();
        try {
            if (execution)
                graph.executeConformance(*execution, target);
            else
                graph.execute();
        } catch (const core::DiagnosticError& error) {
            std::fprintf(stderr, "[sampled-execution] %s\n", error.what());
            return 1;
        }
        assert(surface.present(acquired).success);
        if (execution) {
            const auto* failure = execution->finalizePresented();
            if (failure)
                std::fprintf(stderr, "[sampled-present] %s\n", failure);
            assert(!failure);
        }
        if (!validator) {
            rhi::semantics::ShutdownDrainRequest drain;
            drain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            assert(system.drainGpuForShutdown(drain).completed());
        }
    }
    if (execution) {
        const auto report = rhi::OriginalConformanceAudit::inspect(execution->run());
        if (report.diagnostic)
            std::fprintf(stderr, "[sampled-audit] %s\n", report.diagnostic);
        assert(report.originalOutputValidated && report.frames == 3
            && report.actualOutputBytes == 38912);
        assert(!report.platformValidated && !report.productionReady
            && !report.backendReadinessPromoted);
        for (std::size_t i = 0; i < 3; ++i) {
            const auto pixels = execution->run()->pixels(i);
            const auto hash = core::Sha256::hexDigest(
                {reinterpret_cast<const char*>(pixels.data()), pixels.size()});
            std::printf("actualFrame%zuSha256: %s\n", i, hash.c_str());
        }
        std::printf("sealedOriginalOutputValidated: true\nactualOutputBytes: %zu\n",
            report.actualOutputBytes);
        outputQuota->setLimitBytes(outputQuota->limitBytes() - 1);
        assert(!rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
        outputQuota->setLimitBytes(outputQuota->limitBytes() + 1);
        assert(rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
        artifactQuota->setLimitBytes(load.maxCpuMetadataBytes - 1);
        assert(!rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
        artifactQuota->setLimitBytes(load.maxCpuMetadataBytes);
        assert(rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
        device.retireOriginalDeviceLifetime();
        assert(!rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
    }
    std::printf(
        "type: ConformanceSampledExecution\ncontractVersion: 1\nworkloadSha256: %.*s\nframes: 3\nvalidatorOnly: %s\noutputEvidenceClaimed: false\nreadinessPromoted: false\n",
        int(workload->sourceSha256().size()),
        workload->sourceSha256().data(),
        validator ? "true" : "false");
}
