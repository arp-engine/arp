#include "../fixtures/capability_override_device.h"
#include "../fixtures/cloud/cloud_fixture.h"
#include "../fixtures/cloud/cloud_reference_cases.h"
#include "../fixtures/cloud/cloud_scene_fixture.h"
#include "../fixtures/cloud/cloud_sequence_oracle.h"
#include "../fixtures/cloud_native_memory_fixture.h"
#include "core/math/half_float.h"
#include "renderer/volumetric_cloud/cloud_frame_parameters.h"
#include "renderer/volumetric_cloud/cloud_prepared_feature.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_dispatch_validation_session.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_buffer_binding.h"
#include "rhi/backends/metal/metal_original_command_custody.h"
#include "rhi/backends/metal/metal_original_compute_acceptance.h"
#include "rhi/backends/metal/metal_original_graphics_creation.h"
#include "rhi/backends/metal/metal_original_pipeline.h"
#include "rhi/backends/metal/metal_original_texture_binding.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/compute_dispatch_recording.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/frontend/pipeline_synchronous_compute_creation.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "shader/spirv_image_declaration.h"

#import <Metal/Metal.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef NDEBUG
#error Native session tests require assertions
#endif

namespace arp::test {
// Observes cancellation and native commit contention only. It grants no
// original-stream authority and supplies no Source, seal or output proof.
class MetalCommandCancellationFixture final {
    using Native = rhi_metal::MetalRhiNativeCommandPayload;

    static bool drained(const Native& command) noexcept
    {
        return !command.nativeRenderEncoder_ && !command.nativeComputeEncoder_
            && !command.nativeBlitEncoder_ && !command.nativeCommandBuffer_
            && !command.nativeSubmitWaitCommandBuffer_;
    }

    class Probe final : public rhi_metal::MetalOriginalCommandCustody {
    public:
        explicit Probe(const Native& command, unsigned mode)
            : command_(command), mode_(mode), snapshot_(value_)
        {
            assert(snapshot_.prepare(journal_));
            value_ = 19;
        }

        int value() const noexcept { return value_; }

        std::atomic<unsigned> abandoned{0u}, completed{0u};

    private:
        bool beforeSubmit(const Native&, core::pmr::String& diagnostic) noexcept override
        {
            if (mode_ == 3u) {
                diagnostic = "TEST-METAL-CANCELLATION-SUBMIT-REFUSAL";
                return false;
            }
            return true;
        }

        const char* beforeNativeCommit() noexcept override
        {
            return mode_ == 4u ? "TEST-METAL-CANCELLATION-COMMIT-REFUSAL" : nullptr;
        }

        bool nativeCompleted(bool success) noexcept override
        {
            assert(mode_ == 5u && success && !abandoned.load());
            journal_.commit();
            completed.fetch_add(1u, std::memory_order_release);
            return true;
        }

        void abandonUnsubmitted() noexcept override
        {
            assert(drained(command_));
            assert(command_.abandoned_ || command_.recordingPoisoned_
                || command_.readbackWasSubmitted());
            assert(value_ == 19 && !journal_.empty());
            journal_.rollback();
            abandoned.fetch_add(1u, std::memory_order_release);
        }

        const Native& command_;
        const unsigned mode_;
        int value_ = 7;
        core::MutationSnapshot<int> snapshot_;
        core::MutationJournal journal_;
    };

public:
    static void requireNativeResultWithoutOwnerPublication(
        rhi_metal::MetalRhiCompletionState& completion)
    {
        std::unique_lock lock(completion.mutex_);
        assert(completion.condition_.wait_for(lock, std::chrono::seconds(5), [&] {
            return completion.nativeResultReady_;
        }));
        assert(completion.outcome_.load(std::memory_order_acquire)
            == rhi_metal::MetalRhiCompletionState::Outcome::Pending);
        lock.unlock();
        std::thread foreign([&] {
            assert(!completion.isComplete());
            bool refused = false;
            try {
                completion.wait();
            } catch (const std::logic_error& error) {
                refused = !std::strcmp(error.what(), "RHI-SUBMISSION-COMPLETION-THREAD");
            }
            assert(refused);
        });
        foreign.join();
    }

    class CommitGate final : public rhi_metal::MetalOriginalCommandCustody {
    public:
        std::atomic<bool> entered{false}, released{false};
        std::atomic<unsigned> completed{0u}, abandoned{0u};

    private:
        bool beforeSubmit(const Native&, core::pmr::String&) noexcept override { return true; }

        const char* beforeNativeCommit() noexcept override
        {
            entered.store(true, std::memory_order_release);
            // Deliberate test-only CPU pause while a real submit owns the real
            // queue commit lock. No GPU fence or completion is host-signalled.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!released.load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() >= deadline)
                    return "TEST-METAL-COMMIT-CONTENTION-TIMEOUT";
                std::this_thread::yield();
            }
            return nullptr;
        }

        bool nativeCompleted(bool success) noexcept override
        {
            assert(success && released.load(std::memory_order_acquire));
            completed.fetch_add(1u, std::memory_order_release);
            return success;
        }

        void abandonUnsubmitted() noexcept override
        {
            abandoned.fetch_add(1u, std::memory_order_release);
        }
    };

    class CommitHold final {
    public:
        CommitHold(std::shared_ptr<Native> command, std::shared_ptr<CommitGate> gate)
            : command_(std::move(command)), gate_(std::move(gate))
        {
            diagnostic_.reserve(256u);
            worker_ = std::thread([this] {
                accepted_ = command_->submit(
                    stats_, command_->preparedCompletionState(), {}, 0u, {}, 0u, diagnostic_);
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!gate_->entered.load(std::memory_order_acquire)
                && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            assert(gate_->entered.load(std::memory_order_acquire));
        }

        ~CommitHold() { releaseAndWait(); }

        void releaseAndWait()
        {
            if (!worker_.joinable())
                return;
            gate_->released.store(true, std::memory_order_release);
            worker_.join();
            assert(accepted_ && stats_.nativeQueueSubmitCount == 1u);
            command_->preparedCompletionState()->wait();
            assert(gate_->completed.load(std::memory_order_acquire) == 1u
                && !gate_->abandoned.load(std::memory_order_acquire));
        }

    private:
        std::shared_ptr<Native> command_;
        std::shared_ptr<CommitGate> gate_;
        core::pmr::String diagnostic_;
        rhi_metal::MetalRhiNativeQueueStats stats_;
        std::thread worker_;
        bool accepted_ = false;
    };

    static auto holdCommit(const std::shared_ptr<rhi_metal::MetalRhiResourceRegistry>& registry)
    {
        core::pmr::String diagnostic;
        auto command = rhi_metal::createMetalRhiNativeCommandPayload(
            "ActualQueueCommitContention", registry, &diagnostic);
        assert(command && command->finish(diagnostic));
        auto gate = core::makeShared<CommitGate>();
        command->originalCommandCustody_ = gate;
        return core::makeUnique<CommitHold>(std::move(command), std::move(gate));
    }

    static void run(rhi::GpuDevice& device)
    {
        auto registry = core::makeShared<rhi_metal::MetalRhiResourceRegistry>();
        rhi::TextureDesc td;
        td.label = "CancellationScopeTarget";
        td.extent = {4u, 4u, 1u};
        td.format = rhi::TextureFormat::Rgba16Float;
        td.usage = rhi::TextureUsage::RenderAttachment;
        td.initialState = rhi::ResourceState::ColorAttachment;
        auto target = device.createTexture(td);
        assert(target);
        registry->registerTexture(*target);
        rhi::RenderPassDesc pass;
        pass.colorAttachments.push_back({"CancellationScopeColor",
            target->id(),
            td.format,
            rhi::LoadOp::Clear,
            rhi::StoreOp::Store});

        // Actual render, compute and blit encoders; active cancellation,
        // recording poison, early and late submit refusals, and real completion.
        for (unsigned scope = 0u; scope < 3u; ++scope)
            for (unsigned mode = 0u; mode < 6u; ++mode) {
                core::pmr::String diagnostic;
                diagnostic.reserve(256u);
                auto command = rhi_metal::createMetalRhiNativeCommandPayload(
                    "CancellationScope", registry, &diagnostic);
                assert(command && command->stats()->nativeRuntimeBacked);
                auto probe = core::makeShared<Probe>(*command, mode);
                command->originalCommandCustody_ = probe;
                assert(scope == 0u    ? command->recordRenderPassBegin(pass, diagnostic)
                        : scope == 1u ? command->recordComputePassBegin(diagnostic)
                                      : command->recordBlitPassBegin(diagnostic));
                auto completion = command->preparedCompletionState();
                assert(completion);
                rhi_metal::MetalRhiNativeQueueStats stats;
                if (mode >= 3u) {
                    assert(scope == 0u    ? command->recordRenderPassEnd(diagnostic)
                            : scope == 1u ? command->recordComputePassEnd(diagnostic)
                                          : command->recordBlitPassEnd(diagnostic));
                    assert(command->finish(diagnostic));
                }
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                if (mode == 0u)
                    command->abandonUnsubmitted();
                else if (mode == 1u)
                    command->poisonRecording("TEST-METAL-CANCELLATION-POISON");
                else if (mode == 5u) {
                    auto gate =
                        rhi_metal::createMetalRhiNativeSharedEventPayload("CancellationGate");
                    assert(gate && gate->nativeRuntimeBacked);
                    // Keep the original command's external-sync profile closed.
                    // A preceding real command waits on this queue instead.
                    auto blocker = rhi_metal::createMetalRhiNativeCommandPayload(
                        "CancellationBlocker", registry, &diagnostic);
                    assert(blocker && blocker->finish(diagnostic));
                    auto blockerCompletion = blocker->preparedCompletionState();
                    rhi_metal::MetalRhiNativeQueueStats blockerStats;
                    assert(blocker->submit(
                        blockerStats, blockerCompletion, gate, 1u, {}, 0u, diagnostic));
                    assert(command->submit(stats, completion, {}, 0u, {}, 0u, diagnostic));
                    assert(!completion->isComplete() && !probe->completed.load());
                    command->poisonRecording("TEST-METAL-LATE-POISON");
                    command->abandonUnsubmitted();
                    assert(!probe->abandoned.load() && probe->value() == 19
                        && !command->recordingPoisoned());
                    rhi_metal::signalMetalRhiNativeSharedEvent(gate, 1u);
                    completion->wait();
                    blockerCompletion->wait();
                    assert(completion->outcome()
                        == rhi_metal::MetalRhiCompletionState::Outcome::Completed);
                    assert(probe->completed.load(std::memory_order_acquire) == 1u);
                    assert(probe->value() == 19 && !probe->abandoned.load());
                } else
                    assert(!command->submit(stats, completion, {}, 0u, {}, 0u, diagnostic));
                if (mode < 5u) {
                    assert(drained(*command) && probe->abandoned.load() == 1u
                        && !probe->completed.load() && probe->value() == 7);
                    command->abandonUnsubmitted();
                    command->poisonRecording("TEST-METAL-REPEATED-CANCELLATION");
                    assert(probe->abandoned.load() == 1u);
                    assert(!command->submit(stats, completion, {}, 0u, {}, 0u, diagnostic));
                    assert(stats.nativeQueueSubmitCount == 0u);
                    if (mode < 2u)
                        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                }
                command.reset();
                assert(probe->abandoned.load() == (mode == 5u ? 0u : 1u));
            }
        std::puts(
            "METAL_NATIVE_CANCELLATION_ORDER_PASS scopes=3 modes=6 journalRollbackAfterNativeDrain=true repeatedCancellationOnce=true submittedOwnershipPreserved=true cloudFrames=0");
    }
};

class MetalDispatchValidationSessionFixture final {
public:
    using Session = rhi_metal::MetalDispatchValidationSession;

    static auto prepare(rhi::GpuDevice& device,
        const rhi::ComputeDispatchValidationRequest& request,
        const Session::Budget& budget,
        Session::Quotas quotas,
        std::shared_ptr<rhi_metal::MetalRhiNativeSharedEventPayload> gate,
        const char*& diagnostic)
    {
        return Session::prepareImpl(
            device, request, budget, std::move(quotas), std::move(gate), diagnostic);
    }

    static auto retained(const Session& session) { return session.retainedGraphForTest(); }

    static bool corrupt(Session& session) { return session.corruptCompletedOutputForTest(); }

    static auto wrap(std::shared_ptr<Session> session, const char*& diagnostic)
    {
        return Session::wrapL2(std::move(session), diagnostic);
    }
};
} // namespace arp::test

namespace {
using namespace arp;
using Session = rhi_metal::MetalDispatchValidationSession;
using Fixture = test::MetalDispatchValidationSessionFixture;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t metadata = 4u * 1024u * 1024u;
const rhi::ComputeDispatchValidationDomainBudget domainBudget{1u, true, metadata};
const Session::Budget budget{1u,
    true,
    1u,
    metadata,
    8u * 1024u * 1024u,
    metadata,
    metadata * 8u,
    metadata,
    metadata,
    metadata,
    metadata};

Session::Quotas quotas(const Session::Budget& b = budget)
{
    return {core::makeShared<core::CpuMemoryQuota>(b.maxCpuMetadataBytes),
        core::makeShared<core::CpuMemoryQuota>(b.maxArtifactCpuBytes),
        core::makeShared<core::CpuMemoryQuota>(b.maxInputCpuBytes),
        core::makeShared<core::CpuMemoryQuota>(b.maxPipelineCpuBytes)};
}

rhi::ComputeDispatchValidationRequest request(rhi::Extent3D domain = {3u, 2u, 3u})
{
    return {1u,
        true,
        {8u, 8u, 1u},
        domain,
        {5207040u, 14400u, 52743600u, 3375590400u, 208281600u, 160u, 30000000000u}};
}

auto device(bool armNativeLoss = true,
    bool creationBudget = false,
    std::uint32_t descriptorCapacity = 1u,
    std::uint32_t pipelineCapacity = 2u)
{
    rhi_metal::MetalRhiDeviceDesc desc;
    desc.label = "DispatchValidationSession";
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.uploadStagingPageSizeBytes = 4096u;
    desc.uploadStagingMaxPageCount = 1u;
    if (creationBudget)
        desc.directObjectBudget = {
            1u, "MetalOriginalComputeCreation/1", pipelineCapacity, descriptorCapacity};
    desc.capabilityContract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice =
        bool(rhi_metal::metalRhiNativeDeviceIdentity());
    desc.capabilityContract.queriedExtendedFeatures = true;
    auto result = rhi_metal::createMetalRhiDevice(desc);
    assert(result);
    assert(!result->prepareOriginalDeviceLifetime(
        {1u, true, 1u, 2u, metadata}, core::makeShared<core::CpuMemoryQuota>(metadata)));
    if (armNativeLoss)
        assert(!result->prepareOriginalDeviceNativeResultLoss(
            {1u, true, 1u, 2u, metadata}, core::makeShared<core::CpuMemoryQuota>(metadata)));
    return result;
}

template <class Predicate> void until(Predicate&& done, const char* stage = "predicate")
{
    const auto start = Clock::now();
    while (!done()) {
        if (Clock::now() - start >= std::chrono::seconds(40)) {
            std::fprintf(stderr, "native session wait failed: %s\n", stage);
            assert(false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void clean(const Session::Quotas& q)
{
    until(
        [&] {
            return !q.session->liveBytes() && !q.artifact->liveBytes() && !q.input->liveBytes()
                && !q.pipeline->liveBytes();
        },
        "CPU quota drain");
}

std::uint64_t successful(rhi::Extent3D domain)
{
    auto d = device();
    const auto q = quotas();
    const char* diagnostic = nullptr;
    auto session = Session::prepare(*d, request(domain), budget, q, diagnostic);
    if (diagnostic)
        std::fprintf(stderr, "session prepare: %s\n", diagnostic);
    assert(session && !diagnostic);
    auto initial = session->observe();
    assert(initial.pending && !initial.verified
        && initial.retainedNativeReservationBytes == budget.maxNativeAllocationBytes);
    assert(initial.actualNativeAllocationBytes
        && initial.actualNativeAllocationBytes <= budget.maxNativeAllocationBytes);
    until([&] {
        assert(!session->advance());
        return session->observe().verified;
    });
    const auto done = session->observe();
    assert(!done.pending && !done.diagnostic && done.checkedRows == domain.width);
    until([&] {
        return !session->observe().retainedNativeReservationBytes;
    });
    for (unsigned i = 0u; i < 10u; ++i)
        assert(!session->advance() && session->observe().verified);
    const auto domainQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    auto coverage = session->issueDomain(*d, domainBudget, domainQuota, diagnostic);
    assert(coverage && !diagnostic && !coverage->executionAvailable());
    const auto generic = d->capabilities().computeResources;
    assert(!generic.maxComputeWorkgroupCountX && !generic.maxComputeWorkgroupCountY
        && !generic.maxComputeWorkgroupCountZ);
    // Both L2 devices share the same native M4 singleton. The source has one
    // explicit subscriber, so the foreign domain does not arm a second one.
    auto foreign = device(false);
    const auto fakeOwner = std::shared_ptr<const rhi::ComputeDispatchValidationDomain>(
        coverage.get(), [](const auto*) {});
    tests::CapabilityOverrideDevice proxy(*d, [](rhi::DeviceCapabilities& caps) {
        caps.computeResources.maxComputeWorkgroupCountX = UINT32_MAX;
        caps.computeResources.maxComputeWorkgroupCountY = UINT32_MAX;
        caps.computeResources.maxComputeWorkgroupCountZ = UINT32_MAX;
    });
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(proxy.validateOriginalComputeDispatchDomain(coverage, {8u, 8u, 1u}, domain));
    assert(!session->issueDomain(proxy, domainBudget, domainQuota, diagnostic));
    assert(!std::strcmp(diagnostic, "RHI-DISPATCH-SESSION-ORIGINAL-DEVICE"));
    assert(d->validateOriginalComputeDispatchDomain({}, {8u, 8u, 1u}, domain));
    assert(d->validateOriginalComputeDispatchDomain(fakeOwner, {8u, 8u, 1u}, domain));
    assert(foreign->validateOriginalComputeDispatchDomain(coverage, {8u, 8u, 1u}, domain));
    assert(!session->issueDomain(*foreign, domainBudget, domainQuota, diagnostic));
    assert(!std::strcmp(diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE"));
    for (std::uint32_t z = 1u; z <= domain.depth; ++z)
        for (std::uint32_t y = 1u; y <= domain.height; ++y)
            for (std::uint32_t x = 1u; x <= domain.width; ++x)
                assert(
                    !d->validateOriginalComputeDispatchDomain(coverage, {8u, 8u, 1u}, {x, y, z}));
    for (unsigned axis = 0u; axis < 3u; ++axis) {
        auto outside = domain;
        auto& value = axis == 0u ? outside.width : axis == 1u ? outside.height : outside.depth;
        ++value;
        assert(d->validateOriginalComputeDispatchDomain(coverage, {8u, 8u, 1u}, outside));
        value = 0u;
        assert(d->validateOriginalComputeDispatchDomain(coverage, {8u, 8u, 1u}, outside));
    }
    assert(d->validateOriginalComputeDispatchDomain(coverage, {4u, 16u, 1u}, domain));
    assert(!d->validateOriginalComputeDispatchDomain(coverage, {8u, 8u, 1u}, domain));
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    std::printf(
        "METAL_DISPATCH_SESSION_COMPLETE grid=%u/%u/%u rows=%u allocationBytes=%llu reservationDrained=true domainIssued=true cloudFrames=0\n",
        domain.width,
        domain.height,
        domain.depth,
        done.checkedRows,
        static_cast<unsigned long long>(done.actualNativeAllocationBytes));
    session.reset();
    assert(!coverage->diagnostic() && q.session->liveBytes() && domainQuota->liveBytes());
    assert(!d->validateOriginalComputeDispatchDomain(coverage, {8u, 8u, 1u}, domain));
    coverage.reset();
    assert(!domainQuota->liveBytes());
    clean(q);
    return done.actualNativeAllocationBytes;
}

void pending(unsigned mode)
{
    auto d = device();
    auto intent = request();
    if (mode == 0u)
        intent.budget.maxColdValidationNanoseconds = 2000000000u;
    const auto q = quotas();
    auto gate = rhi_metal::createMetalRhiNativeSharedEventPayload("DispatchValidationDelay");
    assert(gate && gate->nativeRuntimeBacked);
    const char* diagnostic = nullptr;
    auto session = Fixture::prepare(*d, intent, budget, q, gate, diagnostic);
    if (diagnostic)
        std::fprintf(stderr, "gated prepare: %s\n", diagnostic);
    assert(session && !diagnostic);
    auto retained = Fixture::retained(*session);
    assert(!retained.expired() && session->observe().pending);
    const auto domainQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto beforeIssue = core::defaultCpuAllocator().stats().allocationCount;
    assert(!session->issueDomain(*d, domainBudget, domainQuota, diagnostic));
    assert(!std::strcmp(diagnostic, "RHI-DISPATCH-DOMAIN-INCOMPLETE"));
    assert(beforeIssue == core::defaultCpuAllocator().stats().allocationCount);
    auto secondQuotas = quotas();
    const auto beforeRetry = core::defaultCpuAllocator().stats().allocationCount;
    assert(!Session::prepare(*d, intent, budget, secondQuotas, diagnostic));
    assert(diagnostic && !std::strcmp(diagnostic, "RHI-DISPATCH-SESSION-DEVICE-BUSY"));
    assert(beforeRetry == core::defaultCpuAllocator().stats().allocationCount);
    clean(secondQuotas);
    if (mode == 0u) {
        until([&] {
            return session->advance() != nullptr;
        });
        assert(!std::strcmp(session->observe().diagnostic, "RHI-DISPATCH-VALIDATION-TIMEOUT"));
    } else if (mode == 1u) {
        assert(!std::strcmp(session->cancel(), "RHI-DISPATCH-SESSION-CANCELLED"));
    } else if (mode == 2u) {
        d->retireOriginalDeviceLifetime();
        assert(session->advance());
    }
    assert(session->observe().pending && !session->observe().verified);
    assert(session->observe().retainedNativeReservationBytes == budget.maxNativeAllocationBytes);
    if (mode == 3u || mode == 4u) {
        // The facade AND L2 device may disappear while native work is pending.
        session.reset();
        if (mode == 3u)
            d.reset();
        assert(!retained.expired() && q.session->liveBytes() && q.pipeline->liveBytes());
        if (d) {
            assert(!Session::prepare(*d, request(), budget, secondQuotas, diagnostic));
            assert(diagnostic && !std::strcmp(diagnostic, "RHI-DISPATCH-SESSION-DEVICE-BUSY"));
        }
    }
    rhi_metal::signalMetalRhiNativeSharedEvent(gate, 1u);
    if (session) {
        until([&] {
            session->advance();
            return !session->observe().pending;
        });
        assert(session->observe().diagnostic && !session->observe().verified);
        const auto beforeFailureIssue = core::defaultCpuAllocator().stats().allocationCount;
        assert(!session->issueDomain(*d, domainBudget, domainQuota, diagnostic));
        assert(beforeFailureIssue == core::defaultCpuAllocator().stats().allocationCount);
        for (unsigned i = 0u; i < 10u; ++i)
            assert(session->advance() && !session->observe().pending);
        until([&] {
            return !session->observe().retainedNativeReservationBytes;
        });
        session.reset();
        if (mode < 2u) {
            // Only actual completion/last native owner returns device admission.
            auto retry = Session::prepare(*d, request(), budget, secondQuotas, diagnostic);
            assert(retry && !diagnostic);
            until([&] {
                assert(!retry->advance());
                return retry->observe().verified;
            });
            retry.reset();
            clean(secondQuotas);
        }
    }
    until(
        [&] {
            return retained.expired();
        },
        "native graph release");
    // An expired weak observation still retains its allocator control block.
    // Drop that CPU metadata witness before asserting the entire quota is empty.
    retained.reset();
    clean(q);
    std::printf(
        "METAL_DISPATCH_SESSION_RETAIN_PASS mode=%u pendingBeforeSignal=true retainedThroughCompletion=true quotasDrained=true noRearm=true\n",
        mode);
}

void negatives(std::uint64_t observed720pAllocation)
{
    auto d = device();
    const char* diagnostic = nullptr;
    auto q = quotas();
    auto invalid = q;
    invalid.input = q.artifact;
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    assert(!Session::prepare(*d, request(), budget, invalid, diagnostic) && diagnostic);
    assert(before == core::defaultCpuAllocator().stats().allocationCount);
    q.session->setLimitBytes(metadata - 1u);
    assert(!Session::prepare(*d, request(), budget, q, diagnostic) && diagnostic);
    q.session->setLimitBytes(metadata);
    assert(q.session->tryReserve(metadata));
    assert(!Session::prepare(*d, request(), budget, q, diagnostic));
    assert(diagnostic && !std::strcmp(diagnostic, "RHI-DISPATCH-SESSION-CPU-ALLOCATION"));
    q.session->release(metadata);
    clean(q);
    // Actual driver rounding exceeds this valid logical result-size ceiling.
    auto small = budget;
    // Use the queried 720p allocation, whose rounding was just observed. The
    // small 3D result has an exact 1536-byte allocation on this device.
    assert(observed720pAllocation > 5207040u);
    small.maxNativeAllocationBytes = observed720pAllocation - 1u;
    q = quotas(small);
    assert(!Session::prepare(*d, request({160u, 90u, 1u}), small, q, diagnostic));
    assert(diagnostic && !std::strcmp(diagnostic, "RHI-DISPATCH-SESSION-NATIVE-ALLOCATION"));
    clean(q);
    q = quotas();
    auto session = Session::prepare(*d, request(), budget, q, diagnostic);
    assert(session && !diagnostic);
    until([&] {
        return Fixture::corrupt(*session);
    });
    assert(session->advance());
    assert(!std::strcmp(session->observe().diagnostic, "RHI-DISPATCH-SESSION-RESULT-MISMATCH"));
    assert(!session->observe().verified && !session->observe().checkedRows);
    const auto domainQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto beforeCorruptIssue = core::defaultCpuAllocator().stats().allocationCount;
    assert(!session->issueDomain(*d, domainBudget, domainQuota, diagnostic));
    assert(!std::strcmp(diagnostic, "RHI-DISPATCH-SESSION-RESULT-MISMATCH"));
    assert(beforeCorruptIssue == core::defaultCpuAllocator().stats().allocationCount);
    session.reset();
    clean(q);
    std::puts(
        "METAL_DISPATCH_SESSION_NEGATIVE_PASS quota-overlap/mutation/exhaustion/native-rounding/corrupt-real-results");
}

void domainRevocation(unsigned mode)
{
    auto d = device();
    const auto q = quotas();
    const auto domainQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const char* diagnostic = nullptr;
    auto session = Session::prepare(*d, request(), budget, q, diagnostic);
    assert(session && !diagnostic);
    until([&] {
        assert(!session->advance());
        return session->observe().verified;
    });
    // Invalid issuance requests never consume successful proof or domain quota.
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    assert(!session->issueDomain(*d, {0u, true, metadata}, domainQuota, diagnostic));
    assert(!session->issueDomain(*d, {1u, false, metadata}, domainQuota, diagnostic));
    assert(!session->issueDomain(*d, domainBudget, {}, diagnostic));
    for (const auto& original : {q.session, q.artifact, q.input, q.pipeline})
        assert(!session->issueDomain(*d, domainBudget, original, diagnostic)
            && !std::strcmp(diagnostic, "RHI-DISPATCH-DOMAIN-QUOTA-OVERLAP"));
    assert(before == core::defaultCpuAllocator().stats().allocationCount);
    assert(domainQuota->tryReserve(metadata));
    assert(!session->issueDomain(*d, domainBudget, domainQuota, diagnostic));
    assert(!std::strcmp(diagnostic, "RHI-DISPATCH-DOMAIN-CPU-ALLOCATION"));
    domainQuota->release(metadata);
    assert(!domainQuota->liveBytes());
    auto domain = session->issueDomain(*d, domainBudget, domainQuota, diagnostic);
    assert(domain && !diagnostic && !domain->diagnostic());
    // Polling remains owner-thread-only. A published proof can be consumed
    // after a serialized handoff, as it is by actual native completion.
    std::thread proofConsumer([&] {
        assert(!std::strcmp(session->advance(), "RHI-DISPATCH-SESSION-THREAD"));
        assert(!std::strcmp(session->observe().diagnostic, "RHI-DISPATCH-SESSION-THREAD"));
        assert(!std::strcmp(session->cancel(), "RHI-DISPATCH-SESSION-THREAD"));
        assert(!domain->diagnostic());
    });
    proofConsumer.join();
    std::shared_ptr<core::CpuMemoryQuota> changed;
    if (mode == 0u)
        changed = domainQuota;
    else if (mode < 5u) {
        const std::shared_ptr<core::CpuMemoryQuota> originals[]{
            q.session, q.artifact, q.input, q.pipeline};
        changed = originals[mode - 1u];
    } else if (mode == 5u)
        assert(session->cancel());
    else if (mode == 6u)
        d->retireOriginalDeviceLifetime();
    else if (mode == 7u)
        d.reset();
    else if (mode == 8u)
        assert(!d->detachOriginalDeviceNativeLoss());
    else
        changed = mode == 9u ? q.session : q.input;
    if (changed)
        changed->setLimitBytes(metadata - 1u);
    const auto beforeValidation = core::defaultCpuAllocator().stats().allocationCount;
    if (mode == 9u) {
        assert(session->observe().diagnostic);
        changed->setLimitBytes(metadata);
        assert(session->observe().diagnostic && !session->observe().verified);
    } else if (mode == 10u) {
        assert(!session->issueDomain(*d, domainBudget, domainQuota, diagnostic));
        changed->setLimitBytes(metadata);
    }
    const auto* first = domain->diagnostic();
    assert(first);
    if (mode == 6u)
        assert(!std::strcmp(first, "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"));
    if (mode == 7u)
        assert(!std::strcmp(first, "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN"));
    if (changed)
        changed->setLimitBytes(metadata);
    for (unsigned repeat = 0u; repeat < 10u; ++repeat) {
        assert(domain->diagnostic() == first);
        if (d) {
            assert(
                d->validateOriginalComputeDispatchDomain(domain, {8u, 8u, 1u}, request().groups));
            if (mode != 0u)
                assert(!session->issueDomain(*d, domainBudget, domainQuota, diagnostic));
        }
    }
    assert(beforeValidation == core::defaultCpuAllocator().stats().allocationCount);
    if (mode == 0u) {
        // Domain-local quota failure cannot rearm that domain, but does not
        // rewrite the successful immutable GPU proof. Reissue is explicit.
        const auto freshQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        auto fresh = session->issueDomain(*d, domainBudget, freshQuota, diagnostic);
        assert(fresh && !diagnostic && !fresh->diagnostic() && domain->diagnostic() == first);
        fresh.reset();
        assert(!freshQuota->liveBytes());
    }
    domain.reset();
    session.reset();
    assert(!domainQuota->liveBytes());
    clean(q);
    std::printf(
        "METAL_DISPATCH_DOMAIN_REVOKE_PASS mode=%u irreversible=true allocation0=true quotasDrained=true\n",
        mode);
}

void l2Preparation(rhi::Extent3D groups)
{
    auto d = device();
    const auto q = quotas();
    const auto domainQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const char* diagnostic = nullptr;
    auto prepared =
        d->prepareOriginalComputeDispatchValidation(request(groups), budget, q, diagnostic);
    assert(prepared && !diagnostic && prepared->observe().pending && !prepared->observe().verified);
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    assert(!d->issueOriginalComputeDispatchDomain(prepared, domainBudget, domainQuota, diagnostic));
    assert(!std::strcmp(diagnostic, "RHI-DISPATCH-DOMAIN-INCOMPLETE"));
    assert(before == core::defaultCpuAllocator().stats().allocationCount);
    until([&] {
        assert(!prepared->advance());
        return prepared->observe().verified;
    });
    auto foreign = device(false);
    tests::CapabilityOverrideDevice proxy(*d, nullptr);
    auto null = rhi::createNullDevice();
    const auto counterfeit =
        std::shared_ptr<rhi::OriginalComputeDispatchValidation>(prepared.get(), [](auto*) {});
    const auto afterComplete = core::defaultCpuAllocator().stats().allocationCount;
    assert(!d->issueOriginalComputeDispatchDomain({}, domainBudget, domainQuota, diagnostic));
    assert(
        !d->issueOriginalComputeDispatchDomain(counterfeit, domainBudget, domainQuota, diagnostic));
    assert(!foreign->issueOriginalComputeDispatchDomain(
        prepared, domainBudget, domainQuota, diagnostic));
    assert(
        !proxy.issueOriginalComputeDispatchDomain(prepared, domainBudget, domainQuota, diagnostic));
    assert(!null->prepareOriginalComputeDispatchValidation(request(), budget, q, diagnostic));
    assert(!std::strcmp(diagnostic, "RHI-DISPATCH-VALIDATION-VALIDATOR-ONLY"));
    assert(afterComplete == core::defaultCpuAllocator().stats().allocationCount);
    assert(prepared->observe().verified); // Foreign rejections cannot cancel the original.
    auto domain =
        d->issueOriginalComputeDispatchDomain(prepared, domainBudget, domainQuota, diagnostic);
    assert(domain && !diagnostic
        && !d->validateOriginalComputeDispatchDomain(domain, {8u, 8u, 1u}, groups));
    prepared.reset();
    assert(!domain->diagnostic() && q.session->liveBytes());
    assert(!d->validateOriginalComputeDispatchDomain(domain, {8u, 8u, 1u}, groups));
    domain.reset();
    clean(q);
    assert(!domainQuota->liveBytes());
    std::printf(
        "L2_DISPATCH_PREPARATION_COMPLETE grid=%u/%u/%u sameDeviceDomain=true facadeDropped=true cloudFrames=0\n",
        groups.width,
        groups.height,
        groups.depth);
}

void l2Delayed(unsigned mode)
{
    auto d = device();
    const auto q = quotas();
    auto intent = request();
    if (mode == 3u)
        intent.budget.maxColdValidationNanoseconds = 2000000000u;
    auto gate = rhi_metal::createMetalRhiNativeSharedEventPayload("L2DispatchDelay");
    const char* diagnostic = nullptr;
    auto native = Fixture::prepare(*d, intent, budget, q, gate, diagnostic);
    assert(native && !diagnostic);
    auto retained = Fixture::retained(*native);
    std::uint64_t fill = 0u;
    if (mode == 2u) {
        fill = q.session->limitBytes() - q.session->liveBytes();
        assert(fill && q.session->tryReserve(fill));
    }
    auto prepared = Fixture::wrap(native, diagnostic);
    if (mode == 2u) {
        assert(!prepared && !std::strcmp(diagnostic, "RHI-DISPATCH-PREPARATION-CPU-ALLOCATION"));
        q.session->release(fill);
        assert(native->observe().pending && !native->observe().verified
            && native->observe().diagnostic);
    } else {
        assert(prepared && !diagnostic && prepared->observe().pending);
        if (mode == 3u) {
            until([&] {
                return prepared->advance() != nullptr;
            });
            assert(!std::strcmp(prepared->observe().diagnostic, "RHI-DISPATCH-VALIDATION-TIMEOUT"));
        } else if (mode == 4u) {
            assert(!std::strcmp(prepared->cancel(), "RHI-DISPATCH-SESSION-CANCELLED"));
            assert(prepared->observe().pending && !prepared->observe().verified);
        } else if (mode == 5u) {
            d->retireOriginalDeviceLifetime();
            assert(prepared->advance());
            assert(!std::strcmp(
                prepared->observe().diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"));
        } else if (mode == 6u) {
            d.reset();
            assert(prepared->advance());
            assert(!std::strcmp(
                prepared->observe().diagnostic, "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN"));
        }
    }
    native.reset();
    prepared.reset();
    if (mode == 1u)
        d.reset();
    assert(!retained.expired() && q.session->liveBytes() && q.pipeline->liveBytes());
    if (d && mode != 5u) {
        const auto other = quotas();
        const auto beforeRetry = core::defaultCpuAllocator().stats().allocationCount;
        assert(!d->prepareOriginalComputeDispatchValidation(request(), budget, other, diagnostic));
        assert(!std::strcmp(diagnostic, "RHI-DISPATCH-SESSION-DEVICE-BUSY"));
        assert(beforeRetry == core::defaultCpuAllocator().stats().allocationCount);
    }
    rhi_metal::signalMetalRhiNativeSharedEvent(gate, 1u);
    until([&] {
        return retained.expired();
    });
    retained.reset();
    clean(q);
    std::printf(
        "L2_DISPATCH_PREPARATION_RETAIN_PASS mode=%u nativeCompletionOnly=true quotasDrained=true\n",
        mode);
}

void cloudAdmission(rhi::Extent3D groups)
{
    using namespace renderer::cloud;
    const auto require = [](CloudResult result) {
        if (!result.success)
            std::fprintf(stderr, "cloud admission: %s\n", result.diagnostic);
        assert(result.success);
    };
    const auto rejects = [](CloudResult result, const char* expected) {
        if (result.success || std::strcmp(result.diagnostic, expected))
            std::fprintf(stderr,
                "cloud expected %s, got %s\n",
                expected,
                result.success ? "success" : result.diagnostic);
        assert(!result.success && !std::strcmp(result.diagnostic, expected));
    };
    auto d = device();
    const auto q = quotas();
    auto domainQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const bool large = groups.width == 160u;
    {
        const char* diagnostic = nullptr;
        auto prepared =
            d->prepareOriginalComputeDispatchValidation(request(groups), budget, q, diagnostic);
        assert(prepared && !diagnostic);
        until([&] {
            assert(!prepared->advance());
            return prepared->observe().verified;
        });
        auto domain =
            d->issueOriginalComputeDispatchDomain(prepared, domainBudget, domainQuota, diagnostic);
        assert(domain && !diagnostic && !domain->executionAvailable());
        const auto caps = d->capabilities();
        const auto limits = caps.computeResources;
        assert(!limits.maxComputeWorkgroupCountX && !limits.maxComputeWorkgroupCountY
            && !limits.maxComputeWorkgroupCountZ);
        const auto asset = tests::cloud::asset();
        auto config = tests::cloud::config();
        config.resolutionDivisor = 2u;
        const CloudPreparedResourceRequest resources{config,
            large ? rhi::Extent2D{1280u, 720u} : rhi::Extent2D{48u, 32u},
            asset.cells.size() * sizeof(core::Vec4),
            9u,
            45u};
        const CloudPreparedPipelineSelection temporal{1u, CloudPreparedResolve::Temporal1, {}};
        const CloudPreparedPipelineSelection spatial{
            1u, CloudPreparedResolve::Reconstruction1, {1u, .025f, .025f}};
        const CloudOriginalDispatchAdmission admission{*d, domain};
        CloudPreparedResourcePlan plan;
        const auto beforePreflight = core::defaultCpuAllocator().stats().allocationCount;
        rejects(CloudPreparedPipelines::planResources(temporal, resources, limits, plan),
            "CLOUD-DISPATCH-LIMIT");
        require(
            CloudPreparedPipelines::planResources(temporal, resources, limits, admission, plan));
        assert(plan.raw.dispatchGroups.width == (large ? 80u : 3u)
            && plan.raw.dispatchGroups.height == (large ? 45u : 2u));
        const auto reconstruction =
            CloudPreparedPipelines::planResources(spatial, resources, limits, admission, plan);
        if (large) {
            require(reconstruction);
            assert(plan.reconstruction.dispatchGroups.width == 160u
                && plan.reconstruction.dispatchGroups.height == 90u);
            for (unsigned mutation = 0u; mutation < 5u; ++mutation) {
                auto bounded = limits;
                auto requested = resources;
                const char* expected = nullptr;
                switch (mutation) {
                case 0:
                    bounded.maxStorageBufferBindingsPerStage = 3u;
                    expected = "CLOUD-RECONSTRUCTION-BINDING-LIMIT";
                    break;
                case 1:
                    bounded.maxUniformBufferBindingsPerStage = 1u;
                    expected = "CLOUD-RECONSTRUCTION-BINDING-LIMIT";
                    break;
                case 2:
                    bounded.maxStorageBufferBindingSizeBytes = plan.raw.sampleBufferBytes;
                    expected = "CLOUD-RECONSTRUCTION-STORAGE-LIMIT";
                    break;
                case 3:
                    requested.config.maxDensityEvaluations = plan.raw.densityEvaluationBound;
                    expected = "CLOUD-RECONSTRUCTION-WORK-BUDGET";
                    break;
                case 4:
                    requested.config.gpuBudgetBytes = plan.raw.retainedGpuBytes;
                    expected = "CLOUD-RECONSTRUCTION-MEMORY-BUDGET";
                    break;
                }
                CloudPreparedResourcePlan rejected;
                rejects(CloudPreparedPipelines::planResources(
                            spatial, requested, bounded, admission, rejected),
                    expected);
                assert(!rejected.raw.sampleCount && !rejected.reconstruction.resolvedCount);
            }
        } else {
            rejects(reconstruction, "RHI-DISPATCH-VALIDATION-OUTSIDE-DOMAIN");
            assert(!plan.raw.sampleCount && !plan.reconstruction.resolvedCount
                && !plan.descriptorSets);
        }
        for (unsigned axis = 0u; axis < 2u; ++axis) {
            auto outside = resources;
            // Keep the other axis inside: coverage is per-axis, not total group count.
            outside.extent = axis ? rhi::Extent2D{1u, groups.height * 16u + 1u}
                                  : rhi::Extent2D{groups.width * 16u + 1u, 1u};
            rejects(
                CloudPreparedPipelines::planResources(temporal, outside, limits, admission, plan),
                "RHI-DISPATCH-VALIDATION-OUTSIDE-DOMAIN");
            assert(!plan.raw.sampleCount && !plan.descriptorSets);
        }
        for (unsigned mutation = 0u; mutation < 11u; ++mutation) {
            auto bounded = limits;
            auto requested = resources;
            const char* expected = nullptr;
            switch (mutation) {
            case 0:
                bounded.maxComputeWorkgroupSizeX = 7u;
                expected = "CLOUD-COMPUTE-LIMIT";
                break;
            case 1:
                bounded.maxStorageBufferBindingsPerStage = 2u;
                expected = "CLOUD-COMPUTE-LIMIT";
                break;
            case 2:
                bounded.maxUniformBufferBindingsPerStage = 0u;
                expected = "CLOUD-UNIFORM-LIMIT";
                break;
            case 3:
                bounded.maxUniformBufferBindingSizeBytes = sizeof(CloudGpuConstants) - 1u;
                expected = "CLOUD-UNIFORM-LIMIT";
                break;
            case 4:
                bounded.maxSampledTextureBindingsPerStage = 1u;
                expected = "CLOUD-TEXTURE-LIMIT";
                break;
            case 5:
                bounded.maxTexture2DDimension = resources.extent.width - 1u;
                expected = "CLOUD-TEXTURE-LIMIT";
                break;
            case 6:
                bounded.maxStorageBufferBindingSizeBytes = 1u;
                expected = "CLOUD-STORAGE-LIMIT";
                break;
            case 7:
                requested.config.maxDensityEvaluations = 1u;
                expected = "CLOUD-WORK-BUDGET";
                break;
            case 8:
                requested.config.gpuBudgetBytes = 1u;
                expected = "CLOUD-MEMORY-BUDGET";
                break;
            case 9:
                requested.maxDescriptorElements = 1u;
                expected = "CLOUD-PREPARED-PIPELINE-DESCRIPTOR-BUDGET";
                break;
            case 10:
                bounded.mode = rhi::CapabilityMode::Degraded;
                expected = "CLOUD-CAPABILITY-UNAVAILABLE";
                break;
            }
            rejects(CloudPreparedPipelines::planResources(
                        temporal, requested, bounded, admission, plan),
                expected);
            assert(!plan.raw.sampleCount && !plan.descriptorElements);
        }
        assert(beforePreflight == core::defaultCpuAllocator().stats().allocationCount);
        auto foreign = device(false);
        tests::CapabilityOverrideDevice proxy(*d, nullptr);
        const auto counterfeit =
            std::shared_ptr<const rhi::ComputeDispatchValidationDomain>(domain.get(), [](auto*) {});
        const auto beforeForeign = core::defaultCpuAllocator().stats().allocationCount;
        rejects(CloudPreparedPipelines::planResources(
                    temporal, resources, limits, {*foreign, domain}, plan),
            "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
        rejects(CloudPreparedPipelines::planResources(
                    temporal, resources, limits, {proxy, domain}, plan),
            "RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");
        rejects(CloudPreparedPipelines::planResources(
                    temporal, resources, limits, {*d, counterfeit}, plan),
            "RHI-DISPATCH-DOMAIN-OWNER");
        assert(beforeForeign == core::defaultCpuAllocator().stats().allocationCount
            && !domain->diagnostic());
        CloudPreparedResourceCreateContext c{*d, asset};
        c.contractVersion = 1u;
        c.prototypeOptIn = true;
        c.cameraId = 1u;
        c.selection = temporal;
        c.resources = resources;
        c.sampleMemory = rhi::MemoryUsage::CpuToGpu;
        c.maxCpuOwnerBytes = c.maxCpuInitializationBytes = metadata;
        c.ownerQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        c.initializationQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        c.sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        c.historyQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        c.retainedLogicalBytes =
            core::makeShared<core::AtomicCapacityBudget>(config.gpuBudgetBytes);
        c.dispatchDomain = domain;
        // The 720p path is pure preflight, not a physical resource peak claim.
        c.maxCpuInitializationBytes = large ? 8u * 1024u * 1024u : metadata;
        c.initializationQuota->setLimitBytes(c.maxCpuInitializationBytes);
        const auto beforeContext = core::defaultCpuAllocator().stats().allocationCount;
        require(CloudPreparedResources::validateCreate(c, caps, plan));
        assert(!c.retainedLogicalBytes->used() && !c.ownerQuota->liveBytes());
        CloudPreparedPipelineCreateContext pipeline{*d};
        pipeline.selection = c.selection;
        pipeline.resources = c.resources;
        VolumetricCloudFeature feature;
        core::pmr::String detail{&core::defaultCpuMemoryResource()};
        rejects(feature.create({pipeline, c, 1u, true}, detail),
            "CLOUD-PREPARED-FEATURE-CREATE-MISMATCH");
        assert(!feature.created() && !c.retainedLogicalBytes->used());
        assert(beforeContext == core::defaultCpuAllocator().stats().allocationCount);
        CloudPreparedResources owner;
        render_graph::FrameGraph graph(*d);
        CloudGraphContext frame{graph};
        CloudPreparedFrameBuffers pending;
        if (!large) {
            assert(!d->prepareOriginalResourceRegistration({1u, true, 16u, 4u, 4u, 51u, metadata},
                core::makeShared<core::CpuMemoryQuota>(metadata)));
            require(owner.create(c));
            frame.cameraId = c.cameraId;
            frame.layer = tests::cloud::layer();
            frame.cameraPosition = {39.f, 10.f, -57.f};
            frame.view = core::lookAt(
                frame.cameraPosition, frame.cameraPosition + core::Vec3{.25f, .6f, 1.f});
            frame.projection = core::perspective(57.f, 1.5f, 1.f, 20000.f);
            frame.sunDirection = core::normalize(core::Vec3{1.f, 1.f, -.2f});
            frame.sunRadiance = {2.f, 1.8f, 1.6f};
            frame.ambientRadiance = {.1f, .2f, .3f};
            require(owner.prepareFrame(frame));
            require(owner.pendingBuffers(pending));
            assert(pending.raw && pending.constants && pending.historyGeneration);
        }
        domainQuota->setLimitBytes(metadata - 1u);
        const auto beforeRevocation = core::defaultCpuAllocator().stats().allocationCount;
        rejects(CloudPreparedResources::validateCreate(c, caps, plan), "RHI-DISPATCH-DOMAIN-QUOTA");
        assert(!plan.raw.sampleCount && !plan.descriptorSets);
        if (!large) {
            const auto generation = owner.history()->token().generation;
            rejects(owner.prepareFrame(frame), "RHI-DISPATCH-DOMAIN-QUOTA");
            rejects(owner.pendingBuffers(pending), "RHI-DISPATCH-DOMAIN-QUOTA");
            assert(!pending.raw && !pending.constants && !pending.historyGeneration);
            assert(generation == owner.history()->token().generation);
        }
        domainQuota->setLimitBytes(metadata);
        rejects(CloudPreparedPipelines::planResources(temporal, resources, limits, admission, plan),
            "RHI-DISPATCH-DOMAIN-QUOTA");
        assert(beforeRevocation == core::defaultCpuAllocator().stats().allocationCount);
        if (!large) {
            require(owner.history()->cancelPendingFrame());
            require(owner.releaseIdle());
            assert(!c.retainedLogicalBytes->used());
            assert(!c.ownerQuota->liveBytes() && !c.initializationQuota->liveBytes()
                && !c.sourceQuota->liveBytes() && !c.historyQuota->liveBytes());
        }
        auto renewedQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        auto renewed =
            d->issueOriginalComputeDispatchDomain(prepared, domainBudget, renewedQuota, diagnostic);
        assert(renewed && !diagnostic);
        d->retireOriginalDeviceLifetime();
        const auto beforeRetire = core::defaultCpuAllocator().stats().allocationCount;
        rejects(
            CloudPreparedPipelines::planResources(temporal, resources, limits, {*d, renewed}, plan),
            "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        assert(beforeRetire == core::defaultCpuAllocator().stats().allocationCount
            && !plan.raw.sampleCount);
        // All borrowed/admission/context copies leave scope before quota drain.
        c.dispatchDomain.reset();
        domain.reset();
        renewed.reset();
        prepared.reset();
    }
    clean(q);
    assert(!domainQuota->liveBytes());
    std::printf(
        "CLOUD_DISPATCH_ADMISSION_PASS grid=%u/%u/%u temporal=true reconstruction=%s counts=0 preflightAllocations=0 cloudFrames=0\n",
        groups.width,
        groups.height,
        groups.depth,
        large ? "covered" : "outside-refused");
}

void originalComputeBufferLimits()
{
    using Limits = rhi::OriginalDescriptorBufferBindingContract::Limits;
    using Inspector = rhi_metal::MetalOriginalComputeBufferLimits;
    // Scalar fixtures exercise admission arithmetic only, never native authority.
    for (unsigned mode = 0u; mode < 17u; ++mode) {
        rhi::ShaderReflectionDesc reflection;
        rhi_metal::MetalRhiNativePipelinePayload payload;
        rhi::ComputeResourceCapabilities queried;
        queried.maxUniformBufferBindingSizeBytes = 1024u;
        queried.maxStorageBufferBindingSizeBytes = 2048u;
        payload.computeBufferFactsVersion = 1u;
        for (unsigned i = 0u; i < 3u; ++i) {
            const auto type = i == 1u ? rhi::DescriptorType::ReadOnlyStorageBuffer
                                      : rhi::DescriptorType::UniformBuffer;
            reflection.descriptorBindings.push_back({0u, i, type, rhi::ShaderStage::Compute});
            payload.metalBindingSpace.push_back({0u, i, i, type, true});
            payload.computeBufferFacts.push_back({i, i == 0u ? 16u : i == 1u ? 32u : 24u, 64u});
            assert(payload.nativeBindingStages.record(type, i, rhi::ShaderStage::Compute));
        }
        const char* expected = "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-FACTS";
        switch (mode) {
        case 0u:
            break;
        case 1u:
            payload.computeBufferFactsVersion = 0u;
            break;
        case 2u:
            queried.maxUniformBufferBindingSizeBytes = 0u;
            break;
        case 3u:
            queried.maxStorageBufferBindingSizeBytes = 0u;
            break;
        case 4u:
            payload.computeBufferFacts[0].alignment = 0u;
            break;
        case 5u:
            payload.computeBufferFacts[0].alignment = std::uint64_t{UINT32_MAX} + 1u;
            break;
        case 6u:
            payload.computeBufferFacts[0].requiredBytes = 0u;
            break;
        case 7u:
            payload.computeBufferFacts[0].requiredBytes = 1025u;
            break;
        case 8u:
            payload.computeBufferFacts.pop_back();
            break;
        case 9u:
            payload.computeBufferFacts.push_back(payload.computeBufferFacts[0]);
            break;
        case 10u:
            payload.metalBindingSpace.pop_back();
            expected = "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-MAPPING";
            break;
        case 11u:
            payload.metalBindingSpace.push_back(payload.metalBindingSpace[0]);
            expected = "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-MAPPING";
            break;
        case 12u:
            payload.metalBindingSpace[0].active = false;
            expected = "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-MAPPING";
            break;
        case 13u:
            payload.metalBindingSpace[0].type = rhi::DescriptorType::StorageBuffer;
            expected = "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-MAPPING";
            break;
        case 14u:
            assert(payload.nativeBindingStages.record(
                rhi::DescriptorType::UniformBuffer, 0u, rhi::ShaderStage::Vertex));
            expected = "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-MAPPING";
            break;
        case 15u:
            payload.computeBufferFacts[0].alignment = UINT32_MAX;
            payload.computeBufferFacts[2].alignment = UINT32_MAX - 1u;
            expected = "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-ALIGNMENT-OVERFLOW";
            break;
        case 16u:
            reflection.descriptorBindings.erase(reflection.descriptorBindings.begin() + 1);
            expected = "RHI-ORIGINAL-DESCRIPTOR-BUFFER-LIMITS-UNAVAILABLE";
            break;
        }
        Limits output{1u, 99u, 99u, 99u, 99u};
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto* id = Inspector::inspect(reflection, payload, queried, output);
        if (!mode)
            assert(!id && output.uniformOffsetAlignment == 48u
                && output.storageOffsetAlignment == 32u && output.maxUniformBindingBytes == 1024u
                && output.maxStorageBindingBytes == 2048u);
        else {
            assert(id && !std::strcmp(id, expected));
            assert(!output.contractVersion && !output.uniformOffsetAlignment
                && !output.storageOffsetAlignment && !output.maxUniformBindingBytes
                && !output.maxStorageBindingBytes);
        }
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
    }
    std::puts(
        "METAL_COMPUTE_BUFFER_LIMITS_PASS scalarCases=17 checkedLCM=true failureOutputCleared=true allocation0=true nativeAuthority=false");
}

template <class Source, class Pipeline>
void publicPipelineObservation(const std::shared_ptr<const Source>& source,
    const std::shared_ptr<Pipeline>& pipeline,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    rhi::GpuDevice& device,
    rhi::GpuDevice& foreign)
{
    using Sources = rhi::OriginalCommandResourceSources;
    using Assembly = rhi::OriginalCommandSourceAssembly;
    constexpr bool graphics = std::is_same_v<Source, rhi::OriginalCommandGraphicsPipelineSource>;
    const auto baseline = quota->liveBytes();
    {
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = 1u;
        admission.maxGraphOwners = 64u;
        admission.maxCpuBytes.fill(metadata);
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            1u;
        const char* id = nullptr;
        auto inventory =
            Sources::prepareColdOriginalTyped(nullptr, 0u, admission, {2u, true, 1u}, quota, id);
        assert(inventory && !id);
        assert(!Sources::appendOriginalTyped(inventory, source, device));
        const auto counterfeit =
            std::shared_ptr<const Sources>(inventory.get(), [](const auto*) {});
        std::shared_ptr<const std::remove_const_t<Pipeline>> observed;
        const auto inspect = [&](const auto& candidate, const auto& owner, std::size_t index) {
            if constexpr (graphics)
                return Assembly::inspectGraphicsPipeline(candidate, owner, index, observed);
            else
                return Assembly::inspectComputePipeline(candidate, owner, index, observed);
        };
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(!inspect(inventory, device, 0u));
        assert(core::SharedOwnerProjection::sameOwner(observed, pipeline));
        assert(inspect(inventory, device, SIZE_MAX) && !observed);
        observed = pipeline;
        assert(inspect(counterfeit, device, 0u) && !observed);
        observed = pipeline;
        assert(inspect(inventory, foreign, 0u) && !observed);
        if constexpr (graphics) {
            std::shared_ptr<const rhi::ComputePipeline> wrong;
            assert(Assembly::inspectComputePipeline(inventory, device, 0u, wrong) && !wrong);
        } else {
            std::shared_ptr<const rhi::RenderPipeline> wrong;
            assert(Assembly::inspectGraphicsPipeline(inventory, device, 0u, wrong) && !wrong);
        }
        bool descriptor = true;
        assert(!Assembly::descriptorForPipeline(inventory, device, 0u, descriptor) && !descriptor);
        descriptor = true;
        assert(Assembly::descriptorForPipeline(inventory, device, SIZE_MAX, descriptor)
            && !descriptor);
        std::uint32_t owners = 99u;
        assert(Assembly::cpuOwnerCount(counterfeit, device, 1u, owners) && !owners);
        assert(!Assembly::cpuOwnerCount(inventory, device, 1u, owners) && owners);
        {
            core::CpuMemoryQuotaLease pressure(quota, metadata - quota->liveBytes());
            assert(pressure.charged() && !inspect(inventory, device, 0u));
            assert(core::SharedOwnerProjection::sameOwner(observed, pipeline));
        }
        quota->setLimitBytes(metadata - 1u);
        assert(inspect(inventory, device, 0u) && !observed);
        quota->setLimitBytes(metadata);
        assert(!inspect(inventory, device, 0u));
        assert(core::SharedOwnerProjection::sameOwner(observed, pipeline));
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        assert(!inventory->nativeExecutionProven() && !source->nativeExecutionProven());
    }
    assert(quota->liveBytes() == baseline);
    std::printf(
        "PUBLIC_SOURCE_PIPELINE_OBSERVATION_PASS graphics=%d samePipeline=true allocation0=true quotaPressure=true clearedOutputs=true nativeAuthority=false cloudFrames=0\n",
        graphics);
}

std::shared_ptr<const rhi::ComputeDispatchValidationDomain> pipelineDispatchAdmission(
    const std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>& source,
    rhi::GpuDevice& device,
    rhi::GpuDevice& foreign,
    const Session::Quotas& proofQuotas,
    const std::shared_ptr<core::CpuMemoryQuota>& domainQuota)
{
    using A = rhi::OriginalComputeDispatchRecordingAdmission;
    const char* id = nullptr;
    auto prepared =
        device.prepareOriginalComputeDispatchValidation(request(), budget, proofQuotas, id);
    if (id)
        std::fprintf(stderr, "recording admission proof: %s\n", id);
    assert(prepared && !id);
    until([&] {
        assert(!prepared->advance());
        return prepared->observe().verified;
    });
    auto domain =
        device.issueOriginalComputeDispatchDomain(prepared, domainBudget, domainQuota, id);
    assert(domain && !id);
    const auto fakeDomain = std::shared_ptr<const rhi::ComputeDispatchValidationDomain>(
        domain.get(), [](const auto*) {});
    const auto fakeSource = std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>(
        source.get(), [](const auto*) {});
    const auto revokedQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    auto revoked =
        device.issueOriginalComputeDispatchDomain(prepared, domainBudget, revokedQuota, id);
    assert(revoked && !id);
    prepared.reset();
    using Source = rhi::OriginalCommandComputePipelineSource;
    const rhi::OriginalCommandPipelineSourceBudget sourceBudget{2u,
        true,
        Source::kind,
        source->originalPipeline()->originalResourceRegistration().generation(),
        metadata,
        Source::exclusiveOwnerCount};
    const auto alternativeSource = Source::prepareOriginal(
        source->originalPipeline(), sourceBudget, source->originalMetadataQuota(), device, id);
    assert(alternativeSource && !id && alternativeSource.get() != source.get());
    const rhi::OriginalComputeDispatchRecordingInput input{device, domain};
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    for (std::uint32_t z = 1u; z <= 3u; ++z)
        for (std::uint32_t y = 1u; y <= 2u; ++y)
            for (std::uint32_t x = 1u; x <= 3u; ++x) {
                assert(!A::inspect(input, source, {x, y, z}));
                const auto frozen = A::freeze(input, source, {x, y, z}, id);
                assert(frozen && !id && !frozen->nativeExecutionProven());
                assert(!frozen->diagnostic() && !frozen->diagnostic(source, {x, y, z}));
                assert(frozen->diagnostic(fakeSource, {x, y, z}));
                assert(frozen->diagnostic(alternativeSource, {x, y, z}));
                assert(frozen->diagnostic({}, {x, y, z}));
                assert(frozen->diagnostic(source, {x == 1u ? 2u : 1u, y, z}));
                assert(!frozen->diagnostic());
            }
    for (const auto groups : {rhi::Extent3D{4u, 2u, 3u}, {3u, 3u, 3u}, {3u, 2u, 4u}, {0u, 1u, 1u}})
        assert(A::inspect(input, source, groups) && !A::freeze(input, source, groups, id) && id);
    assert(A::inspect(input, {}, {1u, 1u, 1u}));
    assert(A::inspect(input, fakeSource, {1u, 1u, 1u}));
    assert(A::inspect({device, fakeDomain}, source, {1u, 1u, 1u}));
    assert(A::inspect({foreign, domain}, source, {1u, 1u, 1u}));
    const std::shared_ptr<const rhi::ComputeDispatchValidationDomain> missing;
    assert(A::inspect({device, missing}, source, {1u, 1u, 1u}));
    for (const auto& rejectedDomain : {missing, fakeDomain})
        assert(!A::freeze({device, rejectedDomain}, source, {1u, 1u, 1u}, id) && id);
    assert(!A::freeze(input, fakeSource, {1u, 1u, 1u}, id) && id);
    assert(!A::freeze({foreign, domain}, source, {1u, 1u, 1u}, id) && id);
    auto frozenRevoked = A::freeze({device, revoked}, source, {1u, 1u, 1u}, id);
    assert(frozenRevoked && !id && !frozenRevoked->diagnostic());
    revokedQuota->setLimitBytes(metadata - 1u);
    assert(A::inspect({device, revoked}, source, {1u, 1u, 1u}));
    assert(frozenRevoked->diagnostic());
    revokedQuota->setLimitBytes(metadata);
    assert(A::inspect({device, revoked}, source, {1u, 1u, 1u}));
    assert(frozenRevoked->diagnostic());
    frozenRevoked.reset();
    const auto frozenSource = A::freeze(input, source, {3u, 2u, 3u}, id);
    assert(frozenSource && !id);
    const auto& metadataQuota = source->originalMetadataQuota();
    metadataQuota->setLimitBytes(metadata - 1u);
    assert(frozenSource->diagnostic());
    metadataQuota->setLimitBytes(metadata);
    assert(!A::inspect(input, source, {3u, 2u, 3u}));
    assert(frozenSource->diagnostic());
    const auto fresh = A::freeze(input, source, {3u, 2u, 3u}, id);
    assert(fresh && !id && !fresh->diagnostic());
    assert(!A::inspect(input, source, {3u, 2u, 3u}));
    rhi::OriginalRenderPassPreparationRequest preparation{6u,
        true,
        {1u, true, 1u, 1u, 1u, {1u, true, 1u, 0u, 0u, 0u, 0u, metadata}},
        4u,
        1u,
        rhi::OriginalRenderPassContentBudget{3u, true, 1u, 1u, 1u, 1u, 1u},
        rhi::OriginalRenderPassRecordingBudget{3u, true, 1u, 1u}};
    preparation.recording->dispatchDomainVersion = 1u;
    preparation.recording->dispatchDomainOptIn = true;
    preparation.dispatchDomain = domain;
    assert(!rhi::OriginalRenderPassPreparationContract::validate(preparation));
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255u)}) {
        const auto decision =
            rhi::OriginalRenderPassPreparationContract::lowering(backend, preparation);
        assert(decision.diagnostic && !decision.recordingAvailable
            && !decision.privatePreparationAvailable);
        if (backend == rhi::Backend::Metal)
            assert(!std::strcmp(
                decision.diagnostic, "RHI-RENDER-PASS-PREPARATION-METAL-PAYLOAD-MISSING"));
    }
    auto unrequested = preparation;
    unrequested.recording->dispatchDomainVersion = 0u;
    unrequested.recording->dispatchDomainOptIn = false;
    assert(rhi::OriginalRenderPassPreparationContract::validate(unrequested));
    assert(before == core::defaultCpuAllocator().stats().allocationCount);
    revoked.reset();
    assert(!revokedQuota->liveBytes());
    std::puts(
        "METAL_PIPELINE_DISPATCH_ADMISSION_PASS tuples=18 exactSource=true immutableLocalSize=true counterfeitAndForeignRefused=true irreversibleDomainRevocation=true allocation0=true preparationGateClosed=true cloudFrames=0");
    std::puts(
        "METAL_FROZEN_DISPATCH_ADMISSION_PASS tuples=18 exactSourceAndTuple=true noDeviceFacade=true counterfeitAndForeignRefused=true differentValidSourceRefused=true sourceAndDomainRevocation=true freshSourceObservationAfterRecovery=true allocation0=true execution=false cloudFrames=0");
    return domain;
}

// Exercises the real GeneralWrite entry's independently requested loss-group
// join. This deliberately omits the binding inventory and stops at its exact
// missing-group diagnostic; full publication is exercised by ordinary bindings.
template <class Pipeline> class SynchronousLossGroupProbe final {
public:
    static constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;
    const std::shared_ptr<core::CpuMemoryQuota> objects =
        core::makeShared<core::CpuMemoryQuota>(metadata);
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> quotas;
    std::shared_ptr<rhi::DescriptorArena> arena;
    core::UniquePtr<rhi::DescriptorSet> set;
    std::uint64_t initialRevision = 0u;
    rhi::OriginalGeneralDescriptorWriteRequest request;

    SynchronousLossGroupProbe(rhi::GpuDevice& device, const std::shared_ptr<Pipeline>& pipeline)
    {
        for (auto& quota : quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(metadata);
        core::CpuMemoryQuotaScope scope(objects);
        const auto& layout = pipeline->originalLayoutOwner()->desc().descriptorLayouts.front();
        rhi::DescriptorArenaDesc desc;
        desc.maxSets = 1u;
        desc.maxDescriptors = static_cast<std::uint32_t>(layout.bindings.size());
        arena = core::shareUnique(device.createOriginalDescriptorArena(desc));
        if (!arena)
            std::fprintf(stderr,
                "Metal loss group arena: %.*s\n",
                int(device.lastOriginalResourceRegistrationDiagnostic().size()),
                device.lastOriginalResourceRegistrationDiagnostic().data());
        assert(arena);
        set = device.allocateOriginalDescriptorSet(arena, {layout.name, layout});
        if (!set)
            std::fprintf(stderr,
                "Metal loss group set: %.*s\n",
                int(device.lastOriginalResourceRegistrationDiagnostic().size()),
                device.lastOriginalResourceRegistrationDiagnostic().data());
        assert(set);
        initialRevision = set->contentRevision();
        request = {compute ? 4u : 3u,
            true,
            desc.maxDescriptors,
            static_cast<std::uint32_t>(rhi::OriginalGeneralDescriptorWriteContract::ownerSlots(
                desc.maxDescriptors, compute ? 4u : 3u)),
            0u,
            {metadata, metadata, metadata},
            metadata * 8u,
            4u,
            true};
    }

    const char* begin(rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& selected) const
    {
        return device.beginOriginalGeneralDescriptorWrite(
            *set, pipeline, {}, 0u, nullptr, 0u, request, selected);
    }

    static const char* payloadMissing() { return "RHI-ORIGINAL-GENERAL-WRITE-BINDING-COUNT"; }

    void verify(rhi::GpuDevice& device,
        rhi::GpuDevice& foreign,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 6u>& overlapping) const
    {
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto revision = set->contentRevision();
        const auto* id = begin(device, pipeline, quotas);
        if (!id || std::strcmp(id, payloadMissing()))
            std::fprintf(stderr, "Metal synchronous loss join: %s\n", id ? id : "null");
        assert(id && !std::strcmp(id, payloadMissing()));
        assert(begin(foreign, pipeline, quotas));
        const auto fake = std::shared_ptr<Pipeline>(pipeline.get(), [](auto*) {});
        assert(std::strcmp(begin(device, fake, quotas), payloadMissing()));
        assert(std::strcmp(begin(device, {}, quotas), payloadMissing()));
        for (std::size_t axis = 0u; axis < quotas.size(); ++axis) {
            quotas[axis]->setLimitBytes(metadata - 1u);
            assert(std::strcmp(begin(device, pipeline, quotas), payloadMissing()));
            quotas[axis]->setLimitBytes(metadata);
            auto changed = quotas;
            changed[axis].reset();
            assert(std::strcmp(begin(device, pipeline, changed), payloadMissing()));
            changed = quotas;
            changed[axis] = quotas[(axis + 1u) % quotas.size()];
            assert(std::strcmp(begin(device, pipeline, changed), payloadMissing()));
            for (const auto& owner : overlapping) {
                changed = quotas;
                changed[axis] = owner;
                assert(std::strcmp(begin(device, pipeline, changed), payloadMissing()));
            }
        }
        assert(set->contentRevision() == revision && !set->hasOriginalGeneralContent());
        {
            core::CpuMemoryQuotaLease a(quotas[0], metadata), b(quotas[1], metadata),
                c(quotas[2], metadata);
            assert(a.charged() && b.charged() && c.charged());
            assert(!std::strcmp(begin(device, pipeline, quotas), payloadMissing()));
        }
        assert(set->finishOriginalGeneralWrite() && set->cancelOriginalGeneralWrite());
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        std::printf(
            "METAL_SYNC_LOSS_GROUP_JOIN_PASS compute=%d canonicalSetArenaPipeline=true counterfeitAndForeignRefused=true threeIndependentQuotas=true allocation0=true missingBindingGroupRefused=true cloudFrames=0\n",
            compute);
    }

    void revoked(rhi::GpuDevice* device,
        const std::shared_ptr<const Pipeline>& pipeline,
        unsigned mode) const
    {
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        if (device) {
            const auto* id = begin(*device, std::const_pointer_cast<Pipeline>(pipeline), quotas);
            assert(id && std::strcmp(id, payloadMissing()));
        } else
            assert(set->originalResourceRegistration().diagnostic());
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        assert(!set->hasOriginalGeneralContent() && set->contentRevision() == initialRevision);
        std::printf(
            "METAL_SYNC_LOSS_GROUP_REVOKE_PASS compute=%d mode=%u allocation0=true beforePublication=true\n",
            compute,
            mode);
    }

    ~SynchronousLossGroupProbe()
    {
        set.reset();
        arena.reset();
        assert(!objects->liveBytes());
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
    }
};

void originalComputeCreation(const char* path, unsigned revoke, bool accept = false)
{
    using C = rhi::OriginalSynchronousComputeCreationContract;
    // Selected probe has one uniform and one storage descriptor.
    auto d = device(true, true, accept ? 2u : 1u);
    auto foreign = device(false);
    const auto registryQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    rhi::OriginalResourceRegistrationBudget registration;
    registration.contractVersion = 10u;
    registration.prototypeOptIn = true;
    registration.maxPipelineLayouts = registration.maxComputePipelines = 1u;
    registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
    registration.maxRetainedOwners = 32u;
    registration.maxCpuMetadataBytes = metadata;
    const char* id = nullptr;
    const auto loss = d->observeOriginalDeviceNativeLoss(id);
    assert(loss && !id);
    assert(!d->prepareOriginalTypedNativeLossRegistration(
        {3u, true}, registration, registryQuota, *loss));
    assert(!d->validateOriginalTypedNativeLossJoin(*loss, {3u, true}));
    assert(foreign->validateOriginalTypedNativeLossJoin(*loss, {3u, true}));
    assert(rhi::OriginalTypedNativeLossJoinContract::lowering(rhi::Backend::Metal, 3u)
            .acceptedPipelineJoinAvailable);
    const auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto acceptedQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> q;
    for (auto& quota : q)
        quota = core::makeShared<core::CpuMemoryQuota>(metadata);
    {
        core::pmr::String detail;
        auto artifact = shader::SingleEntryShaderArtifact::load(
            path, {metadata, metadata * 8u, metadata, 2u}, q[0], detail);
        assert(artifact);
        auto input =
            rhi::OriginalSingleEntryPipelineInput::prepare(artifact, {1u, metadata}, q[1], detail);
        assert(input);
        auto layout = core::shareUnique(d->createOriginalPipelineLayout(
            rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection)));
        if (!layout)
            std::fprintf(stderr,
                "original Metal layout creation: %.*s\n",
                int(d->lastOriginalResourceRegistrationDiagnostic().size()),
                d->lastOriginalResourceRegistrationDiagnostic().data());
        else if (const auto* layoutId = d->validateOriginalResourceRegistration(*layout))
            std::fprintf(stderr, "original Metal layout registration: %s\n", layoutId);
        assert(layout && !d->validateOriginalResourceRegistration(*layout));
        rhi::ComputePipelineDesc desc;
        desc.label = "MetalOriginalComputeCreation";
        desc.layout = layout.get();
        desc.shaderPackage = &input->package();
        desc.computeShader = input->package().entrypoints.front();
        const auto target =
            rhi::OriginalSingleEntryPipelineInput::selectTarget(input, rhi::Backend::Metal);
        assert(target.bytes && !target.diagnostic);
        const C::Budget b{1u, true, target.bytes->size(), 64u, metadata, metadata};
        const auto create = [&](const auto& selectedInput,
                                const auto& selectedLayout,
                                const auto& definition,
                                const auto& owner,
                                const auto& native) {
            return d->createOriginalSynchronousComputeCreation(
                selectedInput, selectedLayout, definition, b, owner, native, id);
        };
        const auto refused = [&](const auto& result) {
            if (result || !id) {
                std::fprintf(stderr,
                    "creation negative failed: receipt=%d id=%s\n",
                    bool(result),
                    id ? id : "null");
                assert(false);
            }
        };
        auto bad = rhi::ComputePipelineStateCopy::copy(desc);
        bad.computeShader.computeWorkgroupSizeX = 4u;
        refused(create(input, layout, bad, q[2], q[3]));
        const auto fakeInput = std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>(
            input.get(), [](const auto*) {});
        refused(create(fakeInput, layout, desc, q[2], q[3]));
        const auto fakeLayout =
            std::shared_ptr<rhi::PipelineLayout>(layout.get(), [](const auto*) {});
        refused(create(input, fakeLayout, desc, q[2], q[3]));
        refused(foreign->createOriginalSynchronousComputeCreation(
            input, layout, desc, b, q[2], q[3], id));
        for (const auto& overlap : {q[0], q[1], q[3], registryQuota})
            refused(create(input, layout, desc, overlap, q[3]));
        for (unsigned axis = 0u; axis < 2u; ++axis) {
            auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
            auto exhausted = b;
            (axis ? exhausted.maxCpuNativeBytes : exhausted.maxCpuOwnerBytes) = 1u;
            refused(d->createOriginalSynchronousComputeCreation(
                input, layout, desc, exhausted, axis ? q[2] : tiny, axis ? tiny : q[3], id));
            assert(!tiny->liveBytes() && !q[2]->liveBytes() && !q[3]->liveBytes());
        }
        auto receipt = create(input, layout, desc, q[2], q[3]);
        if (!receipt)
            std::fprintf(stderr, "original Metal compute creation: %s\n", id ? id : "null");
        assert(receipt && !id && !rhi::OriginalComputePipelineCreation::validateOriginal(receipt));
        assert(receipt->originalInput() == input && receipt->originalLayout() == layout);
        assert(receipt->definition().layout == layout.get()
            && receipt->definition().shaderPackage == &input->package());
        assert(q[2]->liveBytes() && q[3]->liveBytes());
        const auto fake = std::shared_ptr<const rhi::OriginalComputePipelineCreation>(
            receipt.get(), [](const auto*) {});
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(rhi::OriginalComputePipelineCreation::validateOriginal(fake));
        for (const auto& quota : q)
            assert(receipt->usesCpuQuotaDomain(quota));
        assert(!receipt->usesCpuQuotaDomain(registryQuota));
        assert(!receipt->validate());
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        const auto wrapperQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        assert(
            !d->createOriginalArtifactComputePipeline(receipt, {1u, true, metadata}, wrapperQuota));
        assert(d->lastOriginalResourceRegistrationDiagnostic()
            == "RHI-ACCEPTED-COMPUTE-FACTORY-METAL-PAYLOAD-MISSING");
        assert(!wrapperQuota->liveBytes());
        refused(create(input, layout, desc, q[2], q[3]));
        assert(!std::strcmp(id, "RHI-SYNC-COMPUTE-CREATION-DIRECT-BUDGET"));
        receipt.reset();
        assert(!q[2]->liveBytes() && !q[3]->liveBytes());
        receipt = create(input, layout, desc, q[2], q[3]);
        assert(receipt && !id);
        using Source = rhi::OriginalCommandComputePipelineSource;
        std::shared_ptr<const Source> source;
        core::UniquePtr<SynchronousLossGroupProbe<rhi::ComputePipeline>> group;
        const auto proofQuotas = accept ? quotas() : Session::Quotas{};
        const auto domainQuota =
            accept ? core::makeShared<core::CpuMemoryQuota>(metadata) : nullptr;
        std::shared_ptr<const rhi::ComputeDispatchValidationDomain> domain;
        std::optional<rhi::OriginalComputeDispatchRecordingAdmission::Frozen> frozenDispatch;
        if (accept) {
            const auto counterfeit = std::shared_ptr<const rhi::OriginalComputePipelineCreation>(
                receipt.get(), [](const auto*) {});
            assert(!d->createOriginalArtifactComputePipeline(
                counterfeit, {2u, true, metadata}, acceptedQuota));
            assert(!foreign->createOriginalArtifactComputePipeline(
                receipt, {2u, true, metadata}, acceptedQuota));
            for (const auto& overlap : {q[0], q[1], q[2], q[3], registryQuota})
                assert(!d->createOriginalArtifactComputePipeline(
                    receipt, {2u, true, metadata}, overlap));
            auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
            assert(!d->createOriginalArtifactComputePipeline(receipt, {2u, true, 1u}, tiny));
            assert(!tiny->liveBytes() && !acceptedQuota->liveBytes());
            auto unique = d->createOriginalArtifactComputePipeline(
                receipt, {2u, true, metadata}, acceptedQuota);
            if (!unique)
                std::fprintf(stderr,
                    "accepted Metal compute: %.*s\n",
                    int(d->lastOriginalResourceRegistrationDiagnostic().size()),
                    d->lastOriginalResourceRegistrationDiagnostic().data());
            assert(unique);
            auto pipeline =
                rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(std::move(unique), id);
            assert(pipeline && !id && !d->validateOriginalResourceRegistration(*pipeline));
            assert(!pipeline->originalArtifactComputeDiagnostic());
            assert(pipeline->originalComputeCreation() == receipt);
            assert(!pipeline->originalDescriptorBufferBindingDiagnostic(
                {1u, true}, rhi::DescriptorType::UniformBuffer, 16u, 0u));
            assert(!pipeline->originalDescriptorBufferBindingDiagnostic(
                {1u, true}, rhi::DescriptorType::StorageBuffer, 64u, 0u));
            assert(pipeline->originalDescriptorBufferBindingDiagnostic(
                {1u, true}, rhi::DescriptorType::StorageBuffer, UINT64_MAX, 0u));
            assert(!d->createOriginalArtifactComputePipeline(
                receipt, {2u, true, metadata}, acceptedQuota));
            const rhi::OriginalCommandPipelineSourceBudget sb{2u,
                true,
                Source::kind,
                pipeline->originalResourceRegistration().generation(),
                metadata,
                Source::exclusiveOwnerCount};
            auto fakePipeline =
                std::shared_ptr<const rhi::ComputePipeline>(pipeline.get(), [](const auto*) {});
            assert(!Source::prepareOriginal(fakePipeline, sb, sourceQuota, *d, id));
            assert(!Source::prepareOriginal(pipeline, sb, sourceQuota, *foreign, id));
            assert(!Source::prepareOriginal(pipeline, sb, acceptedQuota, *d, id));
            source = Source::prepareOriginal(pipeline, sb, sourceQuota, *d, id);
            if (id)
                std::fprintf(stderr, "Metal compute Source: %s\n", id);
            assert(source && !id && !Source::validateOriginal(source, *d));
            group = core::makeUnique<SynchronousLossGroupProbe<rhi::ComputePipeline>>(*d, pipeline);
            group->verify(
                *d, *foreign, pipeline, {q[0], q[1], q[2], q[3], registryQuota, acceptedQuota});
            assert(Source::validateOriginal(source, *foreign));
            assert(!source->nativeExecutionProven());
            if (revoke == 10u)
                publicPipelineObservation(source, pipeline, sourceQuota, *d, *foreign);
            domain = pipelineDispatchAdmission(source, *d, *foreign, proofQuotas, domainQuota);
            auto frozen = rhi::OriginalComputeDispatchRecordingAdmission::freeze(
                {*d, domain}, source, {3u, 2u, 3u}, id);
            assert(frozen && !id && !frozen->diagnostic());
            frozenDispatch.emplace(std::move(*frozen));
            const std::weak_ptr<const rhi::ComputePipeline> retained = pipeline;
            pipeline.reset();
            assert(!retained.expired() && !Source::validateOriginal(source, *d));
            assert(acceptedQuota->liveBytes() && sourceQuota->liveBytes());
        }
        if (accept && revoke == 10u) {
            group.reset();
            std::weak_ptr<const rhi::OriginalComputePipelineCreation> retained = receipt;
            receipt.reset();
            layout.reset();
            input.reset();
            artifact.reset();
            assert(!retained.expired() && !Source::validateOriginal(source, *d));
            for (const auto& quota : q)
                assert(quota->liveBytes());
            source.reset();
            assert(!frozenDispatch->diagnostic());
            assert(!retained.expired() && acceptedQuota->liveBytes() && sourceQuota->liveBytes());
            domain.reset();
            assert(domainQuota->liveBytes());
            frozenDispatch.reset();
            assert(retained.expired() && !acceptedQuota->liveBytes() && !sourceQuota->liveBytes());
            // This observer still owns the quota-bound shared control block.
            retained.reset();
            for (const auto& quota : q)
                assert(!quota->liveBytes());
            domain.reset();
            clean(proofQuotas);
            assert(!domainQuota->liveBytes());
            std::puts(
                "METAL_COMPUTE_ACCEPTANCE_RELEASE_PASS SourceRetainsCreation=true finalOwnerDrainsAllSixQuotas=true cloudFrames=0");
            std::puts(
                "METAL_FROZEN_DISPATCH_RELEASE_PASS onlyFrozenRetainsSourceAndDomain=true finalQuotaDrain=true execution=false cloudFrames=0");
            return;
        }
        const auto generic = d->capabilities().computeResources;
        assert(!generic.maxComputeWorkgroupCountX && !generic.maxComputeWorkgroupCountY
            && !generic.maxComputeWorkgroupCountZ);
        const auto beforeRevoke = core::defaultCpuAllocator().stats().allocationCount;
        if (revoke < 2u || (revoke >= 3u && revoke <= 5u)) {
            const auto& changed = revoke < 2u ? q[2u + revoke]
                : revoke == 3u                ? registryQuota
                : revoke == 4u                ? q[1]
                                              : q[0];
            changed->setLimitBytes(metadata - 1u);
            assert(receipt->validate());
            changed->setLimitBytes(metadata);
            assert(receipt->validate());
        } else if (revoke == 2u) {
            d->retireOriginalDeviceLifetime();
            assert(receipt->validate());
        } else if (revoke == 6u) {
            assert(!d->detachOriginalDeviceNativeLoss());
            assert(receipt->validate());
        } else if (revoke == 7u) {
            d.reset();
            assert(receipt->validate());
        } else {
            const auto& changed = revoke == 8u ? acceptedQuota : sourceQuota;
            changed->setLimitBytes(metadata - 1u);
            assert(Source::validateOriginal(source));
        }
        if (accept) {
            assert(Source::validateOriginal(source));
            assert(frozenDispatch->diagnostic());
        }
        if (accept && revoke != 9u)
            group->revoked(d.get(), source->originalPipeline(), revoke);
        if (accept) {
            if (d)
                assert(rhi::OriginalComputeDispatchRecordingAdmission::inspect(
                    {*d, domain}, source, {1u, 1u, 1u}));
            else
                assert(domain->diagnostic());
        }
        assert(beforeRevoke == core::defaultCpuAllocator().stats().allocationCount);
        if (accept) {
            std::printf(
                "METAL_FROZEN_DISPATCH_REVOKE_PASS mode=%u deviceFacadeAlive=%s allocation0=true execution=false cloudFrames=0\n",
                revoke,
                d ? "true" : "false");
            frozenDispatch.reset();
            domain.reset();
            clean(proofQuotas);
            assert(!domainQuota->liveBytes());
            std::printf(
                "METAL_PIPELINE_DISPATCH_REVOKE_PASS mode=%u beforeNativeMutation=true cloudFrames=0\n",
                revoke);
        }
    }
    for (const auto& quota : q)
        assert(!quota->liveBytes());
    assert(!acceptedQuota->liveBytes() && !sourceQuota->liveBytes());
    if (accept)
        std::printf(
            "METAL_ORIGINAL_COMPUTE_ACCEPTANCE_PASS revoke=%u sameOriginalLoss=true canonicalSource=true quotasDrained=true allocation0=true execution=false cloudFrames=0\n",
            revoke);
    std::printf(
        "METAL_ORIGINAL_COMPUTE_CREATION_PASS revoke=%u originalDevice=true nativeLocalSize=8/8/1 independentQuotas=true directCapacity=2 execution=false cloudFrames=0\n",
        revoke);
}

void originalGraphicsBufferLimits()
{
    using Inspector = rhi_metal::MetalOriginalGraphicsBufferLimits;
    using Limits = rhi::OriginalDescriptorBufferBindingContract::Limits;
    using Stage = rhi::ShaderStage;
    using Type = rhi::DescriptorType;
    for (unsigned mode = 0u; mode < 29u; ++mode) {
        rhi::ShaderReflectionDesc reflection;
        rhi_metal::MetalRhiNativePipelinePayload native;
        rhi::ComputeResourceCapabilities queried;
        queried.maxUniformBufferBindingSizeBytes = 1024u;
        queried.maxStorageBufferBindingSizeBytes = 2048u;
        native.graphicsBufferFacts[0].stage = Stage::Vertex;
        native.graphicsBufferFacts[1].stage = Stage::Fragment;
        for (auto& stage : native.graphicsBufferFacts)
            stage.version = 1u;
        for (unsigned i = 0u; i < 3u; ++i) {
            const auto type = i == 1u ? Type::ReadOnlyStorageBuffer : Type::UniformBuffer;
            reflection.descriptorBindings.push_back({0u, i, type, Stage::Vertex | Stage::Fragment});
            native.metalBindingSpace.push_back({0u, i, i, type, true});
            assert(native.nativeBindingStages.record(
                type, i, i == 1u ? Stage::Fragment : Stage::Vertex));
        }
        assert(native.nativeBindingStages.record(Type::UniformBuffer, 0u, Stage::Fragment));
        native.graphicsBufferFacts[0].entries = {{0u, 16u, 64u}, {2u, 12u, 64u}};
        native.graphicsBufferFacts[1].entries = {{0u, 24u, 64u}, {1u, 32u, 128u}};
        switch (mode) {
        case 1u:
            reflection.descriptorBindings.erase(reflection.descriptorBindings.begin() + 1);
            native.metalBindingSpace.erase(native.metalBindingSpace.begin() + 1);
            native.graphicsBufferFacts[1].entries.pop_back();
            queried.maxStorageBufferBindingSizeBytes = 0u;
            break;
        case 2u:
            reflection.descriptorBindings.erase(reflection.descriptorBindings.begin() + 2);
            reflection.descriptorBindings.erase(reflection.descriptorBindings.begin());
            native.metalBindingSpace.erase(native.metalBindingSpace.begin() + 2);
            native.metalBindingSpace.erase(native.metalBindingSpace.begin());
            native.graphicsBufferFacts[0].entries.clear();
            native.graphicsBufferFacts[1].entries.erase(
                native.graphicsBufferFacts[1].entries.begin());
            queried.maxUniformBufferBindingSizeBytes = 0u;
            break;
        case 3u:
            reflection.descriptorBindings.clear();
            native.metalBindingSpace.clear();
            for (auto& stage : native.graphicsBufferFacts)
                stage.entries.clear();
            queried = {};
            break;
        case 4u:
            native.graphicsBufferFacts[0].version = 0u;
            break;
        case 5u:
            native.graphicsBufferFacts[1].version = 2u;
            break;
        case 6u:
            native.graphicsBufferFacts[1].stage = Stage::Vertex;
            break;
        case 7u:
            native.graphicsBufferFacts[0].stage = Stage::Fragment;
            break;
        case 8u:
            native.graphicsBufferFacts[0].entries.erase(
                native.graphicsBufferFacts[0].entries.begin());
            break;
        case 9u:
            native.graphicsBufferFacts[1].entries.erase(
                native.graphicsBufferFacts[1].entries.begin());
            break;
        case 10u:
            native.graphicsBufferFacts[0].entries.push_back(
                native.graphicsBufferFacts[0].entries.front());
            break;
        case 11u:
            native.graphicsBufferFacts[1].entries.push_back(
                native.graphicsBufferFacts[1].entries.front());
            break;
        case 12u:
            native.graphicsBufferFacts[0].entries[0].alignment = 0u;
            break;
        case 13u:
            native.graphicsBufferFacts[1].entries[0].alignment = std::uint64_t{UINT32_MAX} + 1u;
            break;
        case 14u:
            native.graphicsBufferFacts[0].entries[0].requiredBytes = 0u;
            break;
        case 15u:
            native.graphicsBufferFacts[1].entries[0].requiredBytes = 1025u;
            break;
        case 16u:
            queried.maxUniformBufferBindingSizeBytes = 0u;
            break;
        case 17u:
            queried.maxStorageBufferBindingSizeBytes = 0u;
            break;
        case 18u:
            native.metalBindingSpace.pop_back();
            break;
        case 19u:
            native.metalBindingSpace.push_back(native.metalBindingSpace.front());
            break;
        case 20u:
            native.metalBindingSpace[0].active = false;
            break;
        case 21u:
            native.metalBindingSpace[0].type = Type::StorageBuffer;
            break;
        case 22u:
            assert(native.nativeBindingStages.record(Type::UniformBuffer, 0u, Stage::Compute));
            break;
        case 23u:
            native.graphicsBufferFacts[0].entries[0].alignment = UINT32_MAX;
            native.graphicsBufferFacts[1].entries[0].alignment = UINT32_MAX - 1u;
            break;
        case 24u:
            reflection.descriptorBindings.push_back(reflection.descriptorBindings.front());
            break;
        case 25u:
            native.metalBindingSpace[2].metalSlot = 0u;
            break;
        case 26u:
            native.graphicsBufferFacts[0].entries.push_back({1u, 16u, 128u});
            break;
        case 27u:
            native.graphicsBufferFacts[1].entries.push_back({30u, 16u, 128u});
            break;
        case 28u:
            native.nativeBindingStages = {};
            break;
        }
        if (mode >= 1u && mode <= 3u) {
            native.nativeBindingStages = {};
            for (const auto& stage : native.graphicsBufferFacts)
                for (const auto& fact : stage.entries)
                    assert(native.nativeBindingStages.record(
                        Type::UniformBuffer, fact.slot, stage.stage));
        }
        Limits output{1u, 99u, 99u, 99u, 99u};
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto* id = Inspector::inspect(reflection, native, queried, output);
        if (mode < 4u) {
            const auto mask = mode == 0u ? 3u : mode == 1u ? 1u : mode == 2u ? 2u : 0u;
            assert(!id && output.contractVersion == 2u && output.bufferKindCoverageVersion == 1u
                && output.bufferKindMask == mask);
            assert(output.uniformOffsetAlignment == ((mask & 1u) ? 48u : 0u));
            assert(output.storageOffsetAlignment == ((mask & 2u) ? 32u : 0u));
            assert(output.maxUniformBindingBytes == ((mask & 1u) ? 1024u : 0u));
            assert(output.maxStorageBindingBytes == ((mask & 2u) ? 2048u : 0u));
        } else {
            assert(id && !output.contractVersion && !output.bufferKindCoverageVersion
                && !output.bufferKindMask && !output.uniformOffsetAlignment
                && !output.storageOffsetAlignment && !output.maxUniformBindingBytes
                && !output.maxStorageBindingBytes);
            if (mode == 23u)
                assert(!std::strcmp(id, "RHI-METAL-ACCEPTED-GRAPHICS-BUFFER-ALIGNMENT-OVERFLOW"));
        }
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
    }
    std::puts(
        "METAL_GRAPHICS_BUFFER_LIMITS_PASS scalarCases=29 perStage=true checkedLCM=true absentKind=true failureOutputCleared=true allocation0=true nativeAuthority=false");
}

void originalGraphicsCreation(
    const char* path, unsigned revoke, bool accept = false, bool foreground = false)
{
    using Contract = rhi::OriginalSynchronousGraphicsCreationContract;
    // The two actual publications use one foreground or four composite bindings.
    auto d = device(true, true, accept && !foreground ? 4u : 1u);
    auto foreign = device(false);
    const auto registryQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    rhi::OriginalResourceRegistrationBudget registration;
    registration.contractVersion = 10u;
    registration.prototypeOptIn = true;
    registration.maxPipelineLayouts = registration.maxGraphicsPipelines = 1u;
    registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
    registration.maxRetainedOwners = 32u;
    registration.maxCpuMetadataBytes = metadata;
    const char* id = nullptr;
    const auto loss = d->observeOriginalDeviceNativeLoss(id);
    assert(loss && !id);
    assert(!d->prepareOriginalTypedNativeLossRegistration(
        {3u, true}, registration, registryQuota, *loss));
    const auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto acceptedQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4> q;
    for (auto& quota : q)
        quota = core::makeShared<core::CpuMemoryQuota>(metadata);
    {
        core::pmr::String detail;
        auto artifact = shader::GraphicsShaderModuleArtifact::load(
            path, {metadata, metadata * 8u, metadata, 32u, 32u, 4096u, 32u}, q[0], detail);
        if (!artifact)
            std::fprintf(stderr, "Metal graphics module: %s\n", detail.c_str());
        assert(artifact);
        auto input =
            rhi::OriginalGraphicsPipelineInput::prepare(artifact, {1u, metadata}, q[1], detail);
        assert(input);
        auto layout = core::shareUnique(d->createOriginalPipelineLayout(
            rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection)));
        assert(layout && !d->validateOriginalResourceRegistration(*layout));
        rhi::RenderPipelineDesc desc;
        desc.label = "MetalOriginalGraphicsCreation";
        desc.layout = layout.get();
        desc.shaderPackage = &input->package();
        desc.vertexShader = input->package().entrypoints[0];
        desc.fragmentShader = input->package().entrypoints[1];
        desc.colorFormat = rhi::TextureFormat::Rgba16Float;
        desc.colorTargets.push_back({desc.colorFormat});
        desc.hasViewport = desc.hasScissor = true;
        desc.viewport = {0.0f, 0.0f, 32.0f, 24.0f, 0.0f, 1.0f};
        desc.scissor = {0, 0, 32u, 24u};
        if ((!accept && revoke == 8u) || foreground) {
            desc.depthFormat = desc.depthStencil.format = rhi::TextureFormat::D32Float;
            desc.depthStencil.depthTestEnabled = desc.depthStencil.depthWriteEnabled = true;
            desc.depthStencil.depthCompare = rhi::CompareOp::Less;
        }
        const auto target =
            rhi::OriginalGraphicsPipelineInput::selectTarget(input, rhi::Backend::Metal);
        assert(!target.diagnostic && target.sourceCount == 1u && target.combinedModule
            && target.sources[0]);
        const Contract::Budget b{
            1u, true, target.sources[0]->size(), desc.label.size(), metadata, metadata};
        if (!accept && revoke == 8u) {
            auto bareQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
            auto caps = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
            caps.queriedFromNativeDevice = bool(rhi_metal::metalRhiNativeDeviceIdentity());
            auto bare = rhi_metal::MetalOriginalPipeline::create(desc,
                input,
                caps,
                {1u, true, target.sources[0]->size(), metadata, 5000000000u},
                bareQuota,
                id);
            assert(bare && !id && rhi_metal::MetalOriginalPipeline::input(*bare) == input);
            assert(bare->originalArtifactGraphicsDiagnostic());
            bare.reset();
            assert(!bareQuota->liveBytes());
            std::puts(
                "METAL_GRAPHICS_SHARED_BUILDER_PASS bareRetainsOriginalInput=true explicitDeadline=true acceptedAuthority=false cloudFrames=0");
        }
        const auto create = [&](const auto& selectedInput,
                                const auto& selectedLayout,
                                const auto& definition,
                                const auto& owner,
                                const auto& native) {
            return d->createOriginalSynchronousGraphicsCreation(
                selectedInput, selectedLayout, definition, b, owner, native, id);
        };
        const auto refused = [&](const auto& result) {
            assert(!result && id);
        };
        auto bad = rhi::RenderPipelineStateCopy::copy(desc);
        bad.fragmentShader.entryPoint = "wrongFragment";
        refused(create(input, layout, bad, q[2], q[3]));
        bad = rhi::RenderPipelineStateCopy::copy(desc);
        bad.vertexBuffers.push_back({});
        refused(create(input, layout, bad, q[2], q[3]));
        bad = rhi::RenderPipelineStateCopy::copy(desc);
        bad.depthStencil.depthBiasClamp = 1.0f;
        refused(create(input, layout, bad, q[2], q[3]));
        const auto fakeInput = std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>(
            input.get(), [](const auto*) {});
        const auto fakeLayout =
            std::shared_ptr<rhi::PipelineLayout>(layout.get(), [](const auto*) {});
        refused(create(fakeInput, layout, desc, q[2], q[3]));
        refused(create(input, fakeLayout, desc, q[2], q[3]));
        refused(foreign->createOriginalSynchronousGraphicsCreation(
            input, layout, desc, b, q[2], q[3], id));
        auto null = rhi::createNullDevice();
        refused(null->createOriginalSynchronousGraphicsCreation(
            input, layout, desc, b, q[2], q[3], id));
        assert(!std::strcmp(id, "RHI-SYNC-GRAPHICS-CREATION-NULL-VALIDATOR-ONLY"));
        for (const auto& overlap : {q[0], q[1], q[3], registryQuota})
            refused(create(input, layout, desc, overlap, q[3]));
        for (unsigned field = 0u; field < 4u; ++field) {
            auto shortBudget = b;
            auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
            if (field == 0u)
                --shortBudget.maxTargetBytes;
            if (field == 1u)
                --shortBudget.maxLabelBytes;
            if (field == 2u)
                shortBudget.maxCpuOwnerBytes = 1u;
            if (field == 3u)
                shortBudget.maxCpuNativeBytes = 1u;
            refused(d->createOriginalSynchronousGraphicsCreation(input,
                layout,
                desc,
                shortBudget,
                field == 2u ? tiny : q[2],
                field == 3u ? tiny : q[3],
                id));
            assert(!tiny->liveBytes() && !q[2]->liveBytes() && !q[3]->liveBytes());
        }
        auto receipt = create(input, layout, desc, q[2], q[3]);
        if (!receipt)
            std::fprintf(stderr, "original Metal graphics creation: %s\n", id ? id : "null");
        assert(receipt && !id && !rhi::OriginalGraphicsPipelineCreation::validateOriginal(receipt));
        assert(receipt->originalInput() == input && receipt->originalLayout() == layout);
        const auto fake = std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>(
            receipt.get(), [](const auto*) {});
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        assert(rhi::OriginalGraphicsPipelineCreation::validateOriginal(fake));
        for (const auto& quota : q)
            assert(receipt->usesCpuQuotaDomain(quota));
        assert(!receipt->validate());
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        auto wrapperQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        assert(!d->createOriginalArtifactGraphicsPipeline(
            receipt, {1u, true, metadata}, wrapperQuota));
        assert(d->lastOriginalResourceRegistrationDiagnostic()
            == "RHI-ACCEPTED-GRAPHICS-FACTORY-METAL-PAYLOAD-MISSING");
        assert(!wrapperQuota->liveBytes());
        refused(create(input, layout, desc, q[2], q[3]));
        assert(!std::strcmp(id, "RHI-SYNC-GRAPHICS-CREATION-DIRECT-BUDGET"));
        receipt.reset();
        assert(!q[2]->liveBytes() && !q[3]->liveBytes());
        receipt = create(input, layout, desc, q[2], q[3]);
        assert(receipt && !id);
        using Source = rhi::OriginalCommandGraphicsPipelineSource;
        std::shared_ptr<const Source> source;
        core::UniquePtr<SynchronousLossGroupProbe<rhi::RenderPipeline>> group;
        if (accept) {
            const auto counterfeit = std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>(
                receipt.get(), [](const auto*) {});
            assert(!d->createOriginalArtifactGraphicsPipeline(
                counterfeit, {2u, true, metadata}, acceptedQuota));
            assert(!foreign->createOriginalArtifactGraphicsPipeline(
                receipt, {2u, true, metadata}, acceptedQuota));
            for (const auto& overlap : {q[0], q[1], q[2], q[3], registryQuota})
                assert(!d->createOriginalArtifactGraphicsPipeline(
                    receipt, {2u, true, metadata}, overlap));
            auto tiny = core::makeShared<core::CpuMemoryQuota>(1u);
            assert(!d->createOriginalArtifactGraphicsPipeline(receipt, {2u, true, 1u}, tiny));
            assert(!tiny->liveBytes() && !acceptedQuota->liveBytes());
            assert(acceptedQuota->tryReserve(metadata));
            assert(!d->createOriginalArtifactGraphicsPipeline(
                receipt, {2u, true, metadata}, acceptedQuota));
            acceptedQuota->release(metadata);
            assert(!acceptedQuota->liveBytes());
            auto unique = d->createOriginalArtifactGraphicsPipeline(
                receipt, {2u, true, metadata}, acceptedQuota);
            if (!unique)
                std::fprintf(stderr,
                    "accepted Metal graphics: %.*s\n",
                    int(d->lastOriginalResourceRegistrationDiagnostic().size()),
                    d->lastOriginalResourceRegistrationDiagnostic().data());
            assert(unique);
            auto pipeline =
                rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(std::move(unique), id);
            assert(pipeline && !id && !d->validateOriginalResourceRegistration(*pipeline));
            assert(!pipeline->originalArtifactGraphicsDiagnostic());
            assert(pipeline->originalGraphicsCreation() == receipt);
            assert(!pipeline->originalDescriptorBufferBindingDiagnostic(
                {1u, true}, rhi::DescriptorType::UniformBuffer, 16u, 0u));
            assert(bool(pipeline->originalDescriptorBufferBindingDiagnostic(
                       {1u, true}, rhi::DescriptorType::StorageBuffer, 64u, 0u))
                == foreground);
            assert(pipeline->originalDescriptorBufferBindingDiagnostic(
                {1u, true}, rhi::DescriptorType::StorageBuffer, UINT64_MAX, 0u));
            assert(!d->createOriginalArtifactGraphicsPipeline(
                receipt, {2u, true, metadata}, acceptedQuota));
            const rhi::OriginalCommandPipelineSourceBudget sb{2u,
                true,
                Source::kind,
                pipeline->originalResourceRegistration().generation(),
                metadata,
                Source::exclusiveOwnerCount};
            auto fakePipeline =
                std::shared_ptr<const rhi::RenderPipeline>(pipeline.get(), [](const auto*) {});
            assert(!Source::prepareOriginal(fakePipeline, sb, sourceQuota, *d, id));
            assert(!Source::prepareOriginal(pipeline, sb, sourceQuota, *foreign, id));
            for (const auto& overlap : {q[0], q[1], q[2], q[3], registryQuota, acceptedQuota})
                assert(!Source::prepareOriginal(pipeline, sb, overlap, *d, id));
            auto shortSource = sb;
            --shortSource.maxRetainedOwners;
            assert(!Source::prepareOriginal(pipeline, shortSource, sourceQuota, *d, id));
            shortSource = sb;
            ++shortSource.registrationGeneration;
            assert(!Source::prepareOriginal(pipeline, shortSource, sourceQuota, *d, id));
            shortSource = sb;
            shortSource.sourceCpuBytes = 1u;
            assert(!Source::prepareOriginal(pipeline, shortSource, tiny, *d, id));
            assert(!tiny->liveBytes());
            assert(sourceQuota->tryReserve(metadata));
            assert(!Source::prepareOriginal(pipeline, sb, sourceQuota, *d, id));
            sourceQuota->release(metadata);
            assert(!sourceQuota->liveBytes());
            source = Source::prepareOriginal(pipeline, sb, sourceQuota, *d, id);
            if (id)
                std::fprintf(stderr, "Metal graphics Source: %s\n", id);
            assert(source && !id && !Source::validateOriginal(source, *d));
            group = core::makeUnique<SynchronousLossGroupProbe<rhi::RenderPipeline>>(*d, pipeline);
            group->verify(
                *d, *foreign, pipeline, {q[0], q[1], q[2], q[3], registryQuota, acceptedQuota});
            const auto fakeSource = std::shared_ptr<const Source>(source.get(), [](const auto*) {});
            assert(Source::validateOriginal(fakeSource, *d));
            assert(Source::validateOriginal(source, *foreign));
            assert(!source->nativeExecutionProven());
            if (revoke == 10u)
                publicPipelineObservation(source, pipeline, sourceQuota, *d, *foreign);
            const std::weak_ptr<const rhi::RenderPipeline> retained = pipeline;
            pipeline.reset();
            assert(!retained.expired() && !Source::validateOriginal(source, *d));
            assert(acceptedQuota->liveBytes() && sourceQuota->liveBytes());
        }
        if (accept && revoke == 10u) {
            std::weak_ptr<const rhi::OriginalGraphicsPipelineCreation> retained = receipt;
            group.reset();
            receipt.reset();
            layout.reset();
            input.reset();
            artifact.reset();
            assert(!retained.expired() && !Source::validateOriginal(source, *d));
            for (const auto& quota : q)
                assert(quota->liveBytes());
            source.reset();
            assert(retained.expired() && !acceptedQuota->liveBytes() && !sourceQuota->liveBytes());
            // This observer still owns the quota-bound shared control block.
            retained.reset();
            for (const auto& quota : q)
                assert(!quota->liveBytes());
            std::puts(
                "METAL_GRAPHICS_ACCEPTANCE_RELEASE_PASS SourceRetainsCreation=true finalOwnerDrainsAllSixQuotas=true cloudFrames=0");
            std::printf(
                "METAL_GRAPHICS_ACCEPTANCE_PROFILE_PASS foreground=%d normalRelease=true cloudFrames=0\n",
                foreground);
            return;
        }
        if (!accept && revoke == 8u) {
            std::weak_ptr<const rhi::OriginalGraphicsPipelineInput> retained = input;
            input.reset();
            artifact.reset();
            layout.reset();
            assert(!retained.expired() && !receipt->validate());
            receipt.reset();
            assert(retained.expired());
            retained.reset();
        } else {
            const auto beforeRevoke = core::defaultCpuAllocator().stats().allocationCount;
            if (revoke < 2u || (revoke >= 3u && revoke <= 5u)) {
                const auto& changed = revoke < 2u ? q[2u + revoke]
                    : revoke == 3u                ? registryQuota
                    : revoke == 4u                ? q[1]
                                                  : q[0];
                changed->setLimitBytes(metadata - 1u);
                assert(receipt->validate());
                changed->setLimitBytes(metadata);
                assert(receipt->validate());
            } else if (revoke == 2u) {
                d->retireOriginalDeviceLifetime();
                assert(receipt->validate());
            } else if (revoke == 6u) {
                assert(!d->detachOriginalDeviceNativeLoss());
                assert(receipt->validate());
            } else if (revoke == 7u) {
                d.reset();
                assert(receipt->validate());
            } else {
                const auto& changed = revoke == 8u ? acceptedQuota : sourceQuota;
                changed->setLimitBytes(metadata - 1u);
                assert(Source::validateOriginal(source));
            }
            if (accept)
                assert(Source::validateOriginal(source));
            if (accept && revoke != 9u)
                group->revoked(d.get(), source->originalPipeline(), revoke);
            assert(beforeRevoke == core::defaultCpuAllocator().stats().allocationCount);
        }
    }
    for (const auto& quota : q)
        assert(!quota->liveBytes());
    assert(!acceptedQuota->liveBytes() && !sourceQuota->liveBytes());
    if (accept)
        std::printf(
            "METAL_ORIGINAL_GRAPHICS_ACCEPTANCE_PASS revoke=%u foreground=%d canonicalSource=true allocation0=true quotasDrained=true execution=false cloudFrames=0\n",
            revoke,
            foreground);
    std::printf(
        "METAL_ORIGINAL_GRAPHICS_CREATION_PASS scope=%u originalDevice=true canonicalReceipt=true directCapacity=2 quotasDrained=true execution=false cloudFrames=0\n",
        revoke);
}

void rejectProbePin(const char* path)
{
    core::pmr::String detail{&core::defaultCpuMemoryResource()};
    {
        const auto artifact = shader::SingleEntryShaderArtifact::load(path,
            {metadata, metadata * 8u, metadata, 2u},
            core::makeShared<core::CpuMemoryQuota>(metadata),
            detail);
        assert(artifact && !shader::SingleEntryShaderArtifact::validateOriginal(artifact));
    }
    std::puts("UNPINNED_ARTIFACT_LOAD_PASS");
    auto d = device();
    const auto q = quotas();
    const char* diagnostic = nullptr;
    assert(!Session::prepare(*d, request(), budget, q, diagnostic));
    assert(diagnostic && !std::strcmp(diagnostic, "RHI-DISPATCH-VALIDATION-PROBE-CUSTODY"));
    assert(!d->prepareOriginalComputeDispatchValidation(request(), budget, q, diagnostic));
    assert(diagnostic && !std::strcmp(diagnostic, "RHI-DISPATCH-VALIDATION-PROBE-CUSTODY"));
    clean(q);
    std::puts(
        "METAL_DISPATCH_PROBE_PIN_REJECT_PASS valid publication grants no backend probe authority");
}

template <class Pipeline>
void ordinaryBufferBindings(rhi::GpuDevice& d,
    core::UniquePtr<Pipeline> unique,
    const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& contentQuota,
    const char* name)
{
    using Inspection = rhi_metal::MetalOriginalBufferBindingInspection;
    using Sources = rhi::OriginalCommandResourceSources;
    constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;
    using Source = Inspection::Contract::PipelineSource<compute>;
    const char* id = nullptr;
    core::CpuMemoryQuotaScope scope(resourceQuota);
    auto pipeline = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(std::move(unique), id);
    assert(pipeline && !id);
    auto source = Source::prepareOriginal(pipeline,
        {2u,
            true,
            Source::kind,
            pipeline->originalResourceRegistration().generation(),
            metadata,
            Source::exclusiveOwnerCount},
        sourceQuota,
        d,
        id);
    assert(source && !id);
    core::pmr::Vector<Sources::Source> prefix;
    core::pmr::Vector<rhi::OriginalCommandDescriptorSetElement> elements, textureElements;
    const auto input = rhi_metal::MetalOriginalPipeline::input(*pipeline);
    const auto native = rhi_metal::metalRhiNativePayloadFor(*pipeline);
    const auto entries = [&] {
        const auto& artifact = input->artifact();
        if constexpr (compute)
            return std::array{artifact, std::shared_ptr<const shader::SingleEntryShaderArtifact>{}};
        else
            return std::array{artifact->vertex(), artifact->fragment()};
    }();
    for (const auto& binding : input->package().reflection.descriptorBindings) {
        if (binding.type != rhi::DescriptorType::UniformBuffer
            && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer
            && binding.type != rhi::DescriptorType::StorageBuffer)
            continue;
        const auto* mapped =
            rhi_metal::MetalOriginalBufferLimitInspection::mapping(binding, *native);
        assert(mapped);
        const auto& memory =
            compute ? native->computeBufferMemberMemory : native->graphicsBufferMemberMemory[1];
        const auto& facts =
            compute ? native->computeBufferFacts : native->graphicsBufferFacts[1].entries;
        const auto* fact =
            rhi_metal::MetalOriginalBufferLimitInspection::fact(facts, mapped->metalSlot);
        assert(fact && memory.version == 1u);
        shader::SpirvBufferDeclarationLayout declaration;
        assert(!shader::SingleEntryShaderArtifact::originalBufferDeclarationLayout(
            entries[compute ? 0u : 1u], binding.set, binding.binding, declaration));
        const auto alignment = std::max<std::uint64_t>(256u, fact->alignment);
        const auto offset = declaration.hasRuntimeArray
            ? std::lcm(alignment, declaration.runtimeArrayStrideBytes)
            : alignment;
        const auto bytes = declaration.hasRuntimeArray
            ? declaration.runtimeArrayOffsetBytes + 2u * declaration.runtimeArrayStrideBytes
            : declaration.staticOccupiedExtentBytes;
        std::printf(
            "METAL_BUFFER_BINDING_FACT module=%s binding=%u access=%u elementType=%u required=%llu bytes=%llu stride=%llu\n",
            name,
            binding.binding,
            fact->access,
            fact->elementType,
            static_cast<unsigned long long>(fact->requiredBytes),
            static_cast<unsigned long long>(bytes),
            static_cast<unsigned long long>(declaration.runtimeArrayStrideBytes));
        rhi::BufferDesc desc;
        desc.label = "ordinary native buffer binding";
        desc.sizeBytes = offset + bytes;
        desc.usage = binding.type == rhi::DescriptorType::UniformBuffer
            ? rhi::ResourceUsage::UniformBuffer
            : rhi::ResourceUsage::StorageBuffer;
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.initialState = binding.type == rhi::DescriptorType::StorageBuffer
            ? rhi::ResourceState::ShaderWrite
            : rhi::ResourceState::ShaderRead;
        auto buffer = d.createBuffer(desc);
        assert(buffer);
        buffer->sealHostWrites();
        auto lease = rhi::OriginalCommandBufferLease::prepareOriginal(
            buffer, {1u, true, 2u, metadata}, sourceQuota, id);
        assert(lease && !id);
        rhi::OriginalCommandDescriptorSetElement e;
        e.sourceIndex = prefix.size();
        e.registrationGeneration =
            lease->originalObject()->originalResourceRegistration().generation();
        e.write.binding = binding.binding;
        e.write.type = binding.type;
        e.write.resource = lease->resource();
        e.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
        e.write.resourceState = desc.initialState;
        e.write.bufferBindingRange = {2u, offset, bytes};
        e.bufferAccessOffset = offset;
        e.bufferAccessBytes = bytes;
        e.stages = compute ? rhi::PipelineStage::ComputeShader : rhi::PipelineStage::FragmentShader;
        e.access = binding.type == rhi::DescriptorType::StorageBuffer
            ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
            : rhi::ResourceAccess::ShaderRead;
        elements.push_back(e);
        prefix.push_back(lease);
    }
    for (const auto& binding : input->package().reflection.descriptorBindings) {
        if (binding.type != rhi::DescriptorType::SampledTexture)
            continue;
        rhi::TextureDesc td;
        td.extent = {4u, 4u, 1u};
        td.format =
            binding.depthTexture ? rhi::TextureFormat::D32Float : rhi::TextureFormat::Rgba16Float;
        td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment;
        const auto nativeBytes = d.queryOriginalContentTextureAllocation(td, id, 4u);
        assert(nativeBytes && !id);
        std::uint64_t footprint = 0u;
        assert(!rhi::semantics::TextureUploadDestinationValidation::footprint(td, footprint));
        auto texture = d.createOriginalContentTexture(
            td, {4u, true, 1u, 1u, footprint, 2u, metadata, 1u, nativeBytes}, contentQuota, id);
        if (!texture)
            std::fprintf(stderr, "binding texture creation: %s\n", id ? id : "missing");
        assert(texture && !id);
        auto parent = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, metadata}, sourceQuota, id);
        assert(parent && !id);
        prefix.push_back(parent);
        rhi::TextureViewDesc vd;
        vd.format = td.format;
        vd.aspect = binding.depthTexture ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
        vd.usage = rhi::TextureUsage::Sampled;
        auto view = d.createTextureView(*parent->originalObject(), vd);
        assert(view);
        auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, {1u, true, 2u, metadata}, sourceQuota, id);
        assert(viewLease && !id);
        auto viewSource = rhi::OriginalCommandTextureViewSource::prepareOriginal(viewLease,
            parent,
            {1u, true, rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount, metadata},
            sourceQuota,
            id);
        assert(viewSource && !id);
        rhi::OriginalCommandDescriptorSetElement e;
        e.sourceIndex = prefix.size();
        e.registrationGeneration =
            viewLease->originalObject()->originalResourceRegistration().generation();
        e.write.binding = binding.binding;
        e.write.type = binding.type;
        e.write.resource = viewSource->resource();
        e.write.resourceKind = rhi::DescriptorResourceKind::TextureView;
        e.write.resourceState = rhi::ResourceState::ShaderRead;
        e.stages = compute ? rhi::PipelineStage::ComputeShader : rhi::PipelineStage::FragmentShader;
        e.access = rhi::ResourceAccess::ShaderRead;
        prefix.push_back(viewSource);
        textureElements.push_back(e);
    }
    rhi::OriginalCommandAdmissionBudget budget;
    budget.contractVersion = 1u;
    budget.prototypeOptIn = true;
    budget.maxCommands = budget.maxReferences = budget.maxInputDescriptors = 32u;
    budget.maxStateObservations = 32u;
    budget.maxGraphOwners = 128u;
    budget.maxCpuBytes.fill(metadata);
    budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 8u;
    budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 2u;
    budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] = 2u;
    budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] = 1u;
    auto inventory = Sources::prepareColdOriginalTyped(
        prefix.data(), prefix.size(), budget, {5u, true, 1u}, sourceQuota, id);
    if (id)
        std::fprintf(stderr, "binding inventory: %s\n", id);
    assert(inventory && !id);
    const auto pipelinePosition = prefix.size();
    assert(!Sources::appendOriginalTyped(inventory, source, d));
    const Inspection::Contract::Request request{compute ? 2u : 1u, true};
    std::size_t inspectionIndex = 0u;
    const auto inspect = [&](const auto& sources,
                             std::size_t position,
                             const auto& element,
                             bool expected) {
        Inspection::Observation result;
        result.available = true;
        result.physicalSlot = 0u;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto* error = Inspection::inspect(request, d, sources, position, 0u, element, result);
        ++inspectionIndex;
        if (bool(error) == expected || result.available != expected)
            std::fprintf(stderr,
                "buffer binding %s/%u check=%zu expected=%d available=%d: %s\n",
                name,
                element.write.binding,
                inspectionIndex,
                expected,
                result.available,
                error ? error : "accepted");
        assert(bool(error) != expected && result.available == expected);
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        if (expected) {
            assert(result.buffer && result.nativeIdentity.matches(result.buffer->resource));
            assert(result.sizeBytes == element.bufferAccessBytes
                && result.offsetBytes == element.bufferAccessOffset);
            assert(result.allocationBytes >= result.sizeBytes + result.offsetBytes
                && !result.nativeExecutionProven());
            if constexpr (!compute)
                for (std::size_t i = 0u; i < result.graphicsRanges.size(); ++i) {
                    const auto& range = result.graphicsRanges[i];
                    if (range.stage == rhi::ShaderStage::None)
                        continue;
                    auto changed = range;
                    changed.fact.active = range.fact.active ? 0u : 1u;
                    assert(rhi_metal::MetalRhiStageBufferRangeValidator::validate(
                        native->graphicsBufferFacts[i],
                        range.stage,
                        result.physicalSlot,
                        result.offsetBytes,
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            prefix[element.sourceIndex])
                            ->layout()
                            .sizeBytes,
                        changed));
                }
        } else {
            assert(result.physicalSlot == UINT32_MAX && !result.buffer && !result.allocationBytes);
        }
    };
    auto counterfeit = std::shared_ptr<const Sources>(inventory.get(), [](const auto*) {});
    for (const auto& e : elements) {
        inspect(inventory, pipelinePosition, e, true);
        inspect(counterfeit, pipelinePosition, e, false);
        inspect(inventory, SIZE_MAX, e, false);
        auto bad = e;
        ++bad.registrationGeneration;
        inspect(inventory, pipelinePosition, bad, false);
        bad = e;
        --bad.write.bufferBindingRange.sizeBytes;
        --bad.bufferAccessBytes;
        inspect(inventory, pipelinePosition, bad, false);
        bad = e;
        ++bad.write.bufferBindingRange.offsetBytes;
        ++bad.bufferAccessOffset;
        --bad.write.bufferBindingRange.sizeBytes;
        --bad.bufferAccessBytes;
        inspect(inventory, pipelinePosition, bad, false);
        auto& facts = compute ? native->computeBufferFacts : native->graphicsBufferFacts[1].entries;
        const rhi::ShaderReflectionBinding* reflected = nullptr;
        for (const auto& binding : input->package().reflection.descriptorBindings)
            if (binding.set == 0u && binding.binding == e.write.binding) {
                assert(!reflected);
                reflected = &binding;
            }
        assert(reflected);
        const auto* mapped =
            rhi_metal::MetalOriginalBufferLimitInspection::mapping(*reflected, *native);
        assert(mapped);
        auto* fact = const_cast<rhi_metal::MetalRhiBufferBindingFact*>(
            rhi_metal::MetalOriginalBufferLimitInspection::fact(facts, mapped->metalSlot));
        const auto saved = *fact;
        fact->access = 2u;
        inspect(inventory, pipelinePosition, e, false);
        *fact = saved;
        fact->active = UINT32_MAX;
        inspect(inventory, pipelinePosition, e, false);
        *fact = saved;
        if constexpr (!compute) {
            auto* extra = const_cast<rhi_metal::MetalRhiBufferBindingFact*>(
                rhi_metal::MetalOriginalBufferLimitInspection::fact(
                    native->graphicsBufferFacts[0].entries, mapped->metalSlot));
            if (extra) {
                assert(extra->active == 0u);
                const auto savedExtra = *extra;
                extra->active = 1u;
                inspect(inventory, pipelinePosition, e, false);
                *extra = savedExtra;
                native->graphicsBufferFacts[0].stage = rhi::ShaderStage::Fragment;
                inspect(inventory, pipelinePosition, e, false);
                native->graphicsBufferFacts[0].stage = rhi::ShaderStage::Vertex;
            }
        }
        const auto savedStages = native->nativeBindingStages;
        native->nativeBindingStages = {};
        inspect(inventory, pipelinePosition, e, false);
        native->nativeBindingStages = savedStages;
        const auto& lease =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(prefix[e.sourceIndex]);
        auto payload = rhi_metal::metalRhiNativePayloadFor(*lease->originalObject());
        payload->resource.hazardsExplicitlyTracked = false;
        inspect(inventory, pipelinePosition, e, false);
        payload->resource.hazardsExplicitlyTracked = true;
        inspect(inventory, pipelinePosition, e, true);
    }
    using TextureInspection = rhi_metal::MetalOriginalTextureBindingInspection;
    const auto inspectTexture = [&](const auto& sources, const auto& element, bool expected) {
        TextureInspection::Observation result;
        result.available = true;
        result.physicalSlot = 0u;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto* error =
            TextureInspection::inspect(request, d, sources, pipelinePosition, 0u, element, result);
        if (bool(error) == expected || result.available != expected)
            std::fprintf(stderr,
                "texture binding %s/%u expected=%d: %s\n",
                name,
                element.write.binding,
                expected,
                error ? error : "accepted");
        assert(bool(error) != expected && result.available == expected);
        assert(before == core::defaultCpuAllocator().stats().allocationCount);
        if (expected) {
            assert(result.parent && result.view && result.allocationBytes);
            assert(result.parentIdentity.matches(result.parent->resource)
                && result.viewIdentity.matches(result.view->resource));
            assert(!result.nativeExecutionProven());
        } else {
            assert(result.physicalSlot == UINT32_MAX && !result.parent && !result.view
                && !result.allocationBytes);
        }
    };
    for (const auto& e : textureElements) {
        inspectTexture(inventory, e, true);
        inspectTexture(counterfeit, e, false);
        auto bad = e;
        ++bad.registrationGeneration;
        inspectTexture(inventory, bad, false);
        bad = e;
        bad.access = rhi::ResourceAccess::ShaderWrite;
        inspectTexture(inventory, bad, false);
        const auto& viewSource =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                prefix[e.sourceIndex]);
        const auto parent = rhi_metal::metalRhiNativePayloadFor(
            *viewSource->originalParentLease()->originalObject());
        const auto view = rhi_metal::metalRhiNativePayloadFor(*viewSource->originalObject());
        const auto savedObject = view->resource.nativeObject;
        view->resource.nativeObject = parent->resource.nativeObject;
        inspectTexture(inventory, e, false);
        view->resource.nativeObject = savedObject;
        const auto savedToken = view->resource.allocationReleaseToken;
        view->resource.allocationReleaseToken =
            std::shared_ptr<void>(savedToken.get(), [](void*) {});
        inspectTexture(inventory, e, false);
        view->resource.allocationReleaseToken = savedToken;
        parent->resource.allocationReleaseToken.reset();
        view->resource.allocationReleaseToken.reset();
        inspectTexture(inventory, e, false);
        parent->resource.allocationReleaseToken = savedToken;
        view->resource.allocationReleaseToken = savedToken;
        const auto savedDevice = view->resource.nativeDevice;
        view->resource.nativeDevice = std::shared_ptr<void>(savedDevice.get(), [](void*) {});
        inspectTexture(inventory, e, false);
        view->resource.nativeDevice = savedDevice;
        parent->resource.hazardsExplicitlyTracked = false;
        inspectTexture(inventory, e, false);
        parent->resource.hazardsExplicitlyTracked = true;
        auto& facts = compute ? native->computeTextureFacts : native->graphicsTextureFacts;
        for (auto& fact : facts) {
            const auto& reflection = input->package().reflection.descriptorBindings;
            const auto reflected =
                std::find_if(reflection.begin(), reflection.end(), [&](const auto& b) {
                    return b.set == 0u && b.binding == e.write.binding;
                });
            assert(reflected != reflection.end());
            const auto* mapping =
                rhi_metal::MetalOriginalBufferLimitInspection::mapping(*reflected, *native);
            if (fact.slot != mapping->metalSlot)
                continue;
            const auto saved = fact;
            fact.depth = !saved.depth;
            inspectTexture(inventory, e, false);
            fact = saved;
            fact.access = MTLArgumentAccessWriteOnly;
            inspectTexture(inventory, e, false);
            fact = saved;
            fact.active = UINT32_MAX;
            inspectTexture(inventory, e, false);
            fact = saved;
            if (fact.stage == rhi::ShaderStage::Vertex) {
                assert(!saved.active);
                fact.active = 1u;
                inspectTexture(inventory, e, false);
                fact = saved;
            }
        }
        const auto savedFacts = facts;
        const auto savedVersion =
            compute ? native->computeTextureFactsVersion : native->graphicsTextureFactsVersion;
        auto& version =
            compute ? native->computeTextureFactsVersion : native->graphicsTextureFactsVersion;
        version = 0u;
        inspectTexture(inventory, e, false);
        version = savedVersion;
        facts.clear();
        inspectTexture(inventory, e, false);
        facts = savedFacts;
        facts.insert(facts.end(), savedFacts.begin(), savedFacts.end());
        inspectTexture(inventory, e, false);
        facts = savedFacts;
        const auto savedStages = native->nativeBindingStages;
        native->nativeBindingStages = {};
        inspectTexture(inventory, e, false);
        native->nativeBindingStages = savedStages;
        inspectTexture(inventory, e, true);
    }
    sourceQuota->setLimitBytes(metadata - 1u);
    for (const auto& e : elements)
        inspect(inventory, pipelinePosition, e, false);
    for (const auto& e : textureElements)
        inspectTexture(inventory, e, false);
    sourceQuota->setLimitBytes(metadata);
    for (const auto& e : textureElements)
        inspectTexture(inventory, e, true);
    std::printf(
        "METAL_ORDINARY_TEXTURE_BINDING_PASS module=%s textures=%zu sameInventory=true actualParentView=true allocation0=true groupPublication=false cloudFrames=0\n",
        name,
        textureElements.size());
    for (const auto& e : elements)
        inspect(inventory, pipelinePosition, e, true);
    std::printf(
        "METAL_ORDINARY_BUFFER_BINDING_PASS module=%s buffers=%zu finiteOffset=true accessRangeIdentity=true quotaPressureAndRestore=true allocation0=true groupPublication=false cloudFrames=0\n",
        name,
        elements.size());
    // Use the same complete canonical inventory in the real descriptor owner.
    using SetSource = rhi::OriginalCommandDescriptorSetSource;
    SynchronousLossGroupProbe<Pipeline> publication(d, pipeline);
    core::pmr::Vector<rhi::OriginalCommandDescriptorSetElement> groupElements(elements);
    groupElements.insert(groupElements.end(), textureElements.begin(), textureElements.end());
    const auto beginGroup = [&](const auto& selectedElements,
                                const auto& request,
                                const auto& selectedQuotas,
                                std::uint32_t set = 0u) {
        return d.beginOriginalGeneralDescriptorWrite(*publication.set,
            pipeline,
            inventory,
            set,
            selectedElements.data(),
            selectedElements.size(),
            request,
            selectedQuotas);
    };
    std::uint64_t actualBytes = 0u;
    for (const auto& e : elements) {
        Inspection::Observation observed;
        assert(!Inspection::inspect(request, d, inventory, pipelinePosition, 0u, e, observed));
        actualBytes += observed.allocationBytes;
    }
    for (const auto& e : textureElements) {
        TextureInspection::Observation observed;
        assert(
            !TextureInspection::inspect(request, d, inventory, pipelinePosition, 0u, e, observed));
        actualBytes += observed.allocationBytes;
    }
    publication.request.maxRetainedAllocationBytes = actualBytes;
    const auto beforePreflight = core::defaultCpuAllocator().stats().allocationCount;
    auto badRequest = publication.request;
    --badRequest.maxRetainedAllocationBytes;
    assert(beginGroup(groupElements, badRequest, publication.quotas));
    badRequest = publication.request;
    badRequest.lossGroupJoinVersion = 0u;
    badRequest.lossGroupJoinOptIn = false;
    assert(beginGroup(groupElements, badRequest, publication.quotas));
    badRequest = publication.request;
    --badRequest.maxRetainedOwnerSlots;
    assert(beginGroup(groupElements, badRequest, publication.quotas));
    assert(beginGroup(groupElements, publication.request, publication.quotas, 1u));
    auto overlap = publication.quotas;
    overlap[0] = sourceQuota;
    assert(beginGroup(groupElements, publication.request, overlap));
    assert(beforePreflight == core::defaultCpuAllocator().stats().allocationCount);
    auto badElements = groupElements;
    if (badElements.size() > 1u)
        badElements.back() = badElements.front();
    else
        ++badElements.front().registrationGeneration;
    const auto beforeDuplicate = core::defaultCpuAllocator().stats().allocationCount;
    assert(beginGroup(badElements, publication.request, publication.quotas));
    assert(beforeDuplicate == core::defaultCpuAllocator().stats().allocationCount);
    for (std::size_t axis = 0u; axis < publication.quotas.size(); ++axis) {
        auto tiny = publication.quotas;
        tiny[axis] = core::makeShared<core::CpuMemoryQuota>(1u);
        auto tinyRequest = publication.request;
        tinyRequest.maxCpuBytes[axis] = 1u;
        assert(beginGroup(groupElements, tinyRequest, tiny));
        assert(!publication.set->hasOriginalGeneralContent()
            && publication.set->contentRevision() == publication.initialRevision);
        assert(!rhi_metal::metalRhiNativePayloadFor(*publication.set));
        for (const auto& q : tiny)
            assert(!q->liveBytes());
    }
    const auto* beginError = beginGroup(groupElements, publication.request, publication.quotas);
    if (beginError)
        std::fprintf(stderr, "ordinary group %s: %s\n", name, beginError);
    assert(!beginError);
    assert(!publication.set->hasOriginalGeneralContent()
        && publication.set->contentRevision() == publication.initialRevision);
    assert(!rhi_metal::metalRhiNativePayloadFor(*publication.set));
    const auto beforeCancel = core::defaultCpuAllocator().stats().allocationCount;
    publication.quotas[1]->setLimitBytes(metadata - 1u);
    assert(publication.set->finishOriginalGeneralWrite());
    publication.quotas[1]->setLimitBytes(metadata);
    assert(!publication.set->cancelOriginalGeneralWrite());
    assert(beforeCancel == core::defaultCpuAllocator().stats().allocationCount);
    for (const auto& q : publication.quotas)
        assert(!q->liveBytes());
    assert(!beginGroup(groupElements, publication.request, publication.quotas));
    auto& mutableFacts =
        compute ? native->computeBufferFacts : native->graphicsBufferFacts[1].entries;
    const auto savedFact = mutableFacts.front();
    mutableFacts.front().active = UINT32_MAX;
    const auto beforeFinish = core::defaultCpuAllocator().stats().allocationCount;
    assert(publication.set->finishOriginalGeneralWrite());
    assert(publication.set->contentRevision() == publication.initialRevision);
    mutableFacts.front() = savedFact;
    assert(!publication.set->finishOriginalGeneralWrite());
    assert(beforeFinish == core::defaultCpuAllocator().stats().allocationCount);
    assert(publication.set->hasOriginalGeneralContent()
        && publication.set->contentRevision() == publication.initialRevision + 1u);
    assert(publication.set->cancelOriginalGeneralWrite()
        && publication.set->finishOriginalGeneralWrite());
    assert(!rhi_metal::metalRhiNativePayloadFor(*publication.set));
    rhi::OriginalCommandDescriptorSourceBudget sourceBudget{compute ? 7u : 5u,
        true,
        static_cast<std::uint32_t>(groupElements.size()),
        static_cast<std::uint32_t>(groupElements.size()),
        SetSource::finiteExclusiveRetainedOwnerCount,
        metadata};
    sourceBudget.finitePipelineSourceIndex = pipelinePosition;
    sourceBudget.finiteFootprintContractVersion = 2u;
    sourceBudget.finiteFullBoundViewOptIn = true;
    auto groupSource = SetSource::prepareOriginal(publication.set,
        publication.arena,
        inventory,
        groupElements.data(),
        groupElements.size(),
        sourceBudget,
        sourceQuota,
        d,
        id);
    if (id)
        std::fprintf(stderr, "ordinary group Source %s: %s\n", name, id);
    assert(groupSource && !id && !publication.set);
    assert(!SetSource::validateOriginal(groupSource));
    auto nativeGroup = rhi_metal::metalRhiOriginalSynchronousPayloadFor(groupSource);
    assert(nativeGroup && nativeGroup->bindings.size() == groupElements.size());
    const auto beforeFrozen = core::defaultCpuAllocator().stats().allocationCount;
    assert(!rhi_metal::metalRhiNativePayloadFor(*groupSource->originalObject()));
    const auto savedResource = nativeGroup->bindings.front().resourceId;
    auto& changedGroup = const_cast<rhi_metal::MetalRhiNativeDescriptorGroupPayload&>(*nativeGroup);
    changedGroup.bindings.front().resourceId = {};
    assert(SetSource::validateOriginal(groupSource));
    assert(!rhi_metal::metalRhiOriginalSynchronousPayloadFor(groupSource));
    changedGroup.bindings.front().resourceId = savedResource;
    assert(!SetSource::validateOriginal(groupSource));
    assert(beforeFrozen == core::defaultCpuAllocator().stats().allocationCount);
    const auto savedPipelineState = native->pipelineState;
    native->pipelineState = std::shared_ptr<void>(savedPipelineState.get(), [](void*) {});
    assert(SetSource::validateOriginal(groupSource));
    assert(!rhi_metal::metalRhiOriginalSynchronousPayloadFor(groupSource));
    native->pipelineState = savedPipelineState;
    assert(!SetSource::validateOriginal(groupSource));
    for (const auto& quota : publication.quotas) {
        quota->setLimitBytes(metadata - 1u);
        assert(SetSource::validateOriginal(groupSource));
        assert(!rhi_metal::metalRhiOriginalSynchronousPayloadFor(groupSource));
        quota->setLimitBytes(metadata);
        assert(!SetSource::validateOriginal(groupSource));
    }
    assert(!d.detachOriginalDeviceNativeLoss());
    assert(SetSource::validateOriginal(groupSource));
    assert(!rhi_metal::metalRhiOriginalSynchronousPayloadFor(groupSource));
    assert(beforeFrozen == core::defaultCpuAllocator().stats().allocationCount);
    std::weak_ptr<const Sources> weakInventory = inventory;
    inventory.reset();
    assert(weakInventory.expired());
    nativeGroup.reset();
    groupSource.reset();
    std::printf(
        "METAL_ORDINARY_GROUP_PUBLICATION_PASS module=%s bindings=%zu buffers=%zu textures=%zu nativeAllocation=%llu exactPhysicalBound=true independentQuotas3=true finishAllocation0=true Source=%u immutableExport=true noInventoryCycle=true cloudFrames=0\n",
        name,
        groupElements.size(),
        elements.size(),
        textureElements.size(),
        static_cast<unsigned long long>(actualBytes),
        compute ? 7u : 5u);
}

template <class Pipeline> void ordinaryTextureFacts(const Pipeline& pipeline, const char* name)
{
    constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;
    const auto input = rhi_metal::MetalOriginalPipeline::input(pipeline);
    const auto native = rhi_metal::metalRhiNativePayloadFor(pipeline);
    assert(input && native);
    const auto& facts = compute ? native->computeTextureFacts : native->graphicsTextureFacts;
    assert(
        (compute ? native->computeTextureFactsVersion : native->graphicsTextureFactsVersion) == 1u);
    const auto entries = [&] {
        if constexpr (compute)
            return std::array{
                input->artifact(), std::shared_ptr<const shader::SingleEntryShaderArtifact>{}};
        else
            return std::array{input->artifact()->vertex(), input->artifact()->fragment()};
    }();
    const auto stages = compute ? std::array{rhi::ShaderStage::Compute, rhi::ShaderStage::None}
                                : std::array{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
    static_assert(
        MTLTextureType2D == 2u && MTLDataTypeFloat == 3u && MTLArgumentAccessReadOnly == 0u);
    std::size_t logicalCount = 0u, semanticFacts = 0u, depthBindings = 0u;
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    for (const auto& binding : input->package().reflection.descriptorBindings) {
        if (binding.type != rhi::DescriptorType::SampledTexture)
            continue;
        assert(binding.arrayCount == 1u);
        ++logicalCount;
        const auto* mapping =
            rhi_metal::MetalOriginalBufferLimitInspection::mapping(binding, *native);
        assert(mapping);
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            if ((binding.stages & stages[i]) == rhi::ShaderStage::None)
                continue;
            assert(!shader::SingleEntryShaderArtifact::validateOriginal(entries[i]));
            const auto& bytes = entries[i]->bytes(shader::ShaderArtifactRole::Spirv);
            shader::SpirvImageDeclaration declaration;
            assert(!shader::SpirvImageDeclarationVerifier::inspect(
                bytes.data(), bytes.size(), binding.set, binding.binding, declaration));
            assert(declaration.descriptorCount == 1u && declaration.dimension == 1u
                && !declaration.arrayed && !declaration.multisampled && declaration.sampled == 1u
                && declaration.floatScalar && declaration.scalarWidth == 32u);
            const rhi_metal::MetalRhiTextureBindingFact* selected = nullptr;
            for (const auto& fact : facts)
                if (fact.stage == stages[i] && fact.slot == mapping->metalSlot
                    && fact.argumentIndex == UINT32_MAX) {
                    assert(!selected);
                    selected = &fact;
                }
            if (selected)
                std::fprintf(stderr,
                    "METAL_TEXTURE_FACT module=%s binding=%u stage=%u type=%u scalar=%u depth=%d logicalDepth=%d access=%u active=%u\n",
                    name,
                    binding.binding,
                    static_cast<unsigned>(stages[i]),
                    selected->textureType,
                    selected->scalarDataType,
                    selected->depth,
                    binding.depthTexture,
                    selected->access,
                    selected->active);
            assert(selected && selected->textureType == MTLTextureType2D
                && selected->scalarDataType == MTLDataTypeFloat
                && selected->access == MTLArgumentAccessReadOnly && selected->active <= 1u);
            const auto* abi = rhi_metal::MetalRhiOriginalSampledTexture2DFloatAbi::inspect(
                *selected, binding.depthTexture);
            assert(!abi && selected->depth == binding.depthTexture);
            if (binding.depthTexture) {
                assert(declaration.depth == 1u);
                ++depthBindings;
            }
            const auto* wrongDepth = rhi_metal::MetalRhiOriginalSampledTexture2DFloatAbi::inspect(
                *selected, !binding.depthTexture);
            assert(wrongDepth
                && !std::strcmp(wrongDepth, "RHI-METAL-ORIGINAL-SAMPLED-TEXTURE-DEPTH-ABI"));
            for (auto invalid : {0u, 1u, 2u, 3u, 4u}) {
                auto changed = *selected;
                if (invalid == 0u)
                    changed.argumentIndex = 0u;
                else if (invalid == 1u)
                    changed.textureType = UINT32_MAX;
                else if (invalid == 2u)
                    changed.scalarDataType = UINT32_MAX;
                else if (invalid == 3u)
                    changed.access = MTLArgumentAccessWriteOnly;
                else
                    changed.active = UINT32_MAX;
                assert(rhi_metal::MetalRhiOriginalSampledTexture2DFloatAbi::inspect(
                    changed, selected->depth));
            }
            ++semanticFacts;
        }
    }
    for (const auto& fact : facts) {
        const rhi::ShaderReflectionBinding* selected = nullptr;
        for (const auto& binding : input->package().reflection.descriptorBindings) {
            if (binding.type != rhi::DescriptorType::SampledTexture)
                continue;
            const auto* mapping =
                rhi_metal::MetalOriginalBufferLimitInspection::mapping(binding, *native);
            if (mapping && mapping->metalSlot == fact.slot) {
                assert(!selected);
                selected = &binding;
            }
        }
        assert(selected && fact.argumentIndex == UINT32_MAX && fact.active <= 1u
            && (fact.stage == stages[0] || fact.stage == stages[1]));
        if ((selected->stages & fact.stage) == rhi::ShaderStage::None)
            assert(fact.active == 0u);
        auto changed = fact;
        changed.access = UINT32_MAX;
        assert(!fact.matches(changed));
        changed = fact;
        changed.active = UINT32_MAX;
        assert(!fact.matches(changed));
    }
    assert(before == core::defaultCpuAllocator().stats().allocationCount);
    std::printf(
        "METAL_ORDINARY_TEXTURE_FACTS_PASS module=%s logical=%zu semantic=%zu formal=%zu depthAbiMatches=%zu originalBinary=true nativeAccessActivity=true allocation0=true viewIdentity=false cloudFrames=0\n",
        name,
        logicalCount,
        semanticFacts,
        facts.size(),
        depthBindings);
}

void ordinaryNativeMemory(const std::filesystem::path& directory, bool bindingInspection = false)
{
    using Plan = test::CloudNativeMemoryFixture;
    Plan::verifyLoad(directory);
    auto caps = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    caps.queriedFromNativeDevice = bool(rhi_metal::metalRhiNativeDeviceIdentity());
    for (unsigned index = 0u; index < 6u; ++index) {
        const bool graphics = index >= 3u, foreground = index == 5u;
        const std::array<const char*, 6u> names{"cloud_raymarch",
            "cloud_temporal",
            "cloud_spatial_resolve",
            "cloud_composite",
            "cloud_spatial_composite",
            "cloud_foreground"};
        const auto path = directory
            / (graphics
                    ? foreground ? "shaders/cloud-foreground" : "shaders/original-cloud-graphics"
                    : "shaders/original-cloud-compute")
            / (std::string(names[index])
                + (graphics ? ".graphics.module.yaml" : ".computeMain.entry.yaml"));
        for (unsigned negative = 0u; negative < (bindingInspection || foreground ? 1u : 2u);
            ++negative) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> q;
            for (auto& quota : q)
                quota = core::makeShared<core::CpuMemoryQuota>(metadata);
            {
                core::pmr::String diagnostic;
                auto d = device(true, bindingInspection, 7u);
                const char* id = nullptr;
                if (bindingInspection) {
                    rhi::OriginalResourceRegistrationBudget registration;
                    registration.contractVersion = 10u;
                    registration.prototypeOptIn = true;
                    registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
                    registration.maxBuffers = 8u;
                    registration.maxTextures = registration.maxTextureViews = 2u;
                    registration.maxPipelineLayouts = 1u;
                    registration.maxGraphicsPipelines = graphics ? 1u : 0u;
                    registration.maxComputePipelines = graphics ? 0u : 1u;
                    registration.maxRetainedOwners = 128u;
                    registration.maxCpuMetadataBytes = metadata;
                    const auto loss = d->observeOriginalDeviceNativeLoss(id);
                    assert(loss && !id
                        && !d->prepareOriginalTypedNativeLossRegistration(
                            {3u, true}, registration, q[6], *loss));
                }
                const auto verify = [&](const auto& pipeline) {
                    if (negative) {
                        assert(!pipeline && id);
                        return;
                    }
                    if (!pipeline)
                        std::fprintf(stderr,
                            "ordinary native ABI %s: %s\n",
                            names[index],
                            id ? id : "missing");
                    assert(pipeline && !id);
                    ordinaryTextureFacts(*pipeline, names[index]);
                    const auto native = rhi_metal::metalRhiNativePayloadFor(*pipeline);
                    assert(native && native->nativeRuntimeBacked
                        && !native->nativeRuntimeDiagnosticOnly);
                    const auto& memory = graphics ? native->graphicsBufferMemberMemory[1]
                                                  : native->computeBufferMemberMemory;
                    const std::array<std::size_t, 6u> expected{3u, 4u, 6u, 2u, 2u, 1u};
                    assert(memory.version == 1u && memory.entries.size() == expected[index]);
                    for (const auto& entry : memory.entries) {
                        assert(entry.targetType != Plan::Graph::absent);
                        if (entry.relation.hasRuntimeArray)
                            assert(entry.relation.sourceRuntimeStrideBytes
                                    == entry.relation.targetRuntimeStrideBytes
                                && entry.relation.targetRuntimeStrideBytes
                                    == entry.element.strideBytes);
                        else
                            assert(entry.relation.occupiedBytes);
                    }
                    std::printf(
                        "METAL_ORDINARY_NATIVE_MEMORY_PASS module=%s buffers=%zu originalArtifact=true perMember=true noBindless=true\n",
                        names[index],
                        memory.entries.size());
                };
                if (graphics) {
                    const auto artifact = Plan::loadGraphics(path,
                        foreground ? Plan::Profile::Foreground : Plan::Profile::Cloud,
                        q[0],
                        diagnostic,
                        negative);
                    assert(artifact);
                    const auto input = rhi::OriginalGraphicsPipelineInput::prepare(
                        artifact, {1u, metadata}, q[1], diagnostic);
                    assert(input);
                    auto definition = rhi::buildPipelineLayoutDescFromShaderReflection(
                        input->package().reflection);
                    if (bindingInspection)
                        for (auto& set : definition.descriptorLayouts)
                            for (auto& binding : set.bindings)
                                if (binding.type == rhi::DescriptorType::SampledTexture) {
                                    binding.textureViewDimensionConstrained = true;
                                    binding.textureViewDimension =
                                        rhi::TextureViewDimension::Texture2D;
                                }
                    const auto layout = core::shareUnique(bindingInspection
                            ? d->createOriginalPipelineLayout(definition)
                            : d->createPipelineLayout(definition));
                    assert(layout);
                    rhi::RenderPipelineDesc desc;
                    desc.layout = layout.get();
                    desc.shaderPackage = &input->package();
                    desc.vertexShader = input->package().entrypoints[0];
                    desc.fragmentShader = input->package().entrypoints[1];
                    desc.colorFormat = rhi::TextureFormat::Rgba16Float;
                    desc.colorTargets.push_back({desc.colorFormat});
                    desc.hasViewport = desc.hasScissor = true;
                    desc.viewport = {0.f, 0.f, 32.f, 24.f, 0.f, 1.f};
                    desc.scissor = {0, 0, 32u, 24u};
                    auto pipeline = [&] {
                        if (bindingInspection) {
                            const auto receipt = d->createOriginalSynchronousGraphicsCreation(input,
                                layout,
                                desc,
                                {1u, true, metadata, metadata, metadata, metadata},
                                q[2],
                                q[3],
                                id);
                            if (!receipt)
                                std::fprintf(
                                    stderr, "binding graphics creation: %s\n", id ? id : "missing");
                            assert(receipt && !id);
                            return d->createOriginalArtifactGraphicsPipeline(
                                receipt, {2u, true, metadata}, q[4]);
                        }
                        return rhi_metal::MetalOriginalPipeline::create(desc,
                            input,
                            caps,
                            {1u, true, metadata, metadata, 5000000000u},
                            q[2],
                            id);
                    }();
                    if (negative) {
                        const auto report =
                            rhi_metal::MetalOriginalPipeline::validate(desc, input, caps);
                        assert(report.success && desc.vertexBuffers.empty());
                        rhi::semantics::MetalVertexStreamMapping streams;
                        streams.success = true;
                        core::CpuMemoryQuotaScope scope(q[2]);
                        const auto failed = rhi_metal::createMetalRhiNativeRenderPipelinePayload(
                            desc, report, streams, artifact);
                        assert(failed && !failed->nativeRuntimeBacked
                            && failed->diagnostics == "SHADER-NATIVE-MEMORY-TARGET-PATH-MATRIX");
                    }
                    verify(pipeline);
                    if (bindingInspection)
                        ordinaryBufferBindings(
                            *d, std::move(pipeline), q[5], q[7], q[8], names[index]);
                } else {
                    const auto artifact =
                        Plan::loadSingle(path, Plan::Profile::Cloud, q[0], diagnostic, negative);
                    assert(artifact);
                    const auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
                        artifact, {1u, metadata}, q[1], diagnostic);
                    assert(input);
                    auto definition = rhi::buildPipelineLayoutDescFromShaderReflection(
                        input->package().reflection);
                    if (bindingInspection)
                        for (auto& set : definition.descriptorLayouts)
                            for (auto& binding : set.bindings)
                                if (binding.type == rhi::DescriptorType::SampledTexture) {
                                    binding.textureViewDimensionConstrained = true;
                                    binding.textureViewDimension =
                                        rhi::TextureViewDimension::Texture2D;
                                }
                    const auto layout = core::shareUnique(bindingInspection
                            ? d->createOriginalPipelineLayout(definition)
                            : d->createPipelineLayout(definition));
                    assert(layout);
                    rhi::ComputePipelineDesc desc;
                    desc.layout = layout.get();
                    desc.shaderPackage = &input->package();
                    desc.computeShader = input->package().entrypoints.front();
                    auto pipeline = [&] {
                        if (bindingInspection) {
                            const auto receipt = d->createOriginalSynchronousComputeCreation(input,
                                layout,
                                desc,
                                {1u, true, metadata, metadata, metadata, metadata},
                                q[2],
                                q[3],
                                id);
                            if (!receipt)
                                std::fprintf(
                                    stderr, "binding compute creation: %s\n", id ? id : "missing");
                            assert(receipt && !id);
                            return d->createOriginalArtifactComputePipeline(
                                receipt, {2u, true, metadata}, q[4]);
                        }
                        return rhi_metal::MetalOriginalPipeline::create(desc,
                            input,
                            caps,
                            {1u, true, metadata, metadata, 5000000000u},
                            q[2],
                            id);
                    }();
                    if (!pipeline) {
                        const auto report =
                            rhi_metal::MetalOriginalPipeline::validate(desc, input, caps);
                        assert(report.success);
                        core::CpuMemoryQuotaScope scope(q[2]);
                        const auto failed = rhi_metal::createMetalRhiNativeComputePipelinePayload(
                            desc, report, artifact);
                        if (negative)
                            assert(failed && !failed->nativeRuntimeBacked
                                && failed->diagnostics
                                    == "SHADER-NATIVE-MEMORY-TARGET-PATH-MATRIX");
                        else
                            std::fprintf(stderr,
                                "ordinary native compute detail: %s\n",
                                failed ? failed->diagnostics.c_str() : "missing");
                    }
                    verify(pipeline);
                    if (bindingInspection)
                        ordinaryBufferBindings(
                            *d, std::move(pipeline), q[5], q[7], q[8], names[index]);
                }
            }
            for (const auto& quota : q)
                assert(!quota->liveBytes());
            if (negative)
                std::printf(
                    "METAL_ORDINARY_NATIVE_MEMORY_REJECT module=%s wrongTargetPath=true quotasDrained=true\n",
                    names[index]);
        }
    }
}

struct OrdinaryQueueSequence {
    rhi::GpuDevice* device = nullptr;
    bool registered = false;
    std::shared_ptr<const rhi_metal::MetalOriginalQueueAcceptance> previous;
};

struct OrdinaryCloudSequence {
    rhi::GpuDevice* device = nullptr;
    std::shared_ptr<core::CpuMemoryQuota> sourceQuota =
        core::makeShared<core::CpuMemoryQuota>(metadata);
    std::shared_ptr<const rhi::OriginalCommandBufferLease> previous;
    renderer::cloud::CloudFrameState frame;
    tests::cloud::SequenceOracle oracle;
    std::array<renderer::cloud::CloudGpuSample, 17u> previousBytes{};
    unsigned submitted = 0u;
    bool registered = false;
    unsigned slotCount = 0u;
    rhi::Extent2D extent{4u, 4u};
    render_graph::PassSubmissionToken retiredToken{};
    std::array<renderer::cloud::CloudPreparedHistorySlot, 3u> slots;
    std::array<std::array<rhi::GpuBuffer*, 2u>, 3u> slotObjects{};
    std::shared_ptr<core::CpuMemoryQuota> historyQuota =
        core::makeShared<core::CpuMemoryQuota>(metadata);
    renderer::cloud::CloudPreparedHistory history;
    std::shared_ptr<rhi::GpuFence> firstWriterFence;
    bool historyCreated = false;

    rhi::GpuBuffer* slotObject(
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& lease) const
    {
        for (unsigned i = 0u; i < slotCount; ++i)
            for (auto* object : slotObjects[i])
                if (object == lease->originalObject())
                    return object;
        assert(false);
        return nullptr;
    }
};

void ordinaryCloudContext(renderer::cloud::CloudGraphContext& context,
    const scene3d::CloudLayerDesc& layer,
    unsigned frame,
    rhi::Extent2D extent = {4u, 4u})
{
    context.layer = layer;
    context.cameraPosition = {39.f, 10.f, -57.f};
    context.view =
        core::lookAt(context.cameraPosition, context.cameraPosition + core::Vec3{.25f, .6f, 1.f});
    context.projection =
        core::perspective(57.f, float(extent.width) / float(extent.height), 1.f, 20000.f);
    rhi::semantics::applyProjectionYConvention(context.projection.m.data(), rhi::Backend::Metal);
    context.sunDirection = core::normalize(core::Vec3{1.f, 1.f, -.2f});
    context.sunRadiance = {2.f, 1.8f, 1.6f};
    context.ambientRadiance = {.1f, .2f, .3f};
    context.time = frame / 30.f;
}

// Real original Program exercised through the existing wrappers. This deliberately
// keeps the public Preparation6 gate closed; sampled mode uses the actual cloud composite.
void ordinaryOwnerClearCopy(const std::filesystem::path& directory,
    unsigned mode,
    bool compute = false,
    bool sampled = false,
    bool cloud = false,
    OrdinaryCloudSequence* sequence = nullptr,
    OrdinaryQueueSequence* queueSequence = nullptr,
    const tests::cloud::ReferenceCase* visualCase = nullptr,
    const char* visualOutput = nullptr)
{
    using namespace rhi;
    using K = CommandUsageKind;
    const auto require = [](const char* id) {
        if (id)
            std::fprintf(stderr, "ordinary owner: %s\n", id);
        assert(!id);
    };
    const auto requireResult = [](const auto& result) {
        if (!result.success)
            std::fprintf(stderr, "ordinary recording: %s\n", result.diagnostics.c_str());
        assert(result.success);
    };
    const auto requireCloud = [](const renderer::cloud::CloudResult& result) {
        if (!result.success)
            std::fprintf(stderr, "ordinary cloud: %s\n", result.diagnostic);
        assert(result.success);
    };
    assert(!compute || !sampled);
    assert(!cloud || sampled);
    assert(!sequence || (cloud && sequence->device));
    assert(!queueSequence || (!sequence && queueSequence->device));
    auto ownedDevice = sequence || queueSequence ? decltype(device()){}
                                                 : device(true,
                                                       true,
                                                       cloud         ? 13u
                                                           : sampled ? 5u
                                                           : compute ? 3u
                                                                     : 1u,
                                                       cloud                    ? 8u
                                                           : compute || sampled ? 4u
                                                                                : 2u);
    auto* d = sequence  ? sequence->device
        : queueSequence ? queueSequence->device
                        : ownedDevice.get();
    const bool controlled = sequence && sequence->slotCount;
    if (visualOutput) {
        FrameCaptureRequest capture;
        capture.outputPath = core::pmr::String(visualOutput) + ".gputrace";
        const auto result = d->beginFrameCapture(capture);
        if (!result.success())
            std::fprintf(stderr, "capture: %s\n", result.diagnostics.c_str());
        assert(result.success());
    }
    const auto extent = visualCase ? Extent2D{32u, 24u}
        : sequence                 ? sequence->extent
                                   : Extent2D{4u, 4u};
    assert(extent.width && extent.width <= 32u && extent.height && extent.height <= 24u);
    const auto sampleCount = extent.width * extent.height;
    const auto sampleStorageBytes = (sampleCount + 1u) * sizeof(renderer::cloud::CloudGpuSample);
    bool reuseCloudSlot = false;
    const auto bindingCount = cloud ? 4u : compute || sampled ? 2u : 1u;
    const bool omitDepthTransition = sampled && mode == 21u;
    const bool omitCloudBarrier = cloud && (mode == 24u || mode == 25u);
    std::uint32_t commandCount = cloud ? (36u - unsigned(omitCloudBarrier))
        : sampled                      ? (omitDepthTransition ? 23u : 24u)
        : compute                      ? 18u
                                       : 13u;
    std::uint32_t referenceCount = cloud ? (21u - unsigned(omitCloudBarrier))
        : sampled                        ? (omitDepthTransition ? 14u : 15u)
        : compute                        ? 9u
                                         : 7u;
    std::uint32_t descriptorCount = cloud ? (19u - unsigned(omitCloudBarrier))
        : sampled                         ? (omitDepthTransition ? 12u : 13u)
        : compute                         ? 8u
                                          : 6u;
    const std::uint32_t implicitAccessCount = cloud ? 26u : sampled ? 10u : compute ? 6u : 2u;
    const auto resourceQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    core::CpuMemoryQuotaScope resourceScope(resourceQuota);
    const auto sourceQuota =
        sequence ? sequence->sourceQuota : core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto contentQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto stateQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const char* id = nullptr;
    OriginalResourceRegistrationBudget registration;
    registration.contractVersion = 10u;
    registration.prototypeOptIn = true;
    // Four current non-slot buffers, two buffers per slot and, only for
    // three-slot speculation, one additional immutable constants generation.
    registration.maxBuffers = controlled
        ? 4u + 2u * sequence->slotCount + unsigned(sequence->slotCount == 3u)
        : cloud              ? 7u
        : compute || sampled ? 4u
                             : 2u;
    registration.maxTextures = sampled ? 3u : 1u;
    registration.maxTextureViews = sampled ? 5u : 1u;
    registration.maxDescriptorArenas = registration.maxDescriptorSets = bindingCount;
    registration.maxPipelineLayouts = bindingCount;
    registration.maxGraphicsPipelines = sampled ? 2u : 1u;
    registration.maxComputePipelines = cloud ? 2u : compute ? 1u : 0u;
    registration.maxRetainedOwners = 256u;
    registration.maxCpuMetadataBytes = metadata;
    const auto registrationQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    const auto loss = d->observeOriginalDeviceNativeLoss(id);
    require(id);
    assert(loss);
    if ((!sequence || !sequence->registered) && (!queueSequence || !queueSequence->registered)) {
        require(d->prepareOriginalTypedNativeLossRegistration(
            {3u, true}, registration, registrationQuota, *loss));
        if (sequence)
            sequence->registered = true;
        if (queueSequence)
            queueSequence->registered = true;
    }
    if (mode != 35u && !rhi_metal::metalRhiOriginalSubmissionDomain(*d)) {
        // Explicit independent fixture capacity: one current command plus one
        // cold command; no native predecessor/read admission is requested.
        const auto completionQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
        const semantics::OriginalSubmissionCompletionBudget completionBudget{
            1u, true, 2u, 1u, metadata};
        std::thread foreignPreparation([&] {
            const auto* refused = rhi_metal::prepareMetalOriginalSubmissionCompletion(
                *d, completionBudget, completionQuota);
            assert(refused && !std::strcmp(refused, "RHI-SUBMISSION-COMPLETION-THREAD"));
        });
        foreignPreparation.join();
        assert(!rhi_metal::metalRhiOriginalSubmissionDomain(*d));
        auto invalidBudget = completionBudget;
        invalidBudget.maxSubmissions = 0u;
        const auto* refused =
            rhi_metal::prepareMetalOriginalSubmissionCompletion(*d, invalidBudget, completionQuota);
        assert(refused && !std::strcmp(refused, "RHI-SUBMISSION-COMPLETION-CAPACITY"));
        assert(!completionQuota->liveBytes() && !rhi_metal::metalRhiOriginalSubmissionDomain(*d));
        invalidBudget = completionBudget;
        invalidBudget.maxCpuMetadataBytes = 1u;
        const auto exhaustedQuota = core::makeShared<core::CpuMemoryQuota>(1u);
        refused =
            rhi_metal::prepareMetalOriginalSubmissionCompletion(*d, invalidBudget, exhaustedQuota);
        assert(refused && !std::strcmp(refused, "RHI-SUBMISSION-COMPLETION-ALLOCATION"));
        assert(!exhaustedQuota->liveBytes() && !rhi_metal::metalRhiOriginalSubmissionDomain(*d));
        require(rhi_metal::prepareMetalOriginalSubmissionCompletion(
            *d, completionBudget, completionQuota));
        const auto canonical = rhi_metal::metalRhiOriginalSubmissionDomain(*d);
        assert(canonical);
        refused = rhi_metal::prepareMetalOriginalSubmissionCompletion(
            *d, completionBudget, completionQuota);
        assert(refused && !std::strcmp(refused, "RHI-SUBMISSION-COMPLETION-ALREADY-PREPARED"));
        assert(core::SharedOwnerProjection::sameOwner(
            canonical, rhi_metal::metalRhiOriginalSubmissionDomain(*d)));
    }
    TextureDesc td;
    td.extent = {extent.width, extent.height, 1u};
    td.format = TextureFormat::Rgba16Float;
    td.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
    if (sampled)
        td.usage = td.usage | TextureUsage::Sampled;
    const auto nativeBytes = d->queryOriginalContentTextureAllocation(td, id, 4u);
    require(id);
    auto texture = d->createOriginalContentTexture(td,
        {4u,
            true,
            1u,
            1u,
            std::uint64_t(extent.width) * extent.height * 8u,
            2u,
            metadata,
            1u,
            nativeBytes},
        contentQuota,
        id);
    require(id);
    assert(texture);
    auto* textureObject = texture.get();
    auto parent = OriginalCommandTextureLease::prepareOriginal(
        texture, {1u, true, 2u, metadata}, sourceQuota, id);
    require(id);
    assert(parent);
    TextureViewDesc vd;
    vd.format = td.format;
    vd.usage = TextureUsage::RenderAttachment;
    auto view = d->createTextureView(*parent->originalObject(), vd);
    assert(view);
    auto viewLease = OriginalCommandTextureViewLease::prepareOriginal(
        view, {1u, true, 2u, metadata}, sourceQuota, id);
    require(id);
    auto viewSource = OriginalCommandTextureViewSource::prepareOriginal(viewLease,
        parent,
        {1u, true, OriginalCommandTextureViewSource::completeRetainedOwnerCount, metadata},
        sourceQuota,
        id);
    require(id);
    BufferDesc bd;
    bd.sizeBytes = visualCase ? 6144u : 1024u;
    bd.usage = ResourceUsage::CopyDst;
    bd.memory = MemoryUsage::GpuToCpu;
    bd.initialState = ResourceState::CopyDst;
    auto buffer = d->createBuffer(bd);
    assert(buffer);
    core::pmr::Vector<unsigned char> initial(bd.sizeBytes);
    std::fill(initial.begin(), initial.end(), 0x5a);
    assert(d->writeBuffer(*buffer, initial.data(), initial.size()).success);
    buffer->sealHostWrites();
    auto bufferLease = OriginalCommandBufferLease::prepareOriginal(
        buffer, {1u, true, 2u, metadata}, sourceQuota, id);
    require(id);
    using Plan = test::CloudNativeMemoryFixture;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> pipelineQuotas;
    for (auto& quota : pipelineQuotas)
        quota = core::makeShared<core::CpuMemoryQuota>(metadata);
    core::pmr::String shaderDiagnostic;
    const auto artifact = Plan::loadGraphics(
        directory / "shaders/cloud-foreground/cloud_foreground.graphics.module.yaml",
        Plan::Profile::Foreground,
        pipelineQuotas[0],
        shaderDiagnostic);
    assert(artifact);
    const auto input = OriginalGraphicsPipelineInput::prepare(
        artifact, {1u, metadata}, pipelineQuotas[1], shaderDiagnostic);
    assert(input);
    const auto layout = core::shareUnique(d->createOriginalPipelineLayout(
        buildPipelineLayoutDescFromShaderReflection(input->package().reflection)));
    assert(layout);
    RenderPipelineDesc pipelineDesc;
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &input->package();
    pipelineDesc.vertexShader = input->package().entrypoints[0];
    pipelineDesc.fragmentShader = input->package().entrypoints[1];
    pipelineDesc.colorFormat = TextureFormat::Rgba16Float;
    pipelineDesc.colorTargets.push_back({pipelineDesc.colorFormat});
    pipelineDesc.hasViewport = pipelineDesc.hasScissor = false;
    pipelineDesc.dynamicViewportScissor = true;
    pipelineDesc.viewport = {0.f, 0.f, float(extent.width), float(extent.height), 0.f, 1.f};
    const auto compositeViewport = pipelineDesc.viewport;
    pipelineDesc.scissor = {0, 0, extent.width, extent.height};
    if (sampled) {
        pipelineDesc.viewport = {
            .25f, .25f, float(extent.width) - .5f, float(extent.height) - .5f, 0.f, 1.f};
        pipelineDesc.depthFormat = TextureFormat::D32Float;
        pipelineDesc.depthStencil.depthTestEnabled = true;
        pipelineDesc.depthStencil.depthWriteEnabled = true;
        pipelineDesc.depthStencil.depthCompare = CompareOp::Less;
    }
    if (visualCase && !visualCase->foreground)
        pipelineDesc.viewport.x = float(extent.width) + 1.f;
    const auto receipt = d->createOriginalSynchronousGraphicsCreation(input,
        layout,
        pipelineDesc,
        {1u, true, metadata, metadata, metadata, metadata},
        pipelineQuotas[2],
        pipelineQuotas[3],
        id);
    require(id);
    assert(receipt);
    auto uniquePipeline =
        d->createOriginalArtifactGraphicsPipeline(receipt, {2u, true, metadata}, pipelineQuotas[4]);
    assert(uniquePipeline);
    auto pipeline =
        OriginalRegisteredPipeline::shareAcceptedOriginal(std::move(uniquePipeline), id);
    require(id);
    using PipelineSource =
        rhi_metal::MetalOriginalBufferBindingInspection::Contract::PipelineSource<false>;
    auto pipelineSource = PipelineSource::prepareOriginal(pipeline,
        {2u,
            true,
            PipelineSource::kind,
            pipeline->originalResourceRegistration().generation(),
            metadata,
            PipelineSource::exclusiveOwnerCount},
        sourceQuota,
        *d,
        id);
    require(id);
    BufferDesc uniformDesc;
    uniformDesc.sizeBytes = 16u;
    uniformDesc.usage = ResourceUsage::UniformBuffer;
    uniformDesc.memory = MemoryUsage::CpuToGpu;
    uniformDesc.initialState = ResourceState::ShaderRead;
    auto uniform = d->createBuffer(uniformDesc);
    assert(uniform);
    const std::array<float, 4u> uniformBytes{.125f, .375f, .75f, .625f};
    if (mode != 8u)
        assert(d->writeBuffer(*uniform,
                    uniformBytes.data(),
                    mode == 9u ? sizeof(float) * 2u : sizeof(uniformBytes))
                .success);
    uniform->sealWrites();
    auto uniformLease = OriginalCommandBufferLease::prepareOriginal(
        uniform, {1u, true, 2u, metadata}, sourceQuota, id);
    require(id);
    using Sources = OriginalCommandResourceSources;
    core::pmr::Vector<Sources::Source> sources{parent, viewSource, bufferLease, uniformLease};
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> computeQuotas;
    std::shared_ptr<ComputePipeline> computePipeline;
    std::shared_ptr<const OriginalCommandComputePipelineSource> computeSource;
    std::array<std::shared_ptr<const OriginalCommandBufferLease>, 2u> computeBuffers;
    core::UniquePtr<SynchronousLossGroupProbe<ComputePipeline>> computePublication;
    const auto domainQuotas = compute || cloud ? quotas() : Session::Quotas{};
    const auto domainQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
    std::shared_ptr<const ComputeDispatchValidationDomain> domain;
    if (compute) {
        for (auto& quota : computeQuotas)
            quota = core::makeShared<core::CpuMemoryQuota>(metadata);
        const auto computeArtifact = Plan::loadSingle(directory
                / "shaders/compute-dispatch-probe/compute_dispatch_grid_probe.computeMain.entry.yaml",
            Plan::Profile::Probe,
            computeQuotas[0],
            shaderDiagnostic);
        assert(computeArtifact);
        const auto computeInput = OriginalSingleEntryPipelineInput::prepare(
            computeArtifact, {1u, metadata}, computeQuotas[1], shaderDiagnostic);
        assert(computeInput);
        const auto computeLayout = core::shareUnique(d->createOriginalPipelineLayout(
            buildPipelineLayoutDescFromShaderReflection(computeInput->package().reflection)));
        assert(computeLayout);
        ComputePipelineDesc computeDesc;
        computeDesc.layout = computeLayout.get();
        computeDesc.shaderPackage = &computeInput->package();
        computeDesc.computeShader = computeInput->package().entrypoints.front();
        const auto computeReceipt = d->createOriginalSynchronousComputeCreation(computeInput,
            computeLayout,
            computeDesc,
            {1u, true, metadata, metadata, metadata, metadata},
            computeQuotas[2],
            computeQuotas[3],
            id);
        require(id);
        assert(computeReceipt);
        auto uniqueCompute = d->createOriginalArtifactComputePipeline(
            computeReceipt, {2u, true, metadata}, computeQuotas[4]);
        assert(uniqueCompute);
        computePipeline =
            OriginalRegisteredPipeline::shareAcceptedOriginal(std::move(uniqueCompute), id);
        require(id);
        using ComputeSource = OriginalCommandComputePipelineSource;
        computeSource = ComputeSource::prepareOriginal(computePipeline,
            {2u,
                true,
                ComputeSource::kind,
                computePipeline->originalResourceRegistration().generation(),
                metadata,
                ComputeSource::exclusiveOwnerCount},
            sourceQuota,
            *d,
            id);
        require(id);
        auto proof =
            d->prepareOriginalComputeDispatchValidation(request(), budget, domainQuotas, id);
        require(id);
        assert(proof);
        until(
            [&] {
                require(proof->advance());
                return proof->observe().verified;
            },
            "ordinary owner domain");
        domain = d->issueOriginalComputeDispatchDomain(proof, domainBudget, domainQuota, id);
        require(id);
        assert(domain);
        const std::array<std::uint32_t, 4u> constants{3u, 2u, 3u, 0u};
        std::array<std::uint32_t, 32u> output;
        output.fill(0xdeadbeefu);
        for (unsigned i = 0u; i < computeBuffers.size(); ++i) {
            BufferDesc desc;
            desc.sizeBytes = i ? sizeof(output) : sizeof(constants);
            desc.usage = i ? ResourceUsage::StorageBuffer : ResourceUsage::UniformBuffer;
            desc.memory = MemoryUsage::CpuToGpu;
            desc.initialState = i ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
            auto object = d->createBuffer(desc);
            assert(object);
            requireResult(d->writeBuffer(*object,
                i ? static_cast<const void*>(output.data())
                  : static_cast<const void*>(constants.data()),
                desc.sizeBytes));
            object->sealHostWrites();
            computeBuffers[i] = OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, metadata}, sourceQuota, id);
            require(id);
            sources.push_back(computeBuffers[i]);
        }
        computePublication =
            core::makeUnique<SynchronousLossGroupProbe<ComputePipeline>>(*d, computePipeline);
    }
    GpuTexture* depthObject = nullptr;
    GpuTexture* outputObject = nullptr;
    renderer::cloud::CloudGpuConstants cloudParameters{};
    renderer::cloud::CloudFrameState cloudFrame;
    tests::cloud::SequenceOracle pendingOracle;
    renderer::cloud::CloudExecutionPlan cloudPlan;
    auto cloudAsset = cloud ? tests::cloud::asset() : assets::cloud::CloudAsset{};
    if (visualCase) {
        tests::cloud::SceneResourceFixture source(
            renderer::cloud::CloudSceneReferenceMode::ResourceId);
        cloudAsset = *source.binding->asset();
    }
    auto cloudLayer = tests::cloud::layer();
    if (visualCase) {
        cloudLayer.coverage = visualCase->coverage;
        cloudLayer.extinction = visualCase->extinction;
    }
    std::array<renderer::cloud::CloudGpuSample, 768u> cloudExpected{};
    std::array<renderer::cloud::CloudGpuSample, 768u> cloudResolvedExpected{};
    std::array<std::shared_ptr<const OriginalCommandBufferLease>, 3u> cloudBuffers;
    std::array<std::shared_ptr<ComputePipeline>, 2u> cloudPipelines;
    std::array<std::shared_ptr<const OriginalCommandComputePipelineSource>, 2u> cloudSources;
    std::array<core::UniquePtr<SynchronousLossGroupProbe<ComputePipeline>>, 2u> cloudPublications;
    std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u>, 2u> cloudQuotas;
    GpuBuffer* resolvedObject = nullptr;
    GpuBuffer* rawObject = nullptr;
    if (cloud) {
        using namespace renderer::cloud;
        auto config = tests::cloud::config();
        config.temporal = sequence != nullptr;
        if (visualCase) {
            config.viewSteps = tests::cloud::ReferenceCases::viewSteps;
            config.lightSteps = tests::cloud::ReferenceCases::lightSteps;
        }
        if (controlled)
            config.framesInFlight = sequence->slotCount;
        requireCloud(CloudExecutionPlanner::describeResources(config,
            extent,
            cloudAsset.cells.size() * sizeof(core::Vec4),
            sizeof(CloudGpuConstants),
            cloudPlan));
        render_graph::FrameGraph parameterGraph(*d);
        CloudGraphContext context{parameterGraph};
        ordinaryCloudContext(context, cloudLayer, sequence ? sequence->submitted : 0u, extent);
        if (visualCase) {
            context.cameraPosition = {39.f, visualCase->altitude, -57.f};
            context.view = core::lookAt(context.cameraPosition,
                context.cameraPosition + core::Vec3{.25f, visualCase->viewElevation, 1.f});
            context.sunDirection = core::normalize(visualCase->sun);
            context.orthographic = visualCase->orthographic;
            if (context.orthographic) {
                context.projection = core::orthographic(1600.f, 1200.f, 1.f, 20000.f);
                semantics::applyProjectionYConvention(context.projection.m.data(), Backend::Metal);
            }
        }
        if (controlled) {
            if (!sequence->historyCreated) {
                for (unsigned slot = 0u; slot < sequence->slotCount; ++slot)
                    for (unsigned buffer = 0u; buffer < 2u; ++buffer) {
                        BufferDesc desc;
                        desc.sizeBytes = sampleStorageBytes;
                        desc.usage = ResourceUsage::StorageBuffer;
                        desc.memory = MemoryUsage::CpuToGpu;
                        desc.initialState = ResourceState::ShaderWrite;
                        auto object = d->createBuffer(desc);
                        assert(object);
                        std::array<CloudGpuSample, 769u> initial;
                        std::memset(initial.data(), 0x5a, sizeof(initial));
                        requireResult(d->writeBuffer(*object, initial.data(), desc.sizeBytes));
                        object->sealHostWrites();
                        sequence->slotObjects[slot][buffer] = object.get();
                        auto lease = OriginalCommandBufferLease::prepareOriginal(
                            object, {1u, true, 2u, metadata}, sourceQuota, id);
                        require(id);
                        (buffer ? sequence->slots[slot].resolved : sequence->slots[slot].raw) =
                            lease;
                    }
                requireCloud(sequence->history.create(*d,
                    context.cameraId,
                    config,
                    cloudPlan,
                    cloudAsset.source,
                    sequence->slots.data(),
                    sequence->slotCount,
                    sequence->historyQuota,
                    nullptr,
                    {},
                    {},
                    sourceQuota));
                sequence->historyCreated = true;
            }
            requireCloud(sequence->history.prepareFrame(context));
            if (sequence->retiredToken.generation) {
                const auto* stale = sequence->history.validateSubmission(sequence->retiredToken);
                assert(stale && !std::strcmp(stale, "CLOUD-STALE-SUBMISSION"));
            }
            cloudFrame = sequence->history.pendingFrame();
            CloudFrameState expectedFrame;
            requireCloud(CloudFrameParameters::build(context,
                Backend::Metal,
                config,
                cloudPlan,
                cloudAsset.source,
                sequence->submitted,
                sequence->submitted ? &sequence->frame : nullptr,
                expectedFrame));
            assert(!std::memcmp(&cloudFrame.parameters,
                &expectedFrame.parameters,
                sizeof(expectedFrame.parameters)));
            reuseCloudSlot =
                sequence->history.raw()->originalObject()->state() == ResourceState::ShaderRead;
            assert((sequence->history.resolved()->originalObject()->state()
                       == ResourceState::ShaderRead)
                == reuseCloudSlot);
            if (reuseCloudSlot) {
                commandCount += 2u;
                referenceCount += 2u;
                descriptorCount += 2u;
            }
        } else
            requireCloud(CloudFrameParameters::build(context,
                Backend::Metal,
                config,
                cloudPlan,
                cloudAsset.source,
                sequence ? sequence->submitted : 0u,
                sequence && sequence->submitted ? &sequence->frame : nullptr,
                cloudFrame));
        cloudParameters = cloudFrame.parameters;
        assert(cloudFrame.historyUsed == bool(sequence && sequence->submitted));
        tests::cloud::SequenceOracle geometry;
        geometry.parameters = cloudParameters;
        CloudReference reference;
        requireCloud(reference.configure(cloudAsset, cloudLayer));
        const tests::cloud::ForegroundFixture triangle{true,
            pipelineDesc.viewport.x,
            pipelineDesc.viewport.y,
            pipelineDesc.viewport.width,
            pipelineDesc.viewport.height};
        for (unsigned y = 0u; y < extent.height; ++y)
            for (unsigned x = 0u; x < extent.width; ++x) {
                const float u = (x + .5f) / float(extent.width),
                            v = (y + .5f) / float(extent.height);
                auto ray = geometry.ray(u, v);
                const auto nativeY =
                    semantics::projectionRequiresYFlip(Backend::Metal) ? extent.height - 1u - y : y;
                const bool covered = triangle.covers(x, nativeY);
                const auto surface = core::transformPoint(
                    cloudParameters.inverseViewProjection, {2 * u - 1, 2 * v - 1, .5f});
                const float opaque = covered
                    ? std::max(0.f, core::dot(surface - ray.origin, ray.direction))
                    : cloudParameters.temporal.z;
                ray.maxDistance = std::min(ray.maxDistance, double(opaque));
                renderer::volume::MediumIntegral integral;
                float moment = cloudParameters.temporal.z;
                requireCloud(reference.integrate(ray,
                    config.viewSteps,
                    config.lightSteps,
                    context.sunDirection,
                    context.sunRadiance,
                    context.ambientRadiance,
                    context.time,
                    integral,
                    cloudParameters.jitter.z,
                    &moment));
                cloudExpected[y * extent.width + x] = {{integral.radiance.x,
                                                           integral.radiance.y,
                                                           integral.radiance.z,
                                                           integral.transmittance},
                    {integral.transmittance < 1.f ? moment : cloudParameters.temporal.z,
                        opaque,
                        0.f,
                        0.f}};
            }
        cloudResolvedExpected = cloudExpected;
        if (sequence) {
            pendingOracle = sequence->oracle;
            pendingOracle.plan = cloudPlan;
            pendingOracle.parameters = cloudParameters;
            pendingOracle.layer = cloudLayer;
            pendingOracle.foreground = triangle;
            pendingOracle.flipForegroundY = semantics::projectionRequiresYFlip(Backend::Metal);
            pendingOracle.build(cloudAsset);
            assert(pendingOracle.raw.size() == sampleCount);
            for (unsigned i = 0u; i < sampleCount; ++i) {
                assert(
                    !std::memcmp(&cloudExpected[i], &pendingOracle.raw[i], sizeof(CloudGpuSample)));
                cloudResolvedExpected[i] = pendingOracle.resolved[i];
            }
        }
        auto proof = d->prepareOriginalComputeDispatchValidation(
            request(cloudPlan.dispatchGroups), budget, domainQuotas, id);
        require(id);
        assert(proof);
        until(
            [&] {
                require(proof->advance());
                return proof->observe().verified;
            },
            "ordinary cloud domain");
        domain = d->issueOriginalComputeDispatchDomain(proof, domainBudget, domainQuota, id);
        require(id);
        assert(domain);
    }
    std::shared_ptr<const OriginalCommandTextureLease> depthParent, outputParent;
    std::shared_ptr<const OriginalCommandTextureViewSource> sceneSample, depthAttachment,
        depthSample, outputAttachment;
    std::array<std::shared_ptr<const OriginalCommandBufferLease>, 2u> compositeBuffers;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> compositeQuotas;
    std::shared_ptr<RenderPipeline> compositePipeline;
    std::shared_ptr<const PipelineSource> compositeSource;
    core::UniquePtr<SynchronousLossGroupProbe<RenderPipeline>> compositePublication;
    if (sampled) {
        const auto makeView = [&](const auto& original, TextureUsage usage) {
            TextureViewDesc desc;
            desc.format = original->originalObject()->desc().format;
            desc.usage = usage;
            desc.aspect = desc.format == TextureFormat::D32Float ? TextureAspect::Depth
                                                                 : TextureAspect::Color;
            auto object = d->createTextureView(*original->originalObject(), desc);
            assert(object);
            auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                object, {1u, true, 2u, metadata}, sourceQuota, id);
            require(id);
            auto result = OriginalCommandTextureViewSource::prepareOriginal(
                lease, original, {1u, true, 5u, metadata}, sourceQuota, id);
            require(id);
            sources.push_back(result);
            return result;
        };
        sceneSample = makeView(parent, TextureUsage::Sampled); // Source4
        const auto makeParent = [&](TextureFormat format) {
            auto desc = td;
            desc.format = format;
            desc.usage = TextureUsage::RenderAttachment
                | (format == TextureFormat::D32Float ? TextureUsage::Sampled
                                                     : TextureUsage::CopySrc);
            const auto allocation = d->queryOriginalContentTextureAllocation(desc, id, 4u);
            require(id);
            auto object = d->createOriginalContentTexture(desc,
                {4u,
                    true,
                    1u,
                    1u,
                    std::uint64_t(extent.width) * extent.height
                        * (format == TextureFormat::D32Float ? 4u : 8u),
                    2u,
                    metadata,
                    1u,
                    allocation},
                contentQuota,
                id);
            require(id);
            assert(object);
            (format == TextureFormat::D32Float ? depthObject : outputObject) = object.get();
            auto result = OriginalCommandTextureLease::prepareOriginal(
                object, {1u, true, 2u, metadata}, sourceQuota, id);
            require(id);
            sources.push_back(result);
            return result;
        };
        depthParent = makeParent(TextureFormat::D32Float);                         // Source5
        depthAttachment = makeView(depthParent, TextureUsage::RenderAttachment);   // Source6
        depthSample = makeView(depthParent, TextureUsage::Sampled);                // Source7
        outputParent = makeParent(TextureFormat::Rgba16Float);                     // Source8
        outputAttachment = makeView(outputParent, TextureUsage::RenderAttachment); // Source9
        renderer::cloud::CloudGpuConstants constants{};
        constants.inverseViewProjection = core::Mat4::identity();
        constants.cameraRadial = {0.f, 100.f, 0.f, 0.f};
        constants.shell = {1.f, 5.f, 100.f, .01f};
        constants.dimensions = {1u, 1u, 4u, 4u};
        constants.temporal = {0.f, 1.f, 1000.f, 0.f};
        if (cloud)
            constants = cloudParameters;
        const renderer::cloud::CloudGpuSample sample{
            {.25f, .5f, .75f, .5f}, {0.f, 1000.f, 0.f, 0.f}};
        for (unsigned i = 0u; i < 2u; ++i) {
            if (controlled) {
                compositeBuffers[i] =
                    i ? sequence->history.resolved() : sequence->history.constants();
                if (i)
                    resolvedObject = sequence->slotObject(compositeBuffers[i]);
                sources.push_back(compositeBuffers[i]);
                continue;
            }
            BufferDesc desc;
            desc.sizeBytes = i ? (cloud ? sampleStorageBytes : sizeof(sample)) : sizeof(constants);
            desc.usage = i ? ResourceUsage::StorageBuffer : ResourceUsage::UniformBuffer;
            desc.memory = MemoryUsage::CpuToGpu;
            desc.initialState = cloud && i ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
            auto object = d->createBuffer(desc);
            assert(object);
            std::array<renderer::cloud::CloudGpuSample, 769u> initial;
            std::memset(initial.data(), 0x5a, sizeof(initial));
            requireResult(d->writeBuffer(*object,
                i ? (cloud ? static_cast<const void*>(initial.data())
                           : static_cast<const void*>(&sample))
                  : static_cast<const void*>(&constants),
                desc.sizeBytes));
            if (cloud && i) {
                resolvedObject = object.get();
                object->sealHostWrites();
            } else
                object->sealWrites();
            compositeBuffers[i] = OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, metadata}, sourceQuota, id);
            require(id);
            sources.push_back(compositeBuffers[i]); // Sources10/11
        }
        for (auto& quota : compositeQuotas)
            quota = core::makeShared<core::CpuMemoryQuota>(metadata);
        const auto compositeArtifact = Plan::loadGraphics(
            directory / "shaders/original-cloud-graphics/cloud_composite.graphics.module.yaml",
            Plan::Profile::Cloud,
            compositeQuotas[0],
            shaderDiagnostic);
        assert(compositeArtifact);
        const auto compositeInput = OriginalGraphicsPipelineInput::prepare(
            compositeArtifact, {1u, metadata}, compositeQuotas[1], shaderDiagnostic);
        assert(compositeInput);
        auto definition =
            buildPipelineLayoutDescFromShaderReflection(compositeInput->package().reflection);
        for (auto& set : definition.descriptorLayouts)
            for (auto& binding : set.bindings)
                if (binding.type == DescriptorType::SampledTexture) {
                    binding.textureViewDimensionConstrained = true;
                    binding.textureViewDimension = TextureViewDimension::Texture2D;
                }
        const auto compositeLayout = core::shareUnique(d->createOriginalPipelineLayout(definition));
        assert(compositeLayout);
        auto desc = pipelineDesc;
        desc.viewport = compositeViewport;
        desc.depthFormat = TextureFormat::Unknown;
        desc.depthStencil = {};
        desc.layout = compositeLayout.get();
        desc.shaderPackage = &compositeInput->package();
        desc.vertexShader = compositeInput->package().entrypoints[0];
        desc.fragmentShader = compositeInput->package().entrypoints[1];
        const auto created = d->createOriginalSynchronousGraphicsCreation(compositeInput,
            compositeLayout,
            desc,
            {1u, true, metadata, metadata, metadata, metadata},
            compositeQuotas[2],
            compositeQuotas[3],
            id);
        require(id);
        assert(created);
        compositePipeline = OriginalRegisteredPipeline::shareAcceptedOriginal(
            d->createOriginalArtifactGraphicsPipeline(
                created, {2u, true, metadata}, compositeQuotas[4]),
            id);
        require(id);
        assert(compositePipeline);
        compositeSource = PipelineSource::prepareOriginal(compositePipeline,
            {2u,
                true,
                PipelineSource::kind,
                compositePipeline->originalResourceRegistration().generation(),
                metadata,
                PipelineSource::exclusiveOwnerCount},
            sourceQuota,
            *d,
            id);
        require(id);
        compositePublication =
            core::makeUnique<SynchronousLossGroupProbe<RenderPipeline>>(*d, compositePipeline);
    }
    if (cloud) {
        for (unsigned i = 0u; i < cloudBuffers.size(); ++i) {
            if (controlled && i) {
                cloudBuffers[i] = i == 1u    ? sequence->history.raw()
                    : cloudFrame.historyUsed ? sequence->history.previous()
                                             : sequence->slots[1].resolved;
                if (i == 1u)
                    rawObject = sequence->slotObject(cloudBuffers[i]);
                sources.push_back(cloudBuffers[i]);
                continue;
            }
            if (i == 2u && sequence && sequence->previous) {
                cloudBuffers[i] = sequence->previous;
                assert(cloudBuffers[i]->originalObject()->state() == ResourceState::ShaderRead);
                assert(!cloudBuffers[i]->originalObject()->originalCommittedStateDiagnostic());
                sources.push_back(cloudBuffers[i]);
                continue;
            }
            BufferDesc desc;
            desc.sizeBytes = i ? sampleStorageBytes : cloudAsset.cells.size() * sizeof(core::Vec4);
            desc.usage = ResourceUsage::StorageBuffer;
            desc.memory = MemoryUsage::CpuToGpu;
            desc.initialState = i == 1u ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
            auto object = d->createBuffer(desc);
            assert(object);
            std::array<renderer::cloud::CloudGpuSample, 769u> initial;
            std::memset(initial.data(), 0x5a, sizeof(initial));
            requireResult(d->writeBuffer(*object,
                i ? static_cast<const void*>(initial.data())
                  : static_cast<const void*>(cloudAsset.cells.data()),
                desc.sizeBytes));
            if (i == 1u) {
                rawObject = object.get();
                object->sealHostWrites();
            } else
                object->sealWrites();
            cloudBuffers[i] = OriginalCommandBufferLease::prepareOriginal(
                object, {1u, true, 2u, metadata}, sourceQuota, id);
            require(id);
            sources.push_back(cloudBuffers[i]); // Sources12 density,13 raw,14 previous
        }
        for (unsigned i = 0u; i < cloudPipelines.size(); ++i) {
            auto& q = cloudQuotas[i];
            for (auto& item : q)
                item = core::makeShared<core::CpuMemoryQuota>(metadata);
            const auto artifact = Plan::loadSingle(directory / "shaders/original-cloud-compute"
                    / (i ? "cloud_temporal.computeMain.entry.yaml"
                         : "cloud_raymarch.computeMain.entry.yaml"),
                Plan::Profile::Cloud,
                q[0],
                shaderDiagnostic);
            assert(artifact);
            const auto input = OriginalSingleEntryPipelineInput::prepare(
                artifact, {1u, metadata}, q[1], shaderDiagnostic);
            assert(input);
            auto definition =
                buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
            for (auto& set : definition.descriptorLayouts)
                for (auto& binding : set.bindings)
                    if (binding.type == DescriptorType::SampledTexture) {
                        binding.textureViewDimensionConstrained = true;
                        binding.textureViewDimension = TextureViewDimension::Texture2D;
                    }
            const auto layout = core::shareUnique(d->createOriginalPipelineLayout(definition));
            assert(layout);
            ComputePipelineDesc desc;
            desc.layout = layout.get();
            desc.shaderPackage = &input->package();
            desc.computeShader = input->package().entrypoints.front();
            const auto receipt = d->createOriginalSynchronousComputeCreation(input,
                layout,
                desc,
                {1u, true, metadata, metadata, metadata, metadata},
                q[2],
                q[3],
                id);
            require(id);
            assert(receipt);
            cloudPipelines[i] = OriginalRegisteredPipeline::shareAcceptedOriginal(
                d->createOriginalArtifactComputePipeline(receipt, {2u, true, metadata}, q[4]), id);
            require(id);
            assert(cloudPipelines[i]);
            using Source = OriginalCommandComputePipelineSource;
            cloudSources[i] = Source::prepareOriginal(cloudPipelines[i],
                {2u,
                    true,
                    Source::kind,
                    cloudPipelines[i]->originalResourceRegistration().generation(),
                    metadata,
                    Source::exclusiveOwnerCount},
                sourceQuota,
                *d,
                id);
            require(id);
            cloudPublications[i] =
                core::makeUnique<SynchronousLossGroupProbe<ComputePipeline>>(*d, cloudPipelines[i]);
        }
    }
    const auto graphicsIndex = static_cast<std::uint32_t>(sources.size());
    const auto graphicsSetIndex = graphicsIndex + 1u;
    const auto computeIndex = graphicsIndex + 2u, computeSetIndex = graphicsIndex + 3u;
    OriginalCommandAdmissionBudget admission;
    admission.contractVersion = 1u;
    admission.prototypeOptIn = true;
    admission.maxCommands = commandCount;
    admission.maxReferences = referenceCount;
    admission.maxInputDescriptors = descriptorCount;
    admission.maxGraphOwners = 256u;
    admission.maxPassColorAttachments = sampled ? 2u : 1u;
    admission.maxPassAttachmentReferences = sampled ? 3u : 1u;
    admission.maxStateObservations = cloud ? 23u : sampled ? 18u : 8u;
    admission.maxTextureRanges = cloud ? 16u : sampled ? 14u : 8u;
    admission.maxCpuBytes.fill(metadata);
    for (const auto domain : {CommandReferenceDomain::Texture,
             CommandReferenceDomain::TextureView,
             CommandReferenceDomain::Buffer})
        admission.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Buffer)] = cloud ? 7u
        : compute || sampled                                                                    ? 4u
                             : 2u;
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Pipeline)] =
        bindingCount;
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] =
        bindingCount;
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Texture)] =
        sampled ? 3u : 1u;
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::TextureView)] =
        sampled ? 5u : 1u;
    SynchronousLossGroupProbe<RenderPipeline> publication(*d, pipeline);
    auto inventory = Sources::prepareColdOriginalTyped(sources.data(),
        sources.size(),
        admission,
        {8u,
            true,
            cloud                    ? 8u
                : compute || sampled ? 4u
                                     : 2u},
        sourceQuota,
        id);
    require(id);
    assert(inventory);
    require(Sources::appendOriginalTyped(inventory, pipelineSource, *d));
    OriginalCommandDescriptorSetElement element;
    element.sourceIndex = 3u;
    element.registrationGeneration =
        uniformLease->originalObject()->originalResourceRegistration().generation();
    element.write.binding = 0u;
    element.write.type = DescriptorType::UniformBuffer;
    element.write.resource = uniformLease->resource();
    element.write.resourceKind = DescriptorResourceKind::Buffer;
    element.write.resourceState = ResourceState::ShaderRead;
    element.write.bufferBindingRange = {2u, 0u, 16u};
    element.bufferAccessBytes = 16u;
    element.stages = PipelineStage::FragmentShader;
    element.access = ResourceAccess::ShaderRead;
    rhi_metal::MetalOriginalBufferBindingInspection::Observation observed;
    require(rhi_metal::MetalOriginalBufferBindingInspection::inspect(
        {1u, true}, *d, inventory, graphicsIndex, 0u, element, observed));
    publication.request.maxRetainedAllocationBytes = observed.allocationBytes;
    require(d->beginOriginalGeneralDescriptorWrite(*publication.set,
        pipeline,
        inventory,
        0u,
        &element,
        1u,
        publication.request,
        publication.quotas));
    require(publication.set->finishOriginalGeneralWrite());
    using SetSource = OriginalCommandDescriptorSetSource;
    OriginalCommandDescriptorSourceBudget setBudget{
        5u, true, 1u, 1u, SetSource::finiteExclusiveRetainedOwnerCount, metadata};
    setBudget.finitePipelineSourceIndex = graphicsIndex;
    setBudget.finiteFootprintContractVersion = 2u;
    setBudget.finiteFullBoundViewOptIn = true;
    auto setSource = SetSource::prepareOriginal(publication.set,
        publication.arena,
        inventory,
        &element,
        1u,
        setBudget,
        sourceQuota,
        *d,
        id);
    require(id);
    require(Sources::appendOriginalTyped(inventory, setSource, *d));
    std::shared_ptr<const SetSource> computeSet;
    if (compute) {
        require(Sources::appendOriginalTyped(inventory, computeSource, *d));
        std::array<OriginalCommandDescriptorSetElement, 2u> elements;
        std::uint64_t retainedBytes = 0u;
        for (unsigned i = 0u; i < elements.size(); ++i) {
            auto& e = elements[i];
            const auto& buffer = *computeBuffers[i]->originalObject();
            e.sourceIndex = 4u + i;
            e.registrationGeneration = buffer.originalResourceRegistration().generation();
            e.write.binding = i;
            e.write.type = i ? DescriptorType::StorageBuffer : DescriptorType::UniformBuffer;
            e.write.resource = buffer.id();
            e.write.resourceKind = DescriptorResourceKind::Buffer;
            e.write.resourceState = buffer.desc().initialState;
            e.write.bufferBindingRange = {2u, 0u, buffer.desc().sizeBytes};
            e.bufferAccessBytes = buffer.desc().sizeBytes;
            e.stages = PipelineStage::ComputeShader;
            e.access = i ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                         : ResourceAccess::ShaderRead;
            rhi_metal::MetalOriginalBufferBindingInspection::Observation observation;
            require(rhi_metal::MetalOriginalBufferBindingInspection::inspect(
                {2u, true}, *d, inventory, computeIndex, 0u, e, observation));
            retainedBytes += observation.allocationBytes;
        }
        computePublication->request.maxRetainedAllocationBytes = retainedBytes;
        require(d->beginOriginalGeneralDescriptorWrite(*computePublication->set,
            computePipeline,
            inventory,
            0u,
            elements.data(),
            elements.size(),
            computePublication->request,
            computePublication->quotas));
        require(computePublication->set->finishOriginalGeneralWrite());
        OriginalCommandDescriptorSourceBudget computeSetBudget{
            7u, true, 2u, 2u, SetSource::finiteExclusiveRetainedOwnerCount, metadata};
        computeSetBudget.finitePipelineSourceIndex = computeIndex;
        computeSetBudget.finiteFootprintContractVersion = 2u;
        computeSetBudget.finiteFullBoundViewOptIn = true;
        computeSet = SetSource::prepareOriginal(computePublication->set,
            computePublication->arena,
            inventory,
            elements.data(),
            elements.size(),
            computeSetBudget,
            sourceQuota,
            *d,
            id);
        require(id);
        require(Sources::appendOriginalTyped(inventory, computeSet, *d));
    }
    const auto compositeIndex = graphicsIndex + 2u, compositeSetIndex = graphicsIndex + 3u;
    std::shared_ptr<const SetSource> compositeSet;
    if (sampled) {
        require(Sources::appendOriginalTyped(inventory, compositeSource, *d));
        std::array<OriginalCommandDescriptorSetElement, 4u> elements;
        std::uint64_t retainedBytes = 0u;
        for (unsigned i = 0u; i < elements.size(); ++i) {
            auto& e = elements[i];
            e.write.binding = i;
            e.write.resourceState = ResourceState::ShaderRead;
            e.stages = PipelineStage::FragmentShader;
            e.access = ResourceAccess::ShaderRead;
            if (i < 2u) {
                const auto& lease = compositeBuffers[i];
                e.sourceIndex = 10u + i;
                e.registrationGeneration =
                    lease->originalObject()->originalResourceRegistration().generation();
                e.write.type =
                    i ? DescriptorType::ReadOnlyStorageBuffer : DescriptorType::UniformBuffer;
                e.write.resource = lease->resource();
                e.write.resourceKind = DescriptorResourceKind::Buffer;
                e.write.bufferBindingRange = {2u, 0u, lease->originalObject()->desc().sizeBytes};
                e.bufferAccessBytes = lease->originalObject()->desc().sizeBytes;
                rhi_metal::MetalOriginalBufferBindingInspection::Observation observation;
                require(rhi_metal::MetalOriginalBufferBindingInspection::inspect(
                    {1u, true}, *d, inventory, compositeIndex, 0u, e, observation));
                retainedBytes += observation.allocationBytes;
            } else {
                const auto& source = i == 2u ? sceneSample : depthSample;
                e.sourceIndex = i == 2u ? 4u : 7u;
                e.registrationGeneration =
                    source->originalObject()->originalResourceRegistration().generation();
                e.write.type = DescriptorType::SampledTexture;
                e.write.resource = source->resource();
                e.write.resourceKind = DescriptorResourceKind::TextureView;
                const auto aspect = i == 2u ? TextureAspect::Color : TextureAspect::Depth;
                assert(source->originalObject()->desc().aspect == aspect);
                rhi_metal::MetalOriginalTextureBindingInspection::Observation observation;
                require(rhi_metal::MetalOriginalTextureBindingInspection::inspect(
                    {1u, true}, *d, inventory, compositeIndex, 0u, e, observation));
                retainedBytes += observation.allocationBytes;
            }
        }
        compositePublication->request.maxRetainedAllocationBytes = retainedBytes;
        require(d->beginOriginalGeneralDescriptorWrite(*compositePublication->set,
            compositePipeline,
            inventory,
            0u,
            elements.data(),
            elements.size(),
            compositePublication->request,
            compositePublication->quotas));
        require(compositePublication->set->finishOriginalGeneralWrite());
        auto budget = setBudget;
        budget.maxLogicalElements = budget.maxWrittenElements = 4u;
        budget.finitePipelineSourceIndex = compositeIndex;
        compositeSet = SetSource::prepareOriginal(compositePublication->set,
            compositePublication->arena,
            inventory,
            elements.data(),
            elements.size(),
            budget,
            sourceQuota,
            *d,
            id);
        require(id);
        require(Sources::appendOriginalTyped(inventory, compositeSet, *d));
    }
    std::array<std::shared_ptr<const SetSource>, 2u> cloudSets;
    std::array<std::uint32_t, 2u> cloudPipelineIndices{}, cloudSetIndices{};
    if (cloud)
        for (unsigned pass = 0u; pass < 2u; ++pass) {
            const auto pipelineIndex = static_cast<std::uint32_t>(inventory->sources().size());
            cloudPipelineIndices[pass] = pipelineIndex;
            cloudSetIndices[pass] = pipelineIndex + 1u;
            require(Sources::appendOriginalTyped(inventory, cloudSources[pass], *d));
            std::array<OriginalCommandDescriptorSetElement, 4u> elements;
            const std::array<std::uint32_t, 4u> indices = pass
                ? std::array<std::uint32_t, 4u>{10u,
                      13u,
                      controlled && !cloudFrame.historyUsed ? 13u : 14u,
                      11u}
                : std::array<std::uint32_t, 4u>{10u, 12u, 7u, 13u};
            std::uint64_t retainedBytes = 0u;
            for (unsigned binding = 0u; binding < 4u; ++binding) {
                auto& e = elements[binding];
                e.sourceIndex = indices[binding];
                e.write.binding = binding;
                e.write.resourceState =
                    binding == 3u ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
                e.stages = PipelineStage::ComputeShader;
                e.access = binding == 3u ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                                         : ResourceAccess::ShaderRead;
                if (!pass && binding == 2u) {
                    e.registrationGeneration =
                        depthSample->originalObject()->originalResourceRegistration().generation();
                    e.write.type = DescriptorType::SampledTexture;
                    e.write.resource = depthSample->resource();
                    e.write.resourceKind = DescriptorResourceKind::TextureView;
                    rhi_metal::MetalOriginalTextureBindingInspection::Observation observed;
                    require(rhi_metal::MetalOriginalTextureBindingInspection::inspect(
                        {2u, true}, *d, inventory, pipelineIndex, 0u, e, observed));
                    retainedBytes += observed.allocationBytes;
                } else {
                    const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                        inventory->sources()[e.sourceIndex]);
                    const auto& object = *lease->originalObject();
                    e.registrationGeneration = object.originalResourceRegistration().generation();
                    e.write.type = binding == 0u ? DescriptorType::UniformBuffer
                        : binding == 3u          ? DescriptorType::StorageBuffer
                                                 : DescriptorType::ReadOnlyStorageBuffer;
                    e.write.resource = object.id();
                    e.write.resourceKind = DescriptorResourceKind::Buffer;
                    e.write.bufferBindingRange = {2u, 0u, object.desc().sizeBytes};
                    e.bufferAccessBytes = object.desc().sizeBytes;
                    rhi_metal::MetalOriginalBufferBindingInspection::Observation observed;
                    require(rhi_metal::MetalOriginalBufferBindingInspection::inspect(
                        {2u, true}, *d, inventory, pipelineIndex, 0u, e, observed));
                    retainedBytes += observed.allocationBytes;
                }
            }
            auto& publication = *cloudPublications[pass];
            publication.request.maxRetainedAllocationBytes = retainedBytes;
            require(d->beginOriginalGeneralDescriptorWrite(*publication.set,
                cloudPipelines[pass],
                inventory,
                0u,
                elements.data(),
                elements.size(),
                publication.request,
                publication.quotas));
            require(publication.set->finishOriginalGeneralWrite());
            auto budget = setBudget;
            budget.contractVersion = 7u;
            budget.maxLogicalElements = budget.maxWrittenElements = 4u;
            budget.finitePipelineSourceIndex = pipelineIndex;
            cloudSets[pass] = SetSource::prepareOriginal(publication.set,
                publication.arena,
                inventory,
                elements.data(),
                elements.size(),
                budget,
                sourceQuota,
                *d,
                id);
            require(id);
            require(Sources::appendOriginalTyped(inventory, cloudSets[pass], *d));
        }
    const OriginalRenderPassStorageContract::Budget storage{1u,
        true,
        sampled ? 2u : 1u,
        sampled ? 1u : 0u,
        sampled ? 3u : 1u,
        {1u, true, sampled ? 2u : 1u, 0u, 0u, 0u, 0u, metadata}};
    RenderPassDesc pass;
    RenderPassColorAttachmentDesc color;
    color.target = viewSource->resource();
    color.format = td.format;
    color.loadOp = LoadOp::Clear;
    color.storeOp = StoreOp::Store;
    color.clearColor = {0.f, 0.f, 0.f, 1.f};
    pass.colorAttachments.push_back(color);
    if (sampled) {
        pass.hasDepthStencilAttachment = true;
        auto& depth = pass.depthStencilAttachment;
        depth.target = depthAttachment->resource();
        depth.format = TextureFormat::D32Float;
        depth.depthLoadOp = LoadOp::Clear;
        depth.depthStoreOp = StoreOp::Store;
        if (mode == 20u)
            depth.depthStoreOp = StoreOp::DontCare;
        depth.stencilLoadOp = LoadOp::DontCare;
        depth.stencilStoreOp = StoreOp::DontCare;
        depth.depthClearValue = 1.f;
    }
    RenderPassDesc compositePass;
    std::shared_ptr<const RenderPassArguments> compositeArguments;
    if (sampled) {
        compositePass.colorAttachments.push_back(color);
        compositePass.colorAttachments.front().target = outputAttachment->resource();
        compositeArguments =
            RenderPassArguments::prepareOriginal(compositePass, storage.arguments, graphQuota, id);
        require(id);
    }
    auto passArguments =
        RenderPassArguments::prepareOriginal(pass, storage.arguments, graphQuota, id);
    require(id);
    ResourceBarrierDesc enter;
    enter.resource = parent->resource();
    enter.textureSubresources = {0u, 1u, 0u, 1u, TextureAspect::Color};
    enter.before = ResourceState::Undefined;
    enter.after = ResourceState::ColorAttachment;
    enter.sourceStages = PipelineStage::TopOfPipe;
    enter.sourceAccess = ResourceAccess::None;
    enter.destinationStages = PipelineStage::ColorAttachmentOutput;
    enter.destinationAccess =
        ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite;
    auto leave = enter;
    leave.before = enter.after;
    leave.after = ResourceState::CopySrc;
    leave.sourceStages = enter.destinationStages;
    leave.sourceAccess = enter.destinationAccess;
    leave.destinationStages = PipelineStage::Transfer;
    leave.destinationAccess = ResourceAccess::TransferRead;
    ResourceBarrierDesc depthEnter = enter, depthLeave = leave, outputEnter = enter,
                        outputLeave = leave;
    if (sampled) {
        leave.after = ResourceState::ShaderRead;
        leave.destinationStages = PipelineStage::FragmentShader;
        leave.destinationAccess = ResourceAccess::ShaderRead;
        depthEnter.resource = depthParent->resource();
        depthEnter.textureSubresources.aspect = TextureAspect::Depth;
        depthEnter.after = ResourceState::DepthAttachment;
        depthEnter.destinationStages = PipelineStage::DepthStencil;
        depthEnter.destinationAccess =
            ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite;
        depthLeave = depthEnter;
        depthLeave.before = ResourceState::DepthAttachment;
        depthLeave.after = ResourceState::ShaderRead;
        depthLeave.sourceStages = depthEnter.destinationStages;
        depthLeave.sourceAccess = depthEnter.destinationAccess;
        depthLeave.destinationStages = cloud
            ? PipelineStage::ComputeShader | PipelineStage::FragmentShader
            : PipelineStage::FragmentShader;
        depthLeave.destinationAccess = ResourceAccess::ShaderRead;
        outputEnter.resource = outputLeave.resource = outputParent->resource();
    }
    std::array<ResourceBarrierDesc, 2u> cloudReadBarriers;
    if (cloud)
        for (unsigned pass = 0u; pass < 2u; ++pass) {
            auto& b = cloudReadBarriers[pass];
            b.resource = pass ? compositeBuffers[1]->resource() : cloudBuffers[1]->resource();
            b.before = ResourceState::ShaderWrite;
            b.after = ResourceState::ShaderRead;
            b.sourceStages = PipelineStage::ComputeShader;
            b.sourceAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            b.destinationStages =
                pass ? PipelineStage::FragmentShader : PipelineStage::ComputeShader;
            b.destinationAccess = ResourceAccess::ShaderRead;
        }
    auto cloudWriteBarriers = cloudReadBarriers;
    if (reuseCloudSlot)
        for (unsigned pass = 0u; pass < 2u; ++pass) {
            auto& b = cloudWriteBarriers[pass];
            b.before = ResourceState::ShaderRead;
            b.after = ResourceState::ShaderWrite;
            b.sourceStages = pass ? PipelineStage::ComputeShader | PipelineStage::FragmentShader
                                  : PipelineStage::ComputeShader;
            b.sourceAccess = ResourceAccess::ShaderRead;
            b.destinationStages = PipelineStage::ComputeShader;
            b.destinationAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        }
    TextureToBufferCopyDesc copy;
    copy.sourceTexture = sampled ? outputParent->resource() : parent->resource();
    copy.destinationBuffer = bufferLease->resource();
    copy.textureSubresources = enter.textureSubresources;
    copy.extent = td.extent;
    copy.bytesPerRow = 256u;
    copy.rowsPerImage = extent.height;
    core::pmr::Vector<CommandUsage> commands;
    core::pmr::Vector<OriginalCommandUsage::Reference> refs;
    core::pmr::Vector<CommandVariableArguments::Input> arguments;
    core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
    const auto op = [&](K kind, std::initializer_list<std::uint32_t> references, auto scalar) {
        const auto index = static_cast<std::uint32_t>(commands.size());
        commands.push_back({kind,
            static_cast<std::uint32_t>(refs.size()),
            static_cast<std::uint32_t>(references.size())});
        for (const auto source : references)
            refs.push_back({source});
        arguments.push_back({*CommandScalarArguments::capture(kind, scalar)});
        return index;
    };
    const auto none = CommandScalarArguments::None{};
    if (compute) {
        const auto begin = op(K::BeginComputePass, {}, none);
        descriptors.push_back({op(K::BindComputePipeline, {computeIndex}, none),
            OriginalCommandPipelineBinding{K::BindComputePipeline,
                computeIndex,
                begin,
                computePipeline->originalResourceRegistration().generation()}});
        descriptors.push_back({op(K::BindComputeDescriptor,
                                   {computeSetIndex},
                                   CommandScalarArguments::DescriptorBinding{0u}),
            OriginalCommandDescriptorBinding{K::BindComputeDescriptor,
                computeSetIndex,
                computeIndex,
                begin,
                computeSet->originalObject()->originalResourceRegistration().generation(),
                computeSet->originalObject()->contentRevision()}});
        op(K::Dispatch, {}, CommandScalarArguments::Dispatch{3u, 2u, 3u});
        op(K::EndComputePass, {}, none);
    }
    descriptors.push_back({op(K::TextureBarrier, {0u}, none),
        *ResourceDependencyArguments::capture(K::TextureBarrier, enter)});
    if (sampled)
        descriptors.push_back({op(K::TextureBarrier, {5u}, none),
            *ResourceDependencyArguments::capture(K::TextureBarrier, depthEnter)});
    const auto begin =
        sampled ? op(K::BeginRenderPass, {1u, 6u}, none) : op(K::BeginRenderPass, {1u}, none);
    descriptors.push_back({begin, passArguments});
    descriptors.push_back({op(K::BindGraphicsPipeline, {graphicsIndex}, none),
        OriginalCommandPipelineBinding{K::BindGraphicsPipeline,
            graphicsIndex,
            begin,
            pipeline->originalResourceRegistration().generation()}});
    op(K::SetViewport, {}, pipelineDesc.viewport);
    op(K::SetScissor, {}, pipelineDesc.scissor);
    descriptors.push_back({op(K::BindRenderDescriptor,
                               {graphicsSetIndex},
                               CommandScalarArguments::DescriptorBinding{0u}),
        OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
            graphicsSetIndex,
            graphicsIndex,
            begin,
            setSource->originalObject()->originalResourceRegistration().generation(),
            setSource->originalObject()->contentRevision()}});
    op(K::Draw, {}, CommandScalarArguments::Draw{3u, 1u, 0u, 0u});
    op(K::EndRenderPass, {}, none);
    descriptors.push_back({op(K::TextureBarrier, {0u}, none),
        *ResourceDependencyArguments::capture(K::TextureBarrier, leave)});
    if (sampled) {
        if (!omitDepthTransition)
            descriptors.push_back({op(K::TextureBarrier, {5u}, none),
                *ResourceDependencyArguments::capture(K::TextureBarrier, depthLeave)});
        if (reuseCloudSlot)
            for (unsigned buffer = 0u; buffer < 2u; ++buffer)
                descriptors.push_back({op(K::BufferBarrier, {buffer ? 11u : 13u}, none),
                    *ResourceDependencyArguments::capture(
                        K::BufferBarrier, cloudWriteBarriers[buffer])});
        if (cloud)
            for (unsigned pass = 0u; pass < 2u; ++pass) {
                const auto begin = op(K::BeginComputePass, {}, none);
                const auto pi = cloudPipelineIndices[pass], si = cloudSetIndices[pass];
                descriptors.push_back({op(K::BindComputePipeline, {pi}, none),
                    OriginalCommandPipelineBinding{K::BindComputePipeline,
                        pi,
                        begin,
                        cloudPipelines[pass]->originalResourceRegistration().generation()}});
                descriptors.push_back({op(K::BindComputeDescriptor,
                                           {si},
                                           CommandScalarArguments::DescriptorBinding{0u}),
                    OriginalCommandDescriptorBinding{K::BindComputeDescriptor,
                        si,
                        pi,
                        begin,
                        cloudSets[pass]
                            ->originalObject()
                            ->originalResourceRegistration()
                            .generation(),
                        cloudSets[pass]->originalObject()->contentRevision()}});
                op(K::Dispatch,
                    {},
                    CommandScalarArguments::Dispatch{
                        cloudPlan.dispatchGroups.width, cloudPlan.dispatchGroups.height, 1u});
                op(K::EndComputePass, {}, none);
                if (mode != 24u + pass)
                    descriptors.push_back({op(K::BufferBarrier, {pass ? 11u : 13u}, none),
                        *ResourceDependencyArguments::capture(
                            K::BufferBarrier, cloudReadBarriers[pass])});
            }
        descriptors.push_back({op(K::TextureBarrier, {8u}, none),
            *ResourceDependencyArguments::capture(K::TextureBarrier, outputEnter)});
        const auto compositeBegin = op(K::BeginRenderPass, {9u}, none);
        descriptors.push_back({compositeBegin, compositeArguments});
        descriptors.push_back({op(K::BindGraphicsPipeline, {compositeIndex}, none),
            OriginalCommandPipelineBinding{K::BindGraphicsPipeline,
                compositeIndex,
                compositeBegin,
                compositePipeline->originalResourceRegistration().generation()}});
        op(K::SetViewport, {}, compositeViewport);
        op(K::SetScissor, {}, pipelineDesc.scissor);
        descriptors.push_back({op(K::BindRenderDescriptor,
                                   {compositeSetIndex},
                                   CommandScalarArguments::DescriptorBinding{0u}),
            OriginalCommandDescriptorBinding{K::BindRenderDescriptor,
                compositeSetIndex,
                compositeIndex,
                compositeBegin,
                compositeSet->originalObject()->originalResourceRegistration().generation(),
                compositeSet->originalObject()->contentRevision()}});
        op(K::Draw, {}, CommandScalarArguments::Draw{3u, 1u, 0u, 0u});
        op(K::EndRenderPass, {}, none);
        descriptors.push_back({op(K::TextureBarrier, {8u}, none),
            *ResourceDependencyArguments::capture(K::TextureBarrier, outputLeave)});
    }
    op(K::BeginTransferPass, {}, none);
    descriptors.push_back({op(K::TextureToBufferCopy, {sampled ? 8u : 0u, 2u}, none),
        *TransferCopyArguments::capture(copy)});
    op(K::EndTransferPass, {}, none);
    op(K::Finish, {}, none);
    assert(commands.size() == commandCount && refs.size() == referenceCount
        && descriptors.size() == descriptorCount);
    auto usage = OriginalCommandUsage::prepareOriginal(
        inventory, commands.data(), commands.size(), refs.data(), refs.size(), graphQuota, id);
    require(id);
    auto variables = CommandVariableArguments::prepareOriginal(arguments.data(),
        arguments.size(),
        {1u, true, commandCount, 0u, 0u, 0u, metadata},
        graphQuota,
        id);
    require(id);
    auto scalars = OriginalCommandScalars::prepareOriginal(usage, variables, graphQuota, id);
    require(id);
    auto inputs = OriginalCommandInputs::prepareOriginal(scalars,
        descriptors.data(),
        descriptors.size(),
        {10u, true, bindingCount, bindingCount},
        graphQuota,
        id);
    require(id);
    OriginalCommandStateAdmissionBudget state{5u,
        true,
        1u,
        cloud         ? 32u
            : sampled ? 20u
                      : 16u,
        32u,
        cloud         ? 16u
            : sampled ? 14u
                      : 8u,
        512u,
        metadata};
    state.descriptorConsumption = {4u,
        true,
        1u,
        1u,
        true,
        true,
        bindingCount,
        cloud         ? 13u
            : sampled ? 5u
            : compute ? 3u
                      : 1u};
    state.descriptorConsumption.maxTextureAccesses = cloud ? 3u : sampled ? 2u : 1u;
    state.descriptorConsumption.generalShaderResourcesVersion = 2u;
    state.descriptorConsumption.generalShaderResourcesOptIn = true;
    state.renderDescriptorUsage = {
        4u, true, 1u, 1u, true, true, commandCount, sampled ? 2u : 1u, sampled ? 5u : 1u};
    state.renderDescriptorUsage.maxTextureAccesses = sampled ? 2u : 1u;
    state.renderDescriptorUsage.generalShaderResourcesVersion = 2u;
    state.renderDescriptorUsage.generalShaderResourcesOptIn = true;
    auto graph = OriginalCommandResourceStates::prepareOriginal(inputs, *d, state, stateQuota, id);
    require(id);
    OriginalCommandStreamBudget stream;
    stream.contractVersion = 5u;
    stream.prototypeOptIn = true;
    stream.maxCommands = commandCount;
    stream.maxDescriptors = descriptorCount;
    stream.maxReferences = referenceCount;
    stream.maxCopyElements = 1u;
    stream.maxRenderPasses = sampled ? 2u : 1u;
    stream.maxAttachmentReferences = sampled ? 3u : 1u;
    stream.maxPipelineBinds = stream.maxDescriptorBinds = stream.maxConsumptions = bindingCount;
    stream.maxImplicitAccesses = implicitAccessCount;
    stream.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
    stream.maxCpuMetadataBytes = metadata;
    stream.descriptorConsumption = state.descriptorConsumption;
    stream.renderDescriptorUsage = state.renderDescriptorUsage;
    stream.stateSequence = {5u, true, commandCount, 0u, 32u, 64u, implicitAccessCount};
    CommandRecordingBudget recording{1u, true, commandCount};
    for (const auto& op : commands)
        recording.limits.append(op.kind);
    recording.limits.resourceReferences = referenceCount;
    recording.limits.descriptorElements = cloud ? 13u : sampled ? 5u : compute ? 3u : 1u;
    auto program = OriginalCommandStreamProgram::prepareRecordingBoundOriginalTyped(
        graph, stream, recording, recordingQuota, id);
    if (omitCloudBarrier) {
        assert(!program && id && !std::strcmp(id, "RHI-ORIGINAL-SEQUENCE-SHADER-STATE"));
        assert(rawObject->state() == ResourceState::ShaderWrite
            && resolvedObject->state() == ResourceState::ShaderWrite);
        assert(textureObject->state() == ResourceState::Undefined
            && depthObject->state() == ResourceState::Undefined
            && outputObject->state() == ResourceState::Undefined);
        std::printf(
            "METAL_ORDINARY_CLOUD_BARRIER_REJECTION_PASS mode=%u diagnostic=%s noClaim=true noSubmit=true cloudFrames=0\n",
            mode,
            id);
        return;
    }
    if (omitDepthTransition) {
        assert(!program && id);
        std::printf(
            "METAL_ORDINARY_SAMPLED_MISSING_TRANSITION_PASS diagnostic=%s noClaim=true noSubmit=true cloudFrames=0\n",
            id);
        assert(textureObject->state() == ResourceState::Undefined
            && depthObject->state() == ResourceState::Undefined
            && outputObject->state() == ResourceState::Undefined);
        return;
    }
    require(id);
    OriginalRenderPassPreparationRequest preparation{6u,
        true,
        storage,
        sampled ? 5u : 4u,
        sampled ? 2u : 1u,
        OriginalRenderPassContentBudget{3u, true, sampled ? 3u : 1u, 1u, 256u, 2048u, 2048u},
        OriginalRenderPassRecordingBudget{3u, true, 2048u, 2048u}};
    if (sampled) {
        preparation.recording->opaqueDepthGraphicsVersion = 1u;
        preparation.recording->opaqueDepthGraphicsOptIn = true;
    }
    if (compute || cloud) {
        preparation.recording->dispatchDomainVersion = 1u;
        preparation.recording->dispatchDomainOptIn = true;
        preparation.dispatchDomain = domain;
    }
    assert(OriginalRenderPassPreparationContract::lowering(Backend::Metal, preparation).diagnostic);
    core::pmr::String diagnostic;
    auto registry = rhi_metal::metalRhiIssuingResourceRegistry(*d);
    if (sampled && (mode == 20u || mode == 22u)) {
        if (mode == 22u)
            preparation.recording->maxAttachmentVisits = 1u;
        assert(!rhi_metal::createMetalRhiOriginalOrdinaryCommandEncoder(
            program, *d, preparation, registry, diagnostic));
        assert(diagnostic
            == (mode == 20u ? "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED"
                            : "RHI-ORDINARY-ATTACHMENT-RECORDING-ATTACHMENT-CAPACITY"));
        assert(textureObject->state() == ResourceState::Undefined
            && depthObject->state() == ResourceState::Undefined
            && outputObject->state() == ResourceState::Undefined);
        std::printf(
            "METAL_ORDINARY_SAMPLED_COLD_REJECTION_PASS mode=%u diagnostic=%s noClaim=true noSubmit=true cloudFrames=0\n",
            mode,
            diagnostic.c_str());
        return;
    }
    if ((compute && mode == 15u) || (cloud && mode == 26u)) {
        preparation.dispatchDomain.reset();
        assert(!rhi_metal::createMetalRhiOriginalOrdinaryCommandEncoder(
            program, *d, preparation, registry, diagnostic));
        assert(diagnostic == "RHI-DISPATCH-RECORDING-DOMAIN-INPUT");
        assert(textureObject->state() == ResourceState::Undefined);
        std::printf(
            "METAL_ORDINARY_COMPUTE_MISSING_DOMAIN_PASS diagnostic=%s noClaim=true cloudFrames=0\n",
            diagnostic.c_str());
        return;
    }
    if (mode == 10u) {
        preparation.maxNativeObjects = 3u;
        assert(!rhi_metal::createMetalRhiOriginalOrdinaryCommandEncoder(
            program, *d, preparation, registry, diagnostic));
        assert(diagnostic == "RHI-METAL-ORDINARY-NATIVE-OBJECT-CAPACITY");
        assert(textureObject->state() == ResourceState::Undefined);
        std::puts("METAL_ORDINARY_OWNER_COLD_BUDGET_REJECTION_PASS noClaim=true cloudFrames=0");
        return;
    }
    if (mode == 35u) {
        assert(!rhi_metal::createMetalRhiOriginalOrdinaryCommandEncoder(
            program, *d, preparation, registry, diagnostic));
        assert(diagnostic == "RHI-SUBMISSION-COMPLETION-UNPREPARED");
        assert(textureObject->state() == ResourceState::Undefined);
        std::puts("METAL_ORIGINAL_OWNER_DOMAIN_REQUIRED_PASS noClaim=true noSubmit=true");
        return;
    }
    if (mode == 0u) {
        std::thread foreignRecordingPreparation([&] {
            core::pmr::String refused;
            assert(!rhi_metal::createMetalRhiOriginalOrdinaryCommandEncoder(
                program, *d, preparation, registry, refused));
            assert(refused == "RHI-SUBMISSION-COMPLETION-THREAD");
        });
        foreignRecordingPreparation.join();
    }
    auto encoder = rhi_metal::createMetalRhiOriginalOrdinaryCommandEncoder(
        program, *d, preparation, registry, diagnostic);
    if (!encoder)
        std::fprintf(stderr, "ordinary owner cold: %s\n", diagnostic.c_str());
    assert(encoder);
    auto native = rhi_metal::metalRhiNativePayloadFor(*encoder);
    if (mode == 0u) {
        std::thread foreignRecording([&] {
            core::pmr::String refused;
            assert(!native->recordOriginalOrdinaryTransition(0u, *textureObject, enter, refused));
            assert(refused == "RHI-SUBMISSION-COMPLETION-THREAD");
        });
        foreignRecording.join();
        assert(!native->recordingPoisoned() && !native->readbackWasSubmitted());
    }
    const auto nativeStats = native->stats();
    const std::weak_ptr<rhi_metal::MetalRhiNativeCommandPayload> nativeLifetime = native;
    auto completion = native->preparedCompletionState();
    assert(!native->originalQueueAcceptance());
    const auto content = textureObject->observeOriginalContent(enter.textureSubresources);
    assert(content);
    const auto depthContent = sampled
        ? depthObject->observeOriginalContent(depthEnter.textureSubresources)
        : decltype(content){};
    const auto outputContent = sampled
        ? outputObject->observeOriginalContent(outputEnter.textureSubresources)
        : decltype(content){};
    if (sampled)
        assert(depthContent && outputContent);
    std::array<const GpuBuffer*, 7u> cloudBufferObjects{};
    std::array<std::optional<GpuBuffer::CommittedStateObservation>, 7u> cloudBufferStates{};
    if (cloud) {
        unsigned count = 0u;
        for (const auto& source : inventory->sources())
            if (const auto* buffer =
                    std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source)) {
                assert(count < cloudBufferObjects.size());
                cloudBufferObjects[count] = (*buffer)->originalObject();
                const auto observed = cloudBufferObjects[count]->observeOriginalCommittedState();
                assert(observed);
                cloudBufferStates[count].emplace(*observed);
                ++count;
            }
        assert(count == cloudBufferObjects.size());
    }
    const auto requireRestored = [&] {
        const auto check = [&](const GpuTexture& object, const auto& before, const auto& range) {
            assert(object.state() == ResourceState::Undefined);
            assert(!object.originalCommittedStateDiagnostic(range));
            const auto restored = object.observeOriginalContent(range);
            assert(restored && !object.validateOriginalContent(*restored));
            assert(restored->state() == before->state()
                && restored->contentGeneration() == before->contentGeneration()
                && restored->revision() >= before->revision());
        };
        check(*textureObject, content, enter.textureSubresources);
        if (sampled) {
            check(*depthObject, depthContent, depthEnter.textureSubresources);
            check(*outputObject, outputContent, outputEnter.textureSubresources);
        }
        if (cloud)
            for (unsigned i = 0u; i < cloudBufferObjects.size(); ++i) {
                const auto& object = *cloudBufferObjects[i];
                assert(!object.originalCommittedStateDiagnostic());
                const auto restored = object.observeOriginalCommittedState();
                assert(restored && restored->state() == cloudBufferStates[i]->state());
            }
    };
    std::optional<core::CpuMemoryQuotaLease> pressure;
    auto controllerFence = controlled || mode == 12u || (mode >= 31u && mode <= 34u)
        ? core::shareUnique(d->createFence())
        : std::shared_ptr<GpuFence>{};
    auto replacementCompletion =
        mode == 28u ? core::makeShared<rhi_metal::MetalRhiCompletionState>() : nullptr;
    const auto foreignAcceptanceOwner = queueSequence ? core::makeShared<unsigned>(0u) : nullptr;
    if (controlled)
        assert(controllerFence && !controllerFence->isSignaled());
    if (mode == 6u)
        pressure.emplace(recordingQuota, metadata - recordingQuota->liveBytes());
    if (mode == 11u)
        sourceQuota->setLimitBytes(metadata - 1u);
    if (mode == 8u || mode == 9u || mode == 11u) {
        const auto rejected = encoder->transition(*textureObject, enter);
        assert(!rejected.success);
        if (mode != 11u)
            assert(rejected.diagnostics == "RHI-METAL-ORDINARY-BUFFER-INITIALIZATION");
        sourceQuota->setLimitBytes(metadata);
        assert(!encoder->transition(*textureObject, enter).success);
        encoder.reset();
        native->abandonUnsubmitted();
        assert(textureObject->state() == ResourceState::Undefined);
        assert(!textureObject->validateOriginalContent(*content));
        rhi_metal::MetalRhiNativeQueueStats stats;
        assert(!native->submit(stats, completion, {}, 0u, {}, 0u, diagnostic));
        assert(!stats.nativeQueueSubmitCount);
        std::printf(
            "METAL_ORDINARY_OWNER_PREFLIGHT_REJECTION_PASS mode=%u noSubmit=true irreversible=true cloudFrames=0\n",
            mode);
        return;
    }
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    if (compute) {
        const auto rejectedCompute = [&](const CommandValidationResult& result,
                                         const char* expected) {
            assert(!result.success && result.diagnostics == expected);
            assert(!nativeStats->nativeDispatchEncoded);
            encoder.reset();
            native->abandonUnsubmitted();
            requireRestored();
            rhi_metal::MetalRhiNativeQueueStats stats;
            assert(!native->submit(stats, completion, {}, 0u, {}, 0u, diagnostic));
            assert(!stats.nativeQueueSubmitCount);
            std::printf(
                "METAL_ORDINARY_COMPUTE_REJECTION_PASS mode=%u diagnostic=%s noDispatch=true noSubmit=true rollback=true cloudFrames=0\n",
                mode,
                expected);
        };
        if (mode == 17u) {
            ComputePassDesc wrong;
            wrong.label = "different compute label";
            assert(!encoder->beginComputePass(wrong));
            rejectedCompute(encoder->lastValidation(), "RHI-METAL-ORDINARY-PASS-LABEL");
            return;
        }
        auto pass = encoder->beginComputePass({});
        if (!pass)
            std::fprintf(stderr,
                "ordinary compute begin: %s\n",
                encoder->lastValidation().diagnostics.c_str());
        assert(pass);
        requireResult(pass->setPipeline(*computePipeline));
        if (mode == 18u) {
            rejectedCompute(pass->bindDescriptorSet(1u, *computeSet->originalObject()),
                "RHI-METAL-ORDINARY-DESCRIPTOR-INPUT");
            return;
        }
        requireResult(pass->bindDescriptorSet(0u, *computeSet->originalObject()));
        if (mode == 14u) {
            rejectedCompute(pass->dispatch(2u, 2u, 3u), "RHI-METAL-ORDINARY-SCALAR-INPUT");
            return;
        }
        if (mode == 16u) {
            domainQuota->setLimitBytes(metadata - 1u);
            const auto rejected = pass->dispatch(3u, 2u, 3u);
            assert(!rejected.success && rejected.diagnostics == "RHI-DISPATCH-DOMAIN-QUOTA");
            domainQuota->setLimitBytes(metadata);
            assert(domain->diagnostic());
            assert(!pass->dispatch(3u, 2u, 3u).success);
            rejectedCompute(rejected, "RHI-DISPATCH-DOMAIN-QUOTA");
            return;
        }
        requireResult(pass->dispatch(3u, 2u, 3u));
        requireResult(pass->end());
    }
    requireResult(encoder->transition(*textureObject, enter));
    if (sampled)
        requireResult(encoder->transition(*depthObject, depthEnter));
    if (mode == 3u) {
        pass.colorAttachments.front().clearColor.r = .25f;
        assert(!encoder->beginRenderPass(pass));
    } else {
        auto render = encoder->beginRenderPass(pass);
        if (!render)
            std::fprintf(stderr,
                "ordinary owner begin: %s\n",
                encoder->lastValidation().diagnostics.c_str());
        assert(render);
        if (mode == 1u) {
            encoder.reset();
            render.reset();
        } else if (mode == 2u) {
            assert(!render->setViewport({0.f, 0.f, 4.f, 4.f, 0.f, 1.f}).success);
        } else if (mode == 13u) {
            requireResult(render->setPipeline(*pipeline));
            auto substituted = pipelineDesc.viewport;
            substituted.width = 3.f;
            assert(!render->setViewport(substituted).success);
        } else {
            requireResult(render->setPipeline(*pipeline));
            requireResult(render->setViewport(pipelineDesc.viewport));
            requireResult(render->setScissor(pipelineDesc.scissor));
            requireResult(render->bindDescriptorSet(0u, *setSource->originalObject()));
            requireResult(render->draw(3u, 1u, 0u, 0u));
            if (sampled)
                assert(nativeStats->nativeDepthStencilAttachmentRecorded
                    && nativeStats->nativeDepthStencilStateBound);
            requireResult(render->end());
            render.reset();
            requireResult(encoder->transition(*textureObject, leave));
            if (mode == 7u) {
                TransferPassDesc substituted;
                substituted.label = "different original label";
                assert(!encoder->beginTransferPass(substituted));
                encoder.reset();
                native->abandonUnsubmitted();
                requireRestored();
                std::puts("METAL_ORDINARY_OWNER_LABEL_REJECTION_PASS rollback=true cloudFrames=0");
                return;
            }
            if (sampled) {
                requireResult(encoder->transition(*depthObject, depthLeave));
                if (reuseCloudSlot) {
                    requireResult(encoder->transition(*rawObject, cloudWriteBarriers[0]));
                    requireResult(encoder->transition(*resolvedObject, cloudWriteBarriers[1]));
                }
                if (cloud)
                    for (unsigned pass = 0u; pass < 2u; ++pass) {
                        auto compute = encoder->beginComputePass({});
                        assert(compute);
                        requireResult(compute->setPipeline(*cloudPipelines[pass]));
                        requireResult(
                            compute->bindDescriptorSet(0u, *cloudSets[pass]->originalObject()));
                        if (mode == 27u) {
                            const auto refused = compute->dispatch(2u, 1u, 1u);
                            assert(!refused.success
                                && refused.diagnostics == "RHI-METAL-ORDINARY-SCALAR-INPUT");
                            compute.reset();
                            encoder.reset();
                            native->abandonUnsubmitted();
                            requireRestored();
                            assert(!nativeStats->nativeDispatchEncoded);
                            rhi_metal::MetalRhiNativeQueueStats stats;
                            assert(!native->submit(stats, completion, {}, 0u, {}, 0u, diagnostic)
                                && !stats.nativeQueueSubmitCount);
                            std::puts(
                                "METAL_ORDINARY_CLOUD_TUPLE_REJECTION_PASS noDispatch=true noSubmit=true rollback=true cloudFrames=0");
                            return;
                        }
                        requireResult(compute->dispatch(
                            cloudPlan.dispatchGroups.width, cloudPlan.dispatchGroups.height, 1u));
                        requireResult(compute->end());
                        compute.reset();
                        auto& object = pass ? *resolvedObject : *rawObject;
                        requireResult(encoder->transition(object, cloudReadBarriers[pass]));
                    }
                requireResult(encoder->transition(*outputObject, outputEnter));
                auto composite = encoder->beginRenderPass(compositePass);
                assert(composite);
                requireResult(composite->setPipeline(*compositePipeline));
                requireResult(composite->setViewport(compositeViewport));
                requireResult(composite->setScissor(pipelineDesc.scissor));
                requireResult(composite->bindDescriptorSet(0u, *compositeSet->originalObject()));
                requireResult(composite->draw(3u, 1u, 0u, 0u));
                requireResult(composite->end());
                composite.reset();
                requireResult(encoder->transition(*outputObject, outputLeave));
            }
            auto transfer = encoder->beginTransferPass({});
            if (!transfer)
                std::fprintf(stderr,
                    "ordinary transfer begin: %s\n",
                    encoder->lastValidation().diagnostics.c_str());
            assert(transfer);
            if (mode == 5u) {
                auto wrong = copy;
                wrong.bufferOffsetBytes = 256u;
                assert(!transfer->copyTextureToBuffer(wrong).success);
            } else {
                requireResult(transfer->copyTextureToBuffer(copy));
                requireResult(transfer->end());
                transfer.reset();
                auto command = encoder->finish();
                if (!command)
                    std::fprintf(stderr,
                        "ordinary owner finish: %s\n",
                        encoder->lastValidation().diagnostics.c_str());
                assert(command && command->commandCount() == commandCount);
                assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
                if (mode == 29u || mode == 30u) {
                    auto hold = test::MetalCommandCancellationFixture::holdCommit(registry);
                    diagnostic.reserve(256u);
                    rhi_metal::MetalRhiNativeQueueStats busyStats;
                    const auto beforeBusy = core::defaultCpuAllocator().stats().allocationCount;
                    assert(!native->submit(busyStats, completion, {}, 0u, {}, 0u, diagnostic));
                    assert(diagnostic == "RHI-QUEUE-ACCEPTANCE-BUSY"
                        && !native->readbackWasSubmitted() && !native->recordingPoisoned()
                        && !busyStats.nativeQueueSubmitCount && !completion->isComplete()
                        && !native->originalQueueAcceptance());
                    assert(beforeBusy == core::defaultCpuAllocator().stats().allocationCount);
                    const auto busy = d->queue().submit(*command);
                    assert(!busy.success && !busy.workAccepted
                        && busy.diagnostics == "RHI-QUEUE-ACCEPTANCE-BUSY");
                    assert(!native->readbackWasSubmitted() && !native->recordingPoisoned()
                        && !completion->isComplete() && !native->originalQueueAcceptance());
                    assert(
                        textureObject->originalCommittedStateDiagnostic(enter.textureSubresources));
                    hold->releaseAndWait();
                    std::printf(
                        "METAL_QUEUE_COMMIT_BUSY_PASS mode=%u realLockHeld=true submitted=false completionPending=true noReceipt=true noPoison=true hotAllocation0=true\n",
                        mode);
                }
                if (mode == 28u) {
                    rhi_metal::MetalRhiNativeQueueStats wrongStats;
                    assert(!native->submit(
                        wrongStats, replacementCompletion, {}, 0u, {}, 0u, diagnostic));
                    assert(diagnostic == "RHI-QUEUE-ACCEPTANCE-COMPLETION-OWNER"
                        && !wrongStats.nativeQueueSubmitCount);
                    assert(!native->originalQueueAcceptance());
                    command.reset();
                } else if (mode == 4u || mode == 30u)
                    command.reset();
                else {
                    std::shared_ptr<rhi_metal::MetalRhiNativeSharedEventPayload> gate;
                    std::shared_ptr<rhi_metal::MetalRhiNativeCommandPayload> blocker;
                    if (mode == 12u || mode == 31u || mode == 32u || mode == 33u || mode == 34u
                        || (compute && mode == 19u)) {
                        gate =
                            rhi_metal::createMetalRhiNativeSharedEventPayload("OrdinaryOwnerGate");
                        assert(gate && gate->nativeRuntimeBacked);
                        blocker = rhi_metal::createMetalRhiNativeCommandPayload(
                            "OrdinaryOwnerBlocker", registry, &diagnostic);
                        assert(blocker && blocker->finish(diagnostic));
                        rhi_metal::MetalRhiNativeQueueStats blockerStats;
                        assert(blocker->submit(blockerStats,
                            blocker->preparedCompletionState(),
                            gate,
                            1u,
                            {},
                            0u,
                            diagnostic));
                    }
                    const auto controllerToken = controlled ? sequence->history.token()
                                                            : render_graph::PassSubmissionToken{};
                    if (controlled)
                        require(sequence->history.validateSubmission(controllerToken));
                    if (mode == 12u) {
                        std::thread foreignSubmission([&] {
                            rhi_metal::MetalRhiNativeQueueStats refusedStats;
                            core::pmr::String refused;
                            assert(
                                !native->submit(refusedStats, completion, {}, 0u, {}, 0u, refused));
                            assert(refused == "RHI-SUBMISSION-COMPLETION-THREAD"
                                && !refusedStats.nativeQueueSubmitCount);
                        });
                        foreignSubmission.join();
                        assert(!native->readbackWasSubmitted() && !native->recordingPoisoned());
                    }
                    const auto beforeSubmitAllocations =
                        core::defaultCpuAllocator().stats().allocationCount;
                    if (mode == 36u) {
                        rhi_metal::MetalRhiNativeQueueStats actualStats;
                        assert(native->submit(actualStats, completion, {}, 0u, {}, 0u, diagnostic));
                        test::MetalCommandCancellationFixture::
                            requireNativeResultWithoutOwnerPublication(*completion);
                        assert(textureObject->originalCommittedStateDiagnostic(
                            enter.textureSubresources));
                        assert(rhi::acceptRecordedCommandOwnership(*command));
                        assert(textureObject->originalCommittedStateDiagnostic(
                            enter.textureSubresources));
                        std::puts(
                            "METAL_ORIGINAL_ACCEPTANCE_BEFORE_PUBLICATION_PASS actualResultArrived=true acknowledgementDoesNotPoll=true originalJournalPending=true");
                    } else {
                        requireResult(d->queue().submit(*command, controllerFence.get()));
                    }
                    const auto acceptance = native->originalQueueAcceptance();
                    assert(!rhi_metal::MetalOriginalQueueAcceptance::validateOriginal(acceptance)
                        && acceptance->matchesProgram(program));
                    assert(!acceptance->provesGpuCompletion());
                    assert(beforeSubmitAllocations
                        == core::defaultCpuAllocator().stats().allocationCount);
                    if (queueSequence) {
                        const std::shared_ptr<const rhi_metal::MetalOriginalQueueAcceptance> forged(
                            foreignAcceptanceOwner, acceptance.get());
                        assert(!std::strcmp(
                            rhi_metal::MetalOriginalQueueAcceptance::validateOriginal(forged),
                            "RHI-QUEUE-ACCEPTANCE-OWNER"));
                        assert(!std::strcmp(
                            rhi_metal::MetalOriginalQueueAcceptance::validateOriginal({}),
                            "RHI-QUEUE-ACCEPTANCE-OWNER"));
                        if (queueSequence->previous) {
                            assert(!queueSequence->previous->precedes(*acceptance));
                            assert(acceptance->precedes(*queueSequence->previous));
                            assert(!queueSequence->previous->matchesProgram(program));
                        }
                        assert(acceptance->precedes(*acceptance));
                        queueSequence->previous = acceptance;
                    }
                    if (controlled) {
                        sequence->history.onSubmitted(controllerToken, controllerFence);
                        if (!sequence->submitted)
                            sequence->firstWriterFence = controllerFence;
                    }
                    if (mode == 12u || mode == 31u || mode == 32u || mode == 33u || mode == 34u
                        || (compute && mode == 19u)) {
                        assert(!completion->isComplete());
                        assert(textureObject->originalCommittedStateDiagnostic(
                            enter.textureSubresources));
                        assert(textureObject->validateOriginalContent(*content));
                        if (sampled) {
                            assert(depthObject->originalCommittedStateDiagnostic(
                                depthEnter.textureSubresources));
                            assert(outputObject->originalCommittedStateDiagnostic(
                                outputEnter.textureSubresources));
                            assert(depthObject->validateOriginalContent(*depthContent));
                            assert(outputObject->validateOriginalContent(*outputContent));
                        }
                        if (cloud)
                            for (const auto* object : cloudBufferObjects)
                                assert(object->originalCommittedStateDiagnostic());
                        if (controlled) {
                            assert(sequence->submitted == 1u
                                && sequence->firstWriterFence->isSignaled());
                            assert(!controllerFence->isSignaled() && !sequence->history.idle());
                            const auto refused = sequence->history.releaseIdle();
                            assert(!refused.success
                                && !std::strcmp(refused.diagnostic, "CLOUD-RESIZE-IN-FLIGHT"));
                            render_graph::FrameGraph nextGraph(*d);
                            renderer::cloud::CloudGraphContext next{nextGraph};
                            ordinaryCloudContext(
                                next, cloudLayer, sequence->submitted + 1u, extent);
                            const auto countBefore =
                                core::defaultCpuAllocator().stats().allocationCount;
                            const auto result = sequence->history.prepareFrame(next);
                            if (sequence->slotCount == 2u) {
                                assert(!result.success
                                    && !std::strcmp(result.diagnostic, "CLOUD-FRAMES-IN-FLIGHT"));
                                assert(!sequence->history.frameReady());
                                assert(countBefore
                                    == core::defaultCpuAllocator().stats().allocationCount);
                            } else {
                                requireCloud(result);
                                assert(sequence->history.raw() == sequence->slots[2].raw);
                                assert(sequence->history.previous() == compositeBuffers[1]);
                                assert(sequence->history.raw() != sequence->slots[0].raw);
                                assert(sequence->history.pendingFrame().historyUsed);
                                const auto phase =
                                    sequence->history.pendingFrame().parameters.jitter.z;
                                requireCloud(sequence->history.cancelPendingFrame());
                                requireCloud(sequence->history.prepareFrame(next));
                                assert(
                                    sequence->history.pendingFrame().parameters.jitter.z == phase);
                                requireCloud(sequence->history.cancelPendingFrame());
                            }
                            const auto* pending = sequence->slots[0]
                                                      .resolved->originalObject()
                                                      ->originalCommittedStateDiagnostic();
                            assert(pending
                                && !std::strcmp(pending, "RHI-ORIGINAL-BUFFER-STATE-PENDING"));
                            std::printf(
                                "METAL_CLOUD_SLOT_PENDING_READER_PASS slots=%u writerComplete=true readerPending=true resizeRefused=true exhaustedOrThirdSlot=true noHostCompletion=true\n",
                                sequence->slotCount);
                        }
                        native->poisonRecording("TEST-METAL-ORDINARY-LATE-POISON");
                        native->abandonUnsubmitted();
                        command.reset();
                        encoder.reset();
                        assert(!completion->isComplete() && !native->recordingPoisoned());
                        native.reset();
                        assert(nativeLifetime.expired());
                        assert(!acceptance->diagnostic() && !completion->isComplete());
                        if (mode == 19u)
                            domainQuota->setLimitBytes(metadata - 1u);
                        const std::weak_ptr<rhi_metal::MetalOriginalSubmissionDomain>
                            retainedDomain = rhi_metal::metalRhiOriginalSubmissionDomain(*d);
                        if (mode == 31u) {
                            semantics::ShutdownDrainRequest request;
                            request.deadline = Clock::now() + std::chrono::milliseconds(10);
                            const auto report = d->drainForShutdown(request);
                            assert(!report.completed()
                                && report.state == semantics::ShutdownDrainState::DeadlineExpired);
                            assert(!completion->isComplete() && !retainedDomain.expired());
                        }
                        if (mode == 32u || mode == 34u) {
                            assert(ownedDevice);
                            ownedDevice.reset();
                            d = nullptr;
                            assert(!completion->isComplete() && !retainedDomain.expired());
                        }
                        rhi_metal::signalMetalRhiNativeSharedEvent(gate, 1u);
                        if (mode == 31u || mode == 32u || mode == 34u) {
                            if (mode == 34u)
                                until(
                                    [&] {
                                        return retainedDomain.expired();
                                    },
                                    "final native result owner cleanup");
                            bool failedFence = false;
                            try {
                                controllerFence->wait();
                            } catch (const std::runtime_error& error) {
                                failedFence =
                                    !std::strcmp(error.what(), "RHI-METAL-FENCE-SUBMISSION-FAILED");
                            }
                            assert(failedFence && controllerFence->isSignaled());
                            blocker->preparedCompletionState()->wait();
                            assert(completion->outcome()
                                == rhi_metal::MetalRhiCompletionState::Outcome::Failed);
                            assert(textureObject->originalCommittedStateDiagnostic(
                                enter.textureSubresources));
                            if (mode == 32u)
                                until(
                                    [&] {
                                        return retainedDomain.expired();
                                    },
                                    "retired native domain release");
                            std::printf(
                                "METAL_ORIGINAL_NATIVE_RETIREMENT_PASS mode=%u pendingCustodyRetained=true actualResultRequired=true successNotRevived=true nativeCommandDestroyed=true\n",
                                mode);
                            return;
                        }
                        test::MetalCommandCancellationFixture::
                            requireNativeResultWithoutOwnerPublication(*completion);
                        assert(textureObject->originalCommittedStateDiagnostic(
                            enter.textureSubresources));
                        if (cloud)
                            for (const auto* object : cloudBufferObjects)
                                assert(object->originalCommittedStateDiagnostic());
                        std::puts(
                            "METAL_ORIGINAL_NATIVE_RESULT_HANDOFF_PASS callbackDelivered=true journalStillPending=true foreignWaitRefused=true nativeCommandDestroyed=true");
                        if (mode == 33u) {
                            d->waitIdle();
                            assert(completion->isComplete());
                        }
                        if (mode == 12u) {
                            controllerFence->wait();
                            assert(controllerFence->isSignaled());
                        }
                    }
                    completion->wait();
                    if (blocker)
                        blocker->preparedCompletionState()->wait();
                    if (compute && mode == 19u) {
                        assert(completion->outcome()
                            == rhi_metal::MetalRhiCompletionState::Outcome::Failed);
                        assert(acceptance->diagnostic());
                        assert(domain->diagnostic());
                        domainQuota->setLimitBytes(metadata);
                        assert(domain->diagnostic());
                        assert(textureObject->state()
                            == (sampled ? ResourceState::ShaderRead : ResourceState::CopySrc));
                        assert(textureObject->originalCommittedStateDiagnostic(
                            enter.textureSubresources));
                        assert(!textureObject->observeOriginalContent(enter.textureSubresources));
                        std::puts(
                            "METAL_ORDINARY_COMPUTE_COMPLETION_REVOKED_PASS actualCompletion=true failed=true invalidated=true noRollback=true nativeCommandDestroyed=true cloudFrames=0");
                        return;
                    }
                    assert(completion->outcome()
                        == rhi_metal::MetalRhiCompletionState::Outcome::Completed);
                    assert(!acceptance->diagnostic());
                    if (controlled)
                        assert(controllerFence->isSignaled() && sequence->history.idle());
                    assert(textureObject->state()
                        == (sampled ? ResourceState::ShaderRead : ResourceState::CopySrc));
                    core::pmr::Vector<unsigned char> result(bd.sizeBytes);
                    assert(rhi_metal::readMetalRhiNativeBuffer(
                        rhi_metal::metalRhiNativePayloadFor(*bufferLease->originalObject()),
                        result.data(),
                        result.size(),
                        0u,
                        bd.sizeBytes,
                        diagnostic));
                    const std::array<std::uint16_t, 4u> pixel{0x3000u, 0x3600u, 0x3a00u, 0x3900u};
                    const std::array<std::uint16_t, 4u> cleared{
                        sampled ? std::uint16_t{0x3400u} : std::uint16_t{0u},
                        sampled ? std::uint16_t{0x3800u} : std::uint16_t{0u},
                        sampled ? std::uint16_t{0x3a00u} : std::uint16_t{0u},
                        0x3c00u};
                    if (cloud) {
                        using Sample = renderer::cloud::CloudGpuSample;
                        float worstSample = 0.f, worstMoment = 0.f, worstHdr = 0.f;
                        float historyInfluence = 0.f, expectedHistoryInfluence = 0.f;
                        unsigned nonVacuum = 0u, foregroundCount = 0u;
                        std::array<Sample, 769u> actualRaw, actualResolved;
                        for (unsigned pass = 0u; pass < 2u; ++pass) {
                            const auto& lease = pass ? compositeBuffers[1] : cloudBuffers[1];
                            std::array<Sample, 769u> actual;
                            std::memset(actual.data(), 0x5a, sizeof(actual));
                            assert(rhi_metal::readMetalRhiNativeBuffer(
                                rhi_metal::metalRhiNativePayloadFor(*lease->originalObject()),
                                actual.data(),
                                sampleStorageBytes,
                                0u,
                                sampleStorageBytes,
                                diagnostic));
                            if (pass) {
                                actualResolved = actual;
                                if (!cloudFrame.historyUsed)
                                    assert(!std::memcmp(
                                        actual.data(), actualRaw.data(), sizeof(actual)));
                            } else
                                actualRaw = actual;
                            std::array<unsigned char, sizeof(Sample)> sentinel;
                            sentinel.fill(0x5a);
                            assert(!std::memcmp(
                                &actual[sampleCount], sentinel.data(), sentinel.size()));
                            for (unsigned i = 0u; i < sampleCount; ++i) {
                                const auto a = tests::cloud::SequenceOracle::components(
                                    actual[i].radianceTransmittance);
                                const auto e = tests::cloud::SequenceOracle::components(
                                    (pass ? cloudResolvedExpected[i] : cloudExpected[i])
                                        .radianceTransmittance);
                                for (unsigned c = 0u; c < 4u; ++c) {
                                    assert(std::isfinite(a[c]));
                                    worstSample = std::max(worstSample, std::fabs(a[c] - e[c]));
                                }
                                assert(std::isfinite(actual[i].metadata.x));
                                worstMoment = std::max(worstMoment,
                                    std::fabs(actual[i].metadata.x - cloudExpected[i].metadata.x));
                                if (std::fabs(actual[i].metadata.x - cloudExpected[i].metadata.x)
                                    >= .02f)
                                    std::printf(
                                        "METAL_CLOUD_MOMENT_DIAGNOSTIC pass=%u pixel=%u actual=%.9g expected=%.9g actualT=%.9g expectedT=%.9g\n",
                                        pass,
                                        i,
                                        double(actual[i].metadata.x),
                                        double(cloudExpected[i].metadata.x),
                                        double(actual[i].radianceTransmittance.w),
                                        double(cloudExpected[i].radianceTransmittance.w));
                                assert(std::fabs(actual[i].metadata.y - cloudExpected[i].metadata.y)
                                    < .002f);
                                assert(actual[i].metadata.z == 0.f && actual[i].metadata.w == 0.f);
                            }
                        }
                        const tests::cloud::ForegroundFixture triangle{true,
                            pipelineDesc.viewport.x,
                            pipelineDesc.viewport.y,
                            pipelineDesc.viewport.width,
                            pipelineDesc.viewport.height};
                        for (unsigned y = 0u; y < extent.height; ++y) {
                            for (unsigned x = 0u; x < extent.width; ++x) {
                                const auto nativeY =
                                    semantics::projectionRequiresYFlip(Backend::Metal)
                                    ? extent.height - 1u - y
                                    : y;
                                const bool covered = triangle.covers(x, nativeY);
                                foregroundCount += covered;
                                const auto& expected = cloudResolvedExpected[y * extent.width + x]
                                                           .radianceTransmittance;
                                nonVacuum += !covered && expected.w < 1.f;
                                std::array<std::uint16_t, 4u> texel;
                                std::memcpy(texel.data(), result.data() + y * 256u + x * 8u, 8u);
                                if (covered && !(visualCase && visualCase->orthographic)) {
                                    assert(expected.x == 0.f && expected.y == 0.f
                                        && expected.z == 0.f && expected.w == 1.f);
                                    assert(texel == pixel);
                                }
                                const auto scene =
                                    covered ? uniformBytes : std::array<float, 4u>{0, 0, 0, 1};
                                const std::array<float, 4u> hdr{expected.x + expected.w * scene[0],
                                    expected.y + expected.w * scene[1],
                                    expected.z + expected.w * scene[2],
                                    scene[3]};
                                for (unsigned c = 0u; c < 4u; ++c)
                                    worstHdr = std::max(
                                        worstHdr, std::fabs(core::halfToFloat(texel[c]) - hdr[c]));
                            }
                            for (unsigned x = extent.width * 8u; x < 256u; ++x)
                                assert(result[y * 256u + x] == 0x5a);
                        }
                        for (unsigned i = extent.height * 256u; i < result.size(); ++i)
                            assert(result[i] == 0x5a);
                        std::printf(
                            "METAL_ORDINARY_CLOUD_NUMERIC mode=%u sampleError=%.9g momentError=%.9g hdrError=%.9g nonVacuum=%u foreground=%u\n",
                            mode,
                            double(worstSample),
                            double(worstMoment),
                            double(worstHdr),
                            nonVacuum,
                            foregroundCount);
                        std::fflush(stdout);
                        assert(worstSample < .002f && (visualCase || worstMoment < .02f)
                            && worstHdr < .002f && (visualCase || (nonVacuum && foregroundCount)));
                        assert(nativeStats->nativeDispatchEncoded && nativeStats->nativeDrawEncoded
                            && nativeStats->nativeBlitCopyEncoded);
                        assert(depthObject->state() == ResourceState::ShaderRead
                            && outputObject->state() == ResourceState::CopySrc);
                        for (auto* object : {textureObject, depthObject, outputObject}) {
                            const auto range = object == depthObject
                                ? depthEnter.textureSubresources
                                : enter.textureSubresources;
                            assert(!object->originalCommittedStateDiagnostic(range));
                            const auto published = object->observeOriginalContent(range);
                            assert(published && !object->validateOriginalContent(*published));
                        }
                        for (const auto* object : cloudBufferObjects)
                            assert(!object->originalCommittedStateDiagnostic());
                        assert(rawObject->state() == ResourceState::ShaderRead
                            && resolvedObject->state() == ResourceState::ShaderRead);
                        assert(textureObject->validateOriginalContent(*content)
                            && depthObject->validateOriginalContent(*depthContent)
                            && outputObject->validateOriginalContent(*outputContent));
                        if (sequence) {
                            for (unsigned i = 0u; i < sampleCount; ++i) {
                                const auto raw = tests::cloud::SequenceOracle::components(
                                    actualRaw[i].radianceTransmittance);
                                const auto resolved = tests::cloud::SequenceOracle::components(
                                    actualResolved[i].radianceTransmittance);
                                const auto expectedRaw = tests::cloud::SequenceOracle::components(
                                    cloudExpected[i].radianceTransmittance);
                                const auto expectedResolved =
                                    tests::cloud::SequenceOracle::components(
                                        cloudResolvedExpected[i].radianceTransmittance);
                                for (unsigned c = 0u; c < 4u; ++c) {
                                    historyInfluence =
                                        std::max(historyInfluence, std::fabs(raw[c] - resolved[c]));
                                    expectedHistoryInfluence = std::max(expectedHistoryInfluence,
                                        std::fabs(expectedRaw[c] - expectedResolved[c]));
                                }
                            }
                            if (cloudFrame.historyUsed) {
                                assert(sequence->previous == cloudBuffers[2]);
                                std::array<Sample, 769u> old;
                                std::memset(old.data(), 0x5a, sizeof(old));
                                assert(rhi_metal::readMetalRhiNativeBuffer(
                                    rhi_metal::metalRhiNativePayloadFor(
                                        *sequence->previous->originalObject()),
                                    old.data(),
                                    sampleStorageBytes,
                                    0u,
                                    sampleStorageBytes,
                                    diagnostic));
                                assert(!std::memcmp(old.data(),
                                    sequence->previousBytes.data(),
                                    sizeof(sequence->previousBytes)));
                                assert(pendingOracle.accepted && historyInfluence > .00001f
                                    && expectedHistoryInfluence > .00001f);
                            }
                            std::printf(
                                "METAL_ORDINARY_CLOUD_HISTORY_FRAME_PASS frame=%u historyUsed=%d accepted=%u influence=%.9g expectedInfluence=%.9g previousGpuUnchanged=true hotAllocation0=true\n",
                                sequence->submitted,
                                cloudFrame.historyUsed,
                                pendingOracle.accepted,
                                double(historyInfluence),
                                double(expectedHistoryInfluence));
                            sequence->previous = compositeBuffers[1];
                            std::copy_n(actualResolved.begin(),
                                sequence->previousBytes.size(),
                                sequence->previousBytes.begin());
                            sequence->frame = cloudFrame;
                            sequence->oracle = std::move(pendingOracle);
                            ++sequence->submitted;
                            return;
                        }
                        if (visualOutput) {
                            const auto save =
                                [&](const char* suffix, const void* data, std::size_t bytes) {
                                    const auto path = core::pmr::String(visualOutput) + suffix;
                                    auto* file = std::fopen(path.c_str(), "wb");
                                    assert(file && std::fwrite(data, 1u, bytes, file) == bytes);
                                    assert(std::fclose(file) == 0);
                                };
                            save(".hdr.rgba16f", result.data(), result.size());
                            save(".samples.f32",
                                actualResolved.data(),
                                sampleCount * sizeof(Sample));
                            save(".expected.f32",
                                cloudResolvedExpected.data(),
                                sampleCount * sizeof(Sample));
                            save(".constants.bin", &cloudParameters, sizeof(cloudParameters));
                            const auto captured = d->endFrameCapture();
                            assert(captured.success());
                            std::printf(
                                "METAL_CLOUD_VISUAL_PASS case=%s width=%u height=%u hdrBytes=%zu sampleCount=%u\n",
                                visualCase->name.data(),
                                extent.width,
                                extent.height,
                                result.size(),
                                sampleCount);
                        }
                        std::puts(
                            "METAL_ORDINARY_CLOUD_FIRST_FRAME_PASS raymarch=true temporal=true historyUsed=false actualResolvedConsumed=true hotAllocation0=true rawAndResolvedTailPreserved=true publicGateClosed=true cloudFrames=1");
                        return;
                    }
                    unsigned drawn = 0u, background = 0u;
                    const tests::cloud::ForegroundFixture foreground{true,
                        pipelineDesc.viewport.x,
                        pipelineDesc.viewport.y,
                        pipelineDesc.viewport.width,
                        pipelineDesc.viewport.height};
                    for (unsigned y = 0u; y < extent.height; ++y) {
                        for (unsigned x = 0u; x < extent.width; ++x) {
                            const auto* texel = result.data() + y * 256u + x * 8u;
                            if (sampled) {
                                const auto nativeY =
                                    semantics::projectionRequiresYFlip(Backend::Metal)
                                    ? extent.height - 1u - y
                                    : y;
                                const auto& expected =
                                    foreground.covers(x, nativeY) ? pixel : cleared;
                                assert(!std::memcmp(texel, expected.data(), 8u));
                            }
                            if (!std::memcmp(texel, pixel.data(), 8u))
                                ++drawn;
                            else {
                                assert(!std::memcmp(texel, cleared.data(), 8u));
                                ++background;
                            }
                        }
                        for (unsigned x = extent.width * 8u; x < 256u; ++x)
                            assert(result[y * 256u + x] == 0x5a);
                    }
                    assert(drawn && background && drawn + background == sampleCount);
                    assert(nativeStats->nativeDrawEncoded && nativeStats->nativeBlitCopyEncoded);
                    if (sampled) {
                        assert(depthParent->originalObject()->state() == ResourceState::ShaderRead);
                        assert(outputParent->originalObject()->state() == ResourceState::CopySrc);
                        const auto check = [](const GpuTexture& object, const auto& range) {
                            assert(!object.originalCommittedStateDiagnostic(range));
                            const auto published = object.observeOriginalContent(range);
                            assert(published && !object.validateOriginalContent(*published));
                        };
                        check(*depthObject, depthEnter.textureSubresources);
                        check(*outputObject, outputEnter.textureSubresources);
                        assert(depthObject->validateOriginalContent(*depthContent));
                        assert(outputObject->validateOriginalContent(*outputContent));
                        std::printf(
                            "METAL_ORDINARY_SAMPLED_COMPOSITE_PASS mode=%u sameCommand=true colorAndDepth=true actualCloudComposite=true positionalOracle=true hotAllocation0=true publicGateClosed=true cloudFrames=0\n",
                            mode);
                    }
                    if (compute) {
                        const auto outputNative = rhi_metal::metalRhiNativePayloadFor(
                            *computeBuffers[1]->originalObject());
                        assert(outputNative
                            && outputNative->resource.storageMode
                                == rhi_metal::MetalRhiNativeStorageMode::Shared);
                        std::array<std::uint32_t, 32u> output;
                        assert(rhi_metal::readMetalRhiNativeBuffer(outputNative,
                            output.data(),
                            sizeof(output),
                            0u,
                            sizeof(output),
                            diagnostic));
                        for (std::uint32_t i = 0u; i < output.size(); ++i)
                            assert(output[i] == (i < 18u ? 64u + 128u * i : 0xdeadbeefu));
                        assert(nativeStats->nativeDispatchEncoded);
                        std::printf(
                            "METAL_ORDINARY_COMPUTE_OWNER_PASS mode=%u groups=18 localThreads=64 outputVerified=true tailPreserved=true hotAllocation0=true publicGateClosed=true cloudFrames=0\n",
                            mode);
                    }
                    assert(!textureObject->originalCommittedStateDiagnostic(
                        enter.textureSubresources));
                    const auto published =
                        textureObject->observeOriginalContent(enter.textureSubresources);
                    assert(published && !textureObject->validateOriginalContent(*published));
                    assert(textureObject->validateOriginalContent(*content));
                    std::printf(
                        "METAL_ORDINARY_OWNER_CLEAR_COPY_PASS mode=%u texels=16 paddingPreserved=true shaderAndClearDistinct=true hotAllocation0=true originalJournal=true publicGateClosed=true cloudFrames=0\n",
                        mode);
                    return;
                }
            }
        }
    }
    encoder.reset();
    native->abandonUnsubmitted();
    requireRestored();
    if (controlled)
        requireCloud(sequence->history.cancelPendingFrame());
    rhi_metal::MetalRhiNativeQueueStats stats;
    assert(!native->submit(stats, completion, {}, 0u, {}, 0u, diagnostic));
    std::printf(
        "METAL_ORDINARY_OWNER_REJECTION_PASS mode=%u rollback=true originalContentPreserved=true publicGateClosed=true cloudFrames=0\n",
        mode);
}

void ordinaryQueueAcceptance(const std::filesystem::path& directory)
{
    static_assert(!std::is_default_constructible_v<rhi_metal::MetalOriginalQueueAcceptance>);
    static_assert(!std::is_copy_constructible_v<rhi_metal::MetalOriginalQueueAcceptance>);
    auto d = device(true, true, 1u, 2u);
    OrdinaryQueueSequence sequence;
    sequence.device = d.get();
    ordinaryOwnerClearCopy(directory, 0u, false, false, false, nullptr, &sequence);
    const auto first = sequence.previous;
    ordinaryOwnerClearCopy(directory, 1u, false, false, false, nullptr, &sequence);
    assert(sequence.previous == first && !first->diagnostic());
    ordinaryOwnerClearCopy(directory, 28u, false, false, false, nullptr, &sequence);
    assert(sequence.previous == first && !first->diagnostic());
    ordinaryOwnerClearCopy(directory, 30u, false, false, false, nullptr, &sequence);
    assert(sequence.previous == first && !first->diagnostic());
    ordinaryOwnerClearCopy(directory, 29u, false, false, false, nullptr, &sequence);
    ordinaryOwnerClearCopy(directory, 12u, false, false, false, nullptr, &sequence);
    assert(!first->precedes(*sequence.previous));
    // The current actual native result-loss source admits one original device
    // subscription. Retire it before another genuine device subscribes; no
    // fake registry or bypass of that independent profile is permitted.
    d.reset();
    assert(first->diagnostic() && sequence.previous->diagnostic());
    std::shared_ptr<const rhi_metal::MetalOriginalQueueAcceptance> foreignReceipt;
    {
        auto otherDevice = device(true, true, 1u, 2u);
        OrdinaryQueueSequence other;
        other.device = otherDevice.get();
        ordinaryOwnerClearCopy(directory, 0u, false, false, false, nullptr, &other);
        foreignReceipt = other.previous;
        const auto* id = first->precedes(*foreignReceipt);
        assert(id);
    }
    assert(foreignReceipt->diagnostic());
    std::puts(
        "METAL_ORIGINAL_QUEUE_ACCEPTANCE_PASS ordered=true originalProgram=true foreignDeviceRejected=true replacementCompletionRejected=true cancellation=true destroyedCommandPending=true hotAllocation0=true pendingReadAuthority=false");
}

void ordinaryCloudHistory(const std::filesystem::path& directory, unsigned mode)
{
    auto d = device(true, true, 13u, 8u);
    OrdinaryCloudSequence sequence;
    sequence.device = d.get();
    ordinaryOwnerClearCopy(directory, 0u, false, true, true, &sequence);
    assert(sequence.submitted == 1u && sequence.previous);
    const auto first = sequence.previous;
    const auto firstParameters = sequence.frame.parameters;
    if (mode == 1u || mode == 4u) {
        ordinaryOwnerClearCopy(directory, mode, false, true, true, &sequence);
        assert(sequence.submitted == 1u && sequence.previous == first);
        assert(!std::memcmp(&sequence.frame.parameters, &firstParameters, sizeof(firstParameters)));
        assert(sequence.oracle.parameters.jitter.z == firstParameters.jitter.z);
    }
    ordinaryOwnerClearCopy(directory, mode == 12u ? 12u : 0u, false, true, true, &sequence);
    assert(sequence.submitted == 2u && sequence.previous != first && sequence.frame.historyUsed);
    sequence.previous.reset();
    std::printf(
        "METAL_ORDINARY_CLOUD_HISTORY_SEQUENCE_PASS mode=%u frames=2 cancelledDoesNotAdvance=true cpuHistoryNeverUploaded=true publicGateClosed=true\n",
        mode);
}

void ordinaryCloudSlots(const std::filesystem::path& directory, unsigned slotCount)
{
    assert(slotCount == 2u || slotCount == 3u);
    auto d = device(true, true, 13u, 8u);
    OrdinaryCloudSequence sequence;
    sequence.device = d.get();
    sequence.slotCount = slotCount;
    ordinaryOwnerClearCopy(directory, 0u, false, true, true, &sequence);
    assert(sequence.submitted == 1u && sequence.previous == sequence.slots[0].resolved);
    const auto first = sequence.previous;
    const auto parameters = sequence.frame.parameters;
    const auto firstBytes = sequence.previousBytes;
    ordinaryOwnerClearCopy(directory, slotCount == 2u ? 1u : 4u, false, true, true, &sequence);
    assert(sequence.submitted == 1u && sequence.previous == first);
    assert(sequence.history.frameReady() && sequence.history.idle());
    assert(!std::memcmp(&sequence.frame.parameters, &parameters, sizeof(parameters)));
    assert(!std::memcmp(sequence.previousBytes.data(), firstBytes.data(), sizeof(firstBytes)));
    ordinaryOwnerClearCopy(directory, 12u, false, true, true, &sequence);
    assert(sequence.submitted == 2u && sequence.previous == sequence.slots[1].resolved);
    ordinaryOwnerClearCopy(directory, 0u, false, true, true, &sequence);
    assert(sequence.submitted == 3u && sequence.previous == first && sequence.frame.historyUsed);
    assert(sequence.history.releaseIdle().success);
    std::printf(
        "METAL_CLOUD_SLOT_COMPLETION_SEQUENCE_PASS slots=%u frames=3 cancellation=true pendingReaderProtected=true actualFirstSlotReuse=true cpuHistoryNeverUploaded=true publicGateClosed=true\n",
        slotCount);
}

void ordinaryCloudResize(const std::filesystem::path& directory, unsigned slotCount)
{
    assert(slotCount == 2u || slotCount == 3u);
    auto d = device(true, true, 13u, 8u);
    OrdinaryCloudSequence sequence;
    sequence.device = d.get();
    sequence.slotCount = slotCount;
    ordinaryOwnerClearCopy(directory, 0u, false, true, true, &sequence);
    assert(sequence.submitted == 1u && !sequence.frame.historyUsed);
    const auto firstPhase = sequence.frame.parameters.jitter.z;
    const auto domain = rhi_metal::metalRhiOriginalSubmissionDomain(*d);
    assert(domain && sequence.firstWriterFence->isSignaled() && sequence.history.idle());
    sequence.retiredToken = sequence.history.token();
    std::array<std::weak_ptr<const rhi::OriginalCommandBufferLease>, 6u> retired;
    for (unsigned slot = 0u; slot < slotCount; ++slot) {
        retired[slot * 2u] = sequence.slots[slot].raw;
        retired[slot * 2u + 1u] = sequence.slots[slot].resolved;
        assert(sequence.slots[slot].raw->originalObject()->desc().sizeBytes
            == 17u * sizeof(renderer::cloud::CloudGpuSample));
    }
    assert(sequence.history.releaseIdle().success);
    sequence.previous.reset();
    sequence.firstWriterFence.reset();
    sequence.slots = {};
    sequence.slotObjects = {};
    for (auto& owner : retired) {
        assert(owner.expired());
        owner.reset();
    }
    assert(!sequence.sourceQuota->liveBytes() && !sequence.historyQuota->liveBytes());
    const auto* stale = sequence.history.validateSubmission(sequence.retiredToken);
    assert(stale && !std::strcmp(stale, "CLOUD-STALE-SUBMISSION"));
    sequence.frame = {};
    sequence.oracle = {};
    sequence.previousBytes = {};
    sequence.submitted = 0u;
    sequence.historyCreated = false;
    sequence.extent = {3u, 2u};
    ordinaryOwnerClearCopy(directory, 0u, false, true, true, &sequence);
    assert(sequence.submitted == 1u && !sequence.frame.historyUsed);
    assert(sequence.frame.parameters.jitter.z == firstPhase);
    assert(sequence.frame.parameters.dimensions[2] == 3u
        && sequence.frame.parameters.dimensions[3] == 2u);
    for (unsigned slot = 0u; slot < slotCount; ++slot)
        for (auto* object : sequence.slotObjects[slot])
            assert(object->desc().sizeBytes == 7u * sizeof(renderer::cloud::CloudGpuSample));
    assert(sequence.previous == sequence.slots[0].resolved);
    ordinaryOwnerClearCopy(directory, 0u, false, true, true, &sequence);
    assert(sequence.submitted == 2u && sequence.frame.historyUsed);
    assert(sequence.previous == sequence.slots[1].resolved);
    assert(core::SharedOwnerProjection::sameOwner(
        domain, rhi_metal::metalRhiOriginalSubmissionDomain(*d)));
    assert(sequence.history.releaseIdle().success);
    sequence.previous.reset();
    sequence.firstWriterFence.reset();
    sequence.slots = {};
    assert(!sequence.sourceQuota->liveBytes() && !sequence.historyQuota->liveBytes());
    std::printf(
        "METAL_CLOUD_RESIZE_RECREATE_PASS slots=%u frames=3 oldExtent=4x4 newExtent=3x2 sameDeviceAndDomain=true oldLeaseOwnersExpired=true sourceAndHistoryQuotaDrained=true staleTokenRefused=true newHistoryReset=true newGpuHistoryConsumed=true cpuHistoryNeverUploaded=true publicGateClosed=true\n",
        slotCount);
}

} // namespace

int main(int argc, char** argv)
{
    @autoreleasepool {
        if (argc == 3 && !std::strcmp(argv[1], "--ordinary-owner-completion")) {
            static_assert(
                !std::is_default_constructible_v<rhi_metal::MetalOriginalSubmissionDomain>);
            for (const unsigned mode : {0u, 12u, 31u, 32u, 33u, 34u, 35u, 36u})
                ordinaryOwnerClearCopy(argv[2], mode);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--ordinary-queue-acceptance")) {
            ordinaryQueueAcceptance(argv[2]);
            return 0;
        }
        if (argc == 4 && !std::strcmp(argv[1], "--ordinary-cloud-slots")) {
            unsigned slots = 0u;
            assert(std::sscanf(argv[3], "%u", &slots) == 1);
            ordinaryCloudSlots(argv[2], slots);
            return 0;
        }
        if (argc == 4 && !std::strcmp(argv[1], "--ordinary-cloud-resize")) {
            unsigned slots = 0u;
            assert(std::sscanf(argv[3], "%u", &slots) == 1);
            ordinaryCloudResize(argv[2], slots);
            return 0;
        }
        if ((argc == 3 || argc == 4) && !std::strcmp(argv[1], "--ordinary-cloud-history")) {
            unsigned mode = 0u;
            if (argc == 4) {
                assert(std::sscanf(argv[3], "%u", &mode) == 1);
                assert(mode == 0u || mode == 1u || mode == 4u || mode == 12u);
            }
            ordinaryCloudHistory(argv[2], mode);
            return 0;
        }
        if (argc == 5 && !std::strcmp(argv[1], "--ordinary-cloud-visual")) {
            const auto* selected = tests::cloud::ReferenceCases::find(argv[3]);
            assert(selected);
            ordinaryOwnerClearCopy(
                argv[2], 0u, false, true, true, nullptr, nullptr, selected, argv[4]);
            return 0;
        }
        if ((argc == 3 || argc == 4) && !std::strcmp(argv[1], "--ordinary-owner-cloud")) {
            unsigned mode = 0u;
            if (argc == 4) {
                assert(std::sscanf(argv[3], "%u", &mode) == 1);
                assert(mode == 1u || mode == 4u || mode == 6u || mode == 12u || mode == 24u
                    || mode == 25u || mode == 26u || mode == 27u);
            }
            ordinaryOwnerClearCopy(argv[2], mode, false, true, true);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--ordinary-owner-sampled")) {
            for (const auto mode :
                {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 20u, 21u, 22u}) {
                ordinaryOwnerClearCopy(argv[2], mode, false, true);
                std::fflush(stdout);
            }
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--ordinary-owner-compute")) {
            for (const unsigned mode : {0u, 1u, 4u, 6u, 12u, 14u, 15u, 16u, 17u, 18u, 19u})
                ordinaryOwnerClearCopy(argv[2], mode, true);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--ordinary-owner")) {
            for (unsigned mode = 0u; mode < 14u; ++mode)
                ordinaryOwnerClearCopy(argv[2], mode);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--ordinary-buffer-bindings")) {
            ordinaryNativeMemory(argv[2], true);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--ordinary-native-memory")) {
            ordinaryNativeMemory(argv[2]);
            return 0;
        }
        if (argc == 4 && !std::strcmp(argv[1], "--original-graphics-acceptance")) {
            originalGraphicsBufferLimits();
            for (unsigned mode = 0u; mode < 11u; ++mode) {
                originalGraphicsCreation(argv[2], mode, true);
                originalGraphicsCreation(argv[3], mode, true, true);
            }
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--original-graphics-creation")) {
            for (unsigned mode = 0u; mode < 9u; ++mode)
                originalGraphicsCreation(argv[2], mode);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--original-compute-acceptance")) {
            originalComputeBufferLimits();
            for (unsigned mode = 0u; mode < 11u; ++mode)
                originalComputeCreation(argv[2], mode, true);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--original-compute-creation")) {
            for (unsigned mode = 0u; mode < 8u; ++mode)
                originalComputeCreation(argv[2], mode);
            return 0;
        }
        if (argc == 3 && !std::strcmp(argv[1], "--reject-probe-pin")) {
            rejectProbePin(argv[2]);
            return 0;
        }
        if (argc == 2 && !std::strcmp(argv[1], "--l2-preparation")) {
            l2Preparation({3u, 2u, 3u});
            l2Preparation({160u, 90u, 1u});
            for (unsigned mode = 0u; mode < 7u; ++mode)
                l2Delayed(mode);
            std::puts(
                "L2_DISPATCH_PREPARATION_PASS actual L2 entry and original native completion; no recording or cloud promotion");
            return 0;
        }
        if (argc == 2 && !std::strcmp(argv[1], "--cloud-admission")) {
            cloudAdmission({3u, 2u, 3u});
            cloudAdmission({160u, 90u, 1u});
            return 0;
        }
        if (argc != 1)
            return 2;
        auto cancellationDevice = device();
        test::MetalCommandCancellationFixture::run(*cancellationDevice);
        cancellationDevice.reset();
        successful({3u, 2u, 3u});
        const auto allocation720p = successful({160u, 90u, 1u});
        negatives(allocation720p);
        for (unsigned mode = 0u; mode < 5u; ++mode)
            pending(mode);
        for (unsigned mode = 0u; mode < 11u; ++mode)
            domainRevocation(mode);
    }
    std::puts(
        "METAL_DISPATCH_SESSION_PASS native completion ownership and L2 finite domain; no recording, generic capability or cloud execution promotion");
}
