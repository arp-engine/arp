#include "../fixtures/conformance_sampled_native_negatives.h"
#include "../fixtures/conformance_storage_mrt_native_negatives.h"
#include "../fixtures/webgpu_original_device_fixture.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/math/unsigned_range.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/conformance_sampled_recorder.h"
#include "renderer/render_graph/conformance_storage_mrt_recorder.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/conformance/matrix.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "rhi/frontend/command_encoder_base.h"
#include "shader/graphics_shader_module_artifact.h"
#include "shader/single_entry_shader_artifact.h"

#include <cassert>
#include <cstdio>
#include <emscripten/emscripten.h>

EM_ASYNC_JS(void, nextCanvasFrame, (), {
    // clang-format off
    await new Promise(resolve => requestAnimationFrame(resolve));
    // clang-format on
});

EM_ASYNC_JS(int, awaitCapture, (), {
    // clang-format off
    if (new URLSearchParams(location.search).get('captureGate') !== '1') return 0;
    await fetch('/ready',{method:'POST'});
    while (!(await fetch('/armed')).ok)
        await new Promise(resolve => requestAnimationFrame(resolve));
    return 1;
    // clang-format on
});

EM_JS(void, report, (int success, const char* diagnostic), {
    // clang-format off
    Module.sharedResult = {success:!!success,diagnostic:UTF8ToString(diagnostic),
        nativeErrors:Module.webGpuUncapturedErrors||[],messages:Module.sharedMessages||[],
        shaderTrace:Module.sharedShaderTrace||null,
        productionReady:false,readinessPromoted:false,nativeSyncPromoted:false};
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},
        body:JSON.stringify(Module.sharedResult)});
    // clang-format on
});

namespace arp::test {
class WebGpuSharedClearNegatives final {
    class Observer final : public render_graph::PassSubmissionObserver {
    public:
        void onSubmitted(render_graph::PassSubmissionToken,
            const std::shared_ptr<const rhi::GpuFence>&) noexcept override
        {
            called = true;
        }

        bool called = false;
    };

public:
    static void verify(rhi::GpuDevice& device,
        core::StringView source,
        bool physicalLoss = false,
        bool captureWarmup = false)
    {
        constexpr std::uint64_t metadata = 1024 * 1024;
        const rhi::ConformanceClearExecutionRequest request{
            {1, 3, true, 4096 * 4 * 3, metadata}, metadata, 5000000000};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadata);
        const char* id = nullptr;
        const auto create = [&](const rhi::ConformanceClearExecutionRequest& intent) {
            auto original = rhi::ConformanceClearWorkload::parse(source, {8192, 4096, 3}, id);
            assert(original && !id);
            return device.createConformanceClearExecution(std::move(original), intent, quota, id);
        };
        for (int mode = 0; !physicalLoss && !captureWarmup && mode < 4; ++mode) {
            auto invalid = request;
            if (mode == 0)
                invalid.observation.prototypeOptIn = false;
            if (mode == 1)
                invalid.observation.maxFrames = 2;
            if (mode == 2)
                invalid.observation.maxOutputBytes = 38911;
            if (mode == 3)
                invalid.maxNativeReadbackBytes = 1;
            assert(!create(invalid) && id);
            assert(quota->liveBytes() == 0);
        }
        for (int mode = physicalLoss ? 9
                : captureWarmup      ? 6
                                     : 0;
            mode < (captureWarmup ? 7 : 10);
            ++mode) {
            auto intent = request;
            if (mode == 9)
                intent.completionTimeoutNanoseconds = 1;
            auto execution = create(intent);
            assert(execution && !id);
            rhi::SwapchainDesc desc;
            desc.surfaceName = "#canvas";
            desc.extent = {64, 64};
            desc.format = rhi::TextureFormat::Bgra8Unorm;
            desc.imageCount = 2;
            auto surface = device.createSwapchain(desc);
            assert(surface);
            const auto acquired = surface->acquireNextFrame();
            auto image = surface->acquiredImage(acquired);
            if (!image.success)
                throw core::DiagnosticError(image.diagnostics);
            auto encoder = execution->prepare({}, *image.texture, id);
            assert(encoder && !id);
            if (mode == 0) {
                assert(!surface->present(acquired).success);
                assert(execution->finalizePresented());
            } else if (mode == 1) {
                assert(!encoder->finish());
            } else if (mode == 2) {
                assert(!encoder->beginComputePass({}));
            } else if (mode == 3) {
                auto barrier = rhi::makeInferredResourceBarrier(image.ref.image,
                    rhi::TextureSubresourceRange{},
                    rhi::ResourceState::Undefined,
                    rhi::ResourceState::ColorAttachment);
                barrier.destinationAccess = rhi::ResourceAccess::None;
                assert(!encoder->transition(*image.texture, barrier).success);
            } else if (mode == 4) {
                assert(!execution->prepare({}, *image.texture, id) && id);
            } else {
                assert(encoder
                        ->transition(*image.texture,
                            rhi::ResourceState::Undefined,
                            rhi::ResourceState::ColorAttachment)
                        .success);
                rhi::RenderPassDesc pass;
                rhi::RenderPassColorAttachmentDesc color;
                color.target = image.ref.image;
                color.format = desc.format;
                color.loadOp = rhi::LoadOp::Clear;
                color.storeOp = rhi::StoreOp::Store;
                color.clearColor = {32.f / 255.f, 128.f / 255.f, 191.f / 255.f, 1.f};
                pass.colorAttachments.push_back(color);
                if (mode == 5) {
                    pass.colorAttachments[0].clearColor.r = 1;
                    assert(!encoder->beginRenderPass(pass));
                } else {
                    auto render = encoder->beginRenderPass(pass);
                    assert(render);
                    assert(render->end().success);
                    assert(encoder
                            ->transition(*image.texture,
                                rhi::ResourceState::ColorAttachment,
                                rhi::ResourceState::Present)
                            .success);
                    auto command = encoder->finish();
                    assert(command);
                    if (mode == 6) {
                        const auto ordinary = device.queue().submit(*command);
                        assert(!ordinary.success && !ordinary.hasAcceptedWork());
                        const auto submitted = execution->submit(*command, nullptr);
                        assert(submitted.success && submitted.hasAcceptedWork());
                        auto wrongFrame = acquired;
                        ++wrongFrame.frameSerial;
                        assert(!surface->present(wrongFrame).success);
                        assert(surface->present(acquired).success);
                        assert(!execution->finalizePresented());
                    } else if (mode == 7) {
                        quota->setLimitBytes(metadata - 1);
                        const auto result = execution->submit(*command, nullptr);
                        assert(!result.success && !result.hasAcceptedWork());
                        quota->setLimitBytes(metadata);
                    } else if (mode == 8) {
                        auto foreign = create(request);
                        const auto result = foreign->submit(*command, nullptr);
                        assert(!result.success && !result.hasAcceptedWork());
                    } else {
                        const auto result = execution->submit(*command, nullptr);
                        assert(!result.success && result.hasAcceptedWork());
                        assert(result.diagnostics == "RHI-CONFORMANCE-CLEAR-NATIVE-DEADLINE");
                        assert(!surface->discardFrame(acquired).success);
                        execution.reset();
                        command.reset();
                        encoder.reset();
                        assert(quota->liveBytes() > 0); // accepted work retains exact native owners
                        if (physicalLoss)
                            EM_ASM({
                                // clang-format off
                                Module.preinitializedWebGPUDevice.destroy();
                                // clang-format on
                            });
                    }
                }
            }
            assert(!rhi::RhiConformanceRunner::auditOriginal(execution ? execution->run() : nullptr)
                    .originalOutputValidated);
            execution.reset();
            encoder.reset();
            bool discarded = !surface->isFrameAcquired(acquired);
            for (int attempt = 0; attempt < 120; ++attempt) {
                if (!discarded)
                    discarded = surface->discardFrame(acquired).success;
                if (discarded)
                    image.texture.reset();
                if (discarded && !quota->liveBytes())
                    break;
                nextCanvasFrame();
            }
            if (quota->liveBytes())
                std::printf("negativeMode%d retainedBytes=%llu discarded=%d\n",
                    mode,
                    static_cast<unsigned long long>(quota->liveBytes()),
                    discarded);
            assert(discarded && quota->liveBytes() == 0);
            surface.reset();
            nextCanvasFrame();
        }
        if (!physicalLoss && !captureWarmup) {
            auto execution = create(request);
            assert(execution && !id);
            assert(execution->submissionCompletion()
                == rhi::ConformanceSubmissionCompletion::IssuerOwnedColdObservation);
            rhi::SwapchainDesc desc;
            desc.surfaceName = "#canvas";
            desc.extent = {64, 64};
            desc.format = rhi::TextureFormat::Bgra8Unorm;
            desc.imageCount = 2;
            auto surface = device.createSwapchain(desc);
            assert(surface);
            const auto acquired = surface->acquireNextFrame();
            auto image = surface->acquiredImage(acquired);
            assert(image.success);
            Observer observer;
            {
                render_graph::FrameGraph graph(device);
                const auto target =
                    graph.importSwapchainTexture(std::move(image.texture), image.ref);
                graph.addPass("ColdCompletionObserverRefusal")
                    .writeAttachment(target,
                        rhi::ResourceState::ColorAttachment,
                        rhi::LoadOp::Clear,
                        rhi::StoreOp::Store,
                        {},
                        {32.f / 255.f, 128.f / 255.f, 191.f / 255.f, 1.f})
                    .onSubmitted(observer)
                    .executeRenderPass([](render_graph::RenderPassContext&) {});
                bool rejected = false;
                try {
                    graph.executeConformance(*execution, target);
                } catch (const core::DiagnosticError& error) {
                    rejected = core::StringView(error.what())
                        == "RG-CONFORMANCE-COLD-COMPLETION-OBSERVER-UNSUPPORTED";
                }
                assert(rejected && !observer.called && !graph.lastExecutionSubmitted());
                assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run())
                        .originalOutputValidated);
            }
            execution.reset();
            assert(surface->discardFrame(acquired).success);
            surface.reset();
            for (int attempt = 0; quota->liveBytes() && attempt < 120; ++attempt)
                nextCanvasFrame();
            assert(!quota->liveBytes());
            nextCanvasFrame();
        }
        if (physicalLoss) {
            assert(device.originalDeviceNativeLossDiagnostic());
            std::printf(
                "WebGpuSharedConformance/1 actualDeviceLoss acceptedDeadlineRetained callbacksDrained quotaBaseline noOutputAudit\n");
        } else if (!captureWarmup) {
            std::printf(
                "WebGpuSharedConformance/1 nativeNegatives15 callbacksDrained quotaBaseline acceptedDeadlineRetained ordinaryQueueClosed coldObserverRejected\n");
        }
    }
};
} // namespace arp::test

namespace {
void runStorage(arp::rhi::GpuDevice& device, bool capturing, bool physicalLoss = false)
{
    using namespace arp;
    using Recorder = render_graph::ConformanceStorageMrtRecorder;
    constexpr std::uint64_t metadata = 4 * 1024 * 1024;
    const auto source = core::readTextFile("/conformance-storage.yaml", 16384);
    assert(source.success);
    const char* id = nullptr;
    auto workload = rhi::ConformanceStorageMrtWorkload::parse(source.text, {16384, 4096, 3}, id);
    assert(workload && !id);
    const auto* authored = workload.get();
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    const shader::GraphicsShaderModuleLoadBudget load{
        4 * metadata, 8 * metadata, 8 * metadata, 32, 32, 4096, 32};
    const shader::SingleEntryShaderLoadBudget computeLoad{
        4 * metadata, 8 * metadata, 8 * metadata, 32};
    auto graphicsQuota = core::makeShared<core::CpuMemoryQuota>(load.maxCpuMetadataBytes);
    auto computeQuota = core::makeShared<core::CpuMemoryQuota>(computeLoad.maxCpuMetadataBytes);
    auto graphics = shader::GraphicsShaderModuleArtifact::load(
        "/shared-storage/conformance_storage_mrt.graphics.module.yaml",
        load,
        graphicsQuota,
        diagnostic);
    if (!graphics)
        throw core::DiagnosticError(diagnostic.c_str());
    auto compute = shader::SingleEntryShaderArtifact::load(
        "/shared-storage/Compute/conformance_storage_fill.computeMain.entry.yaml",
        computeLoad,
        computeQuota,
        diagnostic);
    if (!compute)
        throw core::DiagnosticError(diagnostic.c_str());
    rhi::ShaderPackageDesc graphicsPackage, computePackage;
    core::requireSuccessfulResult(
        rhi::populateGraphicsModuleShaderInterface(*graphics, graphicsPackage),
        "STORAGE-GRAPHICS-INTERFACE");
    core::requireSuccessfulResult(rhi::populateSingleEntryShaderInterface(*compute, computePackage),
        "STORAGE-COMPUTE-INTERFACE");
    auto layout = device.createPipelineLayout(Recorder::layoutDefinition(graphicsPackage));
    auto computeLayout = device.createPipelineLayout(Recorder::layoutDefinition(computePackage));
    assert(layout && computeLayout);
    auto state =
        Recorder::pipelineDefinition(*layout, graphicsPackage, rhi::TextureFormat::Bgra8Unorm);
    auto computeState =
        rhi::ConformanceStorageMrtPipeline::computeDefinition(*computeLayout, computePackage);
    const rhi::ConformanceStorageMrtExecutionRequest request{
        {{1, 3, true, 196608, metadata}, 1048576, 5000000000},
        1048576,
        1048576,
        {1, 65536, 65536, 65536, 65536, 65536, 65536, 512, 5000000000}};
    if (!capturing) {
        rhi::SwapchainDesc negativeSurface;
        negativeSurface.surfaceName = "#canvas";
        negativeSurface.extent = authored->frames()[0].extent;
        negativeSurface.format = rhi::TextureFormat::Bgra8Unorm;
        negativeSurface.imageCount = 2;
        auto surface = device.createSwapchain(negativeSurface);
        assert(surface);
        EM_ASM({
            // clang-format off
            const queue=Module.preinitializedWebGPUDevice.queue;
            Module.storageNativeSubmit=queue.submit;
            Module.storageNativeSubmits=0;
            queue.submit=function(commands){++Module.storageNativeSubmits;return Module.storageNativeSubmit.call(this,commands);};
            // clang-format on
        });
        tests::ConformanceStorageMrtNativeNegatives::verify(
            device,
            *surface,
            source.text,
            state,
            computeState,
            graphics,
            compute,
            request.artifacts,
            &nextCanvasFrame,
            +[]() {
                EM_ASM({
                    // clang-format off
                    const queue=Module.preinitializedWebGPUDevice.queue;
                    const completed=queue.onSubmittedWorkDone;
                    const gate=new Promise(resolve=>Module.releaseStorageCompletion=resolve);
                    Module.storageNativeCompleted=completed;
                    queue.onSubmittedWorkDone=function(){return completed.call(this).then(
                        value=>gate.then(()=>value),error=>gate.then(()=>{throw error;}));};
                    // clang-format on
                });
            },
            +[]() {
                EM_ASM({
                    // clang-format off
                    if(new URLSearchParams(location.search).get('nativeLoss')==='1')Module.preinitializedWebGPUDevice.destroy();
                    Module.releaseStorageCompletion();
                    Module.preinitializedWebGPUDevice.queue.onSubmittedWorkDone=Module.storageNativeCompleted;
                    // clang-format on
                });
            },
            physicalLoss);
        const auto submitted = EM_ASM_INT({
            // clang-format off
            Module.preinitializedWebGPUDevice.queue.submit=Module.storageNativeSubmit;
            return Module.storageNativeSubmits;
            // clang-format on
        });
        assert(submitted == (physicalLoss ? 1 : 2));
        if (physicalLoss) {
            assert(device.originalDeviceNativeLossDiagnostic());
            std::printf(
                "WebGpuSharedConformance/1 storageActualDeviceLoss acceptedDeadlineRetained callbacksDrained quotaBaseline noOutputAudit\n");
            return;
        }
        std::printf(
            "WebGpuSharedConformance/1 storageNativeNegatives30 originalGraphicsComputeInputs dispatch MRT threeOutputs callbackDrain acceptedDeadline\n");
    }
    auto outputQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    auto execution =
        device.createConformanceStorageMrtExecution(std::move(workload), request, outputQuota, id);
    if (!execution)
        throw core::DiagnosticError(id);
    EM_ASM(
        {
            // clang-format off
        window.arpBeginSharedShaderTrace(2,2,6,Number($0));
            // clang-format on
        },
        request.artifacts.targetBytes);
    auto pipeline = execution->preparePipeline(state, graphics, id);
    if (!pipeline)
        throw core::DiagnosticError(id);
    auto computePipeline = execution->prepareComputePipeline(computeState, compute, id);
    if (!computePipeline)
        throw core::DiagnosticError(id);
    auto recorderQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    auto recorder = Recorder::prepare(device,
        pipeline,
        computePipeline,
        *authored,
        {1, 3, true, metadata, recorderQuota},
        diagnostic,
        execution.get());
    if (!recorder)
        throw core::DiagnosticError(diagnostic.c_str());
    rhi::SwapchainDesc swap;
    swap.surfaceName = "#canvas";
    swap.extent = authored->frames()[0].extent;
    swap.format = rhi::TextureFormat::Bgra8Unorm;
    swap.imageCount = 2;
    auto surface = device.createSwapchain(swap);
    assert(surface);
    if (capturing)
        assert(awaitCapture());
    std::size_t index = 0;
    for (const auto& frame : authored->frames()) {
        nextCanvasFrame();
        assert(surface->resize(frame.extent).success);
        const auto acquired = surface->acquireNextFrame();
        auto image = surface->acquiredImage(acquired);
        assert(image.success && image.texture);
        render_graph::FrameGraph graph(device);
        const auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
        recorder->record(index, graph, target);
        graph.executeConformance(*execution, target);
        assert(surface->present(acquired).success);
        if (const auto* error = execution->finalizePresented())
            throw core::DiagnosticError(error);
        for (const auto role : {rhi::ConformanceOutputRole::Canvas,
                 rhi::ConformanceOutputRole::Color1,
                 rhi::ConformanceOutputRole::Storage}) {
            const auto pixels = execution->run()->pixels(index, role);
            const auto hash = core::Sha256::hexDigest(
                {reinterpret_cast<const char*>(pixels.data()), pixels.size()});
            std::printf("sharedStorageFrame%zuRole%u=%ux%u bytes=%zu hash=%s\n",
                index,
                unsigned(role),
                frame.extent.width,
                frame.extent.height,
                pixels.size(),
                hash.c_str());
        }
        ++index;
        if (capturing)
            for (int f = 0; f < 4; ++f)
                nextCanvasFrame();
    }
    const auto audit = rhi::RhiConformanceRunner::auditOriginal(execution->run());
    assert(audit.originalOutputValidated && audit.frames == 3 && audit.actualOutputBytes == 155648);
    graphicsQuota->setLimitBytes(load.maxCpuMetadataBytes - 1);
    assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
    graphicsQuota->setLimitBytes(load.maxCpuMetadataBytes);
    computeQuota->setLimitBytes(computeLoad.maxCpuMetadataBytes - 1);
    assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
    computeQuota->setLimitBytes(computeLoad.maxCpuMetadataBytes);
    assert(rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
    std::printf(
        "WebGpuSharedConformance/1 StorageMrt frames3 dispatch3 draw3 outputs9 actualBytes155648 originalCanvasPresent3 sealedAudit graphicsComputeArtifactRetirement\n");
}

void runSampled(
    arp::rhi::GpuDevice& device, bool pingpong, bool capturing, bool physicalLoss = false)
{
    using namespace arp;
    constexpr std::uint64_t metadata = 4 * 1024 * 1024;
    const auto source = core::readTextFile(
        pingpong ? "/conformance-pingpong.yaml" : "/conformance-sampled.yaml", 16384);
    assert(source.success);
    const char* id = nullptr;
    auto workload = rhi::ConformanceSampledWorkload::parse(source.text, {16384, 4096, 3, 4}, id);
    assert(workload && !id);
    const auto* authored = workload.get();
    const shader::GraphicsShaderModuleLoadBudget load{
        4 * metadata, 8 * metadata, 8 * metadata, 32, 32, 4096, 32};
    auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(load.maxCpuMetadataBytes);
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    auto artifact = shader::GraphicsShaderModuleArtifact::load(
        "/shared-graphics/webgl1_graphics.graphics.module.yaml", load, artifactQuota, diagnostic);
    if (!artifact)
        throw core::DiagnosticError(diagnostic.c_str());
    rhi::ShaderPackageDesc package;
    core::requireSuccessfulResult(
        rhi::populateGraphicsModuleShaderInterface(*artifact, package), "SAMPLED-INTERFACE");
    auto definition = rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection);
    if (!definition.pushConstantRanges.empty())
        definition.pushConstantRanges[0].stages =
            *rhi::parseShaderReflectionPushConstantStages(package.reflection.pushConstantStages);
    auto layout = device.createPipelineLayout(definition);
    assert(layout);
    auto state = render_graph::ConformanceSampledRecorder::pipelineDefinition(
        *layout, package, rhi::TextureFormat::Bgra8Unorm);
    auto outputQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    rhi::ConformanceSampledExecutionRequest request{
        {{1, 3, true, 49152, metadata}, 1048576, 5000000000},
        1048576,
        {1, 65536, 65536, 65536, 65536, 65536, 65536, 512, 5000000000}};
    if (!pingpong && !capturing) {
        EM_ASM(
            {
                // clang-format off
            const queue = Module.preinitializedWebGPUDevice.queue;
            const submit = queue.submit;
            Module.sampledNegativeNativeSubmits = 0;
            queue.submit = function(commands) {
                ++Module.sampledNegativeNativeSubmits;
                return submit.call(this, commands);
            };
            // Delay delivery of the genuine native promise, never synthesize
            // queue completion. The timeout case releases this bounded gate.
            const completed = queue.onSubmittedWorkDone;
            const gate = new Promise(resolve => Module.releaseSampledNegativeCompletion = resolve);
            queue.onSubmittedWorkDone = function() {
                return completed.call(this).then(value => gate.then(() => value),
                    error => gate.then(() => {throw error;}));
            };
            Module.sampledNegativePhysicalLoss = !!$0;
                // clang-format on
            },
            physicalLoss);
        rhi::SwapchainDesc negativeSurface;
        negativeSurface.surfaceName = "#canvas";
        negativeSurface.extent = authored->frames()[0].extent;
        negativeSurface.format = rhi::TextureFormat::Bgra8Unorm;
        negativeSurface.imageCount = 2;
        auto surface = device.createSwapchain(negativeSurface);
        assert(surface);
        tests::ConformanceSampledNativeNegatives::verify(
            device,
            *surface,
            source.text,
            state,
            artifact,
            request.artifacts,
            &nextCanvasFrame,
            +[]() {
                EM_ASM({
                    // clang-format off
                    if (Module.sampledNegativePhysicalLoss) Module.preinitializedWebGPUDevice.destroy();
                    Module.releaseSampledNegativeCompletion();
                    // clang-format on
                });
            },
            physicalLoss);
        assert(EM_ASM_INT({
            // clang-format off
            return Module.sampledNegativeNativeSubmits;
            // clang-format on
        }) == 1);
        if (physicalLoss) {
            assert(device.originalDeviceNativeLossDiagnostic());
            std::printf(
                "WebGpuSharedConformance/1 sampledActualDeviceLoss acceptedDeadlineRetained callbacksDrained quotaBaseline noOutputAudit\n");
            return;
        }
        std::printf(
            "WebGpuSharedConformance/1 sampledNativeNegatives24 originalInputs pipeline descriptors push viewport index draw scopes callbackDrain acceptedDeadline\n");
    }
    auto execution =
        device.createConformanceSampledExecution(std::move(workload), request, outputQuota, id);
    if (!execution)
        throw core::DiagnosticError(id);
    EM_ASM(
        {
            // clang-format off
        window.arpBeginSharedShaderTrace(Number($0),Number($0),Number($1),Number($2));
            // clang-format on
        },
        pingpong ? 2 : 1,
        pingpong ? 12 : 3,
        request.artifacts.targetBytes);
    auto pipeline = execution->preparePipeline(state, artifact, id);
    if (!pipeline)
        throw core::DiagnosticError(id);
    auto rgba = render_graph::ConformanceSampledRecorder::pipelineDefinition(
        *layout, package, rhi::TextureFormat::Rgba8Unorm);
    auto intermediate = pingpong ? execution->preparePipeline(rgba, artifact, id) : nullptr;
    if (pingpong && !intermediate)
        throw core::DiagnosticError(id);
    auto recorderQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    auto recorder = render_graph::ConformanceSampledRecorder::prepare(device,
        pipeline,
        *authored,
        {1, 3, true, metadata, recorderQuota},
        diagnostic,
        execution.get(),
        intermediate);
    if (!recorder)
        throw core::DiagnosticError(diagnostic.c_str());
    rhi::SwapchainDesc swap;
    swap.surfaceName = "#canvas";
    swap.extent = authored->frames()[0].extent;
    swap.format = rhi::TextureFormat::Bgra8Unorm;
    swap.imageCount = 2;
    auto surface = device.createSwapchain(swap);
    assert(surface);
    if (capturing)
        assert(awaitCapture());
    std::size_t index = 0;
    for (const auto& frame : authored->frames()) {
        nextCanvasFrame();
        assert(surface->resize(frame.extent).success);
        const auto acquired = surface->acquireNextFrame();
        auto image = surface->acquiredImage(acquired);
        assert(image.success && image.texture);
        render_graph::FrameGraph graph(device);
        const auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
        recorder->record(index, graph, target);
        graph.executeConformance(*execution, target);
        assert(surface->present(acquired).success);
        if (const auto* error = execution->finalizePresented())
            throw core::DiagnosticError(error);
        const auto pixels = execution->run()->pixels(index);
        const auto hash =
            core::Sha256::hexDigest({reinterpret_cast<const char*>(pixels.data()), pixels.size()});
        std::printf("shared%sFrame%zu=%ux%u bytes=%zu hash=%s\n",
            pingpong ? "PingPong" : "Sampled",
            index,
            frame.extent.width,
            frame.extent.height,
            pixels.size(),
            hash.c_str());
        ++index;
        if (capturing)
            for (int f = 0; f < 4; ++f)
                nextCanvasFrame();
    }
    const auto audit = rhi::RhiConformanceRunner::auditOriginal(execution->run());
    assert(audit.originalOutputValidated && audit.frames == 3 && audit.actualOutputBytes == 38912);
    artifactQuota->setLimitBytes(load.maxCpuMetadataBytes - 1);
    assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
    artifactQuota->setLimitBytes(load.maxCpuMetadataBytes);
    assert(rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
    std::printf(
        "WebGpuSharedConformance/1 %s frames3 actualBytes38912 originalCanvasPresent3 sealedAudit artifactRetirement\n",
        pingpong ? "PingPong" : "Sampled");
}
} // namespace

int main()
{
    using namespace arp;
    try {
        constexpr std::uint64_t metadata = 1024 * 1024;
        constexpr rhi::ConformanceWorkloadBudget budget{8192, 4096, 3};
        const auto source = core::readTextFile("/conformance-clear.yaml", budget.maxYamlBytes);
        assert(source.success);
        const char* id = nullptr;
        auto original = rhi::ConformanceClearWorkload::parse(source.text, budget, id);
        assert(original && !id);
        const auto* workload = original.get();
        auto deviceDesc = test::WebGpuOriginalDeviceFixture::describe("WebGpuSharedConformance/1",
            8 * metadata,
            rhi::kL2AtomicPushConstantLimitBytes,
            metadata);
        const auto pingpongSource = core::readTextFile("/conformance-pingpong.yaml", 16384);
        assert(pingpongSource.success);
        const auto pingpongBudget =
            rhi::ConformanceSampledWorkload::parse(pingpongSource.text, {16384, 4096, 3, 4}, id);
        assert(pingpongBudget && !id);
        std::uint64_t alignedPushBytes = 0, pushBudget = 0;
        assert(
            core::roundUpUnsigned(std::uint64_t(sizeof(pingpongBudget->frames()[0].draws[0].push)),
                std::uint64_t(deviceDesc.runtimeProfile.limits.uniformBufferOffsetAlignment),
                alignedPushBytes));
        for (const auto& frame : pingpongBudget->frames())
            pushBudget = std::max(pushBudget, alignedPushBytes * frame.draws.size());
        assert(pushBudget && pushBudget <= UINT32_MAX);
        deviceDesc.pushConstantDeclaredWorkloadByteBudget = std::uint32_t(pushBudget);
        auto device = rhi_webgpu::createWebGpuRhiDevice(deviceDesc);
        assert(device);
        auto domainQuota = core::makeShared<core::CpuMemoryQuota>(65536);
        auto lossQuota = core::makeShared<core::CpuMemoryQuota>(65536);
        auto outputQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        assert(!device->prepareOriginalDeviceLifetime({1, true, 1, 2, 65536}, domainQuota));
        assert(!device->prepareOriginalDeviceNativeLoss({{1, true, 1, 1, 65536}, 1}, lossQuota));
        const bool physicalLoss = EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).get('nativeLoss') === '1';
            // clang-format on
        });
        const int sampled = EM_ASM_INT({
            // clang-format off
            const p = new URLSearchParams(location.search);
            return p.get('conformanceSampledOnly') === '1' ? Number(p.get('conformanceSampledVersion')||1) : 0;
            // clang-format on
        });
        if (physicalLoss) {
            if (EM_ASM_INT({
                    // clang-format off
                return new URLSearchParams(location.search).get('conformanceStorageOnly') === '1';
                    // clang-format on
                }))
                runStorage(*device, false, true);
            else if (sampled)
                runSampled(*device, false, false, true);
            else
                test::WebGpuSharedClearNegatives::verify(*device, source.text, true);
            report(1, "");
            return 0;
        }
        const bool capturing = EM_ASM_INT({
            // clang-format off
            return new URLSearchParams(location.search).get('captureGate') === '1';
            // clang-format on
        });
        // Run the same real negative paths before the capture gate. Dawn's
        // RenderDoc integration registers its native canvas at first present;
        // registration must precede the armed three-frame authored workload.
        test::WebGpuSharedClearNegatives::verify(*device, source.text, false, capturing);
        if (EM_ASM_INT({
                // clang-format off
            return new URLSearchParams(location.search).get('conformanceStorageOnly') === '1';
                // clang-format on
            })) {
            runStorage(*device, capturing);
            report(1, "");
            return 0;
        }
        if (sampled) {
            runSampled(*device, sampled == 2, capturing);
            report(1, "");
            return 0;
        }
        auto execution = device->createConformanceClearExecution(std::move(original),
            {{1, 3, true, 4096 * 4 * 3, metadata}, 1024 * 1024, 5000000000},
            outputQuota,
            id);
        if (!execution)
            throw core::DiagnosticError(id);
        rhi::SwapchainDesc swap;
        swap.surfaceName = "#canvas";
        swap.extent = workload->frames()[0].extent;
        swap.format = rhi::TextureFormat::Bgra8Unorm;
        swap.imageCount = 2;
        auto surface = device->createSwapchain(swap);
        assert(surface);
        if (capturing)
            assert(awaitCapture());
        std::size_t index = 0;
        for (const auto& frame : workload->frames()) {
            nextCanvasFrame();
            assert(surface->resize(frame.extent).success);
            const auto acquired = surface->acquireNextFrame();
            assert(acquired.success);
            auto image = surface->acquiredImage(acquired);
            assert(image.success && image.texture);
            render_graph::FrameGraph graph(*device);
            const auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
            graph.addPass("ConformanceClearWorkload/1")
                .writeAttachment(target,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store,
                    {},
                    frame.clearColor())
                .executeRenderPass([](render_graph::RenderPassContext&) {});
            graph.executeConformance(*execution, target);
            assert(graph.texture(target).state() == rhi::ResourceState::Present);
            assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run())
                    .originalOutputValidated);
            assert(surface->present(acquired).success);
            if (const auto* error = execution->finalizePresented())
                throw core::DiagnosticError(error);
            const auto pixels = execution->run()->pixels(index);
            const auto hash = core::Sha256::hexDigest(
                {reinterpret_cast<const char*>(pixels.data()), pixels.size()});
            std::printf("sharedClearFrame%zu=%ux%u bytes=%zu hash=%s\n",
                index,
                frame.extent.width,
                frame.extent.height,
                pixels.size(),
                hash.c_str());
            ++index;
            if (capturing)
                for (int retainedFrame = 0; retainedFrame < 4; ++retainedFrame)
                    nextCanvasFrame();
        }
        const auto audit = rhi::RhiConformanceRunner::auditOriginal(execution->run());
        assert(
            audit.originalOutputValidated && audit.frames == 3 && audit.actualOutputBytes == 38912);
        assert(
            !audit.productionReady && !audit.backendReadinessPromoted && !audit.platformValidated);
        outputQuota->setLimitBytes(metadata - 1);
        assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
        outputQuota->setLimitBytes(metadata);
        assert(rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
        device->retireOriginalDeviceLifetime();
        assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
        std::printf(
            "WebGpuSharedConformance/1 ClearResize frames3 actualBytes38912 originalCanvasPresent3 sealedAudit quotaRetirement deviceRetirement\n");
        report(1, "");
    } catch (const std::exception& error) {
        report(0, error.what());
        return 1;
    }
}
