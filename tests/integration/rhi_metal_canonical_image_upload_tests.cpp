#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_texture_inventory_observation.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
using namespace arp;
using namespace rhi;
using namespace rhi_metal;
using Content = CanonicalConsumerImageContentContract;
constexpr Extent3D parentExtent{7u, 5u, 1u};
constexpr std::uint32_t mipCount = 3u;
constexpr std::uint32_t fixtureImageCount = 3u;
constexpr std::uint64_t maximumPayload = kTextureUploadRowPitchAlignment * parentExtent.height;

ResourceBarrierDesc dependency(const Content& content, ResourceState before, ResourceState after)
{
    ResourceBarrierDesc intent;
    intent.resource = content.image().parent;
    intent.textureSubresources = content.image().range;
    intent.before = before;
    intent.after = after;
    const auto scope = [](ResourceState state, PipelineStage& stage, ResourceAccess& access) {
        switch (state) {
        case ResourceState::ShaderRead:
            stage = PipelineStage::FragmentShader;
            access = ResourceAccess::ShaderRead;
            break;
        case ResourceState::CopyDst:
            stage = PipelineStage::Transfer;
            access = ResourceAccess::TransferWrite;
            break;
        case ResourceState::CopySrc:
            stage = PipelineStage::Transfer;
            access = ResourceAccess::TransferRead;
            break;
        default:
            assert(false);
        }
    };
    scope(before, intent.sourceStages, intent.sourceAccess);
    scope(after, intent.destinationStages, intent.destinationAccess);
    return intent;
}

void verifyUpload(GpuDevice& device,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    std::uint32_t imageIndex,
    std::uint32_t mip,
    std::uint64_t serial)
{
    const auto before = device.memoryStats().allocatedBytes;
    auto texture = device.createTexture({"CanonicalNPOTImage",
        parentExtent,
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead,
        mipCount});
    assert(texture && inspectMetalRhiNativePayload(*texture).nativeRuntimeBacked);
    TextureViewDesc viewDesc{
        "CanonicalSelectedMip", TextureFormat::Rgba8Unorm, TextureUsage::Sampled};
    viewDesc.baseMipLevel = mip;
    auto view = device.createTextureView(*texture, viewDesc);
    assert(view);
    const auto registry = metalRhiIssuingResourceRegistry(device);
    BindlessTextureInventoryResource image;
    assert(!BindlessTextureInventoryResource::capture(*texture, *view, image));
    Extent3D extent;
    assert(!semantics::TextureSubresourceExtent::resolve(parentExtent, mipCount, mip, extent));
    const auto payloadSize = std::size_t{kTextureUploadRowPitchAlignment} * extent.height;
    core::ByteBuffer authoring(payloadSize, std::uint8_t{0xa5u}, &core::defaultCpuMemoryResource());
    // Every texel/channel is explicitly distinct across image, row, column,
    // selected mip; source padding has a separate pattern and is not texels.
    for (std::uint32_t y = 0u; y < extent.height; ++y)
        for (std::uint32_t x = 0u; x < extent.width; ++x)
            for (std::uint32_t channel = 0u; channel < bytesPerPixel(image.format); ++channel)
                authoring[y * kTextureUploadRowPitchAlignment + x * bytesPerPixel(image.format)
                    + channel] = static_cast<std::uint8_t>(imageIndex * 71u + mip * 29u + y * 13u
                    + x * 7u + channel * 3u);
    auto content = core::makeShared<Content>(Content::Input{image,
                                                 extent,
                                                 kTextureUploadRowPitchAlignment,
                                                 extent.height,
                                                 authoring.data(),
                                                 authoring.size()},
        Content::Budget{1u, true, payloadSize, payloadSize});
    assert(!content->diagnostic());
    Content::Revision revision{1u, image.view, image.parent, serial};
    MetalRhiTextureInventoryObservation observation;
    const auto native = metalRhiNativePayloadFor(*texture);
    assert(native && native->resource.storageMode == MetalRhiNativeStorageMode::Private);
    assert(!MetalRhiTextureInventoryObservation::capture(
        registry, native->resource.nativeDevice, view->id(), observation));
    auto readback = device.createBuffer({"CanonicalImageReadback",
        payloadSize,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst});
    assert(readback);
    core::ByteBuffer poison(payloadSize, std::uint8_t{0xcdu}, &core::defaultCpuMemoryResource());
    assert(device.writeBuffer(*readback, poison.data(), poison.size()).success);
    readback->sealHostWrites();
    const auto textureCharge = texture->allocation().estimatedSizeBytes,
               readbackCharge = readback->allocation().estimatedSizeBytes;
    assert(textureCharge && readbackCharge >= payloadSize);
    auto encoder = device.createCommandEncoder({"CanonicalImageUploadReadback"});
    assert(encoder);
    assert(encoder
            ->transition(
                *texture, dependency(*content, ResourceState::ShaderRead, ResourceState::CopyDst))
            .success);
    auto pass = encoder->beginTransferPass({"CanonicalImageUpload"});
    assert(pass);
    UploadTextureDesc upload;
    assert(!content->describeUpload(revision, serial, upload));
    assert(pass->uploadTexture(upload).success);
    assert(pass->end().success);
    // Snapshot and staging bytes must survive authoring reuse.
    authoring.assign(authoring.size(), std::uint8_t{0xeeu});
    assert(encoder
            ->transition(
                *texture, dependency(*content, ResourceState::CopyDst, ResourceState::ShaderRead))
            .success);
    assert(encoder
            ->transition(
                *texture, dependency(*content, ResourceState::ShaderRead, ResourceState::CopySrc))
            .success);
    pass = encoder->beginTransferPass({"CanonicalImageReadback"});
    assert(pass);
    TextureToBufferCopyDesc copy;
    copy.sourceTexture = image.parent;
    copy.destinationBuffer = readback->id();
    copy.textureSubresources = image.range;
    copy.bufferOffsetBytes = 0u;
    copy.bytesPerRow = content->bytesPerRow();
    copy.rowsPerImage = content->rowsPerImage();
    copy.extent = content->extent();
    assert(pass->copyTextureToBuffer(copy).success);
    assert(pass->end().success);
    assert(encoder
            ->transition(
                *texture, dependency(*content, ResourceState::CopySrc, ResourceState::ShaderRead))
            .success);
    auto command = encoder->finish();
    assert(command && command->commandCount() == 10u);
    const auto commandPayload = metalRhiNativePayloadFor(*command);
    assert(commandPayload && commandPayload->stats());
    const auto& stats = *commandPayload->stats();
    assert(stats.nativeRuntimeBacked && !stats.nativeRuntimeDiagnosticOnly
        && stats.nativeBlitCopyEncoded && stats.nativeResourceBarrierEncoded
        && stats.nativeCommandBufferFinished && !stats.nativeDrawEncoded
        && !stats.nativeIndexedDrawEncoded && !stats.nativeDispatchEncoded
        && !commandPayload->readbackWasSubmitted()
        && commandPayload->readbackCompletionStatus() == MetalReadbackCompletion::Pending);
    const auto submitBefore =
        inspectMetalRhiNativeQueueStats(device.queue()).nativeQueueSubmitCount;
    assert(device.queue().submit(*command).success);
    core::pmr::String diagnostic;
    assert(waitMetalRhiNativeQueueIdle(diagnostic));
    assert(commandPayload->readbackWasSubmitted()
        && commandPayload->readbackCompletionStatus() == MetalReadbackCompletion::Completed);
    const auto queue = inspectMetalRhiNativeQueueStats(device.queue());
    assert(queue.nativeRuntimeBacked && !queue.nativeRuntimeDiagnosticOnly
        && queue.nativeQueueSubmitCount == submitBefore + 1u);
    core::ByteBuffer actual(payloadSize, 0u, &core::defaultCpuMemoryResource());
    assert(readMetalRhiNativeBuffer(metalRhiNativePayloadFor(*readback),
        actual.data(),
        actual.size(),
        0u,
        readback->desc().sizeBytes,
        diagnostic));
    for (std::uint32_t y = 0u; y < extent.height; ++y) {
        const auto offset = std::size_t{y} * content->bytesPerRow();
        const auto rowBytes = std::size_t{extent.width} * bytesPerPixel(image.format);
        assert(!std::memcmp(actual.data() + offset, content->bytes().data() + offset, rowBytes));
    }
    assert(texture->subresourceState(image.range) == ResourceState::ShaderRead
        && !observation.validate());
    // Successful ordinary uploads remain mutable and cannot certify an
    // exclusive native revision. The canonical consumer stays fail-closed.
    const auto decision = Content::lowering(Backend::Metal);
    assert(!decision.executable
        && decision.diagnosticId
            == std::string_view("RHI-CANONICAL-CONSUMER-IMAGE-CONTENT-REVISION-PAYLOAD-MISSING"));
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    const auto cpuLiveBeforeProbe = quota->liveBytes();
    {
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        assert(pressure.charged());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        for (unsigned i = 0u; i < 100u; ++i) {
            assert(!observation.validate());
            assert(!content->validate(image, revision));
        }
        std::pmr::set_default_resource(previous);
    }
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    std::printf(
        "CanonicalImageUploadEvidence/1 image%u mip%u extent%ux%u pitch%u payload%zu textureCharge%llu readbackCharge%llu CPUliveBeforeProbe%llu operations%u nativeCompleted PASS; mutable revision unavailable\n",
        imageIndex,
        mip,
        extent.width,
        extent.height,
        content->bytesPerRow(),
        payloadSize,
        static_cast<unsigned long long>(textureCharge),
        static_cast<unsigned long long>(readbackCharge),
        static_cast<unsigned long long>(cpuLiveBeforeProbe),
        command->commandCount());
    // Native observations intentionally retain allocation charge after wrappers.
    readback.reset();
    view.reset();
    texture.reset();
    assert(observation.validate());
    assert(device.memoryStats().allocatedBytes >= before + textureCharge);
}
} // namespace

int main()
{
    using namespace arp;
    using namespace rhi;
    using namespace rhi_metal;
    // Explicit fixture capacities, separate from production budgets. Largest
    // selected-mip payload determines the sole staging page; calls are serial.
    auto quota = core::makeShared<core::CpuMemoryQuota>(256u * 1024u);
    {
        core::CpuMemoryQuotaScope scope(quota);
        MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.memoryBudgetBytes = 64u * 1024u;
        desc.uploadStagingPageSizeBytes = maximumPayload;
        desc.uploadStagingMaxPageCount = 1u;
        auto device = createMetalRhiDevice(desc);
        assert(device);
        for (std::uint32_t image = 0u; image < fixtureImageCount; ++image)
            for (std::uint32_t mip = 0u; mip < mipCount; ++mip)
                verifyUpload(
                    *device, quota, image, mip, std::uint64_t{image} * mipCount + mip + 1u);
        assert(waitMetalRhiNativeQueueIdle(desc.label));
        std::printf(
            "CanonicalImageUploadEvidence/1 submissions%u CPUpeakIncludingExhaustion%llu GPUpeak%llu finalGPU%llu; no revision/consumer/draw/visual acceptance\n",
            inspectMetalRhiNativeQueueStats(device->queue()).nativeQueueSubmitCount,
            static_cast<unsigned long long>(quota->peakBytes()),
            static_cast<unsigned long long>(device->memoryStats().peakAllocatedBytes),
            static_cast<unsigned long long>(device->memoryStats().allocatedBytes));
    }
    assert(!quota->liveBytes());
}
