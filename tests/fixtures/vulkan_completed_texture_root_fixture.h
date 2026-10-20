#pragma once
#include "completed_texture_root_program_fixture.h"
#include "core/ibl_bake.h"
#include "original_graphics_hdr_gi_data_fixture.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "vulkan_original_root_terminal_fixture.h"
#include "vulkan_original_texture_upload_fixture.h"

#include <cmath>
#include <cstring>

namespace arp::test {
// Genuine upload + sampled Root integration. Reuses the original upload
// staging/packing/stream builders and the Root factory; no ordinary upload API.
class VulkanCompletedTextureRootFixture {
    static constexpr std::uint64_t cpuBytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(cpuBytes); }

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "CompletedTextureRoot8: %s\n", id);
        assert(!id);
    }

public:
    static constexpr bool enabled = true;
    static constexpr bool completedTextures = true;

    static constexpr std::uint32_t completedTexturePublicationVersion() noexcept { return 1u; }

    void verifyColorOutputs(const rhi::OriginalCommandBindlessRootSource& root) const
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        core::pmr::String diagnostic;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding != 4u)
                continue;
            const auto& buffer =
                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(element.owner);
            const auto native = vulkanRhiNativePayload(*buffer->originalObject());
            assert(native);
            const auto bytes = arrayTextures ? 64u : 24u;
            assert(native->backendInvalidateMappedRead(0u, bytes, diagnostic));
            std::array<std::uint32_t, 16u> values{};
            std::memcpy(values.data(), native->backendMappedData(), bytes);
            const auto index = element.declaration.write.arrayElement;
            for (std::uint32_t layer = 0u; layer < (arrayTextures ? 2u : 1u); ++layer) {
                const auto base = arrayTextures ? layer * 8u : 0u;
                float sampled = 0.f, mip = 0.f, loaded = 0.f, alpha = 0.f;
                std::memcpy(&sampled, &values[base + 2u], 4u);
                std::memcpy(&mip, &values[base + 5u], 4u);
                std::memcpy(&loaded, &values[base], 4u);
                std::memcpy(&alpha, &values[base + 4u], 4u);
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
        }
    }

    bool arrayTextures = false;
    // Mixed dimensions still come from each original table's explicit layout.
    bool mixed2DArrayTextures = false;
    bool cubeTextures = false;
    bool fault = false;
    bool abandon = false;
    bool repeatAfterCompletion = true;
    bool terminalCases = false;
    VulkanOriginalRootTerminalScenario terminal;
    std::uint32_t dispatchX = 1u, expectedSamplerOwners = 2u;
    void (*verifyOutputs)(const rhi::OriginalCommandBindlessRootSource&) = nullptr;
    void (*initializeTexturePixels)(
        std::uint32_t, std::uint32_t, core::pmr::Vector<std::uint8_t>&) = nullptr;
    std::uint64_t nativeStagingCeiling = 0u;
    mutable std::array<std::shared_ptr<rhi_vulkan::VulkanRhiNativeResourcePayload>, 2u> outputs;

    template <class Result, class Request>
    void verifyRetiredResult(const Result& result, const Request& request) const
    {
        assert(!result && !request);
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(
        const Root&, const Inventory&, const Binding&, const Transaction&, rhi::GpuDevice&) const
    {
        assert(false
            && "Completed texture execution uses the shared Root observer, not the CPU-state fixture branch");
    }

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
        b.maxCommands = 32u;
        b.maxReferences = 32u;
        b.maxInputDescriptors = 16u;
        b.maxStateObservations = 64u;
        b.maxTextureRanges = 16u;
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table&) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = arrayTextures ? 64u : 24u;
    }

    void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t, const rhi::DescriptorBindingDesc&) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
    }

    void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding) const
    {
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
        std::array<std::uint32_t, 4u> words{};
        if (set == 0u && binding.binding == 3u) {
            const float frame = 32.0f;
            std::memcpy(words.data(), &frame, sizeof(frame));
        } else if (set == 2u && binding.binding == 3u)
            words[0] = 17u;
        else
            assert(set == 2u && binding.binding == 4u);
        assert(buffer.desc().sizeBytes == sizeof(words));
        std::memcpy(native->backendMappedData(), words.data(), sizeof(words));
        core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, sizeof(words), diagnostic));
    }

    template <bool Array>
    static void verifyGraphicsOrdinaryBuffers(const rhi::OriginalCommandBindlessRootSource& root)
    {
        using namespace rhi;
        assert(root.sourceContractVersion() == 2u && root.ordinaryDomains().size() == 2u);
        std::uint32_t count = 0u;
        for (const auto& domain : root.ordinaryDomains())
            for (const auto& element : domain.source->frozenElements()) {
                const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                    element.resourceOwner);
                const auto native = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
                assert(native);
                core::pmr::String diagnostic;
                assert(native->backendInvalidateMappedRead(0u, 16u, diagnostic));
                std::array<std::uint32_t, 4u> words{};
                std::memcpy(words.data(), native->backendMappedData(), sizeof(words));
                if (domain.set == 0u) {
                    float frame = 0.f;
                    std::memcpy(&frame, words.data(), sizeof(frame));
                    assert(frame == 32.f);
                } else if (element.declaration.write.binding == 3u)
                    assert(words[0] == 17u);
                else {
                    assert(domain.set == 2u && element.declaration.write.binding == 4u);
                    assert(
                        (words == std::array<std::uint32_t, 4u>{98u, Array ? 0u : 98u, 98u, 98u}));
                    std::printf("BINDLESS_VULKAN_SAMPLED_GRAPHICS_ORDINARY_WORDS array%u words",
                        Array ? 1u : 0u);
                    for (const auto word : words)
                        std::printf(" %u", word);
                    std::puts(" Root2 groups3 Vertex-Fragment same-journal real-fence");
                }
                ++count;
            }
        assert(count == 3u);
    }

    void configureSampler(rhi::SamplerDesc& desc, std::uint32_t index) const
    {
        desc.minFilter = desc.magFilter =
            index == 0u ? rhi::SamplerFilter::Nearest : rhi::SamplerFilter::Linear;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        outputs[index] = rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(outputs[index] && outputs[index]->backendMappedData());
        std::memset(outputs[index]->backendMappedData(), 0, buffer.desc().sizeBytes);
        core::pmr::String diagnostic;
        assert(outputs[index]->backendFlushMappedWrite(0u, buffer.desc().sizeBytes, diagnostic));
    }

    std::shared_ptr<const rhi::OriginalCommandTextureLease> createCompletedTexture(
        rhi::GpuDevice& device,
        rhi::TextureDesc& td,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        std::uint32_t index) const
    {
        using namespace rhi;
        using namespace rhi_vulkan;
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

    // General upload composition shared by small domain tests and actual-size IBL.
    // Shape/pixels come from explicit metadata; the normative mapper sizes packing.
    std::shared_ptr<const rhi::OriginalCommandTextureLease> createCompletedTextureUpload(
        rhi::GpuDevice& device,
        const rhi::TextureDesc& td,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        rhi::UploadTextureDesc upload,
        bool verifyBudgetBoundaries = false) const
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        using Staging = VulkanOriginalTextureUploadStaging;
        using K = CommandUsageKind;
        const auto pixelBytes = upload.pixelDataSize;
        const auto mipCount = td.mipLevelCount;
        const auto layers = td.arrayLayerCount;
        const auto cells = mipCount * layers;
        const bool hdr = td.format == TextureFormat::Rgba16Float;
        const auto pixelCpuBytes = sourceQuota->limitBytes();
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 13u> q;
        for (auto& owner : q)
            owner = quota();
        const char* id = nullptr;
        const auto bytes = device.queryOriginalContentTextureAllocation(td, id);
        require(id);
        assert(bytes);
        core::UniquePtr<GpuTexture> texture;
        {
            core::CpuMemoryQuotaScope scope(q[0]);
            texture = device.createOriginalContentTexture(
                td, {3u, true, 1u, 1u, pixelBytes, 4u, cpuBytes, 0u, bytes}, q[1], id);
        }
        require(id);
        assert(texture);
        auto lease = OriginalCommandTextureLease::prepareOriginal(
            texture, {1u, true, 2u, pixelCpuBytes}, sourceQuota, id);
        require(id);
        assert(lease);
        upload.destinationTexture = lease->resource();
        TextureUploadInputSnapshotBudget inputBudget{
            1u, true, mipCount, 0u, pixelBytes, cpuBytes, cpuBytes, pixelCpuBytes};
        if (verifyBudgetBoundaries) {
            const auto sourceBaseline = sourceQuota->liveBytes();
            auto shortBudget = inputBudget;
            --shortBudget.maxPixelBytes;
            assert(!TextureUploadInputSnapshot::prepareOriginal(
                upload, shortBudget, q[2], q[2], sourceQuota, id));
            assert(id && q[2]->liveBytes() == 0u && sourceQuota->liveBytes() == sourceBaseline);
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
            assert(id && q[3]->liveBytes() == 0u && q[4]->liveBytes() == 0u);
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
                OriginalTextureUploadStagingContract::retainedOwnerBound,
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
        admission.maxTypedNativeCommandCalls = mipCount + 2u;
        admission.maxNativeStagingBytes = staging->nativeStagingBytes();
        using D = OriginalCommandAdmissionBudget::CpuDomain;
        admission.maxCpuBytes[static_cast<std::size_t>(D::Recording)] = cpuBytes;
        admission.maxCpuBytes[static_cast<std::size_t>(D::Submission)] = cpuBytes;
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepare(
            retained, commands, refs, descriptors, admission, sourceQuota, q[2], id);
        require(id);
        assert(inputs);
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
                + 5u,
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
        auto command = VulkanOriginalTextureUploadCommand::prepareOriginal(
            reservation, program, staging, device, diagnostic);
        if (!command)
            std::fprintf(stderr, "Root texture upload: %s\n", diagnostic.c_str());
        assert(command);
        assert(command->submit(diagnostic));
        assert(command->waitSubmittedFenceUntil(
            std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
        retireCompletedVulkanRhiSubmittedCommands();
        assert(tx->status() == OriginalCommandStateTransaction::Status::Committed);
        assert(lease->originalObject()->observeOriginalContent()->contentGeneration() == 1u);
        return lease;
    }

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        using K = CommandUsageKind;
        using Sources = OriginalCommandResourceSources;
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 7u> q;
        for (auto& owner : q)
            owner = quota();
        auto built = CompletedTextureRootProgramFixture::prepare(
            root, inventory, device, q, dispatchX, cpuBytes);
        auto& inputs = built.inputs;
        auto& graph = built.graph;
        auto& tx = built.tx;
        auto& program = built.program;
        auto& request = built.request;
        auto& result = built.result;
        auto& binding = built.binding;
        const auto& completedInputs = built.completedInputs;
        const auto textureCount = built.textureCount;
        const auto& snapshot = root->originalSnapshot();
        const char* id = nullptr;
        auto count = static_cast<std::uint32_t>(root->elements().size() + 1u);
        for (const auto& domain : root->ordinaryDomains())
            count += domain.source->frozenElements().size();
        const OriginalBindlessRootNativeClosureBudget closureBudget{
            1u, true, count, count * 2u + 2u, 512u, cpuBytes};
        if (!root->ordinaryDomains().empty()) {
            auto shortBudget = closureBudget;
            --shortBudget.maxResourceEntries;
            assert(!VulkanBindlessRootNativeClosure::prepareOriginal(
                root, inventory, *device, shortBudget, q[5], id));
            assert(core::StringView(id) == "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY"
                && !q[5]->liveBytes());
            core::CpuMemoryQuotaLease pressure(q[5], cpuBytes);
            assert(pressure.charged());
            assert(!VulkanBindlessRootNativeClosure::prepareOriginal(
                root, inventory, *device, closureBudget, q[5], id));
            assert(q[5]->liveBytes() == cpuBytes
                && tx->status() == OriginalCommandStateTransaction::Status::Prepared);
        }
        auto closure = VulkanBindlessRootNativeClosure::prepareOriginal(
            root, inventory, *device, closureBudget, q[5], id);
        require(id);
        assert(closure);
        if (!root->ordinaryDomains().empty()) {
            assert(closure->resourceEntryCount() == count);
            const auto charged = q[5]->liveBytes();
            for (const bool nativeOwners : {true, false}) {
                auto shortBudget = closureBudget;
                if (nativeOwners)
                    shortBudget.maxNativeOwners = closure->nativeOwnerCount() - 1u;
                else
                    shortBudget.maxRetainedOwners = closure->retainedOwnerCount() - 1u;
                assert(!VulkanBindlessRootNativeClosure::prepareOriginal(
                    root, inventory, *device, shortBudget, q[5], id));
                assert(core::StringView(id)
                    == (nativeOwners ? "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY"
                                     : "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY"));
                assert(q[5]->liveBytes() == charged
                    && tx->status() == OriginalCommandStateTransaction::Status::Prepared);
            }
        }
        OriginalCommandNativeLifecycleBudget life{
            8u, true, 1u, 0u, 0u, tx->retainedOwnerCount() + 2u, 512u, 0u, cpuBytes, cpuBytes, 0u};
        auto seal = OriginalCommandRecordingSeal::prepareOriginal(tx, *device, life, q[6], {}, id);
        require(id);
        assert(seal);
        assert(!OriginalCommandLifecycleReservation::prepareOriginal(
            seal, {2u, true, 1u, 0u, 0u}, id));
        auto reservation =
            OriginalCommandLifecycleReservation::prepareOriginal(seal, {4u, true, 1u, 0u, 0u}, id);
        require(id);
        assert(reservation);
        core::pmr::String diagnostic;
        diagnostic.reserve(192u);
        auto command = VulkanOriginalBindlessCommand::prepareOriginal(
            closure, reservation, binding, *device, diagnostic);
        if (!command)
            std::fprintf(stderr, "Root texture command: %s\n", diagnostic.c_str());
        assert(command);
        assert(tx->status() == OriginalCommandStateTransaction::Status::Pending
            && !result->publishedBytes());
        if (abandon) {
            command.reset();
            assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned);
            assert(!result->publishedBytes());
        } else {
            if (terminalCases) {
                // Freeze authentic texture/view/sampler Source owners. The weak
                // closure in the shared fixture proves native custody retains
                // this exact canonical inventory after caller closure release.
                core::pmr::Vector<std::weak_ptr<const OriginalCommandTextureLease>> textures;
                core::pmr::Vector<std::weak_ptr<const OriginalCommandTextureViewSource>> views;
                core::pmr::Vector<std::weak_ptr<const OriginalCommandSamplerLease>> samplers;
                for (const auto& source : inventory->sources()) {
                    if (const auto* owner =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(
                                &source))
                        textures.emplace_back(*owner);
                    if (const auto* owner =
                            std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                &source))
                        views.emplace_back(*owner);
                    if (const auto* owner =
                            std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(
                                &source))
                        samplers.emplace_back(*owner);
                }
                assert(textures.size() == textureCount && views.size() == textureCount
                    && samplers.size() == expectedSamplerOwners);
                VulkanOriginalRootTerminalFixture::verify(terminal,
                    command,
                    closure,
                    seal,
                    reservation,
                    binding,
                    tx,
                    root,
                    *device,
                    q,
                    diagnostic,
                    [&] {
                        for (const auto& owner : textures)
                            assert(!owner.expired());
                        for (const auto& owner : views)
                            assert(!owner.expired());
                        for (const auto& owner : samplers)
                            assert(!owner.expired());
                        for (const auto& input : completedInputs)
                            require(input.first->validateOriginalContent(input.second));
                        if (tx->status() == OriginalCommandStateTransaction::Status::Invalidated)
                            for (const auto& owner : textures)
                                assert(
                                    !owner.lock()->originalObject()->observeOriginalCommittedState(
                                        defaultTextureSubresourceRange(
                                            owner.lock()->originalObject()->desc())));
                    });
            } else {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    q, [&]() noexcept -> const char* {
                        return command->submit(diagnostic) ? nullptr : diagnostic.c_str();
                    }));
                assert(command->waitSubmittedFenceUntil(
                    std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
                retireCompletedVulkanRhiSubmittedCommands();
                assert(tx->status() == OriginalCommandStateTransaction::Status::Committed
                    && result->publishedBytes());
            }
            if (terminalCases && (terminal.uncertainSubmit || terminal.uncertainFence)) {
                assert(tx->status() == OriginalCommandStateTransaction::Status::Invalidated
                    && !result->publishedBytes());
                // Read-only content was never reproposed. Its exact uploaded
                // stamp survives quarantine; conservative resource state does
                // not revive. Fault/result publication remains permanently shut.
                for (const auto& input : completedInputs)
                    require(input.first->validateOriginalContent(input.second));
                command.reset();
                assert(q[5]->liveBytes() == 0u && q[6]->liveBytes() == 0u);
                return;
            }
            std::uint32_t bits = 0u;
            std::memcpy(&bits, result->publishedBytes(), 4u);
            assert(bits == (fault ? 1u : 0u));
            if (verifyOutputs)
                verifyOutputs(*root);
            if (!verifyOutputs)
                verifyColorOutputs(*root);
            std::puts(arrayTextures
                    ? "BINDLESS_VULKAN_COMPLETED_ARRAY_ROOT8 1 genuine-upload nonuniform layer-mip-load-sample query real-fence"
                    : "BINDLESS_VULKAN_COMPLETED_TEXTURE_ROOT8 1 genuine-upload whole-barrier nonuniform independent-sampler mip-level real-fence original-journal content-stamp-unchanged");
        }
        for (const auto& source : inventory->sources())
            if (const auto* parent =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(&source)) {
                const auto& object = *(*parent)->originalObject();
                const auto completed = object.observeOriginalContent();
                assert(completed && completed->contentGeneration() == 1u);
                assert(object
                           .observeOriginalCommittedState(
                               defaultTextureSubresourceRange(object.desc()))
                           ->state()
                    == (abandon ? ResourceState::CopyDst : ResourceState::ShaderRead));
            }
        for (const auto& input : completedInputs)
            require(input.first->validateOriginalContent(input.second));
        assert(snapshot->originalSubmissionBudgetOwner()->used() == 0u);
        command.reset();
        reservation.reset();
        seal.reset();
        closure.reset();
        assert(q[5]->liveBytes() == 0u && q[6]->liveBytes() == 0u);
        if (!fault && !abandon && !terminalCases && repeatAfterCompletion) {
            command.reset();
            reservation.reset();
            seal.reset();
            closure.reset();
            binding.reset();
            result.reset();
            request.reset();
            tx.reset();
            program.reset();
            graph.reset();
            inputs.reset();
            auto next = *this;
            next.repeatAfterCompletion = false;
            next(root, inventory, device);
            std::puts(arrayTextures
                    ? "BINDLESS_VULKAN_COMPLETED_ARRAY_ROOT8_REPEAT 1 same-snapshot generation zero-image-transition"
                    : "BINDLESS_VULKAN_COMPLETED_TEXTURE_ROOT8_REPEAT 1 same-snapshot generation content-stamp zero-image-transition");
        }
    }
};
} // namespace arp::test
