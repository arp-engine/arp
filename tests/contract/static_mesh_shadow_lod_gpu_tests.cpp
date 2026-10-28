#include "renderer/shadows/static_mesh_shadow_lod_gpu.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "tests/fixtures/capability_override_device.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>

namespace {
using namespace arp;
using Banks = renderer::StaticMeshShadowLodGpu;

// Null-only delayed completion and outcome injection. Real queue recording is
// exercised; these fences are validator controls, never native proof.
class BankQueue final : public rhi::GpuQueue {
public:
    explicit BankQueue(rhi::GpuQueue& queue) : queue_(queue) {}

    rhi::GpuQueueRole role() const override { return queue_.role(); }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& cmd) override
    {
        return submit(cmd, nullptr);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& cmd, rhi::GpuFence* fence) override
    {
        const auto index = calls++;
        assert(index < completions.size());
        if (index == failAt && !acceptedError)
            return {false, "bank-rejected", 0};
        completions[index] = fence;
        auto result = queue_.submit(cmd);
        if (index == failAt && acceptedError && result.hasAcceptedWork())
            return {false, "bank-accepted-error", result.submittedCommandCount, true};
        return result;
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

    std::uint32_t submitCount() const override { return calls; }

    std::array<rhi::GpuFence*, 3> completions{};
    unsigned calls = 0, failAt = UINT32_MAX;
    bool acceptedError = false;

private:
    rhi::GpuQueue& queue_;
};
} // namespace

int main()
{
    using namespace arp;
    auto native = rhi::createNullDevice();
    tests::CapabilityOverrideDevice device(*native, nullptr);
    auto mesh = assets::MeshAsset::debugTriangle();
    core::pmr::Vector<std::uint32_t> original{&core::defaultCpuMemoryResource()};
    original.assign({2, 0, 1});
    const auto payload = 2
            * (mesh.vertices.size() * sizeof(assets::MeshVertex)
                + mesh.indices.size() * sizeof(std::uint32_t))
        + original.size() * sizeof(std::uint32_t);
    const auto metadata =
        2 * mesh.submeshes.size() * (sizeof(assets::Submesh) + sizeof(renderer::SubmeshDrawBounds));
    const Banks::ResidencyBudget budget{{1, payload, payload, metadata, payload & ~3ull, 5}, 19};
    const auto prepare = [&](Banks& banks, const Banks::ResidencyBudget& policy) {
        return banks.prepareResident(device, original, mesh.vertices.size(), mesh, mesh, policy);
    };
    {
        Banks legacy;
        assert(legacy.prepare(device, original, mesh.vertices.size(), mesh, mesh, payload).success);
        assert(legacy.prepared() && legacy.ready() && !legacy.requiresDrain());
        assert(legacy.candidate(1).vertexBuffer().desc().memory == rhi::MemoryUsage::CpuToGpu);
        legacy.clear();
    }
    for (unsigned field = 0; field < 7; ++field) {
        auto invalid = budget;
        switch (field) {
        case 0:
            --invalid.upload.destinationBytes;
            break;
        case 1:
            --invalid.upload.uploadBytes;
            break;
        case 2:
            --invalid.upload.metadataBytes;
            break;
        case 3:
            --invalid.upload.maxCopyCommands;
            break;
        case 4:
            invalid.sourceGeneration = 0;
            break;
        case 5:
            invalid.upload.contractVersion = 0;
            break;
        case 6:
            invalid.upload.maxCopyBytes = 3;
            break;
        }
        Banks banks;
        const auto allocations = device.bufferCreateAttempts();
        assert(!prepare(banks, invalid).success && !banks.prepared() && !banks.requiresDrain());
        assert(device.bufferCreateAttempts() == allocations);
    }
    {
        Banks banks;
        original[0] = UINT32_MAX;
        assert(prepare(banks, budget).diagnostics == "SHADOW-LOD-GPU-ORIGINAL-INDEX-INVALID");
        original[0] = 2;
        auto invalid = mesh;
        invalid.indices[0] = UINT32_MAX;
        const auto bytesBefore = device.memoryStats().allocatedBytes;
        assert(!banks.prepareResident(device, original, mesh.vertices.size(), mesh, invalid, budget)
                .success);
        assert(!banks.prepared() && device.memoryStats().allocatedBytes == bytesBefore);
        device.failBufferCreation("ImmutableMesh.Indices");
        assert(!prepare(banks, budget).success && !banks.prepared());
        assert(device.memoryStats().allocatedBytes == bytesBefore);
        device.failBufferCreation({});
    }
    {
        BankQueue queue(native->queue());
        device.overrideQueue(&queue);
        Banks banks;
        const auto allocations = device.bufferCreateAttempts();
        assert(prepare(banks, budget).success);
        assert(device.bufferCreateAttempts() - allocations == 10); // Five destination/upload pairs.
        assert(banks.prepared() && !banks.ready() && !banks.requiresDrain());
        assert(banks.residentBytes() == payload && banks.metadataBytes() == metadata);
        assert(banks.copyCommandCount() == 5 && banks.retainedUploadBytes() == payload);
        for (unsigned level : {1u, 2u}) {
            const auto& candidate = banks.candidate(level);
            assert(candidate.validForIndexedDraw() && candidate.immutable());
            assert(candidate.submeshCount() == mesh.submeshes.size());
            assert(candidate.vertexBuffer().desc().memory == rhi::MemoryUsage::GpuOnly);
            assert(candidate.indexBuffer().desc().memory == rhi::MemoryUsage::GpuOnly);
        }
        assert(banks.originalIndexBuffer()->desc().memory == rhi::MemoryUsage::GpuOnly);
        assert(banks.originalIndexBuffer()->hostWritesSealed());
        assert(banks.submitResidency().success && queue.calls == 3);
        assert(!banks.submitResidency().success && banks.requiresDrain());
        assert(!prepare(banks, budget).success);
        auto unrelated = native->createFence();
        unrelated->signal();
        assert(!banks.ready());
        rhi::semantics::ShutdownDrainReport failed;
        assert(!banks.clearAfterDeviceDrain(failed));
        bool rejected = false;
        try {
            banks.clear();
        } catch (const core::DiagnosticError&) {
            rejected = true;
        }
        assert(rejected && banks.prepared());
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope noAlloc(quota);
            assert(!banks.reapCompleted());
            queue.completions[1]->signal();
            queue.completions[0]->signal();
            assert(!banks.ready() && !banks.reapCompleted());
            assert(banks.retainedUploadBytes() == original.size() * sizeof(std::uint32_t));
            queue.completions[2]->signal();
            assert(banks.ready() && banks.reapCompleted());
            assert(banks.retainedUploadBytes() == 0 && banks.requiresDrain());
        }
        assert(quota->peakBytes() == 0);
        rhi::semantics::ShutdownDrainRequest request;
        request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        const auto drained = native->drainForShutdown(request);
        assert(drained.completed() && banks.clearAfterDeviceDrain(drained));
        assert(!banks.prepared() && !banks.requiresDrain());
        device.overrideQueue(nullptr);
    }
    for (unsigned failAt = 0; failAt < 3; ++failAt)
        for (bool accepted : {false, true}) {
            BankQueue queue(native->queue());
            queue.failAt = failAt;
            queue.acceptedError = accepted;
            device.overrideQueue(&queue);
            Banks banks;
            assert(prepare(banks, budget).success);
            assert(!banks.submitResidency().success && !banks.ready());
            assert(queue.calls == failAt + 1);
            assert(!banks.submitResidency().success);
            assert(banks.requiresDrain() == (failAt != 0 || accepted));
            assert(banks.retainedUploadBytes() == payload);
            for (auto* completion : queue.completions)
                if (completion)
                    completion->signal();
            banks.reapCompleted();
            assert(!banks.ready()); // An accepted error is not healed by signaling.
            if (banks.requiresDrain()) {
                bool rejected = false;
                try {
                    banks.clear();
                } catch (const core::DiagnosticError&) {
                    rejected = true;
                }
                assert(rejected);
                banks.clearAfterDeviceLoss(); // Explicit Null fixture teardown.
            } else {
                banks.clear();
            }
            assert(!banks.prepared() && banks.residentBytes() == 0);
            device.overrideQueue(nullptr);
        }
    rhi::NullHiddenFrameSmokeRunner smoke(*native);
    const auto result = smoke.run({});
    assert(result.success && result.submittedFrameCount == 2);
}
