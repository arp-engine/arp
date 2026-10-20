#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_arguments.h"

#include <cstdio>
#include <limits>

namespace arp::test {
class RenderPassArgumentsFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi;
        const char* diagnostic = nullptr;
        RenderPassArgumentsBudget budget;
        assert(std::string_view(RenderPassArguments::validateBudget(budget))
            == "RHI-RENDER-PASS-ARGUMENTS-VERSION");
        budget.contractVersion = 1u;
        assert(std::string_view(RenderPassArguments::validateBudget(budget))
            == "RHI-RENDER-PASS-ARGUMENTS-OPT-IN");
        budget.prototypeOptIn = true;
        assert(std::string_view(RenderPassArguments::validateBudget(budget))
            == "RHI-RENDER-PASS-ARGUMENTS-BUDGET");
        budget.maxCpuMetadataBytes = metadataBytes;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        RenderPassDesc source;
        source.label = "complete original render pass label exceeding SSO";
        source.colorTargetName = "explicit original target name exceeding SSO";
        source.colorAttachments.push_back({"first original color attachment label exceeding SSO",
            allocateGpuResourceId(),
            TextureFormat::Rgba8Unorm,
            LoadOp::Clear,
            StoreOp::Store,
            true,
            allocateGpuResourceId(),
            {0.125f, -0.0f, 0.875f, 1.0f}});
        source.colorAttachments.push_back({"second original color attachment label exceeding SSO",
            allocateGpuResourceId(),
            TextureFormat::Rgba16Float,
            LoadOp::Load,
            StoreOp::DontCare,
            false,
            {},
            {0.375f, 0.125f, 0.875f, 1.0f}});
        source.hasDepthStencilAttachment = true;
        source.depthStencilAttachment.label = "original depth attachment label exceeding SSO";
        source.depthStencilAttachment.target = allocateGpuResourceId();
        source.depthStencilAttachment.format = TextureFormat::Depth24Stencil8;
        source.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
        source.depthStencilAttachment.depthStoreOp = StoreOp::DontCare;
        source.depthStencilAttachment.stencilLoadOp = LoadOp::Load;
        source.depthStencilAttachment.stencilStoreOp = StoreOp::Store;
        source.depthStencilAttachment.depthClearValue = 0.375f;
        source.depthStencilAttachment.stencilClearValue = 73u;
        source.layeredRendering = true;
        source.subpasses.resize(2u);
        source.subpasses[0].colorAttachmentIndices = {1u, 0u};
        source.subpasses[0].useDepthStencilAttachment = true;
        source.subpasses[1].colorAttachmentIndices = {1u};
        source.subpasses[1].inputAttachmentIndices = {0u, 1u};
        source.subpasses[1].readDepthInputAttachment = true;
        source.subpassDependencies.push_back({0u, 1u});
        budget.maxColorAttachments = 2u;
        budget.maxSubpasses = 2u;
        budget.maxSubpassAttachmentIndices = 5u;
        budget.maxDependencies = 1u;
        budget.maxTextBytes = source.label.size() + source.colorTargetName.size()
            + source.depthStencilAttachment.label.size();
        for (const auto& color : source.colorAttachments)
            budget.maxTextBytes += color.label.size();
        const auto factory = [&] {
            return RenderPassArguments::prepareOriginal(source, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-RENDER-PASS-ARGUMENTS-ALLOCATION");
        }
        const auto countBoundary = [&](auto& field) {
            --field;
            assert(!factory() && std::string_view(diagnostic) == "RHI-RENDER-PASS-ARGUMENTS-COUNT");
            ++field;
        };
        countBoundary(budget.maxColorAttachments);
        countBoundary(budget.maxSubpasses);
        countBoundary(budget.maxSubpassAttachmentIndices);
        countBoundary(budget.maxDependencies);
        --budget.maxTextBytes;
        assert(!factory() && std::string_view(diagnostic) == "RHI-RENDER-PASS-ARGUMENTS-TEXT");
        ++budget.maxTextBytes;
        assert(!RenderPassArguments::prepareOriginal(source, budget, {}, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-RENDER-PASS-ARGUMENTS-QUOTA");
        const auto wrongQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes - 1u);
        assert(!RenderPassArguments::prepareOriginal(source, budget, wrongQuota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-RENDER-PASS-ARGUMENTS-QUOTA");
        auto frozen = factory();
        assert(frozen && !diagnostic && frozen->hasQuotaBoundSharedOwner(frozen));
        verifyAllocators(frozen->desc());
        auto foreign = factory();
        assert(foreign && !frozen->hasQuotaBoundSharedOwner(foreign));
        auto forgery = std::shared_ptr<const RenderPassArguments>(foreign, frozen.get());
        assert(!frozen->hasQuotaBoundSharedOwner(forgery));
        foreign.reset();
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            const auto changed = [&](auto& field, const auto& replacement) {
                const auto original = field;
                field = replacement;
                assert(!frozen->matches(source));
                field = original;
                assert(frozen->matches(source));
            };
            changed(source.label[0], 'x');
            changed(source.colorTargetName[0], 'x');
            changed(source.hasDepthStencilAttachment, false);
            changed(source.layeredRendering, false);
            for (auto& color : source.colorAttachments) {
                changed(color.label[0], 'x');
                changed(color.target, GpuResourceId{});
                changed(color.format, TextureFormat::Unknown);
                changed(color.loadOp, LoadOp::DontCare);
                changed(color.storeOp,
                    color.storeOp == StoreOp::Store ? StoreOp::DontCare : StoreOp::Store);
                changed(color.hasResolveTarget, !color.hasResolveTarget);
                changed(color.resolveTarget, color.target);
                for (auto* component : {&color.clearColor.r,
                         &color.clearColor.g,
                         &color.clearColor.b,
                         &color.clearColor.a})
                    changed(*component, 0.25f);
            }
            auto& depth = source.depthStencilAttachment;
            changed(depth.label[0], 'x');
            changed(depth.target, GpuResourceId{});
            changed(depth.format, TextureFormat::Unknown);
            changed(depth.depthLoadOp, LoadOp::DontCare);
            changed(depth.depthStoreOp, StoreOp::Store);
            changed(depth.stencilLoadOp, LoadOp::DontCare);
            changed(depth.stencilStoreOp, StoreOp::DontCare);
            changed(depth.depthClearValue, 0.875f);
            changed(depth.stencilClearValue, 255u);
            changed(depth.depthReadOnly, true);
            changed(depth.stencilReadOnly, true);
            changed(source.colorAttachments[0].clearColor.g,
                0.0f); // signed zero is an explicit stored input.
            for (auto& subpass : source.subpasses) {
                for (auto& index : subpass.colorAttachmentIndices)
                    changed(index, UINT32_MAX);
                for (auto& index : subpass.inputAttachmentIndices)
                    changed(index, UINT32_MAX);
                changed(subpass.useDepthStencilAttachment, !subpass.useDepthStencilAttachment);
                changed(subpass.readDepthInputAttachment, !subpass.readDepthInputAttachment);
            }
            changed(source.subpassDependencies[0].srcSubpass, 1u);
            changed(source.subpassDependencies[0].dstSubpass, 0u);
            std::swap(source.colorAttachments[0], source.colorAttachments[1]);
            assert(!frozen->matches(source));
            std::swap(source.colorAttachments[0], source.colorAttachments[1]);
            assert(frozen->matches(source));
            assert(core::defaultCpuAllocator().stats().allocationCount == before);
        }
        std::pmr::set_default_resource(previous);
        source = {}; // destroy every authoring buffer; original owned copy remains.
        assert(frozen->desc().colorAttachments.size() == 2u
            && frozen->desc().subpasses[1].inputAttachmentIndices.size() == 2u);
        assert(frozen->desc().depthStencilAttachment.depthClearValue == 0.375f
            && !frozen->matches(source));
        frozen.reset();
        // The alias owns foreign only, not the original snapshot or its strings.
        assert(quota->liveBytes());
        forgery.reset();
        assert(!quota->liveBytes());
        verifyInactiveDepth();
        std::printf("BINDLESS_RHI_FROZEN_PASS_ARGUMENTS_OK %llu 2 2 5 1 allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    static std::shared_ptr<const rhi::RenderPassArguments> freezeDepthPass(
        const rhi::RenderPassDesc& pass)
    {
        const rhi::RenderPassArgumentsBudget budget{1u, true, 0u, 0u, 0u, 0u, 0u, metadataBytes};
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        auto result = rhi::RenderPassArguments::prepareOriginal(pass, budget, quota, diagnostic);
        assert(result && !diagnostic && result->matches(pass));
        return result;
    }

private:
    static void verifyAllocators(const rhi::RenderPassDesc& desc)
    {
        auto* resource = &core::defaultCpuMemoryResource();
        assert(desc.label.get_allocator().resource() == resource
            && desc.colorTargetName.get_allocator().resource() == resource);
        assert(desc.colorAttachments.get_allocator().resource() == resource
            && desc.subpasses.get_allocator().resource() == resource
            && desc.subpassDependencies.get_allocator().resource() == resource);
        assert(desc.depthStencilAttachment.label.get_allocator().resource() == resource);
        for (const auto& color : desc.colorAttachments)
            assert(color.label.get_allocator().resource() == resource);
        for (const auto& subpass : desc.subpasses)
            assert(subpass.colorAttachmentIndices.get_allocator().resource() == resource
                && subpass.inputAttachmentIndices.get_allocator().resource() == resource);
    }

    static void verifyInactiveDepth()
    {
        using namespace rhi;
        RenderPassDesc empty;
        empty.depthStencilAttachment.depthClearValue = std::numeric_limits<float>::quiet_NaN();
        empty.depthStencilAttachment.depthReadOnly = true;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const RenderPassArgumentsBudget budget{
            1u, true, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT64_MAX, metadataBytes};
        const char* diagnostic = nullptr;
        auto copy = RenderPassArguments::prepareOriginal(empty, budget, quota, diagnostic);
        assert(copy && copy->matches(empty));
        empty.depthStencilAttachment.stencilReadOnly = true;
        assert(!copy->matches(empty));
        empty.hasDepthStencilAttachment = true;
        assert(!RenderPassArguments::prepareOriginal(empty, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-RENDER-PASS-DEPTH-CLEAR-INVALID");
        copy.reset();
        assert(!quota->liveBytes());
    }

    static constexpr std::uint64_t metadataBytes = 1024u * 1024u;
};
} // namespace arp::test
