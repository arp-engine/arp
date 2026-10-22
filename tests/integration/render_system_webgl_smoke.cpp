#include "core/diagnostics/diagnostic_error.h"
#include "core/math/quad_geometry.h"
#include "platform/render_system.h"
#include "platform/render_system_ordered_profile.h"

#include <cstring>
#include <emscripten.h>

using namespace arp;

namespace {
void check(bool condition, const char* id)
{
    if (!condition)
        throw core::DiagnosticError(id);
}

platform::RenderSystemOrderedProfile profile()
{
    constexpr std::uint64_t MiB = 1024 * 1024;
    platform::RenderSystemOrderedProfile p;
    p.contractVersion = 1;
    p.prototypeOptIn = true;
    auto& b = p.budgets;
    b.contextMetadataBytes = MiB;
    b.resourceMetadataBytes = b.uploadBytes = 4 * MiB;
    b.logicalBufferBytes = b.logicalTextureBytes = MiB;
    b.resourceObjects = 64;
    b.deviceMetadataBytes = MiB;
    b.identityFieldBytes = 4096;
    b.surfaceMutations = 32;
    b.pipelines = b.descriptors = {1, "platform-smoke", 4};
    p.execution = {1, true, 64, MiB, {1, 1, 64, 64, 1, 0, 0, 0, 0}};
    auto& f = p.frames;
    f.contractVersion = 1;
    f.prototypeOptIn = true;
    f.surface = {"#canvas", {64, 64}, rhi::TextureFormat::Rgba8Unorm, 0};
    f.commands = {4, true, 4, 256, 1, {1, true, 16, 16, 4096, MiB, 64}, 1, 1, 1, 4};
    f.maxFrameCpuMetadataBytes = MiB;
    f.frameQuota = core::makeShared<core::CpuMemoryQuota>(MiB);
    f.commandQuota = core::makeShared<core::CpuMemoryQuota>(MiB);
    return p;
}

class SkyWorkload final : public platform::OrderedGraphicsFrameWorkload {
public:
    explicit SkyWorkload(platform::RenderSystem& system) : system_(system)
    {
        platform::OrderedPipelineLoadDesc load;
        load.artifactBudget = {4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32};
        load.maxUniformLeaves = 256;
        load.maxInterfaces = 32;
        load.maxTypeNodes = 1024;
        load.maxInterfaceNameBytes = 1024;
        load.maxDiagnosticBytes = 65536;
        load.pipeline.colorFormat = rhi::TextureFormat::Rgba8Unorm;
        rhi::ColorTargetState color;
        color.format = load.pipeline.colorFormat;
        load.pipeline.colorTargets.push_back(color);
        load.pipeline.dynamicViewportScissor = true;
        rhi::VertexBufferLayoutDesc stream;
        stream.strideBytes = sizeof(core::Vec2);
        stream.attributes = {{0, 0, rhi::VertexFormat::Float32x2}};
        load.pipeline.vertexBuffers.push_back(std::move(stream));
        core::pmr::String diagnostic;
        load.artifactPath = "/artifacts/missing.webgl-program.yaml";
        check(!system_.createOrderedPipeline(load, diagnostic) && !diagnostic.empty(),
            "PLATFORM-SMOKE-MISSING-ARTIFACT");
        load.artifactPath = "/artifacts/queen_portable_sky.webgl-program.yaml";
        load.layout.pushConstantRanges.push_back({rhi::ShaderStage::Vertex, 0, 16});
        check(!system_.createOrderedPipeline(load, diagnostic)
                && diagnostic == "PLATFORM-ORDERED-PIPELINE-REFLECTED-LAYOUT",
            "PLATFORM-SMOKE-LAYOUT-GATE");
        load.layout.pushConstantRanges.clear();
        pipelines_[0] = system_.createOrderedPipeline(load, diagnostic);
        if (!pipelines_[0])
            throw core::DiagnosticError(diagnostic);
        check(!pipelines_[0]->nativeHandleExposedToUpperLayer(), "PLATFORM-SMOKE-NATIVE-HANDLE");
        rhi::BufferDesc vertex;
        vertex.sizeBytes = sizeof(core::QuadTriangleTopology::corners);
        vertex.usage = rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst;
        vertex.memory = rhi::MemoryUsage::CpuToGpu;
        vertex.initialState = rhi::ResourceState::ShaderRead;
        vertices_ = core::shareUnique(system_.gpuDevice()->createBuffer(vertex));
        check(bool(vertices_), "PLATFORM-SMOKE-VERTICES");
        core::requireSuccessfulResult(
            system_.gpuDevice()->writeBuffer(
                *vertices_, core::QuadTriangleTopology::corners.data(), vertex.sizeBytes, 0),
            "PLATFORM-SMOKE-UPLOAD");
    }

    core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines() const noexcept override
    {
        return pipelines_;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle canvas) override
    {
        const auto vertex = graph.importBufferRef(vertices_);
        graph.addPass("PlatformSkySmoke")
            .writeAttachment(canvas,
                rhi::ResourceState::ColorAttachment,
                rhi::LoadOp::Clear,
                rhi::StoreOp::Store)
            .access(vertex,
                {rhi::ResourceState::ShaderRead,
                    rhi::PipelineStage::VertexInput,
                    rhi::ResourceAccess::VertexAttributeRead},
                0,
                sizeof(core::QuadTriangleTopology::corners))
            .executeRenderPass([this](render_graph::RenderPassContext& context) {
                auto& pass = context.renderPass();
                const auto extent = system_.extent();
                const auto require = [](const auto& result) {
                    core::requireSuccessfulResult(result, "PLATFORM-SMOKE-DRAW");
                };
                require(pass.setPipeline(*pipelines_[0]));
                require(pass.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}));
                require(pass.setScissor({0, 0, extent.width, extent.height}));
                require(pass.bindVertexBuffer(0, *vertices_, 0));
                require(pass.draw(6, 1, 0, 0));
            });
    }

private:
    platform::RenderSystem& system_;
    std::array<std::shared_ptr<rhi::RenderPipeline>, 1> pipelines_;
    std::shared_ptr<rhi::GpuBuffer> vertices_;
};

void run()
{
    platform::RenderSystemDesc creation;
    auto closed = platform::createRenderSystem(creation);
    check(!closed.valid() && closed.diagnostics() == "RHI-WEBGL1-RENDERSYSTEM-UNAVAILABLE",
        "PLATFORM-SMOKE-PRODUCTION-GATE");
    auto p = profile();
    creation.orderedGraphics = &p;
    p.execution.requirements.gpuCompletion = true;
    auto rejected = platform::createRenderSystem(creation);
    check(
        !rejected.valid() && rejected.diagnostics() == "RHI-ORDERED-GRAPHICS-WORKLOAD-UNSUPPORTED",
        "PLATFORM-SMOKE-COMPLETION-GATE");
    p.execution.requirements.gpuCompletion = false;
    creation.backendPreference = platform::BackendPreference::WebGPU;
    check(!platform::createRenderSystem(creation).valid(), "PLATFORM-SMOKE-BACKEND-GATE");
    creation.backendPreference = platform::BackendPreference::WebGL;
    auto system = platform::createRenderSystem(creation);
    if (!system.valid())
        throw core::DiagnosticError(system.diagnostics());
    check(!platform::createRenderSystem(creation).valid(), "PLATFORM-SMOKE-EXCLUSIVE-SURFACE");
    const auto caps = system.orderedGraphicsCapabilities();
    check(caps.prototype && caps.orderedGraphics && caps.surfaceLifecycle && !caps.gpuCompletion,
        "PLATFORM-SMOKE-CAPABILITIES");
    check(system.backend() == rhi::Backend::WebGL
            && system.gpuDevice()->backend() == rhi::Backend::WebGL,
        "PLATFORM-SMOKE-IDENTITY");
    check(!system.acquireFrame().success && !system.prepareRendererFrame({}).success
            && !system.swapchain(),
        "PLATFORM-SMOKE-NATIVE-FRAME-GATE");
    SkyWorkload sky(system);
    const auto draw = [&] {
        const auto report = system.renderOrderedFrame(sky);
        if (!report.success)
            throw core::DiagnosticError(report.diagnostics);
        check(report.presented && report.recycled && report.workAccepted
                && !report.gpuCompletionClaimed,
            "PLATFORM-SMOKE-FRAME-RECEIPT");
    };
    for (int frame = 0; frame < 3; ++frame)
        draw();
    core::requireSuccessfulResult(system.resize({0, 0}), "PLATFORM-SMOKE-ZERO-RESIZE");
    check(!system.capabilities().canRender && !system.renderOrderedFrame(sky).success
            && system.valid(),
        "PLATFORM-SMOKE-ZERO-SUSPEND");
    core::requireSuccessfulResult(system.resize({32, 32}), "PLATFORM-SMOKE-RESIZE");
    draw();
    core::requireSuccessfulResult(system.setSurfaceAvailable(false), "PLATFORM-SMOKE-PAUSE");
    check(!system.capabilities().canRender && !system.renderOrderedFrame(sky).success
            && system.valid(),
        "PLATFORM-SMOKE-PAUSE-GATE");
    core::requireSuccessfulResult(system.setSurfaceAvailable(true), "PLATFORM-SMOKE-RESUME");
    draw();
    core::requireSuccessfulResult(system.resize({64, 64}), "PLATFORM-SMOKE-RESTORE-EXTENT");
    draw();
}

// clang-format off
EM_JS(void, report, (const char* failure), {
    fetch('/result', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'WebGlPlatformIntegrationSmoke',contractVersion:1,success:!failure,
        diagnostic:failure?UTF8ToString(failure):"",frames:6,
        webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        gpuCompletionClaimed:false,productionReady:false})});
});
// clang-format on
} // namespace

int main()
{
    try {
        run();
        report(nullptr);
    } catch (const std::exception& error) {
        report(error.what());
    }
}
