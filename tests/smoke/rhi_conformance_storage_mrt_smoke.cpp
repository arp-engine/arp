#include "../fixtures/conformance_storage_mrt_native_negatives.h"
#include "../fixtures/renderdoc_capture.h"
#include "core/diagnostics/diagnostic_error.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "platform/render_system.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/conformance_storage_mrt_recorder.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "shader/graphics_shader_module_artifact.h"
#include "shader/single_entry_shader_artifact.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv)
{
    using namespace arp;
    using Recorder = render_graph::ConformanceStorageMrtRecorder;
    assert(argc == 5);
    const bool validator = std::strcmp(argv[1], "--null") == 0;
    const bool negative = std::strcmp(argv[1], "--vulkan-original-negative") == 0;
    const bool certified = std::strcmp(argv[1], "--vulkan-original") == 0;
    assert(validator || certified || negative || std::strcmp(argv[1], "--vulkan") == 0);
    constexpr rhi::ConformanceWorkloadBudget budget{16384, 4096, 3};
    const auto source = core::readTextFile(argv[2], budget.maxYamlBytes);
    assert(source.success);
    const char* id = nullptr;
    const auto workload = rhi::ConformanceStorageMrtWorkload::parse(source.text, budget, id);
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
    const shader::SingleEntryShaderLoadBudget computeBudget{
        4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32};
    auto computeQuota = core::makeShared<core::CpuMemoryQuota>(computeBudget.maxCpuMetadataBytes);
    const auto computeArtifact =
        shader::SingleEntryShaderArtifact::load(argv[4], computeBudget, computeQuota, diagnostic);
    if (!computeArtifact) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 1;
    }
    rhi::ShaderPackageDesc computePackage;
    core::requireSuccessfulResult(
        rhi::populateSingleEntryShaderInterface(*computeArtifact, computePackage),
        "CONFORMANCE-COMPUTE-INTERFACE");
    const auto computeDirectory = std::filesystem::path(argv[4]).parent_path();
    for (auto* path : {&computePackage.slangModulePath,
             &computePackage.artifacts.spirvPath,
             &computePackage.artifacts.mslPath,
             &computePackage.artifacts.wgslPath,
             &computePackage.artifacts.reflectionPath})
        *path = (computeDirectory / path->c_str()).generic_string().c_str();
    for (auto& entry : computePackage.entrypoints)
        entry.modulePath = computePackage.slangModulePath;
    auto definition = Recorder::layoutDefinition(package);
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
        window = windows.createWindow({"ConformanceStorageMrtWorkload/1",
            int(first.width),
            int(first.height),
            true,
            true,
            true,
            false});
        if (!window.valid())
            return 77;
        system =
            platform::createDefaultRenderSystem(windows, window, "ConformanceStorageMrtWorkload/1");
        if (!system.valid() || system.backend() != rhi::Backend::Vulkan)
            return 77;
    }
    auto& device = validator ? *nullDevice : *system.gpuDevice();
    if (const auto* unavailable = workload->executionDiagnostic(device.capabilities())) {
        std::fprintf(stderr, "%s\n", unavailable);
        return 1;
    }
    const auto layout = device.createPipelineLayout(definition);
    assert(layout);
    const auto state = Recorder::pipelineDefinition(
        *layout, package, validator ? rhi::TextureFormat::Rgba8Unorm : system.colorFormat());
    const auto validation = device.validateRenderPipeline(state);
    if (!validation.success) {
        std::fprintf(stderr, "%s\n", validation.diagnostics.c_str());
        return 1;
    }
    if (negative) {
        auto domain = core::makeShared<core::CpuMemoryQuota>(65536);
        assert(!device.prepareOriginalDeviceLifetime({1, true, 1, 2, 65536}, domain));
        auto loss = core::makeShared<core::CpuMemoryQuota>(65536);
        assert(!device.prepareOriginalDeviceNativeResultLoss({1, true, 1, 2, 65536}, loss));
        const auto computeLayout =
            device.createPipelineLayout(Recorder::layoutDefinition(computePackage));
        assert(computeLayout);
        auto computeState =
            rhi::ConformanceStorageMrtPipeline::computeDefinition(*computeLayout, computePackage);
        tests::ConformanceStorageMrtNativeNegatives::verify(device,
            *system.swapchain(),
            source.text,
            state,
            computeState,
            artifact,
            computeArtifact);
        std::printf("nativeOriginalStorageMrtNegatives: 30\n");
        return 0;
    }
    auto outputQuota = core::makeShared<core::CpuMemoryQuota>(4 * 1024 * 1024);
    auto domainQuota = core::makeShared<core::CpuMemoryQuota>(65536);
    auto lossQuota = core::makeShared<core::CpuMemoryQuota>(65536);
    core::UniquePtr<rhi::OriginalConformanceStorageMrtExecution> execution;
    if (certified) {
        assert(!device.prepareOriginalDeviceLifetime({1, true, 1, 2, 65536}, domainQuota));
        assert(!device.prepareOriginalDeviceNativeResultLoss({1, true, 1, 2, 65536}, lossQuota));
        auto original = rhi::ConformanceStorageMrtWorkload::parse(source.text, budget, id);
        execution = device.createConformanceStorageMrtExecution(std::move(original),
            {{{1, 3, true, 4096 * 16 * 3, outputQuota->limitBytes()}, 1024 * 1024, 5000000000},
                1024 * 1024,
                1024 * 1024},
            outputQuota,
            id);
        if (id)
            std::fprintf(stderr, "[storage-issuer] %s\n", id);
        assert(execution && !id);
    }
    auto pipeline = execution ? execution->preparePipeline(state, artifact, id)
                              : core::shareUnique(device.createRenderPipeline(state));
    if (id)
        std::fprintf(stderr, "[storage-pipeline] %s\n", id);
    assert(pipeline);
    assert(pipeline->shaderReflection().sourceSha256 == package.reflection.sourceSha256);
    assert(pipeline->shaderReflection().reflectionContractVersion
        == package.reflection.reflectionContractVersion);
    const auto computeDefinition = Recorder::layoutDefinition(computePackage);
    const auto computeLayout = device.createPipelineLayout(computeDefinition);
    assert(computeLayout && computePackage.entrypoints.size() == 1);
    rhi::ComputePipelineDesc computeState;
    computeState.label = "Conformance.StorageFill";
    computeState.layout = computeLayout.get();
    computeState.shaderPackage = &computePackage;
    computeState.computeShader = computePackage.entrypoints[0];
    auto compute = execution ? execution->prepareComputePipeline(computeState, computeArtifact, id)
                             : core::shareUnique(device.createComputePipeline(computeState));
    if (id)
        std::fprintf(stderr, "[storage-compute] %s\n", id);
    assert(compute);
    render_graph::ConformanceRecorderDesc config{1, budget.maxFrames, true, 1024 * 1024};
    config.quota = core::makeShared<core::CpuMemoryQuota>(config.maxCpuMetadataBytes);
    auto invalid = config;
    invalid.prototypeOptIn = false;
    assert(!Recorder::prepare(device, pipeline, compute, *workload, invalid, diagnostic));
    invalid = config;
    invalid.maxPreparedFrames = 2;
    assert(!Recorder::prepare(device, pipeline, compute, *workload, invalid, diagnostic));
    invalid = config;
    invalid.maxCpuMetadataBytes = 1;
    invalid.quota = core::makeShared<core::CpuMemoryQuota>(1);
    assert(!Recorder::prepare(device, pipeline, compute, *workload, invalid, diagnostic));
    auto changedSource = source.text;
    const auto hashOffset =
        changedSource.find("shaderSourceSha256: ") + std::strlen("shaderSourceSha256: ");
    changedSource[hashOffset] = changedSource[hashOffset] == '0' ? '1' : '0';
    const auto changed = rhi::ConformanceStorageMrtWorkload::parse(changedSource, budget, id);
    assert(changed && !Recorder::prepare(device, pipeline, compute, *changed, config, diagnostic));
    assert(diagnostic == "RHI-CONFORMANCE-STORAGE-MRT-SHADER-IDENTITY");
    auto aliasSource = source.text;
    const auto aliasOffset = aliasSource.find("requireTransientAliasing: false");
    aliasSource.replace(aliasOffset,
        std::strlen("requireTransientAliasing: false"),
        "requireTransientAliasing: true");
    const auto alias = rhi::ConformanceStorageMrtWorkload::parse(aliasSource, budget, id);
    assert(alias && !Recorder::prepare(device, pipeline, compute, *alias, config, diagnostic));
    assert(diagnostic == "RHI-CONFORMANCE-STORAGE-MRT-ALIAS-CAPABILITY");
    assert(config.quota->liveBytes() == 0);
    auto recorder = Recorder::prepare(
        device, pipeline, compute, *workload, config, diagnostic, execution.get());
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
        const auto& compiled = graph.compile();
        assert(compiled.executablePassCount() == 2 && compiled.aliasPlan().empty());
        assert(std::any_of(
            compiled.barrierPlan().begin(), compiled.barrierPlan().end(), [](const auto& barrier) {
                return barrier.resourceName == "Conformance.StorageOutput"
                    && barrier.before == rhi::ResourceState::ShaderWrite
                    && barrier.after == rhi::ResourceState::ShaderRead
                    && barrier.srcStage == rhi::PipelineStage::ComputeShader
                    && barrier.dstStage == rhi::PipelineStage::FragmentShader
                    && barrier.srcAccess == rhi::ResourceAccess::ShaderWrite
                    && barrier.dstAccess == rhi::ResourceAccess::ShaderRead;
            }));
        try {
            if (execution)
                graph.executeConformance(*execution, target);
            else
                graph.execute();
        } catch (const core::DiagnosticError& error) {
            std::fprintf(stderr, "[storage-execution] %s\n", error.what());
            return 1;
        }
        assert(surface.present(acquired).success);
        if (execution) {
            const auto* failure = execution->finalizePresented();
            if (failure)
                std::fprintf(stderr, "[storage-present] %s\n", failure);
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
            std::fprintf(stderr, "[storage-audit] %s\n", report.diagnostic);
        assert(report.originalOutputValidated && report.frames == 3
            && report.actualOutputBytes == 155648);
        assert(!report.platformValidated && !report.productionReady
            && !report.backendReadinessPromoted);
        assert(execution->run()->outputCount() == 3);
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t role = 0; role < 3; ++role) {
                const auto bytes =
                    execution->run()->pixels(i, static_cast<rhi::ConformanceOutputRole>(role));
                const auto hash = core::Sha256::hexDigest(
                    {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
                std::printf("actualFrame%zuOutput%zuSha256: %s\n", i, role, hash.c_str());
            }
        std::printf("sealedOriginalOutputValidated: true\nactualOutputBytes: %zu\n",
            report.actualOutputBytes);
        for (auto quota : {outputQuota, artifactQuota, computeQuota}) {
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1);
            assert(
                !rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
            quota->setLimitBytes(limit);
            assert(
                rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
        }
        device.retireOriginalDeviceLifetime();
        assert(!rhi::OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
    }
    // Null's registry may retain cold container capacity until its owner retires.
    // This is validator bookkeeping, never native completion evidence.
    if (validator)
        nullDevice.reset();
    assert(config.quota->liveBytes() == 0);
    std::printf(
        "type: ConformanceStorageMrtExecution\ncontractVersion: 1\nworkloadSha256: %.*s\nframes: 3\nvalidatorOnly: %s\noutputEvidenceClaimed: false\nreadinessPromoted: false\n",
        int(workload->sourceSha256().size()),
        workload->sourceSha256().data(),
        validator ? "true" : "false");
}
