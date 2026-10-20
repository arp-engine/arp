#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandTextureViewSourceFixture final {
    static constexpr std::uint64_t metadataBytes = 65536u;

public:
    static void verify(rhi::GpuDevice& device)
    {
        using View = rhi::OriginalCommandTextureViewSource;
        using ViewLease = rhi::OriginalCommandTextureViewLease;
        using Parent = rhi::OriginalCommandTextureLease;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using B = rhi::OriginalCommandAdmissionBudget;
        using K = rhi::CommandUsageKind;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        rhi::TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.format = rhi::TextureFormat::Rgba8Unorm;
        texture.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment;
        auto object = device.createTexture(texture), peerObject = device.createTexture(texture);
        assert(object && peerObject);
        rhi::TextureViewDesc fields;
        fields.format = texture.format;
        fields.usage = rhi::TextureUsage::RenderAttachment;
        auto viewObject = device.createTextureView(*object, fields);
        assert(viewObject);
        const auto* originalView = viewObject.get();
        const auto charge = core::inspectCpuAllocationContaining(originalView);
        auto parent =
            Parent::prepareOriginal(object, {1u, true, 2u, metadataBytes}, quota, diagnostic);
        auto peer =
            Parent::prepareOriginal(peerObject, {1u, true, 2u, metadataBytes}, quota, diagnostic);
        auto lease = ViewLease::prepareOriginal(
            viewObject, {1u, true, 2u, metadataBytes}, quota, diagnostic);
        assert(parent && peer && lease && !viewObject
            && lease->parentResource() == parent->resource());
        const auto retainedCharge = core::inspectCpuAllocationContaining(originalView);
        assert(charge.found && retainedCharge.allocationIdentity == charge.allocationIdentity
            && retainedCharge.quota == charge.quota);
        rhi::OriginalCommandResourceLeaseBudget viewBudget{1u, true, 5u, metadataBytes};
        const auto viewFactory = [&] {
            return View::prepareOriginal(lease, parent, viewBudget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto viewPeak = pressure.measure(viewFactory);
        auto view = viewFactory();
        assert(view && !View::validateOriginal(view) && !view->nativeExecutionProven());
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = 3u;
        budget.maxReferences = 1u;
        budget.maxGraphOwners = 9u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Source)] = metadataBytes;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Graph)] = metadataBytes;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 2u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] =
            1u;
        std::array<Sources::Source, 2> entries{parent, view};
        const auto tableFactory = [&] {
            return Sources::prepareOriginal(
                entries.data(), entries.size(), budget, quota, diagnostic);
        };
        const auto tablePeak = pressure.measure(tableFactory);
        pressure.verify(viewPeak, viewFactory);
        pressure.verify(tablePeak, tableFactory);
        const auto baseline = quota->liveBytes();
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
            assert(full.charged() && !viewFactory() && !tableFactory());
        }
        --viewBudget.maxRetainedOwners;
        assert(!viewFactory()
            && std::string_view(diagnostic) == "RHI-ORIGINAL-VIEW-SOURCE-OWNER-CAPACITY");
        ++viewBudget.maxRetainedOwners;
        assert(!View::prepareOriginal(lease, peer, viewBudget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-VIEW-SOURCE-PARENT");
        assert(!View::prepareOriginal(lease, parent, viewBudget, foreignQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-VIEW-SOURCE-QUOTA");
        auto forgedParent = std::shared_ptr<const Parent>(foreignQuota, parent.get());
        assert(!View::prepareOriginal(lease, forgedParent, viewBudget, quota, diagnostic));
        forgedParent.reset();
        auto forgedView = std::shared_ptr<const View>(foreignQuota, view.get());
        assert(View::validateOriginal(forgedView));
        forgedView.reset();
        entries[0] = peer;
        assert(
            !tableFactory() && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-VIEW-PARENT");
        entries[0] = parent;
        assert(!Sources::prepareOriginal(entries.data() + 1u, 1u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-VIEW-PARENT");
        --budget
              .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)];
        assert(!tableFactory()
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY");
        ++budget
              .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)];
        auto sources = tableFactory();
        assert(sources && !Sources::validateOriginal(sources));
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const std::array<rhi::CommandUsage, 3> commands{
            {{K::BeginRenderPass, 0u, 1u}, {K::EndRenderPass, 1u, 0u}, {K::Finish, 1u, 0u}}};
        const Usage::Reference reference{1u};
        auto usage = Usage::prepareOriginal(
            sources, commands.data(), commands.size(), &reference, 1u, graphQuota, diagnostic);
        assert(usage && usage->retainedOwnerCount() == 7u
            && !usage->compareOriginalReference(0u, *originalView));
        assert(usage->compareOriginalReference(0u, *parent->originalObject()));
        const std::array<Store::Input, 3> input{
            {{*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(input.data(),
            input.size(),
            {1u, true, 3u, 0u, 0u, 0u, metadataBytes},
            graphQuota,
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        assert(scalars && scalars->retainedOwnerCount() == 9u);
        // An original view source cannot replace a required complete pass input.
        // Leave root capacity so the missing descriptor is the rejecting gate.
        auto roomBudget = budget;
        roomBudget.maxGraphOwners = 11u;
        auto roomSources =
            Sources::prepareOriginal(entries.data(), entries.size(), roomBudget, quota, diagnostic);
        assert(roomSources);
        auto roomUsage = Usage::prepareOriginal(
            roomSources, commands.data(), commands.size(), &reference, 1u, graphQuota, diagnostic);
        assert(roomUsage);
        auto roomScalars = Scalars::prepareOriginal(roomUsage, store, graphQuota, diagnostic);
        assert(roomScalars);
        assert(!rhi::OriginalCommandInputs::prepareOriginal(
                   roomScalars, nullptr, 0u, graphQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-ORDER");
        roomScalars.reset();
        roomUsage.reset();
        roomSources.reset();
        auto& viewFields = const_cast<rhi::TextureViewDesc&>(originalView->desc());
        const auto saved = viewFields;
        const auto rejectsMutation = [&](auto mutate) {
            mutate(viewFields);
            assert(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota, graphQuota}, [&]() noexcept {
                    return Sources::validateOriginal(sources);
                }));
            viewFields = saved;
        };
        rejectsMutation([](auto& d) {
            d.format = rhi::TextureFormat::Unknown;
        });
        rejectsMutation([](auto& d) {
            d.usage = rhi::TextureUsage::None;
        });
        rejectsMutation([](auto& d) {
            d.dimension = rhi::TextureViewDimension::Texture3D;
        });
        rejectsMutation([](auto& d) {
            ++d.baseMipLevel;
        });
        rejectsMutation([](auto& d) {
            ++d.mipLevelCount;
        });
        rejectsMutation([](auto& d) {
            ++d.baseArrayLayer;
        });
        rejectsMutation([](auto& d) {
            ++d.arrayLayerCount;
        });
        rejectsMutation([](auto& d) {
            d.aspect = rhi::TextureAspect::Depth;
        });
        auto& parentFields = const_cast<rhi::TextureDesc&>(parent->originalObject()->desc());
        ++parentFields.extent.width;
        assert(View::validateOriginal(view));
        --parentFields.extent.width;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, graphQuota}, [&]() noexcept {
                if (const auto* id = Sources::validateOriginal(sources))
                    return id;
                if (const auto* id = usage->compareOriginalReference(0u, *originalView))
                    return id;
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!scalars->executionDiagnostic(backend))
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        std::weak_ptr<const Parent> parentObserved = parent;
        std::weak_ptr<const ViewLease> viewObserved = lease;
        entries = {};
        view.reset();
        parent.reset();
        peer.reset();
        lease.reset();
        usage.reset();
        store.reset();
        sources.reset();
        assert(!parentObserved.expired() && !viewObserved.expired()
            && !Scalars::validateOriginal(scalars));
        scalars.reset();
        assert(parentObserved.expired() && viewObserved.expired());
        parentObserved.reset();
        viewObserved.reset();
        assert(!quota->liveBytes() && !graphQuota->liveBytes() && !foreignQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_VIEW_SOURCE_OK %llu %llu canonical-view-parent table-parent-required exact-fields Graph-owners7 forged-foreign-parent-quota-reject cold-pressure caller-drop hot-allocation0 pass-gate-closed\n",
            static_cast<unsigned long long>(viewPeak),
            static_cast<unsigned long long>(tablePeak));
    }
};
} // namespace arp::test
