#include "core/math/half_float.h"
#include "renderer/postprocess/taa_renderer.h"
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/frontend/transfer_readback_statistics.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using namespace arp;
using namespace arp::rhi;

constexpr std::uint32_t kWidth = 8u;
constexpr std::uint32_t kHeight = 4u;
constexpr std::uint32_t kPitch = kTextureUploadRowPitchAlignment;
constexpr std::uint64_t kImageBytes = static_cast<std::uint64_t>(kPitch) * kHeight;
constexpr Extent2D kExtent{kWidth, kHeight};
constexpr Extent3D kExtent3D{kWidth, kHeight, 1u};
constexpr TextureSubresourceRange kColorRange{0u, 1u, 0u, 1u, TextureAspect::Color};
constexpr TextureSubresourceRange kDepthRange{0u, 1u, 0u, 1u, TextureAspect::Depth};

template <typename Result> void check(const Result& result)
{
    if (!result.success)
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
    assert(result.success);
}

renderer::RenderFrameContext makeFrame(
    GpuTexture& output, std::uint32_t frameIndex, std::uint64_t frameSerial)
{
    AcquiredFrame acquired;
    acquired.success = true;
    acquired.frameIndex = frameIndex;
    acquired.frameSerial = frameSerial;
    return {
        acquired,
        {"TaaMetalOutput",
            output.id(),
            kExtent,
            TextureFormat::Rgba16Float,
            ResourceState::ColorAttachment},
        nullptr,
        &output,
        nullptr,
        nullptr,
    };
}

struct CaseResult {
    float historyMissCenter = 0.0f;
    float resolvedCenter = 0.0f;
    std::uint64_t historyMissHash = 0u;
    std::uint64_t resolvedHash = 0u;
};

class TaaMetalResolveFixture final {
public:
    TaaMetalResolveFixture()
    {
        rhi_metal::MetalRhiDeviceDesc desc;
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Metal);
        desc.capabilityContract.queriedFromNativeDevice = true;
        desc.capabilityContract.queriedExtendedFeatures = true;
        desc.uploadStagingPageSizeBytes = 16u * 1024u;
        desc.uploadStagingMaxPageCount = 1u;
        desc.memoryBudgetBytes = 256u * 1024u;
        device_ = rhi_metal::createMetalRhiDevice(desc);
        assert(device_);
        baselineBytes_ = device_->memoryStats().allocatedBytes;
    }

    CaseResult run(float velocityX, std::uint64_t serial, const char* label)
    {
        CaseResult result;
        {
            renderer::TaaRenderer taa(*device_);
            const auto created = taa.create({
                ARP_SHADER_DIR "/wocc_taa.slang",
                TextureFormat::Rgba16Float,
                kExtent,
            });
            check(created);

            auto current = makeTexture("TaaMetalCurrent",
                TextureFormat::Rgba16Float,
                TextureUsage::Sampled | TextureUsage::CopyDst,
                ResourceState::Undefined);
            auto velocity = makeTexture("TaaMetalVelocity",
                TextureFormat::Rg16Float,
                TextureUsage::Sampled | TextureUsage::CopyDst,
                ResourceState::Undefined);
            auto depth = makeTexture("TaaMetalDepth",
                TextureFormat::D32Float,
                TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc,
                ResourceState::Undefined);
            auto reactive = makeTexture("TaaMetalReactive",
                TextureFormat::R8Unorm,
                TextureUsage::Sampled | TextureUsage::CopyDst,
                ResourceState::Undefined);
            auto firstColor = makeTexture("TaaMetalFirstColor",
                TextureFormat::Rgba16Float,
                TextureUsage::RenderAttachment | TextureUsage::CopySrc,
                ResourceState::ColorAttachment);
            auto firstMoments = makeTexture("TaaMetalFirstMoments",
                TextureFormat::Rg16Float,
                TextureUsage::RenderAttachment | TextureUsage::CopySrc,
                ResourceState::ColorAttachment);
            auto secondColor = makeTexture("TaaMetalSecondColor",
                TextureFormat::Rgba16Float,
                TextureUsage::RenderAttachment | TextureUsage::CopySrc,
                ResourceState::ColorAttachment);
            auto secondMoments = makeTexture("TaaMetalSecondMoments",
                TextureFormat::Rg16Float,
                TextureUsage::RenderAttachment,
                ResourceState::ColorAttachment);
            auto readback = device_->createBuffer({"TaaMetalReadback",
                2u * kImageBytes,
                ResourceUsage::CopyDst,
                MemoryUsage::GpuToCpu,
                ResourceState::CopyDst});
            assert(current && velocity && depth && reactive && firstColor && firstMoments
                && secondColor && secondMoments && readback);

            uploadInputs(*current, *velocity, *depth, *reactive, velocityX, serial);

            renderer::TaaSettings settings;
            settings.quality = renderer::TaaSettings::Quality::Low;
            settings.blendFactor = 0.25f;
            settings.velocityBlendFloor = 0.25f;
            settings.velocityScale = 0.0f;

            taa.invalidateHistory();
            auto firstFrame = makeFrame(*firstColor, 0u, serial);
            check(taa.drawFrame(
                firstFrame, *current, *velocity, *depth, *reactive, *firstMoments, settings));
            device_->waitIdle();

            copyFirstFrameToHistory(taa, *firstColor, *firstMoments, *depth, *readback, serial);

            auto secondFrame = makeFrame(*secondColor, 1u, serial + 1u);
            check(taa.drawFrame(
                secondFrame, *current, *velocity, *depth, *reactive, *secondMoments, settings));
            device_->waitIdle();
            copyToReadback(*secondColor, *readback, kImageBytes);

            std::array<std::uint8_t, 2u * kImageBytes> bytes{};
            core::pmr::String diagnostics;
            assert(
                rhi_metal::readMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(*readback),
                    bytes.data(),
                    bytes.size(),
                    0u,
                    bytes.size(),
                    diagnostics));

            const auto firstStats = analyzeTextureReadback(
                TextureFormat::Rgba16Float, bytes.data(), kImageBytes, kWidth, kHeight, kPitch);
            const auto secondStats = analyzeTextureReadback(TextureFormat::Rgba16Float,
                bytes.data() + kImageBytes,
                kImageBytes,
                kWidth,
                kHeight,
                kPitch);
            assert(firstStats.valid && firstStats.finiteCount == kWidth * kHeight);
            assert(secondStats.valid && secondStats.finiteCount == kWidth * kHeight);
            result.historyMissCenter = readRed(bytes.data(), kWidth / 2u, kHeight / 2u);
            result.resolvedCenter = readRed(bytes.data() + kImageBytes, kWidth / 2u, kHeight / 2u);
            result.historyMissHash = firstStats.sampleHash;
            result.resolvedHash = secondStats.sampleHash;

            const float expectedCurrent =
                static_cast<float>(kWidth / 2u) / static_cast<float>(kWidth - 1u);
            assert(std::abs(result.historyMissCenter - expectedCurrent) < 0.002f);
            std::printf("TAA Metal %s historyMiss=%g resolved=%g hash=%016llx\n",
                label,
                result.historyMissCenter,
                result.resolvedCenter,
                static_cast<unsigned long long>(result.resolvedHash));
        }
        device_->waitIdle();
        assert(device_->memoryStats().allocatedBytes == baselineBytes_);
        return result;
    }

private:
    core::UniquePtr<GpuTexture> makeTexture(
        const char* label, TextureFormat format, TextureUsage usage, ResourceState initialState)
    {
        return device_->createTexture(
            {label, kExtent3D, format, usage, MemoryUsage::GpuOnly, initialState, 1u});
    }

    void uploadInputs(GpuTexture& current,
        GpuTexture& velocity,
        GpuTexture& depth,
        GpuTexture& reactive,
        float velocityX,
        std::uint64_t serial)
    {
        std::array<std::uint8_t, kImageBytes> currentBytes{};
        std::array<std::uint8_t, kImageBytes> velocityBytes{};
        std::array<std::uint8_t, kImageBytes> depthBytes{};
        std::array<std::uint8_t, kImageBytes> reactiveBytes{};
        for (std::uint32_t y = 0u; y < kHeight; ++y) {
            for (std::uint32_t x = 0u; x < kWidth; ++x) {
                const float ramp = static_cast<float>(x) / static_cast<float>(kWidth - 1u);
                const std::array<std::uint16_t, 4> color{core::floatToHalf(ramp),
                    core::floatToHalf(ramp),
                    core::floatToHalf(ramp),
                    core::floatToHalf(1.0f)};
                const std::array<std::uint16_t, 2> motion{
                    core::floatToHalf(velocityX), core::floatToHalf(0.0f)};
                const float deviceDepth = 0.5f;
                std::memcpy(currentBytes.data() + y * kPitch + x * sizeof(color),
                    color.data(),
                    sizeof(color));
                std::memcpy(velocityBytes.data() + y * kPitch + x * sizeof(motion),
                    motion.data(),
                    sizeof(motion));
                std::memcpy(depthBytes.data() + y * kPitch + x * sizeof(deviceDepth),
                    &deviceDepth,
                    sizeof(deviceDepth));
            }
        }

        auto encoder = device_->createCommandEncoder({"TaaMetalUpload"});
        assert(encoder);
        check(encoder->transition(
            current, kColorRange, ResourceState::Undefined, ResourceState::CopyDst));
        check(encoder->transition(
            velocity, kColorRange, ResourceState::Undefined, ResourceState::CopyDst));
        check(encoder->transition(
            depth, kDepthRange, ResourceState::Undefined, ResourceState::CopyDst));
        check(encoder->transition(
            reactive, kColorRange, ResourceState::Undefined, ResourceState::CopyDst));
        auto transfer = encoder->beginTransferPass({"TaaMetalUpload"});
        assert(transfer);
        upload(*transfer,
            current,
            TextureFormat::Rgba16Float,
            kColorRange,
            currentBytes.data(),
            currentBytes.size(),
            serial);
        upload(*transfer,
            velocity,
            TextureFormat::Rg16Float,
            kColorRange,
            velocityBytes.data(),
            velocityBytes.size(),
            serial);
        upload(*transfer,
            depth,
            TextureFormat::D32Float,
            kDepthRange,
            depthBytes.data(),
            depthBytes.size(),
            serial);
        upload(*transfer,
            reactive,
            TextureFormat::R8Unorm,
            kColorRange,
            reactiveBytes.data(),
            reactiveBytes.size(),
            serial);
        check(transfer->end());
        check(encoder->transition(
            current, kColorRange, ResourceState::CopyDst, ResourceState::ShaderRead));
        check(encoder->transition(
            velocity, kColorRange, ResourceState::CopyDst, ResourceState::ShaderRead));
        check(encoder->transition(
            depth, kDepthRange, ResourceState::CopyDst, ResourceState::ShaderRead));
        check(encoder->transition(
            reactive, kColorRange, ResourceState::CopyDst, ResourceState::ShaderRead));
        submitAndWait(std::move(encoder));
    }

    static void upload(TransferPassEncoder& transfer,
        GpuTexture& destination,
        TextureFormat format,
        TextureSubresourceRange range,
        const void* bytes,
        std::size_t size,
        std::uint64_t serial)
    {
        UploadTextureDesc desc;
        desc.label = destination.desc().label;
        desc.destinationTexture = destination.id();
        desc.extent = kExtent3D;
        desc.format = format;
        desc.bytesPerRow = kPitch;
        desc.rowsPerImage = kHeight;
        desc.subresources = range;
        desc.pixelData = bytes;
        desc.pixelDataSize = static_cast<std::uint32_t>(size);
        desc.backendOwnedStaging = true;
        desc.frameSerial = serial;
        desc.resourceGeneration = 1u;
        check(transfer.uploadTexture(desc));
    }

    void copyFirstFrameToHistory(renderer::TaaRenderer& taa,
        GpuTexture& color,
        GpuTexture& moments,
        GpuTexture& depth,
        GpuBuffer& readback,
        std::uint64_t serial)
    {
        auto historyColor = taa.historyWriteColorRef();
        auto historyDepth = taa.historyWriteDepthRef();
        auto historyMoments = taa.historyWriteMomentsRef();
        assert(historyColor && historyDepth && historyMoments);

        auto encoder = device_->createCommandEncoder({"TaaMetalHistoryCopy"});
        assert(encoder);
        check(encoder->transition(
            color, kColorRange, ResourceState::ColorAttachment, ResourceState::CopySrc));
        check(encoder->transition(
            moments, kColorRange, ResourceState::ColorAttachment, ResourceState::CopySrc));
        check(encoder->transition(
            depth, kDepthRange, ResourceState::ShaderRead, ResourceState::CopySrc));
        check(encoder->transition(
            *historyColor, kColorRange, ResourceState::ShaderRead, ResourceState::CopyDst));
        check(encoder->transition(
            *historyMoments, kColorRange, ResourceState::ShaderRead, ResourceState::CopyDst));
        check(encoder->transition(
            *historyDepth, kDepthRange, ResourceState::ShaderRead, ResourceState::CopyDst));
        auto transfer = encoder->beginTransferPass({"TaaMetalHistoryCopy"});
        assert(transfer);
        check(transfer->copyTextureToTexture(
            {color.id(), historyColor->id(), kColorRange, kColorRange, kExtent3D}));
        check(transfer->copyTextureToTexture(
            {moments.id(), historyMoments->id(), kColorRange, kColorRange, kExtent3D}));
        check(transfer->copyTextureToTexture(
            {depth.id(), historyDepth->id(), kDepthRange, kDepthRange, kExtent3D}));
        check(transfer->copyTextureToBuffer(
            {color.id(), readback.id(), kColorRange, 0u, kPitch, kHeight, kExtent3D}));
        check(transfer->end());
        check(encoder->transition(
            *historyColor, kColorRange, ResourceState::CopyDst, ResourceState::ShaderRead));
        check(encoder->transition(
            *historyMoments, kColorRange, ResourceState::CopyDst, ResourceState::ShaderRead));
        check(encoder->transition(
            *historyDepth, kDepthRange, ResourceState::CopyDst, ResourceState::ShaderRead));
        check(encoder->transition(
            depth, kDepthRange, ResourceState::CopySrc, ResourceState::ShaderRead));
        auto commands = encoder->finish();
        assert(commands);
        const auto submitted = device_->queue().submit(*commands);
        check(submitted);
        taa.advanceHistory();
        device_->waitIdle();
        (void)serial;
    }

    void copyToReadback(GpuTexture& texture, GpuBuffer& readback, std::uint64_t offset)
    {
        auto encoder = device_->createCommandEncoder({"TaaMetalReadback"});
        assert(encoder);
        check(encoder->transition(
            texture, kColorRange, ResourceState::ColorAttachment, ResourceState::CopySrc));
        auto transfer = encoder->beginTransferPass({"TaaMetalReadback"});
        assert(transfer);
        check(transfer->copyTextureToBuffer(
            {texture.id(), readback.id(), kColorRange, offset, kPitch, kHeight, kExtent3D}));
        check(transfer->end());
        submitAndWait(std::move(encoder));
    }

    void submitAndWait(core::UniquePtr<CommandEncoder> encoder)
    {
        auto commands = encoder->finish();
        assert(commands);
        check(device_->queue().submit(*commands));
        device_->waitIdle();
    }

    static float readRed(const std::uint8_t* bytes, std::uint32_t x, std::uint32_t y)
    {
        std::uint16_t bits = 0u;
        std::memcpy(&bits, bytes + y * kPitch + x * 8u, sizeof(bits));
        const float value = core::halfToFloat(bits);
        assert(std::isfinite(value));
        return value;
    }

    core::UniquePtr<GpuDevice> device_;
    std::uint64_t baselineBytes_ = 0u;
};

} // namespace

int main()
{
    TaaMetalResolveFixture fixture;
    const auto positive = fixture.run(1.0f / static_cast<float>(kWidth), 10u, "positive");
    const auto zero = fixture.run(0.0f, 20u, "zero");
    const auto negative = fixture.run(-1.0f / static_cast<float>(kWidth), 30u, "negative");

    assert(positive.historyMissHash == zero.historyMissHash);
    assert(zero.historyMissHash == negative.historyMissHash);
    assert(positive.resolvedHash != zero.resolvedHash);
    assert(zero.resolvedHash != negative.resolvedHash);
    assert(positive.resolvedHash != negative.resolvedHash);
    assert(positive.resolvedCenter < zero.resolvedCenter);
    assert(zero.resolvedCenter < negative.resolvedCenter);
    std::puts("TAA native Metal correct/zero/reversed velocity, history copy and retirement: PASS");
    return 0;
}
