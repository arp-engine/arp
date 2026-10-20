#pragma once
#include "rhi/backends/webgpu/webgpu_original_query_binding.h"
#include "webgpu_original_query_owner_fixture.h"

namespace arp::test {
class WebGpuOriginalQueryBindingFixture final {
    using Binding = rhi_webgpu::WebGpuOriginalQueryBinding;
    using Owner = rhi_webgpu::WebGpuOriginalQueryOwner;
    using Context = WebGpuOriginalQueryOwnerFixture::Context;
    using Budget = rhi_webgpu::WebGpuOriginalQueryBindingBudget;
    using Promises = WebGpuErrorScopeHandoffFixture<28u>;
    static constexpr std::uint64_t cpuBytes = 65536u;

    static Budget budget(const Context& c)
    {
        return {1u, true, 1u, 4u, 2u, c.input->label().size()};
    }

    static auto command(const Context& c,
        std::uint32_t index = 3u,
        rhi::CommandUsageKind kind = rhi::CommandUsageKind::Timestamp)
    {
        return rhi::QueryPoolOriginalInputArguments::captureOriginal(kind, index, c.input);
    }

public:
    static void verifyContract()
    {
        static_assert(!std::is_default_constructible_v<Binding>);
        Context c;
        const char* diagnostic = nullptr;
        auto b = budget(c);
        assert(!Binding::validateBudget(b));
        b.contractVersion = 0u;
        assert(Binding::validateBudget(b));
        b = budget(c);
        b.prototypeOptIn = false;
        assert(Binding::validateBudget(b));
        b = budget(c);
        b.maxNativeOwners = 0u;
        assert(Binding::validateBudget(b));
        b = budget(c);
        b.maxQuerySlots = 0u;
        assert(Binding::validateBudget(b));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = Binding::lowering(backend);
            assert(d.declarationImplemented == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null) && d.diagnostic);
        }
        assert(!Binding::lowering(static_cast<rhi::Backend>(99u)).declarationImplemented);
        assert(!Binding::captureOriginal({}, {}, budget(c), diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-BINDING-INPUT");
        assert(!Binding::captureOriginal(command(c), {}, budget(c), diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-BINDING-OWNER");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        c.retire();
        std::puts(
            "BINDLESS_RHI_QUERY_BINDING_CONTRACT_OK backends4 fixed-value original-command-owner no-new-storage recording-closed allocation0");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyActual()
    {
        verifyOriginals();
        verifyPendingDrop();
        verifyRejected();
        std::puts(
            "BINDLESS_RHI_QUERY_BINDING_NATIVE_OK exact-kind-index owner-input native-owner-distinct counts ready-only full-drop allocation0");
    }

    static void prepareActualLoss()
    {
        for (std::size_t i = 0u; i < lossContexts_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>();
            const char* diagnostic = nullptr;
            auto owner = lossContexts_[i]->prepareDefault(diagnostic);
            assert(owner);
            auto binding = Binding::captureOriginal(
                command(*lossContexts_[i]), owner, budget(*lossContexts_[i]), diagnostic);
            assert(binding);
            lossBindings_[i].emplace(*binding);
            if (i == 0u) {
                assert(!Owner::initialize(owner));
                waitReady(owner);
            } else if (i == 1u) {
                Promises::holdScopes();
                assert(!Owner::initialize(owner));
                Promises::restoreScopes();
                Promises::waitHeldScopes();
            }
        }
    }

    static void verifyActualDeviceLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        std::weak_ptr<const Owner> pending = lossBindings_[1]->originalOwner();
        for (auto& b : lossBindings_) {
            assert(b
                && std::string_view(b->validateReady()) == "RHI-WEBGPU-QUERY-CREATION-DEVICE-LOST"
                && b->executionDiagnostic() && !b->recordingSupported());
            b.reset();
        }
        assert(!pending.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.expired();
        }));
        pending.reset();
        for (auto& c : lossContexts_) {
            c->retire();
            c.reset();
        }
        std::puts(
            "BINDLESS_RHI_QUERY_BINDING_LOSS_OK prepared-pending-ready exact-original admission-closed late-retirement");
    }

private:
    static void waitReady(const std::shared_ptr<Owner>& owner)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner->status() != Owner::Status::Pending;
        }));
        assert(!Owner::validateReadyOriginal(owner));
    }

    static void verifyOriginals()
    {
        Context c, other;
        const char* diagnostic = nullptr;
        auto owner = c.prepareDefault(diagnostic), sameInputOwner = c.prepareDefault(diagnostic),
             otherOwner = other.prepareDefault(diagnostic);
        assert(owner && sameInputOwner && otherOwner);
        auto cmd = command(c), differentIndex = command(c, 2u), invalidIndex = command(c, 4u),
             invalidKind = command(c, 0u, rhi::CommandUsageKind::PipelineStatistics),
             otherCmd = command(other);
        auto b = budget(c);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto old = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto binding = Binding::captureOriginal(cmd, owner, b, diagnostic);
        assert(binding
            && std::string_view(binding->validateReady()) == "RHI-QUERY-CREATION-UNINITIALIZED"
            && !binding->recordingSupported());
        auto another = Binding::captureOriginal(cmd, sameInputOwner, b, diagnostic);
        assert(another && !binding->matchesOriginal(*cmd, sameInputOwner)
            && another->matchesOriginal(*cmd, sameInputOwner));
        another.reset();
        assert(binding->matchesOriginal(*cmd, owner)
            && !binding->matchesOriginal(*differentIndex, owner)
            && !binding->matchesOriginal(*otherCmd, owner));
        assert(!Binding::captureOriginal(cmd, otherOwner, b, diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-BINDING-ORIGINAL-INPUT");
        assert(!Binding::captureOriginal(invalidIndex, owner, b, diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-COMMAND-INDEX");
        assert(!Binding::captureOriginal(invalidKind, owner, b, diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-COMMAND-TYPE");
        auto forged = std::shared_ptr<const Owner>(c.quotas[0], owner.get());
        assert(!Binding::captureOriginal(cmd, forged, b, diagnostic)
            && std::string_view(diagnostic) == "RHI-QUERY-BINDING-OWNER");
        assert(!binding->matchesOriginal(*cmd, forged));
        forged.reset();
        --b.maxQuerySlots;
        assert(!Binding::captureOriginal(cmd, owner, b, diagnostic));
        ++b.maxQuerySlots;
        --b.maxScopeIds;
        assert(!Binding::captureOriginal(cmd, owner, b, diagnostic));
        ++b.maxScopeIds;
        --b.maxLabelBytes;
        assert(!Binding::captureOriginal(cmd, owner, b, diagnostic));
        ++b.maxLabelBytes;
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(old);
        assert(!Owner::initialize(owner));
        waitReady(owner);
        assert(!binding->validateReady() && binding->executionDiagnostic());
        const auto complete = rhi::semantics::WebGpuTimestampCommandPointFacts{
            true, true, true, true, true, true, true, true, true};
        {
            const auto completeDecision = rhi::semantics::OriginalQuerySourceLowering::decide(
                rhi::Backend::WebGPU, 1u, true, binding->originalCommand(), complete);
            assert(!completeDecision.nativeSourceSupported()
                && std::string_view(completeDecision.diagnostic())
                    == "RHI-WEBGPU-ORIGINAL-QUERY-PAYLOAD-UNIMPLEMENTED");
        }
        // All graph quotas may be full: binding copies, ready validation and gate
        // consume existing ownership and never grow a container or native payload.
        const auto hot = core::defaultCpuAllocator().stats().allocationCount;
        {
            std::array<core::CpuMemoryQuotaLease, 5> pressure{
                core::CpuMemoryQuotaLease(c.inputQuotas[0],
                    static_cast<std::size_t>(cpuBytes - c.inputQuotas[0]->liveBytes())),
                core::CpuMemoryQuotaLease(c.inputQuotas[1],
                    static_cast<std::size_t>(cpuBytes - c.inputQuotas[1]->liveBytes())),
                core::CpuMemoryQuotaLease(c.inputQuotas[2],
                    static_cast<std::size_t>(cpuBytes - c.inputQuotas[2]->liveBytes())),
                core::CpuMemoryQuotaLease(
                    c.quotas[0], static_cast<std::size_t>(cpuBytes - c.quotas[0]->liveBytes())),
                core::CpuMemoryQuotaLease(
                    c.quotas[1], static_cast<std::size_t>(cpuBytes - c.quotas[1]->liveBytes()))};
            for (const auto& p : pressure)
                assert(p.charged());
            auto copy = *binding;
            assert(!copy.validateReady() && copy.matchesOriginal(*cmd, owner)
                && copy.executionDiagnostic());
            auto again = Binding::captureOriginal(cmd, owner, b, diagnostic);
            assert(again && !again->recordingSupported());
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == hot);
        sameInputOwner.reset();
        otherOwner.reset();
        otherCmd.reset();
        other.retire();
        differentIndex.reset();
        invalidIndex.reset();
        invalidKind.reset();
        cmd.reset();
        owner.reset();
        c.input.reset();
        assert(!binding->validateReady());
        binding.reset();
        c.retire();
        std::printf(
            "BINDLESS_RHI_QUERY_BINDING_VALUE_OK %zu existing-quotas5 all-pressure copy-compare-current allocation0\n",
            sizeof(Binding));
    }

    static void verifyPendingDrop()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto owner = c.prepareDefault(diagnostic);
        assert(owner);
        auto binding = Binding::captureOriginal(command(c), owner, budget(c), diagnostic);
        assert(binding);
        Promises::holdScopes();
        assert(!Owner::initialize(owner));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        assert(std::string_view(binding->validateReady()) == "RHI-QUERY-CREATION-PENDING"
            && binding->executionDiagnostic());
        std::weak_ptr<Owner> weak = owner;
        owner.reset();
        c.input.reset();
        assert(!weak.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return !binding->validateReady();
        }));
        assert(!binding->recordingSupported() && binding->executionDiagnostic());
        binding.reset();
        assert(weak.expired());
        weak.reset();
        c.retire();
    }

    static void verifyRejected()
    {
        Context c;
        const char* diagnostic = nullptr;
        auto owner = c.prepareDefault(diagnostic);
        assert(owner);
        auto binding = Binding::captureOriginal(command(c), owner, budget(c), diagnostic);
        assert(binding);
        EM_ASM(
            {
                // clang-format off
                    Module.arpQueryBindingBad = Module.preinitializedWebGPUDevice.createQuerySet;
                    Module.preinitializedWebGPUDevice.createQuerySet = function(d) {
                        return Module.arpQueryBindingBad.call(this, Object.assign({}, d, ({count : $0})));
                    };
                // clang-format on
            },
            Owner::Lowering::maximumWebGpuQueryCount + 1u);
        assert(!Owner::initialize(owner));
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createQuerySet = Module.arpQueryBindingBad;
                delete Module.arpQueryBindingBad;
            // clang-format on
        });
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner->creationScopesObserved();
        }));
        assert(owner->status() == Owner::Status::Rejected && binding->validateReady()
            && binding->executionDiagnostic());
        assert(!Binding::captureOriginal(command(c), owner, budget(c), diagnostic));
        binding.reset();
        owner.reset();
        c.retire();
    }

    static inline std::array<core::UniquePtr<Context>, 3> lossContexts_;
    static inline std::array<std::optional<Binding>, 3> lossBindings_;
#endif
};
} // namespace arp::test
