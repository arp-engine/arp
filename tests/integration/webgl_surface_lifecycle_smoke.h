#pragma once

#include "rhi/backends/webgl/webgl_device.h"
#include "rhi/backends/webgl/webgl_pipeline_internal.h"

#include <GLES2/gl2.h>
#include <emscripten.h>

namespace arp::rhi::tests {
EM_JS(void, lifecyclePageAvailability, (int available), {
    // clang-format off
    window.dispatchEvent(new Event(available ? 'pageshow' : 'pagehide'));
    // clang-format on
});

EM_JS(void, lifecycleDocumentVisibility, (int hidden), {
    // clang-format off
    if (hidden) {
        Module.arpOriginalHiddenDescriptor = Object.getOwnPropertyDescriptor(document, 'hidden');
        Object.defineProperty(document, 'hidden', {value:true,configurable:true});
    } else {
        const original = Module.arpOriginalHiddenDescriptor;
        if (original) Object.defineProperty(document, 'hidden', original);
        else delete document.hidden;
        Module.arpOriginalHiddenDescriptor = null;
    }
    document.dispatchEvent(new Event('visibilitychange'));
    // clang-format on
});

EM_JS(int, lifecycleAuthoredExtent, (int width, int height), {
    // clang-format off
    const canvas = GL.currentContext.GLctx.canvas;
    return canvas.width === width && canvas.height === height ? 1 : 0;
    // clang-format on
});

class WebGlSurfaceLifecycleSmoke final {
public:
    const char* diagnostic = "";
    int frames = 0;
    bool run(webgl::WebGlDevice&, const std::shared_ptr<RenderPipeline>&, bool, double (*)());

private:
    static semantics::WebGlPreparedCommandsRequest reservation()
    {
        semantics::WebGlPreparedCommandsRequest request;
        request.contractVersion = 1;
        request.prototypeOptIn = true;
        request.maxDraws = 1;
        request.maxUniformBytes = 160;
        request.maxAnalysisIndices = 3;
        request.issue = {1, true, 3, 8, 12, 256 * 1024, 16};
        return request;
    }

    bool check(bool result, const char* id)
    {
        if (!result)
            diagnostic = id ? id : "SURFACE-FIXTURE-MISSING-DIAGNOSTIC";
        return result;
    }

    bool clear(webgl::WebGlDevice&,
        Swapchain&,
        const std::shared_ptr<RenderPipeline>&,
        Extent2D,
        double (*)());
};

#define SURFACE_REQUIRE(value, id)                                                                 \
    do {                                                                                           \
        if (!check(bool(value), id))                                                               \
            return false;                                                                          \
    } while (false)

inline bool WebGlSurfaceLifecycleSmoke::clear(webgl::WebGlDevice& device,
    Swapchain& canvas,
    const std::shared_ptr<RenderPipeline>& pipeline,
    Extent2D extent,
    double (*nextFrame)())
{
    const auto frame = canvas.acquireNextFrame();
    auto image = canvas.acquiredImage(frame);
    auto target = device.canvasFramebuffer(canvas, frame);
    SURFACE_REQUIRE(frame.success && image.success && target, "SURFACE-CLEAR-ORIGINAL-LEASE");
    const auto request = reservation();
    auto quota = core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
    RenderPassDesc pass;
    pass.colorAttachments.push_back({{},
        image.ref.image,
        TextureFormat::Rgba8Unorm,
        LoadOp::Load,
        StoreOp::Store,
        false,
        {},
        {0, 0, 1, 1}});
    SURFACE_REQUIRE(!device.prepareCommand(pipeline, target, request, quota), device.diagnostic());
    auto encoder = device.createCommandEncoder({});
    SURFACE_REQUIRE(encoder && !encoder->beginRenderPass(pass), "SURFACE-RESUME-LOAD-REJECTED");
    encoder.reset();
    SURFACE_REQUIRE(!quota->liveBytes(), "SURFACE-REJECTED-RECORD-QUOTA-RETIRED");
    pass.colorAttachments[0].loadOp = LoadOp::Clear;
    SURFACE_REQUIRE(!device.prepareCommand(pipeline, target, request, quota), device.diagnostic());
    encoder = device.createCommandEncoder({});
    auto render = encoder ? encoder->beginRenderPass(pass) : core::UniquePtr<RenderPassEncoder>{};
    SURFACE_REQUIRE(render && render->end().success, "SURFACE-FIRST-FRAME-CLEAR-RECORD");
    auto commands = encoder->finish();
    SURFACE_REQUIRE(
        commands && device.queue().submit(*commands).success, "SURFACE-FIRST-FRAME-CLEAR-ISSUE");
    std::array<std::uint8_t, 64 * 64 * 4> pixels{};
    glReadPixels(0, 0, extent.width, extent.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    SURFACE_REQUIRE(glGetError() == GL_NO_ERROR, "SURFACE-COLD-ORACLE-READBACK");
    for (std::size_t i = 0; i < extent.width * extent.height * 4u; ++i)
        SURFACE_REQUIRE(pixels[i] == (i % 4 >= 2 ? 255 : 0), "SURFACE-CLEAR-ALL-PIXELS");
    SURFACE_REQUIRE(canvas.present(frame).success, "SURFACE-CLEAR-BROWSER-HANDOFF");
    (void)nextFrame();
    ++frames;
    commands.reset();
    render.reset();
    encoder.reset();
    target.reset();
    SURFACE_REQUIRE(!quota->liveBytes() && !device.recycleCanvas(canvas, frame, image.texture)
            && !image.texture,
        "SURFACE-CLEAR-EXACT-RETURN-AND-QUOTA-RETIRED");
    return true;
}

inline bool WebGlSurfaceLifecycleSmoke::run(webgl::WebGlDevice& device,
    const std::shared_ptr<RenderPipeline>& pipeline,
    bool exhaust,
    double (*nextFrame)())
{
    SURFACE_REQUIRE(
        !device.enableSurfaceLifecycle({1, true, exhaust ? 10u : 64u}), device.diagnostic());
    auto definition = RenderPipelineStateCopy::copy(
        dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get())->impl->definition);
    definition.shaderPackage = nullptr;
    definition.depthFormat = definition.depthStencil.format = TextureFormat::Unknown;
    auto clearPipeline = core::shareUnique(device.createRenderPipeline(definition));
    SURFACE_REQUIRE(clearPipeline, device.diagnostic());
    SwapchainDesc desc;
    desc.surfaceName = "#canvas";
    desc.extent = {64, 64};
    desc.format = TextureFormat::Rgba8Unorm;
    desc.imageCount = 0;
    auto canvas = device.createSwapchain(desc);
    SURFACE_REQUIRE(canvas, device.diagnostic());
    const auto oldFrame = canvas->acquireNextFrame();
    auto oldImage = canvas->acquiredImage(oldFrame);
    auto target = device.canvasFramebuffer(*canvas, oldFrame);
    SURFACE_REQUIRE(
        oldFrame.success && oldImage.success && target && !canvas->resize({48, 32}).success,
        "SURFACE-LEASED-RESIZE-REJECTED");
    const auto request = reservation();
    auto quota = core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
    SURFACE_REQUIRE(
        !device.prepareCommand(clearPipeline, target, request, quota), device.diagnostic());
    auto encoder = device.createCommandEncoder({});
    RenderPassDesc pass;
    pass.colorAttachments.push_back({{},
        oldImage.ref.image,
        TextureFormat::Rgba8Unorm,
        LoadOp::Clear,
        StoreOp::Store,
        false,
        {},
        {0, 0, 1, 1}});
    auto render = encoder ? encoder->beginRenderPass(pass) : core::UniquePtr<RenderPassEncoder>{};
    SURFACE_REQUIRE(render && render->end().success, "SURFACE-PREPAUSE-RECORD");
    auto commands = encoder->finish();
    SURFACE_REQUIRE(commands, "SURFACE-PREPAUSE-FINISH");
    const auto submissions = device.queue().submitCount();
    SURFACE_REQUIRE(canvas->setSurfaceAvailable(false).success
            && canvas->setSurfaceAvailable(false).success && !canvas->acquireNextFrame().success
            && !device.createSwapchain(desc) && !canvas->isFrameAcquired(oldFrame)
            && !device.queue().submit(*commands).hasAcceptedWork()
            && device.queue().submitCount() == submissions,
        "SURFACE-CALLER-PAUSE-IMMEDIATE-AND-IDEMPOTENT");
    SURFACE_REQUIRE(canvas->setSurfaceAvailable(true).success && !canvas->isFrameAcquired(oldFrame)
            && !device.createSwapchain(desc) && !device.queue().submit(*commands).success,
        "SURFACE-RESUME-OLD-OWNERS-STAY-STALE");
    commands.reset();
    render.reset();
    encoder.reset();
    SURFACE_REQUIRE(!quota->liveBytes(), "SURFACE-CANCELED-COMMAND-QUOTA-RETURNED");
    target.reset();
    oldImage.texture.reset();
    canvas.reset();
    canvas = device.createSwapchain(desc);
    SURFACE_REQUIRE(canvas, device.diagnostic());
    lifecyclePageAvailability(0);
    SURFACE_REQUIRE(!canvas->acquireNextFrame().success && !device.createSwapchain(desc),
        "SURFACE-PAGE-HIDE-CLOSES-EXECUTION");
    lifecyclePageAvailability(1);
    SURFACE_REQUIRE(
        !canvas->acquireNextFrame().success, "SURFACE-PAGE-SHOW-DOES-NOT-REVIVE-OLD-EPOCH");
    canvas.reset();
    canvas = device.createSwapchain(desc);
    SURFACE_REQUIRE(canvas, device.diagnostic());
    lifecycleDocumentVisibility(1);
    SURFACE_REQUIRE(!canvas->acquireNextFrame().success && !device.createSwapchain(desc),
        "SURFACE-HIDDEN-DOCUMENT-CLOSES-EXECUTION");
    lifecycleDocumentVisibility(0);
    SURFACE_REQUIRE(
        !canvas->acquireNextFrame().success && device.queue().submitCount() == submissions,
        "SURFACE-VISIBLE-DOES-NOT-REVIVE-OR-ISSUE-EMPTY-WORK");
    canvas.reset();
    canvas = device.createSwapchain(desc);
    SURFACE_REQUIRE(
        canvas && !canvas->resize({UINT32_MAX, 32}).success && lifecycleAuthoredExtent(64, 64),
        "SURFACE-INVALID-EXTENT-HAS-NO-NATIVE-MUTATION");
    SURFACE_REQUIRE(canvas->resize({0, 0}).success && lifecycleAuthoredExtent(0, 0)
            && !canvas->acquireNextFrame().success && !device.createSwapchain(desc),
        "SURFACE-ZERO-IS-NOT-FAKE-ONE-PIXEL");
    SURFACE_REQUIRE(canvas->resize({48, 32}).success && lifecycleAuthoredExtent(48, 32),
        "SURFACE-EXPLICIT-NONZERO-COLD-RESIZE");
    canvas.reset();
    desc.extent = {48, 32};
    canvas = device.createSwapchain(desc);
    SURFACE_REQUIRE(canvas && !device.enableCanvasReuse(*canvas, {1, true, 2})
            && clear(device, *canvas, clearPipeline, desc.extent, nextFrame),
        diagnostic);
    SURFACE_REQUIRE(canvas->resize({64, 64}).success && !canvas->acquireNextFrame().success,
        "SURFACE-RESIZED-OLD-TEXTURE-IS-NOT-RELABELED");
    canvas.reset();
    desc.extent = {64, 64};
    canvas = device.createSwapchain(desc);
    SURFACE_REQUIRE(canvas && !device.enableCanvasReuse(*canvas, {1, true, 2})
            && clear(device, *canvas, clearPipeline, desc.extent, nextFrame),
        diagnostic);
    SURFACE_REQUIRE(canvas->resize({64, 64}).success, "SURFACE-EXACT-MUTATION-BUDGET");
    if (exhaust) {
        const auto failed = canvas->setSurfaceAvailable(false);
        SURFACE_REQUIRE(!failed.success
                && failed.diagnostics == "RHI-WEBGL1-SURFACE-MUTATION-BUDGET-EXHAUSTED"
                && !canvas->setSurfaceAvailable(true).success && !device.createSwapchain(desc),
            "SURFACE-EXHAUSTION-IS-STICKY");
    }
    canvas.reset();
    return true;
}

#undef SURFACE_REQUIRE
} // namespace arp::rhi::tests
