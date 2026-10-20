#pragma once
#include "completed_texture_root_program_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "original_graphics_hdr_gi_data_fixture.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_bindless_command.h"
#include "rhi/backends/metal/metal_original_texture_upload.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace arp::test {
class MetalCompletedTextureUploadFixture {
protected:
    static constexpr std::uint64_t cpuBytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(cpuBytes); }

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Metal completed Root8: %s\n", id);
        assert(!id);
    }

public:
    static constexpr bool enabled = true, completedTextures = true;

    static constexpr std::uint32_t completedTexturePublicationVersion() noexcept { return 1u; }

    bool arrayTextures = false;
    bool cubeTextures = false;
    // Each original table still declares its own dimension and layer count.
    bool mixed2DArrayTextures = false;
    void (*initializeTexturePixels)(
        std::uint32_t, std::uint32_t, core::pmr::Vector<std::uint8_t>&) = nullptr;
    std::uint64_t nativeStagingCeiling = 16u * 1024u * 1024u;
    bool observeOriginalNativeAllocation = false;

    void prepareOriginalDevice(rhi::GpuDevice& device) const
    {
        require(
            device.prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, cpuBytes}, quota()));
    }

    void configureRegistration(rhi::OriginalResourceRegistrationBudget& b) const
    {
        b.contractVersion = 9u;
        ++b.maxBuffers;
        b.maxRetainedOwners += rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
    }

    void prepareRegistration(rhi::GpuDevice& device,
        const rhi::OriginalResourceRegistrationBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& owner) const
    {
        const char* id = nullptr;
        const auto loss = device.observeOriginalDeviceNativeLoss(id);
        require(id);
        assert(loss);
        require(device.prepareOriginalDescriptorNativeLossRegistration(
            {2u, true}, budget, owner, *loss));
    }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& b) const
    {
        b.maxCommands = b.maxReferences = 32u;
        b.maxInputDescriptors = 16u;
        b.maxStateObservations = 64u;
        b.maxTextureRanges = 16u;
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table&) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = arrayTextures ? 64u : 24u;
    }

    void configureSampler(rhi::SamplerDesc& desc, std::uint32_t index) const
    {
        desc.minFilter = desc.magFilter =
            index == 0u ? rhi::SamplerFilter::Nearest : rhi::SamplerFilter::Linear;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        (void)index;
        const auto native = rhi_metal::metalRhiNativePayloadFor(buffer);
        assert(native);
        std::array<std::uint8_t, 64u> zeros{};
        core::pmr::String diagnostic;
        assert(rhi_metal::writeMetalRhiNativeBuffer(native,
            zeros.data(),
            buffer.desc().sizeBytes,
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
    }

    std::shared_ptr<const rhi::OriginalCommandTextureLease> createCompletedTexture(
        rhi::GpuDevice& device,
        rhi::TextureDesc& td,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        std::uint32_t index) const
    {
        using namespace rhi;
        td.mipLevelCount = 2u;
        td.initialState = ResourceState::Undefined;
        td.usage = TextureUsage::CopyDst | TextureUsage::Sampled;
        const auto layers = td.arrayLayerCount;
        assert(mixed2DArrayTextures ? !cubeTextures && (layers == 1u || layers == 2u)
                                    : layers
                    == (cubeTextures        ? 6u
                            : arrayTextures ? 2u
                                            : 1u));
        assert(td.cubeCompatible == cubeTextures);
        const bool hdr = td.format == TextureFormat::Rgba16Float;
        assert(hdr || td.format == TextureFormat::Rgba8Unorm);
        const auto texelBytes = hdr ? 8u : 4u;
        const auto pixelBytes = 5u * texelBytes * layers;
        core::pmr::Vector<std::uint8_t> pixels(pixelBytes, 0u);
        if (hdr)
            OriginalGraphicsHdrGiDataFixture::initializeTexturePixels(index, layers, pixels);
        else
            OriginalGraphicsColorGiDataFixture::initializeTexturePixels(index, layers, pixels);
        if (initializeTexturePixels)
            initializeTexturePixels(index, layers, pixels);
        UploadTextureDesc upload;
        upload.extent = td.extent;
        upload.format = td.format;
        upload.subresources = defaultTextureSubresourceRange(td);
        upload.bytesPerRow = 2u * texelBytes;
        upload.rowsPerImage = 2u;
        upload.pixelData = pixels.data();
        upload.pixelDataSize = pixels.size();
        upload.mipLayouts = {{0u, {2u, 2u, 1u}, 2u * texelBytes, 2u, 0u},
            {1u, {1u, 1u, 1u}, texelBytes, 1u, 4u * texelBytes * layers}};
        return createCompletedTextureUpload(device, td, sourceQuota, std::move(upload));
    }

    // Authored shapes and payload; shared normative mapper computes packing.
    // Reuses the same original Metal staging, journal and native completion.
    std::shared_ptr<const rhi::OriginalCommandTextureLease> createCompletedTextureUpload(
        rhi::GpuDevice& device,
        const rhi::TextureDesc& td,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        rhi::UploadTextureDesc upload,
        bool verifyBudgetBoundaries = false) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        using Staging = MetalOriginalTextureUploadStaging;
        using K = CommandUsageKind;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 13u> q;
        for (auto& owner : q)
            owner = quota();
        const char* id = nullptr;
        const auto pixelBytes = upload.pixelDataSize;
        const auto mipCount = td.mipLevelCount;
        const auto layers = td.arrayLayerCount;
        const auto cells = mipCount * layers;
        const bool hdr = td.format == TextureFormat::Rgba16Float;
        const auto pixelCpuBytes = sourceQuota->limitBytes();
        const auto bytes = device.queryOriginalContentTextureAllocation(td, id);
        require(id);
        assert(bytes);
        core::UniquePtr<GpuTexture> texture;
        OriginalTextureContentCreationBudget creation{
            3u, true, 1u, 1u, pixelBytes, 4u, cpuBytes, 0u, bytes};
        if (observeOriginalNativeAllocation) {
            const auto memory = device.memoryStats();
            assert(memory.budgetCapacityBytes > memory.allocatedBytes);
            creation.maxNativeAllocationBytes = memory.budgetCapacityBytes - memory.allocatedBytes;
            creation.nativeAllocationObservation =
                OriginalNativeAllocationObservationContract::Intent{1u, true};
        }
        {
            core::CpuMemoryQuotaScope scope(q[0]);
            texture = device.createOriginalContentTexture(td, creation, q[1], id);
        }
        require(id);
        assert(texture);
        if (observeOriginalNativeAllocation && verifyBudgetBoundaries) {
            const auto baseline = device.memoryStats();
            const auto observed = texture->allocation().estimatedSizeBytes;
            assert(observed && observed <= creation.maxNativeAllocationBytes);
            auto oneLess = creation;
            oneLess.maxNativeAllocationBytes = observed - 1u;
            {
                core::CpuMemoryQuotaScope scope(q[0]);
                assert(!device.createOriginalContentTexture(td, oneLess, q[1], id));
            }
            assert(id && device.memoryStats().allocatedBytes == baseline.allocatedBytes
                && device.memoryStats().liveResourceCount == baseline.liveResourceCount);
            std::printf(
                "NATIVE_ALLOCATION_OBSERVATION1_METAL_OK queried%llu observed%llu reserved%llu one-less-refused preserved-ledger\n",
                static_cast<unsigned long long>(bytes),
                static_cast<unsigned long long>(observed),
                static_cast<unsigned long long>(creation.maxNativeAllocationBytes));
        }
        auto lease = OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, pixelCpuBytes}, sourceQuota, id);
        require(id);
        assert(lease);
        upload.destinationTexture = lease->resource();
        const TextureUploadInputSnapshotBudget inputBudget{
            1u, true, mipCount, 0u, pixelBytes, cpuBytes, cpuBytes, pixelCpuBytes};
        if (verifyBudgetBoundaries) {
            const auto baseline = sourceQuota->liveBytes();
            auto shortBudget = inputBudget;
            --shortBudget.maxPixelBytes;
            assert(!TextureUploadInputSnapshot::prepareOriginal(
                upload, shortBudget, q[2], q[2], sourceQuota, id));
            assert(id && !q[2]->liveBytes() && sourceQuota->liveBytes() == baseline);
        }
        auto input = TextureUploadInputSnapshot::prepareOriginal(
            upload, inputBudget, q[2], q[2], sourceQuota, id);
        require(id);
        assert(input);
        auto source = OriginalCommandTextureUploadSource::prepareOriginal(
            input, lease, {1u, true, 5u, pixelCpuBytes}, sourceQuota, id);
        require(id);
        assert(source);
        std::uint64_t packedBytes = 0u;
        require(semantics::TextureUploadLayoutValidation::inspectPackedBytes(
            *input, TextureUploadPacking::Destination::capture(td), packedBytes));
        const auto packedCpuBytes = packedBytes + cpuBytes;
        q[4] = core::makeShared<core::CpuMemoryQuota>(packedCpuBytes);
        const TextureUploadPackingBudget packingBudget{
            1u, true, mipCount, packedBytes, cpuBytes, packedCpuBytes};
        if (verifyBudgetBoundaries) {
            auto shortBudget = packingBudget;
            --shortBudget.maxPackedBytes;
            assert(!TextureUploadPacking::prepareOriginal(input, td, shortBudget, q[3], q[4], id));
            assert(id && !q[3]->liveBytes() && !q[4]->liveBytes());
        }
        auto packing =
            TextureUploadPacking::prepareOriginal(input, td, packingBudget, q[3], q[4], id);
        require(id);
        assert(packing);
        auto packed = OriginalCommandTextureUploadPackingSource::prepareOriginal(source,
            packing,
            {1u,
                true,
                mipCount,
                OriginalCommandTextureUploadPackingSource::retainedOwnerCount,
                pixelBytes,
                packedBytes,
                cpuBytes},
            q[5],
            id);
        require(id);
        assert(packed);
        // Native staging capacity is bounded by the actual device budget;
        // logical packing follows the shared row-pitch contract.
        auto staging = Staging::prepareOriginal(packed,
            device,
            {hdr ? 2u : 1u,
                true,
                1u,
                mipCount,
                Staging::retainedOwnerBound,
                packedBytes,
                nativeStagingCeiling,
                cpuBytes,
                cpuBytes,
                pixelCpuBytes},
            {q[6], q[7], sourceQuota},
            id);
        require(id);
        assert(staging);
        const std::array<OriginalCommandResourceSources::Source, 3u> retained{
            lease, source, staging->originalStagingLease()};
        const std::array<CommandUsage, 6u> commands{{{K::BufferMemoryBarrier, 0u, 1u},
            {K::TextureBarrier, 1u, 1u},
            {K::BeginTransferPass, 2u, 0u},
            {K::TextureUpload, 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        const std::array<OriginalCommandUsage::Reference, 4u> refs{{{2u}, {0u}, {0u}, {1u}}};
        ResourceMemoryBarrierDesc memory;
        memory.resource = staging->originalStagingLease()->resource();
        memory.sourceStages = memory.destinationStages = PipelineStage::Transfer;
        memory.sourceAccess = memory.destinationAccess = ResourceAccess::TransferRead;
        ResourceBarrierDesc barrier{lease->resource(),
            upload.subresources,
            ResourceState::Undefined,
            ResourceState::CopyDst};
        barrier.sourceStages = PipelineStage::TopOfPipe;
        barrier.destinationStages = PipelineStage::Transfer;
        barrier.destinationAccess = ResourceAccess::TransferWrite;
        const std::array<OriginalCommandInputs::OriginalDescriptor, 3u> descriptors{
            {{0u, *ResourceDependencyArguments::capture(K::BufferMemoryBarrier, memory)},
                {1u, *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)},
                {3u, *TextureUploadOriginalInputArguments::captureOriginal(input)}}};
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxStateObservations = 3u;
        admission.maxTextureRanges = 2u;
        admission.maxMipMappings = mipCount;
        admission.maxStateTransactions = 1u;
        admission.maxRetainedUploadBytes = pixelBytes;
        admission.maxPendingSubmissions = admission.maxNativePayloadObjects = 1u;
        admission.maxTypedNativeCommandCalls = cells + 4u;
        admission.maxNativeStagingBytes = staging->nativeStagingBytes();
        using D = OriginalCommandAdmissionBudget::CpuDomain;
        admission.maxCpuBytes[static_cast<std::size_t>(D::Recording)] = cpuBytes;
        admission.maxCpuBytes[static_cast<std::size_t>(D::Submission)] = cpuBytes;
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepare(
            retained, commands, refs, descriptors, admission, sourceQuota, q[2], id);
        require(id);
        assert(inputs);
        // Two authored texture observations: barrier and whole upload. This
        // count is independent of the number of mip mappings inside the copy.
        const OriginalCommandStateAdmissionBudget state{1u, true, 1u, 2u, 3u, 2u, 128u, cpuBytes};
        auto graph =
            OriginalCommandResourceStates::prepareOriginal(inputs, device, state, q[9], id);
        require(id);
        assert(graph);
        auto tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, state, q[10], id);
        require(id);
        assert(tx);
        require(tx->materializeOriginalPrototypePlan(
            {1u, true, 6u, 0u, 1u + 2u * cells, 1u + 2u * cells},
            {7u, true, 1u, 1u, 0u, 2u, 6u, 20u}));
        OriginalCommandStreamBudget streamBudget;
        streamBudget.contractVersion = 1u;
        streamBudget.prototypeOptIn = true;
        streamBudget.maxCommands = 6u;
        streamBudget.maxDescriptors = 3u;
        streamBudget.maxReferences = 4u;
        streamBudget.maxTextureUploadMips = mipCount;
        streamBudget.maxTextureUploadPixelBytes = pixelBytes;
        streamBudget.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
        streamBudget.maxCpuMetadataBytes = cpuBytes;
        auto program =
            OriginalCommandStreamProgram::prepareOriginal(inputs, streamBudget, q[11], id);
        require(id);
        assert(program);
        OriginalCommandNativeLifecycleBudget life{7u,
            true,
            1u,
            0u,
            0u,
            tx->retainedOwnerCount() + 2u,
            tx->retainedOwnerCount() + program->retainedOwnerCount() + staging->retainedOwnerCount()
                + 6u,
            0u,
            cpuBytes,
            cpuBytes,
            0u};
        auto seal = OriginalCommandRecordingSeal::prepareOriginal(tx, device, life, q[12], {}, id);
        require(id);
        assert(seal);
        auto reservation =
            OriginalCommandLifecycleReservation::prepareOriginal(seal, {3u, true, 1u, 0u, 0u}, id);
        require(id);
        assert(reservation);
        core::pmr::String diagnostic;
        auto command = MetalOriginalTextureUploadCommand::prepareOriginal(
            reservation, program, staging, device, diagnostic);
        if (!command)
            std::fprintf(stderr, "Root texture upload: %s\n", diagnostic.c_str());
        assert(command);
        const auto done = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, done, {}, 0u, {}, 0u, diagnostic));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done->isComplete() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        assert(
            done->isComplete() && done->outcome() == MetalRhiCompletionState::Outcome::Completed);
        assert(tx->status() == OriginalCommandStateTransaction::Status::Committed);
        assert(lease->originalObject()->observeOriginalContent()->contentGeneration() == 1u);
        return lease;
    }
};

class MetalCompletedTextureRootFixture final : public MetalCompletedTextureUploadFixture {
public:
    bool fault = false, repeatAfterCompletion = true;
    // 0 normal, 1 cancellation, 2 caller drop, 3 in-flight retirement, 4 before submit.
    unsigned terminal = 0u;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        request.originalExecution = {8u, true};
    }

    bool originalDeviceLifetimeRetired() const noexcept { return terminal == 3u || terminal == 4u; }

    template <class Result, class Request>
    void verifyRetiredResult(const Result& result, const Request& request) const
    {
        assert(!result && !request);
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(
        const Root&, const Inventory&, const Binding&, const Transaction&, rhi::GpuDevice&) const
    {
        assert(false && "Completed color uses the shared Root program observer");
    }

    void verifyOutputs(const rhi::OriginalCommandBindlessRootSource& root) const
    {
        using namespace rhi_metal;
        core::pmr::String diagnostic;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding != 4u)
                continue;
            const auto index = element.declaration.write.arrayElement;
            const auto& source =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(element.owner);
            const auto native = metalRhiNativePayloadFor(*source->originalObject());
            assert(native);
            std::array<std::uint32_t, 16u> values{};
            const auto bytes = arrayTextures ? 64u : 24u;
            assert(readMetalRhiNativeBuffer(native, values.data(), bytes, 0u, bytes, diagnostic));
            for (std::uint32_t layer = 0u; layer < (arrayTextures ? 2u : 1u); ++layer) {
                const auto base = arrayTextures ? layer * 8u : 0u;
                float loaded = 0.f, sampled = 0.f, alpha = 0.f, mip = 0.f;
                std::memcpy(&loaded, &values[base], 4u);
                std::memcpy(&sampled, &values[base + 2u], 4u);
                std::memcpy(&alpha, &values[base + 4u], 4u);
                std::memcpy(&mip, &values[base + 5u], 4u);
                assert(values[base + 1u] == 2u && values[base + 3u] == 2u && alpha == 1.f);
                assert(
                    std::abs(loaded - float((index + 1u) * (16u + 8u * layer)) / 255.f) < 0.00001f);
                assert(std::abs(sampled
                           - float((index == 0u ? 40u : 128u) + (index + 1u) * 8u * layer) / 255.f)
                    < 0.00001f);
                assert(std::abs(mip - float((index + 1u) * (32u + 8u * layer)) / 255.f) < 0.00001f);
                if (arrayTextures) {
                    float mipLoaded = 0.f;
                    std::memcpy(&mipLoaded, &values[base + 7u], 4u);
                    assert(values[base + 6u] == 2u && mipLoaded == mip);
                }
            }
            char name[128u];
            std::snprintf(name,
                sizeof(name),
                "metal-public-color8-array%d-fault%d-terminal%u-result%u.bin",
                arrayTextures,
                fault,
                terminal,
                index);
            auto* file = std::fopen(name, "wb");
            assert(file);
            assert(std::fwrite(values.data(), 1u, bytes, file) == bytes);
            assert(!std::fclose(file));
        }
    }

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using namespace rhi_metal;
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        {
            assert(!metalRhiNativePayloadFor(*root->originalObject()));
            const auto prepared = std::get<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                root->originalPrepared());
            auto encoder =
                device->createCommandEncoder({"Completed color standalone refusal", 1u, 1u});
            assert(encoder);
            auto pass = encoder->beginComputePass({"Completed color standalone refusal"});
            assert(pass);
            assert(pass->setPipeline(*prepared->owners()->desc().pipeline).success);
            auto* indexed =
                static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
            assert(indexed);
            core::pmr::Vector<BindlessResourceAccess> accesses;
            for (std::size_t i = 0u; i < root->size(); ++i) {
                BindlessResourceAccess access;
                assert(root->read(i, access));
                accesses.push_back(access);
            }
            const auto rejected = indexed->bindPreparedComputeBinding(
                prepared, BindlessAccessArray(accesses.data(), accesses.size()));
            assert(!rejected.success
                && rejected.diagnostics == "RHI-METAL-COMPLETED-TEXTURE-ORIGINAL-ROOT-REQUIRED");
            assert(pass->end().success);
        }
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 8u> q;
        for (auto& owner : q)
            owner = quota();
        auto built = core::makeUnique<CompletedTextureRootProgramFixture::Prepared>(
            CompletedTextureRootProgramFixture::prepare(root, inventory, device, q, 1u, cpuBytes));
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        assert(extension);
        const auto entries = static_cast<std::uint32_t>(root->elements().size() + 1u);
        OriginalBindlessRootFenceExecutionInputs inputs{
            root, inventory, built->tx, built->binding, q[5], q[6], q[7]};
        OriginalBindlessRootFenceExecutionBudget budget{{8u, true},
            {1u, true, entries, 512u, 1024u, cpuBytes},
            {8u,
                true,
                1u,
                0u,
                0u,
                built->tx->retainedOwnerCount() + 2u,
                4096u,
                0u,
                cpuBytes,
                cpuBytes,
                0u},
            {4u, true, 1u, 0u, 0u},
            cpuBytes,
            256u};
        budget.requireWorkloadRetention = true;
        core::pmr::String diagnostic;
        diagnostic.reserve(256u);
        const auto refused = [&](const auto& b) {
            assert(!extension->prepareOriginalRootExecution(*device, inputs, b, diagnostic));
            assert(!diagnostic.empty()
                && built->tx->status() == OriginalCommandStateTransaction::Status::Prepared
                && !built->result->publishedBytes());
            for (auto i : {5u, 6u, 7u})
                assert(!q[i]->liveBytes());
        };
        auto wrong = budget;
        wrong.intent.contractVersion = 7u;
        refused(wrong);
        wrong = budget;
        --wrong.closure.maxResourceEntries;
        refused(wrong);
        wrong = budget;
        wrong.closure.maxNativeOwners = 1u;
        refused(wrong);
        for (auto i : {5u, 6u, 7u}) {
            // Only one transaction may occupy this inventory's admitted slot.
            // A one-shot seal followed by failure abandons the unsubmitted plan.
            {
                core::CpuMemoryQuotaLease pressure(q[i], cpuBytes);
                assert(pressure.charged());
                assert(
                    !extension->prepareOriginalRootExecution(*device, inputs, budget, diagnostic));
                assert(built->tx->status()
                    == (i == 6u ? OriginalCommandStateTransaction::Status::Abandoned
                                : OriginalCommandStateTransaction::Status::Prepared));
                assert(!built->result->publishedBytes());
            }
            inputs.transaction.reset();
            inputs.readback.reset();
            built.reset();
            built = core::makeUnique<CompletedTextureRootProgramFixture::Prepared>(
                CompletedTextureRootProgramFixture::prepare(
                    root, inventory, device, q, 1u, cpuBytes));
            inputs.transaction = built->tx;
            inputs.readback = built->binding;
        }
        auto execution =
            extension->prepareOriginalRootExecution(*device, inputs, budget, diagnostic);
        if (!execution)
            std::fprintf(stderr, "Metal public color prepare: %s\n", diagnostic.c_str());
        assert(execution);
        assert(OriginalBindlessRootExecution::record(execution, {7u, true}));
        require(OriginalBindlessRootExecution::record(execution, {8u, true}));
        assert(execution->recordingProven()
            && built->tx->status() == OriginalCommandStateTransaction::Status::Pending);
        assert(!built->result->publishedBytes());
        if (terminal == 1u || terminal == 4u) {
            if (terminal == 4u) {
                device->retireOriginalDeviceLifetime();
                assert(OriginalBindlessRootExecution::submit(execution, {8u, true}));
            }
            require(execution->cancelExecution());
            execution.reset();
            assert(!built->result->publishedBytes());
        } else {
            auto gate = createMetalRhiNativeSharedEventPayload("public-color-gate");
            auto blocker = createMetalRhiNativeCommandPayload("public-color-blocker", {});
            assert(gate && blocker);
            auto blocked = blocker->preparedCompletionState();
            MetalRhiNativeQueueStats queue;
            assert(blocker->finish(diagnostic)
                && blocker->submit(queue, blocked, gate, 1u, {}, 0u, diagnostic));
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                return OriginalBindlessRootExecution::submit(execution, {8u, true});
            }));
            assert(execution->pendingRecords() == 1u && !built->result->publishedBytes());
            if (terminal == 2u)
                execution.reset();
            if (terminal == 3u)
                device->retireOriginalDeviceLifetime();
            const auto pressureBytes = q[5]->limitBytes() - q[5]->liveBytes();
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                signalMetalRhiNativeSharedEvent(gate, 1u);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (q[5]->liveBytes() != pressureBytes) {
                    if (std::chrono::steady_clock::now() > deadline)
                        return "METAL-COLOR-COMPLETION-TIMEOUT";
                    std::this_thread::yield();
                }
                return nullptr;
            }));
            blocked->wait();
            assert(built->tx->status()
                == (terminal == 3u ? OriginalCommandStateTransaction::Status::Invalidated
                                   : OriginalCommandStateTransaction::Status::Committed));
            assert(bool(built->result->publishedBytes()) == (terminal != 3u));
            if (terminal != 3u) {
                std::uint32_t bits = 0u;
                std::memcpy(&bits, built->result->publishedBytes(), 4u);
                assert(bits == std::uint32_t(fault));
                verifyOutputs(*root);
            }
            if (execution) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (execution->pendingRecords()) {
                    assert(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::yield();
                }
                assert(execution->submissionCompleted() == (terminal != 3u));
            }
            execution.reset();
        }
        for (auto i : {5u, 6u, 7u})
            assert(!q[i]->liveBytes());
        assert(!root->originalSnapshot()->originalSubmissionBudgetOwner()->used());
        if (terminal < 3u)
            for (const auto& input : built->completedInputs)
                require(input.first->validateOriginalContent(input.second));
        std::printf(
            "BINDLESS_METAL_PUBLIC_COLOR8 array=%d fault=%d terminal=%u same-journal hot-submit0 hot-completion0 retirement0\n",
            arrayTextures,
            fault,
            terminal);
        if (!terminal && !fault && repeatAfterCompletion) {
            inputs = {};
            built.reset();
            auto next = *this;
            next.repeatAfterCompletion = false;
            next(root, inventory, device);
        }
    }
};
} // namespace arp::test
