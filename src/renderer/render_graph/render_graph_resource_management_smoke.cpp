#include "renderer/render_graph/render_graph_resource_management_smoke.h"

#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"

#include <algorithm>
#include <exception>
#include <memory>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace arp::render_graph {

namespace {

rhi::TextureDesc transientTextureDesc(
    const char* label, const rhi::Extent3D& extent, rhi::TextureFormat format)
{
    return {
        label,
        extent,
        format,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::Transient,
        rhi::ResourceState::Undefined,
    };
}

rhi::BufferDesc transientBufferDesc(std::uint64_t sizeBytes)
{
    return {
        "ResourceManagementSmokeBuffer",
        sizeBytes,
        rhi::ResourceUsage::StorageBuffer,
        rhi::MemoryUsage::Transient,
        rhi::ResourceState::Undefined,
    };
}

} // namespace

RenderGraphResourceManagementSmokeRunner::RenderGraphResourceManagementSmokeRunner(
    rhi::GpuDevice& device, rhi::Swapchain* swapchain)
    : device_(device), swapchain_(swapchain)
{}

RenderGraphResourceManagementSmokeReport RenderGraphResourceManagementSmokeRunner::run(
    const RenderGraphResourceManagementSmokeConfig& config)
{
    RenderGraphResourceManagementSmokeReport report;
    report.backend = device_.backend();
    report.transientAliasingAdvertised = device_.capabilities().features.transientAliasing;
    if (config.logicalFrameSlots == 0 || config.extent.width == 0 || config.extent.height == 0
        || config.extent.depth == 0 || config.format == rhi::TextureFormat::Unknown
        || config.browserCompletionTimeoutMilliseconds == 0) {
        report.diagnostics = "RG-SMOKE-CONFIG-INVALID";
        return report;
    }

    report.requestedFrameCount = config.logicalFrameSlots + 2;
    auto pool = arp::core::makeShared<FrameGraphResourcePool>(device_, config.logicalFrameSlots);
    auto ownedSwapchain = swapchain_ == nullptr ? device_.createSwapchain({
                                                      "RenderGraphResourceManagementSmoke",
                                                      {config.extent.width, config.extent.height},
                                                      config.format,
                                                      std::max(2u, config.logicalFrameSlots),
                                                  })
                                                : nullptr;
    auto* swapchain = swapchain_ != nullptr ? swapchain_ : ownedSwapchain.get();
    if (swapchain == nullptr) {
        report.diagnostics = "RG-SMOKE-SWAPCHAIN-CREATE-FAILED";
        return report;
    }

    bool externalFinalStateCorrect = true;
    auto persistentBuffer =
        core::shareUnique(device_.createBuffer({"ResourceManagementPersistentBuffer",
            64,
            rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::IndirectBuffer,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined}));
    if (!persistentBuffer) {
        report.diagnostics = "RG-SMOKE-PERSISTENT-BUFFER-CREATE-FAILED";
        return report;
    }
    rhi::AcquiredFrame outstandingFrame;
    bool frameOutstanding = false;
    try {
        for (std::uint32_t frame = 0; frame < report.requestedFrameCount; ++frame) {
            outstandingFrame = swapchain->acquireNextFrame();
            if (!outstandingFrame.success) {
                throw std::runtime_error(
                    arp::core::toStdString(std::pmr::string("RG-SMOKE-SWAPCHAIN-ACQUIRE-FAILED: ")
                        + outstandingFrame.diagnostics));
            }
            frameOutstanding = true;
            auto acquiredImage = swapchain->acquiredImage(outstandingFrame);
            if (!acquiredImage.success || !acquiredImage.texture) {
                throw std::runtime_error(
                    arp::core::toStdString(std::pmr::string("RG-SMOKE-SWAPCHAIN-IMAGE-FAILED: ")
                        + acquiredImage.diagnostics));
            }

            FrameGraph graph(device_, pool);
            const auto persistent = graph.importBufferRef(persistentBuffer);
            const auto backbufferHandle =
                graph.importSwapchainTexture(std::move(acquiredImage.texture), acquiredImage.ref);

            auto frameExtent = config.extent;
            const bool budgetTrimFrame = frame + 1 == report.requestedFrameCount;
            if (budgetTrimFrame) {
                frameExtent.width = 1;
                frameExtent.height = 1;
            }
            const auto a = graph.createTransientTexture(
                transientTextureDesc("ResourceManagementSmokeA", frameExtent, config.format));
            const auto b = graph.createTransientTexture(
                transientTextureDesc("ResourceManagementSmokeB", frameExtent, config.format));
            const auto c = graph.createTransientTexture(
                transientTextureDesc("ResourceManagementSmokeC", frameExtent, config.format));
            const auto transientBuffer =
                graph.createTransientBuffer(transientBufferDesc(budgetTrimFrame ? 64 : 4096));

            graph.addPass("ResourceManagementProduceA")
                .writeAttachment(a,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store,
                    {},
                    {0.125f, 0.25f, 0.5f, 1.0f})
                .writeAttachment(c,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store,
                    {},
                    {0.5f, 0.25f, 0.125f, 1.0f})
                .write(transientBuffer, rhi::ResourceState::ShaderWrite)
                .access(persistent,
                    {rhi::ResourceState::ShaderWrite,
                        rhi::PipelineStage::FragmentShader,
                        rhi::ResourceAccess::ShaderWrite},
                    0,
                    64)
                .executeRenderPass([](RenderPassContext&) {});
            graph.addPass("ResourceManagementPreserveC")
                .writeAttachment(
                    c, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Load, rhi::StoreOp::Store)
                .executeRenderPass([](RenderPassContext&) {});
            graph.addPass("ResourceManagementConsumeA")
                .access(a,
                    {rhi::ResourceState::ShaderRead,
                        rhi::PipelineStage::VertexShader,
                        rhi::ResourceAccess::ShaderRead},
                    rhi::defaultTextureSubresourceRange(graph.textureDesc(a)))
                .access(c,
                    {rhi::ResourceState::ShaderRead,
                        rhi::PipelineStage::FragmentShader,
                        rhi::ResourceAccess::ShaderRead},
                    rhi::defaultTextureSubresourceRange(graph.textureDesc(c)))
                .execute([](RenderContext&) {});
            graph.addPass("ResourceManagementProduceB")
                .writeAttachment(b,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store,
                    {},
                    {0.25f, 0.5f, 0.125f, 1.0f})
                .executeRenderPass([](RenderPassContext&) {});
            graph.addPass("ResourceManagementComposite")
                .read(b, rhi::ResourceState::ShaderRead)
                .read(c, rhi::ResourceState::ShaderRead)
                .read(transientBuffer, rhi::ResourceState::ShaderRead)
                .access(persistent,
                    {rhi::ResourceState::ShaderRead,
                        rhi::PipelineStage::DrawIndirect,
                        rhi::ResourceAccess::IndirectCommandRead},
                    0,
                    16)
                .writeAttachment(backbufferHandle,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store,
                    {},
                    {0.125f, 0.5f, 0.75f, 1.0f})
                .executeRenderPass([](RenderPassContext&) {});

            const auto compiled = graph.compile();
            const auto frameHandoffs = compiled.aliasHandoffs().size();
            if (report.transientAliasingAdvertised && frameHandoffs != 1) {
                report.diagnostics = "RG-SMOKE-ALIAS-HANDOFF-COUNT-MISMATCH";
                return report;
            }
            if (!report.transientAliasingAdvertised && frameHandoffs != 0) {
                ++report.fallbackCount;
                report.diagnostics = "RG-SMOKE-ALIAS-CAPABILITY-MISMATCH";
                return report;
            }
            report.aliasHandoffCount += frameHandoffs;

            graph.execute();
            ++report.completedFrameCount;
            if (graph.texture(backbufferHandle).state() != rhi::ResourceState::Present) {
                externalFinalStateCorrect = false;
                throw std::runtime_error("RG-SMOKE-EXTERNAL-FINAL-STATE-MISMATCH");
            }
            const auto presented = swapchain->present(outstandingFrame);
            frameOutstanding = false;
            if (!presented.success) {
                throw std::runtime_error(
                    arp::core::toStdString(std::pmr::string("RG-SMOKE-SWAPCHAIN-PRESENT-FAILED: ")
                        + presented.diagnostics));
            }
            graph.reset();
#if defined(__EMSCRIPTEN__)
            // Browser WebGPU publishes queue completion through callbacks.
            // Poll within the smoke's explicit timeout so completed resources
            // leave the pending pool before the next frame attempts reuse.
            const auto completionDeadline =
                emscripten_get_now() + config.browserCompletionTimeoutMilliseconds;
            while (pool->report().pendingResourceCount != 0
                && emscripten_get_now() < completionDeadline) {
                emscripten_sleep(1);
                pool->recycleCompleted();
            }
            if (pool->report().pendingResourceCount != 0) {
                throw std::runtime_error("RG-SMOKE-COMPLETION-POLL-TIMEOUT");
            }
#endif
        }
    } catch (const std::exception& error) {
        if (frameOutstanding) {
            const auto discarded = swapchain->discardFrame(outstandingFrame);
            if (!discarded.success) {
                report.diagnostics =
                    std::pmr::string("RG-SMOKE-SWAPCHAIN-DISCARD-FAILED: ") + discarded.diagnostics;
                return report;
            }
            frameOutstanding = false;
        }
        const auto message = std::pmr::string(error.what());
        if (message.find("RG-BARRIER-TRANSITION-FAILED") != std::pmr::string::npos) {
            ++report.barrierFailureCount;
        }
        report.diagnostics = std::pmr::string("RG-SMOKE-EXECUTION-FAILED: ") + message;
        return report;
    }

    pool->recycleCompleted();
    report.pool = pool->report();
    report.prematureReuseCount = report.pool.reuseFailureCount;
    report.externalFinalStateCorrect = externalFinalStateCorrect;
    report.budgetWithinBounds =
        report.pool.cachedBytes + report.pool.pendingBytes <= report.pool.byteBudget
        && report.pool.cachedResourceCount + report.pool.pendingResourceCount
            <= report.pool.resourceBudget;
    report.graphRebuildExercised = report.completedFrameCount > 1;
    const auto expectedHandoffs =
        report.transientAliasingAdvertised ? report.requestedFrameCount : 0u;
    report.success = report.completedFrameCount == report.requestedFrameCount
        && report.aliasHandoffCount == expectedHandoffs && report.prematureReuseCount == 0
        && report.barrierFailureCount == 0 && report.fallbackCount == 0
        && report.externalFinalStateCorrect && report.budgetWithinBounds
        && report.graphRebuildExercised && report.pool.hitCount > 0
        && report.pool.evictionCount > 0;
    if (!report.success && report.diagnostics.empty()) {
        report.diagnostics = "RG-SMOKE-REPORT-INVARIANT-FAILED";
    }
    return report;
}

} // namespace arp::render_graph
