#include "core/memory/cpu_allocator.h"
#include "platform/render_system.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_resources.h"
#include "rhi/gpu_swapchain.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

void requireText(const std::pmr::string& text, const std::pmr::string& token)
{
    assert(text.find(token) != std::pmr::string::npos);
}

void forbidText(const std::pmr::string& text, const std::pmr::string& token)
{
    assert(text.find(token) == std::pmr::string::npos);
}

arp::renderer::CustomRenderFrameWorkload makeWorkload(std::uint64_t frameSerial)
{
    static int scenePayload = 0;

    arp::renderer::CustomRenderFrameWorkloadDesc desc;
    desc.extent = arp::rhi::Extent2D{320, 180};
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.frameSerial = frameSerial;
    desc.backbufferState = arp::rhi::ResourceState::Present;
    desc.scene = arp::renderer::CustomWorkloadSection::enabled({
        arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
        &scenePayload,
        "tests/render_system_custom_graph_frame/ScenePayload",
        1u,
    });
    return arp::renderer::CustomRenderFrameWorkload(desc);
}

void assertInvalidRuntimeReportsNotValid()
{
    arp::platform::RenderSystem runtime;
    const auto result = runtime.renderGraphFrame(
        arp::rhi::AcquiredFrame{false, "not acquired", 7u, 0u}, makeWorkload(7u));
    assert(!result.success);
    assert(result.diagnostics.find("render system is not valid") != std::pmr::string::npos);
}

void assertCompatibleMeshUploadReusesExistingBuffers()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    auto uploaded = arp::renderer::uploadMeshToRhi(*device, mesh);
    assert(uploaded.success);
    const auto vertexBufferId = uploaded.mesh.vertexBuffer().id();
    const auto indexBufferId = uploaded.mesh.indexBuffer().id();

    auto updatedMesh = mesh;
    updatedMesh.vertices.front().position[0] += 0.25f;
    updatedMesh.indices = {2u, 1u, 0u};
    const auto updated =
        arp::renderer::uploadOrUpdateMeshToRhi(*device, updatedMesh, uploaded.mesh);
    assert(updated.success);
    assert(updated.reusedExistingBuffers);
    assert(uploaded.mesh.vertexBuffer().id() == vertexBufferId);
    assert(uploaded.mesh.indexBuffer().id() == indexBufferId);
}

void assertRenderSystemHeaderUsesRhiFrameContract(const std::filesystem::path& path)
{
    const auto text = readText(path);
    requireText(text, "#include \"rhi/gpu_common.h\"");
    requireText(text, "#include \"rhi/gpu_swapchain.h\"");
    requireText(text, "rhi::Backend backend() const");
    requireText(text, "rhi::Extent2D extent() const");
    requireText(text, "rhi::TextureFormat colorFormat() const");
    requireText(text, "RenderSystemResult resize(rhi::Extent2D extent)");
    requireText(text, "rhi::AcquiredFrame acquireFrame()");
    requireText(text, "const rhi::AcquiredFrame& frame");
    requireText(text, "rhi::PresentResult presentFrame");
    requireText(text, "RenderSystemPreparedRendererFramePrepareDesc");
    requireText(text, "RenderSystemPreparedRendererFramePrepareReport");
    requireText(text, "prepareRendererFrame(");
    forbidText(text, "#include \"rhi/");
    forbidText(text,
        "#include <"
        "rhi/");
    forbidText(text,
        "rhi"
        "::AcquiredFrame");
    forbidText(text,
        "rhi"
        "::FrameResult");
    forbidText(text,
        "rhi"
        "::TextureFormat");
    forbidText(text,
        "rhi"
        "::Extent2D");
}

void assertPlatformSourceUsesRhiFrameContract(
    const std::filesystem::path& path, const bool expectNativeVulkanSurfaceBackbuffer)
{
    const auto text = readText(path);
    requireText(text, "#include \"renderer/frame/render_frame_director.h\"");
    requireText(text, "#include \"renderer/mesh/mesh_gpu_buffer.h\"");
    requireText(text, "#include \"renderer/pipeline/render_pipeline.h\"");
    requireText(text, "#include \"renderer/ui/gui_renderer.h\"");
    requireText(text, "#include \"renderer/particles/particle_renderer.h\"");
    forbidText(text, "#include \"renderer/pipeline/custom_render_graph_composer.h\"");
    requireText(text, "rhi::AcquiredFrame RenderSystem::acquireFrame");
    requireText(text, "rhi::PresentResult RenderSystem::presentFrame");
    requireText(text, "rhi::Swapchain");
    requireText(text, "std::unique_ptr<renderer::RenderPipeline> pipeline;");
    requireText(text, "core::UniquePtr<renderer::GuiRenderer> guiRenderer;");
    requireText(text, "core::UniquePtr<renderer::ParticleRenderer> particleRenderer;");
    requireText(text, "ParticleRendererConfig particleRendererConfig;");
    requireText(text, "uploadOrUpdateMeshToRhi(*impl_->device, mesh, output)");
    forbidText(text, "uploadMeshToRhi(*impl_->device, mesh)");
    forbidText(text, "output = std::move(uploaded.mesh)");
    requireText(text, "pipeline->create(renderer::RenderPipelineCreateContext");
    requireText(text, "impl_->pipeline = std::move(pipeline)");
    requireText(text, "core::makeUnique<renderer::GuiRenderer>(*impl_->device)");
    requireText(text, "guiRenderer->create(rendererDesc)");
    requireText(text, "impl_->guiRenderer = std::move(guiRenderer)");
    requireText(text, "core::makeUnique<renderer::ParticleRenderer>(*impl_->device)");
    requireText(text, "particleRenderer->create(renderer::ParticleRendererDesc");
    requireText(text, "impl_->particleRendererConfig.shaderPath = shaderPath");
    requireText(text, "impl_->particleRendererConfig.maxParticles = maxParticles");
    requireText(text, "impl_->particleRenderer = std::move(particleRenderer)");
    requireText(text, "if (impl_->particleRenderer && impl_->particleRendererConfig.valid())");
    requireText(text, "particle renderer resize failed: ");
    requireText(text, "render_graph::FrameGraph graph(*impl_->device)");
    if (expectNativeVulkanSurfaceBackbuffer) {
        requireText(text, "impl_->swapchain->acquiredImage(frame)");
        requireText(text, "graph.importTexture(std::move(acquiredImage.texture))");
        requireText(text, "requiredVulkanInstanceExtensions");
        requireText(text, "createVulkanSurface");
        requireText(text, "runtimeDesc.enableSyntheticRuntime = false");
        requireText(text, "surfaceBackedPresentation = true");
        forbidText(text, "graph.importTexture(impl_->device->createTexture");
    } else {
        requireText(text, "graph.importTexture(impl_->device->createTexture");
    }
    requireText(text, "renderer::RenderPipelineFrameFeatures frameFeatures");
    requireText(text, "renderer::RenderFrameDirector");
    requireText(text, "renderer::RenderFrameDirectorRequest");
    requireText(text, "frameDirector.compose(");
    requireText(text, "preparedRendererFrame");
    requireText(text, "RenderFrameExecutionMode::PreparedSubmit");
    requireText(text, "legacyPipelineFallbackUsed");
    requireText(text, "sceneRuntimePathSubmitted");
    requireText(text, "!impl_->lastSceneRuntimeCustomGraphFrameReport.legacyPipelineFallbackUsed");
    forbidText(text, "impl_->pipeline->composeGraph(");
    forbidText(text, "renderer::sceneRuntimePipelinePayloadFrom(");
    forbidText(text, "renderer::addCustomPipelineComposition");
    forbidText(text, "impl_->pipeline->populateCustomGraphComposition");
    requireText(text, "renderer::RenderPipelineGraphCompositionContext");
    requireText(text, "pipelineRequest.pipelinePayload");
    requireText(text, "pipelineRequest.pipelinePayloadContractName");
    requireText(text, "pipelineRequest.pipelinePayloadContractVersion");
    requireText(text, "workload.scenePayloadContractName()");
    requireText(text, "workload.scenePayloadContractVersion()");
    forbidText(text, "impl_->pipeline->recordGraph(graphContext");
    forbidText(text, "impl_->pipeline->supportsGraphPostprocess");
    forbidText(text, "impl_->pipeline->recordGraphPostprocess");
    forbidText(text, "supportsGraphScenePass");
    forbidText(text, "recordGraphScenePass");
    forbidText(text, "PiKa");
    requireText(text, "textureBinding.explicitUpload");
    requireText(text, "textureBinding.textureResource");
    requireText(text, "impl_->particleRenderer->recordPass");
    requireText(text, "impl_->guiRenderer->recordPass");
    requireText(text, "graph.execute()");
    requireText(text, "Custom graph RHI composition/execution failed: ");
    requireText(text, "presented.recoverable");
    requireText(text, "presented.swapchainRebuildRequired");
    requireText(text, "frameSerial");
    requireText(text, "frameIndex");
    requireText(text, "workload.validate()");
    requireText(text, "frame serial");
    requireText(text, "invalid CustomRenderFrameWorkload");
    requireText(text, "present failure");
    forbidText(text, "#include \"rhi/");
    forbidText(text,
        "#include <"
        "rhi/");
    forbidText(text,
        "rhi"
        "::AcquiredFrame");
    forbidText(text,
        "rhi"
        "::FrameResult");
    forbidText(text,
        "rhi"
        "::TextureFormat");
    forbidText(text,
        "rhi"
        "::Vulkan");
    forbidText(text, "LegacyRhiRenderSystemHotPath");
    forbidText(text, "RHI mesh upload is pending Phase 5 renderer cutover");
    forbidText(text, "RHI render pipeline binding is pending Phase 5 renderer cutover");
    forbidText(text, "RHI particle renderer creation is pending Phase 5 renderer cutover");
    forbidText(text, "RHI GUI renderer creation is pending Phase 5 renderer cutover");
    forbidText(text, "RHI RenderGraph execution is pending Phase 4");
    forbidText(text, "graph execution is implemented in Phase D");
    forbidText(text, "composition.mainScene = [](render_graph::RenderContext&) {}");
    forbidText(text, "composition.particles = [](render_graph::RenderContext&) {}");
    forbidText(text, "composition.gui = [](render_graph::RenderContext&) {}");
}

void assertCharacterViewerHandlesRecoverablePresent(const std::filesystem::path& path)
{
    const auto text = readText(path);
    requireText(text, "presented.recoverable");
    requireText(text, "presented.swapchainRebuildRequired");
    requireText(text, "Character Viewer present skipped for recoverable swapchain lifecycle");
    requireText(text, "resizeViewport(");
    forbidText(text,
        "if (!presented.success) {\n        std::cerr << \"Character Viewer present failed: \" << presented.diagnostics << '\\n';\n        return runtime.fail(1);\n    }");
}

} // namespace

int main()
{
    assertInvalidRuntimeReportsNotValid();
    assertCompatibleMeshUploadReusesExistingBuffers();

    const auto root = projectRoot();
    assertRenderSystemHeaderUsesRhiFrameContract(root / "src" / "platform" / "render_system.h");
    assertPlatformSourceUsesRhiFrameContract(
        root / "src" / "platform" / "render_system_vulkan.cpp", true);
    assertPlatformSourceUsesRhiFrameContract(
        root / "src" / "platform" / "render_system_webgpu.cpp", false);
    assertCharacterViewerHandlesRecoverablePresent(
        root / "games" / "character_viewer" / "app" / "character_viewer_runtime.cpp");

    return 0;
}
