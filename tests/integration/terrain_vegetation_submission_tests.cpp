#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/terrain_vegetation_delayed_queue.h"
#include "tests/fixtures/terrain_vegetation_gpu_upload.h"
#include "tests/fixtures/terrain_vegetation_submission.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using Submission = renderer::TerrainVegetationSubmission;
using State = Submission::State;

std::shared_ptr<const renderer::TerrainVegetationGpuSnapshot> upload(
    rhi::GpuDevice& device, renderer::TerrainVegetationGpuUpload::Desc& desc)
{
    desc.validatorOptIn = true;
    renderer::TerrainVegetationGpuUpload ticket;
    assert(ticket.prepare(device, tests::terrainVegetationDrawFixture(), desc).success);
    assert(ticket.submit().success && ticket.poll());
    auto snapshot = ticket.snapshot();
    assert(ticket.cancel());
    return snapshot;
}

struct ValidatorDrain final {
    rhi::GpuDevice& device;
    unsigned calls = 0;

    static rhi::semantics::ShutdownDrainReport run(
        void* context, const rhi::semantics::ShutdownDrainRequest& request)
    {
        auto& self = *static_cast<ValidatorDrain*>(context);
        ++self.calls;
        // No native work is certified here. The delayed Null fixture has no
        // execution payload; never dereference an unknown-submit fence.
        return self.device.drainForShutdown(request);
    }
};

class MeasuredObserver final : public render_graph::PassSubmissionObserver {
public:
    explicit MeasuredObserver(Submission& owner) : owner_(owner) {}

    unsigned calls = 0;

    const char* validateSubmission(render_graph::PassSubmissionToken token) const noexcept override
    {
        return owner_.validateSubmission(token);
    }

    void onSubmitted(render_graph::PassSubmissionToken token,
        const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override
    {
        const auto before = core::defaultCpuAllocator().stats();
        owner_.onSubmitted(token, completion);
        assert(before.allocationCount == core::defaultCpuAllocator().stats().allocationCount);
        ++calls;
    }

private:
    Submission& owner_;
};

void preparationContracts(rhi::GpuDevice& device)
{
    auto uploadDesc = tests::terrainVegetationUploadDescFixture();
    auto snapshot = upload(device, uploadDesc);
    auto desc = tests::terrainVegetationSubmissionDescFixture();
    Submission submission(device);
    assert(submission.prepare(snapshot, desc).diagnostics
        == "VEGETATION-SUBMISSION-PROFILE-UNAVAILABLE");
    desc.validatorOptIn = true;
    auto bad = desc;
    bad.contractVersion = 2u;
    assert(!submission.prepare(snapshot, bad).success);
    bad = desc;
    bad.maxCpuBytes = 1u;
    assert(submission.prepare(snapshot, bad).diagnostics == "VEGETATION-SUBMISSION-CPU-BUDGET");
    bad = desc;
    bad.liveSubmissions = core::makeShared<core::AtomicCapacityBudget>(0u);
    assert(submission.prepare(snapshot, bad).diagnostics == "VEGETATION-SUBMISSION-CAPACITY");
    assert(!submission.prepare(nullptr, desc).success);
    assert(!submission.prepare(core::makeShared<renderer::TerrainVegetationGpuSnapshot>(), desc)
            .success);
    auto other = rhi::createNullDevice();
    Submission foreign(*other);
    assert(foreign.prepare(snapshot, desc).diagnostics == "VEGETATION-SUBMISSION-DEVICE-MISMATCH");
    assert(submission.prepare(snapshot, desc).success);
    assert(!submission.prepare(snapshot, desc).success);
    auto stale = submission.token();
    ++stale.generation;
    assert(submission.validateSubmission(stale));
    auto encoder = other->createCommandEncoder({"ForeignConsumer"});
    render_graph::RenderContext context(*other, *encoder);
    assert(std::strcmp(submission.beginUse(context, submission.token()),
               "VEGETATION-SUBMISSION-CONSUMER-DEVICE-MISMATCH")
        == 0);
    assert(submission.state() == State::Prepared);
    auto localEncoder = device.createCommandEncoder({"IdentityUnavailableConsumer"});
    tests::ComputeRecordFailure masked(*localEncoder, tests::ComputeRecordFailure::Point::Begin);
    masked.exposeRecordingIdentity = false;
    render_graph::RenderContext unavailable(device, masked);
    assert(std::strcmp(submission.beginUse(unavailable, submission.token()),
               "VEGETATION-SUBMISSION-RECORDING-IDENTITY-UNAVAILABLE")
        == 0);
    assert(submission.state() == State::Prepared);
    assert(submission.cancel() && desc.liveSubmissions->used() == 0u);
    render_graph::FrameGraph graph(device);
    graph.addPass("CancelledConsumer")
        .externalSideEffects()
        .onSubmitted(submission, submission.token())
        .execute([](auto&) {
            assert(false);
        });
    bool failed = false;
    try {
        graph.execute();
    } catch (const std::exception&) {
        failed = true;
    }
    assert(failed && !graph.lastExecutionSubmitted());
}

void independentConsumers(rhi::GpuDevice& inner)
{
    tests::CapabilityOverrideDevice device(inner, nullptr);
    auto uploadDesc = tests::terrainVegetationUploadDescFixture();
    auto snapshot = upload(device, uploadDesc);
    const std::weak_ptr<const renderer::TerrainVegetationGpuSnapshot> weak = snapshot;
    auto desc = tests::terrainVegetationSubmissionDescFixture();
    desc.validatorOptIn = true;
    Submission first(device), second(device), overBudget(device);
    assert(first.prepare(snapshot, desc).success && second.prepare(snapshot, desc).success);
    assert(overBudget.prepare(snapshot, desc).diagnostics == "VEGETATION-SUBMISSION-CAPACITY");
    tests::DelayedValidatorQueue firstQueue, secondQueue;
    tests::TerrainVegetationTransferProbe firstProbe, secondProbe;
    const auto consume = [&](Submission& owner,
                             tests::DelayedValidatorQueue& queue,
                             tests::TerrainVegetationTransferProbe& probe) {
        device.overrideQueue(&queue);
        MeasuredObserver observer(owner);
        render_graph::FrameGraph graph(device);
        probe.addTo(graph, device, owner, false, &observer);
        graph.execute();
        assert(owner.state() == State::Accepted && graph.lastExecutionSubmitted());
        assert(observer.calls == 1u); // Three consuming passes, one completion registration.
        bool rejected = false;
        try {
            graph.execute();
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
    };
    consume(first, firstQueue, firstProbe);
    consume(second, secondQueue, secondProbe);
    snapshot.reset();
    assert(!weak.expired() && desc.liveSubmissions->used() == 2u);
    assert(!first.poll() && !first.cancel());
    assert(!first.drainAndClear({}).success && firstQueue.pending);
    firstQueue.complete();
    const auto before = core::defaultCpuAllocator().stats();
    assert(first.poll() && first.poll());
    assert(before.allocationCount == core::defaultCpuAllocator().stats().allocationCount);
    assert(!weak.expired() && uploadDesc.liveInstances->used() == 4u);
    assert(!second.poll() && desc.liveSubmissions->used() == 1u);
    secondQueue.complete();
    assert(second.poll() && weak.expired());
    assert(!uploadDesc.liveGenerations->used() && !uploadDesc.liveInstances->used());
    assert(!desc.liveSubmissions->used());
}

void failureContracts(rhi::GpuDevice& inner)
{
    // typed rejection, accepted-error, unknown throw, callback record failure,
    // completion factory failure, encoder finish failure.
    for (unsigned mode = 0; mode < 6; ++mode) {
        tests::CapabilityOverrideDevice device(inner, nullptr);
        auto uploadDesc = tests::terrainVegetationUploadDescFixture();
        auto snapshot = upload(device, uploadDesc);
        auto desc = tests::terrainVegetationSubmissionDescFixture();
        desc.validatorOptIn = true;
        Submission submission(device);
        assert(submission.prepare(snapshot, desc).success);
        snapshot.reset();
        tests::DelayedValidatorQueue queue;
        queue.rejected = mode == 0;
        queue.acceptedError = mode == 1;
        queue.throws = mode == 2;
        device.overrideQueue(&queue);
        device.failFenceCreation(mode == 4);
        if (mode == 5)
            device.failComputeRecording(
                "FinishFixture", tests::ComputeRecordFailure::Point::Finish);
        tests::TerrainVegetationTransferProbe probe;
        {
            render_graph::FrameGraph graph(device);
            probe.addTo(graph, device, submission, mode == 3);
            bool failed = false;
            try {
                graph.execute();
            } catch (const std::exception& error) {
                std::printf("Failure fixture mode=%u state=%u diagnostic=%s\n",
                    mode,
                    static_cast<unsigned>(submission.state()),
                    error.what());
                std::fflush(stdout);
                failed = true;
            }
            assert(failed);
            assert(graph.lastExecutionSubmitted() == (mode == 1));
        }
        assert(submission.state() == (mode == 1 ? State::Accepted : State::Attempted));
        assert(!submission.cancel() && !submission.poll());
        assert(uploadDesc.liveInstances->used() == 4u && desc.liveSubmissions->used() == 1u);
        if (mode == 1) {
            queue.complete(); // This callback provided an exact retained completion.
            assert(submission.poll());
        } else {
            // Unknown queue throw destroys FrameGraph's local fence. This
            // Null-only queue stores no executable GPU payload; discard the
            // borrowed pointer without touching it. It is never completion proof.
            queue.pending = nullptr;
            device.failFenceCreation(false);
            device.clearComputeRecordingFailure();
            tests::TerrainVegetationTransferProbe retryProbe;
            render_graph::FrameGraph retry(device);
            retryProbe.addTo(retry, device, submission);
            bool replayRejected = false;
            try {
                retry.execute();
            } catch (const std::exception& error) {
                replayRejected =
                    std::strstr(error.what(), "VEGETATION-SUBMISSION-RECORDING-REPLAY");
            }
            assert(replayRejected && !queue.pending);
            assert(!submission.drainAndClear({}).success);
            ValidatorDrain drain{inner};
            device.overrideShutdownDrain(&drain, ValidatorDrain::run);
            assert(!submission.drainAndClear({}).success && drain.calls == 1u);
            rhi::semantics::ShutdownDrainRequest request;
            request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            assert(submission.drainAndClear(request).success && drain.calls == 2u);
        }
        assert(submission.state() == State::Completed && !submission.snapshot());
        assert(!desc.liveSubmissions->used());
        // retry's imported references (if any) have left scope.
        assert(!uploadDesc.liveInstances->used());
    }
}
} // namespace

int main()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    preparationContracts(*device);
    independentConsumers(*device);
    failureContracts(*device);
    std::puts("Terrain vegetation consuming submission contracts PASS (Null validator only)");
}
