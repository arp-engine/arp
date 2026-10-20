#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/finite_graphics_content_validation.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteGraphicsContentContractFixture final {
    using C = rhi::OriginalFiniteGraphicsContentPlanContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Graphics content contract: %s\n", id);
        assert(!id);
    }

public:
    static void verify()
    {
        // Value-schema tests only. Counts below are authored test inputs, never
        // claims about an authenticated original graph or GPU content.
        const rhi::OriginalFiniteGraphicsContentPlanBudget budget{
            6u, true, 1u, 1u, 1u, 2u, 2u, 24u, 64u};
        const C::Counts counts{1u, 1u, 1u, 1u, 1u, 2u, 24u, 64u};
        require(C::validateCounts(budget, counts));
        // Larger ceilings cannot hide an unvisited completed control, a
        // missing attachment cell, or an attachment without its Clear/Store.
        auto reserved = budget;
        reserved.maxCompletedControls = reserved.maxAttachmentControls = 2u;
        reserved.maxAttachmentCells = 2u;
        auto missingRead = counts;
        missingRead.completedControls = 2u;
        auto missingCell = counts;
        missingCell.attachmentControls = 2u;
        auto missingStore = counts;
        missingStore.attachmentSteps = 1u;
        assert(C::validateCounts(reserved, missingRead) && C::validateCounts(reserved, missingCell)
            && C::validateCounts(budget, missingStore));
        for (const auto member :
            {&rhi::OriginalFiniteGraphicsContentPlanBudget::maxCompletedControls,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxAttachmentControls,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxAttachmentCells,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxSampledSteps,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxAttachmentSteps,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxCommandVisits,
                &rhi::OriginalFiniteGraphicsContentPlanBudget::maxProjectionVisits}) {
            auto less = budget;
            --(less.*member);
            assert(C::validateCounts(less, counts));
        }
        for (const auto member : {&C::Counts::completedControls,
                 &C::Counts::attachmentControls,
                 &C::Counts::attachmentCells,
                 &C::Counts::renderBindingReads,
                 &C::Counts::drawReads,
                 &C::Counts::attachmentSteps,
                 &C::Counts::commandVisits,
                 &C::Counts::projectionVisits}) {
            auto missing = counts;
            missing.*member = 0u;
            assert(C::validateCounts(budget, missing));
            auto huge = counts;
            huge.*member = UINT64_MAX;
            assert(C::validateCounts(budget, huge));
        }
        auto hugeBudget = budget;
        hugeBudget.maxSampledSteps = UINT64_MAX;
        auto wrap = counts;
        wrap.renderBindingReads = UINT64_MAX;
        wrap.drawReads = 1u;
        assert(C::validateCounts(hugeBudget, wrap));
        auto disabled = budget;
        disabled.prototypeOptIn = false;
        assert(C::validateIntent(disabled));
        for (std::uint32_t version = 0u; version <= 7u; ++version)
            if (version != 6u) {
                auto legacy = budget;
                legacy.contractVersion = version;
                assert(C::validateIntent(legacy));
            }
        const C::Profile profile{5u, 6u, 5u, 5u, 6u, 6u, 4u, 4u, 2u, true, true, false};
        require(C::validateRootProfile(profile));
        for (const auto member : {&C::Profile::sources,
                 &C::Profile::inputs,
                 &C::Profile::state,
                 &C::Profile::sequence,
                 &C::Profile::usage,
                 &C::Profile::dependency,
                 &C::Profile::consumption,
                 &C::Profile::renderUsage,
                 &C::Profile::generalResources}) {
            auto legacy = profile;
            --(legacy.*member);
            assert(C::validateRootProfile(legacy));
            auto future = profile;
            ++(future.*member);
            assert(C::validateRootProfile(future));
        }
        auto compute = profile;
        compute.graphicsPipeline = false;
        compute.computePipeline = true;
        assert(C::validateRootProfile(compute));
        auto mixed = profile;
        mixed.computePipeline = true;
        assert(C::validateRootProfile(mixed));
        auto noPipeline = profile;
        noPipeline.graphicsPipeline = false;
        assert(C::validateRootProfile(noPipeline));
        auto noOptIn = profile;
        noOptIn.generalResourcesOptIn = false;
        assert(C::validateRootProfile(noOptIn));

        rhi::TextureDesc sampled;
        sampled.extent = {2u, 2u, 1u};
        sampled.mipLevelCount = 2u;
        sampled.format = rhi::TextureFormat::Rgba8Unorm;
        sampled.usage =
            rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
        sampled.initialState = rhi::ResourceState::CopyDst;
        rhi::TextureViewDesc sampledView;
        sampledView.format = sampled.format;
        sampledView.mipLevelCount = 2u;
        sampledView.dimension = rhi::TextureViewDimension::Texture2D;
        sampledView.usage = rhi::TextureUsage::Sampled;
        sampledView.aspect = rhi::TextureAspect::Color;
        const auto sampledUse = [&](const auto& view,
                                    rhi::PipelineStage stage,
                                    rhi::ResourceState state,
                                    rhi::ResourceAccess access,
                                    C::SampledOrigin origin) noexcept {
            return C::validateSampledUse(sampled, view, state, stage, access, origin);
        };
        for (const auto origin :
            {C::SampledOrigin::RenderBinding, C::SampledOrigin::ShaderConsumption}) {
            require(sampledUse(sampledView,
                rhi::PipelineStage::FragmentShader,
                rhi::ResourceState::ShaderRead,
                rhi::ResourceAccess::ShaderRead,
                origin));
            for (const auto stage : {rhi::PipelineStage::None,
                     rhi::PipelineStage::ComputeShader,
                     rhi::PipelineStage::VertexShader,
                     rhi::PipelineStage::FragmentShader | rhi::PipelineStage::ComputeShader})
                assert(sampledUse(sampledView,
                    stage,
                    rhi::ResourceState::ShaderRead,
                    rhi::ResourceAccess::ShaderRead,
                    origin));
            auto partial = sampledView;
            partial.mipLevelCount = 1u;
            assert(sampledUse(partial,
                rhi::PipelineStage::FragmentShader,
                rhi::ResourceState::ShaderRead,
                rhi::ResourceAccess::ShaderRead,
                origin));
            assert(sampledUse(sampledView,
                rhi::PipelineStage::FragmentShader,
                rhi::ResourceState::CopyDst,
                rhi::ResourceAccess::ShaderRead,
                origin));
            assert(sampledUse(sampledView,
                rhi::PipelineStage::FragmentShader,
                rhi::ResourceState::ShaderRead,
                rhi::ResourceAccess::ShaderWrite,
                origin));
        }
        assert(sampledUse(sampledView,
            rhi::PipelineStage::FragmentShader,
            rhi::ResourceState::ShaderRead,
            rhi::ResourceAccess::ShaderRead,
            static_cast<C::SampledOrigin>(99u)));
        using Read = rhi::OriginalCompletedTextureReadPlanContract;
        require(Read::validateObservation(1u, true, 1u, 1u));
        assert(Read::validateObservation(2u, true, 1u, 1u)
            && Read::validateObservation(1u, false, 1u, 1u)
            && Read::validateObservation(1u, true, 0u, 1u)
            && Read::validateObservation(1u, true, 1u, 0u));
        require(C::validateIndependentResources({71u}, 17u, {72u}, 18u));
        assert(C::validateIndependentResources({71u}, 17u, {71u}, 18u)
            && C::validateIndependentResources({}, 17u, {72u}, 18u)
            && C::validateIndependentResources({71u}, 0u, {72u}, 18u)
            && C::validateIndependentResources({71u}, 17u, {72u}, 0u));

        rhi::TextureDesc attachment;
        attachment.extent = {1u, 1u, 1u};
        attachment.format = rhi::TextureFormat::Rgba16Float;
        attachment.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
        attachment.initialState = rhi::ResourceState::Undefined;
        rhi::TextureViewDesc attachmentView;
        attachmentView.format = attachment.format;
        attachmentView.dimension = rhi::TextureViewDimension::Texture2D;
        attachmentView.usage = rhi::TextureUsage::RenderAttachment;
        attachmentView.aspect = rhi::TextureAspect::Color;
        const rhi::OriginalTextureContentCreationBudget creation{
            2u, true, 1u, 1u, 8u, 1u, 65536u, 1u};
        std::uint64_t texelBytes = 0u;
        require(C::inspectAttachment(attachment,
            attachmentView,
            creation,
            rhi::LoadOp::Clear,
            rhi::StoreOp::Store,
            false,
            false,
            texelBytes));
        assert(texelBytes == 8u);
        for (std::uint32_t field = 0u; field < 13u; ++field) {
            auto d = attachment;
            auto v = attachmentView;
            auto b = creation;
            auto load = rhi::LoadOp::Clear;
            auto store = rhi::StoreOp::Store;
            bool resolve = false, layered = false;
            switch (field) {
            case 0u:
                load = rhi::LoadOp::Load;
                break;
            case 1u:
                store = rhi::StoreOp::DontCare;
                break;
            case 2u:
                resolve = true;
                break;
            case 3u:
                layered = true;
                break;
            case 4u:
                b.contractVersion = 1u;
                break;
            case 5u:
                b.prototypeOptIn = false;
                break;
            case 6u:
                d.sampleCount = 4u;
                break;
            case 7u:
                d.initialState = rhi::ResourceState::ColorAttachment;
                break;
            case 8u:
                d.usage = d.usage | rhi::TextureUsage::Storage;
                break;
            case 9u:
                v.baseMipLevel = 1u;
                break;
            case 10u:
                v.usage = rhi::TextureUsage::Sampled;
                break;
            case 11u:
                --b.maxTexelBytes;
                break;
            case 12u:
                d.arrayLayerCount = 2u;
                break;
            }
            texelBytes = UINT64_MAX;
            assert(C::inspectAttachment(d, v, b, load, store, resolve, layered, texelBytes)
                && !texelBytes);
        }
        // Reuse the genuine bounded cell timeline for proposed Clear/Store.
        // Its stored value is CPU planning intent; it never means Completed.
        using Timeline = rhi::OriginalTextureContentCellSequence;
        const char* id = nullptr;
        const auto geometry =
            rhi::semantics::OriginalTextureStateRange::capture(attachment, 1u, id);
        require(id);
        assert(geometry);
        const auto inspectTimeline = [&](rhi::LoadOp load, bool earlyRead) noexcept {
            return Timeline::inspectProjected(*geometry,
                {72u},
                18u,
                0u,
                false,
                12u,
                12u,
                {2u, true, 1u, 2u, 12u},
                [&](std::size_t command, auto&& emit) noexcept -> const char* {
                    if (command == 0u)
                        return emit(Timeline::Step{{72u},
                            18u,
                            {},
                            load == rhi::LoadOp::Clear ? Timeline::Action::Clear
                                                       : Timeline::Action::Load,
                            0u});
                    if (command == 5u && earlyRead)
                        return emit(Timeline::Step{
                            {72u}, 18u, {}, Timeline::Action::SampledRead, SIZE_MAX});
                    if (command == 6u)
                        return emit(Timeline::Step{{72u}, 18u, {}, Timeline::Action::Store, 0u});
                    return nullptr;
                });
        };
        const auto timeline = inspectTimeline(rhi::LoadOp::Clear, false);
        assert(!timeline.diagnostic && timeline.stored && timeline.writes == 1u
            && timeline.steps == 2u && timeline.commandVisits == 12u
            && !timeline.nativeExecutionProven());
        const auto loadFailure = inspectTimeline(rhi::LoadOp::Load, false);
        const auto readFailure = inspectTimeline(rhi::LoadOp::Clear, true);
        assert(loadFailure.diagnostic && !loadFailure.steps && !loadFailure.commandVisits
            && readFailure.diagnostic && !readFailure.steps && !readFailure.commandVisits);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 0u>{}, [&]() noexcept -> const char* {
                if (const auto* failure = C::validateCounts(budget, counts))
                    return failure;
                return inspectTimeline(rhi::LoadOp::Clear, false).diagnostic;
            }));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = C::lowering(backend);
            assert(decision.contractVersion == 6u && decision.diagnostic
                && decision.rootAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.executionAvailable
                && decision.validatorOnly == (backend == rhi::Backend::Null)
                && decision.validationAvailable
                    == (backend == rhi::Backend::Null || backend == rhi::Backend::WebGPU));
            for (const auto version : {0u, 1u, 2u, 3u, 4u, 5u, 7u, UINT32_MAX}) {
                const auto unavailable = C::lowering(backend, version);
                assert(!unavailable.validationAvailable && !unavailable.rootAvailable
                    && !unavailable.executionAvailable && unavailable.diagnostic);
            }
            // Old exact version sets still refuse /6, including native pending.
            assert(!Read::isReadOnly(6u)
                && !rhi::semantics::OriginalCommandStateAdmissionLowering::
                    textureCompletedReadPlanLowering(backend, 6u)
                        .validationAvailable);
            assert(Read::validatePendingProfile(2u, true, false, 6u, true));
        }
        assert(!C::lowering(static_cast<rhi::Backend>(99u)).validationAvailable);
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SCHEMA_OK mixed-completed-sampled-attachment distinct-bounds clear-store Fragment-bind-draw hostile-overflow-refused hot0 four-backends old-profiles-unchanged WebGPU-private-root-only native-unavailable");
    }
};
} // namespace arp::test
