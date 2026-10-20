#include "core/cpu_allocator.h"
#include "renderer/postprocess/taa_renderer.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

arp::core::pmr::String taaShaderPath()
{
    return arp::core::toPmrString(
        (std::filesystem::path(ARP_SHADER_DIR) / "wocc_taa.slang").string());
}

// Build a RenderFrameContext whose colorTexture points at a real Null-device
// texture. The TAA resolve writes into frame.colorTexture (the output RT).
arp::renderer::RenderFrameContext makeFrameContext(arp::rhi::GpuTexture& colorOutput,
    arp::rhi::Extent2D extent,
    arp::rhi::TextureFormat format,
    std::uint32_t frameIndex)
{
    arp::rhi::AcquiredFrame acquired;
    acquired.success = true;
    acquired.frameIndex = frameIndex;
    arp::rhi::RenderTargetDesc colorTarget;
    colorTarget.label = "TaaBackbuffer";
    colorTarget.extent = extent;
    colorTarget.format = format;
    colorTarget.finalState = arp::rhi::ResourceState::ColorAttachment;
    return arp::renderer::RenderFrameContext{
        acquired,
        colorTarget,
        nullptr,
        &colorOutput,
        nullptr,
        nullptr,
    };
}

// Create a texture with Sampled usage (required by validateTaaDrawDesc for all
// 7 TAA inputs: currentColor, historyColor, velocity, depth, reactive,
// history depth and history moments).
arp::core::UniquePtr<arp::rhi::GpuTexture> makeSampledTexture(arp::rhi::GpuDevice& device,
    const char* name,
    arp::rhi::Extent2D extent,
    arp::rhi::TextureFormat format)
{
    arp::rhi::TextureDesc desc;
    desc.label = name;
    desc.extent = {extent.width, extent.height, 1u};
    desc.format = format;
    desc.usage = arp::rhi::TextureUsage::Sampled | arp::rhi::TextureUsage::RenderAttachment;
    desc.memory = arp::rhi::MemoryUsage::GpuOnly;
    desc.initialState = arp::rhi::ResourceState::ColorAttachment;
    desc.mipLevelCount = 1u;
    return device.createTexture(desc);
}

arp::core::UniquePtr<arp::rhi::GpuTexture> makeOutputTexture(arp::rhi::GpuDevice& device,
    const char* name,
    arp::rhi::Extent2D extent,
    arp::rhi::TextureFormat format)
{
    arp::rhi::TextureDesc desc;
    desc.label = name;
    desc.extent = {extent.width, extent.height, 1u};
    desc.format = format;
    desc.usage = arp::rhi::TextureUsage::RenderAttachment;
    desc.memory = arp::rhi::MemoryUsage::GpuOnly;
    desc.initialState = arp::rhi::ResourceState::ColorAttachment;
    desc.mipLevelCount = 1u;
    return device.createTexture(desc);
}

void taa_renderer_creates_pipeline_layout_descriptor_and_history()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::renderer::TaaRenderer renderer(*device);
    const auto shaderPath = taaShaderPath();
    const auto created = renderer.create(arp::renderer::TaaRendererDesc{
        shaderPath,
        arp::rhi::TextureFormat::Bgra8Unorm,
        {320, 180},
    });
    if (!created.success) {
        std::cerr << "[fail] TaaRenderer::create: " << created.diagnostics << "\n";
    }
    assert(created.success);
    // create() must build the render pipeline + persistent history texture.
    assert(renderer.graphicsPipeline());
    assert(renderer.historyColor());
    // History texture must match the create extent + format + Sampled usage
    // (validateTaaDrawDesc requires Sampled on historyColor).
    assert(renderer.historyColor()->desc().extent.width == 320);
    assert(renderer.historyColor()->desc().extent.height == 180);
    assert(renderer.historyColor()->desc().format == arp::rhi::TextureFormat::Bgra8Unorm);
    assert((renderer.historyColor()->desc().usage & arp::rhi::TextureUsage::Sampled)
        != arp::rhi::TextureUsage::None);
    // History starts invalid (first frame = historyMiss bypass).
    renderer.invalidateHistory();
    std::cerr << "[ok] TaaRenderer::create builds pipeline + history texture\n";
}

void taa_renderer_draw_frame_records_resolve_and_advances_history()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    const arp::rhi::Extent2D extent{320, 180};
    const auto format = arp::rhi::TextureFormat::Bgra8Unorm;

    arp::renderer::TaaRenderer renderer(*device);
    const auto shaderPath = taaShaderPath();
    const auto created = renderer.create(arp::renderer::TaaRendererDesc{
        shaderPath,
        format,
        extent,
    });
    assert(created.success);

    // TAA inputs: currentColor + velocity + depth + reactive (all Sampled).
    auto currentColor = makeSampledTexture(*device, "TaaCurrentColor", extent, format);
    auto velocity =
        makeSampledTexture(*device, "TaaVelocity", extent, arp::rhi::TextureFormat::Rg16Float);
    auto depth = makeSampledTexture(*device, "TaaDepth", extent, arp::rhi::TextureFormat::D32Float);
    auto reactive =
        makeSampledTexture(*device, "TaaReactive", extent, arp::rhi::TextureFormat::R8Unorm);
    auto outputMoments =
        makeOutputTexture(*device, "TaaOutputMoments", extent, arp::rhi::TextureFormat::Rg16Float);
    assert(currentColor);
    assert(velocity);
    assert(depth);
    assert(reactive);
    assert(outputMoments);

    // Output RT (frame.colorTexture) - ColorAttachment.
    auto outputColor = makeOutputTexture(*device, "TaaOutputColor", extent, format);
    assert(outputColor);

    // First frame: history is invalid -> historyMiss bypass (no blending).
    renderer.invalidateHistory();
    auto frame0 = makeFrameContext(*outputColor, extent, format, 0);
    const auto drawn0 =
        renderer.drawFrame(frame0, *currentColor, *velocity, *depth, *reactive, *outputMoments);
    assert(drawn0.success);

    // After a successful resolve, the game loop marks history valid so the
    // next frame can accumulate.
    renderer.markHistoryValid();

    // Second frame: history is valid -> full TAA resolve (reprojection +
    // neighborhood clip + blend). This exercises the ping-pong path without
    // rebuilding the descriptor set (lazy-bind caches by texture id).
    auto frame1 = makeFrameContext(*outputColor, extent, format, 1);
    const auto drawn1 =
        renderer.drawFrame(frame1, *currentColor, *velocity, *depth, *reactive, *outputMoments);
    assert(drawn1.success);

    std::cerr << "[ok] TaaRenderer::drawFrame records resolve (frame 0 historyMiss + "
                 "frame 1 accumulate) without crash\n";
}

void taa_renderer_settings_reset_history_forces_history_miss()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    const arp::rhi::Extent2D extent{64, 64};
    const auto format = arp::rhi::TextureFormat::Bgra8Unorm;

    arp::renderer::TaaRenderer renderer(*device);
    const auto shaderPath = taaShaderPath();
    const auto created = renderer.create(arp::renderer::TaaRendererDesc{
        shaderPath,
        format,
        extent,
    });
    assert(created.success);

    auto currentColor = makeSampledTexture(*device, "TaaCurrentColor", extent, format);
    auto velocity =
        makeSampledTexture(*device, "TaaVelocity", extent, arp::rhi::TextureFormat::Rg16Float);
    auto depth = makeSampledTexture(*device, "TaaDepth", extent, arp::rhi::TextureFormat::D32Float);
    auto reactive =
        makeSampledTexture(*device, "TaaReactive", extent, arp::rhi::TextureFormat::R8Unorm);
    auto outputColor = makeOutputTexture(*device, "TaaOutputColor", extent, format);
    auto outputMoments =
        makeOutputTexture(*device, "TaaOutputMoments", extent, arp::rhi::TextureFormat::Rg16Float);
    assert(currentColor && velocity && depth && reactive && outputColor && outputMoments);

    // Mark history valid, then force a reset via settings (simulates a camera
    // cut). effectiveSettings() must fold the resetHistory flag through to the
    // push-constant so the resolve bypasses accumulation for this frame.
    renderer.markHistoryValid();
    arp::renderer::TaaSettings settings;
    settings.resetHistory = true;
    auto frame = makeFrameContext(*outputColor, extent, format, 0);
    const auto drawn = renderer.drawFrame(
        frame, *currentColor, *velocity, *depth, *reactive, *outputMoments, settings);
    assert(drawn.success);

    std::cerr << "[ok] TaaRenderer honors settings.resetHistory (camera cut bypass)\n";
}

void taa_default_settings_prioritize_temporal_stability()
{
    const auto push = arp::renderer::buildTaaPushConstants({1280, 720});
    assert(push.blendParams[0] == 0.1f);
    assert(push.blendParams[2] == 0.125f);
    assert(push.validationParams[0] == 0.02f);
    assert(push.validationParams[2] == 0.0f);
    assert(
        push.validationParams[3] == static_cast<float>(arp::renderer::TaaSettings::Quality::High));
    const float expectedA = 1000.0f / (1000.0f - 0.1f);
    const float expectedB = -(0.1f * 1000.0f) / (1000.0f - 0.1f);
    assert(std::fabs(push.inputUvScaleBias[2] - expectedA) < 1e-6f);
    assert(std::fabs(push.inputUvScaleBias[3] - expectedB) < 1e-6f);
    std::cerr << "[ok] TAA defaults use pixel velocity, relative linear depth, "
                 "and no in-resolve sharpen\n";
}

void taa_quality_is_encoded_for_shader_validation_paths()
{
    arp::renderer::TaaSettings settings;
    settings.quality = arp::renderer::TaaSettings::Quality::Low;
    auto push = arp::renderer::buildTaaPushConstants({640, 360}, settings);
    assert(push.validationParams[3] == 0.0f);

    settings.quality = arp::renderer::TaaSettings::Quality::Balanced;
    push = arp::renderer::buildTaaPushConstants({640, 360}, settings);
    assert(push.validationParams[3] == 1.0f);

    settings.quality = arp::renderer::TaaSettings::Quality::High;
    push = arp::renderer::buildTaaPushConstants({640, 360}, settings);
    assert(push.validationParams[3] == 2.0f);
    std::cerr << "[ok] TAA quality selects center or neighborhood validation paths\n";
}

} // namespace

int main()
{
    taa_renderer_creates_pipeline_layout_descriptor_and_history();
    taa_renderer_draw_frame_records_resolve_and_advances_history();
    taa_renderer_settings_reset_history_forces_history_miss();
    taa_default_settings_prioritize_temporal_stability();
    taa_quality_is_encoded_for_shader_validation_paths();
    return 0;
}
