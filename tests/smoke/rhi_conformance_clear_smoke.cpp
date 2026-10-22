#include "../fixtures/conformance_clear_native_negatives.h"
#include "../fixtures/renderdoc_capture.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "platform/render_system.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/conformance/clear_workload.h"
#include "rhi/conformance/matrix.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
void render(arp::rhi::GpuDevice& device,
    arp::rhi::Swapchain& surface,
    const arp::rhi::ConformanceClearFrame& frame,
    arp::rhi::OriginalConformanceClearExecution* execution = nullptr)
{
    using namespace arp;
    const auto acquired = surface.acquireNextFrame();
    assert(acquired.success);
    auto image = surface.acquiredImage(acquired);
    assert(image.success && image.texture);
    const auto& shape = image.texture->desc();
    std::fprintf(stderr,
        "[conformance-extent] requested=%ux%u actual=%ux%u\n",
        frame.extent.width,
        frame.extent.height,
        shape.extent.width,
        shape.extent.height);
    assert(shape.extent.width == frame.extent.width && shape.extent.height == frame.extent.height);
    assert(shape.format == rhi::TextureFormat::Rgba8Unorm
        || shape.format == rhi::TextureFormat::Bgra8Unorm);
    render_graph::FrameGraph graph(device);
    const auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
    graph.addPass("ConformanceClearWorkload/1")
        .writeAttachment(target,
            rhi::ResourceState::ColorAttachment,
            rhi::LoadOp::Clear,
            rhi::StoreOp::Store,
            {},
            frame.clearColor())
        .executeRenderPass([](render_graph::RenderPassContext&) {});
    if (execution)
        graph.executeConformance(*execution, target);
    else
        graph.execute();
    assert(graph.texture(target).state() == rhi::ResourceState::Present);
    if (execution)
        assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
    assert(surface.present(acquired).success);
    if (execution)
        assert(!execution->finalizePresented());
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 3 || argc == 4);
    const bool negatives = argc == 4 && std::strcmp(argv[3], "--negatives") == 0;
    const bool certified = argc == 4 && std::strcmp(argv[3], "--certified") == 0;
    const bool validator = std::strcmp(argv[1], "--null") == 0;
    assert(validator || std::strcmp(argv[1], "--vulkan") == 0);
    constexpr rhi::ConformanceWorkloadBudget budget{8192, 4096, 3};
    const auto source = core::readTextFile(argv[2], budget.maxYamlBytes);
    assert(source.success);
    const char* id = nullptr;
    const auto workload = rhi::ConformanceClearWorkload::parse(source.text, budget, id);
    assert(workload && !id);
    if (validator) {
        auto device = rhi::createNullDevice();
        assert(device);
        for (const auto& frame : workload->frames()) {
            auto surface = device->createSwapchain(
                {"ConformanceClearWorkload/1", frame.extent, rhi::TextureFormat::Rgba8Unorm, 2});
            assert(surface);
            render(*device, *surface, frame);
        }
    } else {
        platform::WindowSystem windows;
        if (!windows.initialize().success)
            return 77;
        const auto first = workload->frames()[0].extent;
        auto window = windows.createWindow({"ConformanceClearWorkload/1",
            int(first.width),
            int(first.height),
            true,
            true,
            true,
            false});
        if (!window.valid())
            return 77;
        auto system =
            platform::createDefaultRenderSystem(windows, window, "ConformanceClearWorkload/1");
        if (!system.valid() || system.backend() != rhi::Backend::Vulkan)
            return 77;
        constexpr std::uint64_t metadataBytes = 1024 * 1024;
        auto domainQuota = core::makeShared<core::CpuMemoryQuota>(65536);
        auto lossQuota = core::makeShared<core::CpuMemoryQuota>(65536);
        auto outputQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        core::UniquePtr<rhi::OriginalConformanceClearExecution> execution;
        if (certified || negatives) {
            auto& device = *system.gpuDevice();
            assert(!device.prepareOriginalDeviceLifetime({1, true, 1, 2, 65536}, domainQuota));
            assert(
                !device.prepareOriginalDeviceNativeResultLoss({1, true, 1, 2, 65536}, lossQuota));
            if (negatives) {
                tests::ConformanceClearNativeNegatives::verify(
                    device, *system.swapchain(), source.text);
                std::printf("nativeOriginalOutputNegatives: 16\nreadinessPromoted: false\n");
                return 0;
            }
            auto original = rhi::ConformanceClearWorkload::parse(source.text, budget, id);
            execution = device.createConformanceClearExecution(std::move(original),
                {{1, 3, true, 4096 * 4 * 3, metadataBytes}, 1024 * 1024, 5000000000},
                outputQuota,
                id);
            if (id)
                std::fprintf(stderr, "[conformance-issuer] %s\n", id);
            assert(execution && !id);
            assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run())
                    .originalOutputValidated);
        }
        tests::RenderDocCaptureScope capture;
        for (const auto& frame : workload->frames()) {
            window.resize(int(frame.extent.width), int(frame.extent.height));
            const auto resized = system.resize(frame.extent);
            assert(resized.success);
            render(*system.gpuDevice(), *system.swapchain(), frame, execution.get());
            const auto native =
                rhi_vulkan::inspectVulkanRhiNativeResourceStats(*system.gpuDevice());
            assert(native.nativeSwapchainPresentCount > 0u);
            assert(native.nativeRenderPassBeginRecordCount > 0u);
            assert(!native.hiddenWindowRenderOutputHashCaptured);
            assert(native.deterministicRenderOutputHash == 0u);
            assert(native.renderOutputHashCaptureCount == 0u);
            assert(!native.renderOutputHashIncludesNativePresent);
            assert(std::strcmp(native.renderOutputEvidenceDiagnostic,
                       rhi::semantics::RenderOutputObservationContract::unavailableDiagnostic)
                == 0);
            rhi::semantics::ShutdownDrainRequest drain;
            drain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            assert(system.drainGpuForShutdown(drain).completed());
        }

        if (execution) {
            const auto report = rhi::RhiConformanceRunner::auditOriginal(execution->run());
            if (report.diagnostic)
                std::fprintf(stderr, "[conformance-audit] %s\n", report.diagnostic);
            assert(report.originalOutputValidated && report.frames == workload->frames().size());
            assert(report.actualOutputBytes == 38912 && !report.backendReadinessPromoted
                && !report.platformValidated && !report.productionReady);
            for (std::size_t i = 0; i < report.frames; ++i) {
                const auto pixels = execution->run()->pixels(i);
                const auto hash = core::Sha256::hexDigest(
                    {reinterpret_cast<const char*>(pixels.data()), pixels.size()});
                std::printf("actualFrame%zuSha256: %s\n", i, hash.c_str());
            }
            std::printf("sealedOriginalOutputValidated: true\nactualOutputBytes: %zu\n",
                report.actualOutputBytes);
            outputQuota->setLimitBytes(metadataBytes - 1);
            assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run())
                    .originalOutputValidated);
            outputQuota->setLimitBytes(metadataBytes);
            assert(
                rhi::RhiConformanceRunner::auditOriginal(execution->run()).originalOutputValidated);
            system.gpuDevice()->retireOriginalDeviceLifetime();
            assert(!rhi::RhiConformanceRunner::auditOriginal(execution->run())
                    .originalOutputValidated);
        }
    }
    std::printf(
        "type: ConformanceClearExecution\ncontractVersion: 1\nworkloadSha256: %.*s\nframes: %zu\nvalidatorOnly: %s\noutputEvidenceClaimed: false\nreadinessPromoted: false\n",
        int(workload->sourceSha256().size()),
        workload->sourceSha256().data(),
        workload->frames().size(),
        validator ? "true" : "false");
}
