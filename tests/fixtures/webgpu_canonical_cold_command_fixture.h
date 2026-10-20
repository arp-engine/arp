#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_cold_command.h"
#include "rhi/semantics/command_lowering.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_canonical_cold_admission_fixture.h"

namespace arp::test {
class WebGpuCanonicalColdCommandFixture final {
    using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
    using Admission = rhi_webgpu::WebGpuCanonicalColdAdmission;
    using Contract = rhi::OriginalCommandNativeColdCommandContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    static arp::rhi::semantics::OriginalCommandNativeColdCommandLowering::Decision decision(
        rhi::Backend backend)
    {
        return arp::rhi::semantics::OriginalCommandNativeColdCommandLowering::lowering(backend);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Live final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        core::UniquePtr<rhi::GpuDevice> device;
        std::shared_ptr<const Command> command;
    };

    static inline core::UniquePtr<Live> live_;
#endif
public:
    static void verifyContract()
    {
        Command::Budget b;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-NATIVE-COLD-COMMAND-VERSION");
        b.contractVersion = 1u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-NATIVE-COLD-COMMAND-OPT-IN");
        b.prototypeOptIn = true;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-NATIVE-COLD-COMMAND-COUNT");
        b.maxCommandPayloads = 1u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-NATIVE-COLD-COMMAND-BUDGET");
        b = {1u, true, 1u, 51u, 65536u};
        assert(!Contract::validateIntent(b));
        ++b.maxCommandPayloads;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-NATIVE-COLD-COMMAND-COUNT");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = decision(backend);
            assert(!d.recordingAvailable && !d.executionAvailable && d.diagnostic);
            assert(d.nativeConnectionAvailable == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_CANONICAL_COLD_COMMAND_CONTRACT_OK command1 owner-add3 original-quota original-seal-one-shot opaque-no-handle four-record-submit-gates-closed");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(
            create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& root) {
                const char* diagnostic = nullptr;
                const Command::Budget budget{1u, true, 1u, 51u, 65536u};
                const auto graph = root->originalGraph();
                const auto sources =
                    graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
                auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                    context, root, 51u, diagnostic);
                assert(admission);
                const auto rebuild = [&] {
                    admission.reset();
                    root.reset();
                    assert(!sources->originalStateTransactionCount());
                    root = OriginalCommandAccessFixture::prepareOriginalPlan(
                        graph, *context.device, context.quotas[5], diagnostic);
                    assert(root);
                    admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                        context, root, 51u, diagnostic);
                    assert(admission);
                };
                const auto make = [&] {
                    return Command::prepareOriginal(admission, budget, diagnostic);
                };
                CpuQuotaFactoryFixture pressure(context.quotas[5]);
                const auto peak = pressure.measure(make);
                pressure.verifySingleUse(peak, rebuild, make);
                auto invalid = budget;
                --invalid.maxRetainedOwners;
                assert(!Command::prepareOriginal(admission, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-OWNER-CAPACITY");
                invalid = budget;
                --invalid.maxCpuMetadataBytes;
                assert(!Command::prepareOriginal(admission, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-QUOTA");
                auto command = make();
                assert(command && !command->validate() && command->retainedOwnerCount() == 51u);
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-DUPLICATE");
                auto peer = Admission::prepareOriginal(admission->originalClosure(),
                    admission->originalReservation(),
                    admission->originalBudget(),
                    diagnostic);
                assert(peer);
                assert(!Command::prepareOriginal(peer, budget, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-DUPLICATE");
                peer.reset();
                auto callback = admission->originalCallbackQuota(),
                     submission = admission->originalSubmissionQuota();
                auto forged = std::shared_ptr<const Command>(callback, command.get());
                is(Command::validateOriginal(forged), "RHI-WEBGPU-CANONICAL-COLD-COMMAND-OWNER");
                forged.reset();
                std::array quotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    submission,
                    callback};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    if (const auto* id = Command::validateOriginal(command))
                        return id;
                    if (!core::SharedOwnerProjection::sameOwner(
                            command->originalAdmission(), admission))
                        return "TEST-COLD-COMMAND-ADMISSION";
                    return command->nativeExecutionProven() || !command->executionDiagnostic()
                        ? "TEST-COLD-COMMAND-EXECUTION"
                        : nullptr;
                }));
                command.reset();
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-DUPLICATE");
                // A new reservation on the same original seal cannot grant a second
                // native command. Caller retains only exact existing seal/root here.
                auto originalSeal = admission->originalReservation()->originalSeal();
                auto closure = admission->originalClosure();
                admission.reset();
                auto reservation = rhi::OriginalCommandLifecycleReservation::prepareOriginal(
                    originalSeal, Admission::selectedRequirements().capacity, diagnostic);
                assert(reservation);
                auto cold = WebGpuCanonicalColdAdmissionFixture::budget();
                cold.maxRetainedOwners = 51u;
                admission = Admission::prepareOriginal(closure, reservation, cold, diagnostic);
                assert(admission);
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-DUPLICATE");
                originalSeal.reset();
                closure.reset();
                reservation.reset();
                rebuild();
                command = make();
                assert(command);
                callback = admission->originalCallbackQuota();
                submission = admission->originalSubmissionQuota();
                quotas[7] = submission;
                quotas[8] = callback;
                std::weak_ptr<rhi::OriginalCommandStateTransaction> retained = root;
                root.reset();
                admission.reset();
                context.nativeLossObservation.reset();
                assert(!retained.expired() && !command->validate());
                context.device.reset();
                is(command->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                assert(sources->originalReservedSubmissionCount() == 1u
                    && sources->originalReservedCallbackTicketCount() == 8u
                    && sources->originalReservedCallbackRegistrationCount() == 8u);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    command.reset();
                    assert(retained.expired());
                    retained.reset();
                    return static_cast<const char*>(nullptr);
                }));
                assert(!sources->originalStateTransactionCount()
                    && !sources->originalReservedSubmissionCount()
                    && !sources->originalReservedCallbackTicketCount()
                    && !sources->originalReservedCallbackRegistrationCount());
                assert(!context.quotas[5]->liveBytes() && !submission->liveBytes()
                    && !callback->liveBytes() && !context.nativeLossQuota->liveBytes());
                std::printf(
                    "BINDLESS_RHI_CANONICAL_COLD_COMMAND_OK owner51 cold%llu actual-original-device-queue no-encoder exact-runtime-admission quota-bound one-seal-one-command duplicate-peer-and-replacement-reservation-refused exact-less-exhausted forged-count-owner-CPU-refuse caller-drop shutdown-retains-counts hot-allocation0 execution-closed\n",
                    static_cast<unsigned long long>(peak));
            },
            5u,
            true);
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(
            create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& root) {
                const char* diagnostic = nullptr;
                auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                    context, root, 51u, diagnostic);
                assert(admission);
                auto command =
                    Command::prepareOriginal(admission, {1u, true, 1u, 51u, 65536u}, diagnostic);
                assert(command);
                live_ = core::makeUnique<Live>();
                live_->quotas = {context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    admission->originalSubmissionQuota(),
                    admission->originalCallbackQuota()};
                live_->device = std::move(context.device);
                live_->command = std::move(command);
            },
            5u,
            true);
#else
        (void)create;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(live_);
        auto& live = *live_;
        assert(WebGpuAsyncWaitFixture::until([&] {
            const auto* id = live.command->validate();
            return id && std::string_view(id) == "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST";
        }));
        auto source = live.command->originalAdmission()
                          ->originalRoot()
                          ->originalGraph()
                          ->originalInputs()
                          ->originalScalars()
                          ->originalUsage()
                          ->originalSources();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(live.quotas, [&]() noexcept {
            is(live.command->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            assert(source->originalReservedSubmissionCount() == 1u
                && source->originalReservedCallbackTicketCount() == 8u
                && source->originalReservedCallbackRegistrationCount() == 8u);
            live.command.reset();
            assert(!source->originalStateTransactionCount()
                && !source->originalReservedSubmissionCount()
                && !source->originalReservedCallbackTicketCount()
                && !source->originalReservedCallbackRegistrationCount());
            source.reset();
            live.device.reset();
            return static_cast<const char*>(nullptr);
        }));
        for (const auto& quota : live.quotas)
            assert(!quota->liveBytes());
        live_.reset();
        std::puts(
            "BINDLESS_RHI_CANONICAL_COLD_COMMAND_LOSS_OK actual-original-browser-loss opaque-native-custody retains1-8-8 no-encoder-no-submitted-work pre-record-release counts-and-quotas-zero hot-allocation0 no-completion-proof");
#endif
    }
};
} // namespace arp::test
