#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/resource_texture_validation.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalTextureContentCellSequenceFixture final {
public:
    static void verify()
    {
        using C = rhi::OriginalTextureContentCellSequence;
        using A = C::Action;
        rhi::TextureDesc desc;
        desc.extent = {8u, 8u, 1u};
        desc.format = rhi::TextureFormat::D32Float;
        desc.mipLevelCount = desc.arrayLayerCount = 3u;
        const char* id = nullptr;
        const auto geometry = rhi::semantics::OriginalTextureStateRange::capture(desc, 9u, id);
        assert(geometry && !id);
        const rhi::GpuResourceId resource{71u};
        const rhi::TextureSubresourceRange range{1u, 1u, 2u, 1u, rhi::TextureAspect::Depth};
        std::uint64_t selected = 0u;
        assert(geometry->cellIndex(range, 0u, selected));
        std::array<C::Step, 3> steps{{{resource, 17u, range, A::Clear, 0u},
            {resource, 17u, range, A::Store, 0u},
            {resource, 17u, range, A::SampledRead, SIZE_MAX}}};
        C::Budget budget{2u, true, 9u, 3u, 3u};
        const auto inspect =
            [&](std::uint64_t cell, bool initial = false, std::size_t before = 3u) noexcept {
                return C::inspect(*geometry,
                    resource,
                    17u,
                    cell,
                    initial,
                    before,
                    steps.size(),
                    budget,
                    [&](std::size_t command) noexcept -> std::optional<C::Step> {
                        return steps[command];
                    });
            };
        for (std::uint64_t cell = 0u; cell < geometry->cellCount(); ++cell) {
            const auto result = inspect(cell);
            assert(!result.diagnostic && result.stored == (cell == selected)
                && !result.attachmentActive && !result.nativeExecutionProven());
        }
        const auto partial = inspect(selected, false, 1u);
        assert(!partial.diagnostic && partial.attachmentActive && partial.tentative
            && !partial.stored);
        const auto reject = [&](const char* expected) {
            const auto result = inspect(selected);
            assert(result.diagnostic && core::StringView(result.diagnostic) == expected
                && !result.stored && !result.attachmentActive && !result.steps
                && !result.commandVisits);
        };
        steps[0].action = A::Load;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-LOAD-UNINITIALIZED");
        assert(!inspect(selected, true).diagnostic && inspect(selected, true).stored);
        steps[0].action = A::DontCare;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
        steps[0].action = A::Clear;
        steps[1].action = A::Discard;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
        steps[1].action = A::SampledRead;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READ-ACTIVE-ATTACHMENT");
        steps[1].action = A::Store;
        steps[1].passBegin = 1u;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-END");
        steps[1].passBegin = 0u;
        steps[0].action = A::Store;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-END");
        steps[0].action = A::Clear;
        steps[0].passBegin = 1u;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-BEGIN");
        steps[0].passBegin = 0u;
        steps[1].resourceGeneration = 18u;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-GENERATION");
        steps[1].resourceGeneration = 17u;
        steps[1].action = static_cast<A>(UINT32_MAX);
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-ACTION");
        steps[1].action = A::Store;
        --budget.maxCells;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-CELL-CAPACITY");
        ++budget.maxCells;
        --budget.maxSteps;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-STEP-CAPACITY");
        ++budget.maxSteps;
        --budget.maxCommandVisits;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-VISIT-CAPACITY");
        ++budget.maxCommandVisits;
        budget.prototypeOptIn = false;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-OPT-IN");
        budget.prototypeOptIn = true;
        budget.contractVersion = 1u;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-VERSION");
        budget.contractVersion = 2u;
        steps[1].resource = {72u};
        steps[2].resource = {72u};
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-UNCLOSED-ATTACHMENT");
        steps[1].resource = steps[2].resource = resource;
        // /3 preserves the original cell timeline and admits a transfer read
        // only after Store, outside the attachment, without adding a write.
        steps[2].action = A::ReadbackRead;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READBACK-PROFILE");
        budget.contractVersion = 3u;
        const auto copied = inspect(selected);
        assert(!copied.diagnostic && copied.stored && copied.writes == 1u && copied.steps == 3u
            && !copied.nativeExecutionProven());
        steps[1].action = A::ReadbackRead;
        steps[1].passBegin = SIZE_MAX;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READ-ACTIVE-ATTACHMENT");
        steps[1].action = A::Store;
        steps[1].passBegin = 0u;
        steps[1].action = A::Discard;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
        steps[1].action = A::Store;
        steps[2].passBegin = 0u;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READBACK-PASS");
        steps[2].passBegin = SIZE_MAX;
        --budget.maxSteps;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-STEP-CAPACITY");
        ++budget.maxSteps;
        budget.prototypeOptIn = false;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-OPT-IN");
        budget.prototypeOptIn = true;
        // Reading a neighboring mip/layer cannot borrow this cell's clear.
        steps[2].range.baseArrayLayer = 1u;
        std::uint64_t untouched = 0u;
        assert(geometry->cellIndex(steps[2].range, 0u, untouched));
        const auto missing = inspect(untouched);
        assert(missing.diagnostic
            && core::StringView(missing.diagnostic)
                == "RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED"
            && !missing.steps && !missing.commandVisits);
        steps[2].range = range;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 0u>{}, [&]() noexcept -> const char* {
                return inspect(selected).diagnostic;
            }));
        steps[2].action = A::SampledRead;
        budget.contractVersion = 2u;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 0u>{}, [&]() noexcept -> const char* {
                return inspect(selected).diagnostic;
            }));
        // Depth and stencil use different original geometry planes.
        desc.format = rhi::TextureFormat::Depth24Stencil8;
        const auto planes = rhi::semantics::OriginalTextureStateRange::capture(desc, 18u, id);
        assert(planes);
        auto stencil = range;
        stencil.aspect = rhi::TextureAspect::Stencil;
        std::uint64_t stencilCell = 0u;
        assert(planes->cellIndex(stencil, 0u, stencilCell));
        assert(stencilCell != selected);
        budget.maxCells = 18u;
        auto resolved = C::inspect(*planes,
            resource,
            17u,
            stencilCell,
            false,
            3u,
            3u,
            budget,
            [&](std::size_t command) noexcept -> std::optional<C::Step> {
                return steps[command];
            });
        assert(!resolved.diagnostic && !resolved.stored);
        for (auto& step : steps)
            step.range = stencil;
        resolved = C::inspect(*planes,
            resource,
            17u,
            stencilCell,
            false,
            3u,
            3u,
            budget,
            [&](std::size_t command) noexcept -> std::optional<C::Step> {
                return steps[command];
            });
        assert(!resolved.diagnostic && resolved.stored);
        // Two planes at the same original Begin/End ordinal must not be
        // flattened into fictitious commands. They initialize independently.
        resolved = C::inspectProjected(*planes,
            resource,
            17u,
            selected,
            false,
            3u,
            3u,
            budget,
            [&](std::size_t command, auto&& emit) noexcept -> const char* {
                auto depth = steps[command];
                depth.range = range;
                if (const auto* diagnostic = emit(depth))
                    return diagnostic;
                return emit(steps[command]);
            });
        assert(!resolved.diagnostic && resolved.stored && resolved.writes == 1u
            && resolved.steps == 3u && resolved.commandVisits == 3u);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = C::lowering(backend);
            assert(decision.contractVersion == 2u && decision.diagnostic
                && !decision.executionAvailable
                && decision.validatorOnly == (backend == rhi::Backend::Null)
                && decision.validationAvailable
                    == (backend == rhi::Backend::Null || backend == rhi::Backend::WebGPU));
            const auto plan =
                rhi::semantics::OriginalCommandStateAdmissionLowering::textureContentPlanLowering(
                    backend);
            assert(plan.contractVersion == 2u && plan.diagnostic && !plan.executionAvailable
                && plan.validationAvailable == (backend == rhi::Backend::WebGPU)
                && plan.validatorOnly == (backend == rhi::Backend::Null));
            const auto readback = C::lowering(backend, 3u);
            const auto readbackPlan =
                rhi::semantics::OriginalCommandStateAdmissionLowering::textureContentPlanLowering(
                    backend, 3u);
            assert(readback.contractVersion == 3u && readback.diagnostic
                && !readback.executionAvailable
                && readback.validationAvailable
                    == (backend == rhi::Backend::Null || backend == rhi::Backend::WebGPU)
                && readback.validatorOnly == (backend == rhi::Backend::Null));
            assert(readbackPlan.contractVersion == 3u && readbackPlan.diagnostic
                && !readbackPlan.executionAvailable
                && readbackPlan.validationAvailable == (backend == rhi::Backend::WebGPU)
                && readbackPlan.validatorOnly == (backend == rhi::Backend::Null));
            assert(!C::lowering(backend, 5u).validationAvailable
                && !rhi::semantics::OriginalCommandStateAdmissionLowering::
                    textureContentPlanLowering(backend, 4u)
                        .validationAvailable);
        }
        assert(!C::lowering(static_cast<rhi::Backend>(99), 3u).validationAvailable);
        std::puts(
            "BINDLESS_RHI_CONTENT_READBACK_SEQUENCE3_OK same-cell clear-store-copy readonly1 profile2-refused active-discard-untouched-refused bounded hot0 four-backends native-closed");
        std::puts(
            "BINDLESS_RHI_CONTENT_CELL_SEQUENCE_OK clear-store-read partial-only load-before-clear read-before-store discard-read unknown-action generation depth-stencil-independent bounded-visits hot0 native-closed");
    }
};
} // namespace arp::test
