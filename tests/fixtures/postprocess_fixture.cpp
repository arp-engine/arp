#include "tests/fixtures/postprocess_fixture.h"

#include "renderer/gi/ddgi_renderer.h"
#include "renderer/postprocess/fxaa_renderer.h"
#include "renderer/postprocess/fxaa_rhi_pass.h"
#include "renderer/postprocess/ssao_renderer.h"
#include "renderer/postprocess/ssr_renderer.h"
#include "renderer/postprocess/taa_renderer.h"
#include "renderer/render_frame.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_swapchain.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "ssr.shader_bindings.h"
#include "wocc_post.shader_bindings.h"

namespace arp::tests::postprocess {
namespace {

std::uint32_t gDdgiParityModeOverride = 0u;

enum class FixturePattern : std::uint8_t {
    ConstantWhite,
    ColorChecker,
    DepthStep,
    DepthPlane,
    ZeroVelocity,
    ReactiveCenterLine,
};

enum class FixtureReadbackStatus : std::uint8_t {
    Unavailable,
    Available,
    Failed,
};

class DdgiSubmission final : public render_graph::PassSubmissionObserver {
public:
    explicit DdgiSubmission(renderer::DdgiRenderer& renderer) : renderer_(renderer) {}

    const char* validateSubmission(render_graph::PassSubmissionToken token) const noexcept override
    {
        return renderer_.isPreparedHistory(token) ? nullptr : "DDGI-HISTORY-SUBMISSION-STALE";
    }

    void onSubmitted(render_graph::PassSubmissionToken token,
        const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override
    {
        accepted = renderer_.acceptPreparedHistory(token, completion);
    }

    bool accepted = false;

private:
    renderer::DdgiRenderer& renderer_;
};

render_graph::PassBuilder populateDdgiGraph(render_graph::FrameGraph& graph,
    renderer::DdgiRenderer& renderer,
    rhi::Extent2D extent,
    renderer::DdgiPushConstants& push,
    std::uint32_t& descriptorFrameIndex,
    DdgiSubmission& submission,
    const DdgiFixtureFault* recordingFault = nullptr)
{
    const auto graphObject = &graph;
    const int readIndex = renderer.currentReadIndex();
    const int writeIndex = 1 - readIndex;
    rhi::TextureDesc depthDesc;
    depthDesc.label = "FeatureLab.DdgiDepth";
    depthDesc.extent = {extent.width, extent.height, 1u};
    depthDesc.format = rhi::TextureFormat::D32Float;
    depthDesc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled;
    depthDesc.memory = rhi::MemoryUsage::Transient;
    const auto depth = graph.createTransientTexture(depthDesc);
    graph.addPass("DDGI.DepthFixture")
        .writeAttachment(
            depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
        .executeRenderPass([](render_graph::RenderPassContext&) {});
    rhi::TextureDesc normalDesc;
    normalDesc.label = "FeatureLab.DdgiNormal";
    normalDesc.extent = {extent.width, extent.height, 1u};
    normalDesc.format = rhi::TextureFormat::Rgba16Float;
    normalDesc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled;
    normalDesc.memory = rhi::MemoryUsage::Transient;
    const auto normal = graph.createTransientTexture(normalDesc);
    graph.addPass("DDGI.NormalFixture")
        .writeAttachment(
            normal, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
        .executeRenderPass([](render_graph::RenderPassContext&) {});
    const auto irradianceRead = graph.importTextureRef(renderer.probeIrradiance(readIndex));
    const auto irradianceWrite = graph.importTextureRef(renderer.probeIrradiance(writeIndex));
    const auto momentsRead = graph.importTextureRef(renderer.probeDepthMoments(readIndex));
    const auto momentsWrite = graph.importTextureRef(renderer.probeDepthMoments(writeIndex));
    const auto giWrite = graph.importTextureRef(renderer.giOutput(writeIndex));
    graph.addPass("DDGI.HistoryCopy")
        .kind(render_graph::PassKind::Copy)
        .read(irradianceRead, rhi::ResourceState::CopySrc)
        .write(irradianceWrite, rhi::ResourceState::CopyDst)
        .read(momentsRead, rhi::ResourceState::CopySrc)
        .write(momentsWrite, rhi::ResourceState::CopyDst)
        .execute([&renderer,
                     graphObject,
                     irradianceRead,
                     irradianceWrite,
                     momentsRead,
                     momentsWrite,
                     recordingFault](render_graph::RenderContext& context) {
            renderer.recordHistoryCopyPass(context,
                graphObject->texture(irradianceRead),
                graphObject->texture(irradianceWrite),
                graphObject->texture(momentsRead),
                graphObject->texture(momentsWrite));
            if (recordingFault && *recordingFault == DdgiFixtureFault::AfterHistoryCopy)
                throw core::DiagnosticError("FEATURE-LAB-DDGI-RECORD-REJECTED");
        });
    graph.addPass("DDGI.ProbeRelocate")
        .kind(render_graph::PassKind::Compute)
        .read(depth, rhi::ResourceState::ShaderRead)
        .read(normal, rhi::ResourceState::ShaderRead)
        .read(irradianceRead, rhi::ResourceState::ShaderRead)
        .read(momentsRead, rhi::ResourceState::ShaderRead)
        .write(irradianceWrite, rhi::ResourceState::ShaderWrite)
        .execute([&renderer, graphObject, depth, normal, &push, &descriptorFrameIndex](
                     render_graph::RenderContext& context) {
            renderer.recordProbeRelocatePass(context,
                graphObject->texture(depth),
                graphObject->texture(normal),
                push,
                descriptorFrameIndex);
        });
    graph.addPass("DDGI.ProbeTrace")
        .kind(render_graph::PassKind::Compute)
        .read(depth, rhi::ResourceState::ShaderRead)
        .read(normal, rhi::ResourceState::ShaderRead)
        .execute([&renderer, graphObject, depth, normal, &push, &descriptorFrameIndex](
                     render_graph::RenderContext& context) {
            renderer.recordProbeTracePass(context,
                graphObject->texture(depth),
                graphObject->texture(normal),
                push,
                descriptorFrameIndex);
        });
    graph.addPass("DDGI.BlendIrradiance")
        .kind(render_graph::PassKind::Compute)
        .read(irradianceRead, rhi::ResourceState::ShaderRead)
        .write(irradianceWrite, rhi::ResourceState::ShaderWrite)
        .execute([&renderer, &push, &descriptorFrameIndex](render_graph::RenderContext& context) {
            renderer.recordProbeBlendIrradiancePass(context, push, descriptorFrameIndex);
        });
    graph.addPass("DDGI.BlendDistance")
        .kind(render_graph::PassKind::Compute)
        .read(momentsRead, rhi::ResourceState::ShaderRead)
        .write(momentsWrite, rhi::ResourceState::ShaderWrite)
        .execute([&renderer, &push, &descriptorFrameIndex](render_graph::RenderContext& context) {
            renderer.recordProbeBlendDistancePass(context, push, descriptorFrameIndex);
        });
    graph.addPass("DDGI.BorderSourceCopy")
        .kind(render_graph::PassKind::Copy)
        .read(irradianceWrite, rhi::ResourceState::CopySrc)
        .write(irradianceRead, rhi::ResourceState::CopyDst)
        .read(momentsWrite, rhi::ResourceState::CopySrc)
        .write(momentsRead, rhi::ResourceState::CopyDst)
        .execute(
            [&renderer, graphObject, irradianceWrite, irradianceRead, momentsWrite, momentsRead](
                render_graph::RenderContext& context) {
                renderer.recordProbeBorderSourceCopyPass(context,
                    graphObject->texture(irradianceWrite),
                    graphObject->texture(irradianceRead),
                    graphObject->texture(momentsWrite),
                    graphObject->texture(momentsRead));
            });
    graph.addPass("DDGI.BorderUpdate")
        .kind(render_graph::PassKind::Compute)
        .read(irradianceRead, rhi::ResourceState::ShaderRead)
        .read(momentsRead, rhi::ResourceState::ShaderRead)
        .write(irradianceWrite, rhi::ResourceState::ShaderWrite)
        .write(momentsWrite, rhi::ResourceState::ShaderWrite)
        .execute([&renderer, &push, &descriptorFrameIndex](render_graph::RenderContext& context) {
            renderer.recordProbeBorderUpdatePass(context, push, descriptorFrameIndex);
        });
    auto resolve = graph.addPass("DDGI.Resolve")
                       .onSubmitted(submission, renderer.preparedHistoryToken())
                       .kind(render_graph::PassKind::Compute)
                       .read(depth, rhi::ResourceState::ShaderRead)
                       .read(normal, rhi::ResourceState::ShaderRead)
                       .read(irradianceWrite, rhi::ResourceState::ShaderRead)
                       .read(momentsWrite, rhi::ResourceState::ShaderRead)
                       .write(giWrite, rhi::ResourceState::ShaderWrite);
    resolve.execute(
        [&renderer, graphObject, depth, normal, &push, &descriptorFrameIndex, recordingFault](
            render_graph::RenderContext& context) {
            renderer.recordResolvePass(context,
                graphObject->texture(depth),
                graphObject->texture(normal),
                push,
                descriptorFrameIndex);
            if (recordingFault && *recordingFault == DdgiFixtureFault::AfterResolve)
                throw core::DiagnosticError("FEATURE-LAB-DDGI-RECORD-REJECTED");
        });
    return resolve;
}

bool establishDdgiHistory(rhi::GpuDevice& device,
    renderer::DdgiRenderer& renderer,
    rhi::Extent2D extent,
    renderer::DdgiPushConstants push)
{
    // A real update/resolve establishes history before lifecycle recreation.
    // This standalone renderer has no scene BVH; its terrain input is explicit.
    for (auto& value : push.sceneBvhParams)
        value = 0.0f;
    const auto prepared = renderer.prepareProbeUpdateSchedule(push, 0u);
    if (!prepared.success)
        return false;
    std::uint32_t frame = 0u;
    DdgiSubmission submission(renderer);
    render_graph::FrameGraph graph(device);
    auto binding = populateDdgiGraph(graph, renderer, extent, push, frame, submission);
    // The original graph must not commit a candidate prepared afterwards.
    const auto replacement = renderer.prepareProbeUpdateSchedule(push, 0u);
    if (!replacement.success)
        return false;
    const auto submissions = device.queue().submitCount();
    bool staleRejected = false;
    try {
        graph.execute();
    } catch (const core::DiagnosticError& error) {
        staleRejected = std::string_view(error.what()) == "DDGI-HISTORY-SUBMISSION-STALE";
    }
    if (!staleRejected || graph.lastExecutionSubmitted() || submission.accepted
        || device.queue().submitCount() != submissions)
        return false;
    binding.onSubmitted(submission, renderer.preparedHistoryToken());
    graph.execute();
    device.waitIdle();
    return submission.accepted && renderer.hasValidGiHistory();
}

class DdgiFixtureTerrainSurface final : public terrain::TerrainSurface {
public:
    terrain::TerrainBounds bounds() const noexcept override
    {
        return {-50.0f, 50.0f, -50.0f, 50.0f};
    }

    std::uint64_t generation() const noexcept override { return 1u; }

    terrain::TerrainSurfaceQuery sample(float x, float z) const override
    {
        terrain::TerrainSurfaceQuery result;
        if (!bounds().contains(x, z)) {
            result.status = terrain::TerrainSurfaceQueryStatus::OutOfBounds;
            return result;
        }
        result.status = terrain::TerrainSurfaceQueryStatus::Ready;
        result.sample.height = 0.0f;
        return result;
    }
};

core::pmr::String shaderPath(const char* artifact)
{
#ifdef ARP_SHADER_DIR
    return core::toPmrString((std::filesystem::path(ARP_SHADER_DIR) / artifact).string());
#else
    return core::toPmrString((std::filesystem::path("shaders") / artifact).string());
#endif
}

core::UniquePtr<rhi::GpuTexture> createInputTexture(
    rhi::GpuDevice& device, const char* label, rhi::Extent2D extent, rhi::TextureFormat format)
{
    rhi::TextureDesc desc;
    desc.label = label;
    desc.extent = {extent.width, extent.height, 1u};
    desc.format = format;
    desc.usage =
        rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst | rhi::TextureUsage::CopySrc;
    desc.memory = rhi::MemoryUsage::GpuOnly;
    desc.initialState = rhi::ResourceState::Undefined;
    return device.createTexture(desc);
}

core::UniquePtr<rhi::GpuTexture> createOutputTexture(
    rhi::GpuDevice& device, const char* label, rhi::Extent2D extent, rhi::TextureFormat format)
{
    rhi::TextureDesc desc;
    desc.label = label;
    desc.extent = {extent.width, extent.height, 1u};
    desc.format = format;
    desc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
        | rhi::TextureUsage::CopySrc;
    desc.memory = rhi::MemoryUsage::GpuOnly;
    desc.initialState = rhi::ResourceState::Undefined;
    return device.createTexture(desc);
}

core::UniquePtr<rhi::GpuTexture> createDepthFixtureTexture(
    rhi::GpuDevice& device, const char* label, rhi::Extent2D extent)
{
    rhi::TextureDesc desc;
    desc.label = label;
    desc.extent = {extent.width, extent.height, 1u};
    desc.format = rhi::TextureFormat::D32Float;
    desc.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
        | rhi::TextureUsage::CopySrc;
    desc.memory = rhi::MemoryUsage::GpuOnly;
    desc.initialState = rhi::ResourceState::Undefined;
    return device.createTexture(desc);
}

bool rasterizeDepthStepFixture(rhi::GpuDevice& device,
    rhi::GpuTexture& depth,
    rhi::Extent2D extent,
    core::pmr::String& diagnostic)
{
    auto layout = device.createPipelineLayout({});
    if (!layout) {
        diagnostic = "FEATURE-LAB-DEPTH-FIXTURE-LAYOUT";
        return false;
    }
    const auto shader = shaderPath("feature_lab_depth_fixture.slang");
    auto package =
        renderer::detail::makeGraphicsShaderPackage("FeatureLabDepthFixture", shader, 0u);
    const auto hydrated = renderer::detail::validateGeneratedReflectionHydration(package);
    if (!hydrated.success) {
        diagnostic = hydrated.diagnostics;
        return false;
    }
    const auto shaderValid = rhi::validateShaderPackage(package);
    if (!shaderValid.success) {
        diagnostic = shaderValid.diagnostics;
        return false;
    }
    auto pipelineDesc = renderer::detail::makeRenderPipelineDesc(
        "FeatureLabDepthFixture", *layout, package, rhi::TextureFormat::Unknown, extent);
    pipelineDesc.depthFormat = rhi::TextureFormat::D32Float;
    pipelineDesc.depthStencil.format = rhi::TextureFormat::D32Float;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = rhi::CompareOp::Always;
    auto pipeline = device.createRenderPipeline(pipelineDesc);
    if (!pipeline) {
        const auto validation = device.validateRenderPipeline(pipelineDesc);
        diagnostic = validation.diagnostics.empty() ? "FEATURE-LAB-DEPTH-FIXTURE-PIPELINE"
                                                    : validation.diagnostics;
        return false;
    }
    auto encoder = device.createCommandEncoder({"FeatureLab.DepthFixture"});
    if (!encoder) {
        diagnostic = "FEATURE-LAB-DEPTH-FIXTURE-ENCODER";
        return false;
    }
    const auto attachment = encoder->transition(
        depth, rhi::ResourceState::Undefined, rhi::ResourceState::DepthAttachment);
    if (!attachment.success) {
        diagnostic = attachment.diagnostics;
        return false;
    }
    rhi::RenderPassDesc passDesc;
    passDesc.label = "FeatureLab.DepthFixture";
    passDesc.hasDepthStencilAttachment = true;
    passDesc.depthStencilAttachment = {
        "FeatureLab.DepthFixtureAttachment",
        depth.id(),
        rhi::TextureFormat::D32Float,
        rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::DontCare,
    };
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    const auto bound = pass->setPipeline(*pipeline);
    const auto drawn = bound.success ? pass->draw(12u, 1u, 0u, 0u) : bound;
    const auto ended = pass->end();
    if (!bound.success || !drawn.success || !ended.success) {
        diagnostic = !bound.success ? bound.diagnostics
                                    : (!drawn.success ? drawn.diagnostics : ended.diagnostics);
        return false;
    }
    const auto readable = encoder->transition(
        depth, rhi::ResourceState::DepthAttachment, rhi::ResourceState::ShaderRead);
    if (!readable.success) {
        diagnostic = readable.diagnostics;
        return false;
    }
    auto commands = encoder->finish();
    if (!commands) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    const auto submitted = device.queue().submit(*commands);
    if (!submitted.success) {
        diagnostic = submitted.diagnostics;
        return false;
    }
    return true;
}

bool drawColorAttachmentProbe(rhi::GpuDevice& device,
    rhi::GpuTexture& output,
    rhi::Extent2D extent,
    core::pmr::String& diagnostic)
{
    auto layout = device.createPipelineLayout({});
    if (!layout) {
        diagnostic = "FEATURE-LAB-COLOR-PROBE-LAYOUT";
        return false;
    }
    const auto shader = shaderPath("feature_lab_color_probe.slang");
    auto package = renderer::detail::makeGraphicsShaderPackage("FeatureLabColorProbe", shader, 0u);
    const auto hydrated = renderer::detail::validateGeneratedReflectionHydration(package);
    if (!hydrated.success) {
        diagnostic = hydrated.diagnostics;
        return false;
    }
    const auto shaderValid = rhi::validateShaderPackage(package);
    if (!shaderValid.success) {
        diagnostic = shaderValid.diagnostics;
        return false;
    }
    const auto pipelineDesc = renderer::detail::makeRenderPipelineDesc(
        "FeatureLabColorProbe", *layout, package, output.desc().format, extent);
    auto pipeline = device.createRenderPipeline(pipelineDesc);
    if (!pipeline) {
        const auto validation = device.validateRenderPipeline(pipelineDesc);
        diagnostic = validation.diagnostics.empty() ? "FEATURE-LAB-COLOR-PROBE-PIPELINE"
                                                    : validation.diagnostics;
        return false;
    }
    auto encoder = device.createCommandEncoder({"FeatureLab.ColorProbe"});
    if (!encoder) {
        diagnostic = "FEATURE-LAB-COLOR-PROBE-ENCODER";
        return false;
    }
    if (output.state() != rhi::ResourceState::ColorAttachment) {
        const auto colorAttachment =
            encoder->transition(output, output.state(), rhi::ResourceState::ColorAttachment);
        if (!colorAttachment.success) {
            diagnostic = colorAttachment.diagnostics;
            return false;
        }
    }
    rhi::RenderPassDesc passDesc;
    passDesc.label = "FeatureLab.ColorProbe";
    passDesc.colorTargetName = output.desc().label;
    passDesc.colorAttachments.push_back({"FeatureLab.ColorProbeAttachment",
        output.id(),
        output.desc().format,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::Store});
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    const auto bound = pass->setPipeline(*pipeline);
    const auto drawn = bound.success ? pass->draw(3u, 1u, 0u, 0u) : bound;
    const auto ended = pass->end();
    if (!bound.success || !drawn.success || !ended.success) {
        diagnostic = !bound.success ? bound.diagnostics
                                    : (!drawn.success ? drawn.diagnostics : ended.diagnostics);
        return false;
    }
    auto commands = encoder->finish();
    if (!commands) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    const auto submitted = device.queue().submit(*commands);
    if (!submitted.success) {
        diagnostic = submitted.diagnostics;
        return false;
    }
    return true;
}

bool prepareOutputTexture(
    rhi::GpuDevice& device, rhi::GpuTexture& texture, core::pmr::String& diagnostic)
{
    auto encoder = device.createCommandEncoder({"FeatureLab.OutputTransition"});
    if (!encoder) {
        diagnostic = "FEATURE-LAB-OUTPUT-TRANSITION-ENCODER";
        return false;
    }
    const auto transitioned = encoder->transition(
        texture, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
    if (!transitioned.success) {
        diagnostic = transitioned.diagnostics;
        return false;
    }
    auto commands = encoder->finish();
    if (!commands) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    const auto submitted = device.queue().submit(*commands);
    if (!submitted.success) {
        diagnostic = submitted.diagnostics;
        return false;
    }
    return true;
}

bool uploadDeterministicFixture(rhi::GpuDevice& device,
    rhi::GpuTexture& texture,
    FixturePattern pattern,
    core::pmr::String& diagnostic)
{
    const auto pixelBytes = static_cast<std::uint32_t>(rhi::bytesPerPixel(texture.desc().format));
    if (pixelBytes == 0u) {
        diagnostic = "FEATURE-LAB-FIXTURE-FORMAT-UNSUPPORTED";
        return false;
    }
    const auto width = texture.desc().extent.width;
    const auto height = texture.desc().extent.height;
    const auto bytesPerRow = ((width * pixelBytes) + 255u) & ~255u;
    core::pmr::Vector<std::uint8_t> bytes(std::size_t(bytesPerRow) * height, std::uint8_t{0u});
    for (std::uint32_t y = 0u; y < height; ++y) {
        for (std::uint32_t x = 0u; x < width; ++x) {
            const auto offset = std::size_t(y) * bytesPerRow + std::size_t(x) * pixelBytes;
            if (pattern == FixturePattern::DepthStep || pattern == FixturePattern::DepthPlane) {
                const float depth = pattern == FixturePattern::DepthPlane
                    ? 0.35f
                    : (x < width / 2u ? 0.35f : 0.41f);
                std::memcpy(bytes.data() + offset, &depth, sizeof(depth));
            } else if (pattern == FixturePattern::ReactiveCenterLine) {
                bytes[offset] = x == width / 2u ? 255u : 0u;
            } else if (pattern == FixturePattern::ConstantWhite) {
                std::fill_n(bytes.data() + offset, pixelBytes, std::uint8_t{255u});
            } else if (pattern == FixturePattern::ZeroVelocity) {
                std::fill_n(bytes.data() + offset, pixelBytes, std::uint8_t{0u});
            } else {
                for (std::uint32_t channel = 0u; channel < pixelBytes; ++channel) {
                    bytes[offset + channel] =
                        static_cast<std::uint8_t>((x * 17u + y * 31u + channel * 47u) & 0xffu);
                }
            }
        }
    }

    auto encoder = device.createCommandEncoder({"FeatureLab.FixtureUpload"});
    if (!encoder) {
        diagnostic = "FEATURE-LAB-FIXTURE-UPLOAD-ENCODER";
        return false;
    }
    rhi::TextureSubresourceRange range;
    if (rhi::isDepthStencilFormat(texture.desc().format)) {
        range.aspect = rhi::TextureAspect::Depth;
    }
    const auto transitioned = encoder->transition(
        texture, range, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst);
    if (!transitioned.success) {
        diagnostic = transitioned.diagnostics;
        return false;
    }
    auto transfer = encoder->beginTransferPass({"FeatureLab.FixtureUpload"});
    if (!transfer) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    rhi::UploadTextureDesc upload;
    upload.label = texture.desc().label + ".FixtureUpload";
    upload.destinationTexture = texture.id();
    upload.extent = texture.desc().extent;
    upload.format = texture.desc().format;
    upload.bytesPerRow = bytesPerRow;
    upload.rowsPerImage = height;
    upload.subresources = range;
    upload.pixelData = bytes.data();
    upload.pixelDataSize = static_cast<std::uint32_t>(bytes.size());
    const auto uploaded = transfer->uploadTexture(upload);
    if (!uploaded.success) {
        transfer->end();
        diagnostic = texture.desc().label;
        diagnostic += ": ";
        diagnostic += uploaded.diagnostics;
        return false;
    }
    const auto transferEnded = transfer->end();
    if (!transferEnded.success) {
        diagnostic = texture.desc().label;
        diagnostic += ": ";
        diagnostic += transferEnded.diagnostics;
        return false;
    }
    const auto shaderRead = encoder->transition(
        texture, range, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
    if (!shaderRead.success) {
        diagnostic = shaderRead.diagnostics;
        return false;
    }
    auto commands = encoder->finish();
    if (!commands) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    const auto submitted = device.queue().submit(*commands);
    if (!submitted.success) {
        diagnostic = submitted.diagnostics;
        return false;
    }
    return true;
}

FixtureReadbackStatus readbackOutput(rhi::GpuDevice& device,
    rhi::GpuTexture& texture,
    const char* label,
    rhi::TextureReadbackSample& sample,
    core::pmr::String& diagnostic)
{
    auto* extension = static_cast<rhi::TextureReadbackExtension*>(
        device.queryInterface(rhi::kExtensionTextureReadback));
    if (!extension) {
        return FixtureReadbackStatus::Unavailable;
    }
    auto encoder = device.createCommandEncoder({"FeatureLab.AttachmentReadback"});
    if (!encoder) {
        diagnostic = "FEATURE-LAB-READBACK-ENCODER";
        return FixtureReadbackStatus::Failed;
    }
    const auto previousState = texture.state();
    const auto copySource =
        encoder->transition(texture, previousState, rhi::ResourceState::CopySrc);
    if (!copySource.success) {
        diagnostic = copySource.diagnostics;
        return FixtureReadbackStatus::Failed;
    }
    std::string backendDiagnostic;
    if (!extension->schedule(*encoder, texture, label, backendDiagnostic)) {
        diagnostic = core::toPmrString(backendDiagnostic);
        return FixtureReadbackStatus::Failed;
    }
    const auto restored = encoder->transition(texture, rhi::ResourceState::CopySrc, previousState);
    if (!restored.success) {
        diagnostic = restored.diagnostics;
        return FixtureReadbackStatus::Failed;
    }
    auto commands = encoder->finish();
    if (!commands) {
        diagnostic = encoder->lastValidation().diagnostics;
        return FixtureReadbackStatus::Failed;
    }
    const auto submitted = device.queue().submit(*commands);
    if (!submitted.success) {
        diagnostic = submitted.diagnostics;
        return FixtureReadbackStatus::Failed;
    }
    device.waitIdle();
#if defined(__EMSCRIPTEN__)
    // WebGPU mapAsync completion is delivered on the browser event loop;
    // wait cooperatively so the callback can publish the sample.
    for (std::uint32_t attempt = 0u; attempt < 200u; ++attempt) {
        if (extension->poll(sample))
            return FixtureReadbackStatus::Available;
        emscripten_sleep(1);
    }
#endif
    if (!extension->poll(sample)) {
        diagnostic = "FEATURE-LAB-READBACK-NOT-READY-AFTER-IDLE";
        return FixtureReadbackStatus::Failed;
    }
    if (!sample.valid) {
        diagnostic = sample.diagnostic.empty() ? "FEATURE-LAB-READBACK-INVALID"
                                               : core::toPmrString(sample.diagnostic);
        return FixtureReadbackStatus::Failed;
    }
    return FixtureReadbackStatus::Available;
}

renderer::RenderFrameContext frameContext(
    rhi::GpuTexture& output, rhi::Extent2D extent, std::uint32_t frameIndex)
{
    rhi::AcquiredFrame frame;
    frame.success = true;
    frame.frameIndex = frameIndex;
    rhi::RenderTargetDesc target;
    target.label = output.desc().label;
    target.extent = extent;
    target.format = output.desc().format;
    target.finalState = rhi::ResourceState::ColorAttachment;
    return {frame, target, nullptr, &output, nullptr, nullptr};
}

PostProcessFixtureResult fail(const char* id, const core::pmr::String& detail = {})
{
    PostProcessFixtureResult result;
    result.diagnostic = id;
    if (!detail.empty()) {
        result.diagnostic += ": ";
        result.diagnostic += detail;
    }
    return result;
}

} // namespace

void setDdgiParityModeOverride(std::uint32_t mode)
{
    gDdgiParityModeOverride = mode;
}

PostProcessFixtureResult runSsaoFeatureFixture(rhi::Extent2D extent)
{
    auto device = rhi::createNullDevice();
    if (!device)
        return fail("FEATURE-LAB-SSAO-NULL-DEVICE");
    return runSsaoFeatureFixture(*device, extent);
}

PostProcessFixtureResult runSsaoFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent)
{
    extent.width = std::clamp(extent.width, 8u, 64u);
    extent.height = std::clamp(extent.height, 8u, 64u);

    auto depth = createDepthFixtureTexture(device, "FeatureLab.SsaoDepthFixture", extent);
    auto output = createOutputTexture(
        device, "FeatureLab.SsaoVisibility", extent, rhi::TextureFormat::R8Unorm);
    if (!depth || !output)
        return fail("FEATURE-LAB-SSAO-TEXTURE-CREATE");
    core::pmr::String diagnostic;
    if (!rasterizeDepthStepFixture(device, *depth, extent, diagnostic)
        || !prepareOutputTexture(device, *output, diagnostic)) {
        return fail("FEATURE-LAB-SSAO-FIXTURE-UPLOAD", diagnostic);
    }

    // Read the fixture depth once before the production pass. This is a
    // diagnostic observation only; acceptance remains based on its AO output.
    rhi::TextureReadbackSample depthReadback;
    const auto depthReadbackStatus =
        readbackOutput(device, *depth, "FeatureLab.SsaoDepthFixture", depthReadback, diagnostic);
    if (depthReadbackStatus == FixtureReadbackStatus::Failed
        || (depthReadbackStatus == FixtureReadbackStatus::Unavailable
            && device.backend() != rhi::Backend::Null)) {
        return fail("FEATURE-LAB-SSAO-DEPTH-READBACK", diagnostic);
    }
    renderer::SsaoRenderer renderer(device);
    const auto created =
        renderer.create({shaderPath("ssao.slang"), rhi::TextureFormat::R8Unorm, extent});
    if (!created.success)
        return fail("FEATURE-LAB-SSAO-CREATE", created.diagnostics);
    const auto before = device.queue().submitCount();
    renderer::SsaoSettings fixtureSettings;
    fixtureSettings.intensity = 2.0f;
    fixtureSettings.depthBias = 0.001f;
    // The deterministic D32 step is deliberately small. A wider fixture
    // falloff keeps its AO contrast above 8-bit readback quantization on
    // WebGPU without changing the production SSAO defaults.
    fixtureSettings.depthFalloff = 8.0f;
    const auto drawn =
        renderer.drawFrame(frameContext(*output, extent, 0u), *depth, fixtureSettings);
    if (!drawn.success)
        return fail("FEATURE-LAB-SSAO-DRAW", drawn.diagnostics);

    rhi::TextureReadbackSample readback;
    const auto readbackStatus =
        readbackOutput(device, *output, "FeatureLab.SsaoVisibility", readback, diagnostic);
    if (readbackStatus == FixtureReadbackStatus::Failed
        || (readbackStatus == FixtureReadbackStatus::Unavailable
            && device.backend() != rhi::Backend::Null)) {
        return fail("FEATURE-LAB-SSAO-READBACK", diagnostic);
    }

    PostProcessFixtureResult result;
    result.success = true;
    result.passTrace.push_back("Post.Ssao");
    result.submittedFrameCount = device.queue().submitCount() - before;
    result.attachmentMetrics.push_back({"ao",
        "visibility_range",
        static_cast<float>(readback.maxValue - readback.minValue),
        readbackStatus == FixtureReadbackStatus::Available});
    result.attachmentMetrics.push_back({"ao",
        "visibility_mean",
        static_cast<float>(readback.meanValue),
        readbackStatus == FixtureReadbackStatus::Available});
    result.attachmentMetrics.push_back({"ssao-depth-fixture",
        "depth_range",
        static_cast<float>(depthReadback.maxValue - depthReadback.minValue),
        depthReadbackStatus == FixtureReadbackStatus::Available});
    result.gpuMemoryBytes = device.memoryStats().allocatedBytes;
    device.waitIdle();
    return result;
}

PostProcessFixtureResult runTaaFeatureFixture(rhi::Extent2D extent)
{
    auto device = rhi::createNullDevice();
    if (!device)
        return fail("FEATURE-LAB-TAA-NULL-DEVICE");
    return runTaaFeatureFixture(*device, extent);
}

PostProcessFixtureResult runTaaFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent)
{
    extent.width = std::clamp(extent.width, 8u, 64u);
    extent.height = std::clamp(extent.height, 8u, 64u);

    auto color = createInputTexture(
        device, "FeatureLab.TaaColorFixture", extent, rhi::TextureFormat::Bgra8Unorm);
    auto velocity = createInputTexture(
        device, "FeatureLab.TaaVelocityFixture", extent, rhi::TextureFormat::Rg16Float);
    auto depth = createDepthFixtureTexture(device, "FeatureLab.TaaDepthFixture", extent);
    auto reactive = createInputTexture(
        device, "FeatureLab.TaaReactiveFixture", extent, rhi::TextureFormat::R8Unorm);
    auto output =
        createOutputTexture(device, "FeatureLab.TaaOutput", extent, rhi::TextureFormat::Bgra8Unorm);
    auto moments =
        createOutputTexture(device, "FeatureLab.TaaMoments", extent, rhi::TextureFormat::Rg16Float);
    if (!color || !velocity || !depth || !reactive || !output || !moments) {
        return fail("FEATURE-LAB-TAA-TEXTURE-CREATE");
    }
    core::pmr::String diagnostic;
    if (!uploadDeterministicFixture(device, *color, FixturePattern::ColorChecker, diagnostic)
        || !uploadDeterministicFixture(device, *velocity, FixturePattern::ZeroVelocity, diagnostic)
        || !rasterizeDepthStepFixture(device, *depth, extent, diagnostic)
        || !uploadDeterministicFixture(
            device, *reactive, FixturePattern::ReactiveCenterLine, diagnostic)) {
        return fail("FEATURE-LAB-TAA-FIXTURE-UPLOAD", diagnostic);
    }
    if (!prepareOutputTexture(device, *output, diagnostic)
        || !prepareOutputTexture(device, *moments, diagnostic)) {
        return fail("FEATURE-LAB-TAA-OUTPUT-TRANSITION", diagnostic);
    }

    renderer::TaaRenderer renderer(device);
    const auto created =
        renderer.create({shaderPath("wocc_taa.slang"), rhi::TextureFormat::Bgra8Unorm, extent});
    if (!created.success)
        return fail("FEATURE-LAB-TAA-CREATE", created.diagnostics);
    const auto before = device.queue().submitCount();
    renderer.invalidateHistory();
    const auto first = renderer.drawFrame(
        frameContext(*output, extent, 0u), *color, *velocity, *depth, *reactive, *moments);
    if (!first.success)
        return fail("FEATURE-LAB-TAA-FIRST-FRAME", first.diagnostics);
    rhi::TextureReadbackSample firstReadback;
    const auto firstReadbackStatus =
        readbackOutput(device, *output, "FeatureLab.TaaFirstFrame", firstReadback, diagnostic);
    if (firstReadbackStatus == FixtureReadbackStatus::Failed
        || (firstReadbackStatus == FixtureReadbackStatus::Unavailable
            && device.backend() != rhi::Backend::Null)) {
        return fail("FEATURE-LAB-TAA-FIRST-READBACK", diagnostic);
    }
    renderer.markHistoryValid();
    const auto accumulated = renderer.drawFrame(
        frameContext(*output, extent, 1u), *color, *velocity, *depth, *reactive, *moments);
    if (!accumulated.success) {
        return fail("FEATURE-LAB-TAA-ACCUMULATED-FRAME", accumulated.diagnostics);
    }
    renderer::TaaSettings resetSettings;
    resetSettings.resetHistory = true;
    const auto reset = renderer.drawFrame(frameContext(*output, extent, 0u),
        *color,
        *velocity,
        *depth,
        *reactive,
        *moments,
        resetSettings);
    if (!reset.success)
        return fail("FEATURE-LAB-TAA-RESET-FRAME", reset.diagnostics);
    rhi::TextureReadbackSample resetReadback;
    const auto resetReadbackStatus =
        readbackOutput(device, *output, "FeatureLab.TaaResetFrame", resetReadback, diagnostic);
    if (resetReadbackStatus == FixtureReadbackStatus::Failed
        || (resetReadbackStatus == FixtureReadbackStatus::Unavailable
            && device.backend() != rhi::Backend::Null)) {
        return fail("FEATURE-LAB-TAA-RESET-READBACK", diagnostic);
    }

    PostProcessFixtureResult result;
    result.success = true;
    result.passTrace.push_back("Post.TaaResolve.FirstFrame");
    result.passTrace.push_back("Post.TaaResolve.Accumulated");
    result.passTrace.push_back("Post.TaaResolve.Reset");
    result.submittedFrameCount = device.queue().submitCount() - before;
    const bool readbacksAvailable = firstReadbackStatus == FixtureReadbackStatus::Available
        && resetReadbackStatus == FixtureReadbackStatus::Available;
    result.attachmentMetrics.push_back({"taa-history",
        "reset_match",
        readbacksAvailable && firstReadback.sampleHash == resetReadback.sampleHash ? 1.0f : 0.0f,
        readbacksAvailable});
    result.gpuMemoryBytes = device.memoryStats().allocatedBytes;
    device.waitIdle();
    return result;
}

namespace {

using PostPushConstants = arp::shader::gen::WoccPostPushConstants;

struct PostPassPipeline {
    core::UniquePtr<rhi::RenderPipeline> pipeline;
};

core::pmr::String replaceShaderExtension(const core::pmr::String& path, const char* extension)
{
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == core::pmr::String::npos || (slash != core::pmr::String::npos && dot < slash)) {
        return path + extension;
    }
    return path.substr(0, dot) + extension;
}

bool writePostDescriptors(rhi::GpuDevice& device,
    rhi::DescriptorSet& set,
    rhi::GpuTexture& input,
    rhi::GpuTexture& bloom,
    rhi::GpuSampler& sampler,
    core::UniquePtr<rhi::GpuTextureView>& inputView,
    core::UniquePtr<rhi::GpuTextureView>& bloomView,
    core::pmr::String& diagnostic)
{
    auto nextInputView = device.createTextureView(
        input, {"FeatureLab.PostInputView", input.desc().format, rhi::TextureUsage::Sampled});
    auto nextBloomView = device.createTextureView(
        bloom, {"FeatureLab.PostBloomView", bloom.desc().format, rhi::TextureUsage::Sampled});
    if (!nextInputView || !nextBloomView) {
        diagnostic = "FEATURE-LAB-POST-DESCRIPTOR-VIEW";
        return false;
    }
    const auto inputWritten = set.write({0u,
        rhi::DescriptorType::SampledTexture,
        nextInputView->id(),
        rhi::DescriptorResourceKind::TextureView});
    const auto samplerWritten = set.write(
        {1u, rhi::DescriptorType::Sampler, sampler.id(), rhi::DescriptorResourceKind::Sampler});
    const auto bloomWritten = set.write({2u,
        rhi::DescriptorType::SampledTexture,
        nextBloomView->id(),
        rhi::DescriptorResourceKind::TextureView});
    if (!inputWritten.success || !samplerWritten.success || !bloomWritten.success) {
        diagnostic = !inputWritten.success
            ? inputWritten.diagnostics
            : (!samplerWritten.success ? samplerWritten.diagnostics : bloomWritten.diagnostics);
        return false;
    }
    inputView = std::move(nextInputView);
    bloomView = std::move(nextBloomView);
    return true;
}

bool recordPostPass(rhi::GpuDevice& device,
    rhi::GpuTexture& input,
    rhi::GpuTexture& bloom,
    rhi::GpuTexture& output,
    rhi::RenderPipeline& pipeline,
    rhi::DescriptorSet& set,
    rhi::GpuSampler& sampler,
    const PostPushConstants& push,
    core::pmr::String& diagnostic)
{
    core::UniquePtr<rhi::GpuTextureView> inputView;
    core::UniquePtr<rhi::GpuTextureView> bloomView;
    if (!writePostDescriptors(
            device, set, input, bloom, sampler, inputView, bloomView, diagnostic)) {
        return false;
    }
    auto encoder = device.createCommandEncoder({"FeatureLab.PostPass"});
    if (!encoder) {
        diagnostic = "FEATURE-LAB-POST-ENCODER";
        return false;
    }
    if (input.state() != rhi::ResourceState::ShaderRead) {
        const auto transition =
            encoder->transition(input, input.state(), rhi::ResourceState::ShaderRead);
        if (!transition.success) {
            diagnostic = transition.diagnostics;
            return false;
        }
    }
    if (bloom.id() != input.id() && bloom.state() != rhi::ResourceState::ShaderRead) {
        const auto transition =
            encoder->transition(bloom, bloom.state(), rhi::ResourceState::ShaderRead);
        if (!transition.success) {
            diagnostic = transition.diagnostics;
            return false;
        }
    }
    const auto outputTransition =
        encoder->transition(output, output.state(), rhi::ResourceState::ColorAttachment);
    if (!outputTransition.success) {
        diagnostic = outputTransition.diagnostics;
        return false;
    }

    rhi::RenderPassDesc passDesc;
    passDesc.label = output.desc().label;
    passDesc.colorTargetName = output.desc().label;
    passDesc.colorAttachments.push_back({output.desc().label,
        output.id(),
        output.desc().format,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::Store});
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    auto pipelineResult = pass->setPipeline(pipeline);
    auto descriptorResult = pipelineResult.success ? pass->bindDescriptorSet(0u, set)
                                                   : rhi::CommandValidationResult{false, ""};
    if (!pipelineResult.success || !descriptorResult.success) {
        pass->end();
        diagnostic =
            pipelineResult.success ? descriptorResult.diagnostics : pipelineResult.diagnostics;
        return false;
    }
    const auto pushed = pass->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
        0u,
        sizeof(PostPushConstants),
        &push);
    const auto drawn =
        pushed.success ? pass->draw(3u, 1u, 0u, 0u) : rhi::CommandValidationResult{false, ""};
    const auto ended = pass->end();
    if (!pushed.success || !drawn.success || !ended.success) {
        diagnostic = !pushed.success ? pushed.diagnostics
                                     : (!drawn.success ? drawn.diagnostics : ended.diagnostics);
        return false;
    }
    auto commands = encoder->finish();
    if (!commands) {
        diagnostic = encoder->lastValidation().diagnostics;
        return false;
    }
    const auto submitted = device.queue().submit(*commands);
    if (!submitted.success) {
        diagnostic = submitted.diagnostics;
        return false;
    }
    device.waitIdle();
    return true;
}

core::UniquePtr<rhi::PipelineLayout> createPostLayout(
    rhi::GpuDevice& device, core::pmr::String& diagnostic)
{
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back({
        "FeatureLab.PostInput",
        {
            {0u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {1u, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment},
            {2u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
        },
    });
    layoutDesc.descriptorSets.push_back({0u, "FeatureLab.PostInput"});
    layoutDesc.pushConstantRanges.push_back(
        {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0u, sizeof(PostPushConstants)});
    auto layout = device.createPipelineLayout(layoutDesc);
    if (!layout)
        diagnostic = "FEATURE-LAB-POST-PIPELINE-LAYOUT";
    return layout;
}

PostPassPipeline createPostPipeline(rhi::GpuDevice& device,
    rhi::PipelineLayout& layout,
    const core::pmr::String& shader,
    const char* fragment,
    rhi::TextureFormat format,
    rhi::Extent2D extent,
    core::pmr::String& diagnostic)
{
    PostPassPipeline result;
    rhi::ShaderPackageDesc package;
    package.label = "FeatureLab.Post.";
    package.label += fragment;
    package.slangModulePath = shader;
    package.entrypoints.push_back(
        {shader, "vertexMain", rhi::ShaderStage::Vertex, rhi::ShaderSourceLanguage::Slang});
    package.entrypoints.push_back(
        {shader, fragment, rhi::ShaderStage::Fragment, rhi::ShaderSourceLanguage::Slang});
    package.artifacts.spirvPath = replaceShaderExtension(shader, ".spv");
    package.artifacts.mslPath = replaceShaderExtension(shader, ".metal");
    package.artifacts.wgslPath = replaceShaderExtension(shader, ".wgsl");
    package.artifacts.reflectionPath = replaceShaderExtension(shader, ".reflection.yaml");
    package.reflection.pushConstantByteCount = sizeof(PostPushConstants);
    package.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    package.reflection.pushConstantStages = "VS|FS";
    if (!renderer::hydrateShaderPackageReflectionFromArtifact(package)) {
        diagnostic = "FEATURE-LAB-POST-REFLECTION-HYDRATION";
        return result;
    }
    for (const auto& binding : layout.desc().descriptorLayouts.front().bindings) {
        package.reflection.descriptorBindings.push_back(
            rhi::makeShaderReflectionBinding(0u, binding));
    }
    const auto shaderValidation = rhi::validateShaderPackage(package);
    if (!shaderValidation.success) {
        diagnostic = shaderValidation.diagnostics;
        return result;
    }
    rhi::RenderPipelineDesc desc;
    desc.label = package.label;
    desc.layout = &layout;
    desc.shaderPackage = &package;
    desc.vertexShader = {
        shader, "vertexMain", rhi::ShaderStage::Vertex, rhi::ShaderSourceLanguage::Slang};
    desc.fragmentShader = {
        shader, fragment, rhi::ShaderStage::Fragment, rhi::ShaderSourceLanguage::Slang};
    desc.colorFormat = format;
    desc.hasViewport = true;
    desc.viewport = {0.0f,
        0.0f,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0f,
        1.0f};
    desc.hasScissor = true;
    desc.scissor = {0, 0, extent.width, extent.height};
    result.pipeline = device.createRenderPipeline(desc);
    if (!result.pipeline)
        diagnostic = "FEATURE-LAB-POST-PIPELINE-CREATE";
    return result;
}

PostProcessFixtureResult runPostFixtureOnDevice(rhi::GpuDevice& device, rhi::Extent2D extent)
{
    extent.width = std::clamp(extent.width, 8u, 64u);
    extent.height = std::clamp(extent.height, 8u, 64u);
    const auto format = rhi::TextureFormat::Bgra8Unorm;
    auto input = createInputTexture(device, "FeatureLab.PostInput", extent, format);
    auto bloomHigh = createOutputTexture(device, "FeatureLab.BloomHigh", extent, format);
    auto bloomBlur = createOutputTexture(device, "FeatureLab.BloomBlur", extent, format);
    auto composite = createOutputTexture(device, "FeatureLab.BloomComposite", extent, format);
    auto tonemap = createOutputTexture(device, "FeatureLab.Tonemap", extent, format);
    auto grade = createOutputTexture(device, "FeatureLab.Grade", extent, format);
    auto finalOutput = createOutputTexture(device, "FeatureLab.FinalLdr", extent, format);
    if (!input || !bloomHigh || !bloomBlur || !composite || !tonemap || !grade || !finalOutput) {
        return fail("FEATURE-LAB-POST-TEXTURE-CREATE");
    }
    core::pmr::String diagnostic;
    if (!uploadDeterministicFixture(device, *input, FixturePattern::ColorChecker, diagnostic)) {
        return fail("FEATURE-LAB-POST-INPUT-UPLOAD", diagnostic);
    }
    auto layout = createPostLayout(device, diagnostic);
    auto sampler = device.createSampler({"FeatureLab.PostSampler",
        rhi::SamplerFilter::Linear,
        rhi::SamplerFilter::Linear,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge});
    auto arena = device.createDescriptorArena({"FeatureLab.PostArena", 1u, 3u});
    if (!layout || !sampler || !arena)
        return fail("FEATURE-LAB-POST-RESOURCES", diagnostic);
    auto set = arena->allocateDescriptorSet(
        {"FeatureLab.PostInput", layout->desc().descriptorLayouts.front()});
    if (!set)
        return fail("FEATURE-LAB-POST-DESCRIPTOR-SET");
    const auto shader = shaderPath("wocc_post.slang");
    const auto create = [&](const char* entry, rhi::TextureFormat outputFormat) {
        return createPostPipeline(device, *layout, shader, entry, outputFormat, extent, diagnostic);
    };
    auto bloomDownsample = create("bloomDownsampleFragmentMain", format);
    auto bloomBlurPass = create("bloomBlurFragmentMain", format);
    auto bloomComposite = create("bloomCompositeFragmentMain", format);
    auto toneMapping = create("toneMappingFragmentMain", format);
    auto gradePass = create("gradeFragmentMain", format);
    if (!bloomDownsample.pipeline || !bloomBlurPass.pipeline || !bloomComposite.pipeline
        || !toneMapping.pipeline || !gradePass.pipeline) {
        return fail("FEATURE-LAB-POST-PIPELINE", diagnostic);
    }
    PostPushConstants push{};
    push.toneMappingExposure[0] = 1.0f;
    push.bloom[0] = 0.45f;
    push.bloom[1] = 0.8f;
    push.bloom[2] = 1.0f / static_cast<float>(extent.width);
    push.gradeGain[0] = push.gradeGain[1] = push.gradeGain[2] = 1.0f;
    push.gradeGammaSaturation[0] = push.gradeGammaSaturation[1] = push.gradeGammaSaturation[2] =
        1.0f;
    push.gradeGammaSaturation[3] = 1.0f;
    push.vignetteGrain[0] = 0.1f;
    bool recorded = recordPostPass(device,
        *input,
        *input,
        *bloomHigh,
        *bloomDownsample.pipeline,
        *set,
        *sampler,
        push,
        diagnostic);
    recorded = recorded
        && recordPostPass(device,
            *bloomHigh,
            *bloomHigh,
            *bloomBlur,
            *bloomBlurPass.pipeline,
            *set,
            *sampler,
            push,
            diagnostic);
    recorded = recorded
        && recordPostPass(device,
            *input,
            *bloomBlur,
            *composite,
            *bloomComposite.pipeline,
            *set,
            *sampler,
            push,
            diagnostic);
    recorded = recorded
        && recordPostPass(device,
            *composite,
            *composite,
            *tonemap,
            *toneMapping.pipeline,
            *set,
            *sampler,
            push,
            diagnostic);
    recorded = recorded
        && recordPostPass(device,
            *tonemap,
            *tonemap,
            *grade,
            *gradePass.pipeline,
            *set,
            *sampler,
            push,
            diagnostic);
    if (!recorded)
        return fail("FEATURE-LAB-POST-RECORD", diagnostic);

    renderer::FxaaRenderer fxaa(device);
    const auto fxaaCreated = fxaa.create({shaderPath("fxaa.slang"), format, extent});
    if (!fxaaCreated.success)
        return fail("FEATURE-LAB-POST-FXAA-CREATE", fxaaCreated.diagnostics);
    if (!prepareOutputTexture(device, *finalOutput, diagnostic)) {
        return fail("FEATURE-LAB-POST-FXAA-OUTPUT", diagnostic);
    }
    const auto fxaaResult = fxaa.drawFrame(frameContext(*finalOutput, extent, 0u), *grade);
    if (!fxaaResult.success)
        return fail("FEATURE-LAB-POST-FXAA-RECORD", fxaaResult.diagnostics);

    PostProcessFixtureResult result;
    result.success = true;
    result.passTrace = {"Post.Bloom.Downsample",
        "Post.Bloom.Blur",
        "Post.Bloom.Composite",
        "Post.Tonemap.ACES",
        "Post.Grade",
        "Post.FXAA"};
    const auto readMetric = [&](rhi::GpuTexture& texture, const char* label) {
        rhi::TextureReadbackSample sample;
        core::pmr::String readbackDiagnostic;
        const auto status = readbackOutput(device, texture, label, sample, readbackDiagnostic);
        if (status == FixtureReadbackStatus::Failed) {
            diagnostic = readbackDiagnostic;
            return FeatureLabAttachmentMetric{label, "finite_fraction", 0.0f, false};
        }
        if (status == FixtureReadbackStatus::Unavailable) {
            return FeatureLabAttachmentMetric{label, "finite_fraction", 0.0f, false};
        }
        const auto fraction = sample.sampleCount == 0u
            ? 0.0f
            : static_cast<float>(sample.finiteCount) / sample.sampleCount;
        return FeatureLabAttachmentMetric{label, "finite_fraction", fraction, true};
    };
    result.attachmentMetrics.push_back(readMetric(*bloomHigh, "bloom"));
    result.attachmentMetrics.push_back(readMetric(*tonemap, "tonemap"));
    result.attachmentMetrics.push_back(readMetric(*grade, "grade"));
    result.attachmentMetrics.push_back(readMetric(*finalOutput, "final-ldr"));
    const bool nullBackend = device.backend() == rhi::Backend::Null;
    for (const auto& metric : result.attachmentMetrics) {
        // Null validates the graph, descriptors, and pass lifecycle but does
        // not expose pixel readback.  Keep its metrics explicitly unavailable
        // while allowing the contract run to pass, matching the other Lab
        // fixtures.  Real backends must provide every requested measurement.
        result.success = result.success && (metric.available || nullBackend);
    }
    result.submittedFrameCount = device.queue().submitCount();
    result.gpuMemoryBytes = device.memoryStats().allocatedBytes;
    if (!result.success && diagnostic.empty())
        diagnostic = "FEATURE-LAB-POST-READBACK";
    result.diagnostic = diagnostic;
    device.waitIdle();
    return result;
}

} // namespace

PostProcessFixtureResult runPostFeatureFixture(rhi::Extent2D extent)
{
    auto device = rhi::createNullDevice();
    if (!device)
        return fail("FEATURE-LAB-POST-NULL-DEVICE");
    return runPostFixtureOnDevice(*device, extent);
}

PostProcessFixtureResult runPostFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent)
{
    return runPostFixtureOnDevice(device, extent);
}

namespace {

using SsrPushConstants = arp::shader::gen::SsrPushConstants;

PostProcessFixtureResult runSsrFixtureOnDevice(rhi::GpuDevice& device, rhi::Extent2D extent)
{
    extent.width = std::clamp(extent.width, 8u, 64u);
    extent.height = std::clamp(extent.height, 8u, 64u);
    // Exercise the production engine resource/pipeline owner first. The
    // deterministic trace/composite fixture below supplies the synthetic
    // attachments and readback oracle used for pixel acceptance.
    renderer::SsrRenderer productionRenderer(device);
    const auto productionCreated =
        productionRenderer.create(shaderPath("ssr.slang"), extent.width, extent.height);
    if (!productionCreated.success || !productionRenderer.reflectionTexture()) {
        return fail("FEATURE-LAB-SSR-PRODUCTION-CREATE", productionCreated.diagnostics);
    }
    const auto colorFormat = rhi::TextureFormat::Bgra8Unorm;
    auto sceneColor = createInputTexture(device, "FeatureLab.SsrSceneColor", extent, colorFormat);
    auto sceneDepth = createDepthFixtureTexture(device, "FeatureLab.SsrSceneDepth", extent);
    rhi::TextureDesc reflectionDesc;
    reflectionDesc.label = "FeatureLab.SsrReflection";
    reflectionDesc.extent = {extent.width, extent.height, 1u};
    reflectionDesc.format = rhi::TextureFormat::Rgba16Float;
    reflectionDesc.usage =
        rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
    reflectionDesc.memory = rhi::MemoryUsage::GpuOnly;
    auto reflection = device.createTexture(reflectionDesc);
    auto composite = createOutputTexture(
        device, "FeatureLab.SsrComposite", extent, rhi::TextureFormat::Rgba16Float);
    rhi::TextureDesc dummyDesc = reflectionDesc;
    dummyDesc.label = "FeatureLab.SsrDummyStorage";
    auto dummyStorage = device.createTexture(dummyDesc);
    if (!sceneColor || !sceneDepth || !reflection || !composite || !dummyStorage) {
        return fail("FEATURE-LAB-SSR-TEXTURE-CREATE");
    }
    core::pmr::String diagnostic;
    if (!uploadDeterministicFixture(device, *sceneColor, FixturePattern::ColorChecker, diagnostic)
        || !rasterizeDepthStepFixture(device, *sceneDepth, extent, diagnostic)) {
        return fail("FEATURE-LAB-SSR-INPUT-UPLOAD", diagnostic);
    }

    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorLayouts.push_back({
        "FeatureLab.SsrInput",
        {
            {0u,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Compute | rhi::ShaderStage::Fragment},
            {1u,
                rhi::DescriptorType::SampledTexture,
                rhi::ShaderStage::Compute,
                1u,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                true},
            {2u,
                rhi::DescriptorType::Sampler,
                rhi::ShaderStage::Compute | rhi::ShaderStage::Fragment},
            {3u,
                rhi::DescriptorType::StorageTexture,
                rhi::ShaderStage::Compute,
                1u,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                false,
                rhi::TextureFormat::Rgba16Float},
            {4u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
        },
    });
    layoutDesc.descriptorSets.push_back({0u, "FeatureLab.SsrInput"});
    layoutDesc.pushConstantRanges.push_back(
        {rhi::ShaderStage::Compute | rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
            0u,
            sizeof(SsrPushConstants)});
    auto layout = device.createPipelineLayout(layoutDesc);
    auto sampler = device.createSampler({"FeatureLab.SsrSampler",
        rhi::SamplerFilter::Nearest,
        rhi::SamplerFilter::Nearest,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge});
    auto arena = device.createDescriptorArena({"FeatureLab.SsrArena", 2u, 10u});
    if (!layout || !sampler || !arena)
        return fail("FEATURE-LAB-SSR-RESOURCES", diagnostic);
    auto traceSet = arena->allocateDescriptorSet(
        {"FeatureLab.SsrTraceSet", layoutDesc.descriptorLayouts.front()});
    auto compositeSet = arena->allocateDescriptorSet(
        {"FeatureLab.SsrCompositeSet", layoutDesc.descriptorLayouts.front()});
    if (!traceSet || !compositeSet)
        return fail("FEATURE-LAB-SSR-DESCRIPTOR-SET");

    const auto shader = shaderPath("ssr.slang");
    rhi::ShaderPackageDesc package;
    package.label = "FeatureLab.SsrShaderPackage";
    package.slangModulePath = shader;
    package.entrypoints = {
        {shader,
            "ssrTraceComputeMain",
            rhi::ShaderStage::Compute,
            rhi::ShaderSourceLanguage::Slang},
        {shader, "vertexMain", rhi::ShaderStage::Vertex, rhi::ShaderSourceLanguage::Slang},
        {shader,
            "ssrCompositeFragmentMain",
            rhi::ShaderStage::Fragment,
            rhi::ShaderSourceLanguage::Slang},
    };
    package.artifacts.spirvPath = replaceShaderExtension(shader, ".spv");
    package.artifacts.mslPath = replaceShaderExtension(shader, ".metal");
    package.artifacts.wgslPath = replaceShaderExtension(shader, ".wgsl");
    package.artifacts.reflectionPath = replaceShaderExtension(shader, ".reflection.yaml");
    package.reflection.pushConstantByteCount = sizeof(SsrPushConstants);
    package.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    package.reflection.pushConstantStages = "CS|FS|VS";
    if (!renderer::hydrateShaderPackageReflectionFromArtifact(package)) {
        return fail("FEATURE-LAB-SSR-REFLECTION");
    }
    for (const auto& binding : layoutDesc.descriptorLayouts.front().bindings) {
        package.reflection.descriptorBindings.push_back(
            rhi::makeShaderReflectionBinding(0u, binding));
    }
    const auto shaderValidation = rhi::validateShaderPackage(package);
    if (!shaderValidation.success)
        return fail("FEATURE-LAB-SSR-SHADER", shaderValidation.diagnostics);

    rhi::ComputePipelineDesc computeDesc;
    computeDesc.label = "FeatureLab.SsrTrace";
    computeDesc.layout = layout.get();
    computeDesc.shaderPackage = &package;
    computeDesc.computeShader = {
        shader, "ssrTraceComputeMain", rhi::ShaderStage::Compute, rhi::ShaderSourceLanguage::Slang};
    auto tracePipeline = device.createComputePipeline(computeDesc);
    rhi::RenderPipelineDesc compositeDesc;
    compositeDesc.label = "FeatureLab.SsrComposite";
    compositeDesc.layout = layout.get();
    compositeDesc.shaderPackage = &package;
    compositeDesc.vertexShader = {
        shader, "vertexMain", rhi::ShaderStage::Vertex, rhi::ShaderSourceLanguage::Slang};
    compositeDesc.fragmentShader = {shader,
        "ssrCompositeFragmentMain",
        rhi::ShaderStage::Fragment,
        rhi::ShaderSourceLanguage::Slang};
    compositeDesc.colorFormat = rhi::TextureFormat::Rgba16Float;
    compositeDesc.hasViewport = true;
    compositeDesc.viewport = {0.0f,
        0.0f,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0f,
        1.0f};
    compositeDesc.hasScissor = true;
    compositeDesc.scissor = {0, 0, extent.width, extent.height};
    auto compositePipeline = device.createRenderPipeline(compositeDesc);
    if (!tracePipeline || !compositePipeline)
        return fail("FEATURE-LAB-SSR-PIPELINE");

    auto colorView = device.createTextureView(
        *sceneColor, {"FeatureLab.SsrColorView", colorFormat, rhi::TextureUsage::Sampled});
    auto depthView = device.createTextureView(*sceneDepth,
        {"FeatureLab.SsrDepthView",
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::Sampled,
            rhi::TextureViewDimension::Texture2D,
            0u,
            1u,
            0u,
            1u,
            rhi::TextureAspect::Depth});
    auto reflectionStorageView = device.createTextureView(*reflection,
        {"FeatureLab.SsrReflectionStorage",
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::Storage});
    auto reflectionSampledView = device.createTextureView(*reflection,
        {"FeatureLab.SsrReflectionSampled",
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::Sampled});
    auto dummyStorageView = device.createTextureView(*dummyStorage,
        {"FeatureLab.SsrDummyStorageView",
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::Storage});
    if (!colorView || !depthView || !reflectionStorageView || !reflectionSampledView
        || !dummyStorageView)
        return fail("FEATURE-LAB-SSR-VIEWS");
    const auto traceWrites = std::array<rhi::DescriptorBindingWrite, 5>{
        rhi::DescriptorBindingWrite{0u,
            rhi::DescriptorType::SampledTexture,
            colorView->id(),
            rhi::DescriptorResourceKind::TextureView},
        rhi::DescriptorBindingWrite{1u,
            rhi::DescriptorType::SampledTexture,
            depthView->id(),
            rhi::DescriptorResourceKind::TextureView},
        rhi::DescriptorBindingWrite{
            2u, rhi::DescriptorType::Sampler, sampler->id(), rhi::DescriptorResourceKind::Sampler},
        rhi::DescriptorBindingWrite{3u,
            rhi::DescriptorType::StorageTexture,
            reflectionStorageView->id(),
            rhi::DescriptorResourceKind::TextureView},
        rhi::DescriptorBindingWrite{4u,
            rhi::DescriptorType::SampledTexture,
            reflectionSampledView->id(),
            rhi::DescriptorResourceKind::TextureView},
    };
    for (const auto& write : traceWrites) {
        const auto written = traceSet->write(write);
        if (!written.success)
            return fail("FEATURE-LAB-SSR-TRACE-DESCRIPTOR", written.diagnostics);
    }
    const auto compositeWrites = std::array<rhi::DescriptorBindingWrite, 5>{
        traceWrites[0],
        traceWrites[1],
        traceWrites[2],
        rhi::DescriptorBindingWrite{3u,
            rhi::DescriptorType::StorageTexture,
            dummyStorageView->id(),
            rhi::DescriptorResourceKind::TextureView},
        traceWrites[4],
    };
    for (const auto& write : compositeWrites) {
        const auto written = compositeSet->write(write);
        if (!written.success)
            return fail("FEATURE-LAB-SSR-COMPOSITE-DESCRIPTOR", written.diagnostics);
    }

    SsrPushConstants push{};
    push.projectionParams[0] = 0.1f;
    push.projectionParams[1] = 100.0f;
    push.projectionParams[2] = 0.05f;
    push.projectionParams[3] = 16.0f;
    push.ssrParams[0] = 10.0f;
    push.ssrParams[1] = 1.0f;
    push.ssrParams[2] = 1.0f;
    push.ssrParams[3] = 0.2f;
    // Exercise the production water-only SSR contract with a deterministic
    // horizontal plane in front of the depth-step fixture.
    push.invViewProjectionY[2] = 1.0f;
    push.invViewProjectionW[3] = 1.0f;
    push.waterParams[0] = 0.25f;
    push.waterParams[1] = 1.0f;
    push.waterParams[2] = 1.0f;
    push.waterParams[3] = -1.0f;
    push.inputUvScaleBias[0] = 1.0f;
    push.surfaceToKeyLightDirectionView[0] = 0.75f;
    push.surfaceToKeyLightDirectionView[1] = 0.15f;
    push.surfaceToKeyLightDirectionView[2] = 0.65f;
    auto encoder = device.createCommandEncoder({"FeatureLab.SsrTrace"});
    if (!encoder)
        return fail("FEATURE-LAB-SSR-TRACE-ENCODER");
    auto reflectionWrite =
        encoder->transition(*reflection, reflection->state(), rhi::ResourceState::ShaderWrite);
    if (!reflectionWrite.success)
        return fail("FEATURE-LAB-SSR-TRACE-TRANSITION", reflectionWrite.diagnostics);
    auto compute = encoder->beginComputePass({"FeatureLab.SsrTrace"});
    if (!compute)
        return fail("FEATURE-LAB-SSR-TRACE-PASS", encoder->lastValidation().diagnostics);
    auto pipelineBound = compute->setPipeline(*tracePipeline);
    auto descriptorsBound = pipelineBound.success ? compute->bindDescriptorSet(0u, *traceSet)
                                                  : rhi::CommandValidationResult{false, ""};
    auto constantsBound = descriptorsBound.success
        ? compute->pushConstants(rhi::ShaderStage::Compute, 0u, sizeof(SsrPushConstants), &push)
        : rhi::CommandValidationResult{false, ""};
    auto dispatched = constantsBound.success
        ? compute->dispatch((extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u)
        : rhi::CommandValidationResult{false, ""};
    auto computeEnded = compute->end();
    if (!pipelineBound.success || !descriptorsBound.success || !constantsBound.success
        || !dispatched.success || !computeEnded.success) {
        return fail("FEATURE-LAB-SSR-TRACE-RECORD",
            !pipelineBound.success
                ? pipelineBound.diagnostics
                : (!descriptorsBound.success
                          ? descriptorsBound.diagnostics
                          : (!constantsBound.success
                                    ? constantsBound.diagnostics
                                    : (!dispatched.success ? dispatched.diagnostics
                                                           : computeEnded.diagnostics))));
    }
    auto commands = encoder->finish();
    if (!commands)
        return fail("FEATURE-LAB-SSR-TRACE-FINISH", encoder->lastValidation().diagnostics);
    auto submitted = device.queue().submit(*commands);
    if (!submitted.success)
        return fail("FEATURE-LAB-SSR-TRACE-SUBMIT", submitted.diagnostics);
    device.waitIdle();

    auto compositeEncoder = device.createCommandEncoder({"FeatureLab.SsrComposite"});
    if (!compositeEncoder)
        return fail("FEATURE-LAB-SSR-COMPOSITE-ENCODER");
    auto reflectionRead = compositeEncoder->transition(
        *reflection, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);
    if (!reflectionRead.success)
        return fail("FEATURE-LAB-SSR-COMPOSITE-REFLECTION", reflectionRead.diagnostics);
    auto outputTransition = compositeEncoder->transition(
        *composite, composite->state(), rhi::ResourceState::ColorAttachment);
    if (!outputTransition.success)
        return fail("FEATURE-LAB-SSR-COMPOSITE-OUTPUT", outputTransition.diagnostics);
    rhi::RenderPassDesc passDesc;
    passDesc.label = "FeatureLab.SsrComposite";
    passDesc.colorTargetName = composite->desc().label;
    passDesc.colorAttachments.push_back({"SsrCompositeColor",
        composite->id(),
        composite->desc().format,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::Store});
    auto renderPass = compositeEncoder->beginRenderPass(passDesc);
    if (!renderPass)
        return fail(
            "FEATURE-LAB-SSR-COMPOSITE-PASS", compositeEncoder->lastValidation().diagnostics);
    auto compositeBound = renderPass->setPipeline(*compositePipeline);
    auto compositeDescriptors = compositeBound.success
        ? renderPass->bindDescriptorSet(0u, *compositeSet)
        : rhi::CommandValidationResult{false, ""};
    auto compositeConstants = compositeDescriptors.success
        ? renderPass->pushConstants(rhi::ShaderStage::Fragment, 0u, sizeof(SsrPushConstants), &push)
        : rhi::CommandValidationResult{false, ""};
    auto compositeDraw = compositeConstants.success ? renderPass->draw(3u, 1u, 0u, 0u)
                                                    : rhi::CommandValidationResult{false, ""};
    auto renderEnded = renderPass->end();
    if (!compositeBound.success || !compositeDescriptors.success || !compositeConstants.success
        || !compositeDraw.success || !renderEnded.success) {
        return fail("FEATURE-LAB-SSR-COMPOSITE-RECORD",
            !compositeBound.success
                ? compositeBound.diagnostics
                : (!compositeDescriptors.success
                          ? compositeDescriptors.diagnostics
                          : (!compositeConstants.success
                                    ? compositeConstants.diagnostics
                                    : (!compositeDraw.success ? compositeDraw.diagnostics
                                                              : renderEnded.diagnostics))));
    }
    auto compositeCommands = compositeEncoder->finish();
    if (!compositeCommands)
        return fail(
            "FEATURE-LAB-SSR-COMPOSITE-FINISH", compositeEncoder->lastValidation().diagnostics);
    submitted = device.queue().submit(*compositeCommands);
    if (!submitted.success)
        return fail("FEATURE-LAB-SSR-COMPOSITE-SUBMIT", submitted.diagnostics);
    device.waitIdle();

    PostProcessFixtureResult result;
    result.success = true;
    result.passTrace = {"SSR.TraceCompute", "SSR.Composite"};
    rhi::TextureReadbackSample reflectionSample;
    rhi::TextureReadbackSample compositeSample;
    const auto reflectionStatus =
        readbackOutput(device, *reflection, "FeatureLab.SsrHitMask", reflectionSample, diagnostic);
    const auto compositeStatus =
        readbackOutput(device, *composite, "FeatureLab.SsrColor", compositeSample, diagnostic);
    const bool nullBackend = device.backend() == rhi::Backend::Null;
    const bool reflectionAvailable = reflectionStatus == FixtureReadbackStatus::Available;
    const bool compositeAvailable = compositeStatus == FixtureReadbackStatus::Available;
    result.attachmentMetrics.push_back({"ssr-hit-mask",
        "known_plane_hit_fraction",
        reflectionAvailable && reflectionSample.maxValue > 0.0001 ? 1.0f : 0.0f,
        reflectionAvailable});
    result.attachmentMetrics.push_back({"ssr-color",
        "finite_fraction",
        compositeAvailable && compositeSample.sampleCount > 0u
            ? static_cast<float>(compositeSample.finiteCount) / compositeSample.sampleCount
            : 0.0f,
        compositeAvailable});
    result.success = nullBackend
        || (reflectionAvailable && compositeAvailable && result.attachmentMetrics[0].value >= 0.5f
            && result.attachmentMetrics[1].value >= 1.0f);
    result.submittedFrameCount = device.queue().submitCount();
    result.gpuMemoryBytes = device.memoryStats().allocatedBytes;
    if (!result.success && diagnostic.empty())
        diagnostic = "FEATURE-LAB-SSR-READBACK-ACCEPTANCE";
    result.diagnostic = diagnostic;
    return result;
}

} // namespace

PostProcessFixtureResult runSsrFeatureFixture(rhi::Extent2D extent)
{
    auto device = rhi::createNullDevice();
    if (!device)
        return fail("FEATURE-LAB-SSR-NULL-DEVICE");
    return runSsrFixtureOnDevice(*device, extent);
}

PostProcessFixtureResult runSsrFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent)
{
    return runSsrFixtureOnDevice(device, extent);
}

PostProcessFixtureResult runDdgiFeatureFixture(rhi::Extent2D extent)
{
    auto device = rhi::createNullDevice();
    if (!device)
        return fail("FEATURE-LAB-DDGI-NULL-DEVICE");
    return runDdgiFeatureFixtureFrames(*device, extent, 1u);
}

PostProcessFixtureResult runDdgiFeatureFixtureFrames(rhi::Extent2D extent, std::uint32_t frameCount)
{
    auto device = rhi::createNullDevice();
    if (!device)
        return fail("FEATURE-LAB-DDGI-NULL-DEVICE");
    return runDdgiFeatureFixtureFrames(*device, extent, frameCount);
}

PostProcessFixtureResult runDdgiFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent)
{
    return runDdgiFeatureFixtureFrames(device, extent, 1u);
}

PostProcessFixtureResult runDdgiFeatureFixtureFrames(rhi::GpuDevice& device,
    rhi::Extent2D extent,
    std::uint32_t frameCount,
    rhi::Swapchain* presentationSwapchain,
    DdgiFixtureFault recordingFault)
{
    // DDGI L05 accepts production-sized extents so the Vulkan timing smoke can
    // measure the configured performance resolution instead of a tiny fixture.
    extent.width = std::clamp(extent.width, 8u, 4096u);
    extent.height = std::clamp(extent.height, 8u, 4096u);
    renderer::DdgiTerrainSurfaceInput terrainInput;
    terrainInput.surface = core::makeShared<DdgiFixtureTerrainSurface>();
    terrainInput.samplesX = 64u;
    terrainInput.samplesZ = 64u;
    renderer::DdgiRenderer renderer(device);
    const auto created = renderer.create(shaderPath("ddgi.slang"),
        terrainInput,
        2u,
        2u,
        2u,
        std::max(1u, extent.width / 2u),
        std::max(1u, extent.height / 2u));
    if (!created.success)
        return fail("FEATURE-LAB-DDGI-CREATE", created.diagnostics);

    renderer::DdgiSceneBvhTriangle triangle{};
    triangle.v0[0] = -1.0f;
    triangle.v0[1] = 0.0f;
    triangle.v0[2] = 0.0f;
    triangle.v1[0] = 1.0f;
    triangle.v1[1] = 0.0f;
    triangle.v1[2] = 0.0f;
    triangle.v2[0] = 0.0f;
    triangle.v2[1] = 1.0f;
    triangle.v2[2] = 0.0f;
    triangle.emissiveRadiance[0] = 0.4f;
    triangle.emissiveRadiance[1] = 0.1f;
    triangle.emissiveRadiance[2] = 0.05f;
    renderer::DdgiSceneBvhInput scene{&triangle, 1u};
    scene.staticGeometryRevision = 1u;
    scene.dynamicGeometryRevision = 1u;
    scene.staticTransformRevision = 1u;
    const auto bvh = renderer.updateSceneBvh(scene);
    if (!bvh.success)
        return fail("FEATURE-LAB-DDGI-EMISSIVE-CONTRACT", bvh.diagnostic);
    scene.staticTransformRevision = 2u;
    const auto staticTransformBvh = renderer.updateSceneBvh(scene);
    if (!staticTransformBvh.success || !staticTransformBvh.refitted || staticTransformBvh.rebuilt) {
        return fail("FEATURE-LAB-DDGI-STATIC-TRANSFORM-REFIT");
    }

    // Exercise the explicit split path with one local-space static BLAS. A
    // transform-only revision must update binding 27 without touching the
    // static node/leaf buffers.
    renderer::DdgiSceneBvhInstance splitInstance{};
    splitInstance.worldFromLocal[0] = 1.0f;
    splitInstance.worldFromLocal[5] = 1.0f;
    splitInstance.worldFromLocal[10] = 1.0f;
    splitInstance.worldFromLocal[15] = 1.0f;
    splitInstance.localFromWorld[0] = 1.0f;
    splitInstance.localFromWorld[5] = 1.0f;
    splitInstance.localFromWorld[10] = 1.0f;
    splitInstance.localFromWorld[15] = 1.0f;
    renderer::DdgiSceneBvhTriangle dynamicProxy{};
    dynamicProxy.materialIdx = renderer::kDdgiDynamicProxyMaterial;
    dynamicProxy.v0[0] = -0.25f;
    dynamicProxy.v0[1] = -0.25f;
    dynamicProxy.v0[2] = -0.25f;
    dynamicProxy.v1[0] = 0.25f;
    dynamicProxy.v1[1] = 0.25f;
    dynamicProxy.v1[2] = 0.25f;
    renderer::DdgiSceneBvhInput splitScene{};
    splitScene.staticTriangles = &triangle;
    splitScene.staticTriangleCount = 1u;
    splitScene.staticGeometryRevision = 11u;
    splitScene.staticTransformRevision = 1u;
    splitScene.dynamicTriangles = &dynamicProxy;
    splitScene.dynamicTriangleCount = 1u;
    splitScene.dynamicGeometryRevision = 1u;
    splitScene.instances = &splitInstance;
    splitScene.instanceCount = 1u;
    const auto splitBuilt = renderer.updateSceneBvh(splitScene);
    if (!splitBuilt.success || !splitBuilt.rebuilt || splitBuilt.staticTriangleCount != 1u
        || splitBuilt.instanceCount != 1u || splitBuilt.dynamicProxyCount != 1u) {
        return fail("FEATURE-LAB-DDGI-SPLIT-BLAS-BUILD");
    }
    splitInstance.worldFromLocal[12] = 1.0f;
    splitInstance.localFromWorld[12] = -1.0f;
    splitScene.staticTransformRevision = 2u;
    const auto splitTransform = renderer.updateSceneBvh(splitScene);
    if (!splitTransform.success || splitTransform.rebuilt || splitTransform.refitted
        || splitTransform.staticNodeCount != splitBuilt.staticNodeCount
        || splitTransform.staticTriangleCount != splitBuilt.staticTriangleCount) {
        return fail("FEATURE-LAB-DDGI-SPLIT-INSTANCE-UPDATE");
    }
    dynamicProxy.v1[0] = 0.5f;
    splitScene.dynamicGeometryRevision = 2u;
    const auto dynamicRefit = renderer.updateSceneBvh(splitScene);
    if (!dynamicRefit.success || dynamicRefit.rebuilt || !dynamicRefit.refitted
        || dynamicRefit.staticNodeCount != splitBuilt.staticNodeCount
        || dynamicRefit.staticTriangleCount != splitBuilt.staticTriangleCount) {
        return fail("FEATURE-LAB-DDGI-SPLIT-DYNAMIC-REFIT");
    }
    const bool resourcesReady = renderer.probeIrradiance(0) && renderer.probeIrradiance(1)
        && renderer.probeDepthMoments(0) && renderer.probeDepthMoments(1) && renderer.giOutput(0)
        && renderer.giOutput(1);
    renderer::DdgiPushConstants push{};
    push.invViewProjection[0] = 1.0f;
    push.invViewProjection[5] = 1.0f;
    push.invViewProjection[10] = 1.0f;
    push.invViewProjection[15] = 1.0f;
    push.probeGridOriginHysteresis[0] = -1.0f;
    push.probeGridOriginHysteresis[1] = -1.0f;
    push.probeGridOriginHysteresis[2] = -1.0f;
    push.probeGridOriginHysteresis[3] = 0.0f;
    push.probeGridSpacingStrength[0] = 1.0f;
    push.probeGridSpacingStrength[1] = 1.0f;
    push.probeGridSpacingStrength[2] = 1.0f;
    push.probeGridSpacingStrength[3] = 1.0f;
    push.probeCountRayCount[0] = 2.0f;
    push.probeCountRayCount[1] = 2.0f;
    push.probeCountRayCount[2] = 2.0f;
    push.probeCountRayCount[3] = 8.0f;
    push.probeTextureSizeBounceMaxRay[0] = 16.0f;
    push.probeTextureSizeBounceMaxRay[1] = 32.0f;
    push.probeTextureSizeBounceMaxRay[2] = 0.5f;
    push.probeTextureSizeBounceMaxRay[3] = 8.0f;
    push.sunDirectionSkyAmbient[1] = 1.0f;
    push.sunDirectionSkyAmbient[3] = 0.2f;
    push.updateParams[0] = 8.0f;
    push.updateParams[1] = 0.97f;
    push.updateParams[2] = 0.75f;
    push.updateParams[3] = 0.0f;
    const auto bvhParameters = dynamicRefit.shaderParameters();
    std::copy(bvhParameters.begin(), bvhParameters.end(), push.sceneBvhParams);
    // Establish history with real submitted DDGI work, then recreate with a
    // different output extent and
    // require the new renderer state to start with invalid history rather
    // than sampling the old textures.
    bool lifecycleRecreateValid = false;
    bool lifecycleDestroyRecreateValid = false;
    {
        renderer::DdgiRenderer lifecycleRenderer(device);
        const auto lifecycleCreated = lifecycleRenderer.create(shaderPath("ddgi.slang"),
            terrainInput,
            2u,
            2u,
            2u,
            std::max(1u, extent.width / 2u),
            std::max(1u, extent.height / 2u));
        if (!lifecycleCreated.success)
            return fail("FEATURE-LAB-DDGI-LIFECYCLE-CREATE", lifecycleCreated.diagnostics);
        if (!establishDdgiHistory(device, lifecycleRenderer, extent, push))
            return fail("FEATURE-LAB-DDGI-LIFECYCLE-HISTORY");
        const bool hadHistory = lifecycleRenderer.hasValidGiHistory();
        const auto lifecycleResized = lifecycleRenderer.create(shaderPath("ddgi.slang"),
            terrainInput,
            2u,
            2u,
            2u,
            std::max(1u, extent.width / 4u),
            std::max(1u, extent.height / 4u));
        lifecycleRecreateValid =
            hadHistory && lifecycleResized.success && !lifecycleRenderer.hasValidGiHistory();

        renderer::DdgiRenderer recoveryRenderer(device);
        const auto recoveryCreated = recoveryRenderer.create(shaderPath("ddgi.slang"),
            terrainInput,
            2u,
            2u,
            2u,
            std::max(1u, extent.width / 2u),
            std::max(1u, extent.height / 2u));
        if (recoveryCreated.success) {
            if (!establishDdgiHistory(device, recoveryRenderer, extent, push))
                return fail("FEATURE-LAB-DDGI-RECOVERY-HISTORY");
            const bool recoveryHadHistory = recoveryRenderer.hasValidGiHistory();
            recoveryRenderer.destroy();
            const auto recovered = recoveryRenderer.create(shaderPath("ddgi.slang"),
                terrainInput,
                2u,
                2u,
                2u,
                std::max(1u, extent.width / 2u),
                std::max(1u, extent.height / 2u));
            lifecycleDestroyRecreateValid = recoveryHadHistory && recovered.success
                && !recoveryRenderer.hasValidGiHistory() && recoveryRenderer.probeIrradiance(0)
                && recoveryRenderer.probeIrradiance(1) && recoveryRenderer.giOutput(0)
                && recoveryRenderer.giOutput(1);
        }
    }
    if (!lifecycleRecreateValid)
        return fail("FEATURE-LAB-DDGI-LIFECYCLE-RECREATE");

    const renderer::DdgiLocalLightDesc localLight{
        {0.0f, 1.5f, 0.5f},
        6.0f,
        {1.0f, 0.7f, 0.4f},
        2.0f,
    };
    const auto localLightSet = renderer.setLocalLights(&localLight, 1u);
    if (!localLightSet.success)
        return fail("FEATURE-LAB-DDGI-LOCAL-LIGHT-CONTRACT", localLightSet.diagnostics);
    // Optional fixed-value GPU/CPU parity capture. The mode is deliberately
    // opt-in so normal L05 captures retain the production resolve output.
    std::uint32_t parityMode = 0u;
    if (gDdgiParityModeOverride >= 7u && gDdgiParityModeOverride <= 10u) {
        parityMode = gDdgiParityModeOverride;
    } else if (const char* parityEnv = std::getenv("ARP_DDGI_PARITY_MODE")) {
        char* end = nullptr;
        const auto parsed = std::strtoul(parityEnv, &end, 10);
        if (end != parityEnv && *end == '\0' && parsed >= 7u && parsed <= 10u) {
            parityMode = static_cast<std::uint32_t>(parsed);
        }
    }
    const renderer::DdgiProbeGridReference parityGrid{{2u, 2u, 2u}, {1, 1, 1}};
    const std::array<float, 3> parityWorldPosition{-0.25f, -0.25f, -0.25f};
    const std::array<float, 3> parityGridOrigin{-1.0f, -1.0f, -1.0f};
    const std::array<float, 3> parityGridSpacing{1.0f, 1.0f, 1.0f};
    double parityCpuExpected = 0.0;
    if (parityMode == 7u || parityMode == 8u) {
        const std::array<std::uint32_t, 3> logical{1u, 0u, 1u};
        parityCpuExpected = static_cast<double>(parityGrid.linearIndex(logical)) / 7.0;
    } else if (parityMode == 9u) {
        parityCpuExpected = renderer::buildDdgiProbeWeightReference(
            parityWorldPosition, parityGridOrigin, parityGridSpacing, parityGrid)
                                .totalWeight;
    } else if (parityMode == 10u) {
        parityCpuExpected = renderer::ddgiChebyshevWeightReference(2.0f, 5.0f, 3.0f);
    }
    PostProcessFixtureResult aggregate;
    const char* captureScenarioEnv = std::getenv("ARP_DDGI_CAPTURE_SCENARIO");
    const bool cameraCapture =
        captureScenarioEnv != nullptr && std::strcmp(captureScenarioEnv, "camera") == 0;
    const bool lightCapture =
        captureScenarioEnv != nullptr && std::strcmp(captureScenarioEnv, "light") == 0;
    const bool dynamicCapture =
        captureScenarioEnv != nullptr && std::strcmp(captureScenarioEnv, "dynamic") == 0;
    const bool teleportCapture =
        captureScenarioEnv != nullptr && std::strcmp(captureScenarioEnv, "teleport") == 0;
    if (cameraCapture)
        aggregate.ddgiCaptureScenario = "camera";
    else if (lightCapture)
        aggregate.ddgiCaptureScenario = "light";
    else if (dynamicCapture)
        aggregate.ddgiCaptureScenario = "dynamic";
    else if (teleportCapture)
        aggregate.ddgiCaptureScenario = "teleport";
    else
        aggregate.ddgiCaptureScenario = "static";
    bool allFramesSuccessful = true;
    core::pmr::String sequenceDiagnostic;
    double maximumCpuMilliseconds = 0.0;
    double maximumColdStartCpuMilliseconds = 0.0;
    bool hasSteadyStateCpuSample = false;
    bool previousGiReadback = false;
    std::uint64_t previousGiHash = 0u;
    double previousGiMean = 0.0;
    bool previousIrradianceReadback = false;
    std::uint64_t previousIrradianceHash = 0u;
    double firstGiMean = 0.0;
    double lastGiMean = 0.0;
    double maximumGiMeanDelta = 0.0;
    double lightingBaselineMean = 0.0;
    double lightingMeanAfter8 = 0.0;
    double lightingMeanAfter30 = 0.0;
    bool lightingBaselineAvailable = false;
    bool lightingMeanAfter8Available = false;
    bool lightingMeanAfter30Available = false;
    std::uint32_t giReadbackFrames = 0u;
    std::uint32_t finiteGiReadbackFrames = 0u;
    std::uint32_t changedGiHashFrames = 0u;
    std::uint32_t maximumNewlyExposedProbes = 0u;
    const auto requestedFrames = std::max(1u, frameCount);
    core::pmr::Vector<DdgiFrameEvidence> frameEvidence;
    frameEvidence.reserve(requestedFrames);
    DdgiFixtureFault activeRecordingFault = DdgiFixtureFault::None;
    std::uint32_t rejectedFrames = 0u;
    DdgiSubmission submission(renderer);
    std::array<core::UniquePtr<render_graph::FrameGraph>, 2> cachedGraphs;
    std::array<core::UniquePtr<render_graph::CompiledFrameGraph>, 2> cachedCompiled;
    std::array<std::optional<render_graph::PassBuilder>, 2> cachedSubmissionBindings;
    // The cached graph callbacks execute against one of two descriptor-set
    // slots. Advance this slot with the sequence so each ping-pong direction
    // reuses stable bindings instead of rewriting one slot every frame.
    std::uint32_t descriptorFrameIndex = 0u;

    for (std::uint32_t frame = 0u; frame < requestedFrames; ++frame) {
        // Keep one renderer and its ping-pong history alive for the complete
        // sequence. This makes --frames exercise temporal resource hazards
        // instead of repeatedly validating first-use initialization.
        // Keep the baseline scene static while advancing the scheduler's
        // frame slot. Explicit capture scenarios below alter one input at a
        // time so the GPU readback can be attributed to camera, lighting, or
        // dynamic-geometry motion.
        descriptorFrameIndex = frame;
        push.scrollOffsetFrame[3] = static_cast<float>(frame);
        push.updateParams[3] = 0.0f;
        push.scrollOffsetFrame[0] = 0.0f;
        push.scrollOffsetFrame[1] = 0.0f;
        push.scrollOffsetFrame[2] = 0.0f;
        for (auto& value : push.diagnosticParams)
            value = 0.0f;
        if (parityMode != 0u) {
            push.diagnosticParams[0] = static_cast<float>(parityMode);
            if (parityMode == 7u || parityMode == 8u) {
                push.scrollOffsetFrame[0] = 1.0f;
                push.scrollOffsetFrame[1] = 1.0f;
                push.scrollOffsetFrame[2] = 1.0f;
                push.diagnosticParams[1] = 1.0f;
                push.diagnosticParams[2] = 0.0f;
                push.diagnosticParams[3] = 1.0f;
            } else if (parityMode == 9u) {
                push.diagnosticParams[1] = parityWorldPosition[0];
                push.diagnosticParams[2] = parityWorldPosition[1];
                push.diagnosticParams[3] = parityWorldPosition[2];
            } else if (parityMode == 10u) {
                push.diagnosticParams[1] = 2.0f;
                push.diagnosticParams[2] = 5.0f;
                push.diagnosticParams[3] = 3.0f;
            }
        }
        push.probeGridOriginHysteresis[0] = -1.0f;
        push.sunDirectionSkyAmbient[3] = 0.2f;
        std::uint64_t scheduleGeometryRevision = 1u;
        if (cameraCapture) {
            const auto cameraStep = frame / 4u;
            push.scrollOffsetFrame[0] = static_cast<float>(cameraStep % 2u);
            push.probeGridOriginHysteresis[0] =
                -1.0f + 0.35f * std::sin(static_cast<float>(frame) * 0.31f);
            // The fixture has no scene camera object, so carry the camera-only
            // motion through the same inverse-view-projection push contract
            // consumed by resolve, in addition to the probe-grid scroll.
            push.invViewProjection[12] = 0.10f * std::sin(static_cast<float>(frame) * 0.23f);
            push.invViewProjection[13] = 0.05f * std::cos(static_cast<float>(frame) * 0.19f);
        } else if (lightCapture && frame >= 8u) {
            push.sunDirectionSkyAmbient[3] = 0.8f;
            push.updateParams[3] = 1.0f;
        } else if (dynamicCapture && frame >= 8u) {
            dynamicProxy.v0[0] = -0.25f + 0.02f * static_cast<float>(frame - 8u);
            dynamicProxy.v1[0] = 0.25f + 0.02f * static_cast<float>(frame - 8u);
            dynamicProxy.v2[0] = 0.0f + 0.02f * static_cast<float>(frame - 8u);
            splitScene.dynamicGeometryRevision = 3u + frame;
            const auto dynamicUpdate = renderer.updateSceneBvh(splitScene);
            if (!dynamicUpdate.success)
                return fail("FEATURE-LAB-DDGI-DYNAMIC-CAPTURE-UPDATE", dynamicUpdate.diagnostic);
            const auto dynamicParameters = dynamicUpdate.shaderParameters();
            std::copy(dynamicParameters.begin(), dynamicParameters.end(), push.sceneBvhParams);
            scheduleGeometryRevision = splitScene.dynamicGeometryRevision;
        } else if (teleportCapture && frame >= 8u) {
            push.probeGridOriginHysteresis[0] = 3.0f;
            push.invViewProjection[12] = 3.0f;
            scheduleGeometryRevision = 1u;
        }
        const auto schedule = renderer.prepareProbeUpdateSchedule(push, scheduleGeometryRevision);
        if (!schedule.success)
            return fail("FEATURE-LAB-DDGI-SCHEDULE", schedule.diagnostics);
        maximumNewlyExposedProbes =
            std::max(maximumNewlyExposedProbes, renderer.telemetry().newlyExposedProbeCount);
        const int readBefore = renderer.currentReadIndex();
        const int readIndex = readBefore;
        const int writeIndex = 1 - readIndex;
        const bool graphWasCached = cachedCompiled[readIndex] != nullptr;
        if (!cachedGraphs[readIndex]) {
            cachedGraphs[readIndex] = core::makeUnique<render_graph::FrameGraph>(device);
        }
        auto& graph = *cachedGraphs[readIndex];
        if (!cachedCompiled[readIndex]) {
            cachedSubmissionBindings[readIndex].emplace(populateDdgiGraph(graph,
                renderer,
                extent,
                push,
                descriptorFrameIndex,
                submission,
                &activeRecordingFault));
            cachedCompiled[readIndex] =
                core::makeUnique<render_graph::CompiledFrameGraph>(graph.compile());
        }
        const auto& compiled = *cachedCompiled[readIndex];
        // BorderSourceCopy is an explicit WebGPU-compatible pass between
        // distance blending and border update, so the executable DDGI graph
        // contains ten passes in the production path.
        const bool dispatchOrderValid = compiled.passes().size() == 10u;
        // The production graph is cached by ping-pong direction; measure the
        // steady-state per-frame record/submit path after its one-time compile.
        const auto cpuStart = std::chrono::steady_clock::now();
        submission.accepted = false;
        cachedSubmissionBindings[readIndex]->onSubmitted(
            submission, renderer.preparedHistoryToken());
        if (frame == 1u && recordingFault != DdgiFixtureFault::None) {
            const auto submissionsBefore = device.queue().submitCount();
            const auto generationBefore = renderer.acceptedProbeScheduleGeneration();
            const auto newlyExposedBefore = renderer.telemetry().newlyExposedProbeCount;
            activeRecordingFault = recordingFault;
            bool rejected = false;
            try {
                graph.execute(compiled);
            } catch (const core::DiagnosticError& error) {
                rejected = std::string_view(error.what()) == "FEATURE-LAB-DDGI-RECORD-REJECTED";
                if (!rejected)
                    return fail("FEATURE-LAB-DDGI-FAULT-PRECONDITION", error.what());
            } catch (const std::exception& error) {
                auto diagnostic = core::pmr::String(error.what());
                if (!graph.barrierTrace().empty())
                    diagnostic += " resource=" + graph.barrierTrace().back().resourceName;
                return fail("FEATURE-LAB-DDGI-FAULT-PRECONDITION", diagnostic);
            }
            activeRecordingFault = DdgiFixtureFault::None;
            if (!rejected || graph.lastExecutionSubmitted() || submission.accepted
                || device.queue().submitCount() != submissionsBefore
                || renderer.currentReadIndex() != readBefore
                || renderer.acceptedProbeScheduleGeneration() != generationBefore)
                return fail("FEATURE-LAB-DDGI-REJECTION-SIDE-EFFECT");
            ++rejectedFrames;
            // Same business input and independently accepted uploads as the
            // clean sequence. Only the abandoned main graph is omitted.
            const auto retried =
                renderer.prepareProbeUpdateSchedule(push, scheduleGeometryRevision);
            if (!retried.success)
                return fail("FEATURE-LAB-DDGI-RETRY-PREPARE", retried.diagnostics);
            if (renderer.telemetry().newlyExposedProbeCount != newlyExposedBefore)
                return fail("FEATURE-LAB-DDGI-RETRY-SCHEDULE-CHANGED");
            cachedSubmissionBindings[readIndex]->onSubmitted(
                submission, renderer.preparedHistoryToken());
        }
        try {
            graph.execute(compiled);
        } catch (const std::exception& error) {
            auto diagnostic = core::pmr::String(error.what());
            if (!graph.barrierTrace().empty())
                diagnostic += " resource=" + graph.barrierTrace().back().resourceName;
            return fail("FEATURE-LAB-DDGI-EXECUTE", diagnostic);
        }
        if (!submission.accepted)
            return fail("FEATURE-LAB-DDGI-SCHEDULE-COMMIT");
        const auto cpuMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart)
                .count();
        if (graphWasCached) {
            maximumCpuMilliseconds = std::max(maximumCpuMilliseconds, cpuMilliseconds);
            hasSteadyStateCpuSample = true;
        } else {
            maximumColdStartCpuMilliseconds =
                std::max(maximumColdStartCpuMilliseconds, cpuMilliseconds);
        }
        device.waitIdle();
        rhi::TextureReadbackSample giSample;
        core::pmr::String diagnostic;
        const auto giReadback = readbackOutput(device,
            *renderer.giOutput(writeIndex),
            "FeatureLab.DdgiGiOutput",
            giSample,
            diagnostic);
        const bool giAvailable = giReadback == FixtureReadbackStatus::Available;
        const bool giFinite = giAvailable && giSample.sampleCount > 0u
            && giSample.finiteCount == giSample.sampleCount;
        const bool parityAvailable = parityMode != 0u && giAvailable && giFinite;
        const double parityGpuMeasured = parityAvailable ? giSample.meanValue : 0.0;
        const double parityAbsoluteError =
            parityAvailable ? std::abs(parityGpuMeasured - parityCpuExpected) : 0.0;
        const bool parityPassed = parityAvailable && parityAbsoluteError <= 0.02;
        rhi::TextureReadbackSample irradianceSample;
        rhi::TextureReadbackSample distanceSample;
        core::pmr::String irradianceDiagnostic;
        core::pmr::String distanceDiagnostic;
        const auto irradianceReadback = readbackOutput(device,
            *renderer.probeIrradiance(writeIndex),
            "FeatureLab.DdgiIrradiance",
            irradianceSample,
            irradianceDiagnostic);
        const auto distanceReadback = readbackOutput(device,
            *renderer.probeDepthMoments(writeIndex),
            "FeatureLab.DdgiDistance",
            distanceSample,
            distanceDiagnostic);
        const bool irradianceAvailable = irradianceReadback == FixtureReadbackStatus::Available;
        const bool distanceAvailable = distanceReadback == FixtureReadbackStatus::Available;
        const bool irradianceFinite = irradianceAvailable && irradianceSample.sampleCount > 0u
            && irradianceSample.finiteCount == irradianceSample.sampleCount;
        const bool distanceFinite = distanceAvailable && distanceSample.sampleCount > 0u
            && distanceSample.finiteCount == distanceSample.sampleCount;
        frameEvidence.push_back({{giSample.channelStatistics,
                                     irradianceSample.channelStatistics,
                                     distanceSample.channelStatistics},
            {giSample.sampleHash, irradianceSample.sampleHash, distanceSample.sampleHash},
            renderer.telemetry().newlyExposedProbeCount,
            renderer.currentReadIndex()});
        bool frameReadbackHashChanged = false;
        if (giAvailable) {
            ++giReadbackFrames;
            if (giFinite)
                ++finiteGiReadbackFrames;
            if (lightCapture && frame < 8u) {
                lightingBaselineMean = giSample.meanValue;
                lightingBaselineAvailable = true;
            }
            // The light step is authored at frame 8. These samples represent
            // the first readback after eight and thirty post-step frames.
            if (lightCapture && frame >= 15u && !lightingMeanAfter8Available) {
                lightingMeanAfter8 = giSample.meanValue;
                lightingMeanAfter8Available = true;
            }
            if (lightCapture && frame >= 37u) {
                lightingMeanAfter30 = giSample.meanValue;
                lightingMeanAfter30Available = true;
            }
            if (!previousGiReadback) {
                firstGiMean = giSample.meanValue;
            } else {
                frameReadbackHashChanged =
                    frameReadbackHashChanged || giSample.sampleHash != previousGiHash;
                maximumGiMeanDelta =
                    std::max(maximumGiMeanDelta, std::abs(giSample.meanValue - previousGiMean));
            }
            lastGiMean = giSample.meanValue;
            previousGiHash = giSample.sampleHash;
            previousGiMean = giSample.meanValue;
            previousGiReadback = true;
        }
        if (irradianceAvailable) {
            if (previousIrradianceReadback) {
                frameReadbackHashChanged = frameReadbackHashChanged
                    || irradianceSample.sampleHash != previousIrradianceHash;
            }
            previousIrradianceHash = irradianceSample.sampleHash;
            previousIrradianceReadback = true;
        }
        if (frameReadbackHashChanged)
            ++changedGiHashFrames;
        const bool pingPongValid = renderer.currentReadIndex() == 1 - readBefore;

        PostProcessFixtureResult result;
        result.graphPassCount = static_cast<std::uint32_t>(compiled.passes().size());
        result.success = resourcesReady && dispatchOrderValid && pingPongValid
            && (device.backend() == rhi::Backend::Null || (giAvailable && giFinite))
            && (!renderer.sceneGeometryRayQuerySupported() || bvh.triangleCount == 1u);
        result.passTrace = {"DDGI.Create",
            "DDGI.BvhUpload",
            "DDGI.HistoryCopy",
            "DDGI.ProbeRelocate",
            "DDGI.ProbeTrace",
            "DDGI.BlendIrradiance",
            "DDGI.BlendDistance",
            "DDGI.BorderSourceCopy",
            "DDGI.BorderUpdate",
            "DDGI.Resolve",
            "DDGI.PingPong"};
        result.attachmentMetrics.push_back(
            {"ddgi-probes", "persistent_resource_set", resourcesReady ? 1.0f : 0.0f, true});
        result.attachmentMetrics.push_back({"ddgi-lifecycle",
            "resize_recreate_reset",
            lifecycleRecreateValid ? 1.0f : 0.0f,
            true});
        result.attachmentMetrics.push_back({"ddgi-lifecycle",
            "destroy_recreate_reset",
            lifecycleDestroyRecreateValid ? 1.0f : 0.0f,
            true});
        result.attachmentMetrics.push_back({"ddgi-bvh",
            "synthetic_triangle_count",
            static_cast<float>(bvh.triangleCount),
            renderer.sceneGeometryRayQuerySupported()});
        result.attachmentMetrics.push_back(
            {"ddgi-gi-output", "ping_pong_valid", pingPongValid ? 1.0f : 0.0f, true});
        result.attachmentMetrics.push_back(
            {"ddgi-gi-output", "finite_fraction", giFinite ? 1.0f : 0.0f, giAvailable});
        // L05-ddgi exposes each production intermediate as a separately named
        // attachment. Classification/age/confidence are state-buffer contract
        // metrics here: the scheduled probe set and finite scheduler statistics
        // prove that the state path is populated without inventing CPU readback
        // data on backends that do not expose storage-buffer readback.
        result.attachmentMetrics.push_back(
            {"irradiance", "finite_fraction", irradianceFinite ? 1.0f : 0.0f, irradianceAvailable});
        result.attachmentMetrics.push_back(
            {"distance", "finite_fraction", distanceFinite ? 1.0f : 0.0f, distanceAvailable});
        const auto totalProbes = 8.0f;
        result.attachmentMetrics.push_back({"classification",
            "active_fraction",
            std::min(
                1.0f, static_cast<float>(renderer.telemetry().updatedProbeCount) / totalProbes),
            true});
        result.attachmentMetrics.push_back({"age",
            "finite_fraction",
            std::isfinite(renderer.telemetry().averageProbeAge) ? 1.0f : 0.0f,
            true});
        result.attachmentMetrics.push_back(
            {"confidence", "finite_fraction", schedule.success ? 1.0f : 0.0f, true});
        result.attachmentMetrics.push_back(
            {"screen-cache", "ping_pong_valid", pingPongValid ? 1.0f : 0.0f, true});
        result.attachmentMetrics.push_back(
            {"final-hdr", "finite_fraction", giFinite ? 1.0f : 0.0f, giAvailable});
        result.submittedFrameCount = device.queue().submitCount();
        result.gpuMemoryBytes = device.memoryStats().allocatedBytes;
        result.cpuMilliseconds = cpuMilliseconds;
        result.ddgiCaptureScenario = aggregate.ddgiCaptureScenario;
        result.ddgiReadbackFrames = giReadbackFrames;
        result.ddgiFiniteReadbackFrames = finiteGiReadbackFrames;
        result.ddgiChangedHashFrames = changedGiHashFrames;
        result.ddgiMaximumNewlyExposedProbes = maximumNewlyExposedProbes;
        result.ddgiFirstMean = firstGiMean;
        result.ddgiLastMean = lastGiMean;
        result.ddgiMaximumMeanDelta = maximumGiMeanDelta;
        result.ddgiReadbackAvailable = giReadbackFrames != 0u;
        result.ddgiParityMode = parityMode;
        result.ddgiParityCpuExpected = parityCpuExpected;
        result.ddgiParityGpuMeasured = parityGpuMeasured;
        result.ddgiParityAbsoluteError = parityAbsoluteError;
        result.ddgiParityAvailable = parityAvailable;
        result.ddgiParityPassed = parityMode == 0u || parityPassed;
        result.success = result.success && (parityMode == 0u || parityPassed);
        if (!result.success)
            result.diagnostic =
                diagnostic.empty() ? "FEATURE-LAB-DDGI-EXECUTION-ACCEPTANCE" : diagnostic;
        allFramesSuccessful = allFramesSuccessful && result.success;
        if (!result.success && sequenceDiagnostic.empty())
            sequenceDiagnostic = result.diagnostic;
        aggregate = std::move(result);
    }
    aggregate.success = allFramesSuccessful;
    aggregate.ddgiRejectedFrames = rejectedFrames;
    aggregate.ddgiFrameEvidence = std::move(frameEvidence);
    // Performance evidence must describe graph reuse, not the one-time
    // transient materialization of each ping-pong topology. Short one-frame
    // runs have no reuse sample, so retain their cold-start measurement.
    aggregate.cpuMilliseconds =
        hasSteadyStateCpuSample ? maximumCpuMilliseconds : maximumColdStartCpuMilliseconds;
    aggregate.cpuTimingScope = hasSteadyStateCpuSample ? "compiled-topology-reuse" : "cold-start";
    aggregate.ddgiReadbackFrames = giReadbackFrames;
    aggregate.ddgiFiniteReadbackFrames = finiteGiReadbackFrames;
    aggregate.ddgiChangedHashFrames = changedGiHashFrames;
    aggregate.ddgiMaximumNewlyExposedProbes = maximumNewlyExposedProbes;
    aggregate.ddgiFirstMean = firstGiMean;
    aggregate.ddgiLastMean = lastGiMean;
    aggregate.ddgiMaximumMeanDelta = maximumGiMeanDelta;
    if (lightCapture && lightingBaselineAvailable && lightingMeanAfter8Available
        && lightingMeanAfter30Available && std::isfinite(lastGiMean)) {
        const auto totalChange = std::abs(lastGiMean - lightingBaselineMean);
        if (totalChange > 1.0e-6) {
            const auto convergence = [](double sample, double baseline, double total) {
                return std::clamp(std::abs(sample - baseline) / total, 0.0, 1.0);
            };
            aggregate.ddgiLightingConvergence8 =
                convergence(lightingMeanAfter8, lightingBaselineMean, totalChange);
            aggregate.ddgiLightingConvergence30 =
                convergence(lightingMeanAfter30, lightingBaselineMean, totalChange);
            aggregate.ddgiLightingConvergenceAvailable = true;
        }
    }
    aggregate.ddgiReadbackAvailable = giReadbackFrames != 0u;
    aggregate.ddgiParityMode = parityMode;
    aggregate.ddgiParityCpuExpected = parityCpuExpected;
    aggregate.ddgiParityAvailable = parityMode != 0u && aggregate.ddgiParityGpuMeasured != 0.0;
    aggregate.ddgiParityPassed = parityMode == 0u || aggregate.ddgiParityPassed;
    if (presentationSwapchain != nullptr) {
        aggregate.ddgiPresentationAttempted = true;
        auto acquired = presentationSwapchain->acquireNextFrame();
        if (!acquired.success) {
            aggregate.success = false;
            aggregate.diagnostic = acquired.diagnostics;
        } else {
            auto acquiredImage = presentationSwapchain->acquiredImage(acquired);
            if (!acquiredImage.success || !acquiredImage.texture) {
                presentationSwapchain->discardFrame(acquired);
                aggregate.success = false;
                aggregate.diagnostic = acquiredImage.diagnostics;
            } else {
                render_graph::FrameGraph presentGraph(device);
                const auto source =
                    presentGraph.importTextureRef(renderer.giOutput(renderer.currentReadIndex()));
                const auto target = presentGraph.importSwapchainTexture(
                    std::move(acquiredImage.texture), acquiredImage.ref);
                const auto targetExtent =
                    rhi::Extent2D{acquiredImage.ref.extent.width, acquiredImage.ref.extent.height};
                renderer::FxaaRenderer presenter(device);
                const auto presenterCreated = presenter.create(
                    {shaderPath("fxaa.slang"), acquiredImage.ref.format, targetExtent});
                if (!presenterCreated.success) {
                    presentationSwapchain->discardFrame(acquired);
                    aggregate.success = false;
                    aggregate.diagnostic = presenterCreated.diagnostics;
                } else {
                    const auto presentGraphObject = &presentGraph;
                    core::pmr::String presentationDiagnostic;
                    presentGraph.addPass("DDGI.Present")
                        .writeAttachment(target,
                            rhi::ResourceState::ColorAttachment,
                            rhi::LoadOp::DontCare,
                            rhi::StoreOp::Store)
                        .read(source, rhi::ResourceState::ShaderRead)
                        .execute(
                            [&presenter,
                                presentGraphObject,
                                source,
                                target,
                                acquired,
                                &presentationDiagnostic](render_graph::RenderContext& context) {
                                auto& sourceTexture = presentGraphObject->texture(source);
                                auto& targetTexture = presentGraphObject->texture(target);
                                const auto frame = frameContext(targetTexture,
                                    {targetTexture.desc().extent.width,
                                        targetTexture.desc().extent.height},
                                    acquired.frameIndex);
                                const auto recorded =
                                    presenter.recordPass(context, frame, sourceTexture);
                                if (!recorded.success) {
                                    presentationDiagnostic = recorded.diagnostics;
                                }
                            });
                    try {
                        presentGraph.execute();
                        device.waitIdle();
#if defined(__EMSCRIPTEN__)
                        // Read the acquired canvas texture through the same
                        // backend-owned staging path used by DDGI attachments.
                        // This is direct WebGPU texture evidence, distinct
                        // from the browser screenshot collected by the verifier.
                        rhi::TextureReadbackSample presentationSample;
                        core::pmr::String presentationReadbackDiagnostic;
                        const auto presentationReadbackStatus = readbackOutput(device,
                            presentGraph.texture(target),
                            "FeatureLab.DdgiPresentation",
                            presentationSample,
                            presentationReadbackDiagnostic);
                        aggregate.ddgiPresentationReadbackAvailable =
                            presentationReadbackStatus == FixtureReadbackStatus::Available;
                        aggregate.ddgiPresentationReadbackFinite =
                            aggregate.ddgiPresentationReadbackAvailable
                            && presentationSample.sampleCount > 0u
                            && presentationSample.finiteCount == presentationSample.sampleCount;
                        if (aggregate.ddgiPresentationReadbackAvailable) {
                            aggregate.ddgiPresentationReadbackMean = presentationSample.meanValue;
                            aggregate.ddgiPresentationReadbackHash = presentationSample.sampleHash;
                        }
#endif
                    } catch (const std::exception& error) {
                        presentationDiagnostic = error.what();
                    }
                    if (!presentationDiagnostic.empty()) {
                        presentationSwapchain->discardFrame(acquired);
                        aggregate.success = false;
                        aggregate.diagnostic = presentationDiagnostic;
                    } else {
                        const auto presented = presentationSwapchain->present(acquired);
                        aggregate.ddgiPresentationSucceeded = presented.success;
                        if (!presented.success) {
                            aggregate.success = false;
                            aggregate.diagnostic = presented.diagnostics;
                        } else {
                            aggregate.passTrace.push_back("DDGI.Present");
                        }
                    }
                }
            }
        }
    }
    if (!aggregate.success && aggregate.diagnostic.empty())
        aggregate.diagnostic = sequenceDiagnostic;
    return aggregate;
}

PostProcessFixtureResult runBasicAttachmentFixture(rhi::Extent2D extent, const char* attachment)
{
    auto device = rhi::createNullDevice();
    if (!device)
        return fail("FEATURE-LAB-BASIC-NULL-DEVICE");
    return runBasicAttachmentFixture(*device, extent, attachment);
}

PostProcessFixtureResult runBasicAttachmentFixture(
    rhi::GpuDevice& device, rhi::Extent2D extent, const char* attachment)
{
    extent.width = std::clamp(extent.width, 8u, 64u);
    extent.height = std::clamp(extent.height, 8u, 64u);
    const bool depthAttachment =
        std::strcmp(attachment, "exact-depth") == 0 || std::strcmp(attachment, "csm-atlas") == 0;
    core::pmr::String diagnostic;
    PostProcessFixtureResult result;
    result.success = true;
    if (depthAttachment) {
        auto depth = createDepthFixtureTexture(device, "FeatureLab.BasicDepth", extent);
        if (!depth || !rasterizeDepthStepFixture(device, *depth, extent, diagnostic)) {
            return fail("FEATURE-LAB-BASIC-DEPTH", diagnostic);
        }
        rhi::TextureReadbackSample sample;
        const auto status =
            readbackOutput(device, *depth, "FeatureLab.BasicDepthReadback", sample, diagnostic);
        const bool available = status == FixtureReadbackStatus::Available;
        const bool nullBackend = device.backend() == rhi::Backend::Null;
        result.attachmentMetrics.push_back({attachment,
            "finite_fraction",
            available && sample.sampleCount > 0u
                ? static_cast<float>(sample.finiteCount) / sample.sampleCount
                : 0.0f,
            available});
        result.success =
            nullBackend || (available && result.attachmentMetrics.back().value >= 1.0f);
        result.passTrace.push_back("FeatureLab.ExactDepthPrepass");
    } else {
        auto input = createInputTexture(
            device, "FeatureLab.BasicColorInput", extent, rhi::TextureFormat::Bgra8Unorm);
        auto output = createOutputTexture(
            device, "FeatureLab.BasicColorOutput", extent, rhi::TextureFormat::Bgra8Unorm);
        if (!input || !output
            || !uploadDeterministicFixture(
                device, *input, FixturePattern::ColorChecker, diagnostic)) {
            return fail("FEATURE-LAB-BASIC-COLOR", diagnostic);
        }
        if (!prepareOutputTexture(device, *output, diagnostic)) {
            return fail("FEATURE-LAB-BASIC-OUTPUT", diagnostic);
        }
        renderer::FxaaRenderer fxaa(device);
        const auto created =
            fxaa.create({shaderPath("fxaa.slang"), rhi::TextureFormat::Bgra8Unorm, extent});
        if (!created.success)
            return fail("FEATURE-LAB-BASIC-PIPELINE", created.diagnostics);
        const auto drawn = fxaa.drawFrame(frameContext(*output, extent, 0u), *input);
        if (!drawn.success)
            return fail("FEATURE-LAB-BASIC-DRAW", drawn.diagnostics);
        rhi::TextureReadbackSample sample;
        const auto status =
            readbackOutput(device, *output, "FeatureLab.BasicColorReadback", sample, diagnostic);
        const bool available = status == FixtureReadbackStatus::Available;
        const bool nullBackend = device.backend() == rhi::Backend::Null;
        const char* metricName =
            std::strcmp(attachment, "final-ldr") == 0 ? "non_empty_fraction" : "finite_fraction";
        result.attachmentMetrics.push_back({attachment,
            metricName,
            available && sample.sampleCount > 0u
                ? static_cast<float>(sample.finiteCount) / sample.sampleCount
                : 0.0f,
            available});
        result.success =
            nullBackend || (available && result.attachmentMetrics.back().value >= 1.0f);
        result.passTrace.push_back("FeatureLab.SurfaceColor");
    }
    result.submittedFrameCount = device.queue().submitCount();
    result.gpuMemoryBytes = device.memoryStats().allocatedBytes;
    result.diagnostic = diagnostic;
    return result;
}

} // namespace arp::tests::postprocess
