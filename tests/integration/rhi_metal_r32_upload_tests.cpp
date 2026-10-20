#include "../fixtures/render_pass_arguments_fixture.h"
#include "renderer/gi/ddgi_terrain_surface_gpu.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_texture_inventory_observation.h"
#include "rhi/frontend/transfer_readback_statistics.h"
#include "rhi/frontend/transfer_texture_readback_batch.h"
#include "rhi/gpu_extensions.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/metal_native_identity_fixture.h"

#include <cassert>
#include <cstdio>
#include <cstring>

void verifyMetalStorageTextureUsage(arp::rhi::GpuDevice& device);

namespace {

std::shared_ptr<arp::core::CpuMemoryQuota> verifyGuardedTextureInventory(
    const arp::rhi_metal::MetalRhiDeviceDesc& deviceDesc)
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi_metal;
    using Observation = MetalRhiTextureInventoryObservation;
    auto quota = core::makeShared<core::CpuMemoryQuota>(deviceDesc.memoryBudgetBytes);
    core::CpuMemoryQuotaScope scope(quota);
    auto device = createMetalRhiDevice(deviceDesc);
    assert(device);
    const auto registry = metalRhiIssuingResourceRegistry(*device);
    auto texture = device->createTexture({"GuardedInventory",
        {4u, 2u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
        3u,
        1u,
        1u});
    assert(texture);
    TextureViewDesc viewDesc{"GuardedMipView", TextureFormat::Rgba8Unorm, TextureUsage::Sampled};
    viewDesc.baseMipLevel = 1u;
    auto view = device->createTextureView(*texture, viewDesc);
    assert(view);
    const auto nativeDevice = metalRhiNativePayloadFor(*texture)->resource.nativeDevice;
    Observation observed;
    const auto capture = [&](Observation& output) {
        return Observation::capture(registry, nativeDevice, view->id(), output);
    };
    const auto* id = capture(observed);
    if (id)
        std::fprintf(stderr, "Inventory: %s\n", id);
    assert(!id && !observed.validate() && observed.values().range.baseMipLevel == 1u);
    assert(observed.view().textureView->extent.width == 2u
        && observed.view().textureView->extent.height == 1u);
    assert(observed.borrowedResidentBytes() == texture->allocation().estimatedSizeBytes);
    auto repeat = observed;
    assert(!repeat.validate());
    Observation failed;
    assert(Observation::capture({}, nativeDevice, view->id(), failed)
        && !failed.values().view.isValid());
    assert(
        Observation::capture(registry, {}, view->id(), failed) && !failed.values().view.isValid());
    auto foreign = createMetalRhiDevice(deviceDesc);
    assert(foreign);
    auto other = foreign->createTexture({"Foreign",
        {4u, 2u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(other);
    assert(Observation::capture(
               metalRhiIssuingResourceRegistry(*foreign), nativeDevice, view->id(), failed)
        && !failed.values().view.isValid());
    other.reset();
    foreign.reset();
    const auto mutate = [&](auto& field, auto replacement) {
        const auto saved = field;
        field = replacement;
        assert(observed.validate());
        assert(capture(failed) && !failed.values().view.isValid());
        field = saved;
        assert(!observed.validate());
    };
    auto& parentDesc = const_cast<TextureDesc&>(texture->desc());
    mutate(parentDesc.format, TextureFormat::R32Float);
    mutate(parentDesc.extent.width, 8u);
    mutate(parentDesc.sampleCount, 2u);
    mutate(parentDesc.usage, TextureUsage::CopySrc);
    auto& desc = const_cast<TextureViewDesc&>(view->desc());
    mutate(desc.baseMipLevel, 0u);
    mutate(desc.dimension, TextureViewDimension::Texture2DArray);
    mutate(desc.mipLevelCount, 2u);
    mutate(desc.format, TextureFormat::R32Float);
    mutate(desc.usage, TextureUsage::CopySrc);
    auto& parentNative = *observed.parent().texture;
    auto& viewNative = *observed.view().textureView;
    mutate(parentNative.extent.width, 8u);
    mutate(viewNative.extent.height, 2u);
    mutate(viewNative.resource.nativeRuntimeDiagnosticOnly, true);
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        viewNative.resource.nativeDevice, parentNative.resource.nativeObject, [&] {
            assert(observed.validate());
        });
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        viewNative.resource.allocationReleaseToken, parentNative.resource.nativeObject, [&] {
            assert(observed.validate());
        });
    test::MetalNativeIdentityMutationProbe::verifyOwner(
        parentNative.resource.nativeObject, viewNative.resource.nativeObject, [&] {
            assert(observed.validate());
        });
    ResourceBarrierDesc barrier{
        texture->id(), observed.values().range, ResourceState::ShaderRead, ResourceState::CopySrc};
    assert(setMetalRhiTextureStateForBarrier(*texture, barrier).success && observed.validate());
    std::swap(barrier.before, barrier.after);
    assert(setMetalRhiTextureStateForBarrier(*texture, barrier).success && !observed.validate());
    registry->unregisterResource(view->id());
    assert(observed.validate());
    registry->registerTextureView(*view);
    assert(!observed.validate());
    registry->unregisterResource(texture->id());
    assert(observed.validate());
    registry->registerTexture(*texture);
    assert(!observed.validate());
    const auto charge = device->memoryStats().allocatedBytes;
    auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!observed.validate());
            Observation current;
            assert(!capture(current));
        }
        // Native storage stays owned; expired wrapper tickets must reject before
        // dereference. No view/parent shared_ptr can masquerade as wrapper owner.
        texture.reset();
        assert(device->memoryStats().allocatedBytes == charge
            && !observed.parent().textureWrapper.get());
        for (unsigned i = 0u; i < 100u; ++i)
            assert(observed.validate());
        view.reset();
        assert(!observed.view().textureViewWrapper.get());
        for (unsigned i = 0u; i < 100u; ++i)
            assert(observed.validate());
    }
    std::pmr::set_default_resource(previous);
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    assert(device->memoryStats().allocatedBytes == charge);
    std::weak_ptr<void> object = parentNative.resource.nativeObject,
                        token = parentNative.resource.allocationReleaseToken;
    observed = {};
    assert(!object.expired() && !token.expired());
    repeat = {};
    failed = {};
    assert(object.expired() && token.expired() && !device->memoryStats().allocatedBytes);
    std::puts(
        "MetalGuardedTextureInventory/1: real native mip1 parent/view queries, original owners, wrapper retirement, retained charge and100 allocation0 observations PASS; no GPU work");
    return quota;
}

void verifyUpload(arp::rhi::GpuDevice& device,
    std::uint32_t width,
    std::uint64_t serial,
    arp::rhi::TextureFormat format = arp::rhi::TextureFormat::R32Float,
    std::uint32_t nonfiniteChannel = UINT32_MAX)
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    constexpr std::uint32_t height = 3u;
    const auto pixelBytes = bytesPerPixel(format);
    const auto aligned =
        arp::core::alignCpuAllocationOffsetUp(width * pixelBytes, kTextureUploadRowPitchAlignment);
    assert(aligned.success);
    const auto pitch = static_cast<std::uint32_t>(aligned.alignedValue);
    arp::core::pmr::Vector<std::uint8_t> bytes(pitch * height, 0u);
    for (std::uint32_t z = 0; z < height; ++z) {
        for (std::uint32_t x = 0; x < width; ++x) {
            auto* pixel = bytes.data() + z * pitch + x * pixelBytes;
            if (format == TextureFormat::R32Float) {
                const float value = static_cast<float>(x) * 0.25f - static_cast<float>(z) * 17.5f;
                std::memcpy(pixel, &value, sizeof(value));
            } else if (format == TextureFormat::Rgba16Float) {
                // Distinct finite half-float channels and asymmetric rows/columns
                // expose channel swaps, incorrect pitch and partial copies.
                const std::uint16_t channels[]{static_cast<std::uint16_t>(0x3c00u + x),
                    static_cast<std::uint16_t>(0xc000u + z),
                    static_cast<std::uint16_t>(0x3800u + x + z),
                    0x4000u};
                std::memcpy(pixel, channels, sizeof(channels));
            } else {
                assert(format == TextureFormat::R8Unorm);
                *pixel = static_cast<std::uint8_t>(x + z * 17u);
            }
        }
    }
    if (nonfiniteChannel != UINT32_MAX) {
        assert(
            format == TextureFormat::Rgba16Float && nonfiniteChannel > 0u && nonfiniteChannel < 4u);
        const std::uint16_t nan = 0x7e00u;
        std::memcpy(bytes.data() + nonfiniteChannel * sizeof(nan), &nan, sizeof(nan));
    }
    const auto expectedBytes = bytes;
    const auto expectedStatistics =
        analyzeTextureReadback(format, bytes.data(), bytes.size(), width, height, pitch);
    assert(expectedStatistics.valid);
    auto texture = device.createTexture({"R32UploadTexture",
        {width, height, 1u},
        format,
        TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    assert(texture && inspectMetalRhiNativePayload(*texture).nativeRuntimeBacked);
    TextureViewDesc viewDesc;
    viewDesc.format = format;
    viewDesc.usage = TextureUsage::Sampled;
    auto view = device.createTextureView(*texture, viewDesc);
    assert(view && inspectMetalRhiNativePayload(*view).nativeRuntimeBacked);
    auto copiedTexture = device.createTexture({"TextureCopyDestination",
        {width, height, 1u},
        format,
        TextureUsage::CopyDst | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    assert(copiedTexture && inspectMetalRhiNativePayload(*copiedTexture).nativeRuntimeBacked);
    auto readback = device.createBuffer({"R32UploadReadback",
        bytes.size(),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(readback);
    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Color};
    auto encoder = device.createCommandEncoder({"R32Upload"});
    assert(encoder);
    assert(encoder->transition(*texture, range, ResourceState::Undefined, ResourceState::CopyDst)
            .success);
    auto transfer = encoder->beginTransferPass({"R32UploadTransfer"});
    assert(transfer);
    UploadTextureDesc upload;
    upload.destinationTexture = texture->id();
    upload.extent = {width, height, 1u};
    upload.format = format;
    upload.bytesPerRow = pitch;
    upload.rowsPerImage = height;
    upload.subresources = range;
    upload.pixelData = bytes.data();
    upload.pixelDataSize = static_cast<std::uint32_t>(bytes.size());
    upload.backendOwnedStaging = true;
    upload.frameSerial = serial;
    upload.resourceGeneration = serial;
    assert(transfer->uploadTexture(upload).success);
    // Recording must own the upload bytes before the producer reuses them.
    std::memset(bytes.data(), 0xcd, bytes.size());
    assert(transfer->end().success);
    assert(encoder->transition(*texture, range, ResourceState::CopyDst, ResourceState::ShaderRead)
            .success);
    assert(encoder->transition(*texture, range, ResourceState::ShaderRead, ResourceState::CopySrc)
            .success);
    assert(
        encoder->transition(*copiedTexture, range, ResourceState::Undefined, ResourceState::CopyDst)
            .success);
    transfer = encoder->beginTransferPass({"TextureCopyTransfer"});
    assert(transfer);
    assert(transfer
            ->copyTextureToTexture(
                {texture->id(), copiedTexture->id(), range, range, {width, height, 1u}})
            .success);
    assert(transfer->end().success);
    assert(
        encoder->transition(*copiedTexture, range, ResourceState::CopyDst, ResourceState::CopySrc)
            .success);
    transfer = encoder->beginTransferPass({"R32ReadbackTransfer"});
    assert(transfer);
    assert(transfer
            ->copyTextureToBuffer({copiedTexture->id(),
                readback->id(),
                range,
                0u,
                pitch,
                height,
                {width, height, 1u}})
            .success);
    assert(transfer->end().success);
    auto* extension =
        static_cast<TextureReadbackExtension*>(device.queryInterface(kExtensionTextureReadback));
    assert(extension);
    std::string extensionDiagnostic;
    arp::core::pmr::String budgetDiagnostic;
    const auto readbackBeforeBudget = extension->budgetStats();
    if (serial == 1u) {
        assert(!readbackBeforeBudget.configured);
        assert(!extension->schedule(*encoder, *texture, "r32", extensionDiagnostic));
        assert(extensionDiagnostic == "RHI-READBACK-BUDGET-REQUIRED");
        assert(extension->budgetStats().budgetRejectionCount == 1u);
    }
    assert(extension->configureBudget({bytes.size() * 2u - 1u, 1u}, budgetDiagnostic));
    assert(!extension->schedule(*encoder, *texture, "r32", extensionDiagnostic));
    assert(extensionDiagnostic == "RHI-READBACK-BYTE-LIMIT");
    const auto byteRejected = extension->budgetStats();
    assert(byteRejected.configured);
    assert(byteRejected.capacityBytes == bytes.size() * 2u - 1u);
    assert(byteRejected.chargedBytes == 0u && byteRejected.peakChargedBytes == 0u);
    assert(byteRejected.budgetRejectionCount == 1u);
    assert(extension->configureBudget({bytes.size() * 2u, 1u}, budgetDiagnostic));
    assert(extension->schedule(*encoder, *texture, "r32", extensionDiagnostic));
    const auto scheduledReadback = extension->budgetStats();
    assert(scheduledReadback.chargedBytes == bytes.size() * 2u);
    assert(scheduledReadback.peakChargedBytes == scheduledReadback.chargedBytes);
    assert(scheduledReadback.pendingRequestCount == 1u);
    TextureReadbackSample extensionSample;
    assert(!extension->poll(extensionSample));
    assert(!extension->configureBudget({bytes.size() * 2u, 1u}, budgetDiagnostic));
    assert(budgetDiagnostic == "RHI-READBACK-BUDGET-IN-FLIGHT");
    assert(!extension->schedule(*encoder, *texture, "second", extensionDiagnostic));
    assert(extensionDiagnostic == "RHI-READBACK-PENDING-LIMIT");
    const auto pendingRejected = extension->budgetStats();
    assert(pendingRejected.budgetRejectionCount == 1u);
    const auto physicalBudget = device.physicalBudgetLedger();
    assert(physicalBudget.valid());
    const auto& readbackBudget =
        physicalBudget.category(arp::rhi::semantics::PhysicalBudgetCategory::TextureReadback);
    assert(readbackBudget.observed());
    assert(readbackBudget.unit == arp::rhi::semantics::PhysicalBudgetUnit::Bytes);
    assert(readbackBudget.chargeRelation
        == arp::rhi::semantics::PhysicalBudgetChargeRelation::OverlapsResourceAllocation);
    assert(readbackBudget.capacity == pendingRejected.capacityBytes);
    assert(readbackBudget.current == pendingRejected.chargedBytes);
    assert(readbackBudget.peak == pendingRejected.peakChargedBytes);
    assert(readbackBudget.rejectionCount == pendingRejected.budgetRejectionCount);
    assert(readbackBudget.liveCount == pendingRejected.pendingRequestCount);
    auto commands = encoder->finish();
    assert(commands && device.queue().submit(*commands).success);
    device.waitIdle();
    assert(extension->poll(extensionSample));
    const auto drainedReadback = extension->budgetStats();
    assert(drainedReadback.chargedBytes == 0u);
    assert(drainedReadback.peakChargedBytes == bytes.size() * 2u);
    assert(extensionSample.valid && extensionSample.sampleCount == width * height);
    assert(extensionSample.channelStatisticsVersion == 1u);
    const auto channelCount = format == TextureFormat::Rgba16Float ? 4u : 1u;
    assert(extensionSample.channelStatistics.channelCount == channelCount);
    assert(extensionSample.channelStatistics.valid == (nonfiniteChannel == UINT32_MAX));
    for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
        const auto& stats = extensionSample.channelStatistics.channels[channel];
        assert(stats.sampleCount == width * height);
        assert(stats.finiteCount == width * height - (channel == nonfiniteChannel ? 1u : 0u));
        assert(stats.valid == (channel != nonfiniteChannel));
    }
    if (nonfiniteChannel != UINT32_MAX)
        assert(extensionSample.diagnostic == "RHI-READBACK-NONFINITE");
    else
        assert(extensionSample.diagnostic.empty());
    assert(extensionSample.minValue == expectedStatistics.minValue
        && extensionSample.maxValue == expectedStatistics.maxValue
        && extensionSample.sampleHash == expectedStatistics.sampleHash);
    assert(!extension->poll(extensionSample));
    assert(extension->supportsTrackedRequests());
    auto tracked = device.createCommandEncoder({"TrackedReadback"});
    TextureReadbackBatch<1> batch;
    assert(batch.begin(serial + 100u, serial + 200u));
    assert(batch.expect("tracked", *texture) && batch.seal());
    const auto identity = batch.requestIdentity(0u);
    assert(!extension->scheduleTracked(*tracked, *texture, "tracked", {}, budgetDiagnostic));
    assert(budgetDiagnostic == "RHI-READBACK-IDENTITY-INVALID");
    assert(batch.schedule(*extension, *tracked, 0, *texture));
    assert(
        !extension->scheduleTracked(*tracked, *texture, "duplicate", identity, budgetDiagnostic));
    assert(budgetDiagnostic == "RHI-READBACK-IDENTITY-DUPLICATE");
    assert(!extension->poll(extensionSample));
    auto trackedCommands = tracked->finish();
    assert(trackedCommands && device.queue().submit(*trackedCommands).success);
    device.waitIdle();
    assert(extension->poll(extensionSample));
    assert(extensionSample.identity == identity && extensionSample.source == texture->id());
    assert(extensionSample.textureFormat == format);
    assert(extensionSample.completion == TextureReadbackCompletion::Completed);
    // Prove the consumer rejects actual GPU G/B/A nonfinite payloads even
    // when the compatible legacy R-channel validity is true.
    const bool expectedValid = nonfiniteChannel == UINT32_MAX;
    assert(batch.consume(extensionSample) == expectedValid);
    assert(batch.complete() == expectedValid && batch.finish() == expectedValid);
    assert(!extension->poll(extensionSample));
    if (serial == 1u) {
        auto abandoned = device.createCommandEncoder({"AbandonedReadback"});
        TextureReadbackBatch<1> abandonedBatch;
        assert(abandonedBatch.begin(identity.frameSerial, identity.resourceGeneration));
        assert(abandonedBatch.expect("abandoned", *texture) && abandonedBatch.seal());
        assert(abandonedBatch.schedule(*extension, *abandoned, 0, *texture));
        abandoned.reset();
        assert(extension->poll(extensionSample) && !extensionSample.valid);
        assert(extensionSample.diagnostic == "RHI-READBACK-COMMAND-ABANDONED");
        assert(extensionSample.identity == abandonedBatch.requestIdentity(0u));
        assert(extensionSample.identity.requestId != identity.requestId);
        assert(extensionSample.completion == TextureReadbackCompletion::Abandoned);
        assert(!abandonedBatch.consume(extensionSample) && !abandonedBatch.finish());

        assert(extension->configureBudget({bytes.size() * 4u, 2u}, budgetDiagnostic));
        TextureReadbackBatch<1> first;
        TextureReadbackBatch<2> second;
        assert(first.begin(identity.frameSerial, identity.resourceGeneration));
        assert(second.begin(identity.frameSerial, identity.resourceGeneration));
        assert(first.expect("cross-batch", *texture) && first.seal());
        assert(second.expect("cross-batch", *texture) && second.seal());
        assert(first.requestIdentity(0u).requestId != second.requestIdentity(0u).requestId);
        auto sharedCommands = device.createCommandEncoder({"TwoReadbackBatches"});
        assert(sharedCommands);
        assert(first.schedule(*extension, *sharedCommands, 0u, *texture));
        assert(second.schedule(*extension, *sharedCommands, 0u, *texture));
        auto sharedSubmission = sharedCommands->finish();
        assert(sharedSubmission && device.queue().submit(*sharedSubmission).success);
        device.waitIdle();
        TextureReadbackSample firstResult, secondResult;
        assert(extension->poll(firstResult) && extension->poll(secondResult));
        if (firstResult.identity == second.requestIdentity(0u))
            std::swap(firstResult, secondResult);
        assert(firstResult.identity == first.requestIdentity(0u) && firstResult.valid);
        assert(secondResult.identity == second.requestIdentity(0u) && secondResult.valid);
        assert(!first.consume(secondResult) && !second.consume(firstResult));
        assert(first.diagnostic() == "RHI-READBACK-RESULT-UNEXPECTED");
        assert(second.diagnostic() == "RHI-READBACK-RESULT-UNEXPECTED");
        assert(first.counts().completed == 0u && second.counts().completed == 0u);
        assert(!extension->poll(extensionSample));
        std::puts(
            "TextureReadbackBatchIsolation/1 actual same-frame/source/label GPU results reject cross-batch consumption: PASS");
        for (unsigned mode = 0u; mode != 4u; ++mode) {
            const auto ownershipBaseline = device.memoryStats();
            assert(extension->configureBudget({bytes.size() * 4u, 2u}, budgetDiagnostic));
            TextureReadbackBatch<2> discardedBatch;
            assert(discardedBatch.begin(identity.frameSerial, identity.resourceGeneration));
            assert(discardedBatch.expect("discard-a", *texture));
            assert(discardedBatch.expect("discard-b", *texture) && discardedBatch.seal());
            auto recording = device.createCommandEncoder({"MultiReadbackOwnership"});
            auto nativeObserver = metalRhiNativePayloadFor(*recording);
            assert(discardedBatch.schedule(*extension, *recording, 0u, *texture));
            assert(discardedBatch.schedule(*extension, *recording, 1u, *texture));
            assert(!extension->poll(extensionSample));
            if (mode == 0u) {
                recording.reset();
            } else {
                auto finished = recording->finish();
                assert(finished);
                if (mode == 1u || mode == 3u) {
                    recording.reset();
                    assert(!extension->poll(extensionSample));
                }
                if (mode == 3u) {
                    assert(device.queue().submit(*finished).success);
                }
                finished.reset();
                if (mode == 3u)
                    device.waitIdle();
            }
            for (std::uint32_t i = 0u; i != 2u; ++i) {
                assert(extension->poll(extensionSample));
                assert(extensionSample.identity == discardedBatch.requestIdentity(i));
                assert(extensionSample.completion
                    == (mode == 3u ? TextureReadbackCompletion::Completed
                                   : TextureReadbackCompletion::Abandoned));
                assert(extensionSample.valid == (mode == 3u));
                if (mode != 3u)
                    assert(extensionSample.diagnostic == "RHI-READBACK-COMMAND-ABANDONED");
            }
            assert(!extension->poll(extensionSample));
            assert(extension->configureBudget({bytes.size() * 4u, 2u}, budgetDiagnostic));
            recording.reset();
            nativeObserver.reset();
            assert(device.memoryStats().allocatedBytes == ownershipBaseline.allocatedBytes);
            assert(device.memoryStats().liveResourceCount == ownershipBaseline.liveResourceCount);
        }
        std::puts(
            "TextureReadbackCommandAbandonment/1 multi-request public ownership and submitted retirement: PASS");
    }
    std::pmr::string diagnostics;
    assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
        bytes.data(),
        bytes.size(),
        0u,
        readback->desc().sizeBytes,
        diagnostics));
    const auto statistics =
        analyzeTextureReadback(format, bytes.data(), bytes.size(), width, height, pitch);
    assert(statistics.valid && statistics.sampleCount == width * height);
    assert(statistics.minValue == expectedStatistics.minValue
        && statistics.maxValue == expectedStatistics.maxValue
        && statistics.sampleHash == expectedStatistics.sampleHash);
    for (std::uint32_t z = 0; z < height; ++z) {
        assert(std::memcmp(
                   bytes.data() + z * pitch, expectedBytes.data() + z * pitch, width * pixelBytes)
            == 0);
    }
    std::printf("Texture upload/copy/readback format=%u width=%u height=%u pitch=%u: PASS\n",
        static_cast<unsigned>(format),
        width,
        height,
        pitch);
}

void verifyRenderedCopy(arp::rhi::GpuDevice& device, bool placed)
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    constexpr std::uint32_t width = 64u, height = 32u, pitch = 512u;
    TextureDesc desc{"RenderedCopySource",
        {width, height, 1u},
        TextureFormat::Rgba16Float,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        placed ? MemoryUsage::Transient : MemoryUsage::GpuOnly,
        ResourceState::Undefined};
    const auto requirements = device.queryTexturePlacementRequirements(desc);
    auto heap = placed
        ? device.createMemoryHeap(
              requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient)
        : nullptr;
    assert(!placed || heap);
    auto source = placed ? device.createPlacedTexture(desc, *heap, 0u) : device.createTexture(desc);
    if (!source)
        std::fprintf(stderr,
            "placed source failed size=%llu alignment=%llu\n",
            static_cast<unsigned long long>(requirements.sizeBytes),
            static_cast<unsigned long long>(requirements.alignmentBytes));
    assert(source);
    auto alias = placed ? device.createPlacedTexture(desc, *heap, 0u) : nullptr;
    assert(!placed || alias);
    auto destination = device.createTexture({"RenderedCopyDestination",
        desc.extent,
        desc.format,
        TextureUsage::CopyDst | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto buffer = device.createBuffer({"RenderedCopyReadback",
        pitch * height,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(destination && buffer);
    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Color};
    arp::core::pmr::Vector<std::uint8_t> bytes(pitch * height);
    for (unsigned iteration = 0; iteration < (placed ? 1u : 16u); ++iteration) {
        auto encoder = device.createCommandEncoder({"RenderedCopy"});
        assert(encoder);
        assert(encoder
                ->transition(*source,
                    range,
                    iteration ? ResourceState::CopySrc : ResourceState::Undefined,
                    ResourceState::ColorAttachment)
                .success);
        RenderPassDesc render;
        render.label = "RenderedCopyClear";
        RenderPassColorAttachmentDesc attachment;
        attachment.target = source->id();
        attachment.format = desc.format;
        attachment.loadOp = LoadOp::Clear;
        attachment.storeOp = StoreOp::Store;
        attachment.clearColor = {iteration % 2 ? 0.5f : 0.25f, 0.5f, 0.75f, 1.0f};
        render.colorAttachments.push_back(attachment);
        auto pass = encoder->beginRenderPass(render);
        assert(pass && pass->end().success);
        assert(encoder
                ->transition(*source, range, ResourceState::ColorAttachment, ResourceState::CopySrc)
                .success);
        assert(encoder
                ->transition(*destination,
                    range,
                    iteration ? ResourceState::CopySrc : ResourceState::Undefined,
                    ResourceState::CopyDst)
                .success);
        auto transfer = encoder->beginTransferPass({"RenderedTextureCopy"});
        assert(transfer
            && transfer
                ->copyTextureToTexture({source->id(), destination->id(), range, range, desc.extent})
                .success);
        assert(transfer->end().success);
        assert(
            encoder->transition(*destination, range, ResourceState::CopyDst, ResourceState::CopySrc)
                .success);
        if (alias) {
            // Reuse the source storage after its final copy read. The snapshot
            // must retain the first color even when the alias is overwritten.
            AliasingBarrierDesc barrier;
            barrier.beforeResource = source->id();
            barrier.afterResource = alias->id();
            barrier.aliasGroup = 1u;
            barrier.sourceStages = PipelineStage::Transfer;
            barrier.destinationStages = PipelineStage::ColorAttachmentOutput;
            barrier.sourceAccess = ResourceAccess::TransferRead;
            barrier.destinationAccess = ResourceAccess::ColorAttachmentWrite;
            assert(encoder->aliasBarrier(barrier).success);
            assert(encoder
                    ->transition(
                        *alias, range, ResourceState::Undefined, ResourceState::ColorAttachment)
                    .success);
            render.colorAttachments[0].target = alias->id();
            render.colorAttachments[0].clearColor = {1.0f, 0.0f, 0.0f, 1.0f};
            pass = encoder->beginRenderPass(render);
            assert(pass && pass->end().success);
        }
        transfer = encoder->beginTransferPass({"RenderedTextureReadback"});
        assert(transfer
            && transfer
                ->copyTextureToBuffer(
                    {destination->id(), buffer->id(), range, 0u, pitch, height, desc.extent})
                .success);
        assert(transfer->end().success);
        auto commands = encoder->finish();
        assert(commands && device.queue().submit(*commands).success);
        device.waitIdle();
        arp::core::pmr::String diagnostic;
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*buffer),
            bytes.data(),
            bytes.size(),
            0u,
            bytes.size(),
            diagnostic));
        const std::uint16_t expected[]{
            static_cast<std::uint16_t>(iteration % 2 ? 0x3800 : 0x3400), 0x3800, 0x3a00, 0x3c00};
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const bool matches =
                    std::memcmp(
                        bytes.data() + y * pitch + x * sizeof(expected), expected, sizeof(expected))
                    == 0;
                if (!matches)
                    std::fprintf(stderr,
                        "render/copy mismatch placed=%d iteration=%u x=%u y=%u\n",
                        placed,
                        iteration,
                        x,
                        y);
                assert(matches);
            }
        }
    }
    std::printf("RGBA16 render/copy/readback placed=%d: PASS\n", placed);
}

void verifyDepthAttachmentInput(arp::rhi::GpuDevice& device)
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    constexpr std::uint32_t width = 3u, height = 2u;
    const auto aligned = arp::core::alignCpuAllocationOffsetUp(
        width * bytesPerPixel(TextureFormat::D32Float), kTextureUploadRowPitchAlignment);
    assert(aligned.success && aligned.alignedValue <= UINT32_MAX);
    const auto pitch = static_cast<std::uint32_t>(aligned.alignedValue);
    const auto readbackBytes = pitch * height;
    auto depth = device.createTexture({"DepthStencilInputClear",
        {width, height, 1u},
        TextureFormat::D32Float,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined});
    auto output = device.createBuffer({"DepthStencilInputReadback",
        readbackBytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(depth && output && inspectMetalRhiNativePayload(*depth).nativeRuntimeBacked);
    const TextureSubresourceRange range{0u, 1u, 0u, 1u, TextureAspect::Depth};
    arp::core::pmr::Vector<std::uint8_t> bytes(readbackBytes);
    bool first = true;
    for (const auto expected : {0.125f, 0.375f, 0.875f}) {
        auto encoder = device.createCommandEncoder({"DepthStencilInputClearReadback"});
        assert(encoder);
        assert(encoder
                ->transition(*depth,
                    range,
                    first ? ResourceState::Undefined : ResourceState::CopySrc,
                    ResourceState::DepthAttachment)
                .success);
        RenderPassDesc pass;
        pass.hasDepthStencilAttachment = true;
        pass.depthStencilAttachment.target = depth->id();
        pass.depthStencilAttachment.format = depth->desc().format;
        pass.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
        pass.depthStencilAttachment.depthStoreOp = StoreOp::Store;
        pass.depthStencilAttachment.depthClearValue = expected;
        const auto frozen = arp::test::RenderPassArgumentsFixture::freezeDepthPass(pass);
        pass.depthStencilAttachment.depthClearValue = 1.0f;
        assert(!frozen->matches(pass));
        auto render = encoder->beginRenderPass(frozen->desc());
        assert(render && render->end().success);
        assert(encoder
                ->transition(*depth, range, ResourceState::DepthAttachment, ResourceState::CopySrc)
                .success);
        auto transfer = encoder->beginTransferPass({"DepthStencilInputCopy"});
        assert(transfer);
        assert(transfer
                ->copyTextureToBuffer(
                    {depth->id(), output->id(), range, 0u, pitch, height, {width, height, 1u}})
                .success);
        assert(transfer->end().success);
        auto commands = encoder->finish();
        assert(commands && device.queue().submit(*commands).hasAcceptedWork());
        device.waitIdle();
        arp::core::pmr::String diagnostic;
        assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*output),
            bytes.data(),
            bytes.size(),
            0u,
            bytes.size(),
            diagnostic));
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x) {
                float actual = 0.0f;
                std::memcpy(&actual, bytes.data() + y * pitch + x * sizeof(actual), sizeof(actual));
                assert(actual == expected);
            }
        first = false;
    }
    std::puts(
        "DepthStencilAttachmentInput/1 Metal: real depth clear/readback 0.125 0.375 0.875 PASS");
}

void verifyHeapBudget(arp::rhi_metal::MetalRhiDeviceDesc desc)
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    TextureDesc texture{"HeapBudget",
        {64u, 32u, 1u},
        TextureFormat::Rgba16Float,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        MemoryUsage::Transient,
        ResourceState::Undefined};
    TexturePlacementRequirements requirements;
    assert(queryMetalRhiNativeTexturePlacementRequirements(texture, requirements));
    auto nativeHeap = createMetalRhiNativeHeap(requirements.sizeBytes, requirements.alignmentBytes);
    assert(nativeHeap && nativeHeap->allocationSizeBytes >= requirements.sizeBytes);
    const auto chargedBytes = nativeHeap->allocationSizeBytes;
    desc.memoryBudgetBytes = chargedBytes - 1u;
    auto insufficient = createMetalRhiDevice(desc);
    assert(insufficient);
    assert(!insufficient->createMemoryHeap(
        requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient));
    assert(insufficient->memoryStats().allocatedBytes == 0u);
    desc.memoryBudgetBytes = chargedBytes;
    auto exact = createMetalRhiDevice(desc);
    assert(exact);
    auto heap = exact->createMemoryHeap(
        requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient);
    assert(heap && heap->sizeBytes() == requirements.sizeBytes);
    assert(exact->memoryStats().allocatedBytes == chargedBytes);
    assert(!exact->createMemoryHeap(
        requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient));
    heap.reset();
    assert(exact->memoryStats().allocatedBytes == 0u);
    assert(exact->createMemoryHeap(
        requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient));
    std::printf("Heap capacity requested=%llu charged=%llu budget/release: PASS\n",
        static_cast<unsigned long long>(requirements.sizeBytes),
        static_cast<unsigned long long>(chargedBytes));
}

void verifyRetainedAllocationBudget(arp::rhi_metal::MetalRhiDeviceDesc desc)
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    constexpr std::uint64_t bytes = 256u;
    const auto originalBudget = desc.memoryBudgetBytes;
    desc.memoryBudgetBytes = bytes * 2u;
    auto device = createMetalRhiDevice(desc);
    assert(device);
    auto source = device->createBuffer({"RetainedSource",
        bytes,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc});
    auto target = device->createBuffer({"RetainedTarget",
        bytes,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(source && target);
    std::array<std::uint8_t, bytes> input{};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<std::uint8_t>(i);
    assert(device->writeBuffer(*source, input.data(), input.size()).success);
    auto encoder = device->createCommandEncoder({"RetainedCopy"});
    auto transfer = encoder->beginTransferPass({"RetainedCopy"});
    assert(transfer
        && transfer->copyBufferToBuffer({source->id(), target->id(), 0, 0, bytes}).success);
    assert(transfer->end().success);
    transfer.reset();
    auto commands = encoder->finish();
    assert(commands);
    source.reset();
    std::fprintf(stderr,
        "retained buffer expected=%llu actual=%llu\n",
        static_cast<unsigned long long>(bytes * 2u),
        static_cast<unsigned long long>(device->memoryStats().allocatedBytes));
    assert(device->memoryStats().allocatedBytes == bytes * 2u);
    assert(!device->createBuffer({"CannotReuseRetainedBytes",
        1u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst}));
    assert(device->queue().submit(*commands).success);
    device->waitIdle();
    std::array<std::uint8_t, bytes> output{};
    arp::core::pmr::String diagnostic;
    assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*target),
        output.data(),
        output.size(),
        0,
        output.size(),
        diagnostic));
    assert(input == output);
    encoder.reset();
    commands.reset();
    assert(device->memoryStats().allocatedBytes == bytes);
    target.reset();
    assert(device->memoryStats().allocatedBytes == 0u);

    desc.memoryBudgetBytes = originalBudget;
    device = createMetalRhiDevice(desc);
    assert(device);
    TextureDesc textureDesc{"RetainedTexture",
        {8u, 8u, 1u},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled | TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined};
    auto texture = device->createTexture(textureDesc);
    assert(texture);
    auto view = device->createTextureView(*texture, {"RetainedView"});
    assert(view && inspectMetalRhiNativePayload(*view).nativeRuntimeBacked);
    const auto textureCharge = device->memoryStats().allocatedBytes;
    assert(textureCharge > 0u);
    texture.reset();
    assert(device->memoryStats().allocatedBytes == textureCharge);
    view.reset();
    assert(device->memoryStats().allocatedBytes == 0u);

    textureDesc.memory = MemoryUsage::Transient;
    TexturePlacementRequirements requirements;
    assert(queryMetalRhiNativeTexturePlacementRequirements(textureDesc, requirements));
    auto heap = device->createMemoryHeap(
        requirements.sizeBytes, requirements.alignmentBytes, MemoryUsage::Transient);
    assert(heap);
    const auto heapCharge = device->memoryStats().allocatedBytes;
    texture = device->createPlacedTexture(textureDesc, *heap, 0u);
    assert(texture);
    view = device->createTextureView(*texture, {"PlacedRetainedView"});
    assert(view);
    auto parentArena = device->createDescriptorArena({"PlacedParentArena", 2u, 2u});
    assert(parentArena);
    DescriptorLayoutDesc parentLayout;
    parentLayout.name = "PlacedParent";
    parentLayout.bindings.push_back({0u, DescriptorType::SampledTexture, ShaderStage::Fragment});
    auto parentSet = parentArena->allocateDescriptorSet({"PlacedParentSet", parentLayout});
    auto parentTable = parentArena->allocateDescriptorTable(
        {"PlacedParentTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 1u});
    assert(parentSet && parentTable);
    const DescriptorBindingWrite parentWrite{
        0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView};
    const DescriptorTableWrite parentTableWrite{
        0u, DescriptorType::SampledTexture, view->id(), DescriptorResourceKind::TextureView};
    assert(parentSet->write(parentWrite).success && parentTable->write(parentTableWrite).success);
    // DescriptorResourceLiveness/1 stores logical IDs. Explicit backend
    // snapshots own selected native storage; writing metadata alone is not
    // a recorded/native storage owner or permission to retain a stale ID.
    auto setSnapshot = metalRhiNativePayloadFor(*parentSet);
    auto tableSnapshot = metalRhiNativePayloadFor(*parentTable);
    assert(setSnapshot && tableSnapshot);
    std::weak_ptr<MetalRhiNativeTextureViewPayload> retainedView = metalRhiNativePayloadFor(*view);
    assert(device->memoryStats().allocatedBytes == heapCharge);
    heap.reset();
    assert(device->memoryStats().allocatedBytes == heapCharge);
    assert(parentSet->validateBinding(0u).success && parentTable->validateDescriptor(0u).success);
    texture.reset();
    const auto revision = parentSet->contentRevision();
    const auto tableRevision = parentTable->contentRevision();
    assert(parentSet->validateBinding(0u).diagnostics.find("RHI-DESCRIPTOR-PARENT-NOT-LIVE") == 0u);
    assert(parentTable->validateDescriptor(0u).diagnostics.find("RHI-DESCRIPTOR-PARENT-NOT-LIVE")
        == 0u);
    assert(!parentSet->write(parentWrite).success && !parentTable->write(parentTableWrite).success);
    assert(parentSet->contentRevision() == revision
        && parentTable->contentRevision() == tableRevision);
    assert(device->memoryStats().allocatedBytes == heapCharge);
    assert(!device->createBuffer({"HeapStillRetained",
        originalBudget,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst}));
    view.reset();
    assert(!retainedView.expired() && device->memoryStats().allocatedBytes == heapCharge);
    setSnapshot.reset();
    assert(!retainedView.expired() && device->memoryStats().allocatedBytes == heapCharge);
    tableSnapshot.reset();
    assert(retainedView.expired() && device->memoryStats().allocatedBytes == 0u);
    // Live descriptor metadata/arena cannot extend native allocation charge.
    assert(!parentSet->validateBinding(0u).success && !parentTable->validateDescriptor(0u).success);
    parentSet.reset();
    parentTable.reset();
    parentArena.reset();
    assert(device->memoryStats().allocatedBytes == 0u);
    assert(device->createBuffer({"HeapBudgetReturned",
        originalBudget,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst}));
    std::puts("Native retained allocation and completion release: PASS");
}

} // namespace

int main(int argc, char** argv)
{
    arp::rhi_metal::MetalRhiDeviceDesc desc;
    desc.capabilityContract =
        arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    desc.capabilityContract.queriedFromNativeDevice = true;
    desc.capabilityContract.queriedExtendedFeatures = true;
    // Largest padded fixture is 512 bytes/row * 3 rows. One 2 KiB
    // staging page suffices because every upload waits for completion.
    desc.uploadStagingPageSizeBytes = 2048u;
    desc.uploadStagingMaxPageCount = 1u;
    desc.memoryBudgetBytes = 64u * 1024u;
    if (argc == 2 && std::strcmp(argv[1], "--guarded-texture-inventory") == 0) {
        const auto quota = verifyGuardedTextureInventory(desc);
        assert(!quota->liveBytes());
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--retained-allocation") == 0) {
        verifyRetainedAllocationBudget(desc);
        return 0;
    }
    auto device = arp::rhi_metal::createMetalRhiDevice(desc);
    assert(device);
    assert(device->capabilities().features.r32FloatSampledUpload);
    verifyMetalStorageTextureUsage(*device);
    std::uint64_t serial = 1u;
    for (const auto width : {3u, 64u, 65u})
        verifyUpload(*device, width, serial++);
    for (const auto width : {3u, 32u, 33u})
        verifyUpload(*device, width, serial++, arp::rhi::TextureFormat::Rgba16Float);
    verifyUpload(*device, 65u, serial++, arp::rhi::TextureFormat::R8Unorm);
    for (const auto channel : {1u, 2u, 3u})
        verifyUpload(*device, 3u, serial++, arp::rhi::TextureFormat::Rgba16Float, channel);
    verifyRenderedCopy(*device, false);
    verifyRenderedCopy(*device, true);
    verifyDepthAttachmentInput(*device);
    arp::renderer::DdgiBakedTerrainGrid grid;
    grid.bounds = {-1.0f, 1.0f, -1.0f, 1.0f};
    grid.samplesX = 3u;
    grid.samplesZ = 3u;
    grid.heights.assign(9u, -2.5f);
    arp::renderer::DdgiTerrainSurfaceGpuPayload terrain;
    arp::tests::CapabilityOverrideDevice unavailable(*device, [](auto& caps) {
        caps.features.r32FloatSampledUpload = false;
    });
    const auto denied = terrain.create(unavailable, grid, 1u);
    assert(!denied.success && !terrain.ready());
    assert(denied.diagnostics.find("R32SampledUpload/1") != std::pmr::string::npos);
    assert(!terrain.create(*device, grid, 0u).success);
    const auto uploaded = terrain.create(*device, grid, serial);
    if (!uploaded.success)
        std::fprintf(stderr, "DDGI upload failed: %s\n", uploaded.diagnostics.c_str());
    assert(
        uploaded.success && uploaded.status == arp::renderer::DdgiTerrainSurfaceGpuStatus::Native);
    assert(terrain.ready());
    assert(arp::rhi_metal::inspectMetalRhiNativePayload(*terrain.texture()).nativeRuntimeBacked);
    device->waitIdle();
    terrain.clear();
    assert(!terrain.ready());
    std::puts("DDGI terrain capability gate and native payload: PASS");
    verifyHeapBudget(desc);
}
