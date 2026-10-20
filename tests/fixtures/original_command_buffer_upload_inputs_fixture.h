#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_stream_program_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandBufferUploadInputsFixture final {
    static constexpr std::uint64_t metadataBytes = 65536u;

public:
    static std::shared_ptr<const rhi::OriginalCommandInputs> prepareInterleaved(
        const std::array<std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>, 2>& uploads,
        std::shared_ptr<const rhi::OriginalCommandBufferLease> checkpoint,
        const char*& diagnostic)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        using S = rhi::ResourceState;
        const auto& target = uploads[0]->originalDestinationLease();
        const std::array<Sources::Source, 6> retained{uploads[0]->originalSourceLease(),
            uploads[1]->originalSourceLease(),
            target,
            checkpoint,
            uploads[0],
            uploads[1]};
        // Barriers are encoder commands: end each transfer pass before them.
        const std::array<rhi::CommandUsage, 12> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::BufferUpload, 0u, 3u},
            {K::EndTransferPass, 3u, 0u},
            {K::BufferBarrier, 3u, 1u},
            {K::BeginTransferPass, 4u, 0u},
            {K::BufferCopy, 4u, 2u},
            {K::EndTransferPass, 6u, 0u},
            {K::BufferBarrier, 6u, 1u},
            {K::BeginTransferPass, 7u, 0u},
            {K::BufferUpload, 7u, 3u},
            {K::EndTransferPass, 10u, 0u},
            {K::Finish, 10u, 0u}}};
        const std::array<Usage::Reference, 10> references{
            {{0u}, {2u}, {4u}, {2u}, {2u}, {3u}, {2u}, {1u}, {2u}, {5u}}};
        rhi::ResourceBarrierDesc barrier{target->resource(), {}, S::CopyDst, S::CopySrc};
        barrier.sourceStages = barrier.destinationStages = rhi::PipelineStage::Transfer;
        barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
        barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
        const auto first = *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, barrier);
        std::swap(barrier.before, barrier.after);
        std::swap(barrier.sourceAccess, barrier.destinationAccess);
        rhi::BufferCopyDesc copy;
        copy.sourceBuffer = target->resource();
        copy.destinationBuffer = checkpoint->resource();
        copy.sizeBytes = uploads[0]->sizeBytes();
        const std::array<Inputs::OriginalDescriptor, 5> descriptors{{{1u, uploads[0]->arguments()},
            {3u, first},
            {5u, rhi::TransferCopyArguments::captureScalarFields(copy)},
            {7u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, barrier)},
            {9u, uploads[1]->arguments()}}};
        rhi::OriginalCommandAdmissionBudget b;
        b.contractVersion = 1u;
        b.prototypeOptIn = true;
        b.maxStateObservations = 4u;
        b.maxStateTransactions = b.maxPendingSubmissions = 1u;
        b.maxCallbackTickets = b.maxCallbackRegistrations = 8u;
        b.maxNativePayloadObjects = 4u;
        b.maxNativeStagingBytes = uploads[0]->sizeBytes() + uploads[1]->sizeBytes();
        b.maxRetainedUploadBytes = b.maxNativeStagingBytes;
        for (const auto domain : {rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording,
                 rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission,
                 rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback})
            b.maxCpuBytes[static_cast<std::size_t>(domain)] = metadataBytes;
        return OriginalCommandUploadStreamBuilderFixture::prepare(retained,
            commands,
            references,
            descriptors,
            b,
            uploads[0]->originalMetadataQuota(),
            uploads[0]->originalInputQuota(),
            diagnostic);
    }

    // Same two-command stream, over pre-existing initialized source ownership.
    static std::shared_ptr<const rhi::OriginalCommandInputs> prepareForSource(
        std::shared_ptr<const rhi::OriginalCommandBufferUploadSource> upload,
        const char*& diagnostic,
        std::uint32_t nativeObjects = 2u,
        std::uint64_t nativeStagingBytes = 16u,
        bool nativeLifecycle = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Store = rhi::CommandVariableArguments;
        using Inputs = rhi::OriginalCommandInputs;
        auto sourceQuota = upload->originalMetadataQuota(),
             graphQuota = upload->originalInputQuota();
        rhi::UploadBufferDesc desc;
        upload->arguments().writeScalarFields(desc);
        rhi::OriginalCommandAdmissionBudget budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxStateObservations = 2u;
        budget.maxCommands = 5u;
        budget.maxReferences = 6u;
        budget.maxInputDescriptors = 2u;
        budget.maxGraphOwners = 11u;
        budget.maxRetainedUploadBytes = 2u * desc.sizeBytes;
        budget.maxStateTransactions = 1u;
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
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 2u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] = 1u;
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Source)] = sourceQuota->limitBytes();
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Graph)] = graphQuota->limitBytes();
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)] = metadataBytes;
        const std::array<Sources::Source, 3> retained{
            upload->originalSourceLease(), upload->originalDestinationLease(), upload};
        auto sources = Sources::prepareOriginal(
            retained.data(), retained.size(), budget, sourceQuota, diagnostic);
        if (!sources)
            return {};
        const std::array<rhi::CommandUsage, 5> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::BufferUpload, 0u, 3u},
            {K::BufferUpload, 3u, 3u},
            {K::EndTransferPass, 6u, 0u},
            {K::Finish, 6u, 0u}}};
        const std::array<Usage::Reference, 6> references{{{0u}, {1u}, {2u}, {0u}, {1u}, {2u}}};
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
                {*Scalar::capture(K::BufferUpload, Scalar::None{})},
                {*Scalar::capture(K::BufferUpload, Scalar::None{})},
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
        const std::array<Inputs::OriginalDescriptor, 2> descriptors{
            {{1u, upload->arguments()}, {2u, upload->arguments()}}};
        return Inputs::prepareOriginal(
            scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic);
    }

    static void verify(rhi::GpuDevice& device, bool requireRegistered = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Usage = rhi::OriginalCommandUsage;
        using Scalars = rhi::OriginalCommandScalars;
        using Store = rhi::CommandVariableArguments;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        using Upload = rhi::OriginalCommandBufferUploadSource;
        using B = rhi::OriginalCommandAdmissionBudget;
        using K = rhi::CommandUsageKind;
        using Arguments = rhi::BufferUploadArguments;
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes),
             graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        rhi::BufferDesc physical;
        physical.sizeBytes = 8u;
        physical.usage = rhi::ResourceUsage::CopySrc;
        auto sourceObject = device.createBuffer(physical);
        assert(sourceObject);
        const auto* rawSource = sourceObject.get();
        physical.usage = rhi::ResourceUsage::CopyDst;
        auto destinationObject = device.createBuffer(physical);
        assert(destinationObject);
        const auto* rawDestination = destinationObject.get();
        auto source = rhi::OriginalCommandBufferLease::prepareOriginal(
            sourceObject, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
        assert(source);
        auto destination = rhi::OriginalCommandBufferLease::prepareOriginal(
            destinationObject, {1u, true, 2u, metadataBytes}, sourceQuota, diagnostic);
        assert(destination);
        rhi::UploadBufferDesc desc;
        desc.sourceUploadBuffer = source->resource();
        desc.destinationResource = destination->resource();
        desc.sizeBytes = 8u;
        desc.frameSerial = 23u;
        desc.label = "buf";
        const rhi::OriginalCommandBufferUploadSourceBudget local{
            1u, true, 6u, 32u, metadataBytes, metadataBytes};
        const auto uploadFactory = [&] {
            return Upload::prepareOriginal(
                desc, source, destination, local, sourceQuota, graphQuota, diagnostic);
        };
        CpuQuotaFactoryFixture sourcePressure(sourceQuota), graphPressure(graphQuota);
        const auto uploadSourcePeak = sourcePressure.measure(uploadFactory),
                   uploadGraphPeak = graphPressure.measure(uploadFactory);
        auto upload = uploadFactory();
        assert(upload && !Upload::validateOriginal(upload) && !upload->resource().isValid());
        B budget;
        budget.contractVersion = 1u;
        budget.prototypeOptIn = true;
        budget.maxCommands = 5u;
        budget.maxReferences = 6u;
        budget.maxGraphOwners = 11u;
        budget.maxInputDescriptors = 2u;
        budget.maxRetainedUploadBytes = 16u;
        budget.maxLabelBytes = 12u;
        budget.maxStateObservations = 2u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Recording)] = metadataBytes;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 2u;
        budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] = 1u;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Source)] = metadataBytes;
        budget.maxCpuBytes[static_cast<std::size_t>(B::CpuDomain::Graph)] = metadataBytes;
        std::array<Sources::Source, 3> retained{source, destination, upload};
        auto sources = Sources::prepareOriginal(
            retained.data(), retained.size(), budget, sourceQuota, diagnostic);
        assert(sources);
        if (requireRegistered)
            assert(!Sources::validateRegisteredLifetime(sources, device));
        const std::array<rhi::CommandUsage, 5> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::BufferUpload, 0u, 3u},
            {K::BufferUpload, 3u, 3u},
            {K::EndTransferPass, 6u, 0u},
            {K::Finish, 6u, 0u}}};
        const std::array<Usage::Reference, 6> refs{{{0u}, {1u}, {2u}, {0u}, {1u}, {2u}}};
        auto usage = Usage::prepareOriginal(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            graphQuota,
            diagnostic);
        assert(usage && usage->retainedOwnerCount() == 8u);
        const std::array<Store::Input, 5> fields{
            {{*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                {*Scalar::capture(K::BufferUpload, Scalar::None{}), "buf"},
                {*Scalar::capture(K::BufferUpload, Scalar::None{}), "buf"},
                {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                {*Scalar::capture(K::Finish, Scalar::None{})}}};
        auto store = Store::prepareOriginal(fields.data(),
            fields.size(),
            {1u, true, 5u, 0u, 12u, 0u, metadataBytes},
            graphQuota,
            diagnostic);
        assert(store);
        auto scalars = Scalars::prepareOriginal(usage, store, graphQuota, diagnostic);
        assert(scalars && scalars->retainedOwnerCount() == 10u);
        std::array<Inputs::OriginalDescriptor, 2> descriptors{
            {{1u, Arguments::captureScalarFields(desc)},
                {2u, Arguments::captureScalarFields(desc)}}};
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, descriptors.data(), descriptors.size(), graphQuota, diagnostic);
        };
        const auto inputPeak = graphPressure.measure(factory);
        sourcePressure.verify(uploadSourcePeak, uploadFactory);
        graphPressure.verify(uploadGraphPeak, uploadFactory);
        graphPressure.verify(inputPeak, factory);
        for (const auto& quota : std::array{sourceQuota, graphQuota}) {
            const auto baseline = quota->liveBytes();
            {
                core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
                assert(full.charged() && !uploadFactory());
                if (quota == graphQuota)
                    assert(!factory());
            }
            assert(quota->liveBytes() == baseline);
        }
        for (unsigned limit = 0u; limit < 4u; ++limit) {
            auto bounded = budget;
            if (limit == 0u)
                --bounded.maxRetainedUploadBytes;
            if (limit == 1u)
                --bounded.maxLabelBytes;
            if (limit == 2u)
                --bounded.maxGraphOwners;
            if (limit == 3u)
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
                == (limit == 0u       ? "RHI-ORIGINAL-INPUTS-UPLOAD-CAPACITY"
                        : limit == 1u ? "RHI-ORIGINAL-INPUTS-UPLOAD-TEXT-CAPACITY"
                        : limit == 2u ? "RHI-ORIGINAL-INPUTS-OWNER-CAPACITY"
                                      : "RHI-ORIGINAL-INPUTS-CAPACITY"));
        }
        verifySources(desc,
            source,
            destination,
            upload,
            retained,
            budget,
            commands,
            refs,
            sourceQuota,
            graphQuota);
        {
            auto changed = desc;
            ++changed.frameSerial;
            descriptors[0].arguments = Arguments::captureScalarFields(changed);
            assert(!factory()
                && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-BUFFER-UPLOAD-INPUT");
            descriptors[0].arguments = upload->arguments();
            auto changedFields = fields;
            changedFields[1].text = "bad";
            auto different = Store::prepareOriginal(changedFields.data(),
                changedFields.size(),
                {1u, true, 5u, 0u, 12u, 0u, metadataBytes},
                graphQuota,
                diagnostic);
            assert(different);
            auto original = scalars;
            scalars = Scalars::prepareOriginal(usage, different, graphQuota, diagnostic);
            assert(scalars);
            assert(
                !factory() && std::string_view(diagnostic) == "RHI-ORIGINAL-INPUTS-UPLOAD-LABEL");
            scalars = original;
        }
        std::array<Inputs::IncomingReference, 3> raw{rawSource, rawDestination, upload.get()};
        const auto incoming = [&](std::size_t index, const rhi::UploadBufferDesc* input) {
            return Inputs::Incoming{fields[index], input, raw.data(), raw.size()};
        };
        auto frozen = factory();
        assert(frozen && !Inputs::validateOriginal(frozen) && frozen->retainedOwnerCount() == 11u);
        auto compiled = OriginalCommandStreamProgramFixture::verifyInputs(frozen,
            {5u, 2u, 6u, 0u, 2u, 0u, 0u, 0u, 0u, 16u},
            "buffer-upload",
            {2u, 0u, 16u, 0u, 16u});
        if (requireRegistered) {
            // Two repeated upload commands retain only two original GPU records.
            auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
            const rhi::OriginalCommandStateAdmissionBudget state{
                1u, true, 1u, 2u, 2u, 0u, 17u, metadataBytes};
            const auto observations = rhi::OriginalCommandBufferStates::prepareOriginal(
                frozen, device, state, quota, diagnostic);
            assert(observations && observations->retainedOwnerCount() == 17u
                && observations->entries().size() == 2u);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sourceQuota, graphQuota, quota}, [&]() noexcept {
                    return observations->compareOriginalInputs(frozen, device);
                }));
        }
        assert(!frozen->compareOriginalInput(
            1u, {fields[1], upload->arguments(), raw.data(), raw.size()}));
        const auto rejects = [&](auto mutate) {
            auto changed = desc;
            mutate(changed);
            assert(frozen->compareOriginalInput(1u, incoming(1u, &changed)));
        };
        rejects([](auto& d) {
            d.sourceUploadBuffer = {};
        });
        rejects([](auto& d) {
            d.destinationResource = {};
        });
        rejects([](auto& d) {
            --d.sizeBytes;
        });
        rejects([](auto& d) {
            d.stagingIntent = rhi::StagingIntent::Readback;
        });
        rejects([](auto& d) {
            d.backendOwnedStaging = false;
        });
        rejects([](auto& d) {
            ++d.frameSerial;
        });
        rejects([](auto& d) {
            d.label += "x";
        });
        auto twin = uploadFactory();
        assert(twin);
        raw[2] = twin.get();
        assert(frozen->compareOriginalInput(1u, incoming(1u, &desc)));
        raw[2] = upload.get();
        twin.reset();
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
                const auto saved = raw[0];
                raw[0] = raw[1];
                const auto* mismatch = frozen->compareOriginalInput(1u, incoming(1u, &desc));
                raw[0] = saved;
                if (!mismatch)
                    return "TEST-SWAPPED-SOURCE-ACCEPTED";
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    if (!frozen->executionDiagnostic(backend) || frozen->nativeExecutionProven()
                        || upload->nativeExecutionProven())
                        return "TEST-EXECUTION-OPEN";
                return static_cast<const char*>(nullptr);
            }));
        std::weak_ptr<const Upload> observed = upload;
        retained = {};
        upload.reset();
        source.reset();
        destination.reset();
        scalars.reset();
        store.reset();
        usage.reset();
        sources.reset();
        desc.label.clear();
        assert(!observed.expired() && !Inputs::validateOriginal(frozen)
            && observed.lock()->label() == "buf");
        frozen.reset();
        assert(!observed.expired() && !compiled->validate());
        compiled.reset();
        assert(observed.expired());
        observed.reset();
        assert(!sourceQuota->liveBytes() && !graphQuota->liveBytes());
        std::printf(
            "BINDLESS_RHI_GENERIC_ORIGINAL_BUFFER_UPLOAD_INPUTS_OK %llu %llu %llu uploads2 declared-bytes16 owner11 exact-source-destination Graph-Source-quotas scalar-label-refs independent-bounds cold-pressure caller-drop hot-allocation0 staging-unproven gates4-closed\n",
            static_cast<unsigned long long>(uploadSourcePeak),
            static_cast<unsigned long long>(uploadGraphPeak),
            static_cast<unsigned long long>(inputPeak));
    }

private:
    template <class Commands, class References>
    static void verifySources(const rhi::UploadBufferDesc& desc,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& source,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& destination,
        const std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>& upload,
        const std::array<rhi::OriginalCommandResourceSources::Source, 3>& retained,
        const rhi::OriginalCommandAdmissionBudget& budget,
        const Commands& commands,
        const References& refs,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota)
    {
        using Upload = rhi::OriginalCommandBufferUploadSource;
        using Sources = rhi::OriginalCommandResourceSources;
        const char* diagnostic = nullptr;
        const rhi::OriginalCommandBufferUploadSourceBudget local{
            1u, true, 6u, 32u, metadataBytes, metadataBytes};
        const auto factory =
            [&](const auto& input, const auto& original, const auto& target, const auto& graph) {
                return Upload::prepareOriginal(
                    input, original, target, local, sourceQuota, graph, diagnostic);
            };
        auto forged = std::shared_ptr<const Upload>(sourceQuota, upload.get());
        assert(Upload::validateOriginal(forged));
        auto forgedSource =
            std::shared_ptr<const rhi::OriginalCommandBufferLease>(sourceQuota, source.get());
        assert(!factory(desc, forgedSource, destination, graphQuota));
        for (unsigned missing = 0u; missing < 2u; ++missing) {
            auto entries = retained;
            entries[missing] = upload;
            auto bounded = budget;
            bounded.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Upload)] =
                2u;
            const std::array<Sources::Source, 2> pair{retained[1u - missing], upload};
            assert(!Sources::prepareOriginal(
                pair.data(), pair.size(), bounded, sourceQuota, diagnostic));
            assert(std::string_view(diagnostic)
                == (missing == 0u ? "RHI-ORIGINAL-SOURCES-UPLOAD-SOURCE"
                                  : "RHI-ORIGINAL-SOURCES-UPLOAD-DESTINATION"));
        }
        auto changed = desc;
        changed.destinationResource = source->resource();
        assert(!factory(changed, source, source, graphQuota));
        const auto reject = [&](auto mutate, const char* expected) {
            auto input = desc;
            mutate(input);
            assert(!factory(input, source, destination, graphQuota)
                && std::string_view(diagnostic) == expected);
        };
        reject(
            [](auto& d) {
                d.sizeBytes = 0u;
            },
            "RHI-ORIGINAL-BUFFER-UPLOAD-RANGE");
        reject(
            [](auto& d) {
                d.sizeBytes = 9u;
            },
            "RHI-ORIGINAL-BUFFER-UPLOAD-RANGE");
        reject(
            [](auto& d) {
                d.sizeBytes = UINT64_MAX;
            },
            "RHI-ORIGINAL-BUFFER-UPLOAD-RANGE");
        reject(
            [](auto& d) {
                d.sourceUploadBuffer = {};
            },
            "RHI-ORIGINAL-BUFFER-UPLOAD-RESOURCE");
        reject(
            [](auto& d) {
                d.backendOwnedStaging = false;
            },
            "RHI-ORIGINAL-BUFFER-UPLOAD-INTENT");
        reject(
            [](auto& d) {
                d.stagingIntent = rhi::StagingIntent::Readback;
            },
            "RHI-ORIGINAL-BUFFER-UPLOAD-INTENT");
        auto foreign = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto foreignSource = factory(desc, source, destination, foreign);
        assert(foreignSource);
        auto foreignEntries = retained;
        foreignEntries[2] = foreignSource;
        auto table = Sources::prepareOriginal(
            foreignEntries.data(), foreignEntries.size(), budget, sourceQuota, diagnostic);
        assert(table);
        assert(!rhi::OriginalCommandUsage::prepareOriginal(table,
                   commands.data(),
                   commands.size(),
                   refs.data(),
                   refs.size(),
                   graphQuota,
                   diagnostic)
            && std::string_view(diagnostic) == "RHI-ORIGINAL-USAGE-UPLOAD-GRAPH-QUOTA");
        assert(!factory(desc, source, destination, sourceQuota));
        for (const auto& quota : std::array{sourceQuota, graphQuota}) {
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1u);
            assert(Upload::validateOriginal(upload));
            quota->setLimitBytes(limit);
        }
        auto smaller = local;
        --smaller.maxRetainedOwners;
        assert(!Upload::prepareOriginal(
            desc, source, destination, smaller, sourceQuota, graphQuota, diagnostic));
        auto& layout = const_cast<rhi::BufferDesc&>(source->originalObject()->desc());
        const auto usage = layout.usage;
        layout.usage = rhi::ResourceUsage::None;
        assert(Upload::validateOriginal(upload));
        layout.usage = usage;
    }
};
} // namespace arp::test
