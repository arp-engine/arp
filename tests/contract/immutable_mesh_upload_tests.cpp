#include "3d/vegetation/mesh_adapter.h"
#include "renderer/mesh/immutable_mesh_upload.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/submission_outcome_queue.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using Upload = renderer::ImmutableMeshUpload;

// Explicit Null validator delay; this is never native completion proof.
class PendingQueue final : public rhi::GpuQueue {
public:
    explicit PendingQueue(rhi::GpuQueue& inner) : inner_(inner) {}

    rhi::GpuQueueRole role() const override { return inner_.role(); }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& cmd) override
    {
        return submit(cmd, nullptr);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& cmd, rhi::GpuFence* fence) override
    {
        completion = fence;
        return inner_.submit(cmd);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer&,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuFence*) override
    {
        return {false, "not a timeline fixture", 0};
    }

    std::uint32_t submitCount() const override { return inner_.submitCount(); }

    rhi::GpuFence* completion = nullptr;

private:
    rhi::GpuQueue& inner_;
};
} // namespace

int main()
{
    using namespace arp;
    auto null = rhi::createNullDevice();
    tests::CapabilityOverrideDevice device(*null, nullptr);
    const auto mesh = assets::MeshAsset::debugTriangle();
    const auto bytes = mesh.vertices.size() * sizeof(assets::MeshVertex)
        + mesh.indices.size() * sizeof(std::uint32_t);
    const Upload::Budget budget{1, bytes, bytes, 4096, 16, 64};
    {
        auto vertices = vegetation::convertToInstanceVertices(mesh, nullptr, false);
        vertices[1].flutter = 0.375f;
        vertices[2].stiffness = 0.625f;
        const auto streamBytes = vertices.size() * sizeof(vegetation::InstanceVertex)
            + mesh.indices.size() * sizeof(std::uint32_t);
        Upload upload;
        const auto allocations = device.bufferCreateAttempts();
        vertices.pop_back();
        assert(upload.prepareVertexStream(device, mesh, vertices, 17, budget).diagnostics
            == "IMMUTABLE-MESH-VERTEX-STREAM-COUNT");
        vertices = vegetation::convertToInstanceVertices(mesh, nullptr, false);
        vertices[0].position[0] += 1;
        assert(upload.prepareVertexStream(device, mesh, vertices, 17, budget).diagnostics
            == "IMMUTABLE-MESH-VERTEX-STREAM-POSITION");
        assert(device.bufferCreateAttempts() == allocations);
        vertices = vegetation::convertToInstanceVertices(mesh, nullptr, false);
        const Upload::Budget streamBudget{
            1, streamBytes, streamBytes, 4096, streamBytes & ~3ull, 2};
        assert(upload.prepareVertexStream(device, mesh, vertices, 17, streamBudget).success);
        assert(upload.payloadBytes() == streamBytes && upload.copyCommandCount() == 2);
        assert(upload.metadataBytes() >= mesh.submeshes.size() * sizeof(assets::Submesh));
        core::UniquePtr<rhi::GpuBuffer> v, i;
        upload.installPreparedGeometryBindings(v, i);
        assert(
            v && i && v->desc().sizeBytes == vertices.size() * sizeof(vegetation::InstanceVertex));
        assert(v->hostWritesSealed() && i->hostWritesSealed());
        bool duplicateRejected = false;
        try {
            core::UniquePtr<rhi::GpuBuffer> duplicateV, duplicateI;
            upload.installPreparedGeometryBindings(duplicateV, duplicateI);
        } catch (const core::DiagnosticError& error) {
            duplicateRejected =
                std::strcmp(error.what(), "IMMUTABLE-MESH-PREPARED-BINDING-STATE") == 0;
        }
        assert(duplicateRejected && !upload.ready());
        PendingQueue pending(null->queue());
        device.overrideQueue(&pending);
        assert(upload.submit().success && !upload.ready() && !upload.reapCompleted());
        pending.completion->signal();
        assert(upload.ready() && upload.reapCompleted() && upload.retainedUploadBytes() == 0);
        upload.clearAfterDeviceLoss(); // Null fixture teardown only.
        device.overrideQueue(nullptr);
    }
    {
        core::pmr::Vector<std::uint32_t> indices{&core::defaultCpuMemoryResource()};
        indices.assign({2, 0, 1});
        const Upload::Budget indexBudget{1, 12, 12, 0, 4, 3};
        Upload upload;
        const auto allocations = device.bufferCreateAttempts();
        auto invalid = indexBudget;
        --invalid.maxCopyCommands;
        assert(upload.prepareIndexStream(device, indices, 3, 23, invalid).diagnostics
            == "IMMUTABLE-MESH-COMMAND-BUDGET");
        invalid = indexBudget;
        --invalid.uploadBytes;
        assert(upload.prepareIndexStream(device, indices, 3, 23, invalid).diagnostics
            == "IMMUTABLE-MESH-PAYLOAD-BUDGET");
        indices[0] = 3;
        assert(upload.prepareIndexStream(device, indices, 3, 23, indexBudget).diagnostics
            == "IMMUTABLE-MESH-INDEX-INVALID");
        indices[0] = 2;
        assert(device.bufferCreateAttempts() == allocations);
        PendingQueue pending(null->queue());
        device.overrideQueue(&pending);
        assert(upload.prepareIndexStream(device, indices, 3, 23, indexBudget).success);
        assert(device.bufferCreateAttempts() - allocations == 2);
        assert(upload.metadataBytes() == 0 && upload.payloadBytes() == 12
            && upload.copyCommandCount() == 3 && upload.sourceGeneration() == 23);
        renderer::MeshGpuBuffer destination;
        bool invalidFullBinding = false;
        try {
            upload.installPreparedBindings(destination);
        } catch (const core::DiagnosticError&) {
            invalidFullBinding = true;
        }
        assert(invalidFullBinding);
        core::UniquePtr<rhi::GpuBuffer> binding;
        upload.installPreparedIndexBinding(binding);
        assert(binding && binding->desc().memory == rhi::MemoryUsage::GpuOnly);
        assert(binding->hostWritesSealed() && upload.submit().success);
        assert(!upload.ready() && !upload.cancelUnsubmitted());
        pending.completion->signal();
        assert(upload.ready() && upload.reapCompleted() && upload.retainedUploadBytes() == 0);
        upload.clearAfterDeviceLoss();
        device.overrideQueue(nullptr);
    }
    const auto rejects =
        [&](const assets::MeshAsset& source, Upload::Budget policy, const char* diagnostic) {
            Upload upload;
            const auto allocations = device.bufferCreateAttempts();
            const auto result = upload.prepare(device, source, 7, policy);
            if (result.success || result.diagnostics != diagnostic)
                std::fprintf(stderr,
                    "expected=%s actual=%s success=%d\n",
                    diagnostic,
                    result.diagnostics.c_str(),
                    int(result.success));
            assert(!result.success && result.diagnostics == diagnostic);
            assert(upload.state() == Upload::State::Empty && !upload.ready());
            assert(device.bufferCreateAttempts() == allocations);
        };
    auto bad = budget;
    bad.destinationBytes--;
    rejects(mesh, bad, "IMMUTABLE-MESH-PAYLOAD-BUDGET");
    bad = budget;
    bad.uploadBytes--;
    rejects(mesh, bad, "IMMUTABLE-MESH-PAYLOAD-BUDGET");
    bad = budget;
    bad.metadataBytes = 1;
    rejects(mesh, bad, "IMMUTABLE-MESH-PAYLOAD-BUDGET");
    bad = budget;
    bad.maxCopyCommands = 1;
    rejects(mesh, bad, "IMMUTABLE-MESH-COMMAND-BUDGET");
    bad = budget;
    bad.maxCopyBytes = 3;
    rejects(mesh, bad, "IMMUTABLE-MESH-BUDGET-OR-GENERATION");
    auto invalid = mesh;
    invalid.indices[0] = UINT32_MAX;
    rejects(invalid, budget, "IMMUTABLE-MESH-INDEX-INVALID");
    invalid = mesh;
    invalid.submeshes[0].firstIndex = UINT32_MAX;
    rejects(invalid, budget, "IMMUTABLE-MESH-SECTION-INVALID");
    {
        tests::CapabilityOverrideDevice noMemory(*null, [](rhi::DeviceCapabilities& caps) {
            caps.memory.deviceLocal = false;
        });
        Upload upload;
        assert(upload.prepare(noMemory, mesh, 7, budget).diagnostics
            == "IMMUTABLE-MESH-EXECUTION-UNAVAILABLE");
        assert(noMemory.bufferCreateAttempts() == 0);
    }
    {
        device.failBufferCreation("ImmutableMesh.Indices");
        Upload upload;
        const auto before = device.memoryStats().allocatedBytes;
        assert(!upload.prepare(device, mesh, 7, budget).success);
        assert(upload.state() == Upload::State::Empty);
        assert(device.memoryStats().allocatedBytes == before);
        device.failBufferCreation({});
        device.failBufferWrites("ImmutableMesh.Upload");
        assert(!upload.prepare(device, mesh, 7, budget).success);
        assert(device.memoryStats().allocatedBytes == before);
        device.failBufferWrites({});
        device.failFenceCreation(true);
        assert(upload.prepare(device, mesh, 7, budget).diagnostics
            == "IMMUTABLE-MESH-COMPLETION-UNAVAILABLE");
        assert(device.memoryStats().allocatedBytes == before);
        device.failFenceCreation(false);
    }
    {
        PendingQueue pending(null->queue());
        device.overrideQueue(&pending);
        Upload upload;
        device.observeBuffer("ImmutableMesh.Vertices", 0);
        device.observeBuffer("ImmutableMesh.Indices", 1);
        assert(upload.prepare(device, mesh, 7, budget).success);
        assert(upload.sourceGeneration() == 7 && upload.payloadBytes() == bytes);
        assert(upload.state() == Upload::State::Prepared && !upload.ready());
        assert(device.observedBuffer(0)->desc().memory == rhi::MemoryUsage::GpuOnly);
        assert(device.observedBuffer(1)->desc().memory == rhi::MemoryUsage::GpuOnly);
        assert(device.observedBufferWriteAttempts(0) == 0);
        assert(device.observedBufferWriteAttempts(1) == 0);
        assert(upload.submit().success);
        assert(upload.state() == Upload::State::AcceptedPending && !upload.ready());
        assert(!upload.cancelUnsubmitted() && !upload.reapCompleted());
        rhi::semantics::ShutdownDrainReport failedDrain;
        assert(!upload.clearAfterDeviceDrain(failedDrain));
        assert(upload.retainedUploadBytes() == bytes);
        auto unrelated = null->createFence();
        unrelated->signal();
        assert(!upload.ready());
        renderer::MeshGpuBuffer bindings;
        upload.installBindings(bindings);
        assert(bindings.validForIndexedDraw() && bindings.immutable());
        assert(bindings.vertexBuffer().hostWritesSealed());
        assert(renderer::uploadOrUpdateMeshToRhi(device, mesh, bindings).diagnostics
            == "IMMUTABLE-MESH-UPDATE-FORBIDDEN");
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope noAlloc(quota);
            for (unsigned i = 0; i < 100; ++i)
                assert(!upload.ready() && !upload.reapCompleted());
            pending.completion->signal();
            assert(upload.ready() && upload.reapCompleted());
            assert(upload.ready() && upload.retainedUploadBytes() == 0);
        }
        assert(quota->peakBytes() == 0);
        upload.clearAfterDeviceLoss(); // Null fixture teardown, not native loss evidence.
        device.overrideQueue(nullptr);
    }
    for (bool accepted : {false, true}) {
        tests::SubmissionOutcomeQueue queue(null->queue());
        queue.reject = !accepted;
        queue.failAfterAcceptance = accepted;
        device.overrideQueue(&queue);
        Upload upload;
        assert(upload.prepare(device, mesh, 7, budget).success);
        assert(!upload.submit().success && !upload.ready());
        assert(
            upload.state() == (accepted ? Upload::State::AcceptedError : Upload::State::Rejected));
        assert(!upload.reapCompleted());
        assert(upload.retainedUploadBytes() == bytes);
        assert(upload.cancelUnsubmitted() == !accepted);
        if (accepted)
            upload.clearAfterDeviceLoss();
        device.overrideQueue(nullptr);
    }
    rhi::NullHiddenFrameSmokeRunner runner(*null);
    const auto smoke = runner.run({});
    assert(smoke.success && smoke.submittedFrameCount == 2);
    assert(smoke.resourceStats.pendingDestroyCount == 0);
}
