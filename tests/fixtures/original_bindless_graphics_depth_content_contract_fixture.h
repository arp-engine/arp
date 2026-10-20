#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/bindless_graphics_depth_content.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalBindlessGraphicsDepthContentContractFixture final {
public:
    static void verifyCanonical()
    {
        using namespace rhi;
        using Contract = OriginalBindlessGraphicsDepthContentPlanContract;
        Contract::Budget budget{19u, true, {9u, true, 1u, 1u, 1u, 1u, 2u, 15u, 31u}, 1u, 2u, 2u};
        budget.graphics.bindlessCompletedReadContractVersion = 3u;
        Contract::Counts counts{{1u, 1u, 1u, 0u, 1u, 2u, 7u, 11u}, 1u, 2u, 2u, 8u, 20u};
        assert(!Contract::validateCounts(budget, counts));
        for (const auto profile : {0u, 1u, 2u, 4u}) {
            auto wrong = budget;
            wrong.graphics.bindlessCompletedReadContractVersion = profile;
            assert(Contract::validateIntent(wrong));
        }
        for (const auto member : {&Contract::Budget::maxDepthControls,
                 &Contract::Budget::maxDepthCells,
                 &Contract::Budget::maxDepthReadSteps}) {
            auto less = budget;
            --(less.*member);
            assert(Contract::validateCounts(less, counts));
        }
        for (const auto member : {&Contract::Counts::depthControls,
                 &Contract::Counts::depthCells,
                 &Contract::Counts::depthReadSteps,
                 &Contract::Counts::depthCommandVisits,
                 &Contract::Counts::depthProjectionVisits}) {
            auto wrong = counts;
            wrong.*member = 0u;
            assert(Contract::validateCounts(budget, wrong));
            wrong.*member = UINT64_MAX;
            assert(Contract::validateCounts(budget, wrong));
        }
        assert(!Contract::validateDepthControl(6u, 1u, 2u, true, 2u, 3u, 2u, 0u, 19u));
        assert(Contract::validateDepthControl(6u, 3u, 4u, true, 2u, 3u, 1u, 0u, 19u));
        assert(Contract::validateDepthControl(6u, 1u, 2u, true, 2u, 3u, 2u, 1u, 19u));
        assert(Contract::validateDepthControl(6u, 1u, 2u, false, 2u, 3u, 2u, 0u, 19u));
        assert(Contract::validateDepthControl(6u, 1u, 2u, true, 2u, 3u, 2u, 0u, 18u));
        assert(Contract::validateDepthControl(6u, 1u, 2u, true, 2u, 3u, 2u, 0u, 20u));
        TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.format = TextureFormat::D32Float;
        texture.mipLevelCount = 2u;
        texture.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
        TextureViewDesc view;
        view.format = texture.format;
        view.usage = TextureUsage::Sampled;
        view.aspect = TextureAspect::Depth;
        view.mipLevelCount = 2u;
        const auto read = [&](const auto& desc, const auto& selected, auto version) {
            return Contract::inspectDepthRead(desc,
                selected,
                ResourceState::ShaderRead,
                PipelineStage::FragmentShader,
                ResourceAccess::ShaderRead,
                version);
        };
        assert(!read(texture, view, 19u));
        assert(read(texture, view, 18u) && read(texture, view, 20u));
        for (unsigned field = 0u; field < 4u; ++field) {
            auto wrong = view;
            if (field == 0u)
                wrong.baseMipLevel = 1u;
            else if (field == 1u)
                wrong.mipLevelCount = 1u;
            else if (field == 2u)
                wrong.baseArrayLayer = 1u;
            else
                wrong.arrayLayerCount = 2u;
            assert(read(texture, wrong, 19u));
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto d = Contract::lowering(backend, 19u);
            assert(d.contractVersion == 19u && !d.executionAvailable && d.diagnostic
                && d.validatorOnly == (backend == Backend::Null)
                && d.validationAvailable == (backend != Backend::WebGL)
                && d.rootAvailable == (backend == Backend::Null || backend == Backend::WebGPU));
            assert(!Contract::lowering(backend, 20u).validationAvailable);
        }
        assert(OriginalTextureContentCreationContract::cellControlMatchesPlan(5u, 19u));
        for (const auto control : {1u, 2u, 3u, 4u, 6u, 7u})
            assert(!OriginalTextureContentCreationContract::cellControlMatchesPlan(control, 19u));
        std::puts(
            "BINDLESS_GRAPHICS_DEPTH_CONTENT19_CONTRACT_OK canonical-control2-readonly creation5 exact-less overflow five-backend execution-closed");
    }

    static void verify()
    {
        using namespace rhi;
        using Domain = semantics::OriginalBindlessOrdinaryDepthDomain;
        using Contract = OriginalBindlessGraphicsDepthContentPlanContract;
        DescriptorBindingDesc depth;
        depth.type = DescriptorType::SampledTexture;
        depth.arrayCount = 1u;
        depth.stages = ShaderStage::Fragment | ShaderStage::Compute;
        depth.depthTexture = true;
        depth.textureViewDimensionConstrained = true;
        depth.textureViewDimension = TextureViewDimension::Texture2D;
        auto sampler = depth;
        sampler.type = DescriptorType::Sampler;
        sampler.depthTexture = false;
        sampler.comparisonSampler = true;
        for (const auto stage : {ShaderStage::Fragment, ShaderStage::Compute})
            assert(!Domain::inspectSelectedDeclarations(depth, sampler, stage));
        for (const auto stage :
            {ShaderStage::None, ShaderStage::Vertex, ShaderStage::Fragment | ShaderStage::Compute})
            assert(Domain::inspectSelectedDeclarations(depth, sampler, stage));
        auto inactive = sampler;
        inactive.stages = ShaderStage::Compute;
        assert(Domain::inspectSelectedDeclarations(depth, inactive, ShaderStage::Fragment));
        for (auto binding : {depth, sampler}) {
            assert(Domain::acceptsBinding(binding));
            for (unsigned field = 0u; field < 4u; ++field) {
                auto invalid = binding;
                if (field == 0u)
                    invalid.arrayCount = 2u;
                else if (field == 1u)
                    invalid.dynamicOffset = true;
                else if (field == 2u)
                    invalid.tableBacked = true;
                else
                    invalid.stages = ShaderStage::Vertex | ShaderStage::Fragment;
                assert(!Domain::acceptsBinding(invalid));
            }
        }
        for (const auto dimension : {TextureViewDimension::Texture2DArray,
                 TextureViewDimension::TextureCube,
                 TextureViewDimension::Texture3D}) {
            auto invalid = depth;
            invalid.textureViewDimension = dimension;
            assert(!Domain::acceptsBinding(invalid));
        }
        auto invalid = depth;
        invalid.depthTexture = false;
        assert(!Domain::acceptsBinding(invalid));
        invalid = depth;
        invalid.textureViewDimensionConstrained = false;
        assert(!Domain::acceptsBinding(invalid));
        invalid = sampler;
        invalid.comparisonSampler = false;
        assert(!Domain::acceptsBinding(invalid));

        // Visits are measured per inspector traversal. Exact aggregate limits
        // include both color/attachment and depth passes in the same journal.
        Contract::Budget budget{18u, true, {9u, true, 1u, 1u, 1u, 2u, 2u, 15u, 31u}, 2u, 2u, 4u};
        Contract::Counts counts{{1u, 1u, 1u, 0u, 2u, 2u, 7u, 11u}, 2u, 2u, 4u, 8u, 20u};
        assert(!Contract::validateCounts(budget, counts));
        auto wrong = budget;
        wrong.contractVersion = 9u;
        assert(Contract::validateIntent(wrong));
        wrong = budget;
        wrong.prototypeOptIn = false;
        assert(Contract::validateIntent(wrong));
        wrong = budget;
        wrong.graphics.contractVersion = 18u;
        assert(Contract::validateIntent(wrong));
        wrong = budget;
        wrong.graphics.bindlessCompletedReadContractVersion = 1u;
        assert(Contract::validateIntent(wrong));
        wrong = budget;
        wrong.graphics.bindlessCompletedReadContractVersion = 2u;
        assert(!Contract::validateCounts(wrong, counts));
        for (const auto member : {&Contract::Budget::maxDepthControls,
                 &Contract::Budget::maxDepthCells,
                 &Contract::Budget::maxDepthReadSteps}) {
            wrong = budget;
            --(wrong.*member);
            assert(Contract::validateCounts(wrong, counts));
            wrong = budget;
            wrong.*member = 0u;
            assert(Contract::validateIntent(wrong));
        }
        for (const auto member : {&OriginalFiniteGraphicsContentPlanBudget::maxCommandVisits,
                 &OriginalFiniteGraphicsContentPlanBudget::maxProjectionVisits}) {
            wrong = budget;
            --(wrong.graphics.*member);
            assert(Contract::validateCounts(wrong, counts));
        }
        for (const auto member : {&Contract::Counts::depthControls,
                 &Contract::Counts::depthCells,
                 &Contract::Counts::depthReadSteps,
                 &Contract::Counts::depthCommandVisits,
                 &Contract::Counts::depthProjectionVisits}) {
            auto missing = counts;
            missing.*member = 0u;
            assert(Contract::validateCounts(budget, missing));
            missing.*member = UINT64_MAX;
            assert(Contract::validateCounts(budget, missing));
        }
        auto noColor = budget;
        noColor.graphics.maxCompletedControls = noColor.graphics.maxSampledSteps = 0u;
        auto depthOnly = counts;
        depthOnly.graphics.completedControls = depthOnly.graphics.drawReads = 0u;
        assert(!Contract::validateCounts(noColor, depthOnly));
        // The new schema never reinterprets old content versions.
        auto old = budget.graphics;
        old.contractVersion = 18u;
        assert(OriginalBindlessGraphicsContentPlanContract::validateIntent(old));
        assert(OriginalFiniteGraphicsContentPlanContract::validateIntent(old));
        assert(!OriginalFiniteGraphicsContentPlanContract::isMixed(18u));
        assert(!OriginalCompletedDepthRootReadPlanContract::isVersion(18u));
        assert(!OriginalStorageTextureContentPlanContract::isVersion(18u));
        assert(OriginalCompletedDepthRootReadPlanContract::validatePendingProfile(
            2u, 14u, 18u, true, true));
        assert(OriginalCompletedDepthRootReadPlanContract::validatePendingProfile(
            2u, 17u, 18u, true, true));
        assert(OriginalCompletedTextureReadPlanContract::validatePendingProfile(
            7u, true, false, 18u, false, true, 2u));
        assert(Contract::validateDepthControl(2u, 2u, 4u, true, 2u, 3u, 1u, 0u));
        assert(Contract::validateDepthControl(6u, 3u, 4u, true, 2u, 3u, 2u, 0u));
        assert(
            !Contract::validateIndependentAttachment(GpuResourceId{7u}, 1u, GpuResourceId{8u}, 1u));
        assert(
            Contract::validateIndependentAttachment(GpuResourceId{7u}, 1u, GpuResourceId{7u}, 2u));
        assert(Contract::validateIndependentAttachment(GpuResourceId{}, 1u, GpuResourceId{8u}, 1u));
        assert(
            Contract::validateIndependentAttachment(GpuResourceId{7u}, 0u, GpuResourceId{8u}, 1u));
        for (unsigned field = 0u; field < 8u; ++field) {
            std::array<std::uint64_t, 8u> facts{6u, 3u, 4u, 1u, 2u, 3u, 1u, 0u};
            assert(!Contract::validateDepthControl(
                6u, 3u, 4u, true, facts[4], facts[5], facts[6], facts[7]));
            facts[field] = field == 7u ? 1u : 0u;
            assert(Contract::validateDepthControl(static_cast<std::uint32_t>(facts[0]),
                static_cast<std::uint32_t>(facts[1]),
                static_cast<std::uint32_t>(facts[2]),
                bool(facts[3]),
                facts[4],
                facts[5],
                facts[6],
                facts[7]));
        }
        TextureDesc texture;
        texture.extent = {4u, 2u, 1u};
        texture.format = TextureFormat::D32Float;
        texture.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
        TextureViewDesc view;
        view.format = texture.format;
        view.usage = TextureUsage::Sampled;
        view.aspect = TextureAspect::Depth;
        const auto read = [&](const auto& desc, const auto& selected) {
            return Contract::inspectDepthRead(desc,
                selected,
                ResourceState::ShaderRead,
                PipelineStage::FragmentShader,
                ResourceAccess::ShaderRead);
        };
        assert(!read(texture, view));
        for (unsigned field = 0u; field < 8u; ++field) {
            auto desc = texture;
            auto selected = view;
            if (field == 0u)
                desc.format = selected.format = TextureFormat::Depth24Stencil8;
            else if (field == 1u)
                selected.baseMipLevel = 1u;
            else if (field == 2u)
                desc.mipLevelCount = selected.mipLevelCount = 2u;
            else if (field == 3u)
                desc.arrayLayerCount = selected.arrayLayerCount = 2u;
            else if (field == 4u)
                selected.aspect = TextureAspect::Color;
            else if (field == 5u)
                desc.sampleCount = 4u;
            else if (field == 6u)
                desc.usage = TextureUsage::RenderAttachment;
            else
                selected.usage = TextureUsage::RenderAttachment;
            assert(read(desc, selected));
        }
        for (const auto stages : {PipelineStage::None,
                 PipelineStage::ComputeShader,
                 PipelineStage::VertexShader,
                 PipelineStage::FragmentShader | PipelineStage::ComputeShader})
            assert(Contract::inspectDepthRead(
                texture, view, ResourceState::ShaderRead, stages, ResourceAccess::ShaderRead));
        assert(Contract::inspectDepthRead(texture,
            view,
            ResourceState::DepthAttachment,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderRead));
        assert(Contract::inspectDepthRead(texture,
            view,
            ResourceState::ShaderRead,
            PipelineStage::FragmentShader,
            ResourceAccess::ShaderWrite));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto domain = Domain::lowering(backend);
            assert(domain.inspectionAvailable && !domain.nativeClosureAvailable
                && domain.validatorOnly == (backend == Backend::Null) && domain.diagnostic);
            const auto decision = Contract::lowering(backend);
            assert(decision.contractVersion == 18u && decision.validationAvailable
                && decision.rootAvailable
                    == (backend == Backend::Null || backend == Backend::Vulkan
                        || backend == Backend::Metal)
                && !decision.executionAvailable
                && decision.validatorOnly == (backend == Backend::Null) && decision.diagnostic);
            for (const auto version : {0u, 9u, 12u, 17u, 20u}) {
                const auto unsupported = Contract::lowering(backend, version);
                assert(!unsupported.validationAvailable && !unsupported.rootAvailable
                    && !unsupported.executionAvailable && unsupported.diagnostic);
            }
            assert(!Domain::lowering(backend, 0u).inspectionAvailable);
            assert(!Domain::lowering(backend, 2u).inspectionAvailable);
        }
        assert(!Contract::lowering(Backend::WebGL).validationAvailable);
        assert(OriginalTextureContentCreationContract::cellControlMatchesPlan(4u, 18u));
        for (const auto control : {1u, 2u, 3u, 5u, 6u, 7u})
            assert(!OriginalTextureContentCreationContract::cellControlMatchesPlan(control, 18u));
        assert(!Domain::lowering(Backend::WebGL).inspectionAvailable);
        const auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                if (const auto* id = Contract::validateCounts(budget, counts))
                    return id;
                return read(texture, view);
            }));
        std::puts(
            "BINDLESS_GRAPHICS_DEPTH_CONTENT18_CONTRACT_OK exact-less-budgets old-plans-refuse hot0 original-cpu-journal native-execution-closed");
    }
};
} // namespace arp::test
