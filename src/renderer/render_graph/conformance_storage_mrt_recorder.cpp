#include "renderer/render_graph/conformance_storage_mrt_recorder.h"

#include "core/diagnostics/diagnostic_error.h"
#include "rhi/conformance/storage_mrt_pipeline.h"
#include "rhi/gpu_device.h"

namespace arp::render_graph {
using namespace rhi;

struct ConformanceStorageMrtRecorder::PreparedFrame {
    Extent2D extent;
    std::shared_ptr<GpuBuffer> input;
    std::shared_ptr<GpuTexture> color1, storage;
    core::UniquePtr<GpuTextureView> storageView, sampledView;
    core::UniquePtr<DescriptorArena> arena;
    core::UniquePtr<DescriptorSet> descriptors, computeDescriptors;
};

ConformanceStorageMrtRecorder::ConformanceStorageMrtRecorder(ConstructionKey,
    std::shared_ptr<RenderPipeline> pipeline,
    std::shared_ptr<ComputePipeline> compute,
    std::shared_ptr<core::CpuMemoryQuota> quota)
    : pipeline_(std::move(pipeline)), compute_(std::move(compute)), quota_(std::move(quota))
{}

ConformanceStorageMrtRecorder::~ConformanceStorageMrtRecorder() = default;

PipelineLayoutDesc ConformanceStorageMrtRecorder::layoutDefinition(const ShaderPackageDesc& package)
{
    return ConformanceStorageMrtPipeline::layoutDefinition(package);
}

RenderPipelineDesc ConformanceStorageMrtRecorder::pipelineDefinition(
    const PipelineLayout& layout, const ShaderPackageDesc& package, TextureFormat format)
{
    return ConformanceStorageMrtPipeline::pipelineDefinition(layout, package, format);
}

core::UniquePtr<ConformanceStorageMrtRecorder> ConformanceStorageMrtRecorder::prepare(
    GpuDevice& device,
    std::shared_ptr<RenderPipeline> pipeline,
    std::shared_ptr<ComputePipeline> compute,
    const ConformanceStorageMrtWorkload& workload,
    const ConformanceRecorderDesc& desc,
    core::pmr::String& diagnostic,
    OriginalConformanceStorageMrtExecution* original)
{
    diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-PREPARATION-CONTRACT";
    if (desc.contractVersion != 1 || !desc.prototypeOptIn || !desc.maxPreparedFrames
        || !desc.maxCpuMetadataBytes || !desc.quota
        || desc.quota->limitBytes() != desc.maxCpuMetadataBytes
        || workload.frames().size() > desc.maxPreparedFrames)
        return {};
    if (const auto* id = workload.executionDiagnostic(device.capabilities())) {
        diagnostic = id;
        return {};
    }
    if (original
        && (!original->run() || original->run()->workloadSha256() != workload.sourceSha256())) {
        diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-ORIGINAL-WORKLOAD";
        return {};
    }
    diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-SHADER-IDENTITY";
    if (!pipeline || !pipeline->layout() || !compute || !compute->layout())
        return {};
    const auto& reflection = pipeline->shaderReflection();
    if (reflection.sourceSha256 != workload.shaderSourceSha256())
        return {};
    diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-SHADER-IDENTITY";
    if (compute->shaderReflection().sourceSha256 != workload.computeSourceSha256())
        return {};
    diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-PIPELINE-ABI";
    if (!pipeline->validationReport().success || pipeline->validationReport().colorTargetCount != 2
        || pipeline->validationReport().colorFormats.size() != 2
        || pipeline->validationReport().colorFormats[1] != TextureFormat::Rgba8Unorm
        || !compute->validationReport().success)
        return {};
    diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-DESCRIPTOR-ABI";
    if (reflection.pushConstantByteCount || reflection.descriptorBindings.size() != 2
        || pipeline->layout()->desc().descriptorLayouts.size() != 1
        || compute->shaderReflection().pushConstantByteCount
        || compute->shaderReflection().descriptorBindings.size() != 2
        || compute->layout()->desc().descriptorLayouts.size() != 1)
        return {};
    const auto& bindings = pipeline->layout()->desc().descriptorLayouts[0].bindings;
    const auto& computeBindings = compute->layout()->desc().descriptorLayouts[0].bindings;
    if (bindings.size() != 2 || computeBindings.size() != 2)
        return {};
    for (const auto& b : bindings) {
        if (b.arrayCount != 1 || b.stages != ShaderStage::Fragment || b.dynamicOffset
            || b.tableBacked)
            return {};
        if (b.binding == 0 && b.type == DescriptorType::ReadOnlyStorageBuffer)
            continue;
        if (b.binding == 1 && b.type == DescriptorType::SampledTexture
            && !b.sampledTextureFilterable)
            continue;
        return {};
    }
    for (const auto& b : computeBindings) {
        if (b.arrayCount != 1 || b.stages != ShaderStage::Compute || b.dynamicOffset
            || b.tableBacked)
            return {};
        if (b.binding == 0 && b.type == DescriptorType::ReadOnlyStorageBuffer)
            continue;
        if (b.binding == 1 && b.type == DescriptorType::StorageTexture
            && b.storageTextureFormat == TextureFormat::Rgba16Float
            && b.storageTextureAccess == StorageTextureAccess::WriteOnly)
            continue;
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(desc.quota);
        auto result = core::makeUnique<ConformanceStorageMrtRecorder>(
            ConstructionKey{}, pipeline, compute, desc.quota);
        result->frames_.reserve(workload.frames().size());
        std::size_t frameIndex = 0;
        const char* originalDiagnostic = nullptr;
        for (const auto& input : workload.frames()) {
            auto frame = core::makeShared<PreparedFrame>();
            frame->extent = input.extent;
            frame->input = original
                ? original->prepareStorageInput(frameIndex, originalDiagnostic)
                : core::shareUnique(device.createBuffer({"Conformance.StorageInput",
                      sizeof(input.values),
                      ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
                      MemoryUsage::CpuToGpu,
                      ResourceState::ShaderRead}));
            if (!frame->input)
                throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-BUFFER-CREATION");
            if (!original)
                core::requireSuccessfulResult(
                    device.writeBuffer(*frame->input, input.values.data(), sizeof(input.values)),
                    "CONFORMANCE-STORAGE-UPLOAD");
            frame->color1 = original
                ? original->prepareStorageTexture(
                      ConformanceOutputRole::Color1, frameIndex, originalDiagnostic)
                : core::shareUnique(device.createTexture({"Conformance.MrtColor1",
                      {input.extent.width, input.extent.height, 1},
                      TextureFormat::Rgba8Unorm,
                      TextureUsage::RenderAttachment,
                      MemoryUsage::GpuOnly,
                      ResourceState::Undefined}));
            frame->storage = original
                ? original->prepareStorageTexture(
                      ConformanceOutputRole::Storage, frameIndex, originalDiagnostic)
                : core::shareUnique(device.createTexture({"Conformance.StorageOutput",
                      {input.extent.width, input.extent.height, 1},
                      TextureFormat::Rgba16Float,
                      TextureUsage::Storage | TextureUsage::Sampled,
                      MemoryUsage::GpuOnly,
                      ResourceState::Undefined}));
            if (!frame->color1 || !frame->storage)
                throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-TEXTURE-CREATION");
            TextureViewDesc view;
            view.format = TextureFormat::Rgba16Float;
            view.usage = TextureUsage::Storage;
            view.aspect = TextureAspect::Color;
            frame->storageView = device.createTextureView(*frame->storage, view);
            view.usage = TextureUsage::Sampled;
            frame->sampledView = device.createTextureView(*frame->storage, view);
            frame->arena =
                device.createDescriptorArena({"Conformance.StorageMrtDescriptors", 2, 4});
            if (!frame->storageView || !frame->sampledView || !frame->arena)
                throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-DESCRIPTOR-CREATION");
            DescriptorSetDesc set;
            set.layout = pipeline->layout()->desc().descriptorLayouts[0];
            frame->descriptors = frame->arena->allocateDescriptorSet(set);
            if (!frame->descriptors)
                throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-DESCRIPTOR-SET");
            core::requireSuccessfulResult(frame->descriptors->write({0,
                                              DescriptorType::ReadOnlyStorageBuffer,
                                              frame->input->id(),
                                              DescriptorResourceKind::Buffer}),
                "CONFORMANCE-STORAGE-BINDING");
            core::requireSuccessfulResult(frame->descriptors->write({1,
                                              DescriptorType::SampledTexture,
                                              frame->sampledView->id(),
                                              DescriptorResourceKind::TextureView}),
                "CONFORMANCE-STORAGE-SAMPLED-BINDING");
            set.layout = compute->layout()->desc().descriptorLayouts[0];
            frame->computeDescriptors = frame->arena->allocateDescriptorSet(set);
            if (!frame->computeDescriptors)
                throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-SET");
            core::requireSuccessfulResult(frame->computeDescriptors->write({0,
                                              DescriptorType::ReadOnlyStorageBuffer,
                                              frame->input->id(),
                                              DescriptorResourceKind::Buffer}),
                "CONFORMANCE-STORAGE-COMPUTE-BUFFER");
            core::requireSuccessfulResult(frame->computeDescriptors->write({1,
                                              DescriptorType::StorageTexture,
                                              frame->storageView->id(),
                                              DescriptorResourceKind::TextureView}),
                "CONFORMANCE-STORAGE-COMPUTE-IMAGE");
            result->frames_.push_back(std::move(frame));
            ++frameIndex;
        }
        diagnostic.clear();
        return result;
    } catch (const core::DiagnosticError& error) {
        diagnostic = error.what();
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-CPU-ALLOCATION";
    }
    return {};
}

void ConformanceStorageMrtRecorder::record(
    std::size_t index, FrameGraph& graph, TextureHandle target)
{
    if (index >= frames_.size())
        throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-FRAME-INDEX");
    auto frame = frames_[index];
    if (graph.texture(target).desc().format != pipeline_->validationReport().colorFormats[0])
        throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-FRAME-FORMAT");
    const auto extent = graph.texture(target).desc().extent;
    if (extent.width != frame->extent.width || extent.height != frame->extent.height)
        throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-FRAME-EXTENT");
    const auto color1 = graph.importTextureRef(frame->color1);
    const auto storage = graph.importTextureRef(frame->storage);
    const auto input = graph.importBufferRef(frame->input);
    graph.addPass("Conformance.StorageFill")
        .kind(PassKind::Compute)
        .access(input,
            {ResourceState::ShaderRead,
                PipelineStage::ComputeShader,
                rhi::ResourceAccess::ShaderRead},
            0,
            sizeof(std::array<float, 12>))
        .access(storage,
            {ResourceState::ShaderWrite,
                PipelineStage::ComputeShader,
                rhi::ResourceAccess::ShaderWrite},
            defaultTextureSubresourceRange(frame->storage->desc()))
        .execute([frame, compute = compute_](RenderContext& context) {
            auto encoder = context.commands().beginComputePass({"Conformance.StorageFill"});
            if (!encoder)
                throw core::DiagnosticError("RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-PASS");
            core::requireSuccessfulResult(
                encoder->setPipeline(*compute), "CONFORMANCE-STORAGE-COMPUTE-PIPELINE");
            core::requireSuccessfulResult(encoder->bindDescriptorSet(0, *frame->computeDescriptors),
                "CONFORMANCE-STORAGE-COMPUTE-DESCRIPTOR");
            core::requireSuccessfulResult(
                encoder->dispatch(frame->extent.width, frame->extent.height, 1),
                "CONFORMANCE-STORAGE-DISPATCH");
            core::requireSuccessfulResult(encoder->end(), "CONFORMANCE-STORAGE-COMPUTE-END");
        });
    graph.addPass("Conformance.StorageMrt")
        .access(input,
            {ResourceState::ShaderRead,
                PipelineStage::FragmentShader,
                rhi::ResourceAccess::ShaderRead},
            0,
            sizeof(std::array<float, 12>))
        .access(storage,
            {ResourceState::ShaderRead,
                PipelineStage::FragmentShader,
                rhi::ResourceAccess::ShaderRead},
            defaultTextureSubresourceRange(frame->storage->desc()))
        .writeAttachment(
            target, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store, {}, {0, 0, 0, 1})
        .writeAttachment(
            color1, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store, {}, {0, 0, 0, 1})
        .executeRenderPass([frame, pipeline = pipeline_](RenderPassContext& context) {
            auto& encoder = context.renderPass();
            core::requireSuccessfulResult(
                encoder.setPipeline(*pipeline), "CONFORMANCE-STORAGE-PIPELINE");
            core::requireSuccessfulResult(
                encoder.setViewport(
                    {0, 0, float(frame->extent.width), float(frame->extent.height), 0, 1}),
                "CONFORMANCE-STORAGE-VIEWPORT");
            core::requireSuccessfulResult(
                encoder.setScissor({0, 0, frame->extent.width, frame->extent.height}),
                "CONFORMANCE-STORAGE-SCISSOR");
            core::requireSuccessfulResult(encoder.bindDescriptorSet(0, *frame->descriptors),
                "CONFORMANCE-STORAGE-DESCRIPTOR");
            core::requireSuccessfulResult(encoder.draw(3, 1, 0, 0), "CONFORMANCE-STORAGE-DRAW");
        });
}
} // namespace arp::render_graph
