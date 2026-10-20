#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_readback_result_contract_fixture.h"
#include "original_command_stream_program_fixture.h"
#include "original_command_texture_transfer_states_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandTextureReadbackInputsFixture final {
    static constexpr std::uint64_t metadataBytes = 65536u;
    using Texture = rhi::OriginalCommandReadbackTextureSource;
    using Readback = rhi::OriginalCommandTextureReadbackSource;
    using SourceBudget = rhi::OriginalCommandReadbackSourceBudget;

    class Chain final {
    public:
        rhi::ReadbackTextureDesc desc;
        std::shared_ptr<const rhi::TextureReadbackInputSnapshot> input;
        std::shared_ptr<const rhi::TextureReadbackLayout> layout;
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request;
        std::shared_ptr<const rhi::TextureReadbackSourceLease> lease;
        std::shared_ptr<const rhi::OriginalCommandBufferLease> destination;
        const rhi::GpuTexture* object = nullptr;
        const rhi::GpuBuffer* destinationObject = nullptr;

        Chain(rhi::GpuDevice& device,
            const std::array<std::shared_ptr<core::CpuMemoryQuota>, 5>& quotas,
            const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
            std::uint64_t destinationBytes = 768u,
            rhi::ResourceUsage destinationUsage = rhi::ResourceUsage::CopyDst,
            bool readyStates = false,
            std::shared_ptr<const rhi::OriginalCommandBufferLease> suppliedDestination = {},
            rhi::MemoryUsage destinationMemory = rhi::MemoryUsage::GpuOnly,
            rhi::TextureUsage sourceUsage = rhi::TextureUsage::CopySrc)
        {
            const char* diagnostic = nullptr;
            rhi::TextureDesc physical;
            physical.extent = {2u, 2u, 1u};
            physical.format = rhi::TextureFormat::Rgba8Unorm;
            physical.usage = sourceUsage;
            physical.mipLevelCount = 2u;
            physical.initialState =
                readyStates ? rhi::ResourceState::CopySrc : rhi::ResourceState::Undefined;
            auto texture = device.createTexture(physical);
            assert(texture);
            object = texture.get();
            const auto allocation = core::inspectCpuAllocationContaining(object);
            assert(allocation.found);
            rhi::BufferDesc buffer;
            buffer.sizeBytes = destinationBytes;
            buffer.usage = destinationUsage;
            buffer.memory = destinationMemory;
            buffer.initialState =
                readyStates ? rhi::ResourceState::CopyDst : rhi::ResourceState::Undefined;
            if (suppliedDestination)
                destination = std::move(suppliedDestination);
            else {
                auto target = device.createBuffer(buffer);
                assert(target);
                destination = rhi::OriginalCommandBufferLease::prepareOriginal(
                    target, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
                assert(destination);
            }
            destinationObject = destination->originalObject();
            desc.sourceResource = texture->id();
            desc.destinationReadbackBuffer = destination->resource();
            desc.sizeBytes = 768u;
            desc.extent = physical.extent;
            desc.bytesPerRow = 256u;
            desc.rowsPerImage = 2u;
            desc.sourceSubresources.mipLevelCount = 2u;
            desc.label = "read";
            desc.mipLayouts = {
                {0u, {2u, 2u, 1u}, 256u, 2u, 0u}, {1u, {1u, 1u, 1u}, 256u, 1u, 512u}};
            input = rhi::TextureReadbackInputSnapshot::prepareOriginal(desc,
                {1u, true, 2u, 4u, metadataBytes, metadataBytes},
                quotas[0],
                quotas[1],
                diagnostic);
            assert(input);
            layout = rhi::TextureReadbackLayout::prepareOriginal(
                input, physical, {1u, true, 2u, 768u, metadataBytes}, quotas[2], diagnostic);
            assert(layout);
            request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(layout,
                {17u, 23u, 29u},
                {1u, true, 2u, 4u, 768u, metadataBytes},
                quotas[3],
                diagnostic);
            assert(request);
            lease = rhi::TextureReadbackSourceLease::prepareOriginal(
                texture, request, {1u, true, 2u, 2u, 4u, metadataBytes}, quotas[4], diagnostic);
            assert(lease && !texture && lease->sourceTexture() == object);
            const auto preserved = core::inspectCpuAllocationContaining(object);
            assert(preserved.found && preserved.quota == allocation.quota
                && preserved.sizeBytes == allocation.sizeBytes
                && preserved.allocationIdentity == allocation.allocationIdentity);
        }

        void drop()
        {
            input.reset();
            layout.reset();
            request.reset();
            lease.reset();
            destination.reset();
            desc.label.clear();
            desc.mipLayouts.clear();
        }
    };

    static SourceBudget localBudget(std::uint32_t owners)
    {
        return {1u, true, owners, 2u, 4u, 768u, metadataBytes, metadataBytes};
    }

public:
    static std::shared_ptr<const rhi::OriginalCommandInputs> prepareCanonicalInputs(
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota,
        const char*& diagnostic,
        rhi::MemoryUsage destinationMemory = rhi::MemoryUsage::GpuToCpu,
        std::uint32_t nativePayloads = 2u,
        std::uint64_t stagingBytes = 768u,
        const rhi::OriginalCommandLifecycleCapacityRequest& capacity = {})
    {
        using K = rhi::CommandUsageKind;
        using Inputs = rhi::OriginalCommandInputs;
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        Chain chain(device,
            {graphQuota, graphQuota, graphQuota, graphQuota, sourceQuota},
            sourceQuota,
            768u,
            rhi::ResourceUsage::CopyDst,
            true,
            {},
            destinationMemory,
            rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst);
        auto texture = Texture::prepareOriginal(chain.lease,
            localBudget(Texture::retainedOwnerCount),
            sourceQuota,
            graphQuota,
            diagnostic);
        if (!texture)
            return {};
        auto readback = Readback::prepareOriginal(texture,
            chain.destination,
            localBudget(Readback::completeRetainedOwnerCount),
            sourceQuota,
            diagnostic);
        if (!readback)
            return {};
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxStateObservations = 3u;
        budget.maxTextureRanges = 2u;
        budget.maxStateTransactions = 1u;
        // Two scalar labels and two original descriptor labels are separate
        // retained work, even though each contains the same four bytes.
        budget.maxMipMappings = 4u;
        budget.maxLabelBytes = 16u;
        budget.maxUnpublishedResultBytes = 1536u;
        budget.maxNativePayloadObjects = nativePayloads;
        budget.maxNativeStagingBytes = stagingBytes;
        budget.maxPendingSubmissions = capacity.pendingSubmissions;
        budget.maxCallbackTickets = capacity.callbackTickets;
        budget.maxCallbackRegistrations = capacity.callbackRegistrations;
        budget.maxCpuBytes.fill(metadataBytes);
        const std::array<Sources::Source, 3> retained{texture, chain.destination, readback};
        const std::array<rhi::CommandUsage, 5> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::TextureReadback, 0u, 2u},
            {K::TextureReadback, 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        const std::array<Usage::Reference, 4> refs{{{0u}, {2u}, {0u}, {2u}}};
        const auto arguments =
            rhi::TextureReadbackOriginalInputArguments::captureOriginal(chain.input);
        if (!arguments)
            return {};
        const std::array<Inputs::OriginalDescriptor, 2> descriptors{
            {{1u, *arguments}, {2u, *arguments}}};
        return OriginalCommandUploadStreamBuilderFixture::prepare(retained,
            commands,
            refs,
            descriptors,
            budget,
            sourceQuota,
            graphQuota,
            diagnostic,
            std::array<std::string_view, 5>{"", "read", "read", "", ""});
    }

    static void verify(rhi::GpuDevice& device, bool requireRegistered = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        using Arguments = rhi::TextureReadbackOriginalInputArguments;
        using B = rhi::OriginalCommandAdmissionBudget;
        using K = rhi::CommandUsageKind;
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes),
             graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        Chain chain(device,
            {graphQuota, graphQuota, graphQuota, graphQuota, sourceQuota},
            sourceQuota,
            768u,
            rhi::ResourceUsage::CopyDst,
            requireRegistered);
        const char* diagnostic = nullptr;
        const auto textureFactory = [&] {
            return Texture::prepareOriginal(
                chain.lease, localBudget(7u), sourceQuota, graphQuota, diagnostic);
        };
        CpuQuotaFactoryFixture sourcePressure(sourceQuota), graphPressure(graphQuota);
        const auto texturePeak = sourcePressure.measure(textureFactory);
        auto texture = textureFactory();
        assert(texture && !Texture::validateOriginal(texture));
        assert(texture->originalObject() == chain.object
            && core::SharedOwnerProjection::sameOwner(texture->originalLease(), chain.lease)
            && core::SharedOwnerProjection::sameOwner(texture->originalRequest(), chain.request));
        const auto readbackFactory = [&] {
            return Readback::prepareOriginal(
                texture, chain.destination, localBudget(10u), sourceQuota, diagnostic);
        };
        const auto readbackPeak = sourcePressure.measure(readbackFactory);
        auto readback = readbackFactory();
        assert(readback && !Readback::validateOriginal(readback));
        assert(
            readback->originalObject() == chain.request.get() && !readback->resource().isValid());
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxStateObservations = 3u;
        budget.maxTextureRanges = 2u;
        budget.maxCommands = 5u;
        budget.maxReferences = 4u;
        budget.maxInputDescriptors = 2u;
        budget.maxGraphOwners = 15u;
        budget.maxMipMappings = 4u;
        budget.maxLabelBytes = 16u;
        budget.maxUnpublishedResultBytes = 1536u;
        for (const auto domain : {rhi::CommandReferenceDomain::Texture,
                 rhi::CommandReferenceDomain::Readback,
                 rhi::CommandReferenceDomain::Buffer})
            budget.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
        budget.maxStateTransactions = 1u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Recording)] = metadataBytes;
        for (const auto domain : {B::CpuDomain::Source, B::CpuDomain::Graph, B::CpuDomain::Result})
            budget.maxCpuBytes[static_cast<std::size_t>(domain)] = metadataBytes;
        std::array<Sources::Source, 3> retained{texture, chain.destination, readback};
        const auto sourceFactory = [&] {
            return Sources::prepareOriginal(
                retained.data(), retained.size(), budget, sourceQuota, diagnostic);
        };
        const auto sourcePeak = sourcePressure.measure(sourceFactory);
        auto sources = sourceFactory();
        assert(sources);
        if (requireRegistered)
            assert(!Sources::validateRegisteredLifetime(sources, device));
        const std::array<rhi::CommandUsage, 5> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::TextureReadback, 0u, 2u},
            {K::TextureReadback, 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        const std::array<Usage::Reference, 4> refs{{{0u}, {2u}, {0u}, {2u}}};
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            graphQuota,
            diagnostic);
        assert(usage && usage->retainedOwnerCount() == 12u);
        const std::array<Store::Input, 5> fields{
            {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "read"},
                {*Scalar::capture(K::TextureReadback, Scalar::None{}), "read"},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 5u, 0u, 8u, 0u, metadataBytes},
            graphQuota,
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        assert(scalars && scalars->retainedOwnerCount() == 14u);
        auto arguments = Arguments::captureOriginal(chain.input);
        assert(arguments);
        std::array<Inputs::OriginalDescriptor, 2> descriptors{{{1u, *arguments}, {2u, *arguments}}};
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic);
        };
        const auto inputPeak = graphPressure.measure(factory);
        sourcePressure.verify(texturePeak, textureFactory);
        sourcePressure.verify(readbackPeak, readbackFactory);
        sourcePressure.verify(sourcePeak, sourceFactory);
        graphPressure.verify(inputPeak, factory);
        for (const auto& quota : std::array{sourceQuota, graphQuota}) {
            const auto baseline = quota->liveBytes();
            {
                core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
                assert(full.charged());
                if (quota == sourceQuota)
                    assert(!textureFactory() && !readbackFactory() && !sourceFactory());
                else
                    assert(!factory());
            }
            assert(quota->liveBytes() == baseline);
        }
        verifySources(device,
            chain,
            texture,
            readback,
            retained,
            budget,
            commands,
            refs,
            sourceQuota,
            graphQuota);
        {
            // Two genuine source/Request owners have equal caller labels/IDs,
            // but share one destination. Their map/result lifetime is ambiguous.
            Chain other(device,
                {graphQuota, graphQuota, graphQuota, graphQuota, sourceQuota},
                sourceQuota,
                768u,
                rhi::ResourceUsage::CopyDst,
                false,
                chain.destination);
            auto otherTexture = Texture::prepareOriginal(
                other.lease, localBudget(7u), sourceQuota, graphQuota, diagnostic);
            assert(otherTexture);
            auto otherReadback = Readback::prepareOriginal(
                otherTexture, chain.destination, localBudget(10u), sourceQuota, diagnostic);
            assert(otherReadback);
            auto bounded = budget;
            bounded.maxSourceOwners = {};
            const std::array<Sources::Source, 5> entries{
                texture, otherTexture, chain.destination, readback, otherReadback};
            const std::array<Usage::Reference, 4> distinctReferences{{{0u}, {3u}, {1u}, {4u}}};
            const auto otherArguments =
                rhi::TextureReadbackOriginalInputArguments::captureOriginal(other.input);
            assert(otherArguments);
            const std::array<Inputs::OriginalDescriptor, 2> distinctDescriptors{
                {{1u, *arguments}, {2u, *otherArguments}}};
            auto aliased = OriginalCommandUploadStreamBuilderFixture::prepare(entries,
                commands,
                distinctReferences,
                distinctDescriptors,
                bounded,
                sourceQuota,
                graphQuota,
                diagnostic,
                std::array<std::string_view, 5>{"", "read", "read", "", ""});
            assert(aliased);
            using Contract = rhi::OriginalCommandReadbackResultContract;
            const rhi::OriginalCommandStreamBudget stream{1u,
                true,
                5u,
                2u,
                4u,
                0u,
                0u,
                0u,
                4u,
                0u,
                0u,
                0u,
                0u,
                1536u,
                aliased->retainedOwnerCount() + 1u,
                metadataBytes};
            const auto rejected = Contract::inspect(
                aliased, stream, {1u, true, 2u, 4u, 2u, 1536u, 1536u, metadataBytes});
            assert(rejected.diagnostic
                && std::string_view(rejected.diagnostic)
                    == "RHI-ORIGINAL-READBACK-RESULT-DESTINATION-ALIAS"
                && !rejected.required.resultDeclarations && !rejected.required.uniqueResultBytes);
            std::puts(
                "BINDLESS_RHI_CANONICAL_READBACK_RESULT_DESTINATION_ALIAS_REFUSED distinct-original-requests equal-caller-labels-IDs shared-destination no-concurrent-map no-partial-result");
        }
        for (unsigned limit = 0u; limit < 5u; ++limit) {
            auto bounded = budget;
            if (limit == 0u)
                --bounded.maxMipMappings;
            if (limit == 1u)
                --bounded.maxUnpublishedResultBytes;
            if (limit == 2u)
                --bounded.maxLabelBytes;
            if (limit == 3u)
                --bounded.maxGraphOwners;
            if (limit == 4u)
                --bounded.maxInputDescriptors;
            auto table = Sources::prepareOriginal(
                retained.data(), retained.size(), bounded, sourceQuota, diagnostic);
            assert(table);
            auto graph = Usage::prepareOriginal(table,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                graphQuota,
                diagnostic);
            assert(graph);
            auto scalar = Scalars::prepareOriginal(graph, store, graphQuota, diagnostic);
            assert(scalar);
            assert(!Inputs::prepareOriginal(
                scalar, descriptors.data(), descriptors.size(), graphQuota, diagnostic));
            assert(std::string_view(diagnostic)
                == (limit == 0u       ? "RHI-ORIGINAL-INPUTS-MIP-CAPACITY"
                        : limit == 1u ? "RHI-ORIGINAL-INPUTS-READBACK-RESULT-CAPACITY"
                        : limit == 2u ? "RHI-ORIGINAL-INPUTS-READBACK-TEXT-CAPACITY"
                        : limit == 3u ? "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY"
                                      : "RHI-ORIGINAL-INPUTS-CAPACITY"));
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
        };
        descriptors[0].commandIndex = 0u;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors[0].commandIndex = 1u;
        descriptors[0].arguments = Arguments{};
        reject("RHI-ORIGINAL-INPUTS-READBACK-OWNER");
        descriptors[0].arguments = *arguments;
        {
            auto equal = rhi::TextureReadbackInputSnapshot::prepareOriginal(chain.desc,
                {1u, true, 2u, 4u, metadataBytes, metadataBytes},
                graphQuota,
                graphQuota,
                diagnostic);
            assert(equal && equal->matches(chain.desc));
            descriptors[0].arguments = *Arguments::captureOriginal(equal);
            reject("RHI-ORIGINAL-INPUTS-READBACK-OWNER");
            descriptors[0].arguments = *arguments;
            auto changed = fields;
            changed[1].text = "fake";
            auto different = Store::prepareOriginal(changed.data(),
                changed.size(),
                {1u, true, 5u, 0u, 8u, 0u, metadataBytes},
                graphQuota,
                diagnostic);
            assert(different);
            auto original = scalars;
            scalars = Scalars::prepareOriginal(usage, different, graphQuota, diagnostic);
            assert(scalars);
            reject("RHI-ORIGINAL-INPUTS-READBACK-LABEL");
            scalars = original;
        }
        std::array<Inputs::IncomingReference, 2> raw{chain.object, chain.request.get()};
        const auto incoming = [&](std::size_t index, const rhi::ReadbackTextureDesc* input) {
            return Inputs::Incoming{fields[index], input, raw.data(), raw.size()};
        };
        auto frozen = factory();
        assert(frozen && !Inputs::validateOriginal(frozen) && frozen->retainedOwnerCount() == 15u);
        OriginalCommandReadbackResultContractFixture::verify(frozen);
        auto compiled = OriginalCommandStreamProgramFixture::verifyInputs(
            frozen, {5u, 2u, 4u, 0u, 0u, 0u, 4u, 0u, 0u, 0u, 0u, 1536u}, "texture-readback");
        if (requireRegistered)
            OriginalCommandTextureTransferStatesFixture::verify(
                frozen, device, 1u, 2u, 21u, chain.desc.sourceSubresources, "readback");
        assert(!frozen->compareOriginalInput(1u, {fields[1], *arguments, raw.data(), raw.size()}));
        verifyFields(frozen, incoming, chain.desc);
        {
            auto other = rhi::TextureReadbackRequestDeclaration::prepareOriginal(chain.layout,
                {18u, 23u, 29u},
                {1u, true, 2u, 4u, 768u, metadataBytes},
                graphQuota,
                diagnostic);
            assert(other);
            raw[1] = other.get();
            assert(frozen->compareOriginalInput(1u, incoming(1u, &chain.desc)));
            raw[1] = chain.request.get();
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, graphQuota}, [&]() noexcept {
                if (const auto* id = Inputs::validateOriginal(frozen))
                    return id;
                if (requireRegistered)
                    if (const auto* id = Sources::validateRegisteredLifetime(sources, device))
                        return id;
                for (std::size_t i = 1u; i < 3u; ++i)
                    if (const auto* id = frozen->compareOriginalInput(i, incoming(i, &chain.desc)))
                        return id;
                if (!frozen->compareOriginalInput(1u, incoming(1u, nullptr)))
                    return "TEST-NULL-READBACK-ACCEPTED";
                const auto original = raw[1];
                raw[1] = chain.destinationObject;
                const auto* different = frozen->compareOriginalInput(1u, incoming(1u, &chain.desc));
                raw[1] = original;
                if (!different)
                    return "TEST-WRONG-READBACK-DOMAIN-ACCEPTED";
                auto& physical = const_cast<rhi::TextureDesc&>(chain.object->desc());
                ++physical.extent.width;
                const auto* changed = Inputs::validateOriginal(frozen);
                --physical.extent.width;
                if (!changed)
                    return "TEST-MUTATED-READBACK-TEXTURE-ACCEPTED";
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!frozen->executionDiagnostic(backend) || frozen->nativeExecutionProven()
                        || readback->nativeExecutionProven())
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        std::weak_ptr<const rhi::TextureReadbackSourceLease> observedLease = chain.lease;
        std::weak_ptr<const rhi::TextureReadbackRequestDeclaration> observedRequest = chain.request;
        const auto* object = chain.object;
        descriptors[0].arguments = Arguments{};
        descriptors[1].arguments = Arguments{};
        arguments.reset();
        chain.drop();
        retained = {};
        texture.reset();
        readback.reset();
        scalars.reset();
        store.reset();
        usage.reset();
        sources.reset();
        assert(!observedLease.expired() && !observedRequest.expired()
            && observedLease.lock()->sourceTexture() == object
            && !Inputs::validateOriginal(frozen));
        frozen.reset();
        assert(!observedLease.expired() && !observedRequest.expired() && !compiled->validate());
        compiled.reset();
        assert(observedLease.expired() && observedRequest.expired());
        observedLease.reset();
        observedRequest.reset();
        assert(!sourceQuota->liveBytes() && !graphQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_TEXTURE_READBACK_INPUTS_OK %llu %llu %llu %llu readbacks2 mips4 declared-results1536 owner15 exact-existing-lease-Request-destination Graph-Source-quotas full-fields-refs independent-bounds cold-pressure single-source caller-drop hot-allocation0 gates4-closed\n",
            static_cast<unsigned long long>(texturePeak),
            static_cast<unsigned long long>(readbackPeak),
            static_cast<unsigned long long>(sourcePeak),
            static_cast<unsigned long long>(inputPeak));
    }

private:
    template <class Commands, class References>
    static void verifySources(rhi::GpuDevice& device,
        Chain& chain,
        const std::shared_ptr<const Texture>& texture,
        const std::shared_ptr<const Readback>& readback,
        const std::array<rhi::OriginalCommandResourceSources::Source, 3>& retained,
        const rhi::OriginalCommandAdmissionBudget& budget,
        const Commands& commands,
        const References& refs,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        const char* diagnostic = nullptr;
        auto forgedTexture = std::shared_ptr<const Texture>(sourceQuota, texture.get());
        assert(Texture::validateOriginal(forgedTexture));
        auto forgedReadback = std::shared_ptr<const Readback>(sourceQuota, readback.get());
        assert(Readback::validateOriginal(forgedReadback));
        auto forgedLease =
            std::shared_ptr<const rhi::TextureReadbackSourceLease>(sourceQuota, chain.lease.get());
        assert(!Texture::prepareOriginal(
            forgedLease, localBudget(7u), sourceQuota, graphQuota, diagnostic));
        for (unsigned missing = 0u; missing < 2u; ++missing) {
            const std::array<Sources::Source, 2> pair{retained[1u - missing], readback};
            assert(
                !Sources::prepareOriginal(pair.data(), pair.size(), budget, sourceQuota, diagnostic)
                && std::string_view(diagnostic)
                    == (missing == 0u ? "RHI-ORIGINAL-SOURCES-READBACK-SOURCE"
                                      : "RHI-ORIGINAL-SOURCES-READBACK-DESTINATION"));
        }
        auto broader = budget;
        broader.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Readback)] =
            2u;
        const std::array<Sources::Source, 4> duplicate{
            texture, chain.destination, readback, readback};
        assert(!Sources::prepareOriginal(
                   duplicate.data(), duplicate.size(), broader, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-DUPLICATE");
        auto another = Readback::prepareOriginal(
            texture, chain.destination, localBudget(10u), sourceQuota, diagnostic);
        assert(another);
        auto alias = duplicate;
        alias[3] = another;
        assert(
            !Sources::prepareOriginal(alias.data(), alias.size(), broader, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-IDENTITY-ALIAS");
        auto foreign = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        assert(!Texture::prepareOriginal(
            chain.lease, localBudget(7u), sourceQuota, foreign, diagnostic));
        for (unsigned part = 0u; part < 5u; ++part) {
            auto quotas = std::array{graphQuota, graphQuota, graphQuota, graphQuota, sourceQuota};
            quotas[part] = foreign;
            Chain mixed(device, quotas, sourceQuota);
            assert(!Texture::prepareOriginal(
                mixed.lease, localBudget(7u), sourceQuota, graphQuota, diagnostic));
            assert(std::string_view(diagnostic)
                == (part == 4u ? "RHI-ORIGINAL-READBACK-TEXTURE-SOURCE-QUOTA"
                               : "RHI-ORIGINAL-READBACK-TEXTURE-GRAPH-QUOTA"));
        }
        {
            Chain independent(
                device, {foreign, foreign, foreign, foreign, sourceQuota}, sourceQuota);
            auto retainedTexture = Texture::prepareOriginal(
                independent.lease, localBudget(7u), sourceQuota, foreign, diagnostic);
            assert(retainedTexture);
            auto retainedReadback = Readback::prepareOriginal(retainedTexture,
                independent.destination,
                localBudget(10u),
                sourceQuota,
                diagnostic);
            assert(retainedReadback);
            const std::array<Sources::Source, 3> entries{
                retainedTexture, independent.destination, retainedReadback};
            auto table = Sources::prepareOriginal(
                entries.data(), entries.size(), budget, sourceQuota, diagnostic);
            assert(table);
            assert(!Usage::prepareOriginal(table,
                       commands.data(),
                       commands.size(),
                       refs.data(),
                       refs.size(),
                       graphQuota,
                       diagnostic)
                && std::string_view(diagnostic) == "RHI-ORIGINAL-USAGE-READBACK-GRAPH-QUOTA");
            assert(!Readback::prepareOriginal(
                       texture, independent.destination, localBudget(10u), sourceQuota, diagnostic)
                && std::string_view(diagnostic) == "RHI-ORIGINAL-TEXTURE-READBACK-DESTINATION");
        }
        for (unsigned kind = 0u; kind < 2u; ++kind) {
            Chain invalid(device,
                {graphQuota, graphQuota, graphQuota, graphQuota, sourceQuota},
                sourceQuota,
                kind == 0u ? 767u : 768u,
                kind == 0u ? rhi::ResourceUsage::CopyDst : rhi::ResourceUsage::CopySrc);
            auto original = Texture::prepareOriginal(
                invalid.lease, localBudget(7u), sourceQuota, graphQuota, diagnostic);
            assert(original);
            assert(!Readback::prepareOriginal(
                       original, invalid.destination, localBudget(10u), sourceQuota, diagnostic)
                && std::string_view(diagnostic)
                    == "RHI-ORIGINAL-TEXTURE-READBACK-DESTINATION-RANGE");
        }
        for (unsigned part = 0u; part < 4u; ++part) {
            auto smaller = localBudget(7u);
            if (part == 0u)
                --smaller.maxRetainedOwners;
            if (part == 1u)
                --smaller.maxMipCopies;
            if (part == 2u)
                --smaller.maxLabelBytes;
            if (part == 3u)
                --smaller.maxDeclaredResultBytes;
            assert(!Texture::prepareOriginal(
                chain.lease, smaller, sourceQuota, graphQuota, diagnostic));
            smaller = localBudget(10u);
            if (part == 0u)
                --smaller.maxRetainedOwners;
            if (part == 1u)
                --smaller.maxMipCopies;
            if (part == 2u)
                --smaller.maxLabelBytes;
            if (part == 3u)
                --smaller.maxDeclaredResultBytes;
            assert(!Readback::prepareOriginal(
                texture, chain.destination, smaller, sourceQuota, diagnostic));
        }
        for (const auto& quota : std::array{sourceQuota, graphQuota}) {
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1u);
            assert(Texture::validateOriginal(texture) && Readback::validateOriginal(readback));
            quota->setLimitBytes(limit);
        }
        {
            auto object = device.createTexture(chain.object->desc());
            assert(object);
            auto unrelated = rhi::OriginalCommandTextureLease::prepareOriginal(
                object, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
            assert(unrelated);
            const std::array<Sources::Source, 4> entries{
                texture, chain.destination, readback, unrelated};
            auto bounded = budget;
            bounded.maxGraphOwners = 17u;
            bounded
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                2u;
            auto table = Sources::prepareOriginal(
                entries.data(), entries.size(), bounded, sourceQuota, diagnostic);
            assert(table);
            auto wrongReferences = refs;
            wrongReferences[0].sourceIndex = 3u;
            auto graph = Usage::prepareOriginal(table,
                commands.data(),
                commands.size(),
                wrongReferences.data(),
                wrongReferences.size(),
                graphQuota,
                diagnostic);
            assert(graph);
            using Scalar = rhi::CommandScalarArguments;
            using Store = rhi::CommandVariableArguments;
            using K = rhi::CommandUsageKind;
            const std::array<Store::Input, 5> fields{
                {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::TextureReadback, Scalar::None{}), "read"},
                    {*Scalar::capture(K::TextureReadback, Scalar::None{}), "read"},
                    {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::Finish, Scalar::None{})}}};
            auto store = Store::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, 5u, 0u, 8u, 0u, metadataBytes},
                graphQuota,
                diagnostic);
            assert(store);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(graph, store, graphQuota, diagnostic);
            assert(scalars);
            const auto input =
                *rhi::TextureReadbackOriginalInputArguments::captureOriginal(chain.input);
            const std::array<rhi::OriginalCommandInputs::OriginalDescriptor, 2> descriptors{
                {{1u, input}, {2u, input}}};
            assert(!rhi::OriginalCommandInputs::prepareOriginal(
                       scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic)
                && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-READBACK-SOURCE");
        }
        auto none = budget;
        none.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Readback)] = 0u;
        assert(!Sources::prepareOriginal(
                   retained.data(), retained.size(), none, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY");
    }

    template <class Incoming>
    static void verifyFields(const std::shared_ptr<const rhi::OriginalCommandInputs>& frozen,
        Incoming&& incoming,
        const rhi::ReadbackTextureDesc& desc)
    {
        const auto rejects = [&](auto mutate) {
            auto changed = desc;
            mutate(changed);
            assert(frozen->compareOriginalInput(1u, incoming(1u, &changed)));
        };
        rejects([](auto& d) {
            d.sourceResource = {};
        });
        rejects([](auto& d) {
            d.destinationReadbackBuffer = {};
        });
        rejects([](auto& d) {
            --d.sizeBytes;
        });
        rejects([](auto& d) {
            d.stagingIntent = rhi::StagingIntent::Upload;
        });
        rejects([](auto& d) {
            d.backendOwnedStaging = false;
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
        rejects([](auto& d) {
            ++d.bytesPerRow;
        });
        rejects([](auto& d) {
            ++d.rowsPerImage;
        });
        rejects([](auto& d) {
            ++d.destinationOffsetBytes;
        });
        rejects([](auto& d) {
            ++d.sourceSubresources.baseMipLevel;
        });
        rejects([](auto& d) {
            --d.sourceSubresources.mipLevelCount;
        });
        rejects([](auto& d) {
            ++d.sourceSubresources.baseArrayLayer;
        });
        rejects([](auto& d) {
            ++d.sourceSubresources.arrayLayerCount;
        });
        rejects([](auto& d) {
            d.sourceSubresources.aspect = rhi::TextureAspect::Depth;
        });
        rejects([](auto& d) {
            d.label += "x";
        });
        rejects([](auto& d) {
            d.mipLayouts.pop_back();
        });
        rejects([](auto& d) {
            ++d.mipLayouts[1].mipLevel;
        });
        rejects([](auto& d) {
            ++d.mipLayouts[1].extent.width;
        });
        rejects([](auto& d) {
            ++d.mipLayouts[1].bytesPerRow;
        });
        rejects([](auto& d) {
            ++d.mipLayouts[1].rowsPerImage;
        });
        rejects([](auto& d) {
            ++d.mipLayouts[1].bufferOffsetBytes;
        });
    }
};
} // namespace arp::test
