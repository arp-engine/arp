#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_stream_program_fixture.h"
#include "original_command_texture_content_plan_fixture.h"
#include "original_command_texture_transfer_states_fixture.h"
#include "original_command_texture_upload_packing_source_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandTextureUploadInputsFixture final {
    static constexpr std::uint64_t metadataBytes = 65536u;

public:
    static std::shared_ptr<const rhi::OriginalCommandInputs> prepareInterleaved(
        const std::array<std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>, 2>&
            uploads,
        const std::array<std::shared_ptr<const rhi::OriginalCommandBufferLease>, 2>& staging,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& copySource,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& copyDestination,
        const char*& diagnostic)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        const std::array<Sources::Source, 7> retained{uploads[0]->originalDestinationLease(),
            uploads[0],
            uploads[1],
            staging[0],
            staging[1],
            copySource,
            copyDestination};
        const std::array<rhi::CommandUsage, 6> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::TextureUpload, 0u, 2u},
            {K::BufferCopy, 2u, 2u},
            {K::TextureUpload, 4u, 2u},
            {K::EndTransferPass, 6u, 0u},
            {K::Finish, 6u, 0u}}};
        const std::array<Usage::Reference, 6> references{{{0u}, {1u}, {5u}, {6u}, {0u}, {2u}}};
        rhi::BufferCopyDesc copy;
        copy.sourceBuffer = copySource->resource();
        copy.destinationBuffer = copyDestination->resource();
        copy.sizeBytes = 16u;
        const auto a =
            rhi::TextureUploadOriginalInputArguments::captureOriginal(uploads[0]->originalInput());
        const auto b =
            rhi::TextureUploadOriginalInputArguments::captureOriginal(uploads[1]->originalInput());
        if (!a || !b)
            return {};
        const std::array<Inputs::OriginalDescriptor, 3> descriptors{
            {{1u, *a}, {2u, rhi::TransferCopyArguments::captureScalarFields(copy)}, {3u, *b}}};
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxStateObservations = 6u;
        budget.maxTextureRanges = 2u;
        budget.maxStateTransactions = budget.maxPendingSubmissions = 1u;
        budget.maxCallbackTickets = budget.maxCallbackRegistrations = 8u;
        budget.maxNativePayloadObjects = 5u;
        budget.maxNativeStagingBytes = staging[0]->originalObject()->desc().sizeBytes
            + staging[1]->originalObject()->desc().sizeBytes;
        budget.maxMipMappings = 4u;
        budget.maxRetainedUploadBytes = 40u;
        for (const auto domain : {rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording,
                 rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission,
                 rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback})
            budget.maxCpuBytes[static_cast<std::size_t>(domain)] = metadataBytes;
        return OriginalCommandUploadStreamBuilderFixture::prepare(retained,
            commands,
            references,
            descriptors,
            budget,
            uploads[0]->originalMetadataQuota(),
            uploads[0]->originalInput()->originalMetadataQuota(),
            diagnostic);
    }

    // Reusable exact two-upload graph over an already frozen original source.
    // Used by content/staging tests; no new resource, byte snapshot or journal.
    static std::shared_ptr<const rhi::OriginalCommandInputs> prepareForSource(
        std::shared_ptr<const rhi::OriginalCommandTextureUploadSource> upload,
        const char*& diagnostic,
        std::shared_ptr<const rhi::OriginalCommandBufferLease> staging = {},
        std::uint32_t nativeObjects = 0u,
        std::uint64_t nativeStagingBytes = 0u,
        bool nativeLifecycle = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Store = rhi::CommandVariableArguments;
        using Inputs = rhi::OriginalCommandInputs;
        auto sourceQuota = upload->originalMetadataQuota(),
             graphQuota = upload->originalInput()->originalMetadataQuota();
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxStateObservations = 2u;
        budget.maxTextureRanges = 2u;
        budget.maxCommands = 5u;
        budget.maxReferences = 4u;
        budget.maxInputDescriptors = 2u;
        budget.maxGraphOwners = 10u;
        budget.maxMipMappings = 2u * upload->originalInput()->mipLayouts().size();
        budget.maxRetainedUploadBytes = 2u * upload->originalInput()->pixelBytes().size();
        budget.maxStateTransactions = 1u;
        budget.maxLabelBytes = 0u;
        budget.maxNativePayloadObjects = nativeObjects;
        budget.maxNativeStagingBytes = nativeStagingBytes;
        if (nativeLifecycle) {
            budget.maxPendingSubmissions = 1u;
            budget.maxCallbackTickets = budget.maxCallbackRegistrations = 8u;
            budget.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)] = metadataBytes;
            budget.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = metadataBytes;
        }
        if (staging) {
            ++budget.maxStateObservations;
            budget.maxGraphOwners += rhi::OriginalCommandBufferLease::retainedOwnerCount;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                1u;
        }
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] = 1u;
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Source)] = sourceQuota->limitBytes();
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Graph)] = graphQuota->limitBytes();
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)] = metadataBytes;
        const std::array<Sources::Source, 3> retained{
            upload->originalDestinationLease(), upload, staging};
        auto sources = Sources::prepareOriginal(
            retained.data(), staging ? 3u : 2u, budget, sourceQuota, diagnostic);
        if (!sources)
            return {};
        const std::array<rhi::CommandUsage, 5> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::TextureUpload, 0u, 2u},
            {K::TextureUpload, 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        const std::array<Usage::Reference, 4> references{{{0u}, {1u}, {0u}, {1u}}};
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            references.data(),
            references.size(),
            graphQuota,
            diagnostic);
        if (!usage)
            return {};
        const std::array<Store::Input, 5> fields{
            {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::TextureUpload, Scalar::None{})},
                {*Scalar::capture(K::TextureUpload, Scalar::None{})},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 5u, 0u, 0u, 0u, graphQuota->limitBytes()},
            graphQuota,
            diagnostic);
        if (!store)
            return {};
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        if (!scalars)
            return {};
        auto arguments =
            rhi::TextureUploadOriginalInputArguments::captureOriginal(upload->originalInput());
        if (!arguments)
            return {};
        const std::array<Inputs::OriginalDescriptor, 2> descriptors{
            {{1u, *arguments}, {2u, *arguments}}};
        return Inputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic);
    }

    static void verify(rhi::GpuDevice& device,
        bool requireRegistered = false,
        core::UniquePtr<rhi::GpuTexture> supplied = {},
        std::shared_ptr<core::CpuMemoryQuota> contentQuota = {})
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        using Snapshot = rhi::TextureUploadInputSnapshot;
        using Upload = rhi::OriginalCommandTextureUploadSource;
        using Arguments = rhi::TextureUploadOriginalInputArguments;
        using B = rhi::OriginalCommandAdmissionBudget;
        using K = rhi::CommandUsageKind;
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        rhi::TextureDesc physical;
        physical.extent = {2u, 2u, 1u};
        physical.format = rhi::TextureFormat::Rgba8Unorm;
        physical.usage = rhi::TextureUsage::CopyDst;
        physical.mipLevelCount = 2u;
        physical.initialState =
            requireRegistered ? rhi::ResourceState::CopyDst : rhi::ResourceState::Undefined;
        auto texture = supplied ? std::move(supplied) : device.createTexture(physical);
        assert(texture);
        const auto* object = texture.get();
        auto destination = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
        assert(destination);
        std::array<std::uint8_t, 20> pixels{};
        for (std::size_t i = 0u; i < pixels.size(); ++i)
            pixels[i] = static_cast<std::uint8_t>(i + 1u);
        rhi::UploadTextureDesc desc;
        desc.destinationTexture = object->id();
        desc.extent = physical.extent;
        desc.format = physical.format;
        desc.bytesPerRow = 8u;
        desc.rowsPerImage = 2u;
        desc.subresources.mipLevelCount = 2u;
        desc.pixelData = pixels.data();
        desc.pixelDataSize = pixels.size();
        desc.label = "upload";
        desc.frameSerial = 23u;
        desc.resourceGeneration = 29u;
        desc.mipLayouts = {{0u, {2u, 2u, 1u}, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, 16u}};
        const rhi::TextureUploadInputSnapshotBudget local{
            1u, true, 2u, 64u, 32u, metadataBytes, metadataBytes, metadataBytes};
        auto snapshot =
            Snapshot::prepareOriginal(desc, local, graphQuota, graphQuota, sourceQuota, diagnostic);
        assert(snapshot);
        CpuQuotaFactoryFixture sourcePressure(sourceQuota), graphPressure(graphQuota);
        const auto uploadFactory = [&] {
            return Upload::prepareOriginal(
                snapshot, destination, {1u, true, 5u, metadataBytes}, sourceQuota, diagnostic);
        };
        const auto uploadPeak = sourcePressure.measure(uploadFactory);
        auto upload = uploadFactory();
        assert(upload);
        assert(!Upload::validateOriginal(upload) && !upload->resource().isValid()
            && upload->originalObject() == snapshot.get());
        OriginalCommandTextureUploadPackingSourceFixture::verify(upload);
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxStateObservations = 2u;
        budget.maxTextureRanges = 2u;
        budget.maxCommands = 5u;
        budget.maxReferences = 4u;
        budget.maxInputDescriptors = 2u;
        budget.maxGraphOwners = 10u;
        budget.maxMipMappings = 4u;
        budget.maxRetainedUploadBytes = 40u;
        budget.maxLabelBytes = 24u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 1u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] = 1u;
        budget.maxStateTransactions = 1u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Recording)] = metadataBytes;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Source)] = metadataBytes;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Graph)] = metadataBytes;
        std::array<Sources::Source, 2> retained{destination, upload};
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
            {K::TextureUpload, 0u, 2u},
            {K::TextureUpload, 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        const std::array<Usage::Reference, 4> refs{{{0u}, {1u}, {0u}, {1u}}};
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            graphQuota,
            diagnostic);
        assert(usage && usage->retainedOwnerCount() == 7u);
        const std::array<Store::Input, 5> fields{
            {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::TextureUpload, Scalar::None{}), "upload"},
                {*Scalar::capture(K::TextureUpload, Scalar::None{}), "upload"},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 5u, 0u, 12u, 0u, metadataBytes},
            graphQuota,
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        assert(scalars && scalars->retainedOwnerCount() == 9u);
        auto arguments = Arguments::captureOriginal(snapshot);
        assert(arguments);
        std::array<Inputs::OriginalDescriptor, 2> descriptors{{{1u, *arguments}, {2u, *arguments}}};
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic);
        };
        const auto inputPeak = graphPressure.measure(factory);
        sourcePressure.verify(uploadPeak, uploadFactory);
        sourcePressure.verify(sourcePeak, sourceFactory);
        graphPressure.verify(inputPeak, factory);
        for (const auto& quota : std::array{sourceQuota, graphQuota}) {
            const auto baseline = quota->liveBytes();
            {
                core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
                assert(full.charged());
                if (quota == sourceQuota)
                    assert(!uploadFactory() && !sourceFactory());
                else
                    assert(!factory());
            }
            assert(quota->liveBytes() == baseline);
        }
        const auto reject = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id);
        };
        descriptors[0].commandIndex = 0u;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        descriptors[0].commandIndex = 1u;
        descriptors[0].arguments = Arguments{};
        reject("RHI-ORIGINAL-INPUTS-UPLOAD-OWNER");
        descriptors[0].arguments = *arguments;
        auto twin =
            Snapshot::prepareOriginal(desc, local, graphQuota, graphQuota, sourceQuota, diagnostic);
        assert(twin);
        descriptors[0].arguments = *Arguments::captureOriginal(twin);
        reject("RHI-ORIGINAL-INPUTS-UPLOAD-OWNER");
        descriptors[0].arguments = *arguments;
        twin.reset();
        for (unsigned limit = 0u; limit < 5u; ++limit) {
            auto bounded = budget;
            if (limit == 0u)
                --bounded.maxMipMappings;
            if (limit == 1u)
                --bounded.maxRetainedUploadBytes;
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
            const auto* expected = limit == 0u ? "RHI-ORIGINAL-INPUTS-MIP-CAPACITY"
                : limit == 1u                  ? "RHI-ORIGINAL-INPUTS-UPLOAD-CAPACITY"
                : limit == 2u                  ? "RHI-ORIGINAL-INPUTS-UPLOAD-TEXT-CAPACITY"
                : limit == 3u                  ? "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY"
                                               : "RHI-ORIGINAL-INPUTS-CAPACITY";
            assert(std::string_view(diagnostic) == expected);
        }
        {
            auto changed = fields;
            changed[1].text = "altered";
            auto different = Store::prepareOriginal(changed.data(),
                changed.size(),
                {1u, true, 5u, 0u, 24u, 0u, metadataBytes},
                graphQuota,
                diagnostic);
            assert(different);
            auto scalar = Scalars::prepareOriginal(usage, different, graphQuota, diagnostic);
            assert(scalar);
            assert(!Inputs::prepareOriginal(
                       scalar, descriptors.data(), descriptors.size(), graphQuota, diagnostic)
                && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-UPLOAD-LABEL");
        }
        verifySources(device,
            desc,
            snapshot,
            destination,
            upload,
            retained,
            budget,
            commands,
            refs,
            sourceQuota,
            graphQuota);
        std::array<Inputs::IncomingReference, 2> raw{object, snapshot.get()};
        const auto incoming = [&](std::size_t index, const rhi::UploadTextureDesc* input) {
            return Inputs::Incoming{fields[index], input, raw.data(), raw.size()};
        };
        auto frozen = factory();
        assert(frozen && !Inputs::validateOriginal(frozen) && frozen->retainedOwnerCount() == 10u);
        // Two uploads, each with mip rows 2 + 1 at the normative packed pitch.
        constexpr auto packedBytes = 2u * rhi::kTextureUploadRowPitchAlignment * (2u + 1u);
        auto compiled = OriginalCommandStreamProgramFixture::verifyInputs(frozen,
            {5u, 2u, 4u, 0u, 0u, 4u, 0u, 0u, 0u, 0u, 40u},
            "texture-upload",
            {0u, 4u, 0u, packedBytes, packedBytes});
        if (requireRegistered) {
            if (object->originalContentExclusive())
                OriginalCommandTextureContentPlanFixture::verify(
                    frozen, device, *object, contentQuota);
            else
                OriginalCommandTextureTransferStatesFixture::verify(
                    frozen, device, 0u, 1u, 15u, desc.subresources, "upload");
        }
        assert(!frozen->compareOriginalInput(1u, {fields[1], *arguments, raw.data(), raw.size()}));
        verifyFields(frozen, incoming, desc, pixels);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota, graphQuota}, [&]() noexcept {
                if (const auto* id = Inputs::validateOriginal(frozen))
                    return id;
                if (requireRegistered)
                    if (const auto* id = Sources::validateRegisteredLifetime(sources, device))
                        return id;
                for (std::size_t i = 1u; i < 3u; ++i)
                    if (const auto* id = frozen->compareOriginalInput(i, incoming(i, &desc)))
                        return id;
                if (!frozen->compareOriginalInput(1u, incoming(1u, nullptr)))
                    return "TEST-NULL-UPLOAD-ACCEPTED";
                const auto original = raw[1];
                raw[1] = object;
                const auto* mismatch = frozen->compareOriginalInput(1u, incoming(1u, &desc));
                raw[1] = original;
                if (!mismatch)
                    return "TEST-WRONG-UPLOAD-DOMAIN-ACCEPTED";
                ++pixels[0];
                const auto* changed = frozen->compareOriginalInput(1u, incoming(1u, &desc));
                --pixels[0];
                if (!changed)
                    return "TEST-PIXEL-MUTATION-ACCEPTED";
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!frozen->executionDiagnostic(backend) || frozen->nativeExecutionProven())
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        std::weak_ptr<const Snapshot> observed = snapshot;
        std::weak_ptr<const Upload> observedSource = upload;
        descriptors[0].arguments = Arguments{};
        descriptors[1].arguments = Arguments{};
        arguments.reset();
        retained = {};
        upload.reset();
        snapshot.reset();
        destination.reset();
        scalars.reset();
        store.reset();
        usage.reset();
        sources.reset();
        pixels.fill(0u);
        desc.mipLayouts.clear();
        desc.label.clear();
        desc.pixelData = nullptr;
        assert(
            !observed.expired() && !observedSource.expired() && !Inputs::validateOriginal(frozen));
        assert(observed.lock()->pixelBytes()[0] == 1u && observed.lock()->label() == "upload"
            && observed.lock()->mipLayouts().size() == 2u);
        frozen.reset();
        assert(!observed.expired() && !observedSource.expired() && !compiled->validate());
        compiled.reset();
        assert(observed.expired() && observedSource.expired());
        observed.reset();
        observedSource.reset();
        assert(!sourceQuota->liveBytes() && !graphQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_TEXTURE_UPLOAD_INPUTS_OK %llu %llu %llu uploads2 mips4 bytes40 owner10 canonical-source-destination Graph-Source-quotas CPU-key-namespace exact-fields-refs aggregate-bounds cold-pressure caller-drop hot-allocation0 gates4-closed\n",
            static_cast<unsigned long long>(uploadPeak),
            static_cast<unsigned long long>(sourcePeak),
            static_cast<unsigned long long>(inputPeak));
    }

private:
    template <class Commands, class References>
    static void verifySources(rhi::GpuDevice& device,
        const rhi::UploadTextureDesc& desc,
        const std::shared_ptr<const rhi::TextureUploadInputSnapshot>& snapshot,
        const std::shared_ptr<const rhi::OriginalCommandTextureLease>& destination,
        const std::shared_ptr<const rhi::OriginalCommandTextureUploadSource>& upload,
        const std::array<rhi::OriginalCommandResourceSources::Source, 2>& retained,
        const rhi::OriginalCommandAdmissionBudget& budget,
        const Commands& commands,
        const References& refs,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota)
    {
        using Upload = rhi::OriginalCommandTextureUploadSource;
        using Snapshot = rhi::TextureUploadInputSnapshot;
        using Sources = rhi::OriginalCommandResourceSources;
        const char* diagnostic = nullptr;
        const auto factory = [&](auto input, auto target) {
            return Upload::prepareOriginal(
                input, target, {1u, true, 5u, metadataBytes}, sourceQuota, diagnostic);
        };
        auto forged = std::shared_ptr<const Snapshot>(sourceQuota, snapshot.get());
        assert(!factory(forged, destination));
        auto forgedUpload = std::shared_ptr<const Upload>(sourceQuota, upload.get());
        assert(Upload::validateOriginal(forgedUpload));
        auto absent = retained;
        absent[0] = upload;
        assert(!Sources::prepareOriginal(absent.data() + 1u, 1u, budget, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-UPLOAD-DESTINATION");
        auto duplicated = std::array<Sources::Source, 3>{destination, upload, upload};
        auto broader = budget;
        broader.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] = 2u;
        assert(!Sources::prepareOriginal(
                   duplicated.data(), duplicated.size(), broader, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-DUPLICATE");
        auto alias = factory(snapshot, destination);
        assert(alias);
        duplicated[2] = alias;
        assert(!Sources::prepareOriginal(
                   duplicated.data(), duplicated.size(), broader, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-IDENTITY-ALIAS");
        auto otherObject = device.createTexture(destination->originalObject()->desc());
        assert(otherObject);
        auto other = rhi::OriginalCommandTextureLease::prepareOriginal(
            otherObject, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
        assert(other);
        assert(!factory(snapshot, other)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-TEXTURE-UPLOAD-DESTINATION");
        auto missing = retained;
        missing[0] = other;
        assert(!Sources::prepareOriginal(
                   missing.data(), missing.size(), budget, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-UPLOAD-DESTINATION");
        auto foreign = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const rhi::TextureUploadInputSnapshotBudget local{
            1u, true, 2u, 64u, 32u, metadataBytes, metadataBytes, metadataBytes};
        auto foreignInput =
            Snapshot::prepareOriginal(desc, local, foreign, foreign, sourceQuota, diagnostic);
        assert(foreignInput);
        auto foreignSource = factory(foreignInput, destination);
        assert(foreignSource);
        auto foreignEntries = retained;
        foreignEntries[1] = foreignSource;
        auto foreignTable = Sources::prepareOriginal(
            foreignEntries.data(), foreignEntries.size(), budget, sourceQuota, diagnostic);
        assert(foreignTable);
        assert(!rhi::OriginalCommandUsage::prepareOriginal(foreignTable,
                   commands.data(),
                   commands.size(),
                   refs.data(),
                   refs.size(),
                   graphQuota,
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-USAGE-UPLOAD-GRAPH-QUOTA");
        auto wrongPixels =
            Snapshot::prepareOriginal(desc, local, graphQuota, graphQuota, foreign, diagnostic);
        assert(wrongPixels && !factory(wrongPixels, destination));
        auto wrongLabels =
            Snapshot::prepareOriginal(desc, local, graphQuota, foreign, sourceQuota, diagnostic);
        assert(wrongLabels && !factory(wrongLabels, destination));
        const auto rejectLayout = [&](auto mutate, const char* expected) {
            auto changed = desc;
            mutate(changed);
            auto input = Snapshot::prepareOriginal(
                changed, local, graphQuota, graphQuota, sourceQuota, diagnostic);
            assert(input);
            assert(!factory(input, destination) && std::string_view(diagnostic) == expected);
        };
        rejectLayout(
            [](auto& d) {
                d.format = rhi::TextureFormat::R32Float;
            },
            "RHI-TEXTURE-UPLOAD-LAYOUT-FORMAT-MISMATCH");
        rejectLayout(
            [](auto& d) {
                d.mipLayouts[1].mipLevel = 0u;
            },
            "RHI-TEXTURE-UPLOAD-LAYOUT-MIP-ORDER");
        rejectLayout(
            [](auto& d) {
                d.mipLayouts[1].bufferOffsetBytes = 12u;
            },
            "RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-OVERLAP");
        rejectLayout(
            [](auto& d) {
                d.mipLayouts[1].bufferOffsetBytes = 20u;
            },
            "RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-RANGE");
        rejectLayout(
            [](auto& d) {
                d.mipLayouts[1].bytesPerRow = 3u;
            },
            "RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-PITCH");
        rejectLayout(
            [](auto& d) {
                d.backendOwnedStaging = false;
            },
            "RHI-TEXTURE-UPLOAD-LAYOUT-INPUT");
        rejectLayout(
            [](auto& d) {
                d.subresources.mipLevelCount = 1u;
            },
            "RHI-TEXTURE-UPLOAD-LAYOUT-RANGE");
        for (const auto& quota : std::array{sourceQuota, graphQuota}) {
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1u);
            assert(Upload::validateOriginal(upload));
            quota->setLimitBytes(limit);
        }
        auto noUpload = budget;
        noUpload.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] =
            0u;
        assert(!Sources::prepareOriginal(
                   retained.data(), retained.size(), noUpload, sourceQuota, diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-SOURCES-DOMAIN-CAPACITY");
    }

    template <class Incoming>
    static void verifyFields(const std::shared_ptr<const rhi::OriginalCommandInputs>& frozen,
        Incoming&& incoming,
        const rhi::UploadTextureDesc& desc,
        std::array<std::uint8_t, 20>& pixels)
    {
        const auto rejects = [&](auto mutate) {
            auto changed = desc;
            mutate(changed);
            assert(frozen->compareOriginalInput(1u, incoming(1u, &changed)));
        };
        rejects([](auto& d) {
            d.destinationTexture = {};
        });
        rejects([](auto& d) {
            --d.extent.width;
        });
        rejects([](auto& d) {
            ++d.destinationOffset.width;
        });
        rejects([](auto& d) {
            d.format = rhi::TextureFormat::R32Float;
        });
        rejects([](auto& d) {
            ++d.bytesPerRow;
        });
        rejects([](auto& d) {
            ++d.rowsPerImage;
        });
        rejects([](auto& d) {
            ++d.mipLevel;
        });
        rejects([](auto& d) {
            ++d.arrayLayer;
        });
        rejects([](auto& d) {
            ++d.subresources.baseMipLevel;
        });
        rejects([](auto& d) {
            --d.subresources.mipLevelCount;
        });
        rejects([](auto& d) {
            ++d.subresources.baseArrayLayer;
        });
        rejects([](auto& d) {
            ++d.subresources.arrayLayerCount;
        });
        rejects([](auto& d) {
            d.subresources.aspect = rhi::TextureAspect::Depth;
        });
        rejects([](auto& d) {
            --d.pixelDataSize;
        });
        rejects([](auto& d) {
            d.pixelData = nullptr;
        });
        rejects([](auto& d) {
            d.label += "x";
        });
        rejects([](auto& d) {
            d.backendOwnedStaging = false;
        });
        rejects([](auto& d) {
            d.stagingIntent = rhi::StagingIntent::Readback;
        });
        rejects([](auto& d) {
            ++d.frameSerial;
        });
        rejects([](auto& d) {
            ++d.resourceGeneration;
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
        ++pixels[19];
        assert(frozen->compareOriginalInput(1u, incoming(1u, &desc)));
        --pixels[19];
    }
};
} // namespace arp::test
