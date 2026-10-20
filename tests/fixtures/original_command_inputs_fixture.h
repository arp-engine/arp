#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandInputsFixture final {
    static constexpr std::uint64_t metadataBytes = 65536u;

public:
    static void verify(rhi::GpuDevice& device, bool requireRegistered = false)
    {
        using Inputs = rhi::OriginalCommandInputs;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using K = rhi::CommandUsageKind;
        using B = rhi::OriginalCommandAdmissionBudget;
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        rhi::BufferDesc buffer;
        buffer.sizeBytes = 1024u;
        buffer.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        rhi::TextureDesc texture;
        texture.extent = {2u, 2u, 1u};
        texture.format = rhi::TextureFormat::Rgba8Unorm;
        texture.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
        texture.mipLevelCount = 2u;
        auto b0 = device.createBuffer(buffer), b1 = device.createBuffer(buffer);
        auto t0 = device.createTexture(texture), t1 = device.createTexture(texture);
        assert(b0 && b1 && t0 && t1);
        const std::array<Inputs::IncomingReference, 4> raw{b0.get(), b1.get(), t0.get(), t1.get()};
        const auto bid0 = b0->id(), bid1 = b1->id(), tid0 = t0->id(), tid1 = t1->id();
        const rhi::OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 2u, metadataBytes};
        std::array<Sources::Source, 4> retained{rhi::OriginalCommandBufferLease::prepareOriginal(
                                                    b0, leaseBudget, sourceQuota, diagnostic),
            rhi::OriginalCommandBufferLease::prepareOriginal(
                b1, leaseBudget, sourceQuota, diagnostic),
            rhi::OriginalCommandTextureLease::prepareOriginal(
                t0, leaseBudget, sourceQuota, diagnostic),
            rhi::OriginalCommandTextureLease::prepareOriginal(
                t1, leaseBudget, sourceQuota, diagnostic)};
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = 12u;
        budget.maxReferences = 14u;
        budget.maxInputDescriptors = 9u;
        budget.maxGraphOwners = 13u;
        budget.maxStateObservations = 4u;
        budget.maxSourceOwners[0] = 2u;
        budget.maxSourceOwners[1] = 2u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Source)] = metadataBytes;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Graph)] = metadataBytes;
        std::array<rhi::CommandUsage, 12> commands{{{K::BufferBarrier, 0u, 1u},
            {K::TextureBarrier, 1u, 1u},
            {K::BufferMemoryBarrier, 2u, 1u},
            {K::TextureMemoryBarrier, 3u, 1u},
            {K::AliasBarrier, 4u, 2u},
            {K::BeginTransferPass, 6u, 0u},
            {K::BufferCopy, 6u, 2u},
            {K::BufferToTextureCopy, 8u, 2u},
            {K::TextureToBufferCopy, 10u, 2u},
            {K::TextureToTextureCopy, 12u, 2u},
            {K::EndTransferPass, 14u, 0u},
            {K::Finish, 14u, 0u}}};
        const std::array<Usage::Reference, 14> refs{
            {{0u}, {2u}, {1u}, {3u}, {0u}, {1u}, {0u}, {1u}, {0u}, {2u}, {2u}, {1u}, {2u}, {3u}}};
        // Distinct texture operands: the old source fixture's self-copy only
        // tested signatures, and cannot stand in for full transfer semantics.
        rhi::ResourceBarrierDesc bb;
        bb.resource = bid0;
        bb.after = rhi::ResourceState::CopySrc;
        rhi::ResourceBarrierDesc tb = bb;
        tb.resource = tid0;
        rhi::ResourceMemoryBarrierDesc bm;
        bm.resource = bid1;
        rhi::ResourceMemoryBarrierDesc tm = bm;
        tm.resource = tid1;
        rhi::AliasingBarrierDesc alias;
        alias.beforeResource = bid0;
        alias.afterResource = bid1;
        rhi::BufferCopyDesc bc{bid0, bid1, 4u, 8u, 16u};
        rhi::BufferToTextureCopyDesc bt;
        bt.sourceBuffer = bid0;
        bt.destinationTexture = tid0;
        bt.extent = texture.extent;
        bt.bytesPerRow = 256u;
        bt.rowsPerImage = 2u;
        rhi::TextureToBufferCopyDesc tbuf;
        tbuf.sourceTexture = tid0;
        tbuf.destinationBuffer = bid1;
        tbuf.extent = texture.extent;
        tbuf.bytesPerRow = 256u;
        tbuf.rowsPerImage = 2u;
        rhi::TextureToTextureCopyDesc tt;
        tt.sourceTexture = tid0;
        tt.destinationTexture = tid1;
        tt.extent = texture.extent;
        std::array<Inputs::OriginalDescriptor, 9> descriptors{
            {{0u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, bb)},
                {1u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, tb)},
                {2u, *rhi::ResourceDependencyArguments::capture(K::BufferMemoryBarrier, bm)},
                {3u, *rhi::ResourceDependencyArguments::capture(K::TextureMemoryBarrier, tm)},
                {4u, rhi::ResourceDependencyArguments::capture(alias)},
                {6u, *rhi::TransferCopyArguments::capture(bc)},
                {7u, *rhi::TransferCopyArguments::capture(bt)},
                {8u, *rhi::TransferCopyArguments::capture(tbuf)},
                {9u, *rhi::TransferCopyArguments::capture(tt)}}};
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
        if (requireRegistered) {
            assert(!Sources::validateRegisteredLifetime(sources, device));
            std::uint32_t owners = 0u, registrations = 0u;
            assert(usage->retainedOwnerCount() == 10u);
            assert(!Sources::registeredOwnerUnion(sources, 10u, 17u, 4u, owners, registrations)
                && owners == 17u && registrations == 4u);
            assert(std::string_view(
                       Sources::registeredOwnerUnion(sources, 10u, 16u, 4u, owners, registrations))
                == "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
            assert(std::string_view(
                       Sources::registeredOwnerUnion(sources, 10u, 17u, 3u, owners, registrations))
                == "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
            assert(std::string_view(Sources::registeredOwnerUnion(
                       sources, UINT32_MAX, UINT32_MAX, 4u, owners, registrations))
                == "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        }
        // Reuse the original variable store; build records in core bounded storage.
        core::BoundedSequence<Store::Input> scalarInput(commands.size());
        for (const auto& command : commands)
            scalarInput.append({*Scalar::capture(command.kind, Scalar::None{})});
        const rhi::CommandVariableArgumentsBudget variableBudget{
            1u, true, 12u, 0u, 0u, 0u, metadataBytes};
        auto store = Store::prepareOriginal(
            scalarInput.values().data(), commands.size(), variableBudget, graphQuota, diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        assert(scalars && scalars->retainedOwnerCount() == 12u);
        const auto baseline = graphQuota->liveBytes();
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(graphQuota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(graphQuota, graphQuota->limitBytes() - baseline);
            assert(full.charged() && !factory());
        }
        const auto saved = descriptors;
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id
                && graphQuota->liveBytes() == baseline);
        };
        descriptors[0].commandIndex = 1u;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors = saved;
        descriptors[0].commandIndex = SIZE_MAX;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors = saved;
        descriptors[1] = descriptors[0];
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors = saved;
        descriptors[0].arguments = descriptors[1].arguments;
        reject("RHI-ORIGINAL-INPUTS-KIND");
        descriptors = saved;
        auto reversed = bc;
        reversed.sourceBuffer = bid1;
        reversed.destinationBuffer = bid0;
        descriptors[5].arguments = *rhi::TransferCopyArguments::capture(reversed);
        reject("RHI-ORIGINAL-INPUTS-RESOURCE");
        descriptors = saved;
        auto foreignId = bc;
        foreignId.sourceBuffer = {UINT64_MAX};
        descriptors[5].arguments = *rhi::TransferCopyArguments::capture(foreignId);
        reject("RHI-ORIGINAL-INPUTS-RESOURCE");
        descriptors = saved;
        auto changedBarrier = bb;
        changedBarrier.resource = bid1;
        descriptors[0].arguments =
            *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, changedBarrier);
        reject("RHI-ORIGINAL-INPUTS-RESOURCE");
        descriptors = saved;
        auto changedAlias = alias;
        changedAlias.afterResource = bid0;
        descriptors[4].arguments = rhi::ResourceDependencyArguments::capture(changedAlias);
        reject("RHI-ORIGINAL-INPUTS-RESOURCE");
        descriptors = saved;
        assert(!Inputs::prepareOriginal(
                   scalars, descriptors.data(), descriptors.size() - 1u, graphQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-ORDER");
        assert(
            !Inputs::prepareOriginal(scalars, nullptr, descriptors.size(), graphQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-CAPACITY");
        assert(!Inputs::prepareOriginal(
                   scalars, descriptors.data(), descriptors.size(), sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-GRAPH-QUOTA");
        auto forgedScalars = std::shared_ptr<const Scalars>(sourceQuota, scalars.get());
        assert(!Inputs::prepareOriginal(
            forgedScalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic));
        forgedScalars.reset();
        for (unsigned limit = 0u; limit < 2u; ++limit) {
            auto boundedBudget = budget;
            if (limit == 0u)
                --boundedBudget.maxInputDescriptors;
            else
                --boundedBudget.maxGraphOwners;
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
            assert(std::string_view(diagnostic)
                == (limit == 0u ? "RHI-ORIGINAL-INPUTS-CAPACITY"
                                : "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY"));
        }
        auto frozen = factory();
        assert(frozen && !Inputs::validateOriginal(frozen) && frozen->retainedOwnerCount() == 13u);
        for (const auto& record : frozen->descriptors())
            assert(frozen->descriptorAt(record.commandIndex) == &record.arguments);
        assert(!frozen->descriptorAt(5u) && !frozen->descriptorAt(commands.size())
            && !frozen->descriptorAt(SIZE_MAX));
        if (requireRegistered) {
            const auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            assert(!rhi::OriginalCommandBufferStates::prepareOriginal(
                frozen, device, {1u, true, 1u, 4u, 4u, 0u, 21u, metadataBytes}, quota, diagnostic));
            assert(std::string_view(diagnostic) == "RHI-ORIGINAL-BUFFER-STATES-SOURCE-UNSUPPORTED"
                && !quota->liveBytes());
        }
        auto forged = std::shared_ptr<const Inputs>(sourceQuota, frozen.get());
        assert(Inputs::validateOriginal(forged));
        forged.reset();
        const auto incomingAt = [&](std::size_t i) {
            Inputs::Incoming incoming{scalarInput.values()[i], std::monostate{}};
            for (const auto& entry : saved)
                if (entry.commandIndex == i)
                    std::visit(
                        [&](const auto& value) {
                            using T = std::decay_t<decltype(value)>;
                            if constexpr (std::is_same_v<T, rhi::TransferCopyArguments>
                                || std::is_same_v<T, rhi::ResourceDependencyArguments>)
                                incoming.descriptor = value;
                        },
                        entry.arguments);
            return incoming;
        };
        std::array<Inputs::IncomingReference, 14> rawRefs{};
        for (std::size_t i = 0u; i < refs.size(); ++i)
            rawRefs[i] = raw[refs[i].sourceIndex];
        const auto withReferences = [&](std::size_t i) {
            auto incoming = incomingAt(i);
            incoming.referenceCount = commands[i].referenceCount;
            incoming.references =
                incoming.referenceCount ? rawRefs.data() + commands[i].firstReference : nullptr;
            return incoming;
        };
        verifyCompleteFields(frozen, withReferences, bc, bt, tbuf, tt, bb, tb, bm, tm, alias);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{graphQuota, sourceQuota}, [&]() noexcept {
                if (const auto* id = Inputs::validateOriginal(frozen))
                    return id;
                if (requireRegistered) {
                    std::uint32_t owners = 0u, registrations = 0u;
                    if (const auto* id = Sources::registeredOwnerUnion(
                            sources, 10u, 17u, 4u, owners, registrations))
                        return id;
                }
                for (std::size_t i = 0u; i < commands.size(); ++i)
                    if (const auto* id = frozen->compareOriginalInput(i, withReferences(i)))
                        return id;
                auto altered = withReferences(6u);
                altered.references = nullptr;
                if (!frozen->compareOriginalInput(6u, altered))
                    return "TEST-MISSING-REFERENCE-ACCEPTED";
                altered = withReferences(6u);
                altered.referenceCount = SIZE_MAX;
                if (!frozen->compareOriginalInput(6u, altered))
                    return "TEST-REFERENCE-COUNT-ACCEPTED";
                const auto original = rawRefs[6];
                rawRefs[6] = raw[1];
                altered = withReferences(6u);
                const auto* mismatch = frozen->compareOriginalInput(6u, altered);
                rawRefs[6] = original;
                if (!mismatch)
                    return "TEST-FOREIGN-WRAPPER-ACCEPTED";
                altered = withReferences(5u);
                altered.descriptor = std::get<rhi::TransferCopyArguments>(saved[5].arguments);
                if (!frozen->compareOriginalInput(5u, altered))
                    return "TEST-SCALAR-DESCRIPTOR-ACCEPTED";
                if (!frozen->compareOriginalInput(SIZE_MAX, altered))
                    return "TEST-INDEX-ACCEPTED";
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!frozen->executionDiagnostic(backend) || frozen->nativeExecutionProven())
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        // Actual unsupported sources fail at the existing signature gate;
        // complete typed descriptor representation cannot bypass that gate.
        for (const auto kind :
            {K::BeginRenderPass, K::BufferUpload, K::TextureUpload, K::TextureReadback}) {
            auto bad = commands;
            bad[6].kind = kind;
            assert(!Usage::prepareOriginal(sources,
                       bad.data(),
                       bad.size(),
                       refs.data(),
                       refs.size(),
                       graphQuota,
                       diagnostic)
                && std::string_view(diagnostic) == "RHI-COMMAND-USAGE-SIGNATURE");
            assert(!Inputs::sourceAdapterDiagnostic(rhi::CommandInputContract::family(kind)));
        }
        assert(Inputs::sourceAdapterDiagnostic(Inputs::Family::Unsupported));
        verifyFullCopies(
            sources, commands, refs, scalarInput.values().data(), saved, bt, tbuf, tt, rawRefs);
        // Explicit mips are never silently truncated to the single-mip store.
        bt.mipLayouts.push_back({});
        tbuf.mipLayouts.push_back({});
        tt.mipLayouts.push_back({});
        assert(!rhi::TransferCopyArguments::capture(bt)
            && !rhi::TransferCopyArguments::capture(tbuf)
            && !rhi::TransferCopyArguments::capture(tt));
        std::weak_ptr<const Store> observedStore = store;
        std::weak_ptr<const Sources> observedSources = sources;
        descriptors[0].commandIndex = SIZE_MAX;
        retained = {};
        scalars.reset();
        usage.reset();
        store.reset();
        sources.reset();
        assert(!observedStore.expired() && !observedSources.expired()
            && !Inputs::validateOriginal(frozen));
        frozen.reset();
        assert(observedStore.expired() && observedSources.expired());
        observedStore.reset();
        observedSources.reset();
        assert(!graphQuota->liveBytes() && !sourceQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_INPUTS_OK %llu copies4-dependencies5 exact-positional-fields-wrappers Graph-quota owners-descriptors cold-pressure caller-drop missing-adapters-mips-reject gates4-closed hot-allocation0\n",
            static_cast<unsigned long long>(peak));
    }

private:
    static void verifyFullCopies(
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& originalSources,
        const std::array<rhi::CommandUsage, 12>& commands,
        const std::array<rhi::OriginalCommandUsage::Reference, 14>& references,
        const rhi::CommandVariableArguments::Input* scalarInputs,
        std::array<rhi::OriginalCommandInputs::OriginalDescriptor, 9> descriptors,
        rhi::BufferToTextureCopyDesc bt,
        rhi::TextureToBufferCopyDesc tb,
        rhi::TextureToTextureCopyDesc tt,
        const std::array<rhi::OriginalCommandInputs::IncomingReference, 14>& rawReferences)
    {
        using Copy = rhi::TransferCopyInputSnapshot;
        using Inputs = rhi::OriginalCommandInputs;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Store = rhi::CommandVariableArguments;
        using Scalars = rhi::OriginalCommandScalars;
        const char* diagnostic = nullptr;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const auto& sourceQuota = originalSources->originalMetadataQuota();
        auto budget = originalSources->originalBudget();
        budget.maxGraphOwners = 16u;
        budget.maxMipMappings = 6u;
        const auto& original = originalSources->sources();
        auto sources = Sources::prepareOriginal(
            original.data(), original.size(), budget, sourceQuota, diagnostic);
        assert(sources);
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            references.data(),
            references.size(),
            quota,
            diagnostic);
        assert(usage);
        auto store = Store::prepareOriginal(scalarInputs,
            commands.size(),
            {1u, true, 12u, 0u, 0u, 0u, metadataBytes},
            quota,
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, quota, diagnostic);
        assert(scalars);
        bt.textureSubresources.mipLevelCount = 2u;
        tb.textureSubresources.mipLevelCount = 2u;
        tt.sourceSubresources.mipLevelCount = 2u;
        tt.destinationSubresources.mipLevelCount = 2u;
        bt.mipLayouts = {{0u, {2u, 2u, 1u}, 256u, 2u, 0u}, {1u, {1u, 1u, 1u}, 256u, 1u, 512u}};
        tb.mipLayouts = {{0u, {2u, 2u, 1u}, 256u, 2u, 0u}, {1u, {1u, 1u, 1u}, 256u, 1u, 512u}};
        tt.mipLayouts = {{0u, {2u, 2u, 1u}}, {1u, {1u, 1u, 1u}}};
        rhi::TransferCopyInputSnapshotBudget copyBudget{1u, true, 2u, metadataBytes};
        const auto factory0 = [&] {
            return Copy::prepareOriginal(bt, copyBudget, quota, diagnostic);
        };
        const auto factory1 = [&] {
            return Copy::prepareOriginal(tb, copyBudget, quota, diagnostic);
        };
        const auto factory2 = [&] {
            return Copy::prepareOriginal(tt, copyBudget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const std::array<std::uint64_t, 3> peaks{
            pressure.measure(factory0), pressure.measure(factory1), pressure.measure(factory2)};
        std::array<std::shared_ptr<const Copy>, 3> snapshots{factory0(), factory1(), factory2()};
        for (std::size_t i = 0u; i < snapshots.size(); ++i) {
            assert(snapshots[i]);
            descriptors[i + 6u].arguments = snapshots[i];
        }
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, descriptors.data(), descriptors.size(), quota, diagnostic);
        };
        const auto peak = pressure.measure(factory);
        // Measure every root before any probe saturates this quota's high water.
        pressure.verify(peaks[0], factory0);
        pressure.verify(peaks[1], factory1);
        pressure.verify(peaks[2], factory2);
        pressure.verify(peak, factory);
        const auto baseline = quota->liveBytes();
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
            assert(full.charged() && !factory() && !factory0());
        }
        --copyBudget.maxMipMappings;
        assert(!factory0() && std::string_view(diagnostic) == "RHI-COPY-SNAPSHOT-MIP-CAPACITY");
        ++copyBudget.maxMipMappings;
        copyBudget.contractVersion = 0u;
        assert(!factory0() && std::string_view(diagnostic) == "RHI-COPY-SNAPSHOT-VERSION");
        copyBudget.contractVersion = 1u;
        copyBudget.prototypeOptIn = false;
        assert(!factory0() && std::string_view(diagnostic) == "RHI-COPY-SNAPSHOT-OPT-IN");
        copyBudget.prototypeOptIn = true;
        --copyBudget.maxCpuMetadataBytes;
        assert(!factory0() && std::string_view(diagnostic) == "RHI-COPY-SNAPSHOT-QUOTA");
        ++copyBudget.maxCpuMetadataBytes;
        auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto foreign = Copy::prepareOriginal(bt, copyBudget, foreignQuota, diagnostic);
        assert(foreign);
        descriptors[6].arguments = foreign;
        assert(!factory() && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-COPY-QUOTA");
        descriptors[6].arguments = std::shared_ptr<const Copy>(sourceQuota, snapshots[0].get());
        assert(!factory() && std::string_view(diagnostic) == "RHI-COPY-SNAPSHOT-OWNER");
        descriptors[6].arguments = snapshots[0];
        foreign.reset();
        assert(!foreignQuota->liveBytes());
        for (unsigned limit = 0u; limit < 2u; ++limit) {
            auto boundedBudget = budget;
            if (limit == 0u)
                --boundedBudget.maxMipMappings;
            else
                --boundedBudget.maxGraphOwners;
            auto boundedSources = Sources::prepareOriginal(
                original.data(), original.size(), boundedBudget, sourceQuota, diagnostic);
            assert(boundedSources);
            auto boundedUsage = Usage::prepareOriginal(boundedSources,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                quota,
                diagnostic);
            assert(boundedUsage);
            auto boundedScalars = Scalars::prepareOriginal(boundedUsage, store, quota, diagnostic);
            assert(boundedScalars);
            assert(!Inputs::prepareOriginal(
                boundedScalars, descriptors.data(), descriptors.size(), quota, diagnostic));
            assert(std::string_view(diagnostic)
                == (limit == 0u ? "RHI-ORIGINAL-INPUTS-MIP-CAPACITY"
                                : "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY"));
        }
        {
            // Reusing the same canonical snapshot in two command positions
            // counts one owner, but every positional mip mapping still counts.
            auto repeatedBudget = budget;
            --repeatedBudget.maxGraphOwners;
            auto repeatedCommands = commands;
            repeatedCommands[8].kind = rhi::CommandUsageKind::BufferToTextureCopy;
            auto repeatedReferences = references;
            repeatedReferences[10].sourceIndex = 0u;
            repeatedReferences[11].sourceIndex = 2u;
            auto repeatedSources = Sources::prepareOriginal(
                original.data(), original.size(), repeatedBudget, sourceQuota, diagnostic);
            assert(repeatedSources);
            auto repeatedUsage = Usage::prepareOriginal(repeatedSources,
                repeatedCommands.data(),
                repeatedCommands.size(),
                repeatedReferences.data(),
                repeatedReferences.size(),
                quota,
                diagnostic);
            assert(repeatedUsage);
            core::BoundedSequence<Store::Input> repeatedFields(commands.size());
            for (const auto& command : repeatedCommands)
                repeatedFields.append({*rhi::CommandScalarArguments::capture(
                    command.kind, rhi::CommandScalarArguments::None{})});
            auto repeatedStore = Store::prepareOriginal(repeatedFields.values().data(),
                commands.size(),
                {1u, true, 12u, 0u, 0u, 0u, metadataBytes},
                quota,
                diagnostic);
            assert(repeatedStore);
            auto repeatedScalars =
                Scalars::prepareOriginal(repeatedUsage, repeatedStore, quota, diagnostic);
            assert(repeatedScalars);
            auto repeatedDescriptors = descriptors;
            repeatedDescriptors[7].arguments = snapshots[0];
            auto repeated = Inputs::prepareOriginal(repeatedScalars,
                repeatedDescriptors.data(),
                repeatedDescriptors.size(),
                quota,
                diagnostic);
            assert(repeated && repeated->retainedOwnerCount() == 15u
                && !Inputs::validateOriginal(repeated));
        }
        {
            rhi::BufferCopyDesc copy;
            assert(std::get<rhi::TransferCopyArguments>(descriptors[5].arguments).write(copy));
            const auto original =
                Copy::prepareOriginal(copy, {1u, true, 0u, metadataBytes}, quota, diagnostic);
            assert(original && !Copy::validateOriginal(original) && !original->mipMappingCount()
                && original->matches(copy) && !original->nativeExecutionProven());
            ++copy.destinationOffsetBytes;
            assert(!original->matches(copy));
        }
        auto frozen = factory();
        assert(frozen && frozen->retainedOwnerCount() == 16u);
        const auto inputAt = [&](std::size_t command, const auto& desc) {
            Inputs::Incoming input{scalarInputs[command], Inputs::IncomingCopy{&desc}};
            input.references = rawReferences.data() + commands[command].firstReference;
            input.referenceCount = commands[command].referenceCount;
            return input;
        };
        const auto verifyMips = [&](std::size_t command, const auto& desc) {
            const auto rejects = [&](auto mutate) {
                auto changed = desc;
                mutate(changed);
                assert(std::string_view(
                           frozen->compareOriginalInput(command, inputAt(command, changed)))
                    == "RHI-ORIGINAL-INPUTS-PAYLOAD");
            };
            rejects([](auto& d) {
                d.mipLayouts.pop_back();
            });
            rejects([](auto& d) {
                std::swap(d.mipLayouts[0], d.mipLayouts[1]);
            });
            rejects([](auto& d) {
                ++d.extent.width;
            });
            rejects([](auto& d) {
                ++d.extent.height;
            });
            rejects([](auto& d) {
                ++d.extent.depth;
            });
            for (std::size_t i = 0u; i < desc.mipLayouts.size(); ++i) {
                rejects([&](auto& d) {
                    ++d.mipLayouts[i].mipLevel;
                });
                rejects([&](auto& d) {
                    ++d.mipLayouts[i].extent.width;
                });
                rejects([&](auto& d) {
                    ++d.mipLayouts[i].extent.height;
                });
                rejects([&](auto& d) {
                    ++d.mipLayouts[i].extent.depth;
                });
                if constexpr (!std::is_same_v<std::decay_t<decltype(desc)>,
                                  rhi::TextureToTextureCopyDesc>) {
                    rejects([&](auto& d) {
                        ++d.mipLayouts[i].bytesPerRow;
                    });
                    rejects([&](auto& d) {
                        ++d.mipLayouts[i].rowsPerImage;
                    });
                    rejects([&](auto& d) {
                        ++d.mipLayouts[i].bufferOffsetBytes;
                    });
                }
            }
        };
        verifyMips(7u, bt);
        verifyMips(8u, tb);
        verifyMips(9u, tt);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, sourceQuota}, [&]() noexcept {
                if (const auto* id = Inputs::validateOriginal(frozen))
                    return id;
                if (const auto* id = frozen->compareOriginalInput(7u, inputAt(7u, bt)))
                    return id;
                if (const auto* id = frozen->compareOriginalInput(8u, inputAt(8u, tb)))
                    return id;
                if (const auto* id = frozen->compareOriginalInput(9u, inputAt(9u, tt)))
                    return id;
                auto wrong = inputAt(7u, tb);
                if (!frozen->compareOriginalInput(7u, wrong))
                    return "TEST-WRONG-COPY-TYPE-ACCEPTED";
                wrong.descriptor =
                    Inputs::IncomingCopy{static_cast<const rhi::BufferToTextureCopyDesc*>(nullptr)};
                if (!frozen->compareOriginalInput(7u, wrong))
                    return "TEST-NULL-COPY-ACCEPTED";
                ++bt.mipLayouts[1].bufferOffsetBytes;
                const auto* rejection = frozen->compareOriginalInput(7u, inputAt(7u, bt));
                --bt.mipLayouts[1].bufferOffsetBytes;
                if (!rejection)
                    return "TEST-ALTERED-MIP-ACCEPTED";
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!frozen->executionDiagnostic(backend))
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        std::weak_ptr<const Copy> observed = snapshots[0];
        for (auto& descriptor : descriptors)
            descriptor.arguments = rhi::TransferCopyArguments::finish();
        snapshots = {};
        scalars.reset();
        store.reset();
        usage.reset();
        sources.reset();
        assert(!observed.expired() && !Inputs::validateOriginal(frozen));
        frozen.reset();
        assert(observed.expired());
        observed.reset();
        assert(!quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_COPY_MIPS_OK %llu complete-mips3 mappings6 exact-fields-order-owners Graph-quota independent-owner-mip-caps cold-pressure caller-drop hot-allocation0 gates4-closed\n",
            static_cast<unsigned long long>(peak));
    }

    template <class IncomingFactory>
    static void verifyCompleteFields(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& frozen,
        IncomingFactory&& incoming,
        rhi::BufferCopyDesc bc,
        rhi::BufferToTextureCopyDesc bt,
        rhi::TextureToBufferCopyDesc tbuf,
        rhi::TextureToTextureCopyDesc tt,
        rhi::ResourceBarrierDesc bb,
        rhi::ResourceBarrierDesc tb,
        rhi::ResourceMemoryBarrierDesc bm,
        rhi::ResourceMemoryBarrierDesc tm,
        rhi::AliasingBarrierDesc alias)
    {
        using K = rhi::CommandUsageKind;
        const auto rejects = [&](std::size_t command, const auto& arguments) {
            auto input = incoming(command);
            input.descriptor = arguments;
            assert(std::string_view(frozen->compareOriginalInput(command, input))
                == "RHI-ORIGINAL-INPUTS-PAYLOAD");
        };
        const auto copy = [&](std::size_t command, const auto& original, auto mutate) {
            auto changed = original;
            mutate(changed);
            const auto captured = rhi::TransferCopyArguments::capture(changed);
            assert(captured);
            rejects(command, *captured);
        };
        copy(6u, bc, [](auto& d) {
            ++d.sourceOffsetBytes;
        });
        copy(6u, bc, [](auto& d) {
            ++d.destinationOffsetBytes;
        });
        copy(6u, bc, [](auto& d) {
            ++d.sizeBytes;
        });
        const auto textureCopy = [&](std::size_t command, const auto& d) {
            copy(command, d, [](auto& v) {
                ++v.extent.width;
            });
            copy(command, d, [](auto& v) {
                ++v.extent.height;
            });
            copy(command, d, [](auto& v) {
                ++v.extent.depth;
            });
        };
        textureCopy(7u, bt);
        textureCopy(8u, tbuf);
        textureCopy(9u, tt);
        copy(7u, bt, [](auto& d) {
            ++d.bufferOffsetBytes;
        });
        copy(7u, bt, [](auto& d) {
            ++d.bytesPerRow;
        });
        copy(7u, bt, [](auto& d) {
            ++d.rowsPerImage;
        });
        copy(8u, tbuf, [](auto& d) {
            ++d.bufferOffsetBytes;
        });
        copy(8u, tbuf, [](auto& d) {
            ++d.bytesPerRow;
        });
        copy(8u, tbuf, [](auto& d) {
            ++d.rowsPerImage;
        });
        copy(7u, bt, [](auto& d) {
            ++d.textureSubresources.baseMipLevel;
        });
        copy(8u, tbuf, [](auto& d) {
            ++d.textureSubresources.baseArrayLayer;
        });
        copy(9u, tt, [](auto& d) {
            ++d.sourceSubresources.mipLevelCount;
        });
        copy(9u, tt, [](auto& d) {
            ++d.destinationSubresources.arrayLayerCount;
        });
        const auto dependency =
            [&](std::size_t command, K kind, const auto& original, auto mutate) {
                auto changed = original;
                mutate(changed);
                const auto captured = rhi::ResourceDependencyArguments::capture(kind, changed);
                assert(captured);
                rejects(command, *captured);
            };
        for (const auto pair :
            {std::pair{0u, K::BufferBarrier}, std::pair{1u, K::TextureBarrier}}) {
            const auto& d = pair.first == 0u ? bb : tb;
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.before = rhi::ResourceState::CopySrc;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.after = rhi::ResourceState::CopyDst;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                ++v.textureSubresources.baseMipLevel;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.sourceStages = rhi::PipelineStage::Transfer;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.destinationStages = rhi::PipelineStage::Transfer;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.sourceAccess = rhi::ResourceAccess::TransferRead;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.destinationAccess = rhi::ResourceAccess::TransferWrite;
            });
        }
        for (const auto pair :
            {std::pair{2u, K::BufferMemoryBarrier}, std::pair{3u, K::TextureMemoryBarrier}}) {
            const auto& d = pair.first == 2u ? bm : tm;
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.sourceStages = rhi::PipelineStage::Transfer;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.destinationStages = rhi::PipelineStage::Transfer;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.sourceAccess = rhi::ResourceAccess::TransferRead;
            });
            dependency(pair.first, pair.second, d, [](auto& v) {
                v.destinationAccess = rhi::ResourceAccess::TransferWrite;
            });
        }
        const auto aliasField = [&](auto mutate) {
            auto changed = alias;
            mutate(changed);
            rejects(4u, rhi::ResourceDependencyArguments::capture(changed));
        };
        aliasField([](auto& d) {
            ++d.aliasGroup;
        });
        aliasField([](auto& d) {
            ++d.sourceQueueRole;
        });
        aliasField([](auto& d) {
            ++d.destinationQueueRole;
        });
        aliasField([](auto& d) {
            d.ownershipHandoff = true;
        });
        aliasField([](auto& d) {
            d.sourceStages = rhi::PipelineStage::Transfer;
        });
        aliasField([](auto& d) {
            d.destinationStages = rhi::PipelineStage::Transfer;
        });
        aliasField([](auto& d) {
            d.sourceAccess = rhi::ResourceAccess::TransferRead;
        });
        aliasField([](auto& d) {
            d.destinationAccess = rhi::ResourceAccess::TransferWrite;
        });
    }
};
} // namespace arp::test
