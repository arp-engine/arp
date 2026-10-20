#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

#include <cassert>

namespace {

using namespace arp;

void assertContains(const std::pmr::string& haystack, const char* needle)
{
    assert(haystack.find(needle) != std::pmr::string::npos);
}

constexpr std::uint8_t kTestPixelData[64] = {
    0xFF,
    0x00,
    0x00,
    0xFF,
    0x00,
    0xFF,
    0x00,
    0xFF,
    0x00,
    0x00,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
};

rhi::UploadTextureDesc makeUploadDesc(rhi::GpuResourceId destinationTexture)
{
    rhi::UploadTextureDesc desc;
    desc.label = "AtlasUpload";
    desc.destinationTexture = destinationTexture;
    desc.extent = {4u, 4u, 1u};
    desc.format = rhi::TextureFormat::Rgba8Unorm;
    desc.bytesPerRow = 256u;
    desc.rowsPerImage = 4u;
    desc.subresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
    desc.pixelData = kTestPixelData;
    desc.pixelDataSize = static_cast<std::uint32_t>(sizeof(kTestPixelData));
    desc.frameSerial = 1u;
    desc.resourceGeneration = 3u;
    return desc;
}

rhi_metal::MetalRhiDeviceDesc makeMetalDeviceDesc()
{
    auto contract = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;

    rhi_metal::MetalRhiDeviceDesc desc;
    desc.label = "RHIMetalUploadTextureContract";
    desc.capabilityContract = contract;
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.enableArgumentBuffers = true;
    desc.enablePushConstantUploadBuffer = true;
    desc.enableMemorylessTransientTextures = true;
    desc.enableDrawableLifecycleGuards = true;
    return desc;
}

rhi_webgpu::WebGpuRhiDeviceDesc makeWebGpuDeviceDesc()
{
    rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = true;
    probe.storageTextureBinding = true;
    probe.storageBuffer = true;
    probe.maxTexture2DSize = 4096;
    probe.maxColorAttachments = 4;
    probe.maxDescriptorBindings = 16;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    const auto profile = rhi::buildWebGpuRuntimeProfile(probe);

    const auto bridge = rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);
    assert(bridge.success);

    rhi_webgpu::WebGpuRhiDeviceDesc desc;
    desc.label = "RHIWebGpuUploadTextureContract";
    desc.runtimeProfile = profile;
    desc.capabilityContract = bridge.contract;
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.enableBackendDescriptorGroups = true;
    desc.enablePushConstantUploadRing = true;
    desc.pushConstantUploadRingByteCapacity = 4096;
    desc.enableCanvasLifecycleDiagnostics = true;
    desc.enableResizeDiagnostics = true;
    desc.enableSurfaceLostDiagnostics = true;
    desc.enableHiddenTabFrameSkipping = true;
    desc.requireComputeStorageRuntimePromotion = true;
    return desc;
}

void assertUploadTextureDescDefaults()
{
    rhi::UploadTextureDesc desc;
    assert(desc.format == rhi::TextureFormat::Unknown);
    assert(desc.stagingIntent == rhi::StagingIntent::Upload);
    assert(desc.backendOwnedStaging);
    assert(desc.pixelData == nullptr);
    assert(desc.pixelDataSize == 0u);
    assert(desc.bytesPerRow == 0u);
    assert(desc.rowsPerImage == 0u);
    assert(desc.mipLevel == 0u);
    assert(desc.arrayLayer == 0u);
    assert(desc.frameSerial == 0u);
    assert(desc.resourceGeneration == 0u);
    // destinationOffset defaults to (0,0,0) so full-texture uploads behave
    // identically to before the field existed.
    assert(desc.destinationOffset.width == 0u);
    assert(desc.destinationOffset.height == 0u);
    assert(desc.destinationOffset.depth == 0u);
}

void assertNullUploadTextureValidationAndRecording()
{
    auto device = rhi::createNullDevice();
    assert(device);

    auto texture = device->createTexture({
        "NullUploadTexture",
        {4u, 4u, 1u},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::CopyDst,
    });
    assert(texture);

    auto encoder = device->createCommandEncoder({"NullUploadTexture"});
    assert(encoder);
    auto transfer = encoder->beginTransferPass({"NullUploadTexture"});
    assert(transfer);

    auto invalid = makeUploadDesc(texture->id());
    invalid.bytesPerRow = 0u;
    const auto rejected = transfer->uploadTexture(invalid);
    assert(!rejected.success);
    assertContains(rejected.diagnostics, "bytesPerRow");

    const auto accepted = transfer->uploadTexture(makeUploadDesc(texture->id()));
    assert(accepted.success);
    assert(transfer->end().success);

    auto commands = encoder->finish();
    assert(commands);
    assert(commands->commandCount() == 3u);
}

// Sub-rect uploads (dynamic glyph atlas updates) must set destinationOffset so
// the native buffer-to-image copy lands in the correct atlas slot. This
// exercises the Null path with a non-zero offset to guard the L2 contract.
void assertNullSubRectUploadAcceptsDestinationOffset()
{
    auto device = rhi::createNullDevice();
    assert(device);

    auto texture = device->createTexture({
        "NullSubRectUploadTexture",
        {64u, 64u, 1u},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::CopyDst,
    });
    assert(texture);

    auto encoder = device->createCommandEncoder({"NullSubRectUpload"});
    assert(encoder);
    auto transfer = encoder->beginTransferPass({"NullSubRectUpload"});
    assert(transfer);

    auto subRect = makeUploadDesc(texture->id());
    subRect.destinationOffset = {24u, 16u, 0u}; // glyph slot at (24,16)
    subRect.extent = {8u, 8u, 1u};
    subRect.pixelDataSize = static_cast<std::uint32_t>(sizeof(kTestPixelData));
    const auto accepted = transfer->uploadTexture(subRect);
    assert(accepted.success);
    assert(transfer->end().success);

    auto commands = encoder->finish();
    assert(commands);
    assert(commands->commandCount() == 3u);
}

void assertMetalUploadTextureReportsDeterministicDiagnostic()
{
    auto device = rhi_metal::createMetalRhiDevice(makeMetalDeviceDesc());
    assert(device);

    auto texture = device->createTexture({
        "MetalUploadTexture",
        {4u, 4u, 1u},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::CopyDst,
    });
    assert(texture);

    auto encoder = device->createCommandEncoder({"MetalUploadTexture"});
    assert(encoder);
    auto transfer = encoder->beginTransferPass({"MetalUploadTexture"});
    assert(transfer);

    const auto result = transfer->uploadTexture(makeUploadDesc(texture->id()));
    assert(!result.success);
    assertContains(result.diagnostics, "not backend-validated");
    assertContains(result.diagnostics, "deterministic diagnostic");
}

void assertWebGpuUploadTextureReportsDeterministicDiagnostic()
{
    auto device = rhi_webgpu::createWebGpuRhiDevice(makeWebGpuDeviceDesc());
    assert(device);

    auto texture = device->createTexture({
        "WebGpuUploadTexture",
        {4u, 4u, 1u},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::CopyDst,
    });
    assert(texture);

    auto encoder = device->createCommandEncoder({"WebGpuUploadTexture"});
    assert(encoder);
    auto transfer = encoder->beginTransferPass({"WebGpuUploadTexture"});
    assert(transfer);

    const auto result = transfer->uploadTexture(makeUploadDesc(texture->id()));
    assert(!result.success);
    assertContains(result.diagnostics, "not backend-validated");
    assertContains(result.diagnostics, "deterministic diagnostic");
}

} // namespace

int main()
{
    assertUploadTextureDescDefaults();
    assertNullUploadTextureValidationAndRecording();
    assertNullSubRectUploadAcceptsDestinationOffset();
    assertMetalUploadTextureReportsDeterministicDiagnostic();
    assertWebGpuUploadTextureReportsDeterministicDiagnostic();
    return 0;
}
