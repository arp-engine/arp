#include "renderer/visibility/depth_pyramid_renderer.h"

#include "renderer/shader_package_reflection.h"

#include <cstring>
#include <iostream>

namespace arp::renderer::visibility {
namespace {

struct DepthPyramidPushConstants {
    std::uint32_t destinationWidth;
    std::uint32_t destinationHeight;
    std::uint32_t sourceWidth;
    std::uint32_t sourceHeight;
};

static_assert(sizeof(DepthPyramidPushConstants) == 16u);

core::pmr::String replaceExtension(const core::pmr::String& path, const char* extension)
{
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == core::pmr::String::npos || (slash != core::pmr::String::npos && dot < slash)) {
        return path + extension;
    }
    return path.substr(0u, dot) + extension;
}

rhi::DescriptorLayoutDesc descriptorLayout()
{
    auto depth = rhi::DescriptorBindingDesc{
        0u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Compute};
    depth.depthTexture = true;
    return {
        "DepthPyramidSet",
        {
            depth,
            {1u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Compute},
            {2u,
                rhi::DescriptorType::StorageTexture,
                rhi::ShaderStage::Compute,
                1u,
                false,
                false,
                false,
                rhi::TextureViewDimension::Texture2D,
                false,
                rhi::TextureFormat::Rgba16Float},
        },
    };
}

RendererResult failed(const char* diagnostic)
{
    return {false, diagnostic};
}

std::uint32_t divideRoundUp(std::uint32_t value, std::uint32_t divisor) noexcept
{
    return (value + divisor - 1u) / divisor;
}

} // namespace

RendererResult DepthPyramidRenderer::create(const DepthPyramidCreateContext& context)
{
    release();
    if (!validDepthPyramidExtent(context.extent)) {
        return failed("Depth pyramid requires a non-zero source extent");
    }
    if (context.shaderPath.empty()) {
        return failed("Depth pyramid requires generated Slang shader artifacts");
    }
    const auto& capabilities = context.device.capabilities();
    if (!rhi::isFeatureSupported(capabilities, rhi::EngineFeature::ComputeDispatch)
        || !rhi::isFeatureSupported(capabilities, rhi::EngineFeature::StorageTextureBinding)
        || !rhi::isFeatureSupported(capabilities, rhi::EngineFeature::Rgba16FloatStorageTexture)) {
        return failed("Depth pyramid requires compute and Rgba16Float storage-texture capability");
    }

    const auto levels = depthPyramidMipLevelCount(context.extent);
    if (levels > kMaxMipLevels) {
        return failed("Depth pyramid mip chain exceeds the portable renderer limit");
    }
    device_ = &context.device;
    extent_ = context.extent;
    mipLevelCount_ = levels;

    const auto execution = createExecutionResources(context.shaderPath);
    if (!execution.success) {
        release();
        return execution;
    }
    const auto textureCreated = createPyramidTexture();
    if (!textureCreated.success) {
        release();
        return textureCreated;
    }
    return {true, ""};
}

RendererResult DepthPyramidRenderer::createExecutionResources(const core::pmr::String& shaderPath)
{
    const auto layout = descriptorLayout();
    rhi::PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.descriptorLayouts.push_back(layout);
    pipelineLayoutDesc.descriptorSets.push_back({0u, layout.name});
    pipelineLayoutDesc.pushConstantRanges.push_back(
        {rhi::ShaderStage::Compute, 0u, sizeof(DepthPyramidPushConstants)});
    pipelineLayout_ = device_->createPipelineLayout(pipelineLayoutDesc);
    if (!pipelineLayout_) {
        return failed("Depth pyramid pipeline-layout creation failed");
    }

    shaderPackage_.label = "DepthPyramid";
    shaderPackage_.slangModulePath = shaderPath;
    shaderPackage_.entrypoints.push_back({shaderPath,
        "depthPyramidCopyMain",
        rhi::ShaderStage::Compute,
        rhi::ShaderSourceLanguage::Slang});
    shaderPackage_.entrypoints.push_back({shaderPath,
        "depthPyramidReduceMain",
        rhi::ShaderStage::Compute,
        rhi::ShaderSourceLanguage::Slang});
    shaderPackage_.artifacts.spirvPath = replaceExtension(shaderPath, ".spv");
    shaderPackage_.artifacts.mslPath = replaceExtension(shaderPath, ".metal");
    shaderPackage_.artifacts.wgslPath = replaceExtension(shaderPath, ".wgsl");
    shaderPackage_.artifacts.reflectionPath = replaceExtension(shaderPath, ".reflection.yaml");
    if (!hydrateShaderPackageReflectionFromArtifact(shaderPackage_)) {
        return failed("Depth pyramid generated shader reflection could not be hydrated");
    }
    for (const auto& binding : layout.bindings) {
        shaderPackage_.reflection.descriptorBindings.push_back(
            rhi::makeShaderReflectionBinding(0u, binding));
    }
    const auto shaderValidation = rhi::validateShaderPackage(shaderPackage_);
    if (!shaderValidation.success) {
        return {false, shaderValidation.diagnostics};
    }

    rhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.label = "DepthPyramidCopyPipeline";
    pipelineDesc.layout = pipelineLayout_.get();
    pipelineDesc.shaderPackage = &shaderPackage_;
    pipelineDesc.computeShader = shaderPackage_.entrypoints[0];
    copyPipeline_ = device_->createComputePipeline(pipelineDesc);
    if (!copyPipeline_) {
        return failed("Depth pyramid copy pipeline creation failed");
    }
    pipelineDesc.label = "DepthPyramidReducePipeline";
    pipelineDesc.computeShader = shaderPackage_.entrypoints[1];
    reducePipeline_ = device_->createComputePipeline(pipelineDesc);
    if (!reducePipeline_) {
        return failed("Depth pyramid reduction pipeline creation failed");
    }

    descriptorArena_ = device_->createDescriptorArena({
        "DepthPyramidDescriptorArena",
        kFrameSlots * kMaxMipLevels,
        kFrameSlots * kMaxMipLevels * 3u,
    });
    if (!descriptorArena_) {
        return failed("Depth pyramid descriptor arena creation failed");
    }
    for (std::uint32_t frameSlot = 0u; frameSlot < kFrameSlots; ++frameSlot) {
        for (std::uint32_t mip = 0u; mip < kMaxMipLevels; ++mip) {
            descriptorSets_[frameSlot][mip] =
                descriptorArena_->allocateDescriptorSet({"DepthPyramidSet", layout});
            if (!descriptorSets_[frameSlot][mip]) {
                return failed("Depth pyramid descriptor-set allocation failed");
            }
        }
    }
    return {true, ""};
}

RendererResult DepthPyramidRenderer::createPyramidTexture()
{
    rhi::TextureDesc desc;
    desc.label = "DepthPyramid";
    desc.extent = {extent_.width, extent_.height, 1u};
    desc.format = rhi::TextureFormat::Rgba16Float;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
    desc.memory = rhi::MemoryUsage::GpuOnly;
    desc.initialState = rhi::ResourceState::Undefined;
    desc.mipLevelCount = mipLevelCount_;
    auto texture = device_->createTexture(desc);
    if (!texture) {
        return failed("Depth pyramid texture creation failed");
    }
    rhi::TextureViewDesc wholePyramid;
    wholePyramid.label = "DepthPyramidSampled";
    wholePyramid.format = rhi::TextureFormat::Rgba16Float;
    wholePyramid.usage = rhi::TextureUsage::Sampled;
    wholePyramid.dimension = rhi::TextureViewDimension::Texture2D;
    wholePyramid.baseMipLevel = 0u;
    wholePyramid.mipLevelCount = mipLevelCount_;
    wholePyramid.arrayLayerCount = 1u;
    wholePyramid.aspect = rhi::TextureAspect::Color;
    sampledTextureView_ = device_->createTextureView(*texture, wholePyramid);
    if (!sampledTextureView_) {
        return failed("Depth pyramid full sampled view creation failed");
    }
    for (std::uint32_t mip = 0u; mip < mipLevelCount_; ++mip) {
        rhi::TextureViewDesc sampled;
        sampled.label = "DepthPyramidSampledMip";
        sampled.format = rhi::TextureFormat::Rgba16Float;
        sampled.usage = rhi::TextureUsage::Sampled;
        sampled.dimension = rhi::TextureViewDimension::Texture2D;
        sampled.baseMipLevel = mip;
        sampled.mipLevelCount = 1u;
        sampled.arrayLayerCount = 1u;
        sampled.aspect = rhi::TextureAspect::Color;
        auto sampledView = device_->createTextureView(*texture, sampled);
        if (!sampledView) {
            return failed("Depth pyramid sampled-mip view creation failed");
        }
        rhi::TextureViewDesc storage = sampled;
        storage.label = "DepthPyramidStorageMip";
        storage.usage = rhi::TextureUsage::Storage;
        auto storageView = device_->createTextureView(*texture, storage);
        if (!storageView) {
            return failed("Depth pyramid storage-mip view creation failed");
        }
        sampledViews_[mip] = std::move(sampledView);
        storageViews_[mip] = std::move(storageView);
    }
    if (!copyDummySampledTexture_) {
        rhi::TextureDesc dummyDesc;
        dummyDesc.label = "DepthPyramidCopyDummySampled";
        dummyDesc.extent = {1u, 1u, 1u};
        dummyDesc.format = rhi::TextureFormat::Rgba16Float;
        dummyDesc.usage = rhi::TextureUsage::Sampled;
        dummyDesc.memory = rhi::MemoryUsage::GpuOnly;
        dummyDesc.initialState = rhi::ResourceState::Undefined;
        dummyDesc.mipLevelCount = 1u;
        dummyDesc.arrayLayerCount = 1u;
        auto dummyTexture = device_->createTexture(dummyDesc);
        if (!dummyTexture) {
            return failed("Depth pyramid copy dummy sampled texture creation failed");
        }
        rhi::TextureViewDesc dummyViewDesc;
        dummyViewDesc.label = "DepthPyramidCopyDummySampledView";
        dummyViewDesc.format = rhi::TextureFormat::Rgba16Float;
        dummyViewDesc.usage = rhi::TextureUsage::Sampled;
        dummyViewDesc.dimension = rhi::TextureViewDimension::Texture2D;
        dummyViewDesc.mipLevelCount = 1u;
        dummyViewDesc.arrayLayerCount = 1u;
        dummyViewDesc.aspect = rhi::TextureAspect::Color;
        auto dummyView = device_->createTextureView(*dummyTexture, dummyViewDesc);
        if (!dummyView) {
            return failed("Depth pyramid copy dummy sampled view creation failed");
        }
        copyDummySampledTexture_ = std::shared_ptr<rhi::GpuTexture>(std::move(dummyTexture));
        copyDummySampledView_ = std::move(dummyView);
    }
    texture_ = std::shared_ptr<rhi::GpuTexture>(std::move(texture));
    return {true, ""};
}

RendererResult DepthPyramidRenderer::resize(DepthPyramidExtent extent)
{
    if (!created()) {
        return failed("Depth pyramid renderer must be created before resize");
    }
    if (!validDepthPyramidExtent(extent)) {
        return failed("Depth pyramid resize requires a non-zero source extent");
    }
    if (extent == extent_) {
        return {true, ""};
    }
    const auto levels = depthPyramidMipLevelCount(extent);
    if (levels > kMaxMipLevels) {
        return failed("Depth pyramid mip chain exceeds the portable renderer limit");
    }
    for (auto& view : sampledViews_)
        view.reset();
    for (auto& view : storageViews_)
        view.reset();
    sampledTextureView_.reset();
    texture_.reset();
    extent_ = extent;
    mipLevelCount_ = levels;
    return createPyramidTexture();
}

RendererResult DepthPyramidRenderer::contributeGraph(
    const DepthPyramidGraphContext& context, DepthPyramidGraphOutputs& outputs)
{
    if (!created() || !texture_) {
        return failed("Depth pyramid renderer must be created before graph contribution");
    }
    const auto sourceDesc = context.graph.textureDesc(context.sourceDepth);
    const DepthPyramidExtent sourceExtent{sourceDesc.extent.width, sourceDesc.extent.height};
    if (sourceDesc.format != rhi::TextureFormat::D32Float || sourceExtent != extent_) {
        return failed(
            "Depth pyramid source must be a D32Float texture matching the renderer extent");
    }
    const auto pyramid = context.graph.importTextureRef(texture_);
    const auto frameSlot = context.frameIndex % kFrameSlots;
    context.graph.addPass("DepthPyramid.Copy")
        .kind(render_graph::PassKind::Compute)
        .queue(render_graph::QueueRole::Compute)
        .asyncComputeEligible(false)
        .read(context.sourceDepth, rhi::ResourceState::ShaderRead)
        .write(pyramid, rhi::ResourceState::ShaderWrite)
        .execute([this, &graph = context.graph, source = context.sourceDepth, frameSlot](
                     render_graph::RenderContext& renderContext) {
            recordCopyPass(renderContext, graph.texture(source), frameSlot);
        });
    for (std::uint32_t destinationMip = 1u; destinationMip < mipLevelCount_; ++destinationMip) {
        const auto sourceMip = destinationMip - 1u;
        context.graph.addPass("DepthPyramid.Reduce")
            .kind(render_graph::PassKind::Compute)
            .queue(render_graph::QueueRole::Compute)
            .asyncComputeEligible(false)
            .access(pyramid,
                {rhi::ResourceState::ShaderWrite,
                    rhi::PipelineStage::ComputeShader,
                    rhi::ResourceAccess::ShaderRead},
                {sourceMip, 1u, 0u, 1u})
            .access(pyramid,
                {rhi::ResourceState::ShaderWrite,
                    rhi::PipelineStage::ComputeShader,
                    rhi::ResourceAccess::ShaderWrite},
                {destinationMip, 1u, 0u, 1u})
            .execute([this, sourceMip, destinationMip, frameSlot](
                         render_graph::RenderContext& renderContext) {
                recordReducePass(renderContext, sourceMip, destinationMip, frameSlot);
            });
    }
    outputs.texture = pyramid;
    outputs.mipLevelCount = mipLevelCount_;
    return {true, ""};
}

void DepthPyramidRenderer::recordCopyPass(
    render_graph::RenderContext& context, rhi::GpuTexture& sourceDepth, std::uint32_t frameSlot)
{
    rhi::TextureViewDesc sourceView;
    sourceView.label = "DepthPyramidSourceDepth";
    sourceView.format = sourceDepth.desc().format;
    sourceView.usage = rhi::TextureUsage::Sampled;
    sourceView.dimension = rhi::TextureViewDimension::Texture2D;
    sourceView.mipLevelCount = 1u;
    sourceView.arrayLayerCount = 1u;
    sourceView.aspect = rhi::TextureAspect::Depth;
    auto nextSourceView = context.device().createTextureView(sourceDepth, sourceView);
    if (!nextSourceView) {
        std::cerr << "[depth-pyramid] source depth view creation failed\n";
        return;
    }
    if (!copyDummySampledView_) {
        std::cerr << "[depth-pyramid] copy dummy sampled view is unavailable\n";
        return;
    }
    auto& descriptorSet = *descriptorSets_[frameSlot][0u];
    const auto depthWrite = descriptorSet.write({0u,
        rhi::DescriptorType::SampledTexture,
        nextSourceView->id(),
        rhi::DescriptorResourceKind::TextureView});
    // depthPyramidCopyMain does not consume PyramidSource, but the shared
    // descriptor layout requires binding 1. Binding the destination mip here
    // makes WebGPU reject the pass as simultaneous sampled/storage use.
    const auto sourceWrite = descriptorSet.write({1u,
        rhi::DescriptorType::SampledTexture,
        copyDummySampledView_->id(),
        rhi::DescriptorResourceKind::TextureView});
    const auto destinationWrite = descriptorSet.write({2u,
        rhi::DescriptorType::StorageTexture,
        storageViews_[0u]->id(),
        rhi::DescriptorResourceKind::TextureView});
    if (!depthWrite.success || !sourceWrite.success || !destinationWrite.success) {
        std::cerr << "[depth-pyramid] copy descriptor write failed\n";
        return;
    }
    sourceDepthViews_[frameSlot] = std::move(nextSourceView);
    const auto destination = depthPyramidMipExtent(extent_, 0u);
    const DepthPyramidPushConstants constants{
        destination.width, destination.height, destination.width, destination.height};
    auto pass = context.commands().beginComputePass({"DepthPyramid.Copy"});
    if (!pass)
        return;
    if (const auto* timingPool = context.gpuTimingPool()) {
        pass->writeTimestamp(*timingPool, context.gpuTimingBeginQuery());
    }
    pass->setPipeline(*copyPipeline_);
    pass->bindDescriptorSet(0u, descriptorSet);
    pass->pushConstants(rhi::ShaderStage::Compute, 0u, sizeof(constants), &constants);
    pass->dispatch(divideRoundUp(destination.width, kWorkgroupSize),
        divideRoundUp(destination.height, kWorkgroupSize),
        1u);
    if (const auto* timingPool = context.gpuTimingPool()) {
        pass->writeTimestamp(*timingPool, context.gpuTimingEndQuery());
    }
    pass->end();
}

void DepthPyramidRenderer::recordReducePass(render_graph::RenderContext& context,
    std::uint32_t sourceMip,
    std::uint32_t destinationMip,
    std::uint32_t frameSlot)
{
    auto& descriptorSet = *descriptorSets_[frameSlot][destinationMip];
    const auto depthWrite = descriptorSet.write({0u,
        rhi::DescriptorType::SampledTexture,
        sourceDepthViews_[frameSlot]->id(),
        rhi::DescriptorResourceKind::TextureView});
    auto sourceDescriptor = rhi::DescriptorBindingWrite{1u,
        rhi::DescriptorType::SampledTexture,
        sampledViews_[sourceMip]->id(),
        rhi::DescriptorResourceKind::TextureView};
    // The graph keeps the whole pyramid in ShaderWrite while reducing mips;
    // sampled loads therefore require Vulkan's GENERAL layout for this view.
    sourceDescriptor.resourceState = rhi::ResourceState::ShaderWrite;
    const auto sourceWrite = descriptorSet.write(sourceDescriptor);
    const auto destinationWrite = descriptorSet.write({2u,
        rhi::DescriptorType::StorageTexture,
        storageViews_[destinationMip]->id(),
        rhi::DescriptorResourceKind::TextureView});
    if (!depthWrite.success || !sourceWrite.success || !destinationWrite.success) {
        std::cerr << "[depth-pyramid] reduction descriptor write failed\n";
        return;
    }
    const auto source = depthPyramidMipExtent(extent_, sourceMip);
    const auto destination = depthPyramidMipExtent(extent_, destinationMip);
    const DepthPyramidPushConstants constants{
        destination.width, destination.height, source.width, source.height};
    auto pass = context.commands().beginComputePass({"DepthPyramid.Reduce"});
    if (!pass)
        return;
    pass->setPipeline(*reducePipeline_);
    pass->bindDescriptorSet(0u, descriptorSet);
    pass->pushConstants(rhi::ShaderStage::Compute, 0u, sizeof(constants), &constants);
    pass->dispatch(divideRoundUp(destination.width, kWorkgroupSize),
        divideRoundUp(destination.height, kWorkgroupSize),
        1u);
    pass->end();
}

void DepthPyramidRenderer::release() noexcept
{
    for (auto& frameSets : descriptorSets_) {
        for (auto& descriptorSet : frameSets)
            descriptorSet.reset();
    }
    for (auto& view : sourceDepthViews_)
        view.reset();
    for (auto& view : sampledViews_)
        view.reset();
    for (auto& view : storageViews_)
        view.reset();
    sampledTextureView_.reset();
    copyDummySampledView_.reset();
    copyDummySampledTexture_.reset();
    texture_.reset();
    descriptorArena_.reset();
    reducePipeline_.reset();
    copyPipeline_.reset();
    pipelineLayout_.reset();
    shaderPackage_ = {};
    mipLevelCount_ = 0u;
    extent_ = {};
    device_ = nullptr;
}

} // namespace arp::renderer::visibility
