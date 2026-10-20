#include "../fixtures/webgl_command_state_lowering_fixture.h"
#include "../fixtures/webgl_resource_content_lowering_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/resource_loss_validation.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Resource lifetime contract tests require assertions
#endif

using namespace arp;
using namespace arp::rhi;

static void expect(const char* actual, const char* expected)
{
    if (!actual || std::strcmp(actual, expected) != 0)
        std::fprintf(stderr, "expected %s, got %s\n", expected, actual ? actual : "<null>");
    assert(actual && std::strcmp(actual, expected) == 0);
}

static void verifyWebGlRefusal()
{
    constexpr auto backend = Backend::WebGL;
    const auto lifetime = OriginalDeviceLifetimeContract::lowering(backend);
    assert(lifetime.contractVersion == 1u && lifetime.mode == CapabilityMode::DiagnosticFailure);
    assert(!lifetime.logicalLifetimeAvailable && !lifetime.resourceIssuerAvailable
        && !lifetime.nativeLossObserved && !lifetime.executionAvailable);
    expect(lifetime.diagnostic, "RHI-WEBGL1-ORIGINAL-DEVICE-LIFETIME-UNAVAILABLE");

    for (auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
        const auto registration = OriginalResourceRegistrationContract::lowering(backend, version);
        assert(registration.contractVersion == version
            && registration.mode == CapabilityMode::DiagnosticFailure);
        assert(!registration.typedLifetimeIssuerAvailable
            && !registration.originalStateIssuerAvailable && !registration.nativeLossObserved
            && !registration.executionAvailable && !registration.pipelineLifetimeIssuerAvailable
            && !registration.descriptorLifetimeIssuerAvailable
            && !registration.fullPipelineDefinitionCustodyAvailable);
        expect(registration.diagnostic, "RHI-WEBGL1-ORIGINAL-REGISTRATION-UNAVAILABLE");
    }
    const auto sampler =
        OriginalResourceRegistrationContract::samplerLowering(backend, {1u, true, 1u});
    assert(sampler.contractVersion == 1u && sampler.mode == CapabilityMode::DiagnosticFailure);
    assert(!sampler.typedLifetimeIssuerAvailable && !sampler.originalStateIssuerAvailable
        && !sampler.nativeLossObserved && !sampler.executionAvailable);
    expect(sampler.diagnostic, "RHI-WEBGL1-SAMPLER-REGISTRATION-UNAVAILABLE");
    const auto reference = OriginalCommandTypedSourceContract::samplerReferenceLowering(backend);
    assert(reference.contractVersion == 1u && reference.mode == CapabilityMode::DiagnosticFailure);
    assert(!reference.referenceAvailable && !reference.executionAvailable);
    expect(reference.diagnostic, "RHI-WEBGL1-SAMPLER-REFERENCE-UNAVAILABLE");

    const auto subscription = DeviceLossRegistrationContract::lowering(backend);
    assert(!subscription.nativePrototype && !subscription.validatorOnly);
    expect(subscription.diagnostic, "RHI-WEBGL1-LOSS-SUBSCRIPTION-UNAVAILABLE");
    const auto loss = OriginalDeviceNativeLossContract::lowering(backend);
    assert(!loss.nativePrototype && !loss.validatorOnly && !loss.executionAvailable);
    expect(loss.diagnostic, "RHI-WEBGL1-ORIGINAL-LOSS-SUBSCRIPTION-UNAVAILABLE");
    const auto result = OriginalDeviceNativeResultLossContract::lowering(backend);
    assert(!result.bindingAvailable && !result.validatorOnly && !result.executionAvailable);
    expect(result.diagnostic, "RHI-WEBGL1-RESULT-LOSS-SOURCE-UNAVAILABLE");
    const auto observer = OriginalDeviceNativeLossObserverContract::lowering(backend);
    assert(!observer.validatorOnly && !observer.bindingAvailable
        && !observer.observerIssuanceAvailable && !observer.executionAvailable
        && !observer.typedColdObserverPreparationAvailable);
    expect(observer.diagnostic, "RHI-WEBGL1-LOSS-OBSERVER-UNAVAILABLE");

    for (auto version : {1u, 2u}) {
        const auto typed = OriginalTypedNativeLossJoinContract::lowering(backend, version);
        assert(!typed.registrationAvailable && !typed.validatorOnly
            && !typed.acceptedPipelineJoinAvailable && !typed.scopedGroupJoinAvailable
            && !typed.recordingAvailable && !typed.executionAvailable);
        expect(typed.diagnostic, "RHI-WEBGL1-TYPED-LOSS-JOIN-UNAVAILABLE");
        const auto samplerLoss = OriginalSamplerNativeLossJoinContract::lowering(backend, version);
        assert(samplerLoss.contractVersion == version && !samplerLoss.validatorOnly
            && !samplerLoss.nativeSourceAvailable && !samplerLoss.recordingAvailable
            && !samplerLoss.executionAvailable);
        expect(samplerLoss.diagnostic, "RHI-WEBGL1-SAMPLER-LOSS-JOIN-UNAVAILABLE");
    }
    for (auto version : {1u, 2u, 3u}) {
        const auto descriptor =
            OriginalDescriptorNativeLossRegistrationContract::lowering(backend, version);
        assert(!descriptor.validatorOnly && !descriptor.registrationAvailable
            && !descriptor.executionAvailable);
        expect(descriptor.diagnostic, "RHI-WEBGL1-DESCRIPTOR-LOSS-JOIN-UNAVAILABLE");
    }
}

static void verifyExistingBackendDecisions()
{
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        const bool validator = backend == Backend::Null, webgpu = backend == Backend::WebGPU;
        const auto lifetime = OriginalDeviceLifetimeContract::lowering(backend);
        assert(lifetime.contractVersion == 1u && lifetime.mode == CapabilityMode::Equivalent
            && lifetime.logicalLifetimeAvailable && !lifetime.resourceIssuerAvailable
            && !lifetime.nativeLossObserved && !lifetime.executionAvailable
            && !lifetime.diagnostic);
        for (auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
            const auto registration =
                OriginalResourceRegistrationContract::lowering(backend, version);
            assert(registration.contractVersion == version
                && registration.mode == CapabilityMode::Equivalent
                && registration.typedLifetimeIssuerAvailable
                && !registration.originalStateIssuerAvailable && !registration.nativeLossObserved
                && !registration.executionAvailable && !registration.diagnostic);
            assert(registration.pipelineLifetimeIssuerAvailable == (version >= 2u)
                && registration.descriptorLifetimeIssuerAvailable == (version >= 3u)
                && registration.fullPipelineDefinitionCustodyAvailable == (version >= 4u));
        }
        const auto sampler =
            OriginalResourceRegistrationContract::samplerLowering(backend, {1u, true, 1u});
        assert(sampler.mode == CapabilityMode::Equivalent && sampler.typedLifetimeIssuerAvailable
            && !sampler.originalStateIssuerAvailable && !sampler.nativeLossObserved
            && !sampler.executionAvailable && !sampler.diagnostic);
        const auto reference =
            OriginalCommandTypedSourceContract::samplerReferenceLowering(backend);
        assert(reference.contractVersion == 1u && reference.mode == CapabilityMode::Equivalent
            && reference.referenceAvailable && !reference.executionAvailable
            && !reference.diagnostic);
        const auto subscription = DeviceLossRegistrationContract::lowering(backend);
        assert(subscription.nativePrototype == webgpu && subscription.validatorOnly == validator);
        assert(subscription.diagnostic);
        const auto loss = OriginalDeviceNativeLossContract::lowering(backend);
        assert(loss.nativePrototype == webgpu && loss.validatorOnly == validator
            && !loss.executionAvailable);
        assert(bool(loss.diagnostic) == !webgpu);
        const auto result = OriginalDeviceNativeResultLossContract::lowering(backend);
        const bool resultBinding = backend == Backend::Vulkan || backend == Backend::Metal;
        assert(result.bindingAvailable == resultBinding && result.validatorOnly == validator
            && !result.executionAvailable);
        assert(bool(result.diagnostic) == !resultBinding);
        const auto observer = OriginalDeviceNativeLossObserverContract::lowering(backend);
        assert(observer.validatorOnly == validator && observer.bindingAvailable == webgpu
            && !observer.observerIssuanceAvailable && !observer.executionAvailable
            && observer.typedColdObserverPreparationAvailable == webgpu && observer.diagnostic);
        for (auto version : {1u, 2u}) {
            const auto typed = OriginalTypedNativeLossJoinContract::lowering(backend, version);
            assert(typed.registrationAvailable == webgpu && typed.validatorOnly == validator
                && typed.acceptedPipelineJoinAvailable == webgpu
                && typed.scopedGroupJoinAvailable == (webgpu && version == 1u)
                && !typed.recordingAvailable && !typed.executionAvailable);
            assert(bool(typed.diagnostic) == !webgpu);
            const auto samplerLoss =
                OriginalSamplerNativeLossJoinContract::lowering(backend, version);
            const bool source = version == 1u ? webgpu : !validator;
            assert(samplerLoss.contractVersion == version && samplerLoss.validatorOnly == validator
                && samplerLoss.nativeSourceAvailable == source && !samplerLoss.recordingAvailable
                && !samplerLoss.executionAvailable);
            assert(bool(samplerLoss.diagnostic) == !source);
        }
        for (auto version : {1u, 2u, 3u}) {
            const auto descriptor =
                OriginalDescriptorNativeLossRegistrationContract::lowering(backend, version);
            const bool available = version == 3u ? webgpu : !validator && !webgpu;
            assert(descriptor.validatorOnly == validator
                && descriptor.registrationAvailable == available && !descriptor.executionAvailable);
            assert(bool(descriptor.diagnostic) == !available);
        }
    }
}

static void verifyRequestAndUnknownGates()
{
    // Generic registration never bypasses its explicit native-loss join.
    for (auto backend :
        {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
        for (auto version : {7u, 8u, 9u}) {
            const auto d = OriginalResourceRegistrationContract::lowering(backend, version);
            assert(!d.typedLifetimeIssuerAvailable && !d.executionAvailable);
            expect(d.diagnostic, "RHI-ORIGINAL-TYPED-LOSS-EXPLICIT-REQUEST-REQUIRED");
        }
        for (auto version : {0u, 10u, UINT32_MAX})
            expect(OriginalResourceRegistrationContract::lowering(backend, version).diagnostic,
                "RHI-ORIGINAL-REGISTRATION-VERSION");
        expect(OriginalResourceRegistrationContract::samplerLowering(backend, {}).diagnostic,
            "RHI-ORIGINAL-SAMPLER-REGISTRATION-OPT-IN");
        expect(OriginalResourceRegistrationContract::samplerLowering(backend, {1u, false, 1u})
                   .diagnostic,
            "RHI-ORIGINAL-SAMPLER-REGISTRATION-OPT-IN");
        expect(OriginalResourceRegistrationContract::samplerLowering(backend, {1u, true, 0u})
                   .diagnostic,
            "RHI-ORIGINAL-SAMPLER-REGISTRATION-CAPACITY");
        for (auto version : {0u, 4u, UINT32_MAX})
            expect(OriginalDescriptorNativeLossRegistrationContract::lowering(backend, version)
                       .diagnostic,
                "RHI-ORIGINAL-DESCRIPTOR-LOSS-VERSION");
        for (auto version : {0u, 3u, UINT32_MAX}) {
            assert(!OriginalTypedNativeLossJoinContract::lowering(backend, version)
                    .registrationAvailable);
            expect(OriginalSamplerNativeLossJoinContract::lowering(backend, version).diagnostic,
                "RHI-ORIGINAL-SAMPLER-LOSS-VERSION");
        }
    }
    constexpr auto unknown = static_cast<Backend>(255u);
    expect(OriginalDeviceLifetimeContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-DEVICE-LIFETIME-BACKEND-UNKNOWN");
    expect(OriginalResourceRegistrationContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-REGISTRATION-BACKEND-UNKNOWN");
    expect(
        OriginalResourceRegistrationContract::samplerLowering(unknown, {1u, true, 1u}).diagnostic,
        "RHI-ORIGINAL-REGISTRATION-BACKEND-UNKNOWN");
    expect(OriginalCommandTypedSourceContract::samplerReferenceLowering(unknown).diagnostic,
        "RHI-ORIGINAL-SAMPLER-REFERENCE-BACKEND-UNKNOWN");
    expect(DeviceLossRegistrationContract::lowering(unknown).diagnostic,
        "RHI-DEVICE-LOSS-REGISTRATION-BACKEND");
    expect(OriginalDeviceNativeLossContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-DEVICE-LOSS-BACKEND-UNKNOWN");
    expect(OriginalDeviceNativeResultLossContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-RESULT-LOSS-BACKEND-UNKNOWN");
    expect(OriginalDeviceNativeLossObserverContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-LOSS-OBSERVER-BACKEND");
    for (auto version : {1u, 2u}) {
        expect(OriginalTypedNativeLossJoinContract::lowering(unknown, version).diagnostic,
            "RHI-ORIGINAL-TYPED-LOSS-BACKEND-UNKNOWN");
        expect(OriginalSamplerNativeLossJoinContract::lowering(unknown, version).diagnostic,
            "RHI-ORIGINAL-SAMPLER-LOSS-BACKEND-UNKNOWN");
    }
    for (auto version : {1u, 2u, 3u})
        expect(
            OriginalDescriptorNativeLossRegistrationContract::lowering(unknown, version).diagnostic,
            "RHI-ORIGINAL-DESCRIPTOR-LOSS-BACKEND");
}

int main()
{
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    verifyWebGlRefusal();
    verifyExistingBackendDecisions();
    verifyRequestAndUnknownGates();
    test::WebGlResourceContentLoweringFixture::verify();
    test::WebGlCommandStateLoweringFixture::verify();
    assert(core::defaultCpuAllocator().stats().allocationCount == before);
}
