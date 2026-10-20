#pragma once

#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_sources.h"
#include "texture_readback_source_lease_fixture.h"

namespace arp::test {
class TextureReadbackRetainedSourceFixture final {
    using Lease = rhi::TextureReadbackSourceLease;
    using Texture = rhi::OriginalCommandReadbackTextureSource;
    using Readback = rhi::OriginalCommandTextureReadbackSource;
    static constexpr std::uint64_t bytes = TextureReadbackSourceLeaseFixture::cpuBytes;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(
                stderr, "retained readback: %s expected %s\n", actual ? actual : "none", expected);
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verify(rhi::GpuDevice& device)
    {
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto lowering = Lease::lowering(backend, 2u);
            assert(lowering.contractVersion == 2u && lowering.cpuMetadataImplemented
                && lowering.validatorOnly == (backend == rhi::Backend::Null)
                && !lowering.nativeExecutionImplemented && lowering.diagnostic);
            assert(!Lease::lowering(backend, 3u).cpuMetadataImplemented);
        }
        assert(!Lease::lowering(static_cast<rhi::Backend>(99), 2u).cpuMetadataImplemented);
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        rhi::TextureDesc desc;
        desc.extent = {2u, 2u, 1u};
        desc.format = rhi::TextureFormat::Rgba8Unorm;
        desc.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::RenderAttachment;
        auto object = device.createTexture(desc);
        assert(object);
        const auto* exact = object.get();
        auto owner = rhi::OriginalCommandTextureLease::prepareOriginal(
            object, {1u, true, 2u, bytes}, sourceQuota, id);
        assert(owner && !id && !object && owner->originalObject() == exact);
        rhi::BufferDesc bufferDesc;
        bufferDesc.sizeBytes = 512u;
        bufferDesc.usage = rhi::ResourceUsage::CopyDst;
        bufferDesc.memory = rhi::MemoryUsage::GpuToCpu;
        auto buffer = device.createBuffer(bufferDesc);
        assert(buffer);
        auto destination = rhi::OriginalCommandBufferLease::prepareOriginal(
            buffer, {1u, true, 2u, bytes}, sourceQuota, id);
        assert(destination && !id);
        rhi::ReadbackTextureDesc read;
        read.sourceResource = owner->resource();
        read.destinationReadbackBuffer = destination->resource();
        read.sizeBytes = 512u;
        read.extent = desc.extent;
        read.bytesPerRow = 256u;
        read.rowsPerImage = 2u;
        read.label = "retained";
        auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
            read, {1u, true, 1u, 8u, bytes, bytes}, graphQuota, graphQuota, id);
        assert(input && !id);
        auto layout = rhi::TextureReadbackLayout::prepareOriginal(
            input, desc, {1u, true, 1u, 512u, bytes}, graphQuota, id);
        assert(layout && !id);
        auto request = TextureReadbackSourceLeaseFixture::request(layout, graphQuota);
        auto budget = TextureReadbackSourceLeaseFixture::budget(request);
        budget.contractVersion = 2u;
        budget.maxRetainedOwners = Lease::retainedOriginalOwnerCount;
        const auto factory = [&] {
            return Lease::prepareRetainedOriginal(owner, request, budget, sourceQuota, id);
        };
        CpuQuotaFactoryFixture pressure(sourceQuota);
        const auto charge = pressure.measureRetained(factory);
        pressure.verify(charge, factory);
        auto wrong = budget;
        wrong.contractVersion = 1u;
        assert(!Lease::prepareRetainedOriginal(owner, request, wrong, sourceQuota, id));
        is(id, "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE");
        wrong = budget;
        wrong.prototypeOptIn = false;
        assert(!Lease::prepareRetainedOriginal(owner, request, wrong, sourceQuota, id));
        is(id, "RHI-READBACK-SOURCE-LEASE-OPT-IN");
        --budget.maxRetainedOwners;
        assert(!factory());
        is(id, "RHI-READBACK-SOURCE-LEASE-CAPACITY");
        ++budget.maxRetainedOwners;
        {
            core::CpuMemoryQuotaLease full(
                sourceQuota, sourceQuota->limitBytes() - sourceQuota->liveBytes());
            assert(full.charged() && !factory());
        }
        auto forgedOwner =
            std::shared_ptr<const rhi::OriginalCommandTextureLease>(graphQuota, owner.get());
        assert(!Lease::prepareRetainedOriginal(forgedOwner, request, budget, sourceQuota, id));
        is(id, "RHI-ORIGINAL-SOURCE-LEASE-OWNER");
        forgedOwner.reset();
        auto foreignObject = device.createTexture(desc);
        assert(foreignObject);
        auto foreign = rhi::OriginalCommandTextureLease::prepareOriginal(
            foreignObject, {1u, true, 2u, bytes}, sourceQuota, id);
        assert(
            foreign && !Lease::prepareRetainedOriginal(foreign, request, budget, sourceQuota, id));
        is(id, "RHI-READBACK-SOURCE-LEASE-OBJECT-MISMATCH");
        foreign.reset();
        auto peerQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        assert(!Lease::prepareRetainedOriginal(owner, request, budget, peerQuota, id));
        is(id, "RHI-READBACK-SOURCE-LEASE-ORIGINAL-QUOTA");
        peerQuota.reset();
        auto lease = factory();
        assert(lease && !id && lease->sourceTexture() == exact);
        assert(core::SharedOwnerProjection::sameOwner(lease->originalTextureOwner(), owner));
        assert(lease->retainedSourceOwnerCount() == 3u && !lease->nativeExecutionProven());
        auto forged = std::shared_ptr<const Lease>(graphQuota, lease.get());
        assert(Lease::validateOriginal(forged, request));
        forged.reset();
        auto callerObject = device.createTexture(desc);
        assert(callerObject);
        assert(!Lease::prepareOriginal(callerObject, request, budget, sourceQuota, id));
        is(id, "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE");
        assert(Lease::validateColdOriginal(lease, *exact));
        assert(Lease::adoptOriginal(std::const_pointer_cast<Lease>(lease), callerObject));
        callerObject.reset();
        rhi::OriginalCommandReadbackSourceBudget sourceBudget{
            2u, true, Texture::retainedOwnerCount + 1u, 1u, 8u, 512u, bytes, bytes};
        auto oldBudget = sourceBudget;
        oldBudget.contractVersion = 1u;
        assert(!Texture::prepareOriginal(lease, oldBudget, sourceQuota, graphQuota, id));
        is(id, "RHI-ORIGINAL-READBACK-TEXTURE-LEASE-PROFILE");
        --sourceBudget.maxRetainedOwners;
        assert(!Texture::prepareOriginal(lease, sourceBudget, sourceQuota, graphQuota, id));
        is(id, "RHI-ORIGINAL-READBACK-SOURCE-OWNER-CAPACITY");
        ++sourceBudget.maxRetainedOwners;
        auto texture = Texture::prepareOriginal(lease, sourceBudget, sourceQuota, graphQuota, id);
        assert(texture && !id);
        sourceBudget.maxRetainedOwners = Readback::completeRetainedOwnerCount + 1u;
        auto readback =
            Readback::prepareOriginal(texture, destination, sourceBudget, sourceQuota, id);
        assert(readback && !id);
        auto oldReadback = sourceBudget;
        oldReadback.contractVersion = 1u;
        assert(!Readback::prepareOriginal(texture, destination, oldReadback, sourceQuota, id));
        is(id, "RHI-ORIGINAL-TEXTURE-READBACK-SOURCE-PROFILE");
        {
            using Sources = rhi::OriginalCommandResourceSources;
            rhi::OriginalCommandAdmissionBudget admission;
            admission.contractVersion = 1u;
            admission.prototypeOptIn = true;
            admission.maxCpuBytes.fill(bytes);
            admission.maxStateObservations = 2u;
            admission.maxTextureRanges = 1u;
            admission.maxCommands = 5u;
            admission.maxGraphOwners = 12u;
            admission.maxStateTransactions = 1u;
            for (const auto domain : {rhi::CommandReferenceDomain::Texture,
                     rhi::CommandReferenceDomain::Buffer,
                     rhi::CommandReferenceDomain::Readback})
                admission.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
            const std::array<Sources::Source, 3u> entries{texture, destination, readback};
            auto sources = Sources::prepareOriginal(
                entries.data(), entries.size(), admission, sourceQuota, id);
            if (!sources)
                std::fprintf(stderr, "retained source inventory: %s\n", id ? id : "none");
            assert(sources && !id);
            std::uint32_t counted = 0u;
            assert(!sources->cpuOwnerCount(1u, 12u, counted) && counted == 12u);
            is(sources->cpuOwnerCount(1u, 11u, counted), "RHI-ORIGINAL-USAGE-OWNER-CAPACITY");
        }
        auto correspondence = verifyCorrespondence(
            device, owner, destination, texture, readback, sourceQuota, graphQuota);
        auto& mutableDesc = const_cast<rhi::TextureDesc&>(exact->desc());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, graphQuota}, [&]() noexcept -> const char* {
                ++mutableDesc.extent.width;
                assert(lease->validate() && texture->validate() && readback->validate());
                --mutableDesc.extent.width;
                const auto limit = sourceQuota->limitBytes();
                sourceQuota->setLimitBytes(limit - 1u);
                assert(lease->validate() && texture->validate() && readback->validate());
                sourceQuota->setLimitBytes(limit);
                if (const auto* diagnostic = lease->validate())
                    return diagnostic;
                if (const auto* diagnostic = texture->validate())
                    return diagnostic;
                return readback->validate();
            }));
        std::weak_ptr<const rhi::OriginalCommandTextureLease> retained = owner;
        owner.reset();
        lease.reset();
        texture.reset();
        destination.reset();
        request.reset();
        layout.reset();
        input.reset();
        assert(!retained.expired() && !readback->validate()
            && readback->originalSource()->originalObject() == exact);
        readback.reset();
        assert(!retained.expired() && !correspondence->validate());
        correspondence.reset();
        assert(retained.expired());
        retained.reset();
        assert(!sourceQuota->liveBytes() && !graphQuota->liveBytes());
        std::printf(
            "ORIGINAL_READBACK_RETAINED_SOURCE2_OK owner3 source8 readback11 metadata%llu exact-less-exhausted original-object forged-foreign-profile-quota-mutation caller-drop hot0 native-closed\n",
            static_cast<unsigned long long>(charge));
    }

    static std::shared_ptr<const rhi::OriginalCommandInputs> verifyCorrespondence(
        rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandTextureLease>& owner,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& destination,
        const std::shared_ptr<const Texture>& texture,
        const std::shared_ptr<const Readback>& readback,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        using Correspondence = rhi::OriginalRetainedReadbackCorrespondence;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Correspondence::lowering(backend, 1u);
            assert(decision.cpuMetadataImplemented && !decision.nativeExecutionImplemented
                && decision.validatorOnly == (backend == rhi::Backend::Null)
                && decision.diagnostic);
            assert(!Correspondence::lowering(backend, 2u).cpuMetadataImplemented);
        }
        assert(!Correspondence::lowering(static_cast<rhi::Backend>(99), 1u).cpuMetadataImplemented);
        const char* id = nullptr;
        rhi::TextureViewDesc viewDesc;
        viewDesc.format = owner->originalObject()->desc().format;
        viewDesc.aspect = rhi::TextureAspect::Color;
        viewDesc.usage = rhi::TextureUsage::RenderAttachment;
        auto viewObject = device.createTextureView(*owner->originalObject(), viewDesc);
        assert(viewObject);
        auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            viewObject, {1u, true, 2u, bytes}, sourceQuota, id);
        assert(viewLease && !id);
        auto view = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            viewLease, owner, {1u, true, 5u, bytes}, sourceQuota, id);
        assert(view && !id);
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCpuBytes.fill(bytes);
        admission.maxCommands = 5u;
        admission.maxReferences = 2u;
        admission.maxInputDescriptors = 1u;
        admission.maxGraphOwners = 19u;
        admission.maxStateTransactions = 1u;
        admission.maxStateObservations = 3u;
        admission.maxTextureRanges = 2u;
        admission.maxMipMappings = 1u;
        admission.maxLabelBytes = 16u;
        admission.maxUnpublishedResultBytes = 512u;
        for (const auto domain : {rhi::CommandReferenceDomain::Texture,
                 rhi::CommandReferenceDomain::TextureView,
                 rhi::CommandReferenceDomain::Buffer,
                 rhi::CommandReferenceDomain::Readback})
            admission.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
        const std::array<Sources::Source, 4u> entries{owner, destination, readback, view};
        Correspondence request{1u, true, 1u, 7u};
        const auto sourceFactory = [&] {
            return Sources::prepareOriginal(
                entries.data(), entries.size(), admission, sourceQuota, id, request);
        };
        CpuQuotaFactoryFixture sourcePressure(sourceQuota), graphPressure(graphQuota);
        const auto sourceCharge = sourcePressure.measureRetained(sourceFactory);
        sourcePressure.verify(sourceCharge, sourceFactory);
        {
            core::CpuMemoryQuotaLease full(
                sourceQuota, sourceQuota->limitBytes() - sourceQuota->liveBytes());
            assert(full.charged() && !sourceFactory());
        }
        auto sources = sourceFactory();
        assert(sources && !id);
        std::uint32_t owners = 0u;
        assert(!sources->cpuOwnerCount(1u, 15u, owners) && owners == 15u);
        is(sources->cpuOwnerCount(1u, 14u, owners), "RHI-ORIGINAL-USAGE-OWNER-CAPACITY");
        const auto rejectSource = [&](const char* expected) {
            assert(!sourceFactory());
            is(id, expected);
        };
        request.prototypeOptIn = false;
        rejectSource("RHI-ORIGINAL-RETAINED-READBACK-OPT-IN");
        request.prototypeOptIn = true;
        request.contractVersion = 2u;
        rejectSource("RHI-ORIGINAL-RETAINED-READBACK-VERSION");
        request.contractVersion = 1u;
        request.maxReadbackSources = 0u;
        rejectSource("RHI-ORIGINAL-RETAINED-READBACK-BUDGET");
        request.maxReadbackSources = 1u;
        request = {0u, true, 1u, 7u};
        rejectSource("RHI-ORIGINAL-RETAINED-READBACK-VERSION");
        request = {1u, true, 1u, 7u};
        request.maxNestedOwners = 6u;
        rejectSource("RHI-ORIGINAL-RETAINED-READBACK-OWNER-CAPACITY");
        request.maxNestedOwners = 7u;
        assert(
            !Sources::prepareOriginal(entries.data(), entries.size(), admission, sourceQuota, id));
        is(id, "RHI-ORIGINAL-SOURCES-READBACK-SOURCE");
        {
            // The wrapper cannot replace the attachment's authentic parent.
            auto wrongEntries = entries;
            wrongEntries[0] = texture;
            assert(!Sources::prepareOriginal(
                wrongEntries.data(), wrongEntries.size(), admission, sourceQuota, id, request));
            is(id, "RHI-ORIGINAL-SOURCES-READBACK-SOURCE");
            auto duplicate =
                std::array<Sources::Source, 5u>{owner, destination, readback, view, texture};
            auto extra = admission;
            extra.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                2u;
            assert(!Sources::prepareOriginal(
                duplicate.data(), duplicate.size(), extra, sourceQuota, id, request));
            is(id, "RHI-ORIGINAL-SOURCES-IDENTITY-ALIAS");
            auto foreignObject = device.createTexture(owner->originalObject()->desc());
            assert(foreignObject);
            auto foreign = rhi::OriginalCommandTextureLease::prepareOriginal(
                foreignObject, {1u, true, 2u, bytes}, sourceQuota, id);
            assert(foreign);
            wrongEntries = entries;
            wrongEntries[0] = foreign;
            assert(!Sources::prepareOriginal(
                wrongEntries.data(), wrongEntries.size(), admission, sourceQuota, id, request));
            is(id, "RHI-ORIGINAL-SOURCES-READBACK-SOURCE");
            auto forged =
                std::shared_ptr<const rhi::OriginalCommandTextureLease>(graphQuota, owner.get());
            wrongEntries[0] = forged;
            assert(!Sources::prepareOriginal(
                wrongEntries.data(), wrongEntries.size(), admission, sourceQuota, id, request));
            is(id, "RHI-ORIGINAL-SOURCE-LEASE-OWNER");
            auto missingDestination = std::array<Sources::Source, 3u>{owner, readback, view};
            assert(!Sources::prepareOriginal(missingDestination.data(),
                missingDestination.size(),
                admission,
                sourceQuota,
                id,
                request));
            is(id, "RHI-ORIGINAL-SOURCES-READBACK-DESTINATION");
        }
        {
            // Independent requests may retain the same canonical texture/destination;
            // both hidden metadata closures must fit before any native work.
            auto request2 = TextureReadbackSourceLeaseFixture::request(
                texture->originalRequest()->originalLayout(), graphQuota);
            auto leaseBudget = TextureReadbackSourceLeaseFixture::budget(request2);
            leaseBudget.contractVersion = 2u;
            leaseBudget.maxRetainedOwners = 3u;
            auto lease2 =
                Lease::prepareRetainedOriginal(owner, request2, leaseBudget, sourceQuota, id);
            assert(lease2);
            rhi::OriginalCommandReadbackSourceBudget b{2u, true, 8u, 1u, 8u, 512u, bytes, bytes};
            auto texture2 = Texture::prepareOriginal(lease2, b, sourceQuota, graphQuota, id);
            assert(texture2);
            b.maxRetainedOwners = 11u;
            auto readback2 = Readback::prepareOriginal(texture2, destination, b, sourceQuota, id);
            assert(readback2);
            const auto two =
                std::array<Sources::Source, 5u>{owner, destination, readback, view, readback2};
            auto multiple = admission;
            multiple
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Readback)] =
                2u;
            multiple.maxGraphOwners = 26u;
            auto twoRequest = request;
            twoRequest.maxNestedOwners = 14u;
            assert(!Sources::prepareOriginal(
                two.data(), two.size(), multiple, sourceQuota, id, twoRequest));
            is(id, "RHI-ORIGINAL-RETAINED-READBACK-SOURCE-CAPACITY");
            twoRequest.maxReadbackSources = 2u;
            twoRequest.maxNestedOwners = 13u;
            assert(!Sources::prepareOriginal(
                two.data(), two.size(), multiple, sourceQuota, id, twoRequest));
            is(id, "RHI-ORIGINAL-RETAINED-READBACK-OWNER-CAPACITY");
            twoRequest.maxNestedOwners = 14u;
            auto exact = Sources::prepareOriginal(
                two.data(), two.size(), multiple, sourceQuota, id, twoRequest);
            assert(exact && !id);
            assert(!exact->cpuOwnerCount(1u, 22u, owners) && owners == 22u);
            is(exact->cpuOwnerCount(1u, 21u, owners), "RHI-ORIGINAL-USAGE-OWNER-CAPACITY");
        }
        const std::array<rhi::CommandUsage, 4u> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::TextureReadback, 0u, 2u},
            {K::EndTransferPass, 2u, 0u},
            {K::Finish, 2u, 0u}}};
        const std::array<rhi::OriginalCommandUsage::Reference, 2u> refs{{{0u}, {2u}}};
        using Scalar = rhi::CommandScalarArguments;
        const std::array<rhi::CommandVariableArguments::Input, 4u> fields{
            {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "retained"},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        const auto arguments =
            rhi::TextureReadbackOriginalInputArguments::captureOriginal(readback->originalInput());
        assert(arguments);
        std::array<Inputs::OriginalDescriptor, 1u> descriptors{{{1u, *arguments}}};
        rhi::OriginalCommandInputsBudget inputBudget;
        inputBudget.retainedReadback = request;
        const auto inputFactory = [&] {
            return OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                descriptors.data(),
                descriptors.size(),
                inputBudget,
                graphQuota,
                id);
        };
        const auto inputCharge = graphPressure.measureRetained(inputFactory);
        graphPressure.verify(inputCharge, inputFactory);
        {
            core::CpuMemoryQuotaLease full(
                graphQuota, graphQuota->limitBytes() - graphQuota->liveBytes());
            assert(full.charged() && !inputFactory());
        }
        auto inputs = inputFactory();
        assert(inputs && !id && inputs->retainedOwnerCount() == 19u);
        inputBudget.retainedReadback = {};
        assert(!inputFactory());
        is(id, "RHI-ORIGINAL-INPUTS-RETAINED-READBACK-PROFILE");
        inputBudget.retainedReadback = request;
        ++inputBudget.retainedReadback.maxNestedOwners;
        assert(!inputFactory());
        is(id, "RHI-ORIGINAL-INPUTS-RETAINED-READBACK-PROFILE");
        inputBudget.retainedReadback = request;
        {
            rhi::ReadbackTextureDesc equal;
            readback->originalInput()->scalarFields().writeScalarFields(equal);
            equal.label = "retained";
            auto peer = rhi::TextureReadbackInputSnapshot::prepareOriginal(
                equal, {1u, true, 1u, 8u, bytes, bytes}, graphQuota, graphQuota, id);
            assert(peer);
            const auto different =
                rhi::TextureReadbackOriginalInputArguments::captureOriginal(peer);
            assert(different);
            descriptors[0].arguments = *different;
            assert(!inputFactory());
            is(id, "RHI-ORIGINAL-INPUTS-READBACK-OWNER");
            descriptors[0].arguments = *arguments;
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, graphQuota}, [&]() noexcept -> const char* {
                if (const auto* diagnostic = Sources::validateOriginal(sources))
                    return diagnostic;
                if (const auto* diagnostic = Inputs::validateOriginal(inputs))
                    return diagnostic;
                auto& changed = const_cast<rhi::TextureDesc&>(owner->originalObject()->desc());
                ++changed.extent.width;
                assert(sources->validate() && inputs->validate());
                --changed.extent.width;
                const std::array<Inputs::IncomingReference, 2u> raw{
                    owner->originalObject(), readback->originalObject()};
                return inputs->compareOriginalInput(
                    1u, {fields[1], *arguments, raw.data(), raw.size()});
            }));
        verifyMixedResult(entries, admission, request, inputs, graphQuota);
        // The graph alone retains all hidden metadata, attachment parent and destination.
        std::weak_ptr<const Sources> retained = sources;
        sources.reset();
        assert(!retained.expired() && !inputs->validate()
            && !inputs->originalScalars()
                ->originalUsage()
                ->originalSources()
                ->nativeExecutionProven());
        std::printf(
            "ORIGINAL_RETAINED_READBACK_CORRESPONDENCE1_OK same-original-parent hidden7 graph19 source%llu inputs%llu exact-less-exhausted hot0 native-closed\n",
            static_cast<unsigned long long>(sourceCharge),
            static_cast<unsigned long long>(inputCharge));
        return inputs;
    }

    static void verifyMixedResult(
        const std::array<rhi::OriginalCommandResourceSources::Source, 4u>& entries,
        rhi::OriginalCommandAdmissionBudget admission,
        const rhi::OriginalRetainedReadbackCorrespondence& correspondence,
        const std::shared_ptr<const rhi::OriginalCommandInputs>& earlierInputs,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota)
    {
        using Result = rhi::OriginalCommandReadbackResultContract;
        using Stream = rhi::OriginalCommandStreamProgram;
        using Sources = rhi::OriginalCommandResourceSources;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                rhi::semantics::OriginalCommandMixedReadbackResultLowering::lowering(backend, 2u);
            assert(d.cpuMetadataImplemented && d.validatorOnly == (backend == rhi::Backend::Null)
                && !d.recordingAvailable && !d.mapAvailable && !d.publicationAvailable
                && d.diagnostic);
            assert(
                !rhi::semantics::OriginalCommandMixedReadbackResultLowering::lowering(backend, 1u)
                    .cpuMetadataImplemented);
        }
        assert(!rhi::semantics::OriginalCommandMixedReadbackResultLowering::lowering(
            static_cast<rhi::Backend>(99), 2u)
                .cpuMetadataImplemented);
        // Same genuine attachment parent and retained readback owner. Repeating
        // one request charges two copies/declared spans but one result allocation.
        const auto& readback = std::get<std::shared_ptr<const Readback>>(entries[2u]);
        const auto& view =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(entries[3u]);
        const auto& sourceQuota = readback->originalMetadataQuota();
        const char* id = nullptr;
        rhi::RenderPassDesc pass;
        rhi::RenderPassColorAttachmentDesc color;
        color.target = view->resource();
        color.format = view->originalObject()->desc().format;
        color.loadOp = rhi::LoadOp::Clear;
        color.storeOp = rhi::StoreOp::Store;
        color.clearColor = {0.25f, 0.5f, 0.75f, 1.0f};
        pass.colorAttachments.push_back(color);
        auto frozen = rhi::RenderPassArguments::prepareOriginal(
            pass, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, graphQuota, id);
        assert(frozen && !id);
        const std::array<rhi::CommandUsage, 7u> commands{{{K::BeginRenderPass, 0u, 1u},
            {K::EndRenderPass, 1u, 0u},
            {K::BeginTransferPass, 1u, 0u},
            {K::TextureReadback, 1u, 2u},
            {K::TextureReadback, 3u, 2u},
            {K::EndTransferPass, 5u, 0u},
            {K::Finish, 5u, 0u}}};
        const std::array<rhi::OriginalCommandUsage::Reference, 5u> refs{
            {{3u}, {0u}, {2u}, {0u}, {2u}}};
        const std::array<rhi::CommandVariableArguments::Input, 7u> fields{
            {{*Scalar::capture(K::BeginRenderPass, Scalar::None{})},
                {*Scalar::capture(K::EndRenderPass, Scalar::None{})},
                {*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "retained"},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "retained"},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        const auto argument =
            rhi::TextureReadbackOriginalInputArguments::captureOriginal(readback->originalInput());
        assert(argument);
        const std::array<Inputs::OriginalDescriptor, 3u> records{
            {{0u, frozen}, {3u, *argument}, {4u, *argument}}};
        admission.maxCommands = commands.size();
        admission.maxReferences = refs.size();
        admission.maxInputDescriptors = records.size();
        admission.maxLabelBytes = 4u * 8u;
        admission.maxMipMappings = 2u;
        admission.maxUnpublishedResultBytes =
            2u * readback->originalInput()->scalarFields().byteCapacity();
        admission.maxPassColorAttachments = admission.maxPassAttachmentReferences = 1u;
        // Earlier graph + frozen pass + repeated input + retained program owner.
        admission.maxGraphOwners = earlierInputs->retainedOwnerCount() + 3u;
        auto sources = Sources::prepareOriginal(
            entries.data(), entries.size(), admission, sourceQuota, id, correspondence);
        assert(sources && !id);
        rhi::OriginalCommandInputsBudget inputBudget;
        inputBudget.retainedReadback = correspondence;
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            fields.data(),
            records.data(),
            records.size(),
            inputBudget,
            graphQuota,
            id);
        if (!inputs)
            std::fprintf(stderr, "mixed readback Inputs: %s\n", id ? id : "none");
        assert(inputs && !id);
        auto programQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const rhi::OriginalCommandStreamBudget stream{1u,
            true,
            commands.size(),
            records.size(),
            refs.size(),
            0u,
            0u,
            0u,
            2u,
            1u,
            1u,
            0u,
            0u,
            1024u,
            inputs->retainedOwnerCount() + 1u,
            bytes};
        const auto build = [&] {
            return Stream::prepareOriginal(inputs, stream, programQuota, id);
        };
        CpuQuotaFactoryFixture pressure(programQuota);
        const auto programBytes = pressure.measureRetained(build);
        pressure.verify(programBytes, build);
        {
            core::CpuMemoryQuotaLease full(programQuota, bytes - programQuota->liveBytes());
            assert(full.charged() && !build());
        }
        auto program = build();
        if (!program)
            std::fprintf(stderr, "mixed readback Stream: %s\n", id ? id : "none");
        assert(program && !id);
        const Result::Budget budget{2u, true, 2u, 2u, 1u, 1024u, 512u, bytes};
        const auto inspect = [&](auto b) {
            return Result::inspectProgram(program, inputs, b);
        };
        const auto accepted = inspect(budget);
        assert(!accepted.diagnostic && accepted.required.readbackCommands == 2u
            && accepted.required.nativeCopyCommands == 2u
            && accepted.required.resultDeclarations == 1u
            && accepted.required.declaredReadbackBytes == 1024u
            && accepted.required.uniqueResultBytes == 512u
            && accepted.required.cpuResultBytes == bytes
            && !Result::validateProgramRequirements(accepted.required, budget));
        const auto refused = [&](auto b, const char* diagnostic) {
            const auto r = inspect(b);
            is(r.diagnostic, diagnostic);
            assert(!r.required.readbackCommands && !r.required.nativeCopyCommands
                && !r.required.resultDeclarations && !r.required.declaredReadbackBytes
                && !r.required.uniqueResultBytes && !r.required.cpuResultBytes);
        };
        for (const auto version : {0u, 1u, 3u}) {
            auto bad = budget;
            bad.contractVersion = version;
            refused(bad, "RHI-ORIGINAL-MIXED-READBACK-RESULT-VERSION");
        }
        auto bad = budget;
        bad.prototypeOptIn = false;
        refused(bad, "RHI-ORIGINAL-READBACK-RESULT-OPT-IN");
        bad = budget;
        --bad.maxReadbackCommands;
        refused(bad, "RHI-ORIGINAL-READBACK-RESULT-COMMAND-CAPACITY");
        bad = budget;
        --bad.maxNativeCopyCommands;
        refused(bad, "RHI-ORIGINAL-READBACK-RESULT-COPY-CAPACITY");
        bad = budget;
        bad.maxResultDeclarations = 0u;
        refused(bad, "RHI-ORIGINAL-READBACK-RESULT-BUDGET");
        bad = budget;
        --bad.maxUniqueResultBytes;
        refused(bad, "RHI-ORIGINAL-READBACK-RESULT-BYTE-CAPACITY");
        bad = budget;
        --bad.maxDeclaredReadbackBytes;
        refused(bad, "RHI-ORIGINAL-READBACK-RESULT-DECLARED-CAPACITY");
        bad = budget;
        --bad.maxCpuResultBytes;
        refused(bad, "RHI-ORIGINAL-READBACK-RESULT-CPU-BUDGET");
        auto forged = std::shared_ptr<const Stream>(sourceQuota, program.get());
        is(Result::inspectProgram(forged, inputs, budget).diagnostic, "RHI-ORIGINAL-STREAM-OWNER");
        is(Result::inspectProgram(program, earlierInputs, budget).diagnostic,
            "RHI-ORIGINAL-STREAM-INPUTS");
        is(Result::inspectProgram({}, inputs, budget).diagnostic, "RHI-ORIGINAL-STREAM-OWNER");
        is(Result::validateIntent(budget), "RHI-ORIGINAL-READBACK-RESULT-VERSION");
        auto legacy = budget;
        legacy.contractVersion = 1u;
        is(Result::inspect(inputs, stream, legacy).diagnostic,
            "RHI-ORIGINAL-READBACK-RESULT-COMMAND");
        assert(Result::supportsProgram(K::BindRenderDescriptorTable)
            && Result::supportsProgram(K::Draw) && Result::supportsProgram(K::Dispatch)
            && !Result::supportsProgram(K::TextureUpload)
            && !Result::supportsProgram(K::ExecuteSecondaryCommandBuffer)
            && !Result::supportsProgram(K::Count));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, graphQuota, programQuota}, [&]() noexcept -> const char* {
                const auto r = inspect(budget);
                if (r.diagnostic)
                    return r.diagnostic;
                return Result::validateProgramRequirements(r.required, budget);
            }));
        std::weak_ptr<const Inputs> retained = inputs;
        inputs.reset();
        sources.reset();
        assert(!retained.expired()
            && !Result::inspectProgram(program, program->originalInputs(), budget).diagnostic);
        program.reset();
        assert(retained.expired() && programQuota->liveBytes() == 0u);
        std::printf(
            "ORIGINAL_MIXED_READBACK_RESULT2_OK same-original-attachment-parent Source2 render1 readback2 copies2 result1 declared1024 unique512 exact-less-exhausted program%llu hot0 native-closed\n",
            static_cast<unsigned long long>(programBytes));
    }

    // Called only after the selected execution fixture authenticates completion.
    // Preparing CPU readback ownership does not authenticate or map GPU results.
    static void verifyOutputOwner(
        const std::shared_ptr<const rhi::OriginalCommandTextureLease>& owner)
    {
        assert(owner && !owner->validate());
        const auto& quota = owner->originalMetadataQuota();
        assert(quota->limitBytes() == bytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto* exact = owner->originalObject();
        const auto completed =
            exact->observeOriginalContent({0u, 1u, 0u, 1u, rhi::TextureAspect::Color});
        assert(completed && !completed->diagnostic() && completed->contentGeneration()
            && completed->state() == rhi::common::OriginalTextureContentControl::State::Completed);
        const char* id = nullptr;
        rhi::ReadbackTextureDesc read;
        read.sourceResource = owner->resource();
        read.destinationReadbackBuffer = rhi::allocateGpuResourceId();
        read.extent = exact->desc().extent;
        read.bytesPerRow = rhi::kTextureUploadRowPitchAlignment;
        read.rowsPerImage = read.extent.height;
        read.sizeBytes = std::uint64_t(read.bytesPerRow) * read.rowsPerImage;
        read.label = "output";
        auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
            read, {1u, true, 1u, 6u, bytes, bytes}, graphQuota, graphQuota, id);
        assert(input && !id);
        auto layout = rhi::TextureReadbackLayout::prepareOriginal(
            input, exact->desc(), {1u, true, 1u, read.sizeBytes, bytes}, graphQuota, id);
        assert(layout && !id);
        auto request = TextureReadbackSourceLeaseFixture::request(layout, graphQuota);
        auto b = TextureReadbackSourceLeaseFixture::budget(request);
        b.contractVersion = 2u;
        b.maxRetainedOwners = 3u;
        const auto factory = [&] {
            return Lease::prepareRetainedOriginal(owner, request, b, quota, id);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto charge = pressure.measureRetained(factory);
        pressure.verify(charge, factory);
        auto lease = factory();
        assert(lease && !id && lease->sourceTexture() == exact);
        rhi::OriginalCommandReadbackSourceBudget sourceBudget{
            2u, true, 8u, 1u, 6u, read.sizeBytes, bytes, bytes};
        auto source = Texture::prepareOriginal(lease, sourceBudget, quota, graphQuota, id);
        assert(source && !id);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, graphQuota}, [&]() noexcept -> const char* {
                if (const auto* diagnostic = exact->validateOriginalContent(*completed))
                    return diagnostic;
                if (const auto* diagnostic = Lease::validateOriginal(lease, request))
                    return diagnostic;
                return Texture::validateOriginal(source);
            }));
        assert(core::SharedOwnerProjection::sameOwner(lease->originalTextureOwner(), owner)
            && !lease->nativeExecutionProven() && !source->nativeExecutionProven());
        std::printf(
            "ORIGINAL_TABLE_OUTPUT_READBACK_OWNER2_OK Graphics same-adopted-color exact-layout owner3 source8 metadata%llu hot0 original-completed-cell-unchanged copy-map-readback-unproven\n",
            static_cast<unsigned long long>(charge));
    }
};
} // namespace arp::test
