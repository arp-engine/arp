#include "original_typed_issuer_preparation_fixture.h"
#include "rhi/frontend/command_shader_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Graphics Issuer8 value contracts require assertions
#endif

namespace {
using namespace arp::rhi;
using C = OriginalTypedIssuerPreparationContract;
constexpr std::uint32_t rootOwners = 46u;

void diagnostic(const char* actual, const char* expected)
{
    if (!actual || std::strcmp(actual, expected))
        std::fprintf(stderr, "issuer8 expected %s got %s\n", expected, actual ? actual : "success");
    assert(actual && !std::strcmp(actual, expected));
}
} // namespace

int main()
{
    arp::test::OriginalTypedIssuerPreparationFixture::verifyContract();
    constexpr std::uint32_t callbackOwners = 252u; // Existing Callback8 declared workload demand.
    constexpr auto requiredOwners = callbackOwners + C::additionalOwners;
    C::Request request{8u, true, 1u, 1u, 7u, requiredOwners, 65536u, 65536u, 65536u};
    assert(C::additionalOwners == 3u && !C::validateIntent(request));
    assert(!C::validateProfile(request, 8u));
    for (std::uint32_t version = 0u; version <= 9u; ++version) {
        if (version != 8u)
            diagnostic(C::validateProfile(request, version),
                "RHI-TYPED-ISSUER-PREPARATION-CALLBACK-PROFILE");
    }
    for (auto version : {0u, 9u, UINT32_MAX}) {
        auto bad = request;
        bad.contractVersion = version;
        diagnostic(C::validateIntent(bad), "RHI-TYPED-ISSUER-PREPARATION-VERSION");
    }
    auto bad = request;
    bad.prototypeOptIn = false;
    diagnostic(C::validateIntent(bad), "RHI-TYPED-ISSUER-PREPARATION-OPT-IN");
    for (auto member : std::array{&C::Request::maxRecordingIssuers,
             &C::Request::maxSubmissionIssuers,
             &C::Request::maxNativeReceiptSlots}) {
        for (auto value : {0u, 2u, 8u, UINT32_MAX}) {
            bad = request;
            bad.*member = value;
            diagnostic(C::validateIntent(bad), "RHI-TYPED-ISSUER-PREPARATION-PARTICIPANTS");
        }
    }
    for (auto member : std::array{&C::Request::maxRecordingCpuBytes,
             &C::Request::maxSubmissionCpuBytes,
             &C::Request::maxCallbackCpuBytes}) {
        bad = request;
        bad.*member = 0u;
        diagnostic(C::validateIntent(bad), "RHI-TYPED-ISSUER-PREPARATION-BUDGET");
    }
    bad = request;
    bad.maxRetainedOwners = 0u;
    diagnostic(C::validateIntent(bad), "RHI-TYPED-ISSUER-PREPARATION-BUDGET");

    OriginalCommandAdmissionBudget source;
    source.maxGraphOwners = 48u; // Same independent CPU root-domain fixture ceiling.
    OriginalCommandNativeLifecycleBudget lifecycle{5u,
        true,
        1u,
        8u,
        8u,
        requiredOwners,
        requiredOwners,
        requiredOwners,
        65536u,
        65536u,
        65536u};
    const auto owners = [&](const C::Request& intent,
                            std::uint32_t callbackCeiling,
                            std::uint32_t commandCeiling,
                            std::uint32_t admissionCeiling,
                            const OriginalCommandNativeLifecycleBudget& life,
                            std::uint32_t cpuRoot = rootOwners) {
        return C::validateOwners(callbackOwners,
            intent,
            callbackCeiling,
            commandCeiling,
            admissionCeiling,
            source,
            life,
            cpuRoot);
    };
    assert(!owners(request, requiredOwners, requiredOwners, requiredOwners, lifecycle));
    // The observed Callback-only ceiling cannot be retroactively enlarged.
    diagnostic(owners(request, callbackOwners, requiredOwners, requiredOwners, lifecycle),
        "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
    diagnostic(owners(request, requiredOwners, callbackOwners, requiredOwners, lifecycle),
        "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
    diagnostic(owners(request, requiredOwners, requiredOwners, callbackOwners, lifecycle),
        "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
    bad = request;
    --bad.maxRetainedOwners;
    diagnostic(owners(bad, requiredOwners, requiredOwners, requiredOwners, lifecycle),
        "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
    bad = request;
    ++bad.maxRetainedOwners;
    diagnostic(owners(bad, requiredOwners, requiredOwners, requiredOwners, lifecycle),
        "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
    for (auto member : std::array{&OriginalCommandNativeLifecycleBudget::maxRecordingOwners,
             &OriginalCommandNativeLifecycleBudget::maxSubmissionOwners,
             &OriginalCommandNativeLifecycleBudget::maxCallbackOwners}) {
        auto shortLife = lifecycle;
        --(shortLife.*member);
        diagnostic(owners(request, requiredOwners, requiredOwners, requiredOwners, shortLife),
            "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
    }
    for (auto version : {0u, 1u, 3u, 4u, 6u}) {
        auto mixed = lifecycle;
        mixed.contractVersion = version;
        diagnostic(owners(request, requiredOwners, requiredOwners, requiredOwners, mixed),
            "RHI-WEBGPU-TYPED-ISSUER-LIFECYCLE-PROFILE");
    }
    auto disabledLife = lifecycle;
    disabledLife.prototypeOptIn = false;
    diagnostic(owners(request, requiredOwners, requiredOwners, requiredOwners, disabledLife),
        "RHI-WEBGPU-TYPED-ISSUER-LIFECYCLE-PROFILE");
    for (auto root : {0u, 49u, UINT32_MAX})
        diagnostic(owners(request, requiredOwners, requiredOwners, requiredOwners, lifecycle, root),
            "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
    diagnostic(C::validateOwners(UINT32_MAX,
                   request,
                   requiredOwners,
                   requiredOwners,
                   requiredOwners,
                   source,
                   lifecycle,
                   rootOwners),
        "RHI-WEBGPU-TYPED-ISSUER-OWNER-OVERFLOW");

    struct BackendCase {
        Backend backend;
        const char* diagnostic;
    };

    constexpr BackendCase backends[]{
        {Backend::Null, "RHI-ORIGINAL-GRAPHICS-ISSUER-NULL-VALIDATOR-ONLY"},
        {Backend::Vulkan, "RHI-ORIGINAL-GRAPHICS-ISSUER-VULKAN-JOIN-MISSING"},
        {Backend::Metal, "RHI-ORIGINAL-GRAPHICS-ISSUER-METAL-JOIN-MISSING"},
        {Backend::WebGPU, "RHI-ORIGINAL-GRAPHICS-ISSUER-WEBGPU-NATIVE-JOIN-MISSING"}};
    for (const auto& item : backends) {
        const auto backend = item.backend;
        const auto decision =
            arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 8u);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(!decision.preparationAvailable && !decision.recordingAvailable
            && !decision.submissionAvailable);
        diagnostic(decision.diagnostic, item.diagnostic);
        for (auto version : {0u, 9u}) {
            const auto rejected =
                arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(
                    backend, version);
            diagnostic(rejected.diagnostic, "RHI-TYPED-ISSUER-PREPARATION-VERSION");
            assert(!rejected.preparationAvailable && !rejected.recordingAvailable
                && !rejected.submissionAvailable);
        }
        const auto execution =
            arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 8u);
        assert(!execution.recordingAvailable && !execution.submissionAvailable);
        for (std::uint32_t version = 1u; version <= 7u; ++version) {
            auto legacy = request;
            legacy.contractVersion = version;
            assert(!C::validateIntent(legacy) && !C::validateProfile(legacy, version));
            const auto old = arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(
                backend, version);
            assert(old.validatorOnly == (backend == Backend::Null));
            assert(old.preparationAvailable == (backend == Backend::WebGPU));
            assert(!old.recordingAvailable && !old.submissionAvailable);
        }
    }
    const auto unknown = arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(
        static_cast<Backend>(UINT32_MAX), 8u);
    assert(!unknown.validatorOnly && !unknown.preparationAvailable && !unknown.recordingAvailable
        && !unknown.submissionAvailable);
    diagnostic(unknown.diagnostic, "RHI-TYPED-ISSUER-PREPARATION-BACKEND");
    OriginalTypedNativeExecutionRequest execute;
    execute.contractVersion = 8u;
    execute.prototypeOptIn = true;
    execute.synchronizationPolicy =
        OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1;
    diagnostic(OriginalTypedNativeExecutionContract::validateIntent(execute),
        "RHI-TYPED-NATIVE-EXECUTION-VERSION");
    std::puts(
        "Graphics Issuer8 candidate values/four-backend refusals passed; no native preparation or execution");
}
