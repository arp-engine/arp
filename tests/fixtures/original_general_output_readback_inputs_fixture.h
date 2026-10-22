#pragma once
#include "core/image/strided_image_statistics.h"
#include "core/math/half_float.h"
#include "original_command_readback_result_contract_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_general_finite_post_suppression_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_canonical_cold_command.h"
#include "rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_joins.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_canonical_upload_joins_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_native_map_execution_probe_fixture.h"

namespace arp::test {
// Standalone original output readback. Every GPU copy/map is issued by the
// canonical production path; this fixture only inspects its published bytes.
class OriginalGeneralOutputReadbackInputsFixture final {
    using Join = rhi::OriginalCommandReadbackJoinContract;
    using Sources = rhi::OriginalCommandResourceSources;
    using Inputs = rhi::OriginalCommandInputs;
    using Readback = rhi::OriginalCommandTextureReadbackSource;
    using Snapshot = rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot;
    static constexpr std::uint64_t bytes = 65536u;
    // Complete downstream demand before Source publication. CPU Sources48 and
    // the upstream General issuer242 remain separate, unchanged inventories.
    static constexpr std::uint32_t downstreamOwners =
        rhi::OriginalCommandNativeColdCommandContract::additionalOwnerCount
        + rhi::OriginalCommandCallbackCustodyContract::additionalOwnerCount
        + rhi::OriginalCommandReadbackMapContract::participantsPerMap
        + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 1u
        + rhi::OriginalCommandCopyPassLoweringContract::additionalOwnerCount
        + rhi::OriginalCommandIssuerPreparationContract::additionalOwnerCount;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "general output readback input: %s\n", id);
        assert(!id);
    }

public:
    enum class MapTerminal {
        None,
        Cancel,
        NativeError,
        ReadError,
        ScopeNativeError,
        ThrowBefore,
        ThrowAfter,
        ScopeRegistration,
        UnmapThrow,
        PendingLoss,
        ReverseScopes,
        PublishedLoss,
        Drop,
        PostSuppressionPublication
    };

    static MapTerminal selectedMapTerminal() noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
        if (EM_ASM_INT({
                // clang-format off
                const p = new URLSearchParams(location.search);
                return p.has("general-finite-lifecycle") &&
                       p.get("general-post-suppression-failure") === "publication";
                // clang-format on
            }))
            return MapTerminal::PostSuppressionPublication;
#endif
        const auto choice = EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                const graphics = p.get('general-graphics-output-terminal');
                if (graphics !== null &&
                    (!p.has('general-graphics-output-readback') || p.has('general-map-terminal')))
                    return -1;
                const value = graphics === null ? p.get('general-map-terminal') : graphics;
                if (value === null)
                    return 0;
                if (value === "drop" && graphics === null)
                    return -1;
                const names = ([
                    "cancel", "native-error", "read-error", "scope-native-error", "throw-before", "throw-after",
                    "scope-registration", "unmap-throw", "pending-loss", "reverse-scopes", "published-loss",
                    "drop"
                ]);
                const index = names.indexOf(value);
                return index < 0 ? names.length + 1 : index + 1;
            // clang-format on
        });
        assert(choice >= 0 && choice <= 12);
        return static_cast<MapTerminal>(choice);
#else
        return MapTerminal::None;
#endif
    }

    static bool mapUsesPhysicalLoss(MapTerminal terminal) noexcept
    {
        return (terminal >= MapTerminal::ThrowBefore && terminal <= MapTerminal::PendingLoss)
            || terminal == MapTerminal::PublishedLoss;
    }

    static bool mapCompletes(MapTerminal terminal) noexcept
    {
        return terminal == MapTerminal::None || terminal == MapTerminal::ReverseScopes
            || terminal == MapTerminal::PublishedLoss;
    }

    static const char* mapTerminalName(MapTerminal terminal) noexcept
    {
        static constexpr std::array names{"none",
            "cancel",
            "native-error",
            "read-error",
            "scope-native-error",
            "throw-before",
            "throw-after",
            "scope-registration",
            "unmap-throw",
            "pending-loss",
            "reverse-scopes",
            "published-loss",
            "drop",
            "post-suppression-publication"};
        return names[static_cast<std::size_t>(terminal)];
    }

    static std::uint64_t verifyPrepared(rhi::GpuDevice& device,
        const std::shared_ptr<const Snapshot>& snapshot,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
        const std::array<std::uint16_t, 4u>& expected)
    {
        require(Snapshot::validateCompletedOriginal(snapshot));
        const auto allocationBegin = core::defaultCpuAllocator().stats().allocationCount;
        const auto& output = snapshot->originalOutput();
        const auto sourceQuota = output->originalMetadataQuota();
        const auto sourceBaseline = sourceQuota->liveBytes(),
                   resourceBaseline = resourceQuota->liveBytes();
        const std::array quotas{core::makeShared<core::CpuMemoryQuota>(bytes),
            core::makeShared<core::CpuMemoryQuota>(bytes),
            core::makeShared<core::CpuMemoryQuota>(bytes)};
        const auto& graphQuota = quotas[0];
        const char* id = nullptr;
        {
            const auto observation = output->originalObject()->observeOriginalCommittedState(
                snapshot->originalBudget().range);
            const auto outputVersion = snapshot->originalBudget().contractVersion;
            const bool graphics = outputVersion == 3u;
            assert(observation
                && observation->state()
                    == (graphics ? rhi::ResourceState::ColorAttachment
                                 : rhi::ResourceState::ShaderWrite));
            core::UniquePtr<rhi::GpuBuffer> buffer;
            {
                core::CpuMemoryQuotaScope scope(resourceQuota);
                rhi::BufferDesc desc;
                desc.sizeBytes = rhi::kTextureUploadRowPitchAlignment;
                desc.usage = rhi::ResourceUsage::CopyDst;
                desc.memory = rhi::MemoryUsage::GpuToCpu;
                desc.initialState = rhi::ResourceState::CopyDst;
                buffer = device.createBuffer(desc);
                if (!buffer) {
                    const auto diagnostic = device.lastOriginalResourceRegistrationDiagnostic();
                    std::fprintf(stderr,
                        "general output readback buffer: %.*s\n",
                        int(diagnostic.size()),
                        diagnostic.data());
                }
                assert(buffer);
            }
            auto destination = rhi::OriginalCommandBufferLease::prepareOriginal(
                buffer, {1u, true, 2u, bytes}, sourceQuota, id);
            require(id);
            assert(destination);
            rhi::ReadbackTextureDesc desc;
            desc.sourceResource = output->resource();
            desc.destinationReadbackBuffer = destination->resource();
            desc.sizeBytes = rhi::kTextureUploadRowPitchAlignment;
            desc.extent = output->originalObject()->desc().extent;
            desc.sourceSubresources = snapshot->originalBudget().range;
            desc.bytesPerRow = rhi::kTextureUploadRowPitchAlignment;
            desc.rowsPerImage = 1u;
            const auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
                desc, {1u, true, 1u, 0u, bytes, bytes}, graphQuota, graphQuota, id);
            require(id);
            assert(input);
            const auto layout = rhi::TextureReadbackLayout::prepareOriginal(input,
                output->originalObject()->desc(),
                {1u, true, 1u, desc.sizeBytes, bytes},
                graphQuota,
                id);
            require(id);
            assert(layout);
            const auto request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
                layout, {17u, 23u, 29u}, {1u, true, 1u, 0u, desc.sizeBytes, bytes}, graphQuota, id);
            require(id);
            assert(request);
            const auto lease = rhi::TextureReadbackSourceLease::prepareRetainedOriginal(
                output, request, {2u, true, 3u, 1u, 0u, bytes}, sourceQuota, id);
            require(id);
            assert(lease);
            const auto texture = rhi::OriginalCommandReadbackTextureSource::prepareOriginal(lease,
                {2u, true, 8u, 1u, 0u, desc.sizeBytes, bytes, bytes},
                sourceQuota,
                graphQuota,
                id);
            require(id);
            assert(texture);
            const auto readback = Readback::prepareOriginal(texture,
                destination,
                {2u, true, 11u, 1u, 0u, desc.sizeBytes, bytes, bytes},
                sourceQuota,
                id);
            require(id);
            assert(readback);
            require(Join::validateGeneralOutputSource(readback, outputVersion));
            const std::array<Sources::Source, 3u> entries{output, destination, readback};
            rhi::OriginalCommandAdmissionBudget authored;
            authored.contractVersion = 1u;
            authored.prototypeOptIn = true;
            authored.maxCpuBytes.fill(bytes);
            authored.maxCommands = 5u;
            authored.maxReferences = 3u;
            authored.maxInputDescriptors = 2u;
            authored.maxGraphOwners = 48u;
            authored.maxStateTransactions = 1u;
            authored.maxStateObservations = 3u;
            authored.maxTextureRanges = 2u;
            authored.maxMipMappings = 1u;
            authored.maxUnpublishedResultBytes = desc.sizeBytes;
            // Separate downstream cold demand, authored before Source publication:
            // two endpoints plus one encoder/finished-command, one submission
            // and the existing eight callbacks plus one map/four participants.
            authored.maxNativePayloadObjects = 4u;
            authored.maxNativeStagingBytes = desc.sizeBytes;
            authored.maxPendingSubmissions = 1u;
            authored.maxCallbackTickets = 12u;
            authored.maxCallbackRegistrations = 12u;
            for (const auto domain : {rhi::CommandReferenceDomain::Texture,
                     rhi::CommandReferenceDomain::Buffer,
                     rhi::CommandReferenceDomain::Readback})
                authored.maxSourceOwners[static_cast<std::size_t>(domain)] = 1u;
            const rhi::OriginalRetainedReadbackCorrespondence correspondence{1u, true, 1u, 7u};
            const auto sources = Sources::prepareOriginal(
                entries.data(), entries.size(), authored, sourceQuota, id, correspondence);
            require(id);
            assert(sources);
            using K = rhi::CommandUsageKind;
            using Scalar = rhi::CommandScalarArguments;
            const std::array<rhi::CommandUsage, 5u> commands{{{K::TextureBarrier, 0u, 1u},
                {K::BeginTransferPass, 1u, 0u},
                {K::TextureReadback, 1u, 2u},
                {K::EndTransferPass, 3u, 0u},
                {K::Finish, 3u, 0u}}};
            const std::array<rhi::OriginalCommandUsage::Reference, 3u> refs{{{0u}, {0u}, {2u}}};
            const std::array<rhi::CommandVariableArguments::Input, 5u> fields{
                {{*Scalar::capture(K::TextureBarrier, Scalar::None{})},
                    {*Scalar::capture(K::BeginTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::TextureReadback, Scalar::None{})},
                    {*Scalar::capture(K::EndTransferPass, Scalar::None{})},
                    {*Scalar::capture(K::Finish, Scalar::None{})}}};
            rhi::ResourceBarrierDesc barrier;
            barrier.resource = output->resource();
            barrier.textureSubresources = desc.sourceSubresources;
            barrier.before =
                graphics ? rhi::ResourceState::ColorAttachment : rhi::ResourceState::ShaderWrite;
            barrier.after = rhi::ResourceState::CopySrc;
            barrier.sourceStages = graphics ? rhi::PipelineStage::ColorAttachmentOutput
                                            : rhi::PipelineStage::ComputeShader;
            barrier.sourceAccess = graphics ? rhi::ResourceAccess::ColorAttachmentRead
                    | rhi::ResourceAccess::ColorAttachmentWrite
                                            : rhi::ResourceAccess::ShaderWrite;
            barrier.destinationStages = rhi::PipelineStage::Transfer;
            barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
            require(Join::validateGeneralOutputDependency(
                barrier, observation->state(), observation->range(), outputVersion));
            const auto transition =
                rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier);
            assert(transition);
            const auto arguments =
                rhi::TextureReadbackOriginalInputArguments::captureOriginal(input);
            assert(arguments);
            const std::array<Inputs::OriginalDescriptor, 2u> descriptors{
                {{0u, *transition}, {2u, *arguments}}};
            rhi::OriginalCommandInputsBudget inputBudget;
            inputBudget.retainedReadback = correspondence;
            const auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
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
            require(id);
            assert(inputs);
            // One retained attachment control/cell, one ReadbackRead step.
            // The five commands are traversed once globally and once per cell;
            // two texture observations contribute ten visits, one duplicate
            // scan and one resolved readback cell add two more.
            const rhi::OriginalTextureContentPlanBudget content{
                3u, true, 1u, 0u, 1u, 1u, 10u, 12u, 1u};
            const auto root = WebGpuCanonicalUploadJoinsFixture::prepareRoot(inputs,
                device,
                quotas[1],
                quotas[2],
                false,
                2u,
                3u,
                2u,
                {1u, true, 5u, 0u, 3u, 3u},
                id,
                graphics ? &content : nullptr);
            require(id);
            assert(root);
            if (graphics) {
                const auto inspected = root->preflightOriginalContentCells(content);
                require(inspected.diagnostic);
                assert(inspected.controls == 1u && inspected.cells == 1u && inspected.steps == 1u
                    && inspected.readbackCommands == 1u && inspected.commandVisits == 10u
                    && inspected.projectionVisits == 12u);
                auto missing = content;
                missing.maxReadbackCommands = 0u;
                assert(root->preflightOriginalContentCells(missing).diagnostic);
                missing = content;
                --missing.maxProjectionVisits;
                assert(root->preflightOriginalContentCells(missing).diagnostic);
                missing = content;
                --missing.maxCommandVisits;
                assert(root->preflightOriginalContentCells(missing).diagnostic);
            }
            Join::Budget b;
            b.contractVersion = 4u;
            b.prototypeOptIn = true;
            b.generalOutput = {snapshot->originalBudget().contractVersion, true};
            b.maxReadbacks = b.maxNativeSources = b.maxNativeDestinations = 1u;
            b.maxRetainedOwners = 48u;
            b.maxSourceResourceBytes = 8u;
            b.maxStagingResourceBytes = desc.sizeBytes;
            b.maxCpuMetadataBytes = bytes;
            b.stream = {1u,
                true,
                5u,
                2u,
                3u,
                0u,
                0u,
                0u,
                1u,
                0u,
                0u,
                0u,
                0u,
                desc.sizeBytes,
                inputs->retainedOwnerCount() + 1u,
                bytes};
            b.result = {1u, true, 1u, 1u, 1u, desc.sizeBytes, desc.sizeBytes, bytes};
            OriginalCommandReadbackResultContractFixture::verifyGeneralOutputJoin(b);
            const auto inspected = Join::inspectGeneralOutput(root, b);
            require(inspected.diagnostic);
            assert(inspected.required.readbackCommands == 1u
                && inspected.required.nativeCopyCommands == 1u
                && inspected.required.resultDeclarations == 1u
                && inspected.required.uniqueResultBytes == desc.sizeBytes);
            for (unsigned field = 0u; field < 4u; ++field) {
                auto wrong = b;
                if (field == 0u)
                    --wrong.result.maxUniqueResultBytes;
                if (field == 1u)
                    --wrong.stream.maxReferences;
                if (field == 2u)
                    --wrong.stream.maxDescriptors;
                if (field == 3u)
                    --wrong.stream.maxTextureReadbackMips;
                const auto refused = Join::inspectGeneralOutput(root, wrong);
                assert(refused.diagnostic && !refused.required.readbackCommands
                    && !refused.required.uniqueResultBytes);
            }
            auto forged =
                std::shared_ptr<rhi::OriginalCommandStateTransaction>(graphQuota, root.get());
            assert(Join::inspectGeneralOutput(forged, b).diagnostic);
            forged.reset();
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sourceQuota, graphQuota, quotas[1], quotas[2]},
                [&]() noexcept -> const char* {
                    if (const auto* failure = Snapshot::validateCompletedOriginal(snapshot))
                        return failure;
                    if (const auto* failure =
                            output->originalObject()->validateOriginalCommittedState(*observation))
                        return failure;
                    return Join::inspectGeneralOutput(root, b).diagnostic;
                }));
            assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
                && root->journalEmpty());
            require(Snapshot::validateCompletedOriginal(snapshot));
            std::printf(
                "BINDLESS_RHI_GENERAL_OUTPUT_READBACK_INPUTS4_OK original-completed-output revision%llu retained-Source2 correspondence1 %s-CopySrc planned-only copies1 padded%llu exact-less forged-refused hot0 native-copy-map-publication-closed\n",
                static_cast<unsigned long long>(observation->revision()),
                graphics ? "ColorAttachment" : "ShaderWrite",
                static_cast<unsigned long long>(desc.sizeBytes));
            // Cold seal retirement abandons this root. Complete all Prepared
            // probes first; never reuse that root or its frozen graph afterward.
            verifyNativeJoin(
                device, snapshot, root, readback, b, quotas[2], resourceQuota, expected);
        }
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        assert(sourceQuota->liveBytes() == sourceBaseline
            && resourceQuota->liveBytes() == resourceBaseline);
        return core::defaultCpuAllocator().stats().allocationCount - allocationBegin;
    }

private:
    static void verifyNativeJoin(rhi::GpuDevice& device,
        const std::shared_ptr<const Snapshot>& snapshot,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const Readback>& readback,
        Join::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
        const std::array<std::uint16_t, 4u>& expected)
    {
        using NativeJoin = rhi_webgpu::WebGpuCanonicalReadbackJoins;
        const char* id = nullptr;
        const auto& output = snapshot->originalOutput();
        const auto& destination = readback->originalDestinationLease();
        rhi_webgpu::WebGpuTransferResourceObservation nativeSource, nativeDestination;
        require(rhi_webgpu::observeOriginalWebGpuResource(*output->originalObject(), nativeSource));
        require(rhi_webgpu::observeOriginalWebGpuResource(
            *destination->originalObject(), nativeDestination));
        if (snapshot->originalBudget().contractVersion == 3u) {
            const auto& mip =
                readback->originalSource()->originalRequest()->originalLayout()->mipCopies()[0u];
            rhi::TextureToBufferCopyDesc copy;
            copy.sourceTexture = nativeSource.id;
            copy.destinationBuffer = nativeDestination.id;
            copy.textureSubresources = mip.range;
            copy.extent = mip.extent;
            copy.bytesPerRow = mip.bytesPerRow;
            copy.rowsPerImage = mip.rowsPerImage;
            copy.bufferOffsetBytes = mip.bufferOffsetBytes;
            assert(rhi_webgpu::WebGpuTransferCopyValidation::validate(
                nativeSource, nativeDestination, copy));
            require(rhi_webgpu::WebGpuTransferCopyValidation::inspectTextureBufferLayout(
                nativeSource, nativeDestination, copy));
        }
        // Existing join charge plus the complete upstream snapshot graph and
        // its captured observation ticket; separate from original CPU48/native242.
        budget.maxRetainedOwners = root->retainedOwnerCount() + 1u + 1u + 1u
            + nativeSource.ownerCount() + nativeDestination.ownerCount()
            + snapshot->retainedOwnerCount() + 1u;
        budget.maxSourceResourceBytes = nativeSource.allocationBytes;
        budget.maxStagingResourceBytes = nativeDestination.allocationBytes;
        std::array<NativeJoin::Input, 1u> entries{{{2u, readback, snapshot}}};
        const auto make = [&] {
            return NativeJoin::prepareOriginalGeneralOutput(
                root, device, entries.data(), entries.size(), budget, id);
        };
        {
            const auto admitted = make();
            require(id);
            assert(admitted);
        }
        CpuQuotaFactoryFixture pressure(recordingQuota);
        const auto peak = pressure.measureRetained(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(
                recordingQuota, recordingQuota->limitBytes() - recordingQuota->liveBytes());
            assert(full.charged() && !make());
        }
        const auto validBudget = budget;
        for (unsigned field = 0u; field < 4u; ++field) {
            budget = validBudget;
            if (field == 0u)
                --budget.maxRetainedOwners;
            if (field == 1u)
                --budget.maxSourceResourceBytes;
            if (field == 2u)
                --budget.maxStagingResourceBytes;
            if (field == 3u)
                budget.generalOutput.contractVersion =
                    validBudget.generalOutput.contractVersion == 1u ? 2u : 1u;
            assert(!make());
        }
        budget = validBudget;
        assert(
            !NativeJoin::prepareOriginal(root, device, entries.data(), entries.size(), budget, id));
        assert(!NativeJoin::prepareOriginalProgram(
            root, {}, device, entries.data(), entries.size(), budget, id));
        entries[0].generalOutput.reset();
        assert(!make());
        entries[0].generalOutput = snapshot;
        entries[0].generalOutput = std::shared_ptr<const Snapshot>(recordingQuota, snapshot.get());
        assert(!make());
        entries[0].generalOutput = snapshot;
        entries[0].source = std::shared_ptr<const Readback>(recordingQuota, readback.get());
        assert(!make());
        entries[0].source = readback;
        entries[0].commandIndex = 0u;
        assert(!make());
        entries[0].commandIndex = 2u;
        auto joined = make();
        require(id);
        assert(joined);
        assert(joined->retainedOwnerCount() == budget.maxRetainedOwners
            && joined->readbackCount() == 1u && joined->projectedCopyCount() == 1u
            && joined->sourceResourceBytes() == budget.maxSourceResourceBytes
            && joined->stagingResourceBytes() == budget.maxStagingResourceBytes
            && !joined->nativeExecutionProven() && joined->executionDiagnostic());
        auto forged = std::shared_ptr<const NativeJoin>(recordingQuota, joined.get());
        assert(NativeJoin::validateOriginal(forged));
        forged.reset();
#if defined(__EMSCRIPTEN__)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recordingQuota, output->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    const auto frozenSize = nativeDestination.buffer->sizeBytes;
                    --nativeDestination.buffer->sizeBytes;
                    assert(joined->validate());
                    nativeDestination.buffer->sizeBytes = frozenSize;
                    const auto savedDevice = nativeSource.texture->ownerDevice;
                    nativeSource.texture->ownerDevice = nullptr;
                    assert(joined->validate());
                    nativeSource.texture->ownerDevice = savedDevice;
                    return NativeJoin::validateOriginal(joined);
                }));
        });
#endif
        const auto& downstreamInputs = root->originalGraph()->originalInputs();
        const auto* dependency =
            std::get_if<rhi::ResourceDependencyArguments>(downstreamInputs->descriptorAt(0u));
        assert(dependency);
        auto& mutableDependency = const_cast<rhi::ResourceDependencyArguments&>(*dependency);
        const auto original = *dependency;
        rhi::ResourceBarrierDesc wrong;
        assert(original.write(rhi::CommandUsageKind::TextureBarrier, wrong));
        wrong.sourceStages = rhi::PipelineStage::Transfer;
        mutableDependency = *rhi::ResourceDependencyArguments::capture(
            rhi::CommandUsageKind::TextureBarrier, wrong);
        assert(joined->validate());
        mutableDependency = original;
        require(joined->validate());
        verifyCold(device, snapshot, root, joined, budget, recordingQuota, resourceQuota, expected);
        std::weak_ptr<const NativeJoin> kept = joined;
        joined.reset();
        assert(kept.expired());
        kept.reset();
        std::printf(
            "BINDLESS_RHI_GENERAL_OUTPUT_NATIVE_JOIN4_OK completed-original-association frozen-revision1 retained-Source2 native-source%llu staging%llu owners%u metadata%llu exact-less-exhausted forged-missing-scope-native-mutation-refused hot0-query0 old-execution-closed\n",
            static_cast<unsigned long long>(budget.maxSourceResourceBytes),
            static_cast<unsigned long long>(budget.maxStagingResourceBytes),
            budget.maxRetainedOwners,
            static_cast<unsigned long long>(peak));
    }

    static void verifyCold(rhi::GpuDevice& device,
        const std::shared_ptr<const Snapshot>& snapshot,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackJoins>& joined,
        const Join::Budget& joinBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
        const std::array<std::uint16_t, 4u>& expected)
    {
        using Cold = rhi_webgpu::WebGpuCanonicalColdAdmission;
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        using Seal = rhi::OriginalCommandRecordingSeal;
        using Reservation = rhi::OriginalCommandLifecycleReservation;
        const char* id = nullptr;
        const auto loss = device.observeOriginalDeviceNativeLoss(id);
        require(id);
        assert(loss);
        // Each accepted endpoint has at most three already-authored native
        // owners; Closure's existing union deduplicates common device owners.
        const rhi::OriginalCommandNativeClosureBudget closureBudget{1u,
            true,
            2u,
            2u,
            root->retainedOwnerCount() + 2u + 2u * 3u,
            joinBudget.maxSourceResourceBytes + joinBudget.maxStagingResourceBytes,
            bytes};
        const bool graphics = snapshot->originalBudget().contractVersion == 3u;
        if (graphics) {
            assert(!Closure::prepareOriginalLossBound(root, device, closureBudget, *loss, id));
            assert(!Closure::prepareOriginalAttachmentReadbackLossBound(
                root, {}, device, closureBudget, *loss, id));
            auto forged = std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackJoins>(
                recordingQuota, joined.get());
            assert(!Closure::prepareOriginalAttachmentReadbackLossBound(
                root, forged, device, closureBudget, *loss, id));
            for (unsigned field = 0u; field < 3u; ++field) {
                auto wrong = closureBudget;
                if (field == 0u)
                    wrong.prototypeOptIn = false;
                if (field == 1u)
                    wrong.contractVersion = 4u;
                if (field == 2u)
                    wrong.maxResourceBindings = 1u;
                assert(!Closure::prepareOriginalAttachmentReadbackLossBound(
                    root, joined, device, wrong, *loss, id));
            }
        }
        const auto closure = graphics
            ? Closure::prepareOriginalAttachmentReadbackLossBound(
                  root, joined, device, closureBudget, *loss, id)
            : Closure::prepareOriginalLossBound(root, device, closureBudget, *loss, id);
        require(id);
        assert(closure && closure->hasAttachmentReadbackProfile() == graphics);
        Cold::Budget b;
        b.contractVersion = 5u;
        b.prototypeOptIn = true;
        b.generalOutput = {snapshot->originalBudget().contractVersion, true};
        b.maxOriginalCommands = 5u;
        b.maxEncoders = b.maxCommandBuffers = 1u;
        const auto base = Cold::selectedRequirements();
        b.maxRecordingScopeCallbacks = base.recordingScopeCallbacks;
        b.maxSubmissionScopeCallbacks = base.submissionScopeCallbacks;
        b.maxQueueCallbacks = base.queueCallbacks;
        b.maxRuntimeLossCallbacks = base.runtimeLossCallbacks;
        b.maxRuntimeLossRegistrations = base.runtimeLossRegistrations;
        b.maxRetainedOwners = closure->retainedOwnerCount() + Seal::additionalOwnerCount
            + Reservation::additionalOwnerCount + 1u + joined->retainedOwnerCount()
            - root->retainedOwnerCount() + downstreamOwners;
        b.maxCpuMetadataBytes = bytes;
        b.readback = joinBudget.result;
        b.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
        Cold::Requirements required;
        require(Cold::deriveRequirements(root, b, required));
        assert(required.contractVersion == 5u && required.readback.readbackCommands == 1u
            && required.readback.nativeCopyCommands == 1u
            && required.readback.uniqueResultBytes == 256u && required.readbackMap.maps == 1u
            && required.capacity.callbackTickets == 12u
            && required.capacity.callbackRegistrations == 12u);
        const std::array quotas{core::makeShared<core::CpuMemoryQuota>(bytes),
            core::makeShared<core::CpuMemoryQuota>(bytes)};
        {
            auto seal = Seal::prepareOriginal(root,
                device,
                {snapshot->originalBudget().contractVersion == 3u ? 2u : 1u,
                    true,
                    1u,
                    12u,
                    12u,
                    b.maxRetainedOwners,
                    b.maxRetainedOwners,
                    b.maxRetainedOwners,
                    bytes,
                    bytes,
                    bytes},
                quotas[0],
                quotas[1],
                id);
            require(id);
            assert(seal);
            auto reservation = Reservation::prepareOriginal(seal, required.capacity, id);
            require(id);
            assert(reservation);
            const auto make = [&] {
                return Cold::prepareOriginalGeneralOutput(closure, reservation, b, id, joined);
            };
            {
                const auto admitted = make();
                require(id);
                assert(admitted);
            }
            assert(!Cold::prepareOriginal(closure, reservation, b, id, {}, joined));
            const auto valid = b;
            for (unsigned field = 0u; field < 6u; ++field) {
                b = valid;
                if (field == 0u)
                    b.generalOutput = {};
                if (field == 1u)
                    b.generalOutput.prototypeOptIn = false;
                if (field == 2u)
                    b.generalOutput.contractVersion =
                        valid.generalOutput.contractVersion == 1u ? 2u : 1u;
                if (field == 3u)
                    b.contractVersion = 4u;
                if (field == 4u)
                    b.maxRetainedOwners -= downstreamOwners + 1u;
                if (field == 5u)
                    --b.readbackMap.maxTotalCallbackTickets;
                assert(!make());
            }
            b = valid;
            assert(!Cold::prepareOriginalGeneralOutput(closure, reservation, b, id, {}));
            CpuQuotaFactoryFixture pressure(recordingQuota);
            const auto peak = pressure.measureRetained(make);
            pressure.verify(peak, make);
            {
                core::CpuMemoryQuotaLease full(recordingQuota, bytes - recordingQuota->liveBytes());
                assert(full.charged() && !make());
            }
            auto admitted = make();
            require(id);
            assert(admitted
                && admitted->retainedOwnerCount() + downstreamOwners == b.maxRetainedOwners);
            auto forged = std::shared_ptr<const Cold>(quotas[1], admitted.get());
            assert(Cold::validateOriginal(forged));
            forged.reset();
            const auto& upstream = snapshot->originalRoot();
            const auto& graph = upstream->originalGraph();
            const auto& inputs = graph->originalInputs();
            const auto& source = inputs->originalScalars()->originalUsage()->originalSources();
            for (const auto& q : std::array{snapshot->originalMetadataQuota(),
                     upstream->originalMetadataQuota(),
                     graph->originalMetadataQuota(),
                     inputs->originalMetadataQuota(),
                     source->originalMetadataQuota(),
                     resourceQuota})
                assert(snapshot->overlapsCpuOwner(q) && joined->overlapsCpuOwner(q));
            assert(!joined->overlapsCpuOwner(quotas[0]) && !joined->overlapsCpuOwner(quotas[1]));
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recordingQuota, quotas[0], quotas[1]}, [&]() noexcept -> const char* {
                    return Cold::validateOriginal(admitted);
                }));
            rhi::OriginalCommandNativeColdCommandBudget command{
                1u, true, 1u, b.maxRetainedOwners, bytes};
            assert(!rhi_webgpu::WebGpuCanonicalColdCommand::prepareOriginal(admitted, command, id));
            assert(id && std::string_view(id) == "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
            command.generalOutput = b.generalOutput;
            for (const rhi::OriginalGeneralOutputIntent bad :
                {rhi::OriginalGeneralOutputIntent{0u, true},
                    rhi::OriginalGeneralOutputIntent{b.generalOutput.contractVersion, false},
                    rhi::OriginalGeneralOutputIntent{
                        b.generalOutput.contractVersion == 1u ? 2u : 1u, true}}) {
                command.generalOutput = bad;
                assert(
                    !rhi_webgpu::WebGpuCanonicalColdCommand::prepareOriginal(admitted, command, id)
                    && id);
            }
            command.generalOutput = b.generalOutput;
            std::puts(
                "BINDLESS_RHI_GENERAL_OUTPUT_PAIRING1_NATIVE_OK original-Cold5-Join4 missing-command-profile-refused explicit-original-execution");
            require(Snapshot::validateCompletedOriginal(snapshot));
            std::printf(
                "BINDLESS_RHI_GENERAL_OUTPUT_COLD1_OK independent-Cold5-Join4 root-original owners%u metadata%llu maps1 callbacks12 original-upstream-quota-conflicts exact-less-exhausted forged-legacy-refused hot0 execution-closed\n",
                admitted->retainedOwnerCount(),
                static_cast<unsigned long long>(peak));
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            verifyExecution(device, snapshot, root, admitted, command, expected);
#endif
        }
        for (const auto& quota : quotas)
            assert(!quota->liveBytes());
        const auto& source = root->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage()
                                 ->originalSources();
        const auto terminal = selectedMapTerminal();
        assert(root->status()
                == (mapCompletes(terminal)
                        ? rhi::OriginalCommandStateTransaction::Status::Committed
                        : rhi::OriginalCommandStateTransaction::Status::Invalidated)
            && root->journalEmpty());
        assert(!source->originalReservedSubmissionCount()
            && !source->originalReservedCallbackTicketCount()
            && !source->originalReservedCallbackRegistrationCount());
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyExecution(rhi::GpuDevice& device,
        const std::shared_ptr<const Snapshot>& upstream,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root,
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalColdAdmission>& admitted,
        const rhi::OriginalCommandNativeColdCommandBudget& commandBudget,
        const std::array<std::uint16_t, 4u>& expected)
    {
        using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
        using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;
        using Lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering;
        using Issuer = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        using MapProbe = WebGpuNativeMapExecutionProbeFixture;
        using MapScopes = WebGpuErrorScopeHandoffFixture<1023u>;
        const auto terminal = selectedMapTerminal();
        const bool failedMap = !mapCompletes(terminal);
        const char* id = nullptr;
        const auto& seal = admitted->originalReservation()->originalSeal();
        const auto frozen =
            upstream->originalOutput()->originalObject()->observeOriginalCommittedState(
                upstream->originalBudget().range);
        assert(frozen && Custody::validateGeneralOutputPending(upstream, seal, *frozen));
        const auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            Storage::Budget resultBudget{1u,
                true,
                1u,
                admitted->requirements().readback.uniqueResultBytes,
                bytes,
                admitted->retainedOwnerCount()
                    + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners
                    + 1u};
            auto shortResult = resultBudget;
            --shortResult.maxRetainedOwners;
            assert(!Storage::prepareOriginal(admitted, device, shortResult, resultQuota, id));
            auto results =
                Storage::prepareOriginal(admitted, device, resultBudget, resultQuota, id);
            require(id);
            assert(results && results->snapshots().size() == 1u);
            const auto result = results->snapshots()[0u];
            assert(!result->publishedBytes());
            auto shortCommand = commandBudget;
            shortCommand.maxRetainedOwners = admitted->retainedOwnerCount()
                + rhi::OriginalCommandNativeColdCommandContract::additionalOwnerCount - 1u;
            assert(!Command::prepareOriginal(admitted, shortCommand, id));
            auto command = Command::prepareOriginal(admitted, commandBudget, id);
            require(id);
            assert(command);
            Custody::Budget callback{
                2u, true, 1u, 12u, commandBudget.maxRetainedOwners, bytes, bytes};
            callback.generalOutput = commandBudget.generalOutput;
            const auto callbackOwners = command->retainedOwnerCount()
                + rhi::OriginalCommandCallbackCustodyContract::additionalOwners(callback)
                + results->retainedOwnerCount() - admitted->retainedOwnerCount();
            auto shortCallback = callback;
            shortCallback.maxRetainedOwners = callbackOwners - 1u;
            assert(!Custody::prepareOriginal(
                command, shortCallback, id, results, Custody::PublicationIntent{1u, true}));
            auto custody = Custody::prepareOriginal(
                command, callback, id, results, Custody::PublicationIntent{1u, true});
            require(id);
            assert(custody);
            Lowering::Budget lower;
            lower.contractVersion = 5u;
            lower.prototypeOptIn = true;
            lower.maxRetainedOwners = commandBudget.maxRetainedOwners;
            lower.maxCpuMetadataBytes = bytes;
            lower.readback = admitted->originalBudget().readback;
            lower.readbackMap = admitted->originalBudget().readbackMap;
            lower.generalOutput = commandBudget.generalOutput;
            auto shortLower = lower;
            shortLower.maxRetainedOwners = callbackOwners
                + rhi::OriginalCommandCopyPassLoweringContract::additionalOwnerCount - 1u;
            assert(!Lowering::prepareOriginal(custody, shortLower, id));
            auto lowering = Lowering::prepareOriginal(custody, lower, id);
            require(id);
            assert(lowering && lowering->projectedReadbackCopyCount() == 1u);
            Issuer::Budget issuerBudget{
                1u, true, 1u, 1u, commandBudget.maxRetainedOwners, bytes, bytes};
            issuerBudget.generalOutput = commandBudget.generalOutput;
            auto shortIssuer = issuerBudget;
            --shortIssuer.maxRetainedOwners;
            assert(!Issuer::prepareOriginal(lowering, shortIssuer, id));
            auto issuer = Issuer::prepareOriginal(lowering, issuerBudget, id);
            require(id);
            assert(issuer);
            assert(issuer->retainedOwnerCount() == commandBudget.maxRetainedOwners);
            rhi::OriginalCommandScopedRecordingIntent recording{1u, true};
            recording.generalOutput = commandBudget.generalOutput;
            rhi::OriginalCommandQueueSubmissionIntent submission{1u, true};
            submission.generalOutput = commandBudget.generalOutput;
            const auto hotQuotas = std::array{root->originalMetadataQuota(),
                seal->originalSubmissionQuota(),
                seal->originalCallbackQuota(),
                resultQuota};
            assert(Issuer::recordOriginal(issuer, {1u, true}) && root->journalEmpty());
            assert(Issuer::submitOriginal(issuer, submission) && root->journalEmpty());
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            using RecordingScopes = WebGpuErrorScopeHandoffFixture<1021u>;
            RecordingScopes::holdScopes(true);
#endif
            const auto record = [&] {
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(hotQuotas, [&]() noexcept {
                    return Issuer::recordOriginal(issuer, recording);
                }));
            };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(record);
#else
            record();
#endif
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            RecordingScopes::restoreScopes();
#endif
            assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Pending
                && !root->journalEmpty());
            assert(upstream->originalRoot()->validate()
                && Snapshot::validateCompletedOriginal(upstream)
                && !upstream->originalOutput()->originalObject()->observeOriginalCommittedState(
                    frozen->range()));
            require(Snapshot::validateOriginal(upstream));
            require(Custody::validateGeneralOutputPending(upstream, seal, *frozen));
            auto forgedSeal =
                std::shared_ptr<const rhi::OriginalCommandRecordingSeal>(resultQuota, seal.get());
            assert(Custody::validateGeneralOutputPending(upstream, forgedSeal, *frozen));
            forgedSeal.reset();
            auto forgedSnapshot = std::shared_ptr<const Snapshot>(resultQuota, upstream.get());
            assert(Custody::validateGeneralOutputPending(forgedSnapshot, seal, *frozen));
            forgedSnapshot.reset();
            const auto* dependency = std::get_if<rhi::ResourceDependencyArguments>(
                root->originalGraph()->originalInputs()->descriptorAt(0u));
            assert(dependency);
            const auto saved = *dependency;
            rhi::ResourceBarrierDesc wrong;
            assert(saved.write(rhi::CommandUsageKind::TextureBarrier, wrong));
            wrong.sourceStages = rhi::PipelineStage::Transfer;
            auto& mutableDependency = const_cast<rhi::ResourceDependencyArguments&>(*dependency);
            mutableDependency = *rhi::ResourceDependencyArguments::capture(
                rhi::CommandUsageKind::TextureBarrier, wrong);
            assert(Custody::validateGeneralOutputPending(upstream, seal, *frozen));
            mutableDependency = saved;
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(hotQuotas, [&]() noexcept {
                return Custody::validateGeneralOutputPending(upstream, seal, *frozen);
            }));
            assert(!result->publishedBytes());
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            RecordingScopes::waitHeldScopes();
            assert(!issuer->hasOriginalRecordedCommand());
            RecordingScopes::releaseScopes();
#endif
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuer->hasOriginalRecordedCommand();
            }));
            require(issuer->recordingDiagnostic());
            assert(Issuer::submitOriginal(issuer, {1u, true}) && !result->publishedBytes());
            const auto& originalSources = root->originalGraph()
                                              ->originalInputs()
                                              ->originalScalars()
                                              ->originalUsage()
                                              ->originalSources();
            const auto& readback =
                std::get<std::shared_ptr<const Readback>>(originalSources->sources()[2u]);
            const auto target = rhi_webgpu::webGpuNativePayloadFor(
                *readback->originalDestinationLease()->originalObject());
            assert(target && target->buffer && target->ownerDevice);
            bool mapInvoked = false;

            struct MapObservation {
                bool* invoked;
                MapTerminal terminal;
                const std::shared_ptr<rhi::OriginalCommandStateTransaction>* root;
                const std::shared_ptr<const Custody>* custody;
                const std::shared_ptr<const Issuer>* issuer;
                const std::shared_ptr<const rhi::OriginalCommandReadbackResultSnapshot>* result;
            } observation{&mapInvoked, terminal, &root, &custody, &issuer, &result};

            if (terminal != MapTerminal::None) {
                const auto probe = terminal == MapTerminal::NativeError
                    ? MapProbe::Probe::NativeError
                    : terminal == MapTerminal::ReadError        ? MapProbe::Probe::ReadError
                    : terminal == MapTerminal::ScopeNativeError ? MapProbe::Probe::ScopeNativeError
                    : terminal == MapTerminal::ThrowBefore      ? MapProbe::Probe::ThrowBefore
                    : terminal == MapTerminal::ThrowAfter       ? MapProbe::Probe::ThrowAfter
                    : terminal == MapTerminal::ScopeRegistration
                    ? MapProbe::Probe::ScopeRegistration
                    : terminal == MapTerminal::UnmapThrow    ? MapProbe::Probe::UnmapThrow
                    : terminal == MapTerminal::PendingLoss   ? MapProbe::Probe::PendingLoss
                    : terminal == MapTerminal::ReverseScopes ? MapProbe::Probe::ReverseScopes
                                                             : MapProbe::Probe::Success;
                MapProbe::instrument(target->ownerDevice,
                    target->buffer,
                    probe,
                    &observation,
                    [](void* value) noexcept {
                        auto& o = *static_cast<MapObservation*>(value);
                        *o.invoked = true;
                        assert((*o.root)->status()
                                == rhi::OriginalCommandStateTransaction::Status::Pending
                            && !(*o.root)->journalEmpty() && (*o.custody)->originalNativeLossArmed()
                            && !(*o.issuer)->hasOriginalCompletedSubmission()
                            && !(*o.result)->publishedBytes());
                        if (!mapCompletes(o.terminal))
                            MapScopes::holdScopes();
                        if (o.terminal == MapTerminal::Cancel) {
                            const auto* failure = (*o.custody)->cancelOriginal();
                            assert(failure
                                && core::StringView(failure)
                                    == "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
                        }
                    });
            }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            using SubmissionScopes = WebGpuErrorScopeHandoffFixture<1022u>;
            SubmissionScopes::holdScopes(true);
#endif
            const auto submit = [&] {
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(hotQuotas, [&]() noexcept {
                    return Issuer::submitOriginal(issuer, submission);
                }));
            };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(submit);
#else
            submit();
#endif
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            SubmissionScopes::restoreScopes();
            SubmissionScopes::waitHeldScopes();
            assert(!issuer->hasOriginalCompletedSubmission() && !result->publishedBytes());
            require(Custody::validateGeneralOutputPending(upstream, seal, *frozen));
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
            if (terminal == MapTerminal::PostSuppressionPublication)
                OriginalGeneralFinitePostSuppressionFixture::arm(
                    custody, WebGpuPostSuppressionFailureProbeFixture::Failure::Publication, root);
#endif
            if (terminal != MapTerminal::None && upstream->originalBudget().contractVersion == 2u)
                std::printf(
                    "BINDLESS_RHI_GENERAL_FINITE_MAP4_COPY_HELD_OK %s original-upstream-completed downstream-copy-scopes3-held root-Pending unpublished queue-callback-retained public-closed\n",
                    mapTerminalName(terminal));
            SubmissionScopes::releaseScopes();
#endif
            if (failedMap) {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return mapInvoked;
                }));
                MapScopes::waitHeldScopes(terminal == MapTerminal::ScopeRegistration ? 2u : 3u);
                assert(custody->pendingOriginalRecords() == 1u && !result->publishedBytes()
                    && !issuer->hasOriginalCompletedSubmission());
                const std::weak_ptr<const Command> commandObserver = command;
                const std::weak_ptr<const Storage> storageObserver = results;
                if (terminal == MapTerminal::Drop) {
                    const std::weak_ptr<const Issuer> issuerObserver = issuer;
                    const std::weak_ptr<const Lowering> loweringObserver = lowering;
                    const std::weak_ptr<const Custody> custodyObserver = custody;
                    // Wrapper destruction requests cancellation. Native map/scope
                    // userdata still owns the command and result storage until
                    // the last genuine borrowed callback has drained.
                    issuer.reset();
                    lowering.reset();
                    custody.reset();
                    command.reset();
                    results.reset();
                    assert(issuerObserver.expired() && loweringObserver.expired()
                        && custodyObserver.expired());
                    assert(!commandObserver.expired() && !storageObserver.expired()
                        && root->status()
                            == rhi::OriginalCommandStateTransaction::Status::Invalidated
                        && root->journalEmpty() && !result->publishedBytes());
                    std::puts(
                        "BINDLESS_RHI_GENERAL_OUTPUT_OWNER_DROP_HELD_OK wrappers-expired command-storage-native-retained scopes3 Invalidated unpublished");
                }
                if (upstream->originalBudget().contractVersion == 2u)
                    std::printf(
                        "BINDLESS_RHI_GENERAL_FINITE_MAP4_HELD_OK %s native-map-invoked pending-record1 original-scopes-held unpublished owners-retained %s\n",
                        mapTerminalName(terminal),
                        root->status() == rhi::OriginalCommandStateTransaction::Status::Invalidated
                            ? "root-Invalidated"
                            : "root-Pending");
                const auto expectedDiagnostic =
                    terminal == MapTerminal::ThrowBefore || terminal == MapTerminal::ThrowAfter
                    ? "RHI-WEBGPU-NATIVE-MAP-INVOCATION-UNCERTAIN"
                    : terminal == MapTerminal::ScopeRegistration
                    ? "RHI-WEBGPU-CANONICAL-MAP-SCOPE-REGISTRATION-FAILED"
                    // The pinned bridge drops its unmap tracking before the
                    // foreign call. A repeated C unmap cannot prove native
                    // unmapping after the first invocation throws.
                    : terminal == MapTerminal::UnmapThrow ? "RHI-WEBGPU-NATIVE-MAP-UNMAP-UNPROVEN"
                    : terminal == MapTerminal::ReadError ? "RHI-WEBGPU-NATIVE-MAP-READ-NATIVE-ERROR"
                                                         : nullptr;
                if (expectedDiagnostic) {
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return issuer->submissionDiagnostic() != nullptr;
                    }));
                    std::printf("BINDLESS_RHI_GENERAL_OUTPUT_MAP_DIAGNOSTIC %s %s\n",
                        mapTerminalName(terminal),
                        issuer->submissionDiagnostic());
                    assert(core::StringView(issuer->submissionDiagnostic()) == expectedDiagnostic);
                }
                if (terminal == MapTerminal::ReadError) {
                    // The pinned bridge prints its native read diagnostic only
                    // with ASSERTIONS. Release still must prove the real exact
                    // read range, one intercepted call and typed RHI refusal.
                    MapProbe::verifyReadFault(result->byteCapacity());
                }
                if (terminal == MapTerminal::UnmapThrow)
                    MapProbe::verifyUnmapFault();
                if (terminal == MapTerminal::PendingLoss) {
                    assert(WebGpuAsyncWaitFixture::until([] {
                        return EM_ASM_INT({
                            // clang-format off
                                return !!Module.canonicalMapProbe.pendingResolve;
                            // clang-format on
                        });
                    }));
                    assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Pending
                        && !root->journalEmpty());
                }
                MapScopes::restoreScopes();
                MapProbe::restore(terminal == MapTerminal::PendingLoss);
                if (mapUsesPhysicalLoss(terminal)) {
                    assert(!custody->originalNativeLossDelivered()
                        && custody->pendingOriginalRecords() == 1u);
                    assert(EM_ASM_INT(
                        {
                            // clang-format off
                                const d = WebGPU.getJsObject($0);
                                if (d !== Module.preinitializedWebGPUDevice)
                                    return 0;
                                d.destroy();
                                return 1;
                            // clang-format on
                        },
                        target->ownerDevice));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return custody->originalNativeLossDelivered();
                    }));
                    assert(
                        root->status() == rhi::OriginalCommandStateTransaction::Status::Invalidated
                        && root->journalEmpty() && custody->pendingOriginalRecords() == 1u
                        && !result->publishedBytes());
                    if (terminal == MapTerminal::PendingLoss)
                        MapProbe::retirePendingAfterLoss();
                }
                // Two deliveries cannot retire the original owner while the
                // final genuine scope still borrows its callback userdata.
                if (terminal != MapTerminal::ScopeRegistration) {
                    MapScopes::releaseScope(0u);
                    MapScopes::releaseScope(1u);
                    emscripten_sleep(0u);
                    if (terminal == MapTerminal::Drop)
                        assert(!commandObserver.expired() && !storageObserver.expired()
                            && !result->publishedBytes());
                    else
                        assert(
                            custody->pendingOriginalRecords() == 1u && !result->publishedBytes());
                }
                MapScopes::releaseScopes();
                if (terminal == MapTerminal::Drop) {
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return commandObserver.expired() && storageObserver.expired();
                    }));
                    assert(
                        root->status() == rhi::OriginalCommandStateTransaction::Status::Invalidated
                        && root->journalEmpty() && !result->publishedBytes());
                    std::puts(
                        "BINDLESS_RHI_GENERAL_OUTPUT_OWNER_DROP_DRAINED_OK final-native-scope command-storage-expired unpublished no-retry");
                } else {
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return !custody->pendingOriginalRecords();
                    }));
                    assert(
                        root->status() == rhi::OriginalCommandStateTransaction::Status::Invalidated
                        && root->journalEmpty() && !issuer->hasOriginalCompletedSubmission()
                        && !result->publishedBytes() && issuer->submissionDiagnostic()
                        && !custody->originalNativeLossArmed());
                }
                if (terminal == MapTerminal::NativeError)
                    assert(core::StringView(issuer->submissionDiagnostic())
                        == "RHI-WEBGPU-CANONICAL-MAP-NATIVE-ERROR");
                if (terminal == MapTerminal::ScopeNativeError)
                    assert(core::StringView(issuer->submissionDiagnostic())
                        == "RHI-WEBGPU-CANONICAL-MAP-SCOPE-NATIVE-ERROR");
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
                if (terminal == MapTerminal::PostSuppressionPublication)
                    OriginalGeneralFinitePostSuppressionFixture::verifyTerminal(custody,
                        WebGpuPostSuppressionFailureProbeFixture::Failure::Publication,
                        root,
                        issuer->submissionDiagnostic());
#endif
                assert(Issuer::recordOriginal(issuer, recording)
                    && Issuer::submitOriginal(issuer, submission));
                assert(!readback->originalDestinationLease()
                           ->originalObject()
                           ->observeOriginalCommittedState()
                    && Snapshot::validateCompletedOriginal(upstream));
            } else {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return issuer->hasOriginalCompletedSubmission()
                        || root->status()
                        == rhi::OriginalCommandStateTransaction::Status::Invalidated;
                }));
                require(issuer->submissionDiagnostic());
                assert(issuer->hasOriginalCompletedSubmission());
                assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Committed
                    && root->journalEmpty());
                require(root->validate());
                assert(Custody::validateGeneralOutputPending(upstream, seal, *frozen));
                assert(Snapshot::validateCompletedOriginal(upstream));
                const auto* pixels = result->publishedBytes();
                assert(pixels && result->byteCapacity() == 256u);
                const auto statistics = core::analyzeStridedImageChannels(pixels,
                    result->byteCapacity(),
                    1u,
                    1u,
                    256u,
                    8u,
                    4u,
                    [](const std::uint8_t* pixel, std::uint32_t channel) {
                        const auto half = std::uint16_t(pixel[2u * channel])
                            | (std::uint16_t(pixel[2u * channel + 1u]) << 8u);
                        return core::halfToFloat(half);
                    });
                assert(statistics.valid && statistics.channelCount == 4u);
                for (std::size_t channel = 0u; channel < expected.size(); ++channel) {
                    const auto actual = std::uint16_t(pixels[2u * channel])
                        | (std::uint16_t(pixels[2u * channel + 1u]) << 8u);
                    assert(actual == expected[channel]
                        && statistics.channels[channel].sampleCount == 1u
                        && statistics.channels[channel].finiteCount == 1u
                        && statistics.channels[channel].meanValue
                            == core::halfToFloat(expected[channel]));
                }
                assert(!custody->pendingOriginalRecords() && !custody->originalNativeLossArmed());
                std::printf(
                    "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK original-copy1 map1 unmap-drain committed-published rgba %.8f %.8f %.8f %.8f owners%u hot0-query0 public-closed\n",
                    statistics.channels[0].meanValue,
                    statistics.channels[1].meanValue,
                    statistics.channels[2].meanValue,
                    statistics.channels[3].meanValue,
                    issuer->retainedOwnerCount());
                if (terminal != MapTerminal::None) {
                    assert(mapInvoked);
                    MapProbe::restore();
                }
                if (terminal == MapTerminal::PublishedLoss) {
                    const auto loss = device.observeOriginalDeviceNativeLoss(id);
                    require(id);
                    assert(loss);
                    assert(EM_ASM_INT(
                        {
                            // clang-format off
                                const d = WebGPU.getJsObject($0);
                                if (d !== Module.preinitializedWebGPUDevice)
                                    return 0;
                                d.destroy();
                                return 1;
                            // clang-format on
                        },
                        target->ownerDevice));
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return loss->diagnostic() != nullptr;
                    }));
                    assert(result->publishedBytes() == pixels && result->nativeExecutionProven()
                        && Snapshot::validateCompletedOriginal(upstream)
                        && !upstream->originalOutput()
                            ->originalObject()
                            ->observeOriginalCommittedState(frozen->range()));
                    for (std::size_t channel = 0u; channel < expected.size(); ++channel)
                        assert((std::uint16_t(pixels[2u * channel])
                                   | (std::uint16_t(pixels[2u * channel + 1u]) << 8u))
                            == expected[channel]);
                }
            }
        }
        assert(!resultQuota->liveBytes());
        if (terminal != MapTerminal::None)
            std::printf(
                "BINDLESS_RHI_GENERAL_OUTPUT_MAP1_OK %s original-copy-map genuine-drain %s %s result-quota-zero public-closed\n",
                mapTerminalName(terminal),
                failedMap ? "invalidated-unpublished-no-retry" : "committed-numerical-published",
                mapUsesPhysicalLoss(terminal) ? "actual-device-loss" : "no-device-loss");
    }
#endif
};
} // namespace arp::test
