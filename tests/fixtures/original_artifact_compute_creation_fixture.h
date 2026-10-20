#pragma once
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/semantics/original_artifact_compute_creation_lowering.h"
#if !defined(__EMSCRIPTEN__) || defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "webgpu_error_scope_handoff_fixture.h"
#endif
#include "cpu_quota_factory_fixture.h"

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace arp::test {
class OriginalArtifactComputeCreationFixture final {
    using Contract = rhi::OriginalArtifactComputeCreationContract;
    using Budget = Contract::Budget;
#if !defined(__EMSCRIPTEN__) || defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Owner = rhi_webgpu::WebGpuOriginalArtifactComputeCreation;
    using Promises = WebGpuErrorScopeHandoffFixture<1001u>;
#endif
    using Input = rhi::OriginalSingleEntryPipelineInput;
    using Lowering = rhi::semantics::OriginalArtifactComputeCreationLowering;
    static constexpr std::uint64_t cpuBytes = 65536u;
    static constexpr std::string_view label = "original artifact asynchronous acceptance";

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(
                stderr, "creation expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

    static Budget budget(std::uint64_t targetBytes = 1u)
    {
        return {1u,
            true,
            rhi_webgpu::WebGpuErrorScopeContract::count,
            label.size(),
            512u,
            cpuBytes,
            cpuBytes,
            {1u, true, targetBytes, cpuBytes}};
    }

public:
    static void verifyContract()
    {
#if !defined(__EMSCRIPTEN__) || defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        static_assert(!std::is_copy_constructible_v<Owner>);
#endif
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        is(Contract::validateBudget({}), "RHI-ARTIFACT-CREATION-VERSION");
        auto b = budget();
        assert(!Contract::validateBudget(b));
        b.contractVersion = 0u;
        assert(Contract::validateBudget(b));
        b = budget();
        b.prototypeOptIn = false;
        is(Contract::validateBudget(b), "RHI-ARTIFACT-CREATION-OPT-IN");
        b = budget();
        --b.maxCallbacks;
        is(Contract::validateBudget(b), "RHI-ARTIFACT-CREATION-BUDGET");
        for (auto member : {&Budget::maxLabelBytes,
                 &Budget::maxDiagnosticBytes,
                 &Budget::maxCpuOwnerBytes,
                 &Budget::maxCpuCallbackBytes}) {
            b = budget();
            b.*member = 0u;
            is(Contract::validateBudget(b), "RHI-ARTIFACT-CREATION-BUDGET");
        }
        b = budget();
        b.native.contractVersion = 0u;
        assert(Contract::validateBudget(b));
        const Lowering::Facts complete{true, true, true, true, true, true, true};
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d = Lowering::decide(backend, complete);
            assert(
                !d.executionAvailable && d.creationEligible == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            assert(d.mode
                == (backend == rhi::Backend::Null ? rhi::CapabilityMode::Equivalent
                        : backend == rhi::Backend::WebGPU
                        ? rhi::CapabilityMode::Native
                        : rhi::CapabilityMode::DiagnosticFailure));
        }
        for (auto member : std::array{&Lowering::Facts::device,
                 &Lowering::Facts::shader,
                 &Lowering::Facts::compute,
                 &Lowering::Facts::bindGroupLayout,
                 &Lowering::Facts::pipelineLayout,
                 &Lowering::Facts::pushScope,
                 &Lowering::Facts::popScope}) {
            auto f = complete;
            f.*member = false;
            auto d = Lowering::decide(rhi::Backend::WebGPU, f);
            assert(!d.creationEligible && !d.executionAvailable && d.diagnostic);
        }
        assert(!Lowering::decide(static_cast<rhi::Backend>(255u), complete).creationEligible);
#if !defined(__EMSCRIPTEN__) || defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        is(Owner::initialize({}), "RHI-ARTIFACT-CREATION-OWNER");
        is(Owner::validateReadyOriginal({}), "RHI-ARTIFACT-CREATION-OWNER");
#endif
        std::pmr::set_default_resource(previous);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_ARTIFACT_CREATION_CONTRACT_OK backends4 facts7 missing-receipts-reject canonical-closed allocation0");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    class Context final {
    public:
        std::shared_ptr<const Input> input;
        std::shared_ptr<rhi::PipelineLayout> layout;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3> quotas;
        Budget limits;

        Context(const std::shared_ptr<const Input>& original,
            const std::shared_ptr<rhi::PipelineLayout>& originalLayout)
            : input(original), layout(originalLayout),
              limits(budget(original->artifact()->bytes(shader::ShaderArtifactRole::Wgsl).size()))
        {
            for (auto& q : quotas)
                q = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        }

        auto prepare(const char*& diagnostic)
        {
            return Owner::prepareOriginal(
                input, layout, label, limits, quotas[0], quotas[1], quotas[2], diagnostic);
        }

        void empty() const
        {
            for (const auto& q : quotas)
                assert(!q->liveBytes());
        }
    };

    static void verifyActual(const std::shared_ptr<const Input>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout)
    {
        verifyBounds(input, layout);
        verifyReady(input, layout);
        verifyFailures(input, layout);
        verifyDrop(input, layout);
        for (std::size_t i = 0u; i < lossOwners_.size(); ++i) {
            lossContexts_[i] = core::makeUnique<Context>(input, layout);
            const char* diagnostic = nullptr;
            lossOwners_[i] = lossContexts_[i]->prepare(diagnostic);
            assert(lossOwners_[i]);
        }
        assert(!Owner::initialize(lossOwners_[0]));
        waitScopes(lossOwners_[0]);
        assert(!lossOwners_[0]->validate());
        Promises::holdScopes();
        assert(!Owner::initialize(lossOwners_[1]));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        std::puts(
            "BINDLESS_RHI_ARTIFACT_CREATION_NATIVE_OK scopes3 exact-memory-input retained-layout real-ready canonical-closed");
    }

    static void verifyActualLoss()
    {
        assert(rhi_webgpu::webGpuNativeDeviceLost());
        for (const auto& owner : lossOwners_) {
            assert(owner->status() == Owner::Status::DeviceLost);
            is(owner->validate(), "RHI-ARTIFACT-CREATION-DEVICE-LOST");
            is(Owner::initialize(owner), "RHI-ARTIFACT-CREATION-DEVICE-LOST");
        }
        std::weak_ptr<Owner> pending = lossOwners_[1];
        for (auto& owner : lossOwners_)
            owner.reset();
        assert(!pending.expired());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return pending.expired();
        }));
        pending.reset();
        for (auto& c : lossContexts_) {
            const char* diagnostic = nullptr;
            assert(!c->prepare(diagnostic));
            is(diagnostic, "RHI-ARTIFACT-CREATION-DEVICE-LOST");
            c->empty();
            c.reset();
        }
        std::puts(
            "BINDLESS_RHI_ARTIFACT_CREATION_LOSS_OK prepared-pending-ready actual-loss callbacks-drained quotas3-zero");
    }

private:
    static void waitScopes(const std::shared_ptr<Owner>& owner)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner->scopesObserved();
        }));
    }

    static void verifyBounds(const std::shared_ptr<const Input>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout)
    {
        Context c(input, layout);
        const char* diagnostic = nullptr;
        auto factory = [&] {
            return c.prepare(diagnostic);
        };
        std::array<std::uint64_t, 3> peaks{};
        for (const auto i : std::array<std::size_t, 2>{0u, 2u}) {
            CpuQuotaFactoryFixture pressure(c.quotas[i]);
            peaks[i] = pressure.measure(factory);
            pressure.verify(peaks[i], factory);
            {
                core::CpuMemoryQuotaLease full(c.quotas[i], cpuBytes);
                assert(full.charged() && !factory());
                is(diagnostic, "RHI-ARTIFACT-CREATION-ALLOCATION");
            }
            c.empty();
        }
        auto nativeFactory = [&]() -> std::shared_ptr<Owner> {
            auto owner = factory();
            assert(owner);
            Owner::initialize(owner);
            waitScopes(owner);
            if (owner->validate())
                return {};
            return owner;
        };
        CpuQuotaFactoryFixture pressure(c.quotas[1]);
        peaks[1] = pressure.measure(nativeFactory);
        pressure.verify(peaks[1], nativeFactory);
        {
            core::CpuMemoryQuotaLease full(c.quotas[1], cpuBytes);
            assert(full.charged() && !nativeFactory());
        }
        c.empty();
        assert(nativeFactory());
        c.empty();
        auto original = c.input;
        c.input = std::shared_ptr<const Input>(c.quotas[0], input.get());
        assert(!factory());
        is(diagnostic, "RHI-ORIGINAL-ARTIFACT-INPUT-OWNER");
        c.input = original;
        auto originalLayout = c.layout;
        c.layout = std::shared_ptr<rhi::PipelineLayout>(c.quotas[0], layout.get());
        assert(!factory());
        is(diagnostic, "RHI-ARTIFACT-CREATION-LAYOUT-OWNER");
        c.layout = originalLayout;
        --c.limits.maxLabelBytes;
        assert(!factory());
        is(diagnostic, "RHI-ARTIFACT-CREATION-LABEL-CAPACITY");
        ++c.limits.maxLabelBytes;
        for (auto& q : c.quotas) {
            auto saved = q;
            q.reset();
            assert(!factory());
            is(diagnostic, "RHI-ARTIFACT-CREATION-QUOTA");
            q = saved;
        }
        auto saved = c.quotas[1];
        c.quotas[1] = c.quotas[0];
        assert(!factory());
        is(diagnostic, "RHI-ARTIFACT-CREATION-QUOTA-DOMAIN");
        c.quotas[1] = saved;
        c.empty();
        std::printf(
            "BINDLESS_RHI_ARTIFACT_CREATION_COLD_OK %llu %llu %llu independent-owner-native-callback exact-less-exhausted-retry forged-rejected\n",
            static_cast<unsigned long long>(peaks[0]),
            static_cast<unsigned long long>(peaks[1]),
            static_cast<unsigned long long>(peaks[2]));
    }

    static void verifyReady(const std::shared_ptr<const Input>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout)
    {
        Context c(input, layout);
        const char* diagnostic = nullptr;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto owner = c.prepare(diagnostic);
        assert(owner);
        is(owner->validate(), "RHI-ARTIFACT-CREATION-UNINITIALIZED");
        auto forged = std::shared_ptr<Owner>(c.quotas[0], owner.get());
        is(Owner::initialize(forged), "RHI-ARTIFACT-CREATION-OWNER");
        assert(Owner::validateReadyOriginal(forged));
        forged.reset();
        Promises::holdScopes();
        assert(!Owner::initialize(owner));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        is(owner->validate(), "RHI-ARTIFACT-CREATION-PENDING");
        assert(!owner->scopesObserved());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        Promises::releaseScope(2u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner->scopeObserved(2u);
        }));
        assert(owner->status() == Owner::Status::Pending);
        Promises::releaseScope(0u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return owner->scopeObserved(0u);
        }));
        assert(owner->status() == Owner::Status::Pending);
        Promises::releaseScopes();
        waitScopes(owner);
        assert(!Owner::validateReadyOriginal(owner));
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        assert(core::SharedOwnerProjection::sameOwner(owner->originalInput(), input)
            && core::SharedOwnerProjection::sameOwner(owner->originalLayout(), layout));
        assert(owner->definition().shaderPackage == &input->package()
            && owner->definition().computeShader.entryPoint == input->artifact()->entry());
        is(Owner::initialize(owner), "RHI-ARTIFACT-CREATION-START-STATUS");
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            for (const auto& q : c.quotas) {
                q->setLimitBytes(0u);
                is(owner->validate(), "RHI-ARTIFACT-CREATION-QUOTA-LIMIT");
                q->setLimitBytes(cpuBytes - 1u);
                assert(owner->validate());
                q->setLimitBytes(cpuBytes);
            }
            is(owner->executionDiagnostic(), "RHI-ARTIFACT-CREATION-CANONICAL-JOIN-MISSING");
            return Owner::validateReadyOriginal(owner);
        }));
        EM_ASM({
            // clang-format off
                Module.arpArtifactOldDevice = Module.preinitializedWebGPUDevice;
                Module.preinitializedWebGPUDevice = {};
            // clang-format on
        });
        assert(owner->validate());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice = Module.arpArtifactOldDevice;
                delete Module.arpArtifactOldDevice;
            // clang-format on
        });
        assert(!owner->validate());
        owner.reset();
        c.empty();
        std::puts(
            "BINDLESS_RHI_ARTIFACT_CREATION_READY_OK out-of-order3 exact-device unchanged-ceilings hostile-pmr hot-allocation0");
    }

    static void verifyFailures(const std::shared_ptr<const Input>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout)
    {
        // Only the test interception changes the selected entry. The real native
        // pipeline creation returns a genuine validation error inside real scopes.
        Context c(input, layout);
        const char* diagnostic = nullptr;
        auto owner = c.prepare(diagnostic);
        assert(owner);
        EM_ASM({
            // clang-format off
                Module.arpArtifactComputeOriginal = Module.preinitializedWebGPUDevice.createComputePipeline;
                Module.preinitializedWebGPUDevice.createComputePipeline = function(d) {
                    const changed = Object.assign({}, d, {
                        compute : Object.assign({}, d.compute, {entryPoint : 'missingOriginalArtifactEntry'})
                    });
                    return Module.arpArtifactComputeOriginal.call(this, changed);
                };
            // clang-format on
        });
        Owner::initialize(owner);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createComputePipeline = Module.arpArtifactComputeOriginal;
                delete Module.arpArtifactComputeOriginal;
            // clang-format on
        });
        waitScopes(owner);
        assert(owner->status() == Owner::Status::Rejected);
        is(owner->validate(), "RHI-ARTIFACT-CREATION-NATIVE-SCOPE");
        owner.reset();
        c.empty();
        // Pop executes on the real device; a rejected promise is unknown evidence.
        owner = c.prepare(diagnostic);
        assert(owner);
        EM_ASM({
            // clang-format off
                Module.arpArtifactPopOriginal = Module.preinitializedWebGPUDevice.popErrorScope;
                Module.preinitializedWebGPUDevice.popErrorScope = function() {
                    return Module.arpArtifactPopOriginal.call(this).then(function() {
                        throw new Error('artifact creation test pop rejection');
                    });
                };
            // clang-format on
        });
        Owner::initialize(owner);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.popErrorScope = Module.arpArtifactPopOriginal;
                delete Module.arpArtifactPopOriginal;
            // clang-format on
        });
        waitScopes(owner);
        assert(owner->status() == Owner::Status::Rejected);
        is(owner->validate(), "RHI-ARTIFACT-CREATION-NATIVE-SCOPE");
        owner.reset();
        c.empty();
        owner = c.prepare(diagnostic);
        assert(owner);
        EM_ASM({
            // clang-format off
                Module.arpArtifactMissingOriginal = Module.preinitializedWebGPUDevice.createComputePipeline;
                Module.preinitializedWebGPUDevice.createComputePipeline = undefined;
            // clang-format on
        });
        is(Owner::initialize(owner), "RHI-ARTIFACT-CREATION-WEBGPU-API");
        assert(owner->status() == Owner::Status::Prepared);
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createComputePipeline = Module.arpArtifactMissingOriginal;
                delete Module.arpArtifactMissingOriginal;
            // clang-format on
        });
        owner.reset();
        c.empty();
        std::puts(
            "BINDLESS_RHI_ARTIFACT_CREATION_FAILURES_OK real-native-validation-error unknown-pop-rejected changed-api no-ready");
    }

    static void verifyDrop(const std::shared_ptr<const Input>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout)
    {
        Context c(input, layout);
        const char* diagnostic = nullptr;
        auto owner = c.prepare(diagnostic);
        assert(owner);
        Promises::holdScopes();
        assert(!Owner::initialize(owner));
        Promises::restoreScopes();
        Promises::waitHeldScopes();
        std::weak_ptr<Owner> weak = owner;
        owner.reset();
        c.input.reset();
        c.layout.reset();
        assert(!weak.expired());
        for (const auto& q : c.quotas)
            assert(q->liveBytes());
        Promises::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return weak.expired();
        }));
        weak.reset();
        c.empty();
        std::puts(
            "BINDLESS_RHI_ARTIFACT_CREATION_DROP_OK actual-pending-callbacks-retain drain-before-retirement quotas3-zero");
    }

    inline static std::array<core::UniquePtr<Context>, 3> lossContexts_;
    inline static std::array<std::shared_ptr<Owner>, 3> lossOwners_;
#endif
};
} // namespace arp::test
