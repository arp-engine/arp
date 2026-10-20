#include "../fixtures/frame_input_lifecycle_checks.h"
#include "core/runtime/render_thread.h"
#include "platform/render_system.h"
#include "platform/window.h"
#include "renderer/pipeline/render_pipeline.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_native_resources.h"

#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace {
using namespace arp;

class PendingPipeline final : public renderer::RenderPipeline {
public:
    explicit PendingPipeline(std::shared_ptr<const rhi::GpuFence> completion)
        : completion_(std::move(completion))
    {}

    const renderer::RenderPipelineDesc& desc() const override { return desc_; }

    renderer::RendererResult create(const renderer::RenderPipelineCreateContext& context) override
    {
        desc_.extent = context.extent;
        return {true, {}};
    }

    renderer::RendererResult resize(rhi::Extent2D extent) override
    {
        desc_.extent = extent;
        return {true, {}};
    }

    renderer::RendererResult render(renderer::RenderPipelineContext&) override
    {
        return {false, "TEST-ADMISSION-ONLY-PIPELINE"};
    }

    bool frameInputsReady(std::uint32_t) const override { return completion_->isSignaled(); }

private:
    renderer::RenderPipelineDesc desc_;
    std::shared_ptr<const rhi::GpuFence> completion_;
};

class PendingConsumer final : public core::runtime::RenderThreadConsumer<int> {
public:
    explicit PendingConsumer(platform::RenderSystem& system) : system_(system) {}

    core::runtime::RenderFrameConsumeStatus consumeFrame(
        const int& packet, std::uint64_t* serial) override
    {
        assert(packet == 17);
        const auto frame = system_.acquireFrame();
        if (frame.status == rhi::AcquireStatus::NotReady) {
            std::lock_guard<std::mutex> lock(mutex);
            if (pendingSerial != 0u)
                assert(frame.frameSerial == pendingSerial);
            pendingSerial = frame.frameSerial;
            ++deferred;
            cv.notify_all();
            return core::runtime::RenderFrameConsumeStatus::Deferred;
        }
        assert(frame.success && frame.frameSerial == pendingSerial);
        assert(system_.presentFrame(frame).success);
        ++presented;
        *serial = static_cast<std::uint64_t>(packet);
        return core::runtime::RenderFrameConsumeStatus::Completed;
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::uint64_t pendingSerial = 0u;
    unsigned deferred = 0u;
    unsigned presented = 0u;

private:
    platform::RenderSystem& system_;
};
} // namespace

int main()
{
    platform::WindowSystem platform;
    assert(platform.initialize().success);
    auto window = platform.createWindow({"frame-input-smoke", 128, 72, true, true, false, false});
    assert(window.valid());
    auto system = platform::createDefaultRenderSystem(platform, window, "frame-input-smoke");
    assert(system.valid());
    auto* device = system.gpuDevice();
    assert(device);
    rhi::SwapchainDesc lifecycleDesc{
        "frame-input-smoke", {128u, 72u}, rhi::TextureFormat::Bgra8Unorm, 2u};
    lifecycleDesc.platformSurface = window.surfaceToken();
    tests::verifyFrameInputLifecycle(*system.swapchain(), lifecycleDesc);
    const std::array<std::uint32_t, 4> expected{17u, 29u, 43u, 71u};
    auto source = device->createBuffer({"Admission.Source",
        sizeof(expected),
        rhi::ResourceUsage::CopySrc,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::CopySrc});
    auto target = device->createBuffer({"Admission.Readback",
        sizeof(expected),
        rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst});
    assert(source && target
        && device->writeBuffer(*source, expected.data(), sizeof(expected)).success);
    auto fenceOwner = core::makeShared<core::UniquePtr<rhi::GpuFence>>(device->createFence());
    std::shared_ptr<rhi::GpuFence> completion(fenceOwner, fenceOwner->get());
    auto pipeline = core::makeUnique<PendingPipeline>(completion);
    core::UniquePtr<renderer::RenderPipeline> base = std::move(pipeline);
    assert(system.setRenderPipeline(std::move(base)).success);
    auto encoder = device->createCommandEncoder({"Admission.RealConsumer"});
    auto transfer = encoder->beginTransferPass({"Admission.Copy"});
    assert(transfer
        && transfer->copyBufferToBuffer({source->id(), target->id(), 0u, 0u, sizeof(expected)})
            .success);
    assert(transfer->end().success);
    auto commands = encoder->finish();
    auto gate = device->createTimelineSemaphore(0u);
    assert(commands && gate && completion);
    assert(device->queue()
            .submit(*commands, gate.get(), 1u, nullptr, 0u, completion.get())
            .hasAcceptedWork());
    const bool initiallyPending = !completion->isSignaled();
    const auto acquiredBeforeLoss = system.acquireFrame();
    assert(acquiredBeforeLoss.status == rhi::AcquireStatus::NotReady);
    assert(system.swapchain()->setSurfaceAvailable(false).success);
    assert(system.swapchain()->setSurfaceAvailable(true).success);
    const auto rejectedAfterLoss = system.acquireFrame();
    assert(rejectedAfterLoss.status == rhi::AcquireStatus::NotReady
        && rejectedAfterLoss.diagnostics == "PLATFORM-FRAME-INPUT-ACQUISITION-INVALIDATED");
    const auto acquiredAfterLoss = system.acquireFrame();
    assert(acquiredAfterLoss.status == rhi::AcquireStatus::NotReady
        && acquiredAfterLoss.frameSerial != acquiredBeforeLoss.frameSerial);
    assert(system.resize({128u, 72u}).success);
    core::runtime::SpscFrameDoubleBuffer<int> packets;
    PendingConsumer consumer(system);
    core::runtime::RenderThread<int> thread(packets, consumer);
    packets.acquireForWrite() = 17;
    packets.publishWrite();
    thread.start();
    bool observed = false;
    {
        std::unique_lock<std::mutex> lock(consumer.mutex);
        observed = consumer.cv.wait_for(lock, std::chrono::seconds{5}, [&] {
            return consumer.deferred > 0u;
        });
    }
    const bool unpublished = packets.completedFrameSerial() == 0u;
    gate->signal(1u); // Release before assertions or teardown on a failed observation.
    if (observed)
        packets.waitFrameCompleted(17u);
    thread.stop();
    rhi::semantics::ShutdownDrainRequest shutdownDrain;
    shutdownDrain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto shutdownDrainReport = system.drainGpuForShutdown(shutdownDrain);
    assert(shutdownDrainReport.backend == rhi::Backend::Metal);
    assert(shutdownDrainReport.outcome == rhi::semantics::ShutdownDrainOutcome::Native);
    assert(shutdownDrainReport.completed());
    assert(initiallyPending && observed && unpublished && consumer.presented == 1u);
    assert(completion->isSignaled() && packets.completedFrameSerial() == 17u);
    std::array<std::uint32_t, 4> actual{};
    core::pmr::String diagnostic;
    assert(rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*target),
        actual.data(),
        sizeof(actual),
        0u,
        sizeof(actual),
        diagnostic));
    assert(actual == expected);
    std::printf(
        "Frame input Metal surface: pending=1 lossRejected=1 reacquired=1 resized=1 deferred=%u resumed=1 presented=1 packet=17 copy=exact\n",
        consumer.deferred);
}
