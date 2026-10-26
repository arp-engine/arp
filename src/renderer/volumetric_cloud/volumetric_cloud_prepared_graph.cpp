#include "renderer/render_graph/original_frame_graph_execution.h"
#include "renderer/volumetric_cloud/cloud_prepared_history.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"

namespace arp::renderer::cloud {
namespace {
using namespace rhi;
using Sources = OriginalCommandResourceSources;

const OriginalCommandDescriptorSetSource* descriptors(const Sources& sources, std::size_t index)
{
    if (index >= sources.sources().size())
        return nullptr;
    const auto* value = std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
        &sources.sources()[index]);
    return value && *value ? value->get() : nullptr;
}

bool bufferBinding(const Sources& sources,
    const OriginalCommandDescriptorSetSource& set,
    std::uint32_t binding,
    const GpuBuffer& buffer,
    std::uint64_t minimum,
    bool exact = false,
    bool allowOffset = false,
    std::uint64_t* matchedOffset = nullptr)
{
    for (const auto& element : set.frozenElements()) {
        if (element.declaration.write.binding != binding)
            continue;
        if (element.declaration.sourceIndex >= sources.sources().size())
            return false;
        const auto* owner = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
            &sources.sources()[element.declaration.sourceIndex]);
        const auto& e = element.declaration;
        const bool matches = owner && *owner && (*owner)->originalObject() == &buffer
            && e.bufferAccessBytes >= minimum
            && (!exact
                || ((allowOffset || e.bufferAccessOffset == 0u) && e.bufferAccessBytes == minimum
                    && e.write.bufferBindingRange.contractVersion == 2u
                    && e.write.bufferBindingRange.offsetBytes == e.bufferAccessOffset
                    && e.write.bufferBindingRange.sizeBytes == minimum));
        if (matches && matchedOffset)
            *matchedOffset = e.bufferAccessOffset;
        return matches;
    }
    return false;
}

bool textureBinding(const Sources& sources,
    const OriginalCommandDescriptorSetSource& set,
    std::uint32_t binding,
    const GpuTexture& texture)
{
    for (const auto& element : set.frozenElements()) {
        if (element.declaration.write.binding != binding)
            continue;
        if (element.declaration.sourceIndex >= sources.sources().size())
            return false;
        const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            &sources.sources()[element.declaration.sourceIndex]);
        return view && *view && (*view)->originalParentLease()->originalObject() == &texture;
    }
    return false;
}
} // namespace

CloudResult VolumetricCloudFeature::contributePreparedGraph(
    const CloudPreparedGraphContext& c, CloudOutputs& output)
{
    return contributePreparedGraph(
        c, output, c.history, c.history ? c.history->token() : render_graph::PassSubmissionToken{});
}

CloudResult VolumetricCloudFeature::contributePreparedGraph(const CloudPreparedGraphContext& c,
    CloudOutputs& output,
    render_graph::PassSubmissionObserver* observer,
    render_graph::PassSubmissionToken token)
{
    output = {};
    if ((c.contractVersion != 1u && c.contractVersion != 2u && c.contractVersion != 3u)
        || !c.prototypeOptIn)
        return CloudResult::failure("CLOUD-PREPARED-GRAPH-OPT-IN");
    if ((c.contractVersion == 1u && (c.history || c.historyGeneration))
        || (c.contractVersion >= 2u && !c.history)
        || ((c.contractVersion == 3u) != (c.reconstruction != nullptr)))
        return CloudResult::failure("CLOUD-PREPARED-GRAPH-HISTORY-VERSION");
    const auto& p = c.plan;
    const auto* reconstruction = c.reconstruction;
    const auto resolvedBytes =
        reconstruction ? reconstruction->resolvedBufferBytes : p.sampleBufferBytes;
    if (!p.extent.width || !p.extent.height || !p.sampleExtent.width || !p.sampleExtent.height
        || p.sampleCount != std::uint64_t(p.sampleExtent.width) * p.sampleExtent.height
        || p.sampleCount > UINT32_MAX || p.sampleBufferBytes != p.sampleCount * kCloudSampleBytes
        || p.dispatchGroups.width != 1u + (p.sampleExtent.width - 1u) / kCloudWorkgroupWidth
        || p.dispatchGroups.height != 1u + (p.sampleExtent.height - 1u) / kCloudWorkgroupHeight
        || p.dispatchGroups.depth != 1u)
        return CloudResult::failure("CLOUD-PREPARED-GRAPH-PLAN");
    if (reconstruction
        && (reconstruction->resolvedCount != std::uint64_t(p.extent.width) * p.extent.height
            || reconstruction->resolvedCount > UINT32_MAX
            || resolvedBytes != reconstruction->resolvedCount * kCloudSampleBytes
            || p.sampleExtent.width != 1u + (p.extent.width - 1u) / 2u
            || p.sampleExtent.height != 1u + (p.extent.height - 1u) / 2u
            || reconstruction->dispatchGroups
                != Extent3D{1u + (p.extent.width - 1u) / kCloudWorkgroupWidth,
                    1u + (p.extent.height - 1u) / kCloudWorkgroupHeight,
                    1u}))
        return CloudResult::failure("CLOUD-RECONSTRUCTION-GRAPH-PLAN");
    auto& graph = c.execution.graph();
    const auto& color = graph.texture(c.color);
    const auto& depth = graph.texture(c.depth);
    const auto& target = graph.texture(c.output);
    if (color.id() == target.id() || color.id() == depth.id() || depth.id() == target.id())
        return CloudResult::failure("CLOUD-PREPARED-GRAPH-ATTACHMENT-ALIAS");
    for (const auto* texture : {&color, &depth, &target})
        if (texture->desc().extent != Extent3D{p.extent.width, p.extent.height, 1u}
            || texture->desc().sampleCount != 1u)
            return CloudResult::failure("CLOUD-PREPARED-GRAPH-EXTENT");
    if (color.desc().format != TextureFormat::Rgba16Float
        || target.desc().format != TextureFormat::Rgba16Float
        || depth.desc().format != TextureFormat::D32Float)
        return CloudResult::failure("CLOUD-PREPARED-GRAPH-FORMAT");
    if (c.history)
        if (const auto* id = c.history->validateGraph(c))
            return CloudResult::failure(id);
    const auto& sources = c.execution.sources();
    std::array<const OriginalCommandDescriptorSetSource*, kCloudPipelineCount> sets{};
    for (std::size_t i = 0u; i < sets.size(); ++i) {
        sets[i] = descriptors(sources, c.descriptors[i]);
        if (!sets[i] || sets[i]->sourceContractVersion() != (i == 2u ? 5u : 7u)
            || sets[i]->frozenElements().size() != (reconstruction && i == 1u ? 7u : 4u)
            || sets[i]->frozenPipelineSourceIndex() != c.pipelines[i]
            || c.pipelines[i] >= sources.sources().size()
            || (i == 2u ? !std::holds_alternative<
                              std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
                              sources.sources()[c.pipelines[i]])
                        : !std::holds_alternative<
                              std::shared_ptr<const OriginalCommandComputePipelineSource>>(
                              sources.sources()[c.pipelines[i]]))
            || !bufferBinding(sources,
                *sets[i],
                0u,
                graph.buffer(c.constants),
                sizeof(CloudGpuConstants),
                c.history != nullptr))
            return CloudResult::failure("CLOUD-PREPARED-GRAPH-DESCRIPTOR");
    }
    const auto densityBytes = c.history ? c.history->densityBytes() : sizeof(core::Vec4);
    std::uint64_t densityOffset = 0u, resolveDensityOffset = 0u;
    if (!bufferBinding(sources,
            *sets[0],
            1u,
            graph.buffer(c.density),
            densityBytes,
            c.history != nullptr,
            true,
            &densityOffset)
        || !textureBinding(sources, *sets[0], 2u, depth)
        || !bufferBinding(
            sources, *sets[0], 3u, graph.buffer(c.raw), p.sampleBufferBytes, c.history != nullptr)
        || !bufferBinding(
            sources, *sets[1], 1u, graph.buffer(c.raw), p.sampleBufferBytes, c.history != nullptr)
        || !bufferBinding(
            sources, *sets[1], 2u, graph.buffer(c.previous), resolvedBytes, c.history != nullptr)
        || !bufferBinding(
            sources, *sets[1], 3u, graph.buffer(c.resolved), resolvedBytes, c.history != nullptr)
        || !bufferBinding(
            sources, *sets[2], 1u, graph.buffer(c.resolved), resolvedBytes, c.history != nullptr)
        || !textureBinding(sources, *sets[2], 2u, color)
        || !textureBinding(sources, *sets[2], 3u, depth))
        return CloudResult::failure("CLOUD-PREPARED-GRAPH-RESOURCE-BINDING");
    if (reconstruction
        && (!bufferBinding(sources,
                *sets[1],
                4u,
                graph.buffer(c.density),
                densityBytes,
                true,
                true,
                &resolveDensityOffset)
            || resolveDensityOffset != densityOffset
            || !textureBinding(sources, *sets[1], 5u, depth)
            || !bufferBinding(sources,
                *sets[1],
                6u,
                graph.buffer(c.reconstructionConstants),
                sizeof(CloudReconstructionGpuConstants),
                true)))
        return CloudResult::failure("CLOUD-RECONSTRUCTION-GRAPH-RESOURCE-BINDING");
    const auto depthRange = defaultTextureSubresourceRange(depth.desc());
    c.execution.compute("Cloud.Raymarch", c.pipelines[0], c.descriptors[0], p.dispatchGroups)
        .read(c.constants, ResourceState::ShaderRead)
        .read(c.density, ResourceState::ShaderRead)
        .access(c.depth,
            {ResourceState::ShaderRead,
                PipelineStage::ComputeShader | PipelineStage::FragmentShader,
                rhi::ResourceAccess::ShaderRead},
            depthRange)
        .write(c.raw, ResourceState::ShaderWrite);
    auto resolve = c.execution
                       .compute("Cloud.Temporal",
                           c.pipelines[1],
                           c.descriptors[1],
                           reconstruction ? reconstruction->dispatchGroups : p.dispatchGroups)
                       .read(c.constants, ResourceState::ShaderRead)
                       .read(c.raw, ResourceState::ShaderRead)
                       .read(c.previous, ResourceState::ShaderRead)
                       .write(c.resolved, ResourceState::ShaderWrite);
    if (reconstruction)
        resolve.read(c.reconstructionConstants, ResourceState::ShaderRead)
            .read(c.density, ResourceState::ShaderRead)
            .access(c.depth,
                {ResourceState::ShaderRead,
                    PipelineStage::ComputeShader | PipelineStage::FragmentShader,
                    rhi::ResourceAccess::ShaderRead},
                depthRange);
    auto composite =
        c.execution
            .graphics("Cloud.Composite",
                c.pipelines[2],
                c.descriptors[2],
                {0.f, 0.f, float(p.extent.width), float(p.extent.height), 0.f, 1.f},
                {0, 0, p.extent.width, p.extent.height},
                {3u, 1u, 0u, 0u})
            .read(c.constants, ResourceState::ShaderRead)
            .read(c.resolved, ResourceState::ShaderRead)
            .read(c.color, ResourceState::ShaderRead)
            .read(c.depth, ResourceState::ShaderRead)
            .writeAttachment(
                c.output, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store);
    if (observer)
        composite.onSubmitted(*observer, token);
    output = {c.output,
        c.resolved,
        c.constants,
        c.history && c.history->pendingFrame().historyUsed,
        reconstruction ? reconstruction->retainedGpuBytes : p.retainedGpuBytes};
    return {};
}
} // namespace arp::renderer::cloud
