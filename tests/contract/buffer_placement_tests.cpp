#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/frontend/buffer_placement.h"
#if defined(ARP_PLACEMENT_TEST_METAL)
#include "rhi/backends/metal/metal_backend_objects.h"
#endif
#if defined(ARP_PLACEMENT_TEST_WEBGPU)
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_creation.h"
#endif

#include <cassert>
#include <cstring>

using namespace arp::rhi;

namespace {
const char* observedDiagnostic = nullptr;

void observe(RhiSeverity severity, const char* diagnostic, void*)
{
    assert(severity == RhiSeverity::Error);
    observedDiagnostic = diagnostic;
}
} // namespace

int main(int argc, char** argv)
{
    using Contract = HostVisibleDeviceLocalBufferContract;
    BufferDesc desc;
    desc.sizeBytes = 256u;
    desc.usage = ResourceUsage::StorageBuffer;
    desc.memory = MemoryUsage::CpuToGpu;
    desc.initialState = ResourceState::ShaderWrite;
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255)}) {
        // No new default capability or descriptor opt-in.
        assert(!MemoryCapabilities{}.hostVisibleDeviceLocalBuffers);
        assert(!Contract::validate(desc, backend, false));
        desc.hostVisibleDeviceLocal = {1u, false};
        assert(
            !std::strcmp(Contract::validate(desc, backend, true), "RHI-HOST-DEVICE-BUFFER-OPT-IN"));
        desc.hostVisibleDeviceLocal = {0u, true};
        assert(!std::strcmp(
            Contract::validate(desc, backend, true), "RHI-HOST-DEVICE-BUFFER-VERSION"));
        desc.hostVisibleDeviceLocal = {2u, true};
        assert(Contract::validate(desc, backend, true));
        desc.hostVisibleDeviceLocal = {1u, true};
        const auto lower = Contract::lowering(backend, true);
        assert(lower.executionAvailable == (backend == Backend::Vulkan));
        assert(lower.validatorOnly == (backend == Backend::Null));
        assert(bool(lower.diagnostic) == !lower.executionAvailable);
        assert(bool(Contract::validate(desc, backend, true))
            == (backend != Backend::Vulkan && backend != Backend::Null));
        for (const auto memory :
            {MemoryUsage::GpuOnly, MemoryUsage::GpuToCpu, MemoryUsage::Transient}) {
            desc.memory = memory;
            assert(Contract::validate(desc, backend, true));
        }
        desc.memory = MemoryUsage::CpuToGpu;
        desc.hostVisibleDeviceLocal = {};
    }
    desc.hostVisibleDeviceLocal = {1u, true};
    assert(!std::strcmp(
        Contract::validate(desc, Backend::Vulkan, false), "RHI-HOST-DEVICE-BUFFER-CAPABILITY"));
    assert(!Contract::lowering(Backend::Vulkan, false).executionAvailable);
    RhiErrorHandler::get().setCallback(observe, nullptr);
#if defined(ARP_PLACEMENT_TEST_METAL)
    assert(!arp::rhi_metal::createMetalRhiBuffer({}, desc, {}, {}, {}));
    assert(observedDiagnostic
        && !std::strcmp(observedDiagnostic, "RHI-HOST-DEVICE-BUFFER-METAL-PAYLOAD-MISSING"));
    observedDiagnostic = nullptr;
#endif
#if defined(ARP_PLACEMENT_TEST_WEBGPU)
    assert(!arp::rhi_webgpu::createWebGpuRhiBuffer({}, desc, {}, {}, {}));
    assert(observedDiagnostic
        && !std::strcmp(observedDiagnostic, "RHI-HOST-DEVICE-BUFFER-WEBGPU-PLACEMENT-UNAVAILABLE"));
    BufferDesc readback = desc;
    readback.memory = MemoryUsage::GpuToCpu;
    readback.usage = ResourceUsage::CopyDst;
    readback.initialState = ResourceState::CopyDst;
    const char* diagnostic = nullptr;
    assert(!arp::rhi_webgpu::WebGpuNativeBufferCreation::prepareReadbackOriginal(
        readback, nullptr, {}, diagnostic));
    assert(diagnostic && !std::strcmp(diagnostic, "RHI-HOST-DEVICE-BUFFER-MEMORY-INTENT"));
#endif
    RhiErrorHandler::get().setCallback(nullptr, nullptr);
    auto device = createNullDevice();
    assert(!device->capabilities().memory.hostVisibleDeviceLocalBuffers);
    const auto before = device->memoryStats();
    desc.hostVisibleDeviceLocal.prototypeOptIn = false;
    assert(!device->createBuffer(desc));
    assert(device->memoryStats().allocatedBytes == before.allocatedBytes);
    desc.hostVisibleDeviceLocal.prototypeOptIn = true;
    auto buffer = device->createBuffer(desc);
    assert(buffer && buffer->desc().hostVisibleDeviceLocal.requested());
    assert(device->memoryStats().allocatedBytes == before.allocatedBytes + desc.sizeBytes);
    buffer.reset();
    assert(device->memoryStats().allocatedBytes == before.allocatedBytes);
    if (argc == 2 && !std::strcmp(argv[1], "--null-hidden-frame")) {
        const auto report = NullHiddenFrameSmokeRunner(*device).run({});
        assert(report.success && report.submittedFrameCount == 2u);
        assert(!Contract::lowering(Backend::Null, true).executionAvailable);
    }
}
