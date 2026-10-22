#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_staging_upload_pool.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Command retention native tests require assertions
#endif

namespace {
using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_metal;

class CopyFixture final {
public:
    CopyFixture()
    {
        diagnostic.reserve(256u);
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 1024u * 1024u;
        desc.uploadStagingPageSizeBytes = 4096u;
        desc.uploadStagingMaxPageCount = 1u;
        device = createMetalRhiDevice(desc);
        assert(device);
        assert(device->capabilities().commandRetention.contractVersion == 0u);
        registry = core::makeShared<MetalRhiResourceRegistry>();
        source = device->createBuffer({"BudgetSource",
            sizeof(input),
            ResourceUsage::CopySrc,
            MemoryUsage::CpuToGpu,
            ResourceState::CopySrc});
        destination = device->createBuffer({"BudgetDestination",
            sizeof(input),
            ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::CopyDst});
        assert(source && destination);
        assert(device->writeBuffer(*source, input.data(), sizeof(input)).success);
        assert(device->writeBuffer(*destination, sentinel.data(), sizeof(sentinel)).success);
        registry->registerBuffer(*source);
        registry->registerBuffer(*destination);
        copy = {source->id(), destination->id(), 0u, 0u, sizeof(input)};
    }

    core::UniquePtr<common::CommandRetentionStorage> prepare(std::uint32_t owners = 8u)
    {
        CommandRetentionBudget budget{1u, true, owners, 0u, 0u, 0u, 16384u};
        quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        const char* id = nullptr;
        auto storage = common::CommandRetentionStorage::prepare(budget, quota, id);
        assert(storage && !id);
        // The public factory must still fail closed; native fixture access does
        // not grant production capabilities or bypass its public execution gate.
        CommandEncoderDesc encoderDesc;
        encoderDesc.retentionBudget = budget;
        assert(!device->createCommandEncoder(encoderDesc));
        return storage;
    }

    std::shared_ptr<MetalRhiNativeCommandPayload> command(std::uint32_t owners = 8u)
    {
        auto storage = prepare(owners);
        const auto storageCharge = quota->liveBytes();
        auto payload = createMetalRhiNativeCommandPayload(
            "BudgetCopyPrototype", registry, &diagnostic, 0u, 0u, 0u, std::move(storage));
        if (!payload)
            std::fprintf(stderr, "%s\n", diagnostic.c_str());
        assert(payload && quota->liveBytes() > storageCharge);
        return payload;
    }

    void verify(const std::array<std::uint32_t, 8u>& expected)
    {
        std::array<std::uint32_t, 8u> output{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*destination),
            output.data(),
            sizeof(output),
            0u,
            sizeof(output),
            diagnostic));
        assert(output == expected);
    }

    core::UniquePtr<GpuDevice> device;
    core::UniquePtr<GpuBuffer> source, destination;
    std::shared_ptr<MetalRhiResourceRegistry> registry;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    core::pmr::String diagnostic;
    BufferCopyDesc copy;
    const std::array<std::uint32_t, 8u> input{3u, 5u, 11u, 17u, 23u, 31u, 43u, 59u};
    const std::array<std::uint32_t, 8u> sentinel{91u, 91u, 91u, 91u, 91u, 91u, 91u, 91u};
};

// Packing is declared by the shared L2 row-pitch contract and explicit mip
// metadata; every mip and array layer contains a distinct exact byte pattern.
class TextureTransferFixture final {
public:
    enum class Kind { BufferTexture, StagingTexture, TextureBuffer, TextureTexture, StagingBuffer };
    static constexpr std::uint32_t pitch = kTextureUploadRowPitchAlignment;
    static constexpr std::size_t byteCount = pitch * (4u + 2u + 1u) * 2u;
    const std::array<Extent3D, 3u> extents{{{4u, 4u, 1u}, {2u, 2u, 1u}, {1u, 1u, 1u}}};
    const std::array<std::uint64_t, 3u> offsets{0u, pitch * 4u * 2u, pitch * (4u + 2u) * 2u};

    TextureTransferFixture(Kind operation, bool placed = false) : kind(operation)
    {
        sentinel.fill(0x91u);
        pixels = sentinel;
        for (std::size_t mip = 0u; mip < extents.size(); ++mip)
            for (unsigned layer = 0u; layer < 2u; ++layer)
                for (unsigned row = 0u; row < extents[mip].height; ++row)
                    for (unsigned col = 0u; col < extents[mip].width * 4u; ++col)
                        pixels[offsets[mip] + layer * pitch * extents[mip].height + row * pitch
                            + col] = static_cast<std::uint8_t>(1u + mip * 61u + layer * 29u
                            + row * 7u + col);
        input = context.device->createBuffer({"TextureTransferInput",
            byteCount,
            ResourceUsage::CopySrc,
            MemoryUsage::CpuToGpu,
            ResourceState::CopySrc});
        output = context.device->createBuffer({"TextureTransferOutput",
            byteCount,
            ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::CopyDst});
        clear = context.device->createBuffer({"TextureTransferClear",
            byteCount,
            ResourceUsage::CopySrc,
            MemoryUsage::CpuToGpu,
            ResourceState::CopySrc});
        assert(input && output && clear);
        assert(context.device->writeBuffer(*input, pixels.data(), byteCount).success);
        assert(context.device->writeBuffer(*output, sentinel.data(), byteCount).success);
        assert(context.device->writeBuffer(*clear, sentinel.data(), byteCount).success);
        context.registry->registerBuffer(*input);
        context.registry->registerBuffer(*output);
        context.registry->registerBuffer(*clear);
        for (auto* texture : {&source, &destination}) {
            TextureDesc desc{"TextureTransferMipArray",
                extents[0],
                TextureFormat::Rgba8Unorm,
                TextureUsage::CopySrc | TextureUsage::CopyDst,
                MemoryUsage::GpuOnly,
                ResourceState::Undefined,
                3u,
                2u};
            if (texture == &source && placed) {
                desc.memory = MemoryUsage::Transient;
                const auto requirements = context.device->queryTexturePlacementRequirements(desc);
                assert(requirements.sizeBytes && requirements.alignmentBytes);
                heap = context.device->createMemoryHeap(
                    requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient);
                assert(heap);
                *texture = context.device->createPlacedTexture(desc, *heap, 0u);
            } else
                *texture = context.device->createTexture(desc);
            assert(*texture);
            context.registry->registerTexture(**texture);
        }
        toTexture.sourceBuffer = input->id();
        toTexture.destinationTexture = source->id();
        toTexture.textureSubresources = range;
        toBuffer.sourceTexture = source->id();
        toBuffer.destinationBuffer = output->id();
        toBuffer.textureSubresources = range;
        textureCopy.sourceTexture = source->id();
        textureCopy.destinationTexture = destination->id();
        textureCopy.sourceSubresources = range;
        textureCopy.destinationSubresources = range;
        upload.destinationTexture = source->id();
        upload.subresources = range;
        upload.format = TextureFormat::Rgba8Unorm;
        upload.pixelDataSize = byteCount;
        for (unsigned mip = 0u; mip < 3u; ++mip) {
            toTexture.mipLayouts.push_back(
                {mip, extents[mip], pitch, extents[mip].height, offsets[mip]});
            toBuffer.mipLayouts.push_back(
                {mip, extents[mip], pitch, extents[mip].height, offsets[mip]});
            textureCopy.mipLayouts.push_back({mip, extents[mip]});
            upload.mipLayouts.push_back(
                {mip, extents[mip], pitch, extents[mip].height, offsets[mip]});
        }
        // Initialize through ordinary native transfers before measuring the
        // prototype's hot path. This also verifies preserved non-budget paths.
        auto setup = ordinary();
        assert(setup->recordBlitPassBegin(context.diagnostic));
        auto initialize = toTexture;
        initialize.sourceBuffer = (kind == Kind::TextureBuffer || kind == Kind::TextureTexture)
            ? input->id()
            : clear->id();
        assert(setup->recordBlitBufferToTexture(initialize, context.diagnostic));
        initialize.sourceBuffer = clear->id();
        initialize.destinationTexture = destination->id();
        assert(setup->recordBlitBufferToTexture(initialize, context.diagnostic));
        complete(setup);
        pool = core::makeUnique<MetalRhiStagingUploadPool>(MetalRhiStagingUploadPoolDesc{
            "TextureTransferStaging", byteCount + pitch, 1u, pitch, true});
        prefix = pool->allocate({"StagingPrefix", pitch, pitch, 1u});
        assert(prefix.success);
        slice = pool->allocate({"TextureTransferSlice", byteCount, pitch, 1u});
        assert(slice.success && slice.offsetBytes == pitch);
        std::memcpy(slice.mappedData, pixels.data(), byteCount);
        assert(prepareMetalRhiNativeStagingBufferForGpu(
            slice.payload, slice.offsetBytes, byteCount, context.diagnostic));
    }

    std::shared_ptr<MetalRhiNativeCommandPayload> ordinary()
    {
        auto result = createMetalRhiNativeCommandPayload(
            "TextureTransferSetup", context.registry, &context.diagnostic);
        assert(result);
        return result;
    }

    void complete(const std::shared_ptr<MetalRhiNativeCommandPayload>& command)
    {
        assert(
            command->recordBlitPassEnd(context.diagnostic) && command->finish(context.diagnostic));
        auto completion = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, completion, {}, 0u, {}, 0u, context.diagnostic));
        completion->wait();
    }

    unsigned ownerCount() const
    {
        return kind == Kind::StagingTexture || kind == Kind::StagingBuffer ? 7u : 8u;
    }

    bool record(const std::shared_ptr<MetalRhiNativeCommandPayload>& command)
    {
        switch (kind) {
        case Kind::BufferTexture:
            return command->recordBlitBufferToTexture(toTexture, context.diagnostic);
        case Kind::StagingTexture:
            return command->recordUploadStagingTextureCopy(
                slice.payload, slice.offsetBytes, upload, context.diagnostic);
        case Kind::TextureBuffer:
            return command->recordBlitTextureToBuffer(toBuffer, context.diagnostic);
        case Kind::TextureTexture:
            return command->recordBlitTextureCopy(textureCopy, context.diagnostic);
        case Kind::StagingBuffer:
            return command->recordUploadStagingBufferCopy(
                slice.payload, slice.offsetBytes, byteCount, output->id(), context.diagnostic);
        }
        return false;
    }

    void verify(const std::array<std::uint8_t, byteCount>& expected)
    {
        if (kind != Kind::TextureBuffer && kind != Kind::StagingBuffer) {
            auto readback = ordinary();
            assert(readback->recordBlitPassBegin(context.diagnostic));
            auto copy = toBuffer;
            copy.sourceTexture = kind == Kind::TextureTexture ? destination->id() : source->id();
            assert(readback->recordBlitTextureToBuffer(copy, context.diagnostic));
            complete(readback);
        }
        std::array<std::uint8_t, byteCount> bytes{};
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*output),
            bytes.data(),
            byteCount,
            0u,
            byteCount,
            context.diagnostic));
        assert(bytes == expected); // Also proves untouched row padding.
    }

    std::weak_ptr<const void> dropSource()
    {
        std::weak_ptr<const void> observed;
        if (kind == Kind::BufferTexture) {
            observed = metalRhiNativePayloadFor(*input);
            input.reset();
        } else if (kind == Kind::StagingTexture || kind == Kind::StagingBuffer) {
            observed = slice.payload;
            slice.payload.reset();
            prefix.payload.reset();
            pool.reset();
        } else {
            observed = metalRhiNativePayloadFor(*source);
            source.reset();
        }
        return observed;
    }

    CopyFixture context;
    Kind kind;
    const TextureSubresourceRange range{0u, 3u, 0u, 2u, TextureAspect::Color};
    std::array<std::uint8_t, byteCount> pixels{}, sentinel{};
    core::UniquePtr<GpuBuffer> input, output, clear;
    core::UniquePtr<GpuTexture> source, destination;
    core::UniquePtr<GpuMemoryHeap> heap;
    core::UniquePtr<MetalRhiStagingUploadPool> pool;
    MetalRhiStagingUploadSlice prefix, slice;
    BufferToTextureCopyDesc toTexture;
    TextureToBufferCopyDesc toBuffer;
    TextureToTextureCopyDesc textureCopy;
    UploadTextureDesc upload;
};

void verifyTextureTransfers()
{
    using Kind = TextureTransferFixture::Kind;
    for (const auto kind : {Kind::BufferTexture,
             Kind::StagingTexture,
             Kind::TextureBuffer,
             Kind::TextureTexture,
             Kind::StagingBuffer}) {
        for (const bool dropPayload : {false, true}) {
            TextureTransferFixture fixture(kind);
            auto command = fixture.context.command(fixture.ownerCount());
            assert(command->recordBlitPassBegin(fixture.context.diagnostic));
            const auto charged = fixture.context.quota->liveBytes();
            core::CpuMemoryQuotaLease pressure(
                fixture.context.quota, fixture.context.quota->limitBytes() - charged);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned repeat = 0u; repeat < 32u; ++repeat)
                assert(fixture.record(command));
            assert(command->retentionStorage()->resourceOwnerCount() == fixture.ownerCount());
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            assert(fixture.context.quota->liveBytes() == fixture.context.quota->limitBytes());
            pressure.release();
            assert(fixture.context.quota->liveBytes() == charged);
            std::weak_ptr<void> originalObject, originalToken;
            if (kind == Kind::BufferTexture || kind == Kind::TextureBuffer
                || kind == Kind::TextureTexture) {
                auto& resource = kind == Kind::BufferTexture
                    ? metalRhiNativePayloadFor(*fixture.input)->resource
                    : metalRhiNativePayloadFor(*fixture.source)->resource;
                const auto& replacement = kind == Kind::BufferTexture
                    ? metalRhiNativePayloadFor(*fixture.clear)->resource
                    : metalRhiNativePayloadFor(*fixture.destination)->resource;
                originalObject = resource.nativeObject;
                originalToken = resource.allocationReleaseToken;
                assert(!originalObject.expired() && !originalToken.expired());
                resource.nativeObject = replacement.nativeObject;
                resource.allocationReleaseToken = replacement.allocationReleaseToken;
                if (kind != Kind::TextureTexture)
                    assert(!fixture.record(command)
                        && fixture.context.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
                assert(command->retentionStorage()->resourceOwnerCount() == fixture.ownerCount());
            } else
                originalObject =
                    inspectMetalRhiNativeStagingBuffer(fixture.slice.payload).nativeBuffer;
            assert(command->recordBlitPassEnd(fixture.context.diagnostic)
                && command->finish(fixture.context.diagnostic));
            auto observed = fixture.dropSource();
            assert(!observed.expired());
            assert(!originalObject.expired());
            auto gate = createMetalRhiNativeSharedEventPayload("TextureTransferDelay");
            assert(gate && gate->nativeRuntimeBacked);
            auto completion = command->preparedCompletionState();
            MetalRhiNativeQueueStats queue;
            assert(
                command->submit(queue, completion, gate, 1u, {}, 0u, fixture.context.diagnostic));
            assert(!completion->isComplete() && !observed.expired());
            assert(!originalObject.expired());
            command->abandonUnsubmitted();
            assert(!observed.expired());
            if (dropPayload)
                command.reset();
            signalMetalRhiNativeSharedEvent(gate, 1u);
            completion->wait();
            assert(observed.expired());
            assert(originalObject.expired() && originalToken.expired());
            fixture.verify(fixture.pixels);
            std::printf(
                "textureTransfer: kind=%u dropped=%d 32 records allocate0 at exhausted metadata quota; exact three-mip/two-layer bytes; completion releases source\n",
                static_cast<unsigned>(kind),
                dropPayload);
        }
    }
}

void verifyTextureTransferRejections()
{
    using Kind = TextureTransferFixture::Kind;
    for (const auto kind : {Kind::BufferTexture,
             Kind::StagingTexture,
             Kind::TextureBuffer,
             Kind::TextureTexture,
             Kind::StagingBuffer}) {
        for (const auto owners :
            {0u, 1u, (kind == Kind::StagingTexture || kind == Kind::StagingBuffer) ? 6u : 7u}) {
            TextureTransferFixture fixture(kind);
            auto command = fixture.context.command(owners);
            assert(command->recordBlitPassBegin(fixture.context.diagnostic));
            assert(!fixture.record(command)
                && fixture.context.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
            assert(!command->retentionStorage()->resourceOwnerCount()
                && !command->stats()->nativeBlitCopyEncoded);
            fixture.complete(command);
            fixture.verify(fixture.sentinel);
        }
        // A last-mip failure must not admit either owner or encode earlier mips.
        for (unsigned fault = 0u; fault < (kind == Kind::StagingBuffer ? 1u : 3u); ++fault) {
            TextureTransferFixture fixture(kind);
            auto command = fixture.context.command(fixture.ownerCount());
            assert(command->recordBlitPassBegin(fixture.context.diagnostic));
            if (kind == Kind::StagingBuffer) {
                fixture.slice.offsetBytes = UINT64_MAX;
            } else if (fault == 0u) {
                fixture.toTexture.mipLayouts.back().mipLevel = 0u;
                fixture.toBuffer.mipLayouts.back().mipLevel = 0u;
                fixture.textureCopy.mipLayouts.back().mipLevel = 0u;
                fixture.upload.mipLayouts.back().mipLevel = 0u;
            } else if (fault == 1u) {
                fixture.toTexture.mipLayouts.back().extent = {2u, 1u, 1u};
                fixture.toBuffer.mipLayouts.back().extent = {2u, 1u, 1u};
                fixture.textureCopy.mipLayouts.back().extent = {2u, 1u, 1u};
                fixture.upload.mipLayouts.back().extent = {2u, 1u, 1u};
            } else {
                fixture.toTexture.mipLayouts.back().bufferOffsetBytes = UINT64_MAX;
                fixture.toBuffer.mipLayouts.back().bufferOffsetBytes = UINT64_MAX;
                fixture.upload.mipLayouts.back().bufferOffsetBytes = 0u; // Overlapping packed mips.
                fixture.textureCopy.destinationSubresources.baseMipLevel = 1u;
            }
            assert(!fixture.record(command));
            if (kind != Kind::StagingBuffer) {
                const char* expected = nullptr;
                if (fault == 0u) {
                    switch (kind) {
                    case Kind::BufferTexture:
                        expected = "RHI-METAL-BLIT-BUFFER-TEXTURE-MIP-LAYOUT-ORDER-INVALID";
                        break;
                    case Kind::StagingTexture:
                        expected = "RHI-METAL-STAGING-TEXTURE-COPY-MIP-LAYOUT-ORDER-INVALID";
                        break;
                    case Kind::TextureBuffer:
                        expected = "RHI-METAL-BLIT-TEXTURE-BUFFER-MIP-LAYOUT-ORDER-INVALID";
                        break;
                    case Kind::TextureTexture:
                        expected = "RHI-METAL-BLIT-TEXTURE-COPY-MIP-LAYOUT-ORDER-INVALID";
                        break;
                    default:
                        break;
                    }
                } else if (fault == 1u || kind == Kind::TextureTexture) {
                    switch (kind) {
                    case Kind::BufferTexture:
                        expected = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-REGION-INVALID";
                        break;
                    case Kind::StagingTexture:
                        expected = "RHI-METAL-STAGING-TEXTURE-COPY-REGION-INVALID";
                        break;
                    case Kind::TextureBuffer:
                        expected = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-REGION-INVALID";
                        break;
                    case Kind::TextureTexture:
                        expected = "RHI-METAL-BLIT-TEXTURE-COPY-REGION-INVALID";
                        break;
                    default:
                        break;
                    }
                } else {
                    switch (kind) {
                    case Kind::BufferTexture:
                        expected = "RHI-METAL-BLIT-BUFFER-TEXTURE-COPY-BUFFER-RANGE-INVALID";
                        break;
                    case Kind::StagingTexture:
                        expected = "RHI-METAL-STAGING-TEXTURE-COPY-BUFFER-RANGE-INVALID";
                        break;
                    case Kind::TextureBuffer:
                        expected = "RHI-METAL-BLIT-TEXTURE-BUFFER-COPY-BUFFER-RANGE-INVALID";
                        break;
                    default:
                        break;
                    }
                }
                assert(expected && fixture.context.diagnostic == expected);
            }
            assert(!command->retentionStorage()->resourceOwnerCount()
                && !command->stats()->nativeBlitCopyEncoded);
            fixture.complete(command);
            // Restore readback metadata; rejection must leave the entire destination sentinel unchanged.
            for (unsigned mip = 0u; mip < 3u; ++mip)
                fixture.toBuffer.mipLayouts[mip] = {mip,
                    fixture.extents[mip],
                    fixture.pitch,
                    fixture.extents[mip].height,
                    fixture.offsets[mip]};
            fixture.verify(fixture.sentinel);
        }
        for (const bool sealed : {false, true}) {
            TextureTransferFixture fixture(kind);
            auto command = fixture.context.command(fixture.ownerCount());
            assert(command->recordBlitPassBegin(fixture.context.diagnostic)
                && fixture.record(command));
            auto observed = fixture.dropSource();
            assert(!observed.expired());
            if (sealed)
                assert(command->recordBlitPassEnd(fixture.context.diagnostic)
                    && command->finish(fixture.context.diagnostic));
            command->abandonUnsubmitted();
            assert(observed.expired());
            assert(command->readbackCompletionStatus() == MetalReadbackCompletion::Abandoned);
            command.reset();
            assert(!fixture.context.quota->liveBytes());
            // Cancellation is neither queue completion nor a staging pool reuse credential.
        }
    }
    // Preflight-only readback validates all metadata with no ownership/encoding,
    // even if no owners could be admitted for execution.
    TextureTransferFixture fixture(Kind::TextureBuffer);
    auto command = fixture.context.command(0u);
    assert(command->recordBlitPassBegin(fixture.context.diagnostic));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(command->preflightBlitTextureToBuffer(fixture.toBuffer, fixture.context.diagnostic));
    assert(!command->retentionStorage()->resourceOwnerCount()
        && !command->stats()->nativeBlitCopyEncoded);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    assert(!fixture.record(command));
    fixture.complete(command);
    fixture.verify(fixture.sentinel);
    std::puts(
        "textureTransferNegatives: zero/one/one-less owner, last-mip order/region/range, overlap, open/sealed cancellation and validation-only preserve atomic ownership/native mutation boundary");
}

void verifyPlacedTransferOwners()
{
    for (unsigned mode = 0; mode < 4; ++mode) {
        TextureTransferFixture f(TextureTransferFixture::Kind::TextureBuffer, true);
        auto rejected = f.context.command(8u);
        assert(rejected->recordBlitPassBegin(f.context.diagnostic));
        assert(
            !f.record(rejected) && f.context.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        assert(!rejected->retentionStorage()->resourceOwnerCount()
            && !rejected->stats()->nativeBlitCopyEncoded);
        rejected->abandonUnsubmitted();
        rejected.reset();
        auto command = f.context.command(9u);
        assert(command->recordBlitPassBegin(f.context.diagnostic));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaLease pressure(
                f.context.quota, f.context.quota->limitBytes() - f.context.quota->liveBytes());
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            for (unsigned repeat = 0; repeat < 32; ++repeat)
                assert(f.record(command));
            std::pmr::set_default_resource(previous);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        assert(command->retentionStorage()->resourceOwnerCount() == 9u);
        auto& native = *metalRhiNativePayloadFor(*f.source);
        std::weak_ptr<void> object = native.resource.nativeObject,
                            token = native.resource.allocationReleaseToken, heap = native.aliasHeap;
        const auto& replacement = metalRhiNativePayloadFor(*f.destination)->resource;
        native.resource.nativeObject = replacement.nativeObject;
        native.resource.allocationReleaseToken = replacement.allocationReleaseToken;
        native.aliasHeap.reset();
        auto observed = f.dropSource();
        f.heap.reset();
        assert(!observed.expired() && !object.expired() && !token.expired() && !heap.expired());
        if (mode != 0)
            assert(command->recordBlitPassEnd(f.context.diagnostic)
                && command->finish(f.context.diagnostic));
        if (mode < 2) {
            command->abandonUnsubmitted();
            assert(observed.expired() && object.expired() && token.expired() && heap.expired());
            command.reset();
            assert(!f.context.quota->liveBytes());
            continue;
        }
        auto gate = createMetalRhiNativeSharedEventPayload("PlacedTransferDelay");
        assert(gate && gate->nativeRuntimeBacked);
        auto completion = command->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(command->submit(queue, completion, gate, 1u, {}, 0u, f.context.diagnostic));
        if (mode == 3)
            command.reset();
        assert(
            !completion->isComplete() && !object.expired() && !token.expired() && !heap.expired());
        signalMetalRhiNativeSharedEvent(gate, 1u);
        completion->wait();
        assert(observed.expired() && object.expired() && token.expired() && heap.expired());
        f.verify(f.pixels);
    }
    std::puts(
        "placedTransfer: queried heap, exact owner9/one-less rejection, record32 allocation0, native object/token/heap replacements retain originals through cancellation and delayed completion");
}

void verifyTextureViewTransfers()
{
    using Kind = TextureTransferFixture::Kind;
    for (const auto kind :
        {Kind::BufferTexture, Kind::StagingTexture, Kind::TextureBuffer, Kind::TextureTexture}) {
        for (unsigned lifetime = 0u; lifetime < 3u; ++lifetime) {
            TextureTransferFixture fixture(kind);
            const TextureViewDesc viewDesc{"TransferMipLayerView",
                TextureFormat::Rgba8Unorm,
                TextureUsage::CopySrc | TextureUsage::CopyDst,
                TextureViewDimension::Texture2DArray,
                1u,
                2u,
                1u,
                1u};
            auto sourceView = fixture.context.device->createTextureView(*fixture.source, viewDesc);
            auto destinationView =
                fixture.context.device->createTextureView(*fixture.destination, viewDesc);
            assert(sourceView && destinationView);
            fixture.context.registry->registerTextureView(*sourceView);
            fixture.context.registry->registerTextureView(*destinationView);
            const TextureSubresourceRange viewRange{0u, 2u, 0u, 1u, TextureAspect::Color};
            fixture.toTexture.destinationTexture = sourceView->id();
            fixture.toTexture.textureSubresources = viewRange;
            fixture.toBuffer.sourceTexture = sourceView->id();
            fixture.toBuffer.textureSubresources = viewRange;
            fixture.textureCopy.sourceTexture = sourceView->id();
            fixture.textureCopy.destinationTexture = destinationView->id();
            fixture.textureCopy.sourceSubresources = fixture.textureCopy.destinationSubresources =
                viewRange;
            fixture.upload.destinationTexture = sourceView->id();
            fixture.upload.subresources = viewRange;
            fixture.toTexture.mipLayouts.clear();
            fixture.toBuffer.mipLayouts.clear();
            fixture.textureCopy.mipLayouts.clear();
            fixture.upload.mipLayouts.clear();
            auto packed = fixture.sentinel, parentExpected = fixture.sentinel;
            const std::array<std::uint64_t, 2u> viewOffsets{0u, fixture.pitch * 2u};
            for (unsigned mip = 0u; mip < 2u; ++mip) {
                const auto extent = fixture.extents[mip + 1u];
                fixture.toTexture.mipLayouts.push_back(
                    {mip, extent, fixture.pitch, extent.height, viewOffsets[mip]});
                fixture.toBuffer.mipLayouts.push_back(
                    {mip, extent, fixture.pitch, extent.height, viewOffsets[mip]});
                fixture.textureCopy.mipLayouts.push_back({mip, extent});
                fixture.upload.mipLayouts.push_back(
                    {mip, extent, fixture.pitch, extent.height, viewOffsets[mip]});
                for (unsigned row = 0u; row < extent.height; ++row) {
                    const auto parentOffset = fixture.offsets[mip + 1u]
                        + fixture.pitch * extent.height + fixture.pitch * row;
                    std::memcpy(packed.data() + viewOffsets[mip] + fixture.pitch * row,
                        fixture.pixels.data() + parentOffset,
                        extent.width * 4u);
                    std::memcpy(parentExpected.data() + parentOffset,
                        fixture.pixels.data() + parentOffset,
                        extent.width * 4u);
                }
            }
            assert(fixture.context.device->writeBuffer(*fixture.input, packed.data(), packed.size())
                    .success);
            std::memcpy(fixture.slice.mappedData, packed.data(), packed.size());
            assert(prepareMetalRhiNativeStagingBufferForGpu(fixture.slice.payload,
                fixture.slice.offsetBytes,
                packed.size(),
                fixture.context.diagnostic));
            auto command = fixture.context.command(fixture.ownerCount());
            assert(command->recordBlitPassBegin(fixture.context.diagnostic));
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned repeat = 0u; repeat < 32u; ++repeat)
                assert(fixture.record(command));
            assert(command->retentionStorage()->resourceOwnerCount() == fixture.ownerCount()
                && core::defaultCpuAllocator().stats().allocationCount == allocations);
            // For view-source operations the issuing view is the sole C++
            // source payload owner. It keeps native storage and its exact
            // allocation token; retaining the unrelated parent wrapper is wrong.
            std::weak_ptr<MetalRhiNativeTexturePayload> parent;
            std::weak_ptr<MetalRhiNativeTextureViewPayload> view;
            std::weak_ptr<void> allocation, originalObject;
            if (kind == Kind::TextureBuffer || kind == Kind::TextureTexture) {
                parent = metalRhiNativePayloadFor(*fixture.source);
                view = metalRhiNativePayloadFor(*sourceView);
                allocation = metalRhiNativePayloadFor(*sourceView)->resource.allocationReleaseToken;
                assert(!allocation.expired());
                auto& resource = metalRhiNativePayloadFor(*sourceView)->resource;
                originalObject = resource.nativeObject;
                resource.nativeObject =
                    metalRhiNativePayloadFor(*fixture.destination)->resource.nativeObject;
                resource.allocationReleaseToken =
                    metalRhiNativePayloadFor(*fixture.destination)->resource.allocationReleaseToken;
                sourceView.reset();
                fixture.source.reset();
                assert(parent.expired() && !view.expired() && !allocation.expired()
                    && !originalObject.expired());
            }
            if (lifetime == 2u) {
                command->abandonUnsubmitted();
                assert(view.expired() && allocation.expired() && originalObject.expired());
                command.reset();
                assert(!fixture.context.quota->liveBytes());
                continue;
            }
            assert(command->recordBlitPassEnd(fixture.context.diagnostic)
                && command->finish(fixture.context.diagnostic));
            auto gate = createMetalRhiNativeSharedEventPayload("TextureViewDelay");
            assert(gate && gate->nativeRuntimeBacked);
            auto completion = command->preparedCompletionState();
            MetalRhiNativeQueueStats queue;
            assert(
                command->submit(queue, completion, gate, 1u, {}, 0u, fixture.context.diagnostic));
            assert(!completion->isComplete());
            if (kind == Kind::TextureBuffer || kind == Kind::TextureTexture)
                assert(!view.expired() && !allocation.expired());
            if (lifetime == 1u)
                command.reset();
            signalMetalRhiNativeSharedEvent(gate, 1u);
            completion->wait();
            assert(view.expired() && allocation.expired() && originalObject.expired());
            if (kind == Kind::TextureBuffer)
                fixture.verify(packed);
            else {
                // Read the entire parent texture, proving view-relative native
                // mip/layer indices leave all other parent regions untouched.
                fixture.toBuffer.sourceTexture =
                    kind == Kind::TextureTexture ? fixture.destination->id() : fixture.source->id();
                fixture.toBuffer.textureSubresources = fixture.range;
                fixture.toBuffer.mipLayouts.clear();
                for (unsigned mip = 0u; mip < 3u; ++mip)
                    fixture.toBuffer.mipLayouts.push_back({mip,
                        fixture.extents[mip],
                        fixture.pitch,
                        fixture.extents[mip].height,
                        fixture.offsets[mip]});
                fixture.verify(parentExpected);
            }
        }
    }
    std::puts(
        "textureViews: all four transfers allocate0; relative mips0..1/layer0 map exactly to parent mips1..2/layer1; exact view/allocation token retirement, parent payload not retained");
}

void verifyTextureTransferExhaustionAndSeals()
{
    using Kind = TextureTransferFixture::Kind;
    for (const auto kind : {Kind::BufferTexture,
             Kind::StagingTexture,
             Kind::TextureBuffer,
             Kind::TextureTexture,
             Kind::StagingBuffer}) {
        TextureTransferFixture fixture(kind);
        auto command = fixture.context.command(8u);
        assert(command->recordBlitPassBegin(fixture.context.diagnostic));
        assert(command->recordBlitCopy(fixture.context.copy, fixture.context.diagnostic));
        assert(!fixture.record(command)
            && fixture.context.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        assert(command->retentionStorage()->resourceOwnerCount() == 8u);
        fixture.complete(command);
        fixture.verify(fixture.sentinel);
    }
    for (const auto kind : {Kind::TextureBuffer, Kind::StagingBuffer})
        for (const bool bounded : {false, true}) {
            TextureTransferFixture fixture(kind);
            auto command =
                bounded ? fixture.context.command(fixture.ownerCount()) : fixture.ordinary();
            fixture.output->sealWrites();
            assert(command->recordBlitPassBegin(fixture.context.diagnostic));
            assert(!fixture.record(command) && !command->stats()->nativeBlitCopyEncoded);
            assert(!bounded || !command->retentionStorage()->resourceOwnerCount());
            fixture.complete(command);
            fixture.verify(fixture.sentinel);
        }
    std::puts(
        "textureExhaustion: full owner storage rejects new batches atomically; sealed transfer buffers remain unchanged");
}

void verifyCountRejection()
{
    for (const auto count : {0u, 1u, 7u}) {
        CopyFixture fixture;
        auto payload = fixture.command(count);
        assert(payload->recordBlitPassBegin(fixture.diagnostic));
        assert(!payload->recordBlitCopy(fixture.copy, fixture.diagnostic));
        assert(fixture.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY");
        assert(payload->retentionStorage()->resourceOwnerCount() == 0u
            && !payload->stats()->nativeBlitCopyEncoded);
        assert(
            payload->recordBlitPassEnd(fixture.diagnostic) && payload->finish(fixture.diagnostic));
        auto completion = payload->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(payload->submit(queue, completion, {}, 0u, {}, 0u, fixture.diagnostic));
        completion->wait();
        fixture.verify(fixture.sentinel);
    }
    std::puts(
        "ownerCount: zero/one/one-less reject the whole two-buffer batch before native copy; destination sentinel preserved");
}

void verifySealedDestination()
{
    for (const bool bounded : {false, true}) {
        CopyFixture fixture;
        auto payload = bounded ? fixture.command()
                               : createMetalRhiNativeCommandPayload(
                                     "SealedUnboundedCopy", fixture.registry, &fixture.diagnostic);
        assert(payload);
        fixture.destination->sealWrites();
        assert(payload->recordBlitPassBegin(fixture.diagnostic));
        assert(!payload->recordBlitCopy(fixture.copy, fixture.diagnostic));
        assert(fixture.diagnostic == "RHI-METAL-BLIT-BUFFER-COPY-RESOURCES-INVALID"
            && !payload->stats()->nativeBlitCopyEncoded);
        assert(!bounded || payload->retentionStorage()->resourceOwnerCount() == 0u);
        assert(
            payload->recordBlitPassEnd(fixture.diagnostic) && payload->finish(fixture.diagnostic));
        auto completion = payload->preparedCompletionState();
        MetalRhiNativeQueueStats queue;
        assert(payload->submit(queue, completion, {}, 0u, {}, 0u, fixture.diagnostic));
        completion->wait();
        fixture.verify(fixture.sentinel);
    }
}

void verifyCopyAndCompletion(bool delayed, bool dropPayload)
{
    CopyFixture fixture;
    auto payload = fixture.command();
    assert(payload->recordComputePassBegin(fixture.diagnostic));
    assert(!payload->recordComputePipelineBind({}, fixture.diagnostic));
    assert(fixture.diagnostic == "RHI-METAL-COMMAND-RETENTION-OPERATION-UNIMPLEMENTED");
    assert(!payload->recordDispatch(1u, 1u, 1u, fixture.diagnostic));
    assert(payload->recordComputePassEnd(fixture.diagnostic));
    assert(!payload->recordRenderPassBegin({}, fixture.diagnostic));
    assert(!payload->recordDescriptorSetBind(
        0u, {}, ShaderStage::Compute, nullptr, 0u, fixture.diagnostic));
    assert(!payload->recordUploadStagingBufferCopy({}, 0u, 0u, {}, fixture.diagnostic));
    assert(!payload->recordTimestamp({}, 0u, fixture.diagnostic));
    assert(!payload->recordDispatchIndirect({}, 0u, fixture.diagnostic));
    assert(!payload->executeSecondary({}, fixture.diagnostic));
    assert(!payload->recordPushDebugGroup("Unsupported", fixture.diagnostic));
    assert(!payload->recordBufferBarrier(fixture.diagnostic));
    assert(!payload->scheduleBindlessFaultReadback({}, fixture.diagnostic));
    assert(payload->retentionStorage()->resourceOwnerCount() == 0u);
    assert(payload->recordBlitPassBegin(fixture.diagnostic));
    const auto charged = fixture.quota->liveBytes();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    auto invalid = fixture.copy;
    invalid.sourceBuffer = {};
    assert(!payload->recordBlitCopy(invalid, fixture.diagnostic));
    assert(payload->retentionStorage()->resourceOwnerCount() == 0u);
    for (unsigned i = 0u; i < 32u; ++i)
        assert(payload->recordBlitCopy(fixture.copy, fixture.diagnostic));
    assert(payload->retentionStorage()->resourceOwnerCount() == 8u);
    assert(fixture.quota->liveBytes() == charged
        && core::defaultCpuAllocator().stats().allocationCount == allocations);
    auto excess = fixture.device->createBuffer({"ExcessCopySource",
        sizeof(fixture.sentinel),
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc});
    assert(excess);
    assert(fixture.device->writeBuffer(*excess, fixture.sentinel.data(), sizeof(fixture.sentinel))
            .success);
    fixture.registry->registerBuffer(*excess);
    auto excessCopy = fixture.copy;
    excessCopy.sourceBuffer = excess->id();
    assert(!payload->recordBlitCopy(excessCopy, fixture.diagnostic));
    assert(fixture.diagnostic == "RHI-COMMAND-RETENTION-OWNER-CAPACITY"
        && payload->retentionStorage()->resourceOwnerCount() == 8u);
    assert(payload->recordBlitPassEnd(fixture.diagnostic));
    const auto beforeFinish = core::defaultCpuAllocator().stats().allocationCount;
    assert(payload->finish(fixture.diagnostic));
    assert(core::defaultCpuAllocator().stats().allocationCount == beforeFinish);
    assert(!payload->recordBlitPassBegin(fixture.diagnostic)
        && fixture.diagnostic == "RHI-COMMAND-RETENTION-RECORDING-CLOSED");
    std::weak_ptr<MetalRhiNativeBufferPayload> observed = metalRhiNativePayloadFor(*fixture.source);
    const auto& sourceResource = metalRhiNativePayloadFor(*fixture.source)->resource;
    std::weak_ptr<void> originalObject = sourceResource.nativeObject,
                        originalToken = sourceResource.allocationReleaseToken;
    auto& mutableSource = metalRhiNativePayloadFor(*fixture.source)->resource;
    mutableSource.nativeObject = metalRhiNativePayloadFor(*excess)->resource.nativeObject;
    mutableSource.allocationReleaseToken =
        metalRhiNativePayloadFor(*excess)->resource.allocationReleaseToken;
    fixture.source.reset();
    assert(!observed.expired());
    assert(!originalObject.expired() && !originalToken.expired());
    auto gate = delayed ? createMetalRhiNativeSharedEventPayload("BudgetCopyGate") : nullptr;
    assert(!delayed || (gate && gate->nativeRuntimeBacked));
    auto completion = payload->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    const auto beforeSubmit = core::defaultCpuAllocator().stats().allocationCount;
    assert(payload->submit(queue, completion, gate, 1u, {}, 0u, fixture.diagnostic));
    assert(
        core::defaultCpuAllocator().stats().allocationCount - beforeSubmit == (delayed ? 1u : 0u));
    assert(!payload->retentionStorage()); // Ownership moved, logical payload cannot retire it.
    if (delayed) {
        assert(!completion->isComplete() && !observed.expired());
        payload->abandonUnsubmitted();
        assert(!observed.expired());
    }
    if (dropPayload)
        payload.reset();
    if (delayed)
        signalMetalRhiNativeSharedEvent(gate, 1u);
    completion->wait();
    assert(observed.expired() && originalObject.expired() && originalToken.expired());
    fixture.verify(fixture.input);
    assert(!payload || payload->readbackCompletionStatus() == MetalReadbackCompletion::Completed);
    std::printf(
        "nativeCopy: delayed=%d dropped=%d exact data, unchanged output after rejected excess copy, completion retires registered source\n",
        delayed,
        dropPayload);
}

void verifyCancellationAndBudgetFailures()
{
    for (const bool sealed : {false, true}) {
        CopyFixture fixture;
        auto payload = fixture.command();
        assert(payload->recordBlitPassBegin(fixture.diagnostic)
            && payload->recordBlitCopy(fixture.copy, fixture.diagnostic));
        std::weak_ptr<MetalRhiNativeBufferPayload> observed =
            metalRhiNativePayloadFor(*fixture.source);
        fixture.source.reset();
        assert(!observed.expired());
        if (sealed)
            assert(payload->recordBlitPassEnd(fixture.diagnostic)
                && payload->finish(fixture.diagnostic));
        payload->abandonUnsubmitted();
        assert(observed.expired()
            && payload->readbackCompletionStatus() == MetalReadbackCompletion::Abandoned);
        assert(!payload->recordBlitPassBegin(fixture.diagnostic));
        payload.reset();
        assert(fixture.quota->liveBytes() == 0u);
    }
    {
        CopyFixture fixture;
        auto storage = fixture.prepare();
        core::CpuMemoryQuotaLease pressure(
            fixture.quota, fixture.quota->limitBytes() - fixture.quota->liveBytes());
        assert(pressure.bytes() > 0u);
        const auto capacity = inspectSharedMetalRhiCommandCapacity().charged;
        assert(!createMetalRhiNativeCommandPayload("PreparationExhaustion",
            fixture.registry,
            &fixture.diagnostic,
            0u,
            0u,
            0u,
            std::move(storage)));
        assert(fixture.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED");
        assert(inspectSharedMetalRhiCommandCapacity().charged == capacity
            && fixture.quota->liveBytes() == pressure.bytes());
        pressure.release();
        assert(fixture.quota->liveBytes() == 0u);
    }
    {
        CopyFixture fixture;
        auto payload = fixture.command();
        core::CpuMemoryQuotaLease pressure(
            fixture.quota, fixture.quota->limitBytes() - fixture.quota->liveBytes());
        assert(!payload->recordBlitPassBegin(fixture.diagnostic));
        assert(fixture.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED"
            && payload->readbackCompletionStatus() == MetalReadbackCompletion::Failed);
        assert(!payload->finish(fixture.diagnostic));
        pressure.release();
        payload.reset();
        assert(fixture.quota->liveBytes() == 0u);
    }
    {
        CopyFixture fixture;
        auto payload = fixture.command();
        assert(payload->recordBlitPassBegin(fixture.diagnostic)
            && payload->recordBlitCopy(fixture.copy, fixture.diagnostic));
        assert(
            payload->recordBlitPassEnd(fixture.diagnostic) && payload->finish(fixture.diagnostic));
        std::weak_ptr<MetalRhiNativeBufferPayload> observed =
            metalRhiNativePayloadFor(*fixture.source);
        fixture.source.reset();
        assert(!observed.expired());
        auto gate = createMetalRhiNativeSharedEventPayload("FailBeforeCommitGate");
        assert(gate && gate->nativeRuntimeBacked);
        auto completion = payload->preparedCompletionState();
        core::CpuMemoryQuotaLease pressure(
            fixture.quota, fixture.quota->limitBytes() - fixture.quota->liveBytes());
        MetalRhiNativeQueueStats queue;
        assert(!payload->submit(queue, completion, gate, 1u, {}, 0u, fixture.diagnostic));
        assert(fixture.diagnostic == "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED"
            && completion->isComplete() && observed.expired() && queue.nativeQueueSubmitCount == 0u
            && payload->readbackCompletionStatus() == MetalReadbackCompletion::Failed);
        fixture.verify(fixture.sentinel);
        pressure.release();
        completion.reset();
        payload.reset();
        assert(fixture.quota->liveBytes() == 0u);
    }
    std::puts(
        "nativeBudget: open/sealed cancellation, command/encoder/wait metadata exhaustion, precommit failure and exact charge cleanup pass");
}

void verifyPreparationAndQuotaGates()
{
    for (unsigned mode = 0u; mode < 4u; ++mode) {
        CopyFixture fixture;
        auto storage = fixture.prepare();
        if (mode == 0u)
            assert(storage->abandonUnsubmitted());
        if (mode == 1u) {
            const std::shared_ptr<const void> owner = metalRhiNativePayloadFor(*fixture.source);
            assert(!storage->retainResources(&owner, 1u));
        }
        if (mode == 2u)
            fixture.quota->setLimitBytes(storage->budget().maxCpuMetadataBytes + 1u);
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        const auto chargedCapacity = inspectSharedMetalRhiCommandCapacity().charged;
        assert(!createMetalRhiNativeCommandPayload("InvalidPreparedStorage",
            fixture.registry,
            &fixture.diagnostic,
            mode == 3u ? 1u : 0u,
            0u,
            0u,
            std::move(storage)));
        assert(fixture.diagnostic == "RHI-METAL-COMMAND-RETENTION-PREPARATION-INVALID");
        assert(fixture.quota->liveBytes() == 0u
            && inspectSharedMetalRhiCommandCapacity().charged == chargedCapacity
            && core::defaultCpuAllocator().stats().allocationCount == before);
    }
    CopyFixture fixture;
    auto payload = fixture.command();
    const auto limit = payload->retentionStorage()->budget().maxCpuMetadataBytes;
    fixture.quota->setLimitBytes(limit + 1u);
    assert(!payload->recordBlitPassBegin(fixture.diagnostic));
    assert(fixture.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH"
        && !payload->stats()->nativeBlitEncoderCreated);
    fixture.quota->setLimitBytes(limit);
    assert(payload->recordBlitPassBegin(fixture.diagnostic)
        && payload->recordBlitCopy(fixture.copy, fixture.diagnostic));
    assert(payload->recordBlitPassEnd(fixture.diagnostic) && payload->finish(fixture.diagnostic));
    auto completion = payload->preparedCompletionState();
    MetalRhiNativeQueueStats queue;
    fixture.quota->setLimitBytes(limit + 1u);
    assert(!payload->submit(queue, completion, {}, 0u, {}, 0u, fixture.diagnostic));
    assert(fixture.diagnostic == "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH"
        && !payload->readbackWasSubmitted());
    fixture.quota->setLimitBytes(limit);
    assert(payload->submit(queue, completion, {}, 0u, {}, 0u, fixture.diagnostic));
    completion->wait();
    fixture.verify(fixture.input);
    std::puts(
        "preparationGates: closed/prefilled/changed-quota/indexed requests reject before native creation; changed quota cannot relax recording or submission budget");
}
} // namespace

int main()
{
    verifyCountRejection();
    verifySealedDestination();
    verifyCopyAndCompletion(false, false);
    verifyCopyAndCompletion(true, false);
    verifyCopyAndCompletion(true, true);
    verifyCancellationAndBudgetFailures();
    verifyPreparationAndQuotaGates();
    verifyTextureTransfers();
    verifyTextureTransferRejections();
    verifyPlacedTransferOwners();
    verifyTextureViewTransfers();
    verifyTextureTransferExhaustionAndSeals();
}
