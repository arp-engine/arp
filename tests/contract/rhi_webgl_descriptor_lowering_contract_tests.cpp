#include "rhi/frontend/descriptor_completed_texture.h"
#include "rhi/frontend/descriptor_general_binding_validation.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/frontend/descriptor_original_buffer_range.h"
#include "rhi/frontend/descriptor_table_publication.h"
#include "rhi/frontend/descriptor_texture_cell.h"
#include "rhi/frontend/descriptor_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef NDEBUG
#error Descriptor lowering contract tests require assertions
#endif

using namespace arp;
using namespace arp::rhi;

namespace {
constexpr std::array backends{Backend::Null,
    Backend::Vulkan,
    Backend::Metal,
    Backend::WebGPU,
    Backend::WebGL,
    static_cast<Backend>(255u)};

void expect(const char* actual, const char* expected)
{
    if (!actual || std::strcmp(actual, expected) != 0)
        std::fprintf(stderr, "expected %s, got %s\n", expected, actual ? actual : "<null>");
    assert(actual && std::strcmp(actual, expected) == 0);
}

// These are decisions of the real frontend contracts, never mock GPU objects.
void verifyOriginalTablePublicationBoundary()
{
    for (auto backend : backends) {
        const auto decision =
            semantics::OriginalCommandDescriptorBindingLowering::tableLowering(backend);
        assert(decision.mode == CapabilityMode::DiagnosticFailure);
        assert(decision.validatorOnly == (backend == Backend::Null));
        assert(!decision.executionAvailable && !decision.positionalAvailable);
        expect(decision.diagnostic,
            backend == Backend::WebGL ? "RHI-WEBGL1-ORIGINAL-TYPED-INPUT-TABLE-UNAVAILABLE"
                                      : "RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE");
    }
}

void verifyPublication()
{
    for (auto backend : backends) {
        const bool validator = backend == Backend::Null;
        const auto buffer = OriginalBufferRangeDescriptorContract::lowering(backend);
        assert(buffer.validatorOnly == validator && !buffer.shaderExecutionAvailable);
        assert(buffer.publicationAvailable
            == (validator || backend == Backend::Vulkan || backend == Backend::WebGPU));
        if (backend == Backend::WebGL)
            expect(buffer.diagnostic, "RHI-WEBGL1-BUFFER-RANGE-DESCRIPTOR-UNAVAILABLE");
        for (std::uint32_t version = 1u; version <= 4u; ++version) {
            if (version <= 4u) {
                const auto cell = OriginalTextureCellDescriptorContract::lowering(backend, version);
                assert(cell.validatorOnly == validator && !cell.shaderExecutionAvailable);
                const bool available = version == 1u || version == 3u
                    ? (backend == Backend::Vulkan || backend == Backend::Metal)
                    : backend == Backend::WebGPU;
                assert(cell.publicationAvailable == available);
                assert((cell.diagnostic == nullptr) == available);
                if (backend == Backend::WebGL)
                    expect(cell.diagnostic, "RHI-WEBGL1-TEXTURE-CELL-DESCRIPTOR-UNAVAILABLE");
            }
            const auto completed =
                OriginalCompletedTextureDescriptorContract::lowering(backend, version);
            assert(completed.validatorOnly == validator && !completed.shaderExecutionAvailable);
            const bool available = version == 1u || version == 4u
                ? (backend == Backend::Vulkan || backend == Backend::Metal)
                : backend == Backend::WebGPU;
            assert(completed.publicationAvailable == available);
            assert((completed.diagnostic == nullptr) == available);
            if (backend == Backend::WebGL)
                expect(completed.diagnostic, "RHI-WEBGL1-COMPLETED-TEXTURE-DESCRIPTOR-UNAVAILABLE");
        }
    }
}

void verifyMetadata()
{
    for (auto backend : backends) {
        const bool validator = backend == Backend::Null;
        const bool available = backend == Backend::WebGPU;
        const auto binding = OriginalGeneralDescriptorBindingContract::lowering(backend);
        const auto buffer = OriginalDescriptorBufferBindingContract::lowering(backend);
        const auto interval =
            OriginalDescriptorBufferBindingContract::intervalDiagnosticLowering(backend);
        const auto target = OriginalTargetMemoryIntervalContract::lowering(backend);
        for (auto mode : {binding.mode, buffer.mode, interval.mode, target.mode})
            assert(mode
                == (available ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure));
        assert(binding.validatorOnly == validator && buffer.validatorOnly == validator
            && interval.validatorOnly == validator && target.validatorOnly == validator);
        assert(binding.preparationAvailable == available && buffer.captureAvailable == available
            && interval.captureAvailable == available && target.diagnosticAvailable == available);
        assert(!binding.executionAvailable && !buffer.executionAvailable
            && !interval.executionAvailable && !target.executionAvailable
            && !target.writeAvailable);
        if (backend == Backend::WebGL) {
            expect(binding.diagnostic, "RHI-WEBGL1-ORIGINAL-GENERAL-BINDING-UNAVAILABLE");
            expect(buffer.diagnostic, "RHI-WEBGL1-ORIGINAL-DESCRIPTOR-BUFFER-UNAVAILABLE");
            expect(interval.diagnostic, "RHI-WEBGL1-ORIGINAL-RUNTIME-INTERVAL-UNAVAILABLE");
            expect(target.diagnostic, "RHI-WEBGL1-ORIGINAL-TARGET-INTERVAL-UNAVAILABLE");
        }
        for (std::uint32_t version = 1u; version <= 4u; ++version) {
            const auto usage = OriginalRenderDescriptorUsageContract::lowering(backend, version);
            assert(usage.validatorOnly == validator && usage.projectionAvailable == available);
            assert(!usage.stateAvailable && !usage.executionAvailable);
            assert(usage.mode
                == (available ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure));
            if (backend == Backend::WebGL)
                expect(usage.diagnostic, "RHI-WEBGL1-ORIGINAL-RENDER-USAGE-UNAVAILABLE");
        }
    }
}

void verifyWriteFacts()
{
    using Facts = OriginalFiniteDescriptorWriteContract::Facts;
    for (auto backend : backends) {
        // Each native-method fact is independently necessary on the existing
        // WebGPU route. Even all facts together grant WebGL no authority.
        for (unsigned bits = 0u; bits != 32u; ++bits) {
            const Facts facts{bool(bits & 1u),
                bool(bits & 2u),
                bool(bits & 4u),
                bool(bits & 8u),
                bool(bits & 16u)};
            const auto finite = OriginalFiniteDescriptorWriteContract::decide(backend, facts);
            const auto whole = OriginalWholeDescriptorWriteContract::decide(backend, facts);
            const auto general = OriginalGeneralDescriptorWriteContract::decide(backend, facts);
            const auto target = OriginalFiniteTargetMemoryWriteContract::decide(backend, facts);
            const auto table = OriginalDepthTablePublicationContract::decide(backend, facts);
            const bool available = backend == Backend::WebGPU && bits == 31u;
            for (const auto& decision : {finite, whole, general, target, table}) {
                assert(decision.validatorOnly == (backend == Backend::Null));
                assert(decision.preparationAvailable == available && !decision.executionAvailable);
                assert(decision.mode
                    == (available ? CapabilityMode::Equivalent
                                  : CapabilityMode::DiagnosticFailure));
                assert((decision.diagnostic == nullptr) == available);
            }
            if (backend == Backend::WebGL) {
                expect(finite.diagnostic, "RHI-WEBGL1-ORIGINAL-FINITE-WRITE-UNAVAILABLE");
                expect(whole.diagnostic, "RHI-WEBGL1-ORIGINAL-WHOLE-WRITE-UNAVAILABLE");
                expect(general.diagnostic, "RHI-WEBGL1-ORIGINAL-GENERAL-WRITE-UNAVAILABLE");
                expect(target.diagnostic, "RHI-WEBGL1-ORIGINAL-TARGET-FINITE-WRITE-UNAVAILABLE");
                expect(table.diagnostic, "RHI-WEBGL1-ORIGINAL-TABLE-PUBLICATION-UNAVAILABLE");
            }
        }
        const auto source =
            OriginalFiniteDescriptorWriteContract::sourceDiagnosticDecision(backend);
        assert(source.validatorOnly == (backend == Backend::Null) && !source.executionAvailable);
        assert(source.preparationAvailable == (backend == Backend::WebGPU));
        if (backend == Backend::WebGL)
            expect(source.diagnostic, "RHI-WEBGL1-ORIGINAL-FINITE-SOURCE-DIAGNOSTIC-UNAVAILABLE");
    }
}

void verifyIdentityProfiles()
{
    using Profile = OriginalDescriptorNativeIdentityRequest::Profile;
    for (auto backend : backends) {
        for (auto profile : {Profile::None,
                 Profile::FiniteWrite1,
                 Profile::WholeWrite1,
                 Profile::FiniteTargetMemory1,
                 Profile::GeneralWrite1,
                 Profile::GeneralWrite2,
                 static_cast<Profile>(255u)}) {
            const auto identity =
                OriginalDescriptorNativeIdentityContract::lowering(backend, profile);
            const bool knownProfile =
                profile != Profile::None && profile != static_cast<Profile>(255u);
            const bool available = backend == Backend::WebGPU && knownProfile;
            assert(identity.validatorOnly == (backend == Backend::Null)
                && !identity.executionAvailable);
            assert(identity.identityAvailable == available);
            assert(identity.mode
                == (available ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure));
            if (backend == Backend::WebGL)
                expect(identity.diagnostic, "RHI-WEBGL1-ORIGINAL-GROUP-IDENTITY-UNAVAILABLE");
        }
    }
}

void verifyInvalidAndUnknown()
{
    constexpr auto unknown = static_cast<Backend>(255u);
    for (auto backend : backends) {
        for (auto version : {0u, 255u, std::numeric_limits<std::uint32_t>::max()}) {
            expect(OriginalTextureCellDescriptorContract::lowering(backend, version).diagnostic,
                "RHI-TEXTURE-CELL-DESCRIPTOR-VERSION");
            expect(
                OriginalCompletedTextureDescriptorContract::lowering(backend, version).diagnostic,
                "RHI-COMPLETED-TEXTURE-DESCRIPTOR-VERSION");
            expect(OriginalRenderDescriptorUsageContract::lowering(backend, version).diagnostic,
                "RHI-ORIGINAL-RENDER-USAGE-VERSION");
        }
        expect(OriginalTextureCellDescriptorContract::lowering(backend, 5u).diagnostic,
            "RHI-TEXTURE-CELL-DESCRIPTOR-VERSION");
    }
    expect(OriginalTextureCellDescriptorContract::lowering(unknown, 1u).diagnostic,
        "RHI-TEXTURE-CELL-DESCRIPTOR-BACKEND");
    expect(OriginalBufferRangeDescriptorContract::lowering(unknown).diagnostic,
        "RHI-BUFFER-RANGE-DESCRIPTOR-BACKEND");
    expect(OriginalCompletedTextureDescriptorContract::lowering(unknown).diagnostic,
        "RHI-COMPLETED-TEXTURE-DESCRIPTOR-BACKEND");
    expect(OriginalGeneralDescriptorBindingContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-GENERAL-BINDING-BACKEND");
    expect(OriginalTargetMemoryIntervalContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-TARGET-INTERVAL-BACKEND");
    expect(OriginalDescriptorBufferBindingContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-DESCRIPTOR-BUFFER-BACKEND");
    expect(OriginalDescriptorBufferBindingContract::intervalDiagnosticLowering(unknown).diagnostic,
        "RHI-ORIGINAL-RUNTIME-INTERVAL-BACKEND");
    expect(OriginalFiniteDescriptorWriteContract::decide(unknown).diagnostic,
        "RHI-ORIGINAL-FINITE-WRITE-BACKEND");
    expect(OriginalFiniteDescriptorWriteContract::sourceDiagnosticDecision(unknown).diagnostic,
        "RHI-ORIGINAL-FINITE-WRITE-BACKEND");
    expect(OriginalFiniteTargetMemoryWriteContract::decide(unknown).diagnostic,
        "RHI-ORIGINAL-FINITE-WRITE-BACKEND");
    expect(OriginalDescriptorNativeIdentityContract::lowering(
               unknown, OriginalDescriptorNativeIdentityRequest::Profile::None)
               .diagnostic,
        "RHI-ORIGINAL-GROUP-IDENTITY-BACKEND");
    expect(OriginalRenderDescriptorUsageContract::lowering(unknown).diagnostic,
        "RHI-ORIGINAL-RENDER-USAGE-BACKEND-INVALID");
    expect(OriginalGeneralDescriptorWriteContract::decide(unknown).diagnostic,
        "RHI-ORIGINAL-GENERAL-WRITE-BACKEND");
    expect(OriginalWholeDescriptorWriteContract::decide(unknown).diagnostic,
        "RHI-ORIGINAL-WHOLE-WRITE-BACKEND");
    expect(OriginalDepthTablePublicationContract::decide(unknown).diagnostic,
        "RHI-ORIGINAL-FINITE-WRITE-BACKEND");
}
} // namespace

int main()
{
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    verifyOriginalTablePublicationBoundary();
    verifyPublication();
    verifyMetadata();
    verifyWriteFacts();
    verifyIdentityProfiles();
    verifyInvalidAndUnknown();
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
}
