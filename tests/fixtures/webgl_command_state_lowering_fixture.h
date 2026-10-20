#pragma once

#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/semantics/command_state_lowering.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
class WebGlCommandStateLoweringFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using State = semantics::OriginalCommandStateAdmissionLowering;
        using Geometry = semantics::GeometryInputAccessLowering;
        constexpr Backend backends[]{
            Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL};
        for (auto backend : backends) {
            const bool null = backend == Backend::Null, webgl = backend == Backend::WebGL;
            for (auto version : {1u, 2u, 3u}) {
                const auto d = OriginalCompletedDepthReadInputContract::lowering(backend, version);
                const bool input = ((version == 2u || version == 3u)
                                       && (backend == Backend::Vulkan || backend == Backend::Metal))
                    || (version == 1u && backend == Backend::WebGPU);
                assert(d.validatorOnly == null && d.nativeInputValidationAvailable == input
                    && !d.executionAvailable);
                assert(bool(d.diagnostic) == !input);
                if (webgl)
                    expect(d.diagnostic, "RHI-WEBGL1-COMPLETED-DEPTH-READ-INPUT-UNAVAILABLE");
            }
            for (auto version : {0u, 4u, UINT32_MAX}) {
                const auto d = OriginalCompletedDepthReadInputContract::lowering(backend, version);
                assert(
                    !d.validatorOnly && !d.nativeInputValidationAvailable && !d.executionAvailable);
                expect(d.diagnostic, "RHI-COMPLETED-DEPTH-READ-INPUT-VERSION");
            }
            const auto geometry = Geometry::geometryInputAccessLowering(backend);
            const bool mapping = null || backend == Backend::Vulkan;
            const auto mode = null           ? CapabilityMode::Equivalent
                : backend == Backend::Vulkan ? CapabilityMode::Native
                                             : CapabilityMode::DiagnosticFailure;
            assert(geometry.contractVersion == 1u && geometry.mode == mode
                && geometry.validatorOnly == null && geometry.barrierMappingAvailable == mapping);
            assert(bool(geometry.diagnostic) == !mapping);
            if (webgl)
                expect(geometry.diagnostic, "RHI-WEBGL1-GEOMETRY-ACCESS-UNAVAILABLE");

            struct Barrier {
                PipelineStage sourceStages = PipelineStage::None,
                              destinationStages = PipelineStage::None;
                ResourceAccess sourceAccess = ResourceAccess::None,
                               destinationAccess = ResourceAccess::None;
            };

            Barrier barrier;
            assert(!Geometry::geometryInputBarrierDiagnostic(backend, barrier));
            barrier.sourceStages = PipelineStage::Transfer;
            barrier.destinationAccess = ResourceAccess::TransferWrite;
            assert(!Geometry::geometryInputBarrierDiagnostic(backend, barrier));
            barrier.destinationStages = PipelineStage::VertexInput;
            assert(
                Geometry::geometryInputBarrierDiagnostic(backend, barrier) == geometry.diagnostic);
            barrier.destinationStages = PipelineStage::None;
            for (auto access : {ResourceAccess::VertexAttributeRead, ResourceAccess::IndexRead}) {
                barrier.sourceAccess = access;
                assert(Geometry::geometryInputBarrierDiagnostic(backend, barrier)
                    == geometry.diagnostic);
                barrier.sourceAccess = ResourceAccess::None;
                barrier.destinationAccess = access;
                assert(Geometry::geometryInputBarrierDiagnostic(backend, barrier)
                    == geometry.diagnostic);
            }
            const auto state = State::lowering(backend);
            assert(state.contractVersion == 1u && !state.originalIssuerAvailable
                && !state.executionAvailable);
            assert(state.disposition
                == (null ? State::Disposition::ValidatorOnly
                         : State::Disposition::DiagnosticFailure));
            if (webgl)
                expect(state.diagnostic, "RHI-WEBGL1-ORIGINAL-STATE-UNAVAILABLE");
            OriginalCommandStateAdmissionBudget budget;
            expect(State::admissionDiagnostic(backend, budget), "RHI-ORIGINAL-STATE-VERSION");
            budget.contractVersion = 1u;
            expect(State::admissionDiagnostic(backend, budget), "RHI-ORIGINAL-STATE-OPT-IN");
            budget.prototypeOptIn = true;
            expect(State::admissionDiagnostic(backend, budget), "RHI-ORIGINAL-STATE-DEVICE-DOMAIN");
            budget.maxDeviceDomains = 1u;
            budget.maxResourceRegistrations = 1u;
            budget.maxRetainedOwners = 1u;
            budget.maxCpuMetadataBytes = 1u;
            assert(State::admissionDiagnostic(backend, budget) == state.diagnostic);
        }
        constexpr auto unknown = static_cast<Backend>(255u);
        expect(OriginalCompletedDepthReadInputContract::lowering(unknown).diagnostic,
            "RHI-COMPLETED-DEPTH-READ-INPUT-BACKEND");
        expect(Geometry::geometryInputAccessLowering(unknown).diagnostic,
            "RHI-GEOMETRY-ACCESS-BACKEND");
        expect(State::lowering(unknown).diagnostic, "RHI-ORIGINAL-STATE-BACKEND-UNKNOWN");
        verifyCompositeStates();
        verifyContentPlans();
    }

private:
    static void verifyCompositeStates()
    {
        using namespace rhi;
        using State = semantics::OriginalCommandStateAdmissionLowering;
        using Consumption = semantics::OriginalCommandDescriptorConsumptionLowering;
        using Admission = semantics::OriginalCommandAdmissionLowering;
        constexpr Backend backends[]{
            Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL};
        for (auto backend : backends) {
            const bool null = backend == Backend::Null, webgpu = backend == Backend::WebGPU,
                       webgl = backend == Backend::WebGL;
            const auto indexed = Consumption::indexedDrawLowering(backend);
            assert(indexed.mode
                    == (webgpu ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure)
                && indexed.validatorOnly == null && indexed.projectionAvailable == webgpu
                && !indexed.stateAvailable && !indexed.executionAvailable);
            if (webgl)
                expect(indexed.diagnostic, "RHI-WEBGL1-INDEXED-CONSUMPTION-UNAVAILABLE");
            const auto vertex = Consumption::vertexStreamShaderLowering(backend);
            assert(vertex.mode == indexed.mode && vertex.validatorOnly == null
                && vertex.projectionAvailable == webgpu && !vertex.stateAvailable
                && !vertex.executionAvailable);
            if (webgl)
                expect(vertex.diagnostic, "RHI-WEBGL1-VERTEX-SHADER-CONSUMPTION-UNAVAILABLE");
            for (auto version : {0u, 2u, UINT32_MAX})
                expect(Consumption::vertexStreamShaderLowering(backend, version).diagnostic,
                    "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-VERSION");
            for (auto version : {1u, 2u, 3u, 4u}) {
                const auto consumption = Consumption::lowering(backend, version);
                const auto typed = State::typedLowering(backend, version + 1u);
                assert(consumption.mode
                        == (webgpu ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure)
                    && consumption.validatorOnly == null
                    && consumption.projectionAvailable == webgpu && !consumption.stateAvailable
                    && !consumption.executionAvailable);
                assert(typed.contractVersion == version + 1u && typed.mode == consumption.mode
                    && typed.validatorOnly == null && typed.projectionAvailable == webgpu
                    && typed.stateAvailable == webgpu && !typed.executionAvailable
                    && !typed.retainedReadbackStateAvailable);
                assert(typed.diagnostic == consumption.diagnostic);
                if (webgl)
                    expect(consumption.diagnostic, "RHI-WEBGL1-ORIGINAL-CONSUMPTION-UNAVAILABLE");
            }
            const auto admission = Admission::lowering(backend);
            assert(admission.contractVersion == 1u && !admission.executionAvailable
                && admission.disposition
                    == (null ? Admission::Disposition::ValidatorOnly
                             : Admission::Disposition::DiagnosticFailure));
            if (webgl)
                expect(admission.diagnostic, "RHI-WEBGL1-ORIGINAL-COMMAND-UNAVAILABLE");
            for (auto version : {1u, 2u, 3u, 4u, 5u}) {
                const auto d = State::bindlessRootLowering(backend, version);
                const bool projection =
                    !webgl && (version != 5u || null || backend == Backend::Vulkan);
                assert(d.contractVersion == version
                    && d.mode
                        == (projection ? CapabilityMode::Equivalent
                                       : CapabilityMode::DiagnosticFailure)
                    && d.validatorOnly == null && d.projectionAvailable == projection
                    && !d.executionAvailable);
                if (webgl)
                    expect(d.diagnostic, "RHI-WEBGL1-BINDLESS-STATE-UNAVAILABLE");
            }
            for (auto stateVersion : {3u, 4u}) {
                const auto d = State::typedReadbackLowering(backend, 1u, stateVersion);
                assert(d.contractVersion == stateVersion
                    && d.mode
                        == (null || webgpu ? CapabilityMode::Equivalent
                                           : CapabilityMode::DiagnosticFailure)
                    && d.validatorOnly == null && d.projectionAvailable == webgpu
                    && d.stateAvailable == webgpu && d.retainedReadbackStateAvailable == webgpu
                    && !d.executionAvailable);
                if (webgl)
                    expect(d.diagnostic, "RHI-WEBGL1-TYPED-READBACK-STATE-UNAVAILABLE");
            }
            for (auto version : {3u, 5u}) {
                const auto d = State::textureTransactionLowering(backend, version);
                assert(d.contractVersion == version
                    && d.mode
                        == (null || webgpu ? CapabilityMode::Equivalent
                                           : CapabilityMode::DiagnosticFailure)
                    && d.validatorOnly == null && d.ownershipAvailable == webgpu
                    && d.planAvailable == webgpu && !d.executionAvailable);
                if (webgl)
                    expect(d.diagnostic, "RHI-WEBGL1-TEXTURE-TRANSACTION-UNAVAILABLE");
            }
            for (auto version : {0u, 6u, UINT32_MAX}) {
                expect(State::bindlessRootLowering(backend, version).diagnostic,
                    "RHI-ORIGINAL-BINDLESS-STATE-VERSION");
                expect(State::typedLowering(backend, version).diagnostic,
                    "RHI-ORIGINAL-TYPED-STATE-VERSION");
                expect(State::textureTransactionLowering(backend, version).diagnostic,
                    "RHI-ORIGINAL-TEXTURE-TRANSACTION-VERSION");
                expect(Consumption::lowering(backend, version).diagnostic,
                    "RHI-ORIGINAL-CONSUMPTION-VERSION");
            }
            for (auto version : {0u, 2u, UINT32_MAX})
                expect(State::typedReadbackLowering(backend, version).diagnostic,
                    "RHI-ORIGINAL-STATE-READBACK-VERSION");
            for (auto version : {0u, 1u, 2u, 5u, UINT32_MAX})
                expect(State::typedReadbackLowering(backend, 1u, version).diagnostic,
                    "RHI-ORIGINAL-STATE-READBACK-VERSION");
        }
        constexpr auto unknown = static_cast<Backend>(255u);
        expect(
            State::bindlessRootLowering(unknown).diagnostic, "RHI-ORIGINAL-BINDLESS-STATE-BACKEND");
        expect(State::typedReadbackLowering(unknown).diagnostic,
            "RHI-ORIGINAL-TYPED-READBACK-STATE-BACKEND");
        expect(State::textureTransactionLowering(unknown).diagnostic,
            "RHI-ORIGINAL-TEXTURE-TRANSACTION-BACKEND");
        expect(Consumption::lowering(unknown).diagnostic, "RHI-ORIGINAL-CONSUMPTION-BACKEND");
        expect(Consumption::vertexStreamShaderLowering(unknown).diagnostic,
            "RHI-ORIGINAL-CONSUMPTION-BACKEND");
        expect(Admission::lowering(unknown).diagnostic, "RHI-ORIGINAL-COMMAND-BACKEND-UNKNOWN");
    }

    static void verifyContentPlans()
    {
        using namespace rhi;
        using State = semantics::OriginalCommandStateAdmissionLowering;
        constexpr Backend backends[]{
            Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL};
        for (auto backend : backends) {
            const bool null = backend == Backend::Null, webgpu = backend == Backend::WebGPU,
                       webgl = backend == Backend::WebGL;
            const bool vulkan = backend == Backend::Vulkan, metal = backend == Backend::Metal;
            for (auto version : {2u, 3u, 4u}) {
                const auto d = State::textureContentSequenceLowering(backend, version);
                const bool validation = null || (webgpu && version != 4u);
                assert(d.contractVersion == version
                    && d.mode
                        == (validation ? CapabilityMode::Equivalent
                                       : CapabilityMode::DiagnosticFailure)
                    && d.validatorOnly == null && d.validationAvailable == validation
                    && !d.executionAvailable);
                if (webgl)
                    expect(d.diagnostic, "RHI-WEBGL1-CONTENT-SEQUENCE-UNAVAILABLE");
            }
            for (auto version : {2u, 3u, 7u, 11u, 12u, 13u, 14u, 15u, 16u}) {
                const auto d = State::textureContentPlanLowering(backend, version);
                const bool validation = (null && version != 2u && version != 3u)
                    || (vulkan
                        && (version == 7u || version == 12u || version == 13u || version == 15u))
                    || (metal && (version == 12u || version == 13u || version == 15u))
                    || (webgpu
                        && (version == 2u || version == 3u || version == 11u || version == 14u
                            || version == 16u));
                assert(d.contractVersion == version
                    && d.mode
                        == (null || validation ? CapabilityMode::Equivalent
                                               : CapabilityMode::DiagnosticFailure)
                    && d.validatorOnly == null && d.validationAvailable == validation
                    && !d.executionAvailable);
                if (webgl)
                    expect(d.diagnostic, "RHI-WEBGL1-CONTENT-PLAN-UNAVAILABLE");
            }
            for (auto geometry : {0u, 1u}) {
                const auto d = State::bindlessGraphicsContentPlanLowering(backend, geometry);
                const bool valid = null || vulkan || ((metal || webgpu) && geometry == 0u);
                assert(d.contractVersion == 9u
                    && d.mode
                        == (valid ? CapabilityMode::Equivalent : CapabilityMode::DiagnosticFailure)
                    && d.validatorOnly == null && d.validationAvailable == valid
                    && d.rootAvailable == valid && !d.executionAvailable);
                if (webgl)
                    expect(d.diagnostic, "RHI-WEBGL1-BINDLESS-GRAPHICS-CONTENT-UNAVAILABLE");
            }
            const auto mixed = State::finiteGraphicsContentPlanLowering(backend);
            assert(mixed.contractVersion == 6u
                && mixed.mode
                    == (null || webgpu ? CapabilityMode::Equivalent
                                       : CapabilityMode::DiagnosticFailure)
                && mixed.validatorOnly == null && mixed.validationAvailable == (null || webgpu)
                && mixed.rootAvailable == webgpu && !mixed.executionAvailable);
            if (webgl)
                expect(mixed.diagnostic, "RHI-WEBGL1-FINITE-GRAPHICS-CONTENT-UNAVAILABLE");
            for (auto version : {1u, 2u, 3u}) {
                const auto completed = State::bindlessCompletedReadPlanLowering(backend, version);
                const bool validation = null || (version == 2u ? vulkan || metal : webgpu);
                assert(completed.contractVersion == 9u
                    && completed.mode
                        == (validation ? CapabilityMode::Equivalent
                                       : CapabilityMode::DiagnosticFailure)
                    && completed.validatorOnly == null
                    && completed.validationAvailable == validation
                    && completed.rootAvailable == validation && !completed.executionAvailable);
                if (webgl)
                    expect(completed.diagnostic,
                        version == 3u ? "RHI-WEBGL1-BINDLESS-CANONICAL-COLOR-UNAVAILABLE"
                                      : "RHI-WEBGL1-BINDLESS-COMPLETED-READ-UNAVAILABLE");
            }
            for (auto version : {4u, 5u, 8u, 10u}) {
                const auto d = State::textureCompletedReadPlanLowering(backend, version);
                const bool validation = null || webgpu || ((vulkan || metal) && version != 10u);
                assert(d.contractVersion == version
                    && d.mode
                        == (validation ? CapabilityMode::Equivalent
                                       : CapabilityMode::DiagnosticFailure)
                    && d.validatorOnly == null && d.validationAvailable == validation
                    && !d.executionAvailable);
                if (webgl)
                    expect(d.diagnostic,
                        version == 10u ? "RHI-WEBGL1-BINDLESS-COMPLETED-READ-UNAVAILABLE"
                                       : "RHI-WEBGL1-COMPLETED-TEXTURE-READ-PLAN-UNAVAILABLE");
            }
            for (auto version : {0u, 1u, 5u, UINT32_MAX})
                expect(State::textureContentSequenceLowering(backend, version).diagnostic,
                    "RHI-ORIGINAL-CONTENT-SEQUENCE-VERSION");
            for (auto version : {0u, 1u, 4u, 5u, 6u, 8u, 9u, 10u, 17u, UINT32_MAX})
                expect(State::textureContentPlanLowering(backend, version).diagnostic,
                    "RHI-CANONICAL-CONTENT-PLAN-VERSION");
            for (auto version : {0u, 1u, 2u, 3u, 6u, 7u, 9u, 11u, UINT32_MAX})
                expect(State::textureCompletedReadPlanLowering(backend, version).diagnostic,
                    "RHI-COMPLETED-TEXTURE-READ-PLAN-VERSION");
            for (auto version : {0u, 1u, 5u, 7u, UINT32_MAX})
                expect(State::finiteGraphicsContentPlanLowering(backend, version).diagnostic,
                    "RHI-FINITE-GRAPHICS-CONTENT-VERSION");
            for (auto version : {2u, UINT32_MAX})
                expect(State::bindlessGraphicsContentPlanLowering(backend, version).diagnostic,
                    "RHI-BINDLESS-GRAPHICS-CONTENT-GEOMETRY-VERSION");
            for (auto version : {0u, 4u, UINT32_MAX})
                expect(State::bindlessCompletedReadPlanLowering(backend, version).diagnostic,
                    "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-VERSION");
        }
        constexpr auto unknown = static_cast<Backend>(255u);
        assert(!State::textureContentSequenceLowering(unknown).executionAvailable);
        assert(!State::textureContentPlanLowering(unknown).validationAvailable);
        assert(!State::bindlessGraphicsContentPlanLowering(unknown).rootAvailable);
        assert(!State::bindlessCompletedReadPlanLowering(unknown).validationAvailable);
        assert(!State::finiteGraphicsContentPlanLowering(unknown).rootAvailable);
        assert(!State::textureCompletedReadPlanLowering(unknown).validationAvailable);
    }

    static void expect(const char* actual, const char* expected)
    {
        if (!actual || std::strcmp(actual, expected) != 0)
            std::fprintf(stderr, "expected %s, got %s\n", expected, actual ? actual : "<null>");
        assert(actual && std::strcmp(actual, expected) == 0);
    }
};
} // namespace arp::test
