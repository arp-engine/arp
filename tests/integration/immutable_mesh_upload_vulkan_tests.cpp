#include "renderer/mesh/immutable_mesh_upload.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;

class GateQueue final : public rhi::GpuQueue {
public:
    GateQueue(rhi::GpuQueue& queue, rhi::GpuTimelineSemaphore& gate) : queue_(queue), gate_(gate) {}

    rhi::GpuQueueRole role() const override { return queue_.role(); }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& cmd) override
    {
        return submit(cmd, nullptr);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& cmd, rhi::GpuFence* fence) override
    {
        return queue_.submit(cmd, &gate_, 1, nullptr, 0, fence);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer&,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuFence*) override
    {
        return {false, "fixture-only", 0};
    }

    std::uint32_t submitCount() const override { return queue_.submitCount(); }

private:
    rhi::GpuQueue& queue_;
    rhi::GpuTimelineSemaphore& gate_;
};
} // namespace

int main()
{
    using namespace arp;
    tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    auto device = tests::VulkanHeapOwnerFixture::makeDevice(native.runtime());
    assert(device);
    auto gate = device->createTimelineSemaphore(0);
    assert(gate);
    GateQueue gated(device->queue(), *gate);
    tests::CapabilityOverrideDevice proxy(*device, nullptr);
    proxy.overrideQueue(&gated);
    auto mesh = assets::MeshAsset::debugTriangle();
    mesh.vertices[1].position[0] = 13.75f;
    mesh.vertices[2].normal[1] = -0.625f;
    const auto vertexBytes = mesh.vertices.size() * sizeof(assets::MeshVertex);
    const auto indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
    const auto bytes = vertexBytes + indexBytes;
    renderer::ImmutableMeshUpload upload;
    const auto prepared = upload.prepare(proxy, mesh, 9, {1, bytes, bytes, 4096, 16, 64, true});
    if (!prepared.success)
        std::fprintf(stderr, "%s\n", prepared.diagnostics.c_str());
    assert(prepared.success);
    assert(upload.submit().success);
    // A native timeline keeps the actual original copy pending deterministically.
    const bool pending = !upload.ready() && !upload.reapCompleted();
    const bool retained = upload.retainedUploadBytes() == bytes && !upload.cancelUnsubmitted();
    renderer::MeshGpuBuffer bindings;
    upload.installBindings(bindings);
    gate->signal(1); // Release even if a pending assertion later fails.
    rhi::semantics::ShutdownDrainRequest drain;
    drain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto uploaded = device->drainForShutdown(drain);
    assert(uploaded.completed() && pending && retained);
    assert(upload.ready() && upload.reapCompleted() && upload.retainedUploadBytes() == 0);
    assert(
        bindings.immutable() && bindings.vertexBuffer().desc().memory == rhi::MemoryUsage::GpuOnly);
    auto readback = device->createBuffer({"ImmutableMeshReadback",
        bytes + 32,
        rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst});
    auto encoder = device->createCommandEncoder({"ImmutableMesh.Readback"});
    assert(readback && encoder);
    for (auto* buffer : {&bindings.vertexBuffer(), &bindings.indexBuffer()}) {
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = buffer->id();
        barrier.before = rhi::ResourceState::ShaderRead;
        barrier.after = rhi::ResourceState::CopySrc;
        barrier.sourceStages = rhi::PipelineStage::VertexInput;
        barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = buffer == &bindings.vertexBuffer()
            ? rhi::ResourceAccess::VertexAttributeRead
            : rhi::ResourceAccess::IndexRead;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        assert(encoder->transition(*buffer, barrier).success);
    }
    auto pass = encoder->beginTransferPass({"ImmutableMesh.Readback"});
    assert(pass);
    assert(
        pass->copyBufferToBuffer({bindings.vertexBuffer().id(), readback->id(), 0, 16, vertexBytes})
            .success);
    assert(
        pass->copyBufferToBuffer(
                {bindings.indexBuffer().id(), readback->id(), 0, 16 + vertexBytes, indexBytes})
            .success);
    assert(pass->end().success);
    auto commands = encoder->finish();
    auto completion = device->createFence();
    assert(commands && completion && device->queue().submit(*commands, completion.get()).success);
    drain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto copied = device->drainForShutdown(drain);
    assert(copied.completed() && completion->isSignaled());
    const auto payload = rhi_vulkan::vulkanRhiNativePayload(*readback);
    core::pmr::String diagnostic;
    assert(payload && payload->backendInvalidateMappedRead(0, bytes + 32, diagnostic));
    const auto* actual = static_cast<const unsigned char*>(payload->backendMappedData());
    assert(actual && std::memcmp(actual + 16, mesh.vertices.data(), vertexBytes) == 0);
    assert(std::memcmp(actual + 16 + vertexBytes, mesh.indices.data(), indexBytes) == 0);
    assert(upload.clearAfterDeviceDrain(copied));
    std::puts(
        "ImmutableMeshResidency/1 native pending custody, chunked copies and exact vertex/index readback PASS");
}
