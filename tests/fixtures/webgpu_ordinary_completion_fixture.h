#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_ordinary_completion_retention.h"

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace arp::test {

class WebGpuOrdinaryCompletionFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        static_assert(!std::is_copy_constructible_v<WebGpuOrdinaryCompletionGraph>);
        static_assert(!std::is_move_constructible_v<WebGpuOrdinaryCompletionGraph>);
        using Signal = WebGpuNativeCompletionState;
        WebGpuOrdinaryCompletionSignalBudget signalBudget;
        assert(std::string_view(Signal::validateBudget(signalBudget))
            == "RHI-WEBGPU-ORDINARY-SIGNAL-VERSION");
        signalBudget.contractVersion = 1u;
        assert(std::string_view(Signal::validateBudget(signalBudget))
            == "RHI-WEBGPU-ORDINARY-SIGNAL-OPT-IN");
        signalBudget.prototypeOptIn = true;
        assert(std::string_view(Signal::validateBudget(signalBudget))
            == "RHI-WEBGPU-ORDINARY-SIGNAL-BUDGET");
        signalBudget = {1u, true, 1u, metadataQuotaBytes};
        const char* signalDiagnostic = nullptr;
        assert(!Signal::prepareOrdinarySignal(signalBudget, {}, signalDiagnostic)
            && std::string_view(signalDiagnostic) == "RHI-WEBGPU-ORDINARY-SIGNAL-QUOTA");
        auto signalQuota = core::makeShared<core::CpuMemoryQuota>(metadataQuotaBytes);
        auto signalFactory = [&] {
            return Signal::prepareOrdinarySignal(signalBudget, signalQuota, signalDiagnostic);
        };
        CpuQuotaFactoryFixture signalAdmission(signalQuota);
        const auto signalPeak = signalAdmission.measure(signalFactory);
        signalAdmission.verify(signalPeak, signalFactory);
        {
            core::CpuMemoryQuotaLease pressure(signalQuota, signalQuota->limitBytes());
            assert(pressure.charged() && !signalFactory()
                && std::string_view(signalDiagnostic) == "RHI-WEBGPU-ORDINARY-SIGNAL-ALLOCATION");
        }
        auto signal = signalFactory();
        assert(signal && signal->hasQuotaBoundSharedOwner(signal));
        assert(signal->ordinarySignalOwnerMatches(signal));
        auto signalAlias = std::shared_ptr<Signal>(signalQuota, signal.get());
        assert(!signal->hasQuotaBoundSharedOwner(signalAlias));
        signalAlias.reset();
        std::shared_ptr<Signal> unissued;
        {
            core::CpuMemoryQuotaScope scope(signalQuota);
            unissued = Signal::shareOriginal(core::makeUnique<Signal>());
        }
        assert(unissued->hasQuotaBoundSharedOwner(unissued)
            && !unissued->ordinarySignalOwnerMatches(unissued));
        unissued.reset();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto ticket = signal->reserveSignal();
        assert(ticket && !signal->reserveSignal());
        assert(signal->completeSignal(ticket) && signal->isComplete());
        signal->reset();
        assert(!signal->isComplete() && !signal->completeSignal(ticket));
        assert(!signal->canReserveSignal() && signal->releaseSignal(ticket)
            && signal->canReserveSignal());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        signal.reset();
        assert(!signalQuota->liveBytes());
        WebGpuOrdinaryCompletionBudget budget;
        assert(std::string_view(WebGpuOrdinaryCompletionOwner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-COMPLETION-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(WebGpuOrdinaryCompletionOwner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-COMPLETION-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(WebGpuOrdinaryCompletionOwner::validateBudget(budget))
            == "RHI-WEBGPU-ORDINARY-COMPLETION-BUDGET");
        budget.maxPendingRecords = 2u;
        assert(WebGpuOrdinaryCompletionOwner::validateBudget(budget));
        budget.maxCpuMetadataBytes = metadataQuotaBytes;
        assert(!WebGpuOrdinaryCompletionOwner::validateBudget(budget));
        const char* diagnostic = nullptr;
        assert(!prepareWebGpuOrdinaryCompletionOwner(budget, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-QUOTA");
        auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(metadataQuotaBytes - 1u);
        assert(!prepareWebGpuOrdinaryCompletionOwner(budget, wrongQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-QUOTA");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataQuotaBytes);
        assert(!prepareWebGpuOrdinaryCompletionOwner(budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-NATIVE-UNAVAILABLE");
        assert(!quota->liveBytes());
#endif
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class LossProbe final {
    public:
        bool retiredAfterActualLoss()
        {
            const bool expired = owner.expired() && exactOwner.expired() && command.expired()
                && completion.expired();
            owner.reset();
            exactOwner.reset();
            command.reset();
            completion.reset();
            return expired && !ownerQuota->liveBytes() && !exactOwnerQuota->liveBytes()
                && !commandQuota->liveBytes() && !signalQuota->liveBytes();
        }

        std::weak_ptr<rhi_webgpu::WebGpuOrdinaryCompletionOwner> owner, exactOwner;
        std::weak_ptr<rhi_webgpu::WebGpuNativeCommandPayload> command;
        std::weak_ptr<rhi_webgpu::WebGpuNativeCompletionState> completion;
        std::shared_ptr<core::CpuMemoryQuota> ownerQuota, exactOwnerQuota, commandQuota,
            signalQuota;
    };

    static LossProbe verifyActualPreparation()
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataQuotaBytes);
        WebGpuOrdinaryCompletionBudget budget{1u, true, 2u, metadataQuotaBytes};
        auto exhaustedQuota = core::makeShared<core::CpuMemoryQuota>(metadataQuotaBytes);
        {
            core::CpuMemoryQuotaLease pressure(exhaustedQuota, exhaustedQuota->limitBytes());
            assert(pressure.charged()
                && !prepareWebGpuOrdinaryCompletionOwner(budget, exhaustedQuota, diagnostic));
            assert(std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ALLOCATION");
        }
        assert(!quota->liveBytes());
        auto owner = prepareWebGpuOrdinaryCompletionOwner(budget, quota, diagnostic);
        assert(owner && owner->retained.capacity() == 2u && owner->retained.accepting());
        const auto ownerBytes = quota->liveBytes();
        assert(ownerBytes);
        // Use the measured live original owner/observer charges, rather than
        // an estimated sizeof total or a count-derived byte budget.
        auto less = core::makeShared<core::CpuMemoryQuota>(ownerBytes - 1u);
        auto exact = core::makeShared<core::CpuMemoryQuota>(ownerBytes);
        budget.maxCpuMetadataBytes = less->limitBytes();
        assert(!prepareWebGpuOrdinaryCompletionOwner(budget, less, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ALLOCATION"
            && !less->liveBytes());
        budget.maxCpuMetadataBytes = exact->limitBytes();
        auto exactOwner = prepareWebGpuOrdinaryCompletionOwner(budget, exact, diagnostic);
        assert(exactOwner && exact->liveBytes() == ownerBytes);
        budget.maxCpuMetadataBytes = metadataQuotaBytes;
        budget.maxPendingRecords = UINT32_MAX;
        auto hugeQuota = core::makeShared<core::CpuMemoryQuota>(metadataQuotaBytes);
        assert(!prepareWebGpuOrdinaryCompletionOwner(budget, hugeQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ALLOCATION"
            && !hugeQuota->liveBytes());

        auto commandQuota = core::makeShared<core::CpuMemoryQuota>(originalCommandQuotaBytes);
        core::pmr::String message;
        message.reserve(256u);
        const auto limits = webGpuNativeDeviceLimits();
        const auto makeCommand = [&] {
            core::CpuMemoryQuotaScope scope(commandQuota);
            return createWebGpuNativeCommandPayload("cold ordinary completion fixture",
                limits.uniformBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.uniformBufferOffsetAlignment,
                1u,
                {},
                false,
                message);
        };
        auto command = makeCommand();
        auto other = makeCommand();
        auto extra = makeCommand();
        auto signalQuota = core::makeShared<core::CpuMemoryQuota>(metadataQuotaBytes);
        auto completion = WebGpuNativeCompletionState::prepareOrdinarySignal(
            {1u, true, 1u, signalQuota->limitBytes()}, signalQuota, diagnostic);
        assert(
            command && other && extra && command->completionDeviceMatches(owner->nativeDevice()));
        assert(!command->completionDeviceMatches(nullptr));
        assert(!WebGpuOrdinaryCompletionReservation::prepare(owner, {}, completion, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ORIGINAL-COMMAND");
        const auto factory = [&] {
            return WebGpuOrdinaryCompletionReservation::prepare(
                owner, command, completion, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto reservationPeak = admission.measure(factory);
        admission.verify(reservationPeak, factory);
        assert(!diagnostic
            || std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ALLOCATION");
        assert(!owner->retained.pending() && quota->liveBytes() == ownerBytes
            && !completion->isComplete());
        auto record = factory();
        assert(record && !diagnostic && owner->retained.pending() == 1u);
        auto* original = owner->retained.get(record->ticket());
        assert(original && original->command() == command && original->completion() == completion
            && original->matches(owner->nativeDevice()));
        assert(!original->reserveSignal() && original->matches(owner->nativeDevice()));
        auto aliasHolder = core::makeShared<std::uint32_t>(0u);
        std::shared_ptr<WebGpuOrdinaryCompletionOwner> ownerAlias(aliasHolder, owner.get());
        assert(!WebGpuOrdinaryCompletionReservation::prepare(ownerAlias, command, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED");
        ownerAlias.reset();
        std::shared_ptr<WebGpuNativeCommandPayload> alias(aliasHolder, command.get());
        std::shared_ptr<WebGpuNativeCompletionState> completionAlias(aliasHolder, completion.get());
        assert(!WebGpuOrdinaryCompletionReservation::prepare(owner, alias, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ORIGINAL-COMMAND");
        assert(
            !WebGpuOrdinaryCompletionReservation::prepare(owner, extra, completionAlias, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ORIGINAL-COMMAND");
        assert(
            !WebGpuOrdinaryCompletionReservation::prepare(exactOwner, other, completion, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-SIGNAL-UNAVAILABLE");
        auto legacySignal = core::makeShared<WebGpuNativeCompletionState>();
        assert(!WebGpuOrdinaryCompletionReservation::prepare(
                   exactOwner, other, legacySignal, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-ORIGINAL-COMMAND");
        legacySignal.reset();
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(pressure.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-DUPLICATE");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        auto second = WebGpuOrdinaryCompletionReservation::prepare(owner, other, {}, diagnostic);
        assert(second && owner->retained.pending() == 2u);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!WebGpuOrdinaryCompletionReservation::prepare(owner, extra, {}, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-CAPACITY");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        const auto cancelled = second->ticket();
        std::weak_ptr<WebGpuNativeCommandPayload> cancelledCommand = other;
        other.reset();
        assert(!cancelledCommand.expired());
        second.reset();
        assert(owner->retained.pending() == 1u && !owner->retained.get(cancelled));
        assert(cancelledCommand.expired());
        cancelledCommand.reset();
        assert(!extra->finish(message) && message == webGpuGenericRecordingUsageDiagnostic);
        assert(!extra->finished());

        // Inject an unproven ownership result only. No native command was
        // submitted and this does not prove native Error or Success completion.
        assert(owner->retained.submit(record->ticket())
            && owner->retained.resolve(record->ticket(), false));
        assert(!owner->retained.accepting() && owner->retained.pending() == 1u
            && !completion->isComplete());
        const auto closedAllocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED");
        assert(core::defaultCpuAllocator().stats().allocationCount == closedAllocations);
        LossProbe probe{
            owner, exactOwner, command, completion, quota, exact, commandQuota, signalQuota};
        record.reset();
        alias.reset();
        completionAlias.reset();
        command.reset();
        completion.reset();
        other.reset();
        extra.reset();
        assert(
            !probe.command.expired() && !probe.completion.expired() && commandQuota->liveBytes());
        std::printf("BINDLESS_RHI_ORDINARY_COMPLETION_PREPARATION_OK %llu %llu\n",
            static_cast<unsigned long long>(ownerBytes),
            static_cast<unsigned long long>(reservationPeak));
        return probe;
    }
#endif
private:
    // Declared fixture instrumentation ceilings. Production admission uses
    // caller-owned exact bytes/count; the fixture additionally tests measured
    // exact/one-less and exhausted reservations on this actual Wasm ABI.
    static constexpr std::uint64_t metadataQuotaBytes = 65536u;
    static constexpr std::uint64_t originalCommandQuotaBytes = 65536u;
};

} // namespace arp::test
