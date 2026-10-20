#pragma once
#include "bindless_package_request_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_original_texture_upload.h"
#include "rhi/frontend/bindless_snapshot.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>

namespace arp::test {
class VulkanOriginalTextureUploadFixture final {
    static constexpr std::uint64_t cpuBytes = 65536u;

    static void require(const char* diagnostic)
    {
        if (diagnostic)
            std::fprintf(stderr, "Vulkan UploadFence7: %s\n", diagnostic);
        assert(!diagnostic);
    }

public:
    template <class Negative>
    static void verify(
        const rhi_vulkan::VulkanRhiDeviceDesc& desc, const rhi::BindlessShaderArtifact& artifact)
    {
        // One genuine runtime/queue is shared with the captured integration
        // fixture. Each logical device still has its own original inventory.
        for (const auto layers : {1u, 2u})
            for (const auto drop : {true, false})
                verifyOne<Negative>(desc, artifact, layers, drop);
        if (std::getenv("ARP_VULKAN_UPLOAD_TERMINAL_CASES")) {
            for (const auto terminal : {1u, 2u, 3u})
                verifyOne<Negative>(desc, artifact, 1u, false, terminal);
            std::puts(
                "BINDLESS_VULKAN_UPLOAD_FENCE7_TERMINAL 1 withheld-fence missing-failed-idle uncertain-queue rejected-queue retained-unpublished hot0");
        }
        if (std::getenv("ARP_VULKAN_COMPLETED_TEXTURE_PUBLICATION")) {
            // Publication uses its own descriptor-only native-loss join;
            // accepted pipelines and general Root execution remain closed.
            for (const auto layers : {1u, 2u})
                verifyOne<Negative>(desc, artifact, layers, false, 0u, true);
            std::puts(
                "BINDLESS_VULKAN_COMPLETED_TEXTURE_PUBLICATION1 1 genuine-completion original-source 2D-array ordinary-write-rejected snapshot-prefix-padding retained-provenance descriptor-loss-registration9 sampler-loss-join2");
        }
        std::puts(
            "BINDLESS_VULKAN_ORIGINAL_TEXTURE_UPLOAD_FENCE7 1 RGBA8 2D-array mips2 sealed-staging original-journal real-fence quota-return");
    }

private:
    template <class Negative>
    static void verifyOne(const rhi_vulkan::VulkanRhiDeviceDesc& desc,
        const rhi::BindlessShaderArtifact& artifact,
        std::uint32_t layers,
        bool drop,
        unsigned terminal = 0u,
        bool publish = false)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        using Staging = VulkanOriginalTextureUploadStaging;
        using Root = OriginalCommandStateTransaction;
        using Graph = OriginalCommandResourceStates;
        using Inputs = OriginalCommandInputs;
        using Sources = OriginalCommandResourceSources;
        using K = CommandUsageKind;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 21u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        {
            auto device = createVulkanRhiDevice(desc);
            assert(device);
            require(device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, q[0]));
            require(
                device->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, cpuBytes}, q[1]));
            // The snapshot domain is prepared before staging so registry bucket
            // capacity is stable during the existing exact/one-less checks.
            auto* extension = static_cast<const BindlessCrossExtension*>(
                device->queryInterface(kExtensionBindlessCross));
            assert(extension);
            const auto snapshotBuffers = publish && layers == 1u
                ? (artifact.snapshotMetadata.contractVersion ? 1u : 0u)
                    + (artifact.faultLatch.contractVersion ? 1u : 0u)
                    + (extension->capabilities().faultReadbackContractVersion == 1u ? 1u : 0u)
                : 0u;
            const auto descriptorDomains = publish ? (layers == 1u ? 2u : 1u) : 0u;
            OriginalResourceRegistrationBudget registration{publish ? 9u : 1u,
                true,
                1u + snapshotBuffers,
                1u,
                publish ? 1u : 0u,
                OriginalResourceRegistrationContract::fixedOwnerCount
                    + (2u + (publish ? 1u : 0u) + snapshotBuffers)
                        * OriginalResourceRegistrationContract::perResourceOwnerCount
                    + descriptorDomains
                        * (OriginalResourceRegistrationContract::perResourceOwnerCount
                            + OriginalResourceRegistrationContract::perDescriptorOwnerCount),
                cpuBytes};
            registration.maxDescriptorArenas = registration.maxDescriptorSets = descriptorDomains;
            const bool samplerJoin = publish && layers == 2u;
            if (samplerJoin) {
                registration.samplers = {1u, true, 1u};
                registration.maxRetainedOwners +=
                    OriginalResourceRegistrationContract::perResourceOwnerCount;
            }
            if (publish) {
                const char* lossId = nullptr;
                const auto loss = device->observeOriginalDeviceNativeLoss(lossId);
                require(lossId);
                assert(loss);
                assert(device->prepareOriginalResourceRegistration(
                    registration, q[2])); // Ordinary opt-in cannot issue /9.
                assert(device->prepareOriginalDescriptorNativeLossRegistration(
                    {1u, false}, registration, q[2], *loss));
                if (samplerJoin) {
                    assert(device->prepareOriginalDescriptorNativeLossRegistration(
                        {1u, true}, registration, q[2], *loss));
                    assert(device->prepareOriginalDescriptorNativeLossRegistration(
                        {2u, false}, registration, q[2], *loss));
                    require(device->prepareOriginalDescriptorNativeLossRegistration(
                        {2u, true}, registration, q[2], *loss));
                } else
                    require(device->prepareOriginalDescriptorNativeLossRegistration(
                        {1u, true}, registration, q[2], *loss));
            } else
                require(device->prepareOriginalResourceRegistration(registration, q[2]));
            const auto gpuBaseline = device->memoryStats().allocatedBytes;
            std::uint64_t stagingRegistryBytes = 0u;
            const char* id = nullptr;
            {
                TextureDesc td;
                td.extent = {2u, 2u, 1u};
                td.format = TextureFormat::Rgba8Unorm;
                td.arrayLayerCount = layers;
                td.mipLevelCount = 2u;
                td.usage = TextureUsage::CopyDst | TextureUsage::CopySrc | TextureUsage::Sampled;
                const auto nativeBytes = device->queryOriginalContentTextureAllocation(td, id);
                require(id);
                assert(nativeBytes);
                core::UniquePtr<GpuTexture> texture;
                {
                    core::CpuMemoryQuotaScope scope(q[3]);
                    texture = device->createOriginalContentTexture(td,
                        {3u, true, 1u, 1u, 20u * layers, 4u, cpuBytes, 0u, nativeBytes},
                        q[4],
                        id);
                }
                require(id);
                assert(texture);
                const auto* object = texture.get();
                auto lease = OriginalCommandTextureLease::prepareOriginal(
                    texture, {1u, true, 2u, cpuBytes}, q[5], id);
                require(id);
                assert(lease);
                core::UniquePtr<GpuTextureView> view;
                std::shared_ptr<DescriptorArena> arena;
                core::UniquePtr<DescriptorSet> set;
                std::shared_ptr<BindlessSnapshot> snapshot;
                BindlessResourceHandle snapshotHandle;
                std::shared_ptr<const OriginalCommandTextureViewLease> viewLease;
                std::shared_ptr<const OriginalCommandTextureViewSource> viewSource;
                std::shared_ptr<const OriginalCommandSamplerLease> samplerLease;
                DescriptorBindingWrite sampled;
                const OriginalCompletedTextureDescriptorRequest publication{
                    1u, true, 1u, 1u, OriginalCommandTextureViewSource::completeRetainedOwnerCount};
                const auto fresh = *object->observeOriginalContent();
                if (publish) {
                    TextureViewDesc vd;
                    vd.format = td.format;
                    vd.usage = TextureUsage::Sampled;
                    vd.dimension = layers == 1u ? TextureViewDimension::Texture2D
                                                : TextureViewDimension::Texture2DArray;
                    vd.mipLevelCount = td.mipLevelCount;
                    vd.arrayLayerCount = layers;
                    {
                        core::CpuMemoryQuotaScope scope(q[6]);
                        view = device->createTextureView(*object, vd);
                        assert(view);
                        DescriptorBindingDesc binding{
                            0u, DescriptorType::SampledTexture, ShaderStage::Compute, 1u};
                        binding.textureViewDimensionConstrained = true;
                        binding.textureViewDimension = vd.dimension;
                        DescriptorLayoutDesc layout;
                        layout.name = "Completed upload read publication";
                        layout.bindings.push_back(binding);
                        if (samplerJoin)
                            layout.bindings.push_back(
                                {1u, DescriptorType::Sampler, ShaderStage::Compute, 1u});
                        arena = core::shareUnique(device->createOriginalDescriptorArena(
                            {layout.name, 1u, samplerJoin ? 2u : 1u}));
                        assert(arena);
                        set = device->allocateOriginalDescriptorSet(arena, {layout.name, layout});
                        assert(set);
                        if (samplerJoin) {
                            auto sampler = device->createSampler({});
                            assert(sampler);
                            require(device->validateOriginalResourceRegistration(*sampler));
                            samplerLease = OriginalCommandSamplerLease::prepareOriginal(
                                sampler, {1u, true, 2u, cpuBytes}, q[5], id);
                            require(id);
                            assert(samplerLease && !sampler);
                            assert(
                                !device->createSampler({})); // Exact registered sampler capacity.
                        }
                    }
                    viewLease = OriginalCommandTextureViewLease::prepareOriginal(
                        view, {1u, true, 2u, cpuBytes}, q[5], id);
                    require(id);
                    assert(viewLease);
                    viewSource = OriginalCommandTextureViewSource::prepareOriginal(viewLease,
                        lease,
                        {1u,
                            true,
                            OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                            cpuBytes},
                        q[5],
                        id);
                    require(id);
                    assert(viewSource);
                    sampled.binding = 0u;
                    sampled.type = DescriptorType::SampledTexture;
                    sampled.resource = viewSource->resource();
                    sampled.resourceKind = DescriptorResourceKind::TextureView;
                    sampled.resourceState = ResourceState::ShaderRead;

                    assert(!set->write(sampled).success);
                    assert(
                        !set->writeOriginalCompletedTexture(
                                *device, sampled, viewSource, fresh, publication)
                            .success);
                    assert(!set->hasWrittenBinding(0u) && set->contentRevision() == 1u);
                    if (layers == 1u) {
                        core::CpuMemoryQuotaScope scope(q[15]);
                        auto request = BindlessPackageRequestFixture::make(
                            artifact, extension->capabilities().deviceIdentity);
                        for (auto& table : request.tables)
                            if (table.binding.type == DescriptorType::SampledTexture) {
                                table.resourceCount = 1u;
                                snapshotHandle.tableId = table.tableId;
                                break;
                            }
                        BindlessSnapshotPool pool(*device, request.budget);
                        core::pmr::String diag;
                        snapshot = core::shareUnique(pool.prepareOriginalDescriptors(
                            request, artifact, {1u, true, false}, diag));
                        if (!snapshot)
                            std::fprintf(stderr, "upload snapshot: %s\n", diag.c_str());
                        assert(snapshot);
                        snapshotHandle = snapshot->handle(snapshotHandle.tableId, 0u);
                        assert(!snapshot
                                ->writeOriginalCompletedTexture(
                                    snapshotHandle, *device, viewSource, fresh, publication)
                                .success);
                    }
                }
                std::array<std::uint8_t, 40u> pixels{};
                for (std::size_t i = 0u; i < 20u * layers; ++i)
                    pixels[i] = static_cast<std::uint8_t>(i + 1u);
                UploadTextureDesc upload;
                upload.destinationTexture = object->id();
                upload.extent = td.extent;
                upload.format = td.format;
                upload.subresources = defaultTextureSubresourceRange(td);
                upload.bytesPerRow = 8u;
                upload.rowsPerImage = 2u;
                upload.pixelData = pixels.data();
                upload.pixelDataSize = 20u * layers;
                upload.mipLayouts = {
                    {0u, {2u, 2u, 1u}, 8u, 2u, 0u}, {1u, {1u, 1u, 1u}, 4u, 1u, 16u * layers}};
                auto input = TextureUploadInputSnapshot::prepareOriginal(upload,
                    {1u, true, 2u, 0u, 20u * layers, cpuBytes, cpuBytes, cpuBytes},
                    q[16],
                    q[16],
                    q[5],
                    id);
                require(id);
                assert(input);
                auto source = OriginalCommandTextureUploadSource::prepareOriginal(
                    input, lease, {1u, true, 5u, cpuBytes}, q[5], id);
                require(id);
                assert(source);
                const auto packedBytes = 3u * kTextureUploadRowPitchAlignment * layers;
                auto packing = TextureUploadPacking::prepareOriginal(
                    input, td, {1u, true, 2u, packedBytes, cpuBytes, cpuBytes}, q[9], q[10], id);
                require(id);
                assert(packing && packing->packedBytes().size() == packedBytes);
                auto packed = OriginalCommandTextureUploadPackingSource::prepareOriginal(source,
                    packing,
                    {1u,
                        true,
                        2u,
                        OriginalCommandTextureUploadPackingSource::retainedOwnerCount,
                        20u * layers,
                        packedBytes,
                        cpuBytes},
                    q[11],
                    id);
                require(id);
                assert(packed);
                const Staging::Budget b{1u,
                    true,
                    1u,
                    2u,
                    OriginalTextureUploadStagingContract::retainedOwnerBound,
                    packedBytes,
                    desc.memoryBudgetBytes,
                    cpuBytes,
                    cpuBytes,
                    cpuBytes};
                const Staging::Quotas quotas{q[12], q[13], q[5]};
                auto small = b;
                --small.maxLogicalStagingBytes;
                assert(!Staging::prepareOriginal(packed, *device, small, quotas, id));
                assert(!q[12]->liveBytes() && !q[13]->liveBytes() && !q[14]->liveBytes());
                auto alias = quotas;
                alias.metadata = alias.lease;
                assert(!Staging::prepareOriginal(packed, *device, b, alias, id));
                const auto makeStaging = [&] {
                    return Staging::prepareOriginal(packed, *device, b, quotas, id);
                };
                std::uint64_t measuredNativeStaging = 0u;
                {
                    auto warm = makeStaging();
                    require(id);
                    assert(warm);
                    measuredNativeStaging = warm->nativeStagingBytes();
                }
                // Ordinary backend registry buckets retain measured capacity
                // until logical-device teardown. This is not staging custody.
                stagingRegistryBytes = q[13]->liveBytes();
                small = b;
                small.maxNativeStagingBytes = measuredNativeStaging - 1u;
                assert(!Staging::prepareOriginal(packed, *device, small, quotas, id));
                assert(q[13]->liveBytes() == stagingRegistryBytes && !q[12]->liveBytes()
                    && !q[14]->liveBytes());
                for (const auto index : {12u, 13u, 5u}) {
                    CpuQuotaFactoryFixture pressure(q[index]);
                    const auto peak = pressure.measure(makeStaging);
                    pressure.verify(peak, makeStaging);
                }
                auto staging = makeStaging();
                require(id);
                assert(staging);
                const auto nativeStaging = staging->nativeStagingBytes();
                assert(nativeStaging >= packedBytes);
                const auto* buffer = staging->originalStagingLease()->originalObject();
                assert(buffer->hostWritesSealed() && buffer->gpuWritesSealed()
                    && buffer->state() == ResourceState::CopySrc);
                assert(!device->writeBuffer(*const_cast<GpuBuffer*>(buffer), pixels.data(), 1u)
                        .success);
                auto forged = std::shared_ptr<const Staging>(q[12], staging.get());
                assert(Staging::validateOriginal(forged));
                forged.reset();
                // Exact original three-Source inventory. The memory dependency
                // makes the immutable staging part of this same state graph.
                const std::array<Sources::Source, 3u> retained{
                    lease, source, staging->originalStagingLease()};
                const std::array<CommandUsage, 6u> commands{{{K::BufferMemoryBarrier, 0u, 1u},
                    {K::TextureBarrier, 1u, 1u},
                    {K::BeginTransferPass, 2u, 0u},
                    {K::TextureUpload, 2u, 2u},
                    {K::EndTransferPass, 4u, 0u},
                    {K::Finish, 4u, 0u}}};
                const std::array<OriginalCommandUsage::Reference, 4u> refs{
                    {{2u}, {0u}, {0u}, {1u}}};
                ResourceMemoryBarrierDesc memory;
                memory.resource = buffer->id();
                memory.sourceStages = memory.destinationStages = PipelineStage::Transfer;
                memory.sourceAccess = memory.destinationAccess = ResourceAccess::TransferRead;
                ResourceBarrierDesc barrier{object->id(),
                    upload.subresources,
                    ResourceState::Undefined,
                    ResourceState::CopyDst};
                barrier.sourceStages = PipelineStage::TopOfPipe;
                barrier.sourceAccess = ResourceAccess::None;
                barrier.destinationStages = PipelineStage::Transfer;
                barrier.destinationAccess = ResourceAccess::TransferWrite;
                const std::array<Inputs::OriginalDescriptor, 3u> descriptors{
                    {{0u, *ResourceDependencyArguments::capture(K::BufferMemoryBarrier, memory)},
                        {1u, *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)},
                        {3u, *TextureUploadOriginalInputArguments::captureOriginal(input)}}};
                OriginalCommandAdmissionBudget admission;
                admission.contractVersion = 1u;
                admission.prototypeOptIn = true;
                admission.maxStateObservations = 3u;
                admission.maxTextureRanges = 2u;
                admission.maxMipMappings = 2u;
                admission.maxStateTransactions = 1u;
                admission.maxRetainedUploadBytes = 20u * layers;
                admission.maxPendingSubmissions = 1u;
                admission.maxNativePayloadObjects = 1u;
                admission.maxTypedNativeCommandCalls = 4u;
                admission.maxNativeStagingBytes = nativeStaging;
                using D = OriginalCommandAdmissionBudget::CpuDomain;
                admission.maxCpuBytes[static_cast<std::size_t>(D::Recording)] = cpuBytes;
                admission.maxCpuBytes[static_cast<std::size_t>(D::Submission)] = cpuBytes;
                auto inputs = OriginalCommandUploadStreamBuilderFixture::prepare(
                    retained, commands, refs, descriptors, admission, q[5], q[16], id);
                require(id);
                assert(inputs);
                const OriginalCommandStateAdmissionBudget graphBudget{
                    1u, true, 1u, 2u, 3u, 2u, 128u, cpuBytes};
                auto graph = Graph::prepareOriginal(inputs, *device, graphBudget, q[17], id);
                require(id);
                assert(graph);
                auto root = Root::prepareOriginalOwnership(graph, *device, graphBudget, q[18], id);
                require(id);
                assert(root);
                const OriginalCommandStateSequenceBudget sequence{
                    1u, true, 6u, 0u, 1u + 4u * layers, 1u + 4u * layers};
                const OriginalTextureContentPlanBudget content{7u, true, 1u, 1u, 0u, 2u, 6u, 20u};
                const auto inspected = root->preflightOriginalUndefinedTextureUploads(content);
                require(inspected.diagnostic);
                assert(inspected.commandVisits == 6u && inspected.projectionVisits == 20u);
                require(root->materializeOriginalPrototypePlan(sequence, content));
                OriginalCommandStreamBudget streamBudget;
                streamBudget.contractVersion = 1u;
                streamBudget.prototypeOptIn = true;
                streamBudget.maxCommands = 6u;
                streamBudget.maxDescriptors = 3u;
                streamBudget.maxReferences = 4u;
                streamBudget.maxTextureUploadMips = 2u;
                streamBudget.maxTextureUploadPixelBytes = 20u * layers;
                streamBudget.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
                streamBudget.maxCpuMetadataBytes = cpuBytes;
                auto program =
                    OriginalCommandStreamProgram::prepareOriginal(inputs, streamBudget, q[20], id);
                require(id);
                assert(program);
                OriginalCommandNativeLifecycleBudget life{7u,
                    true,
                    1u,
                    0u,
                    0u,
                    root->retainedOwnerCount() + 2u,
                    0u,
                    0u,
                    cpuBytes,
                    cpuBytes,
                    0u};
                life.maxSubmissionOwners = life.maxRecordingOwners + 1u
                    + program->retainedOwnerCount() + staging->retainedOwnerCount() + 2u;
                auto seal = OriginalCommandRecordingSeal::prepareOriginal(
                    root, *device, life, q[19], {}, id);
                require(id);
                assert(seal);
                assert(!OriginalCommandLifecycleReservation::prepareOriginal(
                    seal, {2u, true, 1u, 0u, 0u}, id));
                auto reservation = OriginalCommandLifecycleReservation::prepareOriginal(
                    seal, {3u, true, 1u, 0u, 0u}, id);
                require(id);
                assert(reservation);
                core::pmr::String diagnostic;
                diagnostic.reserve(192u);
                {
                    core::CpuMemoryQuotaLease pressure(
                        q[19], q[19]->limitBytes() - q[19]->liveBytes());
                    assert(pressure.charged());
                    assert(!VulkanOriginalTextureUploadCommand::prepareOriginal(
                        reservation, program, staging, *device, diagnostic));
                    assert(root->status() == Root::Status::Prepared
                        && object->observeOriginalContent());
                }
                auto command = VulkanOriginalTextureUploadCommand::prepareOriginal(
                    reservation, program, staging, *device, diagnostic);
                if (!command)
                    std::fprintf(stderr, "UploadFence7 command: %s\n", diagnostic.c_str());
                assert(command);
                assert(
                    root->status() == Root::Status::Pending && !object->observeOriginalContent());
                assert(Staging::validateOriginal(
                    staging)); // Cold stamps cannot be reused while Pending.
                assert(!VulkanOriginalTextureUploadCommand::prepareOriginal(
                    reservation, program, staging, *device, diagnostic));
                std::weak_ptr<const OriginalCommandRecordingSeal> weakSeal = seal;
                seal.reset();
                reservation.reset();
                assert(!weakSeal.expired());
                if (drop) {
                    command.reset();
                    assert(object->observeOriginalContent()->state()
                        == common::OriginalTextureContentControl::State::Uninitialized);
                    assert(object->observeOriginalCommittedState(upload.subresources)->state()
                        == ResourceState::Undefined);
                } else {
                    Negative::withholdCompletion = terminal != 0u;
                    Negative::uncertainQueueResult = terminal == 2u;
                    Negative::rejectQueueInvocation = terminal == 3u;
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        q, [&]() noexcept -> const char* {
                            const auto success = command->submit(diagnostic);
                            assert(success == (terminal < 2u));
                            return nullptr;
                        }));
                    Negative::uncertainQueueResult = Negative::rejectQueueInvocation = false;
                    assert(!object->observeOriginalContent());
                    if (terminal) {
                        assert(!command->submittedFenceCompleted() && !weakSeal.expired());
                        assert(!command->waitSubmittedFenceUntil(
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(1),
                            diagnostic));
                        assert(!weakSeal.expired());
                        Negative::omitIdleEntrypoint = true;
                        device->waitIdle();
                        Negative::omitIdleEntrypoint = false;
                        assert(!weakSeal.expired());
                        Negative::uncertainIdleResult = true;
                        device->waitIdle();
                        Negative::uncertainIdleResult = false;
                        assert(!weakSeal.expired());
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            q, [&]() noexcept -> const char* {
                                device->waitIdle();
                                return nullptr;
                            }));
                        assert(command->submittedFenceCompleted());
                        Negative::withholdCompletion = false;
                    } else {
                        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                            q, [&]() noexcept -> const char* {
                                return command->waitSubmittedFenceUntil(
                                           std::chrono::steady_clock::now()
                                               + std::chrono::seconds(5),
                                           diagnostic)
                                    ? nullptr
                                    : diagnostic.c_str();
                            }));
                    }
                    retireCompletedVulkanRhiSubmittedCommands();
                    if (terminal < 2u) {
                        auto completed = object->observeOriginalContent();
                        assert(completed);
                        assert(completed->state()
                                == common::OriginalTextureContentControl::State::Completed
                            && completed->contentGeneration() == 1u);
                        assert(object->observeOriginalCommittedState(upload.subresources)->state()
                            == ResourceState::CopyDst);
                        assert(root->status() == Root::Status::Committed);
                        if (publish) {
                            const auto revision = set->contentRevision();
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, viewSource, fresh, publication)
                                    .success);
                            auto forged = std::shared_ptr<const OriginalCommandTextureViewSource>(
                                q[5], viewSource.get());
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, forged, *completed, publication)
                                    .success);
                            forged.reset();
                            auto small = publication;
                            small.maxNativeImageViews = 0u;
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, viewSource, *completed, small)
                                    .success);
                            auto wrong = sampled;
                            wrong.resourceState = ResourceState::ShaderWrite;
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, wrong, viewSource, *completed, publication)
                                    .success);
                            {
                                auto foreign = createVulkanRhiDevice(desc);
                                assert(foreign);
                                require(foreign->prepareOriginalDeviceLifetime(
                                    {1u, true, 1u, 2u, cpuBytes}, q[7]));
                                require(foreign->prepareOriginalResourceRegistration(
                                    {1u, true, 1u, 0u, 0u, 5u, cpuBytes}, q[8]));
                                assert(
                                    !set->writeOriginalCompletedTexture(
                                            *foreign, sampled, viewSource, *completed, publication)
                                        .success);
                            }
                            assert(
                                set->contentRevision() == revision && !set->hasWrittenBinding(0u));
                            assert(
                                set->writeOriginalCompletedTexture(
                                       *device, sampled, viewSource, *completed, publication)
                                    .success);
                            assert(set->contentRevision() == revision + 1u
                                && set->validateBindingElement(0u, 0u).success);
                            const char* observationId = nullptr;
                            assert(set->observeSuccessfulBindingWrite(0u, 0u, observationId)
                                && !observationId);
                            assert(!set->write(sampled)
                                    .success); // No ordinary exclusive-content bypass.
                            if (samplerLease) {
                                DescriptorBindingWrite sw;
                                sw.binding = 1u;
                                sw.type = DescriptorType::Sampler;
                                sw.resourceKind = DescriptorResourceKind::Sampler;
                                sw.resource = samplerLease->resource();
                                sw.resourceState = ResourceState::Undefined;
                                assert(set->write(sw).success
                                    && set->validateBindingElement(1u, 0u).success);
                                const auto nativeSampler =
                                    vulkanRhiNativePayload(*samplerLease->originalObject());
                                assert(
                                    nativeSampler && nativeSampler->backendVulkanSamplerHandle());
                                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                    q, [&]() noexcept -> const char* {
                                        return OriginalCommandSamplerLease::validateOriginal(
                                            samplerLease);
                                    }));
                            }
                            if (snapshot) {
                                assert(snapshot
                                        ->writeOriginalCompletedTexture(snapshotHandle,
                                            *device,
                                            viewSource,
                                            *completed,
                                            publication)
                                        .success);
                                assert(!snapshot
                                        ->writeTexture(snapshotHandle,
                                            *viewSource->originalObject(),
                                            ResourceState::ShaderRead)
                                        .success);
                                assert(!snapshot->initializeInactiveSlots(snapshotHandle).success);
                                assert(snapshot->initializeInactiveSlots(snapshotHandle, *device)
                                        .success);
                                // Revalidation includes the typed source and every
                                // physical padding alias; this partial fixture does
                                // not authorize the unpopulated six-class snapshot.
                                assert(snapshot->initializeInactiveSlots(snapshotHandle, *device)
                                        .success);
                                assert(!snapshot->descriptorSet());
                            }
                            // Native descriptor storage owns authentic provenance even
                            // after every caller Source/view owner is dropped.
                            std::weak_ptr<const OriginalCommandTextureViewSource> weakView =
                                viewSource;
                            auto nativeSet = vulkanRhiNativePayload(*set);
                            assert(nativeSet);
                            require(device->detachOriginalDeviceNativeLoss());
                            if (samplerLease) {
                                require(
                                    OriginalCommandSamplerLease::validateOriginal(samplerLease));
                                assert(device->validateOriginalResourceRegistration(
                                    *samplerLease->originalObject()));
                                assert(!device->createSampler({}));
                                // Ordinary binding metadata is not original use
                                // authority. The original set/sampler tickets
                                // and Root inventory gates must reject use.
                                assert(device->validateOriginalResourceRegistration(*set));
                                samplerLease.reset();
                            }
                            assert(!set->validateBindingElement(0u, 0u).success);
                            const auto retiredRevision = set->contentRevision();
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, viewSource, *completed, publication)
                                    .success);
                            assert(set->contentRevision() == retiredRevision);
                            viewSource.reset();
                            viewLease.reset();
                            snapshot.reset();
                            set.reset();
                            arena.reset();
                            assert(!weakView.expired());
                            nativeSet.reset();
                            assert(weakView.expired());
                        }
                    } else {
                        assert(root->status() == Root::Status::Invalidated
                            && !object->observeOriginalContent());
                    }
                    assert(!command->submit(diagnostic));
                    command.reset();
                }
                assert(weakSeal.expired()
                    && !inputs->originalScalars()
                        ->originalUsage()
                        ->originalSources()
                        ->originalReservedSubmissionCount());
                std::printf(
                    "BINDLESS_VULKAN_UPLOAD_FENCE7_CASE layers=%u drop=%u terminal=%u packed=%llu native-staging=%llu texture=%llu proposed-generation=1\n",
                    layers,
                    drop,
                    terminal,
                    static_cast<unsigned long long>(packedBytes),
                    static_cast<unsigned long long>(nativeStaging),
                    static_cast<unsigned long long>(object->id().value));
            }
            assert(device->memoryStats().allocatedBytes == gpuBaseline);
            assert(q[13]->liveBytes() == stagingRegistryBytes);
            // Texture registry capacity is also released at device teardown.
            for (const auto index :
                {4u, 5u, 7u, 8u, 9u, 10u, 11u, 12u, 14u, 16u, 17u, 18u, 19u, 20u})
                assert(!q[index]->liveBytes());
        }
        for (const auto& owner : q)
            assert(!owner->liveBytes());
    }
};
} // namespace arp::test
