#pragma once

#include "rhi/frontend/resource_texture_content_access.h"
#include "rhi/frontend/resource_texture_validation.h"

#include <cassert>
#include <cstring>

namespace arp::test {

class WebGlResourceContentLoweringFixture final {
    static void expect(const char* actual, const char* expected)
    {
        assert(actual && std::strcmp(actual, expected) == 0);
    }

public:
    static void verify()
    {
        using namespace rhi;
        using Content = OriginalTextureContentCreationContract;
        using State = NormalizedTextureStateCreationContract;
        using Staging = OriginalTextureContentStagingContract;
        using Access = semantics::TextureContentAccessPolicy;
        const auto access = Access::lowering(Backend::WebGL);
        assert(!access.validatorOnly && !access.nativeCreationImplemented);
        expect(access.diagnostic, "RHI-WEBGL1-ORIGINAL-TEXTURE-CONTENT-UNAVAILABLE");
        const auto state = State::lowering(Backend::WebGL);
        assert(!state.normalizedControl && !state.validatorOnly && !state.nativeExecutionAvailable);
        expect(state.diagnostic, "RHI-WEBGL1-ORIGINAL-TEXTURE-STATE-UNAVAILABLE");
        const auto staging = Staging::lowering(Backend::WebGL);
        assert(staging.disposition == Staging::Disposition::DiagnosticFailure
            && !staging.prerequisiteAvailable);
        expect(staging.diagnostic, "RHI-WEBGL1-ORIGINAL-TEXTURE-STAGING-UNAVAILABLE");
        for (auto version : {1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
            const auto content = Content::lowering(Backend::WebGL, version);
            assert(content.contractVersion == version
                && content.disposition == Content::Disposition::DiagnosticFailure
                && !content.exclusiveCreationAvailable && !content.nativeContentExecutionAvailable);
            expect(content.diagnostic, "RHI-WEBGL1-ORIGINAL-TEXTURE-CREATION-UNAVAILABLE");
        }

        for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const bool validator = backend == Backend::Null, webgpu = backend == Backend::WebGPU;
            const auto oldAccess = Access::lowering(backend);
            assert(oldAccess.validatorOnly == validator
                && oldAccess.nativeCreationImplemented == webgpu);
            assert(oldAccess.diagnostic);
            const auto oldState = State::lowering(backend);
            assert(oldState.normalizedControl && oldState.validatorOnly == validator
                && !oldState.nativeExecutionAvailable && oldState.diagnostic);
            const auto oldStaging = Staging::lowering(backend);
            const auto disposition = validator ? Staging::Disposition::ValidatorOnly
                : webgpu                       ? Staging::Disposition::Prototype
                                               : Staging::Disposition::DiagnosticFailure;
            assert(oldStaging.disposition == disposition
                && oldStaging.prerequisiteAvailable == webgpu && oldStaging.diagnostic);
            for (auto version : {1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
                const auto content = Content::lowering(backend, version);
                const bool nativeAllocation = version == 3u || version == 4u || version == 6u;
                const bool prototype = !validator && (webgpu != nativeAllocation);
                assert(content.contractVersion == version
                    && content.disposition
                        == (validator       ? Content::Disposition::ValidatorOnly
                                : prototype ? Content::Disposition::Prototype
                                            : Content::Disposition::DiagnosticFailure)
                    && content.exclusiveCreationAvailable == prototype
                    && !content.nativeContentExecutionAvailable && content.diagnostic);
            }
        }

        constexpr auto unknown = static_cast<Backend>(255u);
        expect(Access::lowering(unknown).diagnostic, "RHI-TEXTURE-DESTINATION-BACKEND-UNKNOWN");
        const auto unknownState = State::lowering(unknown);
        assert(!unknownState.normalizedControl && !unknownState.validatorOnly
            && !unknownState.nativeExecutionAvailable);
        expect(unknownState.diagnostic, "RHI-TEXTURE-STATE-BACKEND-UNKNOWN");
        expect(
            Staging::lowering(unknown).diagnostic, "RHI-CANONICAL-CONTENT-STAGING-BACKEND-UNKNOWN");
        for (auto backend : {Backend::Null,
                 Backend::Vulkan,
                 Backend::Metal,
                 Backend::WebGPU,
                 Backend::WebGL,
                 unknown}) {
            for (auto version : {0u, 8u, UINT32_MAX}) {
                const auto invalid = Content::lowering(backend, version);
                assert(invalid.disposition == Content::Disposition::DiagnosticFailure
                    && !invalid.exclusiveCreationAvailable
                    && !invalid.nativeContentExecutionAvailable);
                expect(invalid.diagnostic, "RHI-CANONICAL-CONTENT-CREATION-BACKEND-UNKNOWN");
            }
        }
        for (auto version : {1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
            const auto content = Content::lowering(unknown, version);
            assert(content.disposition == Content::Disposition::DiagnosticFailure
                && !content.exclusiveCreationAvailable && !content.nativeContentExecutionAvailable);
            expect(content.diagnostic, "RHI-CANONICAL-CONTENT-CREATION-BACKEND-UNKNOWN");
        }
    }
};

} // namespace arp::test
