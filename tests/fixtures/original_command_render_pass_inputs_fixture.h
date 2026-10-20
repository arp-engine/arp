#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandRenderPassInputsFixture final {
    static constexpr std::uint64_t metadataBytes = 65536u;

public:
    static void verify(rhi::GpuDevice& device)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        using Pass = rhi::RenderPassArguments;
        using B = rhi::OriginalCommandAdmissionBudget;
        using K = rhi::CommandUsageKind;
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        rhi::TextureDesc color;
        color.extent = {2u, 2u, 1u};
        color.format = rhi::TextureFormat::Rgba8Unorm;
        color.usage = rhi::TextureUsage::RenderAttachment;
        color.sampleCount = 4u;
        auto resolve = color;
        resolve.sampleCount = 1u;
        auto depth = color;
        depth.format = rhi::TextureFormat::D32Float;
        const std::array<rhi::TextureDesc, 3> physical{color, resolve, depth};
        std::array<Sources::Source, 6> retained{};
        std::array<const rhi::GpuTextureView*, 3> views{};
        for (std::size_t i = 0u; i < physical.size(); ++i) {
            auto texture = device.createTexture(physical[i]);
            assert(texture);
            rhi::TextureViewDesc view;
            view.format = physical[i].format;
            view.usage = rhi::TextureUsage::RenderAttachment;
            view.aspect = i == 2u ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
            auto viewObject = device.createTextureView(*texture, view);
            assert(viewObject);
            views[i] = viewObject.get();
            auto parent = rhi::OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
            auto lease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                viewObject, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
            auto source = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                lease, parent, {1u, true, 5u, metadataBytes}, sourceQuota, diagnostic);
            assert(source);
            retained[i] = parent;
            retained[i + 3u] = source;
        }
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = 6u;
        budget.maxReferences = 6u;
        budget.maxInputDescriptors = 2u;
        budget.maxGraphOwners = 21u;
        budget.maxPassColorAttachments = 2u;
        budget.maxPassAttachmentReferences = 6u;
        budget.maxPassSubpasses = 4u;
        budget.maxPassAttachmentIndices = 6u;
        budget.maxPassDependencies = 2u;
        budget.maxLabelBytes = 46u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 3u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] =
            3u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Source)] = metadataBytes;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Graph)] = metadataBytes;
        const std::array<rhi::CommandUsage, 6> commands{{{K::InsertDebugMarker, 0u, 0u},
            {K::BeginRenderPass, 0u, 3u},
            {K::EndRenderPass, 3u, 0u},
            {K::BeginRenderPass, 3u, 3u},
            {K::EndRenderPass, 6u, 0u},
            {K::Finish, 6u, 0u}}};
        const std::array<Usage::Reference, 6> refs{{{3u}, {4u}, {5u}, {3u}, {4u}, {5u}}};
        auto sources = Sources::prepareOriginal(
            retained.data(), retained.size(), budget, sourceQuota, diagnostic);
        assert(sources);
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            graphQuota,
            diagnostic);
        assert(usage);
        const std::array<Store::Input, 6> fields{
            {{*Scalar::capture(K::InsertDebugMarker, Scalar::None{}), "scalar"},
                {*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 6u, 0u, 6u, 0u, metadataBytes},
            graphQuota,
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        assert(scalars && scalars->retainedOwnerCount() == 19u);
        rhi::RenderPassDesc pass;
        pass.colorTargetName = "target";
        pass.label = "pass";
        rhi::RenderPassColorAttachmentDesc attachment;
        attachment.label = "color";
        attachment.target = views[0]->id();
        attachment.format = color.format;
        attachment.loadOp = rhi::LoadOp::Clear;
        attachment.storeOp = rhi::StoreOp::Store;
        attachment.hasResolveTarget = true;
        attachment.resolveTarget = views[1]->id();
        attachment.clearColor = {0.1f, 0.2f, 0.3f, 1.0f};
        pass.colorAttachments.push_back(attachment);
        pass.hasDepthStencilAttachment = true;
        pass.depthStencilAttachment.label = "depth";
        pass.depthStencilAttachment.target = views[2]->id();
        pass.depthStencilAttachment.format = depth.format;
        pass.depthStencilAttachment.depthLoadOp = rhi::LoadOp::Clear;
        pass.depthStencilAttachment.depthStoreOp = rhi::StoreOp::Store;
        pass.depthStencilAttachment.stencilLoadOp = rhi::LoadOp::DontCare;
        pass.depthStencilAttachment.stencilStoreOp = rhi::StoreOp::DontCare;
        pass.depthStencilAttachment.depthClearValue = 0.75f;
        pass.depthStencilAttachment.stencilClearValue = 7u;
        pass.subpasses.resize(2u);
        pass.subpasses[0].colorAttachmentIndices = {0u};
        pass.subpasses[0].useDepthStencilAttachment = true;
        pass.subpasses[1].colorAttachmentIndices = {0u};
        pass.subpasses[1].inputAttachmentIndices = {0u};
        pass.subpasses[1].readDepthInputAttachment = true;
        pass.subpassDependencies = {{0u, 1u}};
        const rhi::RenderPassArgumentsBudget local{1u, true, 4u, 4u, 16u, 4u, 128u, metadataBytes};
        const auto passFactory = [&] {
            return Pass::prepareOriginal(pass, local, graphQuota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(graphQuota);
        const auto passPeak = pressure.measure(passFactory);
        auto snapshot = passFactory();
        assert(snapshot);
        std::array<Inputs::OriginalDescriptor, 2> descriptors{{{1u, snapshot}, {3u, snapshot}}};
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic);
        };
        const auto inputPeak = pressure.measure(factory);
        pressure.verify(passPeak, passFactory);
        pressure.verify(inputPeak, factory);
        const auto baseline = graphQuota->liveBytes();
        {
            core::CpuMemoryQuotaLease full(graphQuota, graphQuota->limitBytes() - baseline);
            assert(full.charged() && !factory() && !passFactory());
        }
        assert(graphQuota->liveBytes() == baseline);
        verifyCapacityConsumption(pass);
        for (unsigned limit = 0u; limit < 8u; ++limit) {
            auto boundedBudget = budget;
            if (limit == 0u)
                --boundedBudget.maxPassColorAttachments;
            if (limit == 1u)
                --boundedBudget.maxPassAttachmentReferences;
            if (limit == 2u)
                --boundedBudget.maxPassSubpasses;
            if (limit == 3u)
                --boundedBudget.maxPassAttachmentIndices;
            if (limit == 4u)
                --boundedBudget.maxPassDependencies;
            if (limit == 5u)
                --boundedBudget.maxLabelBytes;
            if (limit == 6u)
                --boundedBudget.maxGraphOwners;
            if (limit == 7u)
                --boundedBudget.maxInputDescriptors;
            auto boundedSources = Sources::prepareOriginal(
                retained.data(), retained.size(), boundedBudget, sourceQuota, diagnostic);
            assert(boundedSources);
            auto boundedUsage = Usage::prepareOriginal(boundedSources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                graphQuota,
                diagnostic);
            assert(boundedUsage);
            auto boundedScalars =
                Scalars::prepareOriginal(boundedUsage, store, graphQuota, diagnostic);
            assert(boundedScalars);
            assert(!Inputs::prepareOriginal(
                boundedScalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic));
            const auto* expected = limit == 1u ? "RHI-ORIGINAL-INPUTS-PASS-REFERENCE-CAPACITY"
                : limit == 5u                  ? "RHI-RENDER-PASS-ARGUMENTS-TEXT"
                : limit == 6u                  ? "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY"
                : limit == 7u                  ? "RHI-ORIGINAL-INPUTS-CAPACITY"
                                               : "RHI-RENDER-PASS-ARGUMENTS-COUNT";
            assert(std::string_view(diagnostic) == expected);
        }
        assert(graphQuota->liveBytes() == baseline);
        auto originalScalars = scalars;
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
        };
        descriptors[0].commandIndex = 0u;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors[0].commandIndex = 1u;
        descriptors[1].commandIndex = SIZE_MAX;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors[1].commandIndex = 3u;
        auto forged = std::shared_ptr<const Pass>(sourceQuota, snapshot.get());
        descriptors[0].arguments = forged;
        reject("RHI-ORIGINAL-INPUTS-PASS-OWNER");
        descriptors[0].arguments = snapshot;
        forged.reset();
        descriptors[0].arguments = std::shared_ptr<const Pass>{};
        reject("RHI-ORIGINAL-INPUTS-PASS-OWNER");
        descriptors[0].arguments = snapshot;
        auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto foreign = Pass::prepareOriginal(pass, local, foreignQuota, diagnostic);
        assert(foreign);
        descriptors[0].arguments = foreign;
        reject("RHI-ORIGINAL-INPUTS-PASS-QUOTA");
        descriptors[0].arguments = snapshot;
        foreign.reset();
        assert(!foreignQuota->liveBytes());
        auto twin = passFactory();
        assert(twin);
        descriptors[1].arguments = twin;
        reject("RHI-ORIGINAL-INPUTS-OWNER-CAPACITY");
        descriptors[1].arguments = snapshot;
        twin.reset();
        const auto saved = rhi::RenderPassStateCopy::copy(pass);
        const auto rejectPass = [&](auto mutate, const char* id) {
            mutate(pass);
            auto changed = passFactory();
            assert(changed);
            descriptors[0].arguments = changed;
            reject(id);
            descriptors[0].arguments = snapshot;
            pass = saved;
        };
        rejectPass(
            [](auto& d) {
                d.colorAttachments[0].target = d.colorAttachments[0].resolveTarget;
            },
            "RHI-ORIGINAL-INPUTS-PASS-REFERENCE");
        rejectPass(
            [](auto& d) {
                d.colorAttachments[0].resolveTarget = d.depthStencilAttachment.target;
            },
            "RHI-ORIGINAL-INPUTS-PASS-REFERENCE");
        rejectPass(
            [](auto& d) {
                d.colorAttachments[0].hasResolveTarget = false;
            },
            "RHI-ORIGINAL-INPUTS-PASS-REFERENCE");
        rejectPass(
            [](auto& d) {
                d.colorAttachments[0].format = rhi::TextureFormat::D32Float;
            },
            "RHI-ORIGINAL-INPUTS-PASS-VIEW");
        rejectPass(
            [](auto& d) {
                d.depthStencilAttachment.format = rhi::TextureFormat::Rgba8Unorm;
            },
            "RHI-ORIGINAL-INPUTS-PASS-VIEW");
        // L2 Unknown attachment format remains an explicit field; binding uses
        // the exact canonical view format without choosing a native default.
        {
            pass.colorAttachments[0].format = rhi::TextureFormat::Unknown;
            auto unspecified = passFactory();
            assert(unspecified);
            descriptors[0].arguments = unspecified;
            descriptors[1].arguments = unspecified;
            assert(factory());
            descriptors[0].arguments = snapshot;
            descriptors[1].arguments = snapshot;
            pass = saved;
        }
        const std::array<Inputs::IncomingReference, 6> raw{
            views[0], views[1], views[2], views[0], views[1], views[2]};
        const auto incoming = [&](std::size_t index, const rhi::RenderPassDesc* desc) {
            return Inputs::Incoming{fields[index],
                desc,
                raw.data() + commands[index].firstReference,
                commands[index].referenceCount};
        };
        auto frozen = factory();
        assert(frozen && !Inputs::validateOriginal(frozen) && frozen->retainedOwnerCount() == 21u);
        verifyFields(frozen, incoming, pass);
        auto& physicalView = const_cast<rhi::TextureViewDesc&>(views[0]->desc());
        physicalView.format = rhi::TextureFormat::Unknown;
        assert(Inputs::validateOriginal(frozen));
        physicalView.format = color.format;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, graphQuota}, [&]() noexcept {
                if (const auto* id = Inputs::validateOriginal(frozen))
                    return id;
                if (const auto* id = frozen->compareOriginalInput(1u, incoming(1u, &pass)))
                    return id;
                if (const auto* id = frozen->compareOriginalInput(3u, incoming(3u, &pass)))
                    return id;
                if (!frozen->compareOriginalInput(1u, incoming(1u, nullptr)))
                    return "TEST-NULL-PASS-ACCEPTED";
                ++pass.depthStencilAttachment.stencilClearValue;
                const auto* mismatch = frozen->compareOriginalInput(1u, incoming(1u, &pass));
                --pass.depthStencilAttachment.stencilClearValue;
                if (!mismatch)
                    return "TEST-ALTERED-PASS-ACCEPTED";
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!frozen->executionDiagnostic(backend) || frozen->nativeExecutionProven())
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        std::weak_ptr<const Pass> observed = snapshot;
        descriptors[0].arguments = rhi::TransferCopyArguments::finish();
        descriptors[1].arguments = rhi::TransferCopyArguments::finish();
        retained = {};
        scalars.reset();
        originalScalars.reset();
        store.reset();
        usage.reset();
        sources.reset();
        snapshot.reset();
        assert(!observed.expired() && !Inputs::validateOriginal(frozen));
        frozen.reset();
        assert(observed.expired());
        observed.reset();
        assert(!sourceQuota->liveBytes() && !graphQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_PASS_INPUTS_OK %llu %llu passes2 refs6 owner21 canonical-Graph-quota color-resolve-depth exact-fields-labels-subpasses aggregate-bounds shared-sequence cold-pressure caller-drop hot-allocation0 gates4-closed\n",
            static_cast<unsigned long long>(passPeak),
            static_cast<unsigned long long>(inputPeak));
    }

private:
    static void verifyCapacityConsumption(const rhi::RenderPassDesc& pass)
    {
        rhi::RenderPassArgumentsBudget remaining{1u, true, 1u, 2u, 3u, 1u, 20u, metadataBytes};
        assert(!rhi::RenderPassArguments::consumeInputCapacity(pass, remaining));
        assert(!remaining.maxColorAttachments && !remaining.maxSubpasses
            && !remaining.maxSubpassAttachmentIndices && !remaining.maxDependencies
            && !remaining.maxTextBytes);
        const auto before = remaining;
        assert(rhi::RenderPassArguments::consumeInputCapacity(pass, remaining));
        assert(remaining.maxColorAttachments == before.maxColorAttachments
            && remaining.maxSubpasses == before.maxSubpasses
            && remaining.maxSubpassAttachmentIndices == before.maxSubpassAttachmentIndices
            && remaining.maxDependencies == before.maxDependencies
            && remaining.maxTextBytes == before.maxTextBytes);
        auto input = rhi::RenderPassStateCopy::copy(pass);
        input.depthStencilAttachment.depthReadOnly = true;
        remaining = {1u, true, 1u, 2u, 3u, 1u, 20u, metadataBytes};
        const auto unchanged = remaining;
        assert(rhi::RenderPassArguments::consumeInputCapacity(input, remaining));
        assert(remaining.maxColorAttachments == unchanged.maxColorAttachments
            && remaining.maxSubpasses == unchanged.maxSubpasses
            && remaining.maxSubpassAttachmentIndices == unchanged.maxSubpassAttachmentIndices
            && remaining.maxDependencies == unchanged.maxDependencies
            && remaining.maxTextBytes == unchanged.maxTextBytes);
    }

    template <class Incoming>
    static void verifyFields(const std::shared_ptr<const rhi::OriginalCommandInputs>& frozen,
        Incoming&& incoming,
        const rhi::RenderPassDesc& pass)
    {
        const auto rejects = [&](auto mutate) {
            auto changed = rhi::RenderPassStateCopy::copy(pass);
            mutate(changed);
            assert(std::string_view(frozen->compareOriginalInput(1u, incoming(1u, &changed)))
                == "RHI-ORIGINAL-INPUTS-PAYLOAD");
        };
        rejects([](auto& d) {
            d.label += "x";
        });
        rejects([](auto& d) {
            d.colorTargetName += "x";
        });
        rejects([](auto& d) {
            d.layeredRendering = true;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].label += "x";
        });
        rejects([](auto& d) {
            d.colorAttachments[0].loadOp = rhi::LoadOp::Load;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].storeOp = rhi::StoreOp::DontCare;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].clearColor.r += 0.1f;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].clearColor.g += 0.1f;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].clearColor.b += 0.1f;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].clearColor.a -= 0.1f;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].hasResolveTarget = false;
        });
        rejects([](auto& d) {
            d.colorAttachments[0].resolveTarget = {};
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.label += "x";
        });
        rejects([](auto& d) {
            d.hasDepthStencilAttachment = false;
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.depthLoadOp = rhi::LoadOp::Load;
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.depthStoreOp = rhi::StoreOp::DontCare;
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.stencilLoadOp = rhi::LoadOp::Load;
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.stencilStoreOp = rhi::StoreOp::Store;
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.depthClearValue = 0.5f;
        });
        rejects([](auto& d) {
            ++d.depthStencilAttachment.stencilClearValue;
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.depthReadOnly = true;
        });
        rejects([](auto& d) {
            d.depthStencilAttachment.stencilReadOnly = true;
        });
        rejects([](auto& d) {
            d.subpasses[0].colorAttachmentIndices[0] = 1u;
        });
        rejects([](auto& d) {
            d.subpasses[1].inputAttachmentIndices[0] = 1u;
        });
        rejects([](auto& d) {
            d.subpasses[0].useDepthStencilAttachment = false;
        });
        rejects([](auto& d) {
            d.subpasses[1].readDepthInputAttachment = false;
        });
        rejects([](auto& d) {
            d.subpasses.pop_back();
        });
        rejects([](auto& d) {
            d.subpassDependencies[0].srcSubpass = 1u;
        });
        rejects([](auto& d) {
            d.subpassDependencies[0].dstSubpass = 0u;
        });
        rejects([](auto& d) {
            d.subpassDependencies.clear();
        });
    }
};
} // namespace arp::test
