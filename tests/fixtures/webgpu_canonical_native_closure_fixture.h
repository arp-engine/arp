#pragma once

#include "original_command_access_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/semantics/command_lowering.h"

#include <string_view>

namespace arp::test {
class WebGpuCanonicalNativeClosureFixture final {
    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyContract()
    {
        rhi::OriginalCommandNativeClosureBudget budget;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(budget),
            "RHI-ORIGINAL-NATIVE-CLOSURE-VERSION");
        budget.contractVersion = 1u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(budget),
            "RHI-ORIGINAL-NATIVE-CLOSURE-OPT-IN");
        budget.prototypeOptIn = true;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(budget),
            "RHI-ORIGINAL-NATIVE-CLOSURE-BUDGET");
        budget = {1u, true, 1u, 1u, 1u, 1u, 1u};
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(budget));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandNativeClosureLowering::lowering(backend);
            assert(!decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
        }
        auto typed = budget;
        typed.contractVersion = 2u;
        typed.maxPipelineSources = typed.maxDescriptorSources = 1u;
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(typed));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandNativeClosureLowering::typedLowering(backend);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert((decision.disposition
                       == arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
            const auto bound =
                arp::rhi::semantics::OriginalCommandNativeClosureLowering::typedLossBoundLowering(
                    backend);
            assert(bound.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !bound.recordingIssuerAvailable && !bound.executionAvailable
                && bound.diagnostic);
            assert((bound.disposition
                       == arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
        }
        auto invalid = typed;
        invalid.maxPipelineSources = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-SOURCE-BUDGET");
        invalid = typed;
        invalid.maxDescriptorSources = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-SOURCE-BUDGET");
        invalid = typed;
        invalid.contractVersion = 1u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-LEGACY-SCHEMA");
        auto table = typed;
        table.contractVersion = 3u;
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(table));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                rhi::semantics::OriginalCommandNativeClosureLowering::tableLossBoundLowering(
                    backend);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert((decision.disposition
                       == rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
        }
        invalid = table;
        invalid.maxDescriptorSources = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-SOURCE-BUDGET");
        auto readback = table;
        readback.contractVersion = 4u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(readback),
            "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-BUDGET");
        readback.maxReadbackSources = readback.maxReadbackCopies = 1u;
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(readback));
        invalid = readback;
        invalid.contractVersion = 3u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-SCHEMA");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = rhi::semantics::OriginalCommandNativeClosureLowering::
                tableReadbackLossBoundLowering(backend);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert((decision.disposition
                       == rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
        }
        auto index = table;
        index.contractVersion = 5u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(index),
            "RHI-ORIGINAL-INDEX-CUSTODY-VERSION");
        index.indexContent = {1u, true, 1u, 24u, 16u};
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(index));
        invalid = index;
        invalid.indexContent.prototypeOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-INDEX-CUSTODY-OPT-IN");
        invalid = index;
        invalid.indexContent.maxSources = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-INDEX-CUSTODY-BUDGET");
        invalid = index;
        invalid.contractVersion = 3u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-INDEX-SCHEMA");
        invalid = index;
        invalid.maxReadbackSources = invalid.maxReadbackCopies = 1u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-SCHEMA");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                rhi::semantics::OriginalCommandNativeClosureLowering::indexContentLossBoundLowering(
                    backend);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
        }
        auto general = typed;
        general.contractVersion = 8u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(general),
            "RHI-ORIGINAL-NATIVE-CLOSURE-GENERAL-VERSION");
        general.generalShaderResourcesVersion = 1u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(general),
            "RHI-ORIGINAL-NATIVE-CLOSURE-GENERAL-OPT-IN");
        general.generalShaderResourcesOptIn = true;
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(general));
        auto content = general;
        content.shaderBufferContent = {1u, true, 1u, 16u, 16u};
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(content));
        auto textureContent = general;
        textureContent.shaderTextureContent = {1u, true, 1u, 32u, 20u, 2u, 2u, 5u};
        assert(!rhi::OriginalCommandNativeClosureContract::validateIntent(textureContent));
        auto badTexture = textureContent;
        badTexture.shaderTextureContent.prototypeOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badTexture),
            "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-OPT-IN");
        badTexture = textureContent;
        badTexture.shaderTextureContent.contractVersion = 2u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badTexture),
            "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-VERSION");
        badTexture = textureContent;
        badTexture.shaderTextureContent.maxJoinVisits = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badTexture),
            "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-BUDGET");
        badTexture = textureContent;
        badTexture.contractVersion = 2u;
        badTexture.generalShaderResourcesVersion = 0u;
        badTexture.generalShaderResourcesOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badTexture),
            "RHI-ORIGINAL-NATIVE-CLOSURE-SHADER-TEXTURE-SCHEMA");
        auto badContent = content;
        badContent.shaderBufferContent.prototypeOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badContent),
            "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-OPT-IN");
        badContent = content;
        badContent.shaderBufferContent.contractVersion = 2u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badContent),
            "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-VERSION");
        badContent = content;
        badContent.shaderBufferContent.maxInitializedBytes = 0u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badContent),
            "RHI-ORIGINAL-SHADER-CONTENT-CUSTODY-BUDGET");
        badContent = content;
        badContent.contractVersion = 2u;
        badContent.generalShaderResourcesVersion = 0u;
        badContent.generalShaderResourcesOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(badContent),
            "RHI-ORIGINAL-NATIVE-CLOSURE-SHADER-CONTENT-SCHEMA");
        invalid = general;
        invalid.contractVersion = 2u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-GENERAL-SCHEMA");
        invalid = general;
        invalid.maxReadbackSources = 1u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-READBACK-SCHEMA");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = rhi::semantics::OriginalCommandNativeClosureLowering::
                generalResourcesLossBoundLowering(backend);
            assert(decision.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.recordingIssuerAvailable && !decision.executionAvailable
                && decision.diagnostic);
            assert((decision.disposition
                       == rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
        }
        // Closure10 is the finite graphics custody profile, not an unknown ABI.
        invalid = typed;
        invalid.contractVersion = 11u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-VERSION");
        invalid = typed;
        invalid.contractVersion = 6u;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-GEOMETRY-CUSTODY-VERSION");
        invalid = typed;
        invalid.prototypeOptIn = false;
        is(rhi::OriginalCommandNativeClosureContract::validateIntent(invalid),
            "RHI-ORIGINAL-NATIVE-CLOSURE-OPT-IN");
        std::puts(
            "BINDLESS_RHI_CANONICAL_NATIVE_CLOSURE_BUDGET_OK version-opt-in independent-count-owner-byte-CPU four-backend-recording-execution-closed");
    }

    template <class Context>
    static rhi::OriginalCommandNativeClosureBudget resourceBudget(
        const Context& context, std::uint32_t owners)
    {
        rhi::OriginalCommandNativeClosureBudget result{1u, true, 5u, 5u, owners, 0u, 65536u};
        for (const auto index : {0u, 1u})
            result.maxRetainedResourceBytes +=
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    context.leases[index])
                    ->originalObject()
                    ->allocation()
                    .estimatedSizeBytes;
        for (const auto index : {2u, 3u})
            result.maxRetainedResourceBytes +=
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                    context.leases[index])
                    ->originalObject()
                    ->allocation()
                    .estimatedSizeBytes;
        return result;
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
        OriginalCommandAccessFixture::withOriginalPlan(
            create,
            [&](auto& context, auto& root) {
                const char* diagnostic = nullptr;
                rhi::OriginalCommandNativeClosureBudget typed{
                    2u, true, 5u, 5u, 42u, 4224u, 65536u, 1u, 1u};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    context.quotas, [&]() noexcept -> const char* {
                        assert(!Closure::prepareOriginal(root, *context.device, typed, diagnostic));
                        is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-STREAM-REQUIRED");
                        return nullptr;
                    }));
                assert(root->journalEmpty() && !root->validate());
                assert(!Closure::prepareOriginal(
                    root, *context.device, {1u, true, 5u, 5u, 42u, 4224u, 65536u}, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-PAYLOAD-CAPACITY");
                assert(root->journalEmpty() && !root->validate());
            },
            4u);
        OriginalCommandAccessFixture::withOriginalPlan(create, [&](auto& context, auto& root) {
            rhi::OriginalCommandNativeClosureBudget budget{1u, true, 5u, 5u, 42u, 4224u, 65536u};
            const char* diagnostic = nullptr;
            auto invalid = budget;
            invalid.prototypeOptIn = false;
            assert(!Closure::prepareOriginal(root, *context.device, invalid, diagnostic));
            is(diagnostic, "RHI-ORIGINAL-NATIVE-CLOSURE-OPT-IN");
            invalid = budget;
            --invalid.maxResourceBindings;
            assert(!Closure::prepareOriginal(root, *context.device, invalid, diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-BINDING-CAPACITY");
            invalid = budget;
            --invalid.maxNativePayloadObjects;
            assert(!Closure::prepareOriginal(root, *context.device, invalid, diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-PAYLOAD-CAPACITY");
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            assert(!Closure::prepareOriginal(root, *context.device, budget, diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-NATIVE-UNAVAILABLE");
            assert(root->journalEmpty() && !root->validate());
            std::puts(
                "BINDLESS_RHI_CANONICAL_NATIVE_CLOSURE_CONTRACT_OK four-backend full-gates-closed genuine-original-plan native-absence-refuses before-mutation");
#else
            // Allocation footprints come from genuine factory observations;
            // texture/view parent bytes are counted once, never an estimate here.
            budget=resourceBudget(context,42u);
            const auto factory=[&] {return Closure::prepareOriginal(root,*context.device,budget,diagnostic);};
            CpuQuotaFactoryFixture pressure(context.quotas[5]);const auto peak=pressure.measure(factory);
            auto closure=factory();if(!closure)std::fprintf(stderr,"canonical closure diagnostic: %s\n",diagnostic);
            assert(closure && closure->resourceBindingCount()==5u && closure->retainedOwnerCount()==42u);
            const auto bytes=closure->retainedResourceBytes();assert(bytes);
            closure.reset();budget.maxRetainedResourceBytes=bytes;pressure.verify(peak,factory);
            invalid=budget;--invalid.maxRetainedOwners;
            assert(!Closure::prepareOriginal(root,*context.device,invalid,diagnostic));is(diagnostic,"RHI-WEBGPU-CANONICAL-CLOSURE-OWNER-CAPACITY");
            invalid=budget;--invalid.maxRetainedResourceBytes;
            assert(!Closure::prepareOriginal(root,*context.device,invalid,diagnostic));is(diagnostic,"RHI-WEBGPU-CANONICAL-CLOSURE-ALLOCATION-CAPACITY");
            invalid=budget;--invalid.maxCpuMetadataBytes;
            assert(!Closure::prepareOriginal(root,*context.device,invalid,diagnostic));is(diagnostic,"RHI-WEBGPU-CANONICAL-CLOSURE-QUOTA");
            closure=factory();assert(closure && !closure->compareOriginalRoot(root));
            auto forged=std::shared_ptr<const Closure>(context.quotas[5],closure.get());
            is(Closure::validateOriginal(forged),"RHI-WEBGPU-CANONICAL-CLOSURE-OWNER");forged.reset();
            const auto* buffer=std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(context.leases[0])->originalObject();
            auto native=rhi_webgpu::webGpuNativePayloadFor(*buffer);assert(native);
            const auto saved=native->sizeBytes;--native->sizeBytes;
            assert(closure->validate());native->sizeBytes=saved;assert(!closure->validate());
            const auto nativeObject=native->buffer;native->buffer=nullptr;
            assert(closure->validate());native->buffer=nativeObject;assert(!closure->validate());
            const auto ownerDevice=native->ownerDevice;native->ownerDevice=nullptr;
            assert(closure->validate());native->ownerDevice=ownerDevice;assert(!closure->validate());
            const auto token=native->allocationReleaseToken;native->allocationReleaseToken.reset();
            assert(closure->validate());native->allocationReleaseToken=token;assert(!closure->validate());
            native->allocationReleaseToken=std::shared_ptr<void>(context.quotas[5],token.get());
            assert(closure->validate());native->allocationReleaseToken=token;assert(!closure->validate());
            auto foreign=create();assert(foreign);
            auto foreignQuota=core::makeShared<core::CpuMemoryQuota>(65536u);
            assert(!foreign->prepareOriginalDeviceLifetime({1u,true,1u,2u,65536u},foreignQuota));
            assert(!Closure::prepareOriginal(root,*foreign,budget,diagnostic));is(diagnostic,"RHI-ORIGINAL-DEVICE-LIFETIME-FOREIGN-DEVICE");foreign.reset();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(context.quotas,[&]() noexcept {
                if(const auto* id=Closure::validateOriginal(closure))return id;
                if(const auto* id=root->applyOriginalPrototypePlan())return id;
                if(const auto* id=closure->validate())return id;
                if(!closure->executionDiagnostic() || closure->nativeExecutionProven())return "TEST-CANONICAL-CLOSURE-EXECUTION";
                return root->commitOriginalPendingOwnership();
            }));
            assert(!closure->validate());
            assert(!Closure::prepareOriginal(root,*context.device,budget,diagnostic));is(diagnostic,"RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS");
            std::weak_ptr<rhi::OriginalCommandStateTransaction> retained=root;root.reset();assert(!retained.expired() && !closure->validate());
            context.device.reset();is(closure->validate(),"RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
            closure.reset();assert(retained.expired());
            std::printf("BINDLESS_RHI_CANONICAL_NATIVE_CLOSURE_OK resources5 owners42 bytes%llu cold%llu exact-original-root-registry-generation-device-payload parent-alias-dedup count-owner-byte-quota-boundaries forged-foreign-mutated-reject pending-committed-caller-drop shutdown hot-allocation0 execution-closed\n",
                static_cast<unsigned long long>(bytes),static_cast<unsigned long long>(peak));
#endif
        });
    }
};
} // namespace arp::test
