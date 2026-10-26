#include "rhi/backends/metal/metal_dispatch_validation_session.h"

#include "core/hash/sha256.h"
#include "core/memory/partitioned_budget_ledger.h"
#include "core/runtime/completion_retention_pool.h"
#include "metal_dispatch_probe_pin.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_pipelines.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_device_native_loss.h"
#include "rhi/backends/metal/metal_original_pipeline.h"

#import <Metal/Metal.h>
#include <chrono>
#include <cstring>
#include <thread>

namespace arp::rhi_metal {
namespace {
using Clock = std::chrono::steady_clock;
using Contract = rhi::ComputeDispatchValidationContract;

struct NativeReservation {
    std::uint64_t bytes = 0u;
};

using NativeLedger = core::PartitionedBudgetLedger<NativeReservation, 1u, 1u>;

const char* quotaDiagnostic(const MetalDispatchValidationSession::Budget& b,
    const MetalDispatchValidationSession::Quotas& q) noexcept
{
    return rhi::ComputeDispatchValidationSessionContract::inspectQuotas(b, q);
}

bool pinnedProbe(const shader::SingleEntryShaderArtifact& artifact)
{
    using Role = shader::ShaderArtifactRole;
    const char* hashes[]{dispatch_probe_pin::source,
        dispatch_probe_pin::spirv,
        dispatch_probe_pin::metal,
        dispatch_probe_pin::wgsl,
        dispatch_probe_pin::reflection};
    for (unsigned i = 0u; i < static_cast<unsigned>(Role::Count); ++i)
        if (!core::Sha256::matchesHexDigest(
                shader::shaderArtifactText(artifact.bytes(static_cast<Role>(i))), hashes[i]))
            return false;
    return core::Sha256::matchesHexDigest(
        shader::shaderArtifactText(artifact.publicationBytes()), dispatch_probe_pin::publication);
}

struct Probe {
    core::UniquePtr<rhi::PipelineLayout> layout;
    core::UniquePtr<rhi::ComputePipeline> pipeline;
    std::shared_ptr<MetalRhiNativePipelinePayload> native;
    std::uint32_t uniformSlot = UINT32_MAX, outputSlot = UINT32_MAX;
};

// Member order releases every native owner before the independent reservation.
struct ResultOwners {
    std::shared_ptr<const rhi::ComputeDispatchValidationReservation> deviceSlot;
    NativeLedger::Lease reservation;
    std::shared_ptr<Probe> probe;
    std::shared_ptr<void> buffer;
    id<MTLCommandQueue> queue = nil;
    std::shared_ptr<MetalRhiNativeSharedEventPayload> gate;
    std::uint64_t actualBytes = 0u;

    ~ResultOwners() { [queue release]; }
};

struct PendingRow {
    std::shared_ptr<ResultOwners> owners;
    std::atomic<unsigned> terminal{0u}; // Only a real native completed handler writes this.
    std::uint64_t dispatches = 0u;
};

using PendingPool = core::runtime::CompletionRetentionPool<PendingRow>;
} // namespace

struct MetalDispatchValidationSession::State {
    State(const rhi::ComputeDispatchValidationRequest& request,
        Budget budget,
        Quotas quotas,
        rhi::OriginalDeviceLifetime::Ticket lifetime,
        rhi::OriginalDeviceNativeLossObservation loss,
        Clock::time_point started)
        : progress(request), budget(budget), quotas(std::move(quotas)),
          lifetime(std::move(lifetime)), loss(std::move(loss)), started(started),
          pending(core::makeShared<PendingPool>(1u))
    {}

    std::uint64_t elapsed() const noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
    }

    const char* threadDiagnostic() const noexcept
    {
        return owner == std::this_thread::get_id() ? nullptr : "RHI-DISPATCH-SESSION-THREAD";
    }

    void invalidate(const char* id) noexcept
    {
        progress.invalidate(id);
        latchProofFailure(progress.diagnostic());
        if (ticket)
            pending->resolve(ticket, false); // Close admission; keep the original owners.
    }

    const char* latchProofFailure(const char* id) const noexcept
    {
        const char* expected = nullptr;
        proofFailure.compare_exchange_strong(
            expected, id, std::memory_order_acq_rel, std::memory_order_acquire);
        return expected ? expected : id;
    }

    const char* retainedDependencyDiagnostic() const noexcept
    {
        if (const auto* id = proofFailure.load(std::memory_order_acquire))
            return id;
        const auto* id = lifetime.diagnostic();
        if (!id)
            id = loss.diagnostic();
        if (!id && lossSource->lost())
            id = "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST";
        if (!id)
            id = quotaDiagnostic(budget, quotas);
        return id ? latchProofFailure(id) : nullptr;
    }

    const char* currentDiagnostic() const noexcept
    {
        if (const auto* id = threadDiagnostic())
            return id;
        if (const auto* id = retainedDependencyDiagnostic())
            return id;
        const auto* id = progress.diagnostic();
        return id ? latchProofFailure(id) : nullptr;
    }

    // Native completion consumes the published successful proof, never the
    // owner-thread polling cursor, pending ticket or mutable result storage.
    const char* retainedProofDiagnostic() const noexcept
    {
        if (!proofVerified.load(std::memory_order_acquire))
            return "RHI-DISPATCH-DOMAIN-INCOMPLETE";
        return retainedDependencyDiagnostic();
    }

    const char* proofDiagnostic() const noexcept
    {
        if (const auto* id = currentDiagnostic())
            return id;
        return progress.verified() && !ticket && !result && lowering.coldValidationEligible
            ? nullptr
            : "RHI-DISPATCH-DOMAIN-INCOMPLETE";
    }

    rhi::ComputeDispatchValidationProgress progress;
    const Budget budget;
    const Quotas quotas;
    const rhi::OriginalDeviceLifetime::Ticket lifetime;
    const rhi::OriginalDeviceNativeLossObservation loss;
    const Clock::time_point started;
    const std::thread::id owner = std::this_thread::get_id();
    std::shared_ptr<PendingPool> pending;
    PendingPool::Ticket ticket;
    std::optional<NativeLedger> ledger;
    std::shared_ptr<ResultOwners> result;
    std::shared_ptr<MetalOriginalDeviceNativeResultLossSource> lossSource;
    std::uint64_t actualBytes = 0u;
    rhi::semantics::ComputeDispatchValidationLowering::Decision lowering;
    std::atomic<bool> proofVerified{false};
    mutable std::atomic<const char*> proofFailure{nullptr};
};

MetalDispatchValidationSession::~MetalDispatchValidationSession() = default;

std::shared_ptr<rhi::OriginalComputeDispatchValidation> MetalDispatchValidationSession::prepareL2(
    rhi::GpuDevice& device,
    const rhi::ComputeDispatchValidationRequest& request,
    const Budget& budget,
    Quotas quotas,
    const char*& diagnostic)
{
    auto session = prepare(device, request, budget, std::move(quotas), diagnostic);
    return session ? wrapL2(std::move(session), diagnostic) : nullptr;
}

std::shared_ptr<rhi::OriginalComputeDispatchValidation> MetalDispatchValidationSession::wrapL2(
    std::shared_ptr<MetalDispatchValidationSession> session, const char*& diagnostic)
{
    using Facade = rhi::OriginalComputeDispatchValidation;
    diagnostic = nullptr;
    if (!session || !session->sharedOwnerMatches(session)) {
        diagnostic = "RHI-DISPATCH-PREPARATION-OWNER";
        return {};
    }
    const auto& s = *session->state_;
    if ((diagnostic = s.currentDiagnostic()))
        return {};
    const Facade::Operations operations{
        [](void* owner) {
            return static_cast<MetalDispatchValidationSession*>(owner)->advance();
        },
        [](void* owner) noexcept {
            return static_cast<MetalDispatchValidationSession*>(owner)->cancel();
        },
        [](const void* owner) noexcept {
            const auto native =
                static_cast<const MetalDispatchValidationSession*>(owner)->observe();
            return Facade::Observation{
                native.diagnostic, native.checkedRows, native.pending, native.verified};
        },
        [](const void* owner,
            rhi::GpuDevice& device,
            const rhi::ComputeDispatchValidationDomainBudget& budget,
            std::shared_ptr<core::CpuMemoryQuota> quota,
            const char*& id) {
            return static_cast<const MetalDispatchValidationSession*>(owner)->issueDomain(
                device, budget, std::move(quota), id);
        }};
    try {
        core::CpuMemoryQuotaScope scope(s.quotas.session);
        // The facade is metadata in the original session budget. Its private
        // payload is the same native session, never another completion graph.
        return core::makeShared<Facade>(
            Facade::ConstructionKey{}, s.lifetime, s.quotas.session, session, operations);
    } catch (const std::bad_alloc&) {
        session->cancel();
        diagnostic = "RHI-DISPATCH-PREPARATION-CPU-ALLOCATION";
        return {};
    }
}

std::shared_ptr<const rhi::ComputeDispatchValidationDomain>
MetalDispatchValidationSession::issueDomain(rhi::GpuDevice& device,
    const rhi::ComputeDispatchValidationDomainBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic) const
{
    const auto& s = *state_;
    diagnostic = s.threadDiagnostic();
    if (diagnostic)
        return {};
    if (!metalRhiIssuingResourceRegistry(device)) {
        diagnostic = "RHI-DISPATCH-SESSION-ORIGINAL-DEVICE";
        return {};
    }
    if ((diagnostic = device.validateOriginalDeviceLifetime(s.lifetime))
        || (diagnostic = device.validateOriginalDeviceNativeLoss(s.loss))
        || (diagnostic = s.proofDiagnostic()))
        return {};
    for (const auto& original :
        {s.quotas.session, s.quotas.artifact, s.quotas.input, s.quotas.pipeline})
        if (core::SharedOwnerProjection::overlapsOwner(original, quota)) {
            diagnostic = "RHI-DISPATCH-DOMAIN-QUOTA-OVERLAP";
            return {};
        }
    return device.issueOriginalComputeDispatchDomain(
        s.progress.request(),
        s.lifetime,
        s.loss,
        budget,
        std::move(quota),
        state_,
        [](const void* proof) noexcept {
            return static_cast<const State*>(proof)->retainedProofDiagnostic();
        },
        diagnostic);
}

std::shared_ptr<MetalDispatchValidationSession> MetalDispatchValidationSession::prepare(
    rhi::GpuDevice& device,
    const rhi::ComputeDispatchValidationRequest& request,
    const Budget& budget,
    Quotas quotas,
    const char*& diagnostic)
{
    return prepareImpl(device, request, budget, std::move(quotas), {}, diagnostic);
}

std::shared_ptr<MetalDispatchValidationSession> MetalDispatchValidationSession::prepareImpl(
    rhi::GpuDevice& device,
    const rhi::ComputeDispatchValidationRequest& request,
    const Budget& budget,
    Quotas quotas,
    std::shared_ptr<MetalRhiNativeSharedEventPayload> gate,
    const char*& diagnostic)
{
    const auto started = Clock::now();
    diagnostic = rhi::ComputeDispatchValidationSessionContract::inspect(request, budget);
    if (diagnostic || (diagnostic = quotaDiagnostic(budget, quotas)))
        return {};
    if (device.backend() != rhi::Backend::Metal || !metalRhiIssuingResourceRegistry(device)) {
        diagnostic = "RHI-DISPATCH-SESSION-ORIGINAL-DEVICE";
        return {};
    }
    const auto lifetime = device.originalDeviceLifetimeTicket();
    if ((diagnostic = device.validateOriginalDeviceLifetime(lifetime)))
        return {};
    const auto loss = device.observeOriginalDeviceNativeLoss(diagnostic);
    if (!loss)
        return {};
    for (const auto& quota : {quotas.session, quotas.artifact, quotas.input, quotas.pipeline})
        if (device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-DISPATCH-SESSION-DEVICE-QUOTA-OVERLAP";
            return {};
        }
    auto slot = device.reserveOriginalComputeDispatchValidation(
        request, budget, quotas.session, diagnostic);
    if (!slot)
        return {};
    @autoreleasepool {
        try {
            core::CpuMemoryQuotaScope scope(quotas.session);
            auto state = core::makeShared<State>(request, budget, quotas, lifetime, *loss, started);
            state->lossSource = sharedMetalRhiOriginalResultLossSource();
            const auto nativeDevice = metalRhiNativeDeviceIdentity();
            if (!state->lossSource || state->lossSource->lost()
                || !core::SharedOwnerProjection::sameOwner(
                    state->lossSource->device(), nativeDevice)) {
                diagnostic = "RHI-DISPATCH-SESSION-NATIVE-DEVICE";
                return {};
            }
            if (gate
                && (!gate->nativeRuntimeBacked || !gate->nativeEvent
                    || !core::SharedOwnerProjection::sameOwner(gate->nativeDevice, nativeDevice))) {
                diagnostic = "RHI-DISPATCH-SESSION-GATE-DEVICE";
                return {};
            }
            core::pmr::String detail{&core::defaultCpuMemoryResource()};
            const auto artifact =
                shader::SingleEntryShaderArtifact::load(ARP_METAL_DISPATCH_VALIDATION_PROBE_ENTRY,
                    {budget.maxArtifactFileBytes,
                        budget.maxArtifactTotalBytes,
                        budget.maxArtifactCpuBytes,
                        2u},
                    quotas.artifact,
                    detail);
            const bool probePinned = artifact && pinnedProbe(*artifact);
            if (!probePinned) {
                diagnostic = "RHI-DISPATCH-VALIDATION-PROBE-CUSTODY";
                return {};
            }
            const auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
                artifact, {1u, budget.maxInputCpuBytes}, quotas.input, detail);
            if (!input) {
                diagnostic = "RHI-DISPATCH-SESSION-INPUT";
                return {};
            }
            auto probe = core::makeShared<Probe>();
            {
                core::CpuMemoryQuotaScope pipelineScope(quotas.pipeline);
                const auto& package = input->package();
                probe->layout = createMetalRhiPipelineLayout(
                    rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection));
                if (!probe->layout) {
                    diagnostic = "RHI-DISPATCH-SESSION-LAYOUT";
                    return {};
                }
                rhi::ComputePipelineDesc desc;
                desc.layout = probe->layout.get();
                desc.shaderPackage = &package;
                desc.computeShader = package.entrypoints.front();
                auto capability =
                    rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
                capability.queriedFromNativeDevice = bool(nativeDevice);
                probe->pipeline = MetalOriginalPipeline::create(desc,
                    input,
                    capability,
                    {1u,
                        true,
                        budget.maxPipelineTargetBytes,
                        budget.maxPipelineCpuBytes,
                        request.budget.maxColdValidationNanoseconds},
                    quotas.pipeline,
                    diagnostic);
            }
            if (!probe->pipeline)
                return {};
            probe->native = metalRhiNativePayloadFor(*probe->pipeline);
            const auto& native = probe->native;
            if (!native || !native->nativeRuntimeBacked || !native->pipelineState
                || !core::SharedOwnerProjection::sameOwner(native->nativeDevice, nativeDevice)
                || native->computeWorkgroupSizeX != 8u || native->computeWorkgroupSizeY != 8u
                || native->computeWorkgroupSizeZ != 1u || native->computeBufferFactsVersion != 1u) {
                diagnostic = "RHI-DISPATCH-SESSION-PIPELINE";
                return {};
            }
            for (const auto& binding : native->metalBindingSpace)
                if (binding.set == 0u && binding.active) {
                    if (binding.binding == 0u)
                        probe->uniformSlot = binding.metalSlot;
                    else if (binding.binding == 1u)
                        probe->outputSlot = binding.metalSlot;
                }
            bool aligned = false;
            for (const auto& fact : native->computeBufferFacts)
                if (fact.slot == probe->outputSlot)
                    aligned = fact.alignment
                        && state->progress.plan().resultSliceBytes % fact.alignment == 0u;
            if (!aligned || probe->uniformSlot == UINT32_MAX || probe->outputSlot == UINT32_MAX
                || probe->uniformSlot == probe->outputSlot) {
                diagnostic = "RHI-DISPATCH-SESSION-BUFFER-ABI";
                return {};
            }
            state->ledger = NativeLedger::prepareCold({budget.maxNativeAllocationBytes},
                {&NativeReservation::bytes},
                1u,
                quotas.session,
                diagnostic);
            if (!state->ledger)
                return {};
            auto result = core::makeShared<ResultOwners>();
            result->deviceSlot = std::move(slot);
            result->reservation =
                state->ledger->reserve({budget.maxNativeAllocationBytes}, 0u, diagnostic);
            if (!result->reservation)
                return {};
            result->probe = std::move(probe);
            result->gate = std::move(gate);
            result->buffer = createMetalRhiTrackedNativeBufferObject(nativeDevice,
                state->progress.plan().nativeResultBytes,
                MetalRhiNativeStorageMode::Shared,
                detail);
            const auto actualDevice = reinterpret_cast<id<MTLDevice>>(nativeDevice.get());
            const auto actualBuffer = reinterpret_cast<id<MTLBuffer>>(result->buffer.get());
            if (!actualBuffer || !actualBuffer.contents || actualBuffer.device != actualDevice
                || actualBuffer.storageMode != MTLStorageModeShared
                || actualBuffer.hazardTrackingMode != MTLHazardTrackingModeTracked
                || actualBuffer.length != state->progress.plan().nativeResultBytes
                || !actualBuffer.allocatedSize
                || actualBuffer.allocatedSize > budget.maxNativeAllocationBytes) {
                diagnostic = "RHI-DISPATCH-SESSION-NATIVE-ALLOCATION";
                return {};
            }
            state->actualBytes = result->actualBytes = actualBuffer.allocatedSize;
            result->queue = [actualDevice newCommandQueue];
            if (!result->queue || result->queue.device != actualDevice) {
                diagnostic = "RHI-DISPATCH-SESSION-QUEUE";
                return {};
            }
            state->lowering = rhi::semantics::ComputeDispatchValidationLowering::decide(
                device.backend(),
                {!lifetime.diagnostic() && !loss->diagnostic(),
                    bool(nativeDevice) && !state->lossSource->lost(),
                    probePinned,
                    bool(result->deviceSlot) && bool(result->reservation)
                        && state->pending->capacity() == budget.maxPendingSubmissions,
                    bool(result->probe->pipeline) && bool(result->buffer) && bool(result->queue)});
            if (!state->lowering.coldValidationEligible || state->lowering.diagnostic) {
                diagnostic = state->lowering.diagnostic;
                return {};
            }
            state->result = std::move(result);
            auto session = core::makeShared<MetalDispatchValidationSession>(
                ConstructionKey{}, std::move(state));
            diagnostic = session->advance();
            return diagnostic ? nullptr : session;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-DISPATCH-SESSION-CPU-ALLOCATION";
            return {};
        }
    }
}

const char* MetalDispatchValidationSession::advance()
{
    auto& s = *state_;
    if (const auto* id = s.threadDiagnostic())
        return id;
    if (!s.lowering.coldValidationEligible)
        return s.lowering.diagnostic;
    if (const auto* id = s.currentDiagnostic())
        s.invalidate(id);
    if (s.progress.observeDeadline(s.elapsed()))
        s.invalidate(s.progress.diagnostic());
    const auto& plan = s.progress.plan();
    if (s.ticket) {
        auto* row = s.pending->get(s.ticket);
        const auto terminal = row->terminal.load(std::memory_order_acquire);
        if (!terminal)
            return s.progress.diagnostic();
        std::uint64_t checks = 0u, groups = 0u;
        if (terminal == 1u && !s.progress.diagnostic()) {
            const auto buffer = reinterpret_cast<id<MTLBuffer>>(row->owners->buffer.get());
            std::uint64_t slice = 0u;
            for (std::uint32_t z = 1u; z <= plan.groups.depth && !s.progress.diagnostic(); ++z)
                for (std::uint32_t y = 1u; y <= plan.groups.height && !s.progress.diagnostic();
                    ++y, ++slice) {
                    const auto* words = reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::byte*>(buffer.contents)
                        + slice * plan.resultSliceBytes);
                    for (std::uint64_t index = 0u; index < plan.resultSliceBytes / 4u; ++index) {
                        const bool inside = index < plan.resultCellsPerSlice
                            && index % plan.groups.width < s.progress.nextRow()
                            && index / plan.groups.width % plan.groups.height < y
                            && index / (std::uint64_t(plan.groups.width) * plan.groups.height) < z;
                        const auto expected =
                            inside ? static_cast<std::uint32_t>(64u + 128u * index) : 0u;
                        if (words[index] != expected) {
                            s.invalidate("RHI-DISPATCH-SESSION-RESULT-MISMATCH");
                            break;
                        }
                        ++checks;
                        groups += inside;
                    }
                    s.progress.observeDeadline(s.elapsed());
                }
        }
        s.progress.completeRow(terminal == 1u, row->dispatches, groups, checks, s.elapsed());
        s.pending->resolve(s.ticket, true); // Actual completed/error callback drained this row.
        s.ticket = {};
    }
    if (s.progress.diagnostic() || s.progress.verified()) {
        s.result.reset();
        if (s.progress.verified())
            s.proofVerified.store(true, std::memory_order_release);
        return s.progress.diagnostic();
    }
    @autoreleasepool {
        try {
            core::CpuMemoryQuotaScope scope(s.quotas.session);
            auto candidate = core::makeUnique<PendingRow>();
            candidate->owners = s.result;
            const auto buffer = reinterpret_cast<id<MTLBuffer>>(s.result->buffer.get());
            std::memset(buffer.contents, 0, plan.nativeResultBytes);
            const auto descriptor = [[MTLCommandBufferDescriptor alloc] init];
            descriptor.retainedReferences = YES;
            const auto command = [s.result->queue commandBufferWithDescriptor:descriptor];
            [descriptor release];
            if (!command) {
                s.invalidate("RHI-DISPATCH-SESSION-COMMAND");
                s.result.reset();
                return s.progress.diagnostic();
            }
            if (s.result->gate)
                [command encodeWaitForEvent:reinterpret_cast<id<MTLSharedEvent>>(
                                                s.result->gate->nativeEvent.get())
                                      value:1u];
            const auto encoder =
                [command computeCommandEncoderWithDispatchType:MTLDispatchTypeSerial];
            if (!encoder) {
                s.invalidate("RHI-DISPATCH-SESSION-ENCODER");
                s.result.reset();
                return s.progress.diagnostic();
            }
            const auto& probe = *s.result->probe;
            const std::uint32_t constants[]{
                plan.groups.width, plan.groups.height, plan.groups.depth, 0u};
            [encoder setComputePipelineState:reinterpret_cast<id<MTLComputePipelineState>>(
                                                 probe.native->pipelineState.get())];
            [encoder setBytes:constants length:sizeof(constants) atIndex:probe.uniformSlot];
            std::uint64_t slice = 0u;
            for (std::uint32_t z = 1u; z <= plan.groups.depth; ++z)
                for (std::uint32_t y = 1u; y <= plan.groups.height; ++y, ++slice) {
                    [encoder setBuffer:buffer
                                offset:slice * plan.resultSliceBytes
                               atIndex:probe.outputSlot];
                    [encoder dispatchThreadgroups:MTLSizeMake(s.progress.nextRow(), y, z)
                            threadsPerThreadgroup:MTLSizeMake(8u, 8u, 1u)];
                    ++candidate->dispatches;
                }
            [encoder endEncoding];
            if (const auto* id = s.progress.beginRow(s.elapsed())) {
                s.invalidate(id);
                s.result.reset();
                return id;
            }
            s.ticket = s.pending->reserve(candidate);
            // Capacity and exact ticket are guaranteed by this serialized protocol;
            // the pool is never exposed to callers or native callbacks.
            if (!s.ticket || !s.pending->submit(s.ticket)) {
                s.invalidate("RHI-DISPATCH-SESSION-RETENTION");
                return s.progress.diagnostic();
            }
            auto* row = s.pending->get(s.ticket);
            const auto pool = s.pending;
            const auto owners = row->owners;
            const auto source = s.lossSource;
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                if (completed.status == MTLCommandBufferStatusError &&
                    [completed.error.domain isEqualToString:MTLCommandBufferErrorDomain]
                    && completed.error.code == MTLCommandBufferErrorDeviceRemoved)
                    source->observeDeviceRemoval(completed.device);
                // Capturing the original pool keeps the exact row/owners/charges
                // alive if timeout, cancellation or facade destruction races work.
                (void)pool;
                (void)owners;
                row->terminal.store(
                    completed.status == MTLCommandBufferStatusCompleted && !completed.error ? 1u
                                                                                            : 2u,
                    std::memory_order_release);
            }];
            [command commit];
            return nullptr;
        } catch (const std::bad_alloc&) {
            s.invalidate("RHI-DISPATCH-SESSION-CPU-ALLOCATION");
            if (!s.ticket)
                s.result.reset();
            return s.progress.diagnostic();
        }
    }
}

const char* MetalDispatchValidationSession::cancel() noexcept
{
    if (const auto* id = state_->threadDiagnostic())
        return id;
    state_->invalidate("RHI-DISPATCH-SESSION-CANCELLED");
    if (!state_->ticket)
        state_->result.reset();
    return state_->progress.diagnostic();
}

MetalDispatchValidationSession::Observation MetalDispatchValidationSession::observe() const noexcept
{
    const auto& s = *state_;
    if (const auto* id = s.threadDiagnostic())
        return {id};
    const auto* diagnostic = s.currentDiagnostic();
    return {diagnostic,
        s.progress.checkedRows(),
        bool(s.ticket),
        !diagnostic && s.progress.verified(),
        s.ledger ? s.ledger->snapshot().total.bytes : 0u,
        s.actualBytes};
}

std::weak_ptr<void> MetalDispatchValidationSession::retainedGraphForTest() const noexcept
{
    return state_->result;
}

bool MetalDispatchValidationSession::corruptCompletedOutputForTest() noexcept
{
    if (state_->threadDiagnostic() || !state_->ticket)
        return false;
    const auto* row = state_->pending->get(state_->ticket);
    if (row->terminal.load(std::memory_order_acquire) != 1u)
        return false;
    const auto buffer = reinterpret_cast<id<MTLBuffer>>(row->owners->buffer.get());
    static_cast<std::uint32_t*>(buffer.contents)[0] ^= 1u;
    return true;
}
} // namespace arp::rhi_metal
