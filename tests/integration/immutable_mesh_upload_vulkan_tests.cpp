#include "3d/vegetation/mesh_adapter.h"
#include "renderer/mesh/immutable_mesh_upload.h"
#include "renderer/shadows/static_mesh_shadow_lod_gpu.h"
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

void runUpload(arp::rhi::GpuDevice& nativeDevice, bool customStream)
{
    using namespace arp;
    auto* device = &nativeDevice;
    auto gate = device->createTimelineSemaphore(0);
    assert(gate);
    GateQueue gated(device->queue(), *gate);
    tests::CapabilityOverrideDevice proxy(*device, nullptr);
    proxy.overrideQueue(&gated);
    auto mesh = assets::MeshAsset::debugTriangle();
    mesh.vertices[1].position[0] = 13.75f;
    mesh.vertices[2].normal[1] = -0.625f;
    auto stream = vegetation::convertToInstanceVertices(mesh, nullptr, false);
    stream[1].flutter = 0.375f;
    stream[2].stiffness = 0.625f;
    const auto vertexBytes = mesh.vertices.size()
        * (customStream ? sizeof(vegetation::InstanceVertex) : sizeof(assets::MeshVertex));
    const auto indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
    const auto bytes = vertexBytes + indexBytes;
    renderer::ImmutableMeshUpload upload;
    const renderer::ImmutableMeshUpload::Budget budget{1, bytes, bytes, 4096, 16, 64, true};
    const auto prepared = customStream ? upload.prepareVertexStream(proxy, mesh, stream, 9, budget)
                                       : upload.prepare(proxy, mesh, 9, budget);
    if (!prepared.success)
        std::fprintf(stderr, "%s\n", prepared.diagnostics.c_str());
    assert(prepared.success);
    core::UniquePtr<rhi::GpuBuffer> customVertices, customIndices;
    if (customStream) {
        upload.installPreparedGeometryBindings(customVertices, customIndices);
        assert(!upload.ready());
    }
    assert(upload.submit().success);
    // A native timeline keeps the actual original copy pending deterministically.
    const bool pending = !upload.ready() && !upload.reapCompleted();
    const bool retained = upload.retainedUploadBytes() == bytes && !upload.cancelUnsubmitted();
    renderer::MeshGpuBuffer bindings;
    if (!customStream)
        upload.installBindings(bindings);
    auto& vertexBuffer = customStream ? *customVertices : bindings.vertexBuffer();
    auto& indexBuffer = customStream ? *customIndices : bindings.indexBuffer();
    gate->signal(1); // Release even if a pending assertion later fails.
    rhi::semantics::ShutdownDrainRequest drain;
    drain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto uploaded = device->drainForShutdown(drain);
    assert(uploaded.completed() && pending && retained);
    assert(upload.ready() && upload.reapCompleted() && upload.retainedUploadBytes() == 0);
    assert(vertexBuffer.hostWritesSealed() && indexBuffer.hostWritesSealed());
    assert(vertexBuffer.desc().memory == rhi::MemoryUsage::GpuOnly);
    auto readback = device->createBuffer({"ImmutableMeshReadback",
        bytes + 32,
        rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst});
    auto encoder = device->createCommandEncoder({"ImmutableMesh.Readback"});
    assert(readback && encoder);
    for (auto* buffer : {&vertexBuffer, &indexBuffer}) {
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = buffer->id();
        barrier.before = rhi::ResourceState::ShaderRead;
        barrier.after = rhi::ResourceState::CopySrc;
        barrier.sourceStages = rhi::PipelineStage::VertexInput;
        barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = buffer == &vertexBuffer ? rhi::ResourceAccess::VertexAttributeRead
                                                       : rhi::ResourceAccess::IndexRead;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        assert(encoder->transition(*buffer, barrier).success);
    }
    auto pass = encoder->beginTransferPass({"ImmutableMesh.Readback"});
    assert(pass);
    assert(
        pass->copyBufferToBuffer({vertexBuffer.id(), readback->id(), 0, 16, vertexBytes}).success);
    assert(
        pass->copyBufferToBuffer(
                {indexBuffer.id(), readback->id(), 0, 16 + vertexBytes, indexBytes})
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
    const void* expectedVertices = customStream ? static_cast<const void*>(stream.data())
                                                : static_cast<const void*>(mesh.vertices.data());
    assert(actual && std::memcmp(actual + 16, expectedVertices, vertexBytes) == 0);
    assert(std::memcmp(actual + 16 + vertexBytes, mesh.indices.data(), indexBytes) == 0);
    assert(upload.clearAfterDeviceDrain(copied));
}

// Exercise all five real destination streams, including index-only upload,
// through one native timeline. Readback validates the post-copy payload bytes.
void runShadowBanks(arp::rhi::GpuDevice& device)
{
    using namespace arp;
    auto gate = device.createTimelineSemaphore(0);
    assert(gate);
    GateQueue gated(device.queue(), *gate);
    tests::CapabilityOverrideDevice proxy(device, nullptr);
    proxy.overrideQueue(&gated);
    auto lod1 = assets::MeshAsset::debugTriangle();
    auto lod2 = lod1;
    lod1.vertices[1].position[0] = 13.75f;
    lod2.vertices[2].normal[1] = -0.625f;
    core::pmr::Vector<std::uint32_t> original{&core::defaultCpuMemoryResource()};
    original.assign({2, 0, 1});
    const auto vb = lod1.vertices.size() * sizeof(assets::MeshVertex);
    const auto ib = original.size() * sizeof(std::uint32_t);
    const auto bytes = 2 * vb + 3 * ib;
    renderer::StaticMeshShadowLodGpu banks;
    const renderer::StaticMeshShadowLodGpu::ResidencyBudget budget{
        {1, bytes, bytes, 8192, 16, 128, true}, 41};
    const auto prepared =
        banks.prepareResident(proxy, original, lod1.vertices.size(), lod1, lod2, budget);
    if (!prepared.success)
        std::fprintf(stderr, "%s\n", prepared.diagnostics.c_str());
    assert(prepared.success);
    const auto submitted = banks.submitResidency();
    const bool pending = submitted.success && !banks.ready() && !banks.reapCompleted()
        && banks.requiresDrain() && banks.retainedUploadBytes() == bytes;
    gate->signal(1); // Release actual work before evaluating pending assertions.
    rhi::semantics::ShutdownDrainRequest drain;
    drain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto uploaded = device.drainForShutdown(drain);
    assert(uploaded.completed() && pending && banks.ready() && banks.reapCompleted());
    assert(banks.retainedUploadBytes() == 0 && banks.requiresDrain());
    const std::array<rhi::GpuBuffer*, 5> streams{&banks.candidate(1).vertexBuffer(),
        &banks.candidate(1).indexBuffer(),
        &banks.candidate(2).vertexBuffer(),
        &banks.candidate(2).indexBuffer(),
        banks.originalIndexBuffer()};
    const std::array<const void*, 5> expected{lod1.vertices.data(),
        lod1.indices.data(),
        lod2.vertices.data(),
        lod2.indices.data(),
        original.data()};
    auto readback = device.createBuffer({"ShadowBanks.Readback",
        bytes,
        rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::GpuToCpu,
        rhi::ResourceState::CopyDst});
    auto encoder = device.createCommandEncoder({"ShadowBanks.Readback"});
    assert(readback && encoder);
    for (unsigned i = 0; i < streams.size(); ++i) {
        auto& buffer = *streams[i];
        assert(buffer.desc().memory == rhi::MemoryUsage::GpuOnly && buffer.hostWritesSealed());
        rhi::ResourceBarrierDesc barrier;
        barrier.resource = buffer.id();
        barrier.before = rhi::ResourceState::ShaderRead;
        barrier.after = rhi::ResourceState::CopySrc;
        barrier.sourceStages = rhi::PipelineStage::VertexInput;
        barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = i == 0 || i == 2 ? rhi::ResourceAccess::VertexAttributeRead
                                                : rhi::ResourceAccess::IndexRead;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        assert(encoder->transition(buffer, barrier).success);
    }
    auto pass = encoder->beginTransferPass({"ShadowBanks.Readback"});
    assert(pass);
    std::uint64_t offset = 0;
    for (const auto* buffer : streams) {
        assert(
            pass->copyBufferToBuffer(
                    {buffer->id(), readback->id(), 0, offset, buffer->desc().sizeBytes})
                .success);
        offset += buffer->desc().sizeBytes;
    }
    assert(offset == bytes && pass->end().success);
    auto commands = encoder->finish();
    auto completion = device.createFence();
    assert(commands && completion && device.queue().submit(*commands, completion.get()).success);
    drain.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto copied = device.drainForShutdown(drain);
    assert(copied.completed() && completion->isSignaled());
    const auto payload = rhi_vulkan::vulkanRhiNativePayload(*readback);
    core::pmr::String diagnostic;
    assert(payload && payload->backendInvalidateMappedRead(0, bytes, diagnostic));
    const auto* actual = static_cast<const unsigned char*>(payload->backendMappedData());
    assert(actual);
    offset = 0;
    for (unsigned i = 0; i < streams.size(); ++i) {
        const auto size = streams[i]->desc().sizeBytes;
        assert(std::memcmp(actual + offset, expected[i], size) == 0);
        offset += size;
    }
    assert(banks.clearAfterDeviceDrain(copied));
    std::puts("Shadow bank native pending custody and five-stream exact readback PASS");
}

int main()
{
    arp::tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    auto device = arp::tests::VulkanHeapOwnerFixture::makeDevice(native.runtime());
    assert(device);
    runUpload(*device, false);
    runUpload(*device, true);
    runShadowBanks(*device);
    std::puts(
        "ImmutableMeshResidency/1 native pending custody, chunked MeshVertex/InstanceVertex and exact index readback PASS");
}
