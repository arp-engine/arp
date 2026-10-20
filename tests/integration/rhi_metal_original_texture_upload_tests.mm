#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_bindless_root.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_original_texture_upload.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"
#include "tests/fixtures/metal_native_texture_readback_fixture.h"
#include "tests/fixtures/metal_original_storage_execution_fixture.h"
#include "tests/fixtures/original_command_upload_stream_builder_fixture.h"
#include "tests/fixtures/original_storage_root_closure_fixture.h"
#include "tests/fixtures/original_storage_texture_creation_fixture.h"

#include <Metal/Metal.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef NDEBUG
#error Original Metal upload tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;
constexpr std::uint64_t cpuBytes = 65536u;

void require(const char* issue)
{
    if (issue)
        std::fprintf(stderr, "Metal original upload: %s\n", issue);
    assert(!issue);
}

// Independent native readback after the real original publication. This test
// does not open ordinary L2 access to exclusive original content.
void verifyPixels(const GpuTexture& texture, const TextureUploadPacking& packing, unsigned terminal)
{
    const auto readback = test::MetalNativeTextureReadbackFixture::read(
        texture, packing.mipLayouts(), packing.packedBytes().size());
    const auto* bytes = readback.data();
    for (const auto& mip : packing.mipLayouts())
        for (std::uint32_t layer = 0u; layer < mip.range.arrayLayerCount; ++layer)
            for (std::uint32_t row = 0u; row < mip.rowCount; ++row) {
                const auto offset = mip.packedOffset
                    + (std::uint64_t(layer) * mip.rowCount + row) * mip.packedBytesPerRow;
                assert(!std::memcmp(
                    bytes + offset, packing.packedBytes().data() + offset, mip.extent.width * 4u));
            }
    char name[128u];
    std::snprintf(name,
        sizeof(name),
        "metal-original-upload7-layers%u-terminal%u.bin",
        texture.desc().arrayLayerCount,
        terminal);
    auto* file = std::fopen(name, "wb");
    assert(file);
    assert(
        std::fwrite(bytes, 1u, packing.packedBytes().size(), file) == packing.packedBytes().size());
    assert(!std::fclose(file));
}

void verify(std::uint32_t layers, unsigned terminal)
{
    using Staging = MetalOriginalTextureUploadStaging;
    using K = CommandUsageKind;
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 18u> q;
    for (auto& owner : q)
        owner = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
    {
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 16u * 1024u * 1024u;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        auto device = createMetalRhiDevice(desc);
        assert(device);
        require(device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, q[0]));
        require(device->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, cpuBytes}, q[1]));
        OriginalResourceRegistrationBudget registration{9u,
            true,
            1u,
            1u,
            1u,
            OriginalResourceRegistrationContract::fixedOwnerCount
                + 3u * OriginalResourceRegistrationContract::perResourceOwnerCount
                + 2u
                    * (OriginalResourceRegistrationContract::perResourceOwnerCount
                        + OriginalResourceRegistrationContract::perDescriptorOwnerCount),
            cpuBytes};
        registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
        const char* registrationIssue = nullptr;
        const auto loss = device->observeOriginalDeviceNativeLoss(registrationIssue);
        require(registrationIssue);
        assert(loss);
        require(device->prepareOriginalDescriptorNativeLossRegistration(
            {1u, true}, registration, q[2], *loss));
        const auto gpuBaseline = device->memoryStats().allocatedBytes;
        {
            const char* issue = nullptr;
            TextureDesc td;
            td.extent = {4u, 4u, 1u};
            td.format = TextureFormat::Rgba8Unorm;
            td.arrayLayerCount = layers;
            td.mipLevelCount = 3u;
            td.cubeCompatible = layers == 6u;
            td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
            const auto nativeBytes = device->queryOriginalContentTextureAllocation(td, issue);
            require(issue);
            assert(nativeBytes);
            core::UniquePtr<GpuTexture> texture;
            {
                core::CpuMemoryQuotaScope scope(q[3]);
                texture = device->createOriginalContentTexture(td,
                    {3u, true, 1u, 1u, 84u * layers, 6u, cpuBytes, 0u, nativeBytes},
                    q[4],
                    issue);
            }
            require(issue);
            assert(texture);
            const auto* object = texture.get();
            auto lease = OriginalCommandTextureLease::prepareOriginal(
                texture, {1u, true, 2u, cpuBytes}, q[5], issue);
            require(issue);
            assert(lease);
            const auto fresh = *object->observeOriginalContent();
            TextureViewDesc vd;
            vd.format = td.format;
            vd.usage = TextureUsage::Sampled;
            vd.dimension = layers == 6u ? TextureViewDimension::TextureCube
                : layers == 2u          ? TextureViewDimension::Texture2DArray
                                        : TextureViewDimension::Texture2D;
            vd.mipLevelCount = td.mipLevelCount;
            vd.arrayLayerCount = layers;
            core::UniquePtr<GpuTextureView> view;
            std::shared_ptr<DescriptorArena> arena;
            core::UniquePtr<DescriptorSet> set;
            {
                core::CpuMemoryQuotaScope scope(q[6]);
                view = device->createTextureView(*object, vd);
                assert(view);
                DescriptorBindingDesc binding{
                    0u, DescriptorType::SampledTexture, ShaderStage::Compute, 1u};
                binding.textureViewDimensionConstrained = true;
                binding.textureViewDimension = vd.dimension;
                DescriptorLayoutDesc layout;
                layout.name = "Metal completed original upload";
                layout.bindings.push_back(binding);
                arena =
                    core::shareUnique(device->createOriginalDescriptorArena({layout.name, 1u, 1u}));
                assert(arena);
                set = device->allocateOriginalDescriptorSet(arena, {layout.name, layout});
                assert(set);
            }
            auto viewLease = OriginalCommandTextureViewLease::prepareOriginal(
                view, {1u, true, 2u, cpuBytes}, q[5], issue);
            require(issue);
            assert(viewLease);
            auto viewSource = OriginalCommandTextureViewSource::prepareOriginal(viewLease,
                lease,
                {1u, true, OriginalCommandTextureViewSource::completeRetainedOwnerCount, cpuBytes},
                q[5],
                issue);
            require(issue);
            assert(viewSource);
            DescriptorBindingWrite sampled;
            sampled.binding = 0u;
            sampled.type = DescriptorType::SampledTexture;
            sampled.resource = viewSource->resource();
            sampled.resourceKind = DescriptorResourceKind::TextureView;
            sampled.resourceState = ResourceState::ShaderRead;
            const OriginalCompletedTextureDescriptorRequest publication{
                1u, true, 1u, 1u, OriginalCommandTextureViewSource::completeRetainedOwnerCount};
            assert(!set->write(sampled).success);
            assert(
                !set->writeOriginalCompletedTexture(
                        *device, sampled, viewSource, fresh, publication)
                    .success);
            assert(set->contentRevision() == 1u && !set->hasWrittenBinding(0u));
            std::array<std::uint8_t, 504u> pixels;
            for (std::size_t i = 0u; i < pixels.size(); ++i)
                pixels[i] = static_cast<std::uint8_t>(i * 17u + 13u);
            UploadTextureDesc upload;
            upload.destinationTexture = object->id();
            upload.extent = td.extent;
            upload.format = td.format;
            upload.subresources = defaultTextureSubresourceRange(td);
            upload.bytesPerRow = 16u;
            upload.rowsPerImage = 4u;
            upload.pixelData = pixels.data();
            upload.pixelDataSize = 84u * layers;
            upload.mipLayouts = {{0u, {4u, 4u, 1u}, 16u, 4u, 0u},
                {1u, {2u, 2u, 1u}, 8u, 2u, 64u * layers},
                {2u, {1u, 1u, 1u}, 4u, 1u, 80u * layers}};
            auto input = TextureUploadInputSnapshot::prepareOriginal(upload,
                {1u, true, 3u, 0u, 84u * layers, cpuBytes, cpuBytes, cpuBytes},
                q[12],
                q[12],
                q[5],
                issue);
            require(issue);
            assert(input);
            auto source = OriginalCommandTextureUploadSource::prepareOriginal(
                input, lease, {1u, true, 5u, cpuBytes}, q[5], issue);
            require(issue);
            assert(source);
            const auto packedBytes = 7u * kTextureUploadRowPitchAlignment * layers;
            auto packing = TextureUploadPacking::prepareOriginal(
                input, td, {1u, true, 3u, packedBytes, cpuBytes, cpuBytes}, q[7], q[8], issue);
            require(issue);
            assert(packing);
            auto packed = OriginalCommandTextureUploadPackingSource::prepareOriginal(source,
                packing,
                {1u,
                    true,
                    3u,
                    OriginalCommandTextureUploadPackingSource::retainedOwnerCount,
                    84u * layers,
                    packedBytes,
                    cpuBytes},
                q[9],
                issue);
            require(issue);
            assert(packed);
            Staging::Budget budget{1u,
                true,
                1u,
                3u,
                Staging::retainedOwnerBound,
                packedBytes,
                desc.memoryBudgetBytes,
                cpuBytes,
                cpuBytes,
                cpuBytes};
            const Staging::Quotas quotas{q[10], q[11], q[5]};
            auto small = budget;
            --small.maxLogicalStagingBytes;
            assert(!Staging::prepareOriginal(packed, *device, small, quotas, issue));
            small = budget;
            --small.maxRetainedOwners;
            assert(!Staging::prepareOriginal(packed, *device, small, quotas, issue));
            auto alias = quotas;
            alias.metadata = alias.lease;
            assert(!Staging::prepareOriginal(packed, *device, budget, alias, issue));
            const auto makeStaging = [&] {
                return Staging::prepareOriginal(packed, *device, budget, quotas, issue);
            };
            std::uint64_t measuredNative = 0u;
            {
                auto warm = makeStaging();
                require(issue);
                assert(warm);
                measuredNative = warm->nativeStagingBytes();
            }
            small = budget;
            small.maxNativeStagingBytes = measuredNative - 1u;
            assert(!Staging::prepareOriginal(packed, *device, small, quotas, issue));
            for (const auto index : {10u, 11u, 5u}) {
                test::CpuQuotaFactoryFixture pressure(q[index]);
                const auto peak = pressure.measure(makeStaging);
                pressure.verify(peak, makeStaging);
            }
            auto staging = makeStaging();
            require(issue);
            assert(staging);
            auto forged = std::shared_ptr<const Staging>(q[10], staging.get());
            assert(Staging::validateOriginal(forged));
            forged.reset();
            const auto native = metalRhiNativePayloadFor(*object);
            const auto owner = native->resource.nativeObject;
            native->resource.nativeObject = std::shared_ptr<void>(q[10], owner.get());
            assert(staging->validate());
            native->resource.nativeObject = owner;
            require(staging->validate());
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
            admission.maxMipMappings = 3u;
            admission.maxStateTransactions = 1u;
            admission.maxRetainedUploadBytes = 84u * layers;
            admission.maxPendingSubmissions = admission.maxNativePayloadObjects = 1u;
            admission.maxTypedNativeCommandCalls = 3u * layers + 4u;
            admission.maxNativeStagingBytes = staging->nativeStagingBytes();
            using D = OriginalCommandAdmissionBudget::CpuDomain;
            admission.maxCpuBytes[static_cast<std::size_t>(D::Recording)] = cpuBytes;
            admission.maxCpuBytes[static_cast<std::size_t>(D::Submission)] = cpuBytes;
            auto inputs = test::OriginalCommandUploadStreamBuilderFixture::prepare(
                retained, commands, refs, descriptors, admission, q[5], q[12], issue);
            require(issue);
            assert(inputs);
            const OriginalCommandStateAdmissionBudget state{
                1u, true, 1u, 2u, 3u, 2u, 128u, cpuBytes};
            auto graph = OriginalCommandResourceStates::prepareOriginal(
                inputs, *device, state, q[13], issue);
            require(issue);
            assert(graph);
            auto tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
                graph, *device, state, q[14], issue);
            require(issue);
            assert(tx);
            require(tx->materializeOriginalPrototypePlan(
                {1u, true, 6u, 0u, 1u + 6u * layers, 1u + 6u * layers},
                {7u, true, 1u, 1u, 0u, 3u, 6u, 20u}));
            OriginalCommandStreamBudget stream;
            stream.contractVersion = 1u;
            stream.prototypeOptIn = true;
            stream.maxCommands = 6u;
            stream.maxDescriptors = 3u;
            stream.maxReferences = 4u;
            stream.maxTextureUploadMips = 3u;
            stream.maxTextureUploadPixelBytes = 84u * layers;
            stream.maxRetainedOwners = inputs->retainedOwnerCount() + 1u;
            stream.maxCpuMetadataBytes = cpuBytes;
            auto program =
                OriginalCommandStreamProgram::prepareOriginal(inputs, stream, q[15], issue);
            require(issue);
            assert(program);
            OriginalCommandNativeLifecycleBudget life{7u,
                true,
                1u,
                0u,
                0u,
                tx->retainedOwnerCount() + 2u,
                tx->retainedOwnerCount() + program->retainedOwnerCount()
                    + staging->retainedOwnerCount() + 6u,
                0u,
                cpuBytes,
                cpuBytes,
                0u};
            auto seal =
                OriginalCommandRecordingSeal::prepareOriginal(tx, *device, life, q[16], {}, issue);
            require(issue);
            assert(seal);
            auto reservation = OriginalCommandLifecycleReservation::prepareOriginal(
                seal, {3u, true, 1u, 0u, 0u}, issue);
            require(issue);
            assert(reservation);
            core::pmr::String diagnostic;
            diagnostic.reserve(512u);
            auto command = MetalOriginalTextureUploadCommand::prepareOriginal(
                reservation, program, staging, *device, diagnostic);
            if (!command)
                std::fprintf(stderr, "Metal upload prepare: %s\n", diagnostic.c_str());
            assert(command);
            assert(!object->observeOriginalContent());
            if (terminal == 1u) {
                command.reset();
                assert(object->state() == ResourceState::Undefined
                    && !object->observeOriginalContent()->contentGeneration());
                std::printf(
                    "BINDLESS_METAL_ORIGINAL_UPLOAD7 layers=%u terminal=cancel rollback unpublished\n",
                    layers);
            } else {
                if (terminal == 2u)
                    device->retireOriginalDeviceLifetime();
                const auto done = command->preparedCompletionState();
                MetalRhiNativeQueueStats stats;
                if (terminal == 2u) {
                    assert(!command->submit(stats, done, {}, 0u, {}, 0u, diagnostic));
                    assert(!object->observeOriginalContent());
                } else {
                    // Hold the actual shared native queue before this upload,
                    // so caller drop/retirement is proven while work is pending.
                    id<MTLSharedEvent> gate = nil;
                    if (terminal == 3u || terminal == 4u) {
                        auto queue = reinterpret_cast<id<MTLCommandQueue>>(
                            sharedMetalRhiNativeCommandQueue().get());
                        gate = [queue.device newSharedEvent];
                        assert(gate);
                        auto blocked = [queue commandBuffer];
                        [blocked encodeWaitForEvent:gate value:1u];
                        [blocked commit];
                    }
                    require(test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        q, [&]() noexcept -> const char* {
                            if (!command->submit(stats, done, {}, 0u, {}, 0u, diagnostic))
                                return diagnostic.c_str();
                            if (gate) {
                                assert(!done->isComplete() && !object->observeOriginalContent());
                                command.reset();
                                if (terminal == 4u)
                                    device->retireOriginalDeviceLifetime();
                                gate.signaledValue = 1u;
                                [gate release];
                            }
                            const auto deadline =
                                std::chrono::steady_clock::now() + std::chrono::seconds(10);
                            while (
                                !done->isComplete() && std::chrono::steady_clock::now() < deadline)
                                std::this_thread::yield();
                            return done->isComplete() ? nullptr : "METAL-UPLOAD-TEST-TIMEOUT";
                        }));
                    if (terminal == 4u) {
                        assert(done->outcome() == MetalRhiCompletionState::Outcome::Failed);
                        assert(
                            tx->status() == OriginalCommandStateTransaction::Status::Invalidated);
                        assert(!object->observeOriginalContent());
                    } else {
                        assert(done->outcome() == MetalRhiCompletionState::Outcome::Completed);
                        assert(tx->status() == OriginalCommandStateTransaction::Status::Committed);
                        assert(object->observeOriginalContent()->contentGeneration() == 1u
                            && object->state() == ResourceState::CopyDst);
                        verifyPixels(*object, *packing, terminal);
                        if (terminal == 0u) {
                            const auto completed = *object->observeOriginalContent();
                            auto forged = std::shared_ptr<const OriginalCommandTextureViewSource>(
                                q[5], viewSource.get());
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, forged, completed, publication)
                                    .success);
                            forged.reset();
                            auto wrong = sampled;
                            wrong.resourceState = ResourceState::CopyDst;
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, wrong, viewSource, completed, publication)
                                    .success);
                            auto small = publication;
                            --small.maxRetainedSourceOwners;
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, viewSource, completed, small)
                                    .success);
                            const auto nativeView =
                                metalRhiNativePayloadFor(*viewSource->originalObject());
                            const auto viewObject = nativeView->resource.nativeObject;
                            nativeView->resource.nativeObject = owner;
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, viewSource, completed, publication)
                                    .success);
                            nativeView->resource.nativeObject = viewObject;
                            {
                                core::CpuMemoryQuotaScope scope(q[6]);
                                core::CpuMemoryQuotaLease pressure(
                                    q[6], cpuBytes - q[6]->liveBytes());
                                assert(pressure.charged());
                                assert(
                                    !set->writeOriginalCompletedTexture(
                                            *device, sampled, viewSource, completed, publication)
                                        .success);
                            }
                            assert(!set->hasWrittenBinding(0u) && set->contentRevision() == 1u);
                            {
                                core::CpuMemoryQuotaScope scope(q[6]);
                                const auto published = set->writeOriginalCompletedTexture(
                                    *device, sampled, viewSource, completed, publication);
                                if (!published.success)
                                    std::fprintf(stderr,
                                        "Metal publication: %s\n",
                                        published.diagnostics.c_str());
                                assert(published.success);
                            }
                            assert(set->hasWrittenBinding(0u) && set->contentRevision() == 2u);
                            assert(set->validateBindingElement(0u, 0u).success
                                && set->validateBinding(0u).success);
                            assert(!set->write(sampled).success && !metalRhiNativePayloadFor(*set));
                            auto replacement = OriginalCommandTextureViewSource::prepareOriginal(
                                viewLease,
                                lease,
                                {1u,
                                    true,
                                    OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                                    cpuBytes},
                                q[5],
                                issue);
                            require(issue);
                            assert(replacement);
                            assert(
                                !set->writeOriginalCompletedTexture(
                                        *device, sampled, replacement, completed, publication)
                                    .success);
                            replacement.reset();
                            {
                                core::CpuMemoryQuotaScope scope(q[6]);
                                core::CpuMemoryQuotaLease pressure(
                                    q[6], cpuBytes - q[6]->liveBytes());
                                assert(pressure.charged());
                                const auto refused = set->writeOriginalCompletedTexture(
                                    *device, sampled, viewSource, completed, publication);
                                assert(!refused.success && refused.diagnostics == "RHI-OOM");
                            }
                            assert(set->contentRevision() == 2u
                                && set->validateBindingElement(0u, 0u).success);
                            // A same-address forged native owner cannot replace frozen custody.
                            nativeView->resource.nativeObject =
                                std::shared_ptr<void>(q[6], viewObject.get());
                            assert(!set->validateBindingElement(0u, 0u).success);
                            nativeView->resource.nativeObject = viewObject;
                            require(test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                q, [&]() noexcept -> const char* {
                                    return set->validateBindingElement(0u, 0u).success
                                        ? nullptr
                                        : "METAL-PUBLICATION-VALIDATION";
                                }));
                            std::weak_ptr<const OriginalCommandTextureViewSource> weak = viewSource;
                            viewSource.reset();
                            viewLease.reset();
                            assert(!weak.expired() && set->validateBindingElement(0u, 0u).success);
                            device->retireOriginalDeviceLifetime();
                            assert(!set->validateBindingElement(0u, 0u).success);
                            set.reset();
                            assert(weak.expired());
                            std::printf(
                                "BINDLESS_METAL_COMPLETED_PUBLICATION1 layers=%u retained-source hot-validation0 ordinary-denied retired-invalid\n",
                                layers);
                        }
                    }
                }
                std::printf(
                    "BINDLESS_METAL_ORIGINAL_UPLOAD7 layers=%u terminal=%u texels=%u original-journal completed-handler hot-submit-completion0\n",
                    layers,
                    terminal,
                    84u * layers);
            }
        }
        assert(device->memoryStats().allocatedBytes == gpuBaseline);
    }
    for (const auto& owner : q)
        assert(!owner->liveBytes());
}
} // namespace

int main(int argc, char** argv)
{
    @autoreleasepool {
        MetalRhiDeviceDesc storageDesc;
        storageDesc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        storageDesc.capabilityContract.queriedFromNativeDevice = true;
        storageDesc.capabilityContract.queriedExtendedFeatures = true;
        storageDesc.memoryBudgetBytes = 16u * 1024u * 1024u;
        storageDesc.uploadStagingPageSizeBytes = 4096u;
        storageDesc.uploadStagingMaxPageCount = 1u;
        storageDesc.enableBindlessCompute = true;
        storageDesc.enableBindlessComputeImages = true;
        const bool storageCapture = argc == 3 && std::strcmp(argv[1], "--storage-capture") == 0;
        if (storageCapture || (argc == 2 && std::strcmp(argv[1], "--storage-execution") == 0)) {
            if (storageCapture) {
                FrameCaptureRequest request;
                request.tool = FrameCaptureTool::NativeGpuTrace;
                request.outputPath = argv[2];
                const auto r = beginMetalRhiFrameCapture(request);
                assert(r.success());
            }
            for (unsigned terminal : {0u, 1u, 2u, 3u, 4u})
                for (bool fault : {false, true}) {
                    if (storageCapture && terminal)
                        continue;
                    test::MetalOriginalStorageExecutionFixture::fault = fault;
                    test::MetalOriginalStorageExecutionFixture::terminal = terminal;
                    test::OriginalStorageTextureCreationFixture::verifyDevice(
                        createMetalRhiDevice(storageDesc),
                        [](const DescriptorSet& set) {
                            assert(!metalRhiNativePayloadFor(set));
                        },
                        nullptr,
                        &test::MetalOriginalStorageExecutionFixture::execute);
                }
            if (storageCapture) {
                const auto r = endMetalRhiFrameCapture();
                assert(r.success());
            }
            return 0;
        }
        test::OriginalStorageTextureCreationFixture::verifyDevice(
            createMetalRhiDevice(storageDesc),
            [](const DescriptorSet& set) {
                assert(!metalRhiNativePayloadFor(set));
            },
            [](auto& device, const auto& root, const auto& inventory) {
                test::OriginalStorageRootClosureFixture::verify<MetalRhiBindlessRootNativeClosure>(
                    device, root, inventory);
            });
        for (const auto layers : {1u, 2u, 6u})
            for (const auto terminal : {0u, 1u, 2u, 3u, 4u})
                verify(layers, terminal);
    }
    std::puts("BINDLESS_METAL_ORIGINAL_UPLOAD7_OK cases15 quota0 gpu0");
    std::puts("BINDLESS_METAL_COMPLETED_PUBLICATION1_OK cases3 quota0 gpu0");
}
