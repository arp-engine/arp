#include "renderer/render_graph/conformance_sampled_recorder.h"

#include "core/diagnostic_error.h"
#include "rhi/conformance/sampled_pipeline.h"
#include "rhi/gpu_device.h"

namespace arp::render_graph {
using namespace rhi;

struct ConformanceSampledRecorder::PreparedFrame {
    Extent2D extent;

    struct Draw {
        ConformanceSampledResource sourceA, sourceB, target;
        std::array<float, 8> push;
        bool indexed;
        core::UniquePtr<DescriptorSet> descriptors;
    };

    std::shared_ptr<GpuBuffer> vertices, indices, uniforms;
    std::array<std::shared_ptr<GpuTexture>, 4> sources;
    std::array<Color, 2> clears;
    std::array<core::UniquePtr<GpuTextureView>, 4> views;
    std::shared_ptr<GpuSampler> sampler;
    core::UniquePtr<DescriptorArena> arena;
    core::pmr::Vector<Draw> draws{&core::defaultCpuMemoryResource()};
};

ConformanceSampledRecorder::ConformanceSampledRecorder(ConstructionKey,
    std::shared_ptr<RenderPipeline> pipeline,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : pipeline_(std::move(pipeline)), quota_(std::move(quota))
{}

ConformanceSampledRecorder::~ConformanceSampledRecorder() = default;

RenderPipelineDesc ConformanceSampledRecorder::pipelineDefinition(
    const PipelineLayout& layout, const ShaderPackageDesc& package, TextureFormat format)
{
    if (package.entrypoints.size() != 2)
        throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-SHADER-ENTRIES");
    return ConformanceSampledPipeline::definition(layout, package, format);
}

core::UniquePtr<ConformanceSampledRecorder> ConformanceSampledRecorder::prepare(GpuDevice& device,
    std::shared_ptr<RenderPipeline> pipeline,
    const ConformanceSampledWorkload& workload,
    const ConformanceSampledRecorderDesc& desc,
    core::pmr::String& diagnostic,
    OriginalConformanceSampledExecution* original,
    std::shared_ptr<RenderPipeline> intermediate)
{
    diagnostic = "RHI-CONFORMANCE-SAMPLED-PREPARATION-CONTRACT";
    if (desc.contractVersion != 1 || !desc.prototypeOptIn || !desc.maxPreparedFrames
        || !desc.maxCpuMetadataBytes || !desc.quota
        || desc.quota->limitBytes() != desc.maxCpuMetadataBytes
        || workload.frames().size() > desc.maxPreparedFrames || !pipeline || !pipeline->layout())
        return {};
    diagnostic = "RHI-CONFORMANCE-SAMPLED-SHADER-IDENTITY";
    if (original
        && (!original->run() || original->run()->workloadSha256() != workload.sourceSha256())) {
        diagnostic = "RHI-CONFORMANCE-SAMPLED-ORIGINAL-WORKLOAD-IDENTITY";
        return {};
    }
    const auto& reflection = pipeline->shaderReflection();
    if (reflection.sourceSha256 != workload.shaderSourceSha256()
        || reflection.pushConstantByteCount != 32 || reflection.descriptorBindings.size() != 5
        || pipeline->layout()->desc().descriptorLayouts.size() != 1)
        return {};
    bool needsIntermediate = false;
    for (const auto& frame : workload.frames())
        for (const auto& draw : frame.draws)
            needsIntermediate =
                needsIntermediate || draw.target != ConformanceSampledResource::Output;
    if (!intermediate && pipeline->validationReport().colorFormats.size() == 1
        && pipeline->validationReport().colorFormats[0] == TextureFormat::Rgba8Unorm)
        intermediate = pipeline;
    if (needsIntermediate
        && (!intermediate || intermediate->validationReport().colorFormats.size() != 1
            || intermediate->validationReport().colorFormats[0] != TextureFormat::Rgba8Unorm
            || !PipelineDefinitionIdentity::matches(intermediate->shaderReflection(), reflection)
            || !intermediate->layout()
            || !PipelineDefinitionIdentity::matches(
                intermediate->layout()->desc(), pipeline->layout()->desc()))) {
        diagnostic = "RHI-CONFORMANCE-SAMPLED-INTERMEDIATE-PIPELINE-FORMAT";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(desc.quota);
        auto result =
            core::makeUnique<ConformanceSampledRecorder>(ConstructionKey{}, pipeline, desc.quota);
        result->intermediate_ = std::move(intermediate);
        result->frames_.reserve(workload.frames().size());
        const char* originalDiagnostic = nullptr;
        auto vertices = original
            ? original->prepareInput(ConformanceSampledInput::Vertices, 0, originalDiagnostic)
            : core::shareUnique(device.createBuffer({"Conformance.Vertices",
                  sizeof(workload.vertices()),
                  ResourceUsage::VertexBuffer | ResourceUsage::CopyDst,
                  MemoryUsage::CpuToGpu,
                  ResourceState::ShaderRead}));
        auto indices = original
            ? original->prepareInput(ConformanceSampledInput::Indices, 0, originalDiagnostic)
            : core::shareUnique(device.createBuffer({"Conformance.Indices",
                  sizeof(workload.indices()),
                  ResourceUsage::IndexBuffer | ResourceUsage::CopyDst,
                  MemoryUsage::CpuToGpu,
                  ResourceState::ShaderRead}));
        auto sampler = original
            ? original->prepareSampler(originalDiagnostic)
            : core::shareUnique(device.createSampler({"Conformance.NearestClamp",
                  SamplerFilter::Nearest,
                  SamplerFilter::Nearest,
                  SamplerAddressMode::ClampToEdge,
                  SamplerAddressMode::ClampToEdge,
                  SamplerAddressMode::ClampToEdge,
                  CompareOp::Never,
                  SamplerMipFilter::None,
                  0}));
        if (!vertices || !indices || !sampler)
            throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-RESOURCE-CREATION");
        if (!original) {
            core::requireSuccessfulResult(
                device.writeBuffer(
                    *vertices, workload.vertices().data(), sizeof(workload.vertices())),
                "CONFORMANCE-VERTEX-UPLOAD");
            core::requireSuccessfulResult(
                device.writeBuffer(*indices, workload.indices().data(), sizeof(workload.indices())),
                "CONFORMANCE-INDEX-UPLOAD");
        }
        std::size_t frameIndex = 0;
        for (const auto& input : workload.frames()) {
            auto frame = core::makeShared<PreparedFrame>();
            frame->extent = input.extent;
            frame->vertices = vertices;
            frame->indices = indices;
            frame->sampler = sampler;
            frame->uniforms = original
                ? original->prepareInput(
                      ConformanceSampledInput::Uniforms, frameIndex++, originalDiagnostic)
                : core::shareUnique(device.createBuffer({"Conformance.Uniforms",
                      sizeof(input.uniforms),
                      ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
                      MemoryUsage::CpuToGpu,
                      ResourceState::ShaderRead}));
            if (!frame->uniforms)
                throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-UNIFORM-CREATION");
            if (!original)
                core::requireSuccessfulResult(
                    device.writeBuffer(
                        *frame->uniforms, input.uniforms.data(), sizeof(input.uniforms)),
                    "CONFORMANCE-UNIFORM-UPLOAD");
            frame->clears = {ConformanceClearFrame{input.extent, input.sourceA}.clearColor(),
                ConformanceClearFrame{input.extent, input.sourceB}.clearColor()};
            std::array<bool, 4> required{true, true};
            for (const auto& draw : input.draws)
                if (draw.target != ConformanceSampledResource::Output)
                    required[static_cast<std::size_t>(draw.target)] = true;
            constexpr std::array<const char*, 4> names{"Conformance.SourceA",
                "Conformance.SourceB",
                "Conformance.PingA",
                "Conformance.PingB"};
            for (std::size_t i = 0; i < frame->sources.size(); ++i) {
                if (!required[i])
                    continue;
                frame->sources[i] = core::shareUnique(device.createTexture({names[i],
                    {input.extent.width, input.extent.height, 1},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::RenderAttachment | TextureUsage::Sampled,
                    MemoryUsage::GpuOnly,
                    ResourceState::Undefined}));
                if (!frame->sources[i])
                    throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-SOURCE-CREATION");
                TextureViewDesc view;
                view.format = TextureFormat::Rgba8Unorm;
                view.usage = TextureUsage::Sampled;
                view.aspect = TextureAspect::Color;
                frame->views[i] = device.createTextureView(*frame->sources[i], view);
                if (!frame->views[i])
                    throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-SOURCE-VIEW");
            }
            const auto count = static_cast<std::uint32_t>(input.draws.size());
            frame->arena =
                device.createDescriptorArena({"Conformance.Descriptors", count, count * 5});
            if (!frame->arena)
                throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-DESCRIPTOR-ARENA");
            frame->draws.reserve(count);
            for (const auto& draw : input.draws) {
                PreparedFrame::Draw prepared{
                    draw.sourceA, draw.sourceB, draw.target, draw.push, draw.indexed, {}};
                DescriptorSetDesc set;
                set.layout = pipeline->layout()->desc().descriptorLayouts[0];
                prepared.descriptors = frame->arena->allocateDescriptorSet(set);
                if (!prepared.descriptors)
                    throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-DESCRIPTOR-SET");
                core::requireSuccessfulResult(prepared.descriptors->write({0,
                                                  DescriptorType::UniformBuffer,
                                                  frame->uniforms->id(),
                                                  DescriptorResourceKind::Buffer}),
                    "CONFORMANCE-UNIFORM-BINDING");
                for (std::uint32_t i = 0; i < 2; ++i) {
                    core::requireSuccessfulResult(
                        prepared.descriptors->write({1 + i * 2,
                            DescriptorType::SampledTexture,
                            frame->views[static_cast<std::size_t>(i ? draw.sourceB : draw.sourceA)]
                                ->id(),
                            DescriptorResourceKind::TextureView}),
                        "CONFORMANCE-SOURCE-BINDING");
                    core::requireSuccessfulResult(prepared.descriptors->write({2 + i * 2,
                                                      DescriptorType::Sampler,
                                                      sampler->id(),
                                                      DescriptorResourceKind::Sampler}),
                        "CONFORMANCE-SAMPLER-BINDING");
                }
                frame->draws.push_back(std::move(prepared));
            }
            result->frames_.push_back(std::move(frame));
        }
        diagnostic.clear();
        return result;
    } catch (const core::DiagnosticError& error) {
        diagnostic = error.what();
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-CONFORMANCE-SAMPLED-CPU-ALLOCATION";
    }
    return {};
}

void ConformanceSampledRecorder::record(std::size_t index, FrameGraph& graph, TextureHandle target)
{
    if (index >= frames_.size())
        throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-FRAME-INDEX");
    auto frame = frames_[index];
    const auto extent = graph.texture(target).desc().extent;
    if (extent.width != frame->extent.width || extent.height != frame->extent.height)
        throw core::DiagnosticError("RHI-CONFORMANCE-SAMPLED-FRAME-EXTENT");
    std::array<TextureHandle, 5> sources;
    for (std::size_t i = 0; i < frame->sources.size(); ++i)
        if (frame->sources[i])
            sources[i] = graph.importTextureRef(frame->sources[i]);
    sources[static_cast<std::size_t>(ConformanceSampledResource::Output)] = target;
    const auto vertices = graph.importBufferRef(frame->vertices);
    const auto uniforms = graph.importBufferRef(frame->uniforms);
    const auto indices = graph.importBufferRef(frame->indices);
    for (std::size_t i = 0; i < frame->clears.size(); ++i) {
        graph.addPass(i ? "Conformance.SourceB" : "Conformance.SourceA")
            .writeAttachment(sources[i],
                ResourceState::ColorAttachment,
                LoadOp::Clear,
                StoreOp::Store,
                {},
                frame->clears[i])
            .executeRenderPass([](RenderPassContext&) {});
    }
    for (std::size_t drawIndex = 0; drawIndex < frame->draws.size(); ++drawIndex) {
        const auto& draw = frame->draws[drawIndex];
        auto pass = graph.addPass("Conformance.Sampled");
        pass.read(sources[static_cast<std::size_t>(draw.sourceA)], ResourceState::ShaderRead)
            .read(sources[static_cast<std::size_t>(draw.sourceB)], ResourceState::ShaderRead)
            .access(vertices,
                {ResourceState::ShaderRead,
                    PipelineStage::VertexInput,
                    rhi::ResourceAccess::VertexAttributeRead},
                0,
                frame->vertices->desc().sizeBytes)
            .access(uniforms,
                {ResourceState::ShaderRead,
                    PipelineStage::VertexShader | PipelineStage::FragmentShader,
                    rhi::ResourceAccess::ShaderRead},
                0,
                frame->uniforms->desc().sizeBytes)
            .writeAttachment(sources[static_cast<std::size_t>(draw.target)],
                ResourceState::ColorAttachment,
                LoadOp::Clear,
                StoreOp::Store,
                {},
                {0, 0, 0, 1});
        if (draw.indexed)
            pass.access(indices,
                {ResourceState::ShaderRead,
                    PipelineStage::VertexInput,
                    rhi::ResourceAccess::IndexRead},
                0,
                frame->indices->desc().sizeBytes);
        pass.executeRenderPass([frame,
                                   pipeline = draw.target == ConformanceSampledResource::Output
                                       ? pipeline_
                                       : intermediate_,
                                   drawIndex](RenderPassContext& context) {
            const auto& draw = frame->draws[drawIndex];
            auto& encoder = context.renderPass();
            core::requireSuccessfulResult(encoder.setPipeline(*pipeline), "CONFORMANCE-PIPELINE");
            core::requireSuccessfulResult(
                encoder.setViewport(
                    {0, 0, float(frame->extent.width), float(frame->extent.height), 0, 1}),
                "CONFORMANCE-VIEWPORT");
            core::requireSuccessfulResult(
                encoder.setScissor({0, 0, frame->extent.width, frame->extent.height}),
                "CONFORMANCE-SCISSOR");
            core::requireSuccessfulResult(
                encoder.bindVertexBuffer(3, *frame->vertices), "CONFORMANCE-VERTEX");
            core::requireSuccessfulResult(
                encoder.bindDescriptorSet(0, *draw.descriptors), "CONFORMANCE-DESCRIPTOR");
            core::requireSuccessfulResult(
                encoder.pushConstants(ShaderStage::Vertex | ShaderStage::Fragment,
                    0,
                    sizeof(draw.push),
                    draw.push.data()),
                "CONFORMANCE-PUSH");
            if (draw.indexed) {
                core::requireSuccessfulResult(
                    encoder.bindIndexBuffer(*frame->indices, IndexFormat::Uint16),
                    "CONFORMANCE-INDEX");
                core::requireSuccessfulResult(
                    encoder.drawIndexed(3, 1, 0, 0, 0), "CONFORMANCE-DRAW-INDEXED");
            } else
                core::requireSuccessfulResult(encoder.draw(3, 1, 0, 0), "CONFORMANCE-DRAW");
        });
    }
}
} // namespace arp::render_graph
