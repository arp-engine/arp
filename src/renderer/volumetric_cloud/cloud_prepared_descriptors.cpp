#include "renderer/volumetric_cloud/cloud_prepared_descriptors.h"

namespace arp::renderer::cloud {
namespace {
using namespace rhi;
using Assembly = OriginalCommandSourceAssembly;
using Element = OriginalCommandDescriptorSetElement;
using Buffer = OriginalCommandBufferLease;

bool lookupMiss(const char* id) noexcept
{
    return id
        && (std::string_view(id) == "RHI-ORIGINAL-SOURCES-OBSERVATION-INDEX"
            || std::string_view(id) == "RHI-ORIGINAL-SOURCES-OBSERVATION-KIND");
}

const char* bufferElement(const CloudPreparedDescriptorInput& c,
    const std::shared_ptr<const Buffer>& expected,
    std::size_t index,
    std::uint64_t bytes,
    DescriptorType type,
    Element& e) noexcept
{
    std::shared_ptr<const Buffer> source;
    const auto* id = Assembly::inspectBuffer(c.sources, c.device, index, source);
    if (lookupMiss(id) || (!id && !core::SharedOwnerProjection::sameOwner(source, expected)))
        return "CLOUD-PREPARED-DESCRIPTORS-BUFFER-POSITION";
    if (id)
        return id;
    const auto& object = *source->originalObject();
    const auto usage = type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                             : ResourceUsage::StorageBuffer;
    if (object.desc().sizeBytes != bytes || (object.desc().usage & usage) == ResourceUsage::None
        || !object.hostWritesSealed()
        || (type == DescriptorType::StorageBuffer && object.gpuWritesSealed()))
        return "CLOUD-PREPARED-DESCRIPTORS-BUFFER-RANGE";
    e.sourceIndex = index;
    e.write.type = type;
    e.write.resource = source->resource();
    e.write.resourceKind = DescriptorResourceKind::Buffer;
    e.write.resourceState = type == DescriptorType::StorageBuffer ? ResourceState::ShaderWrite
                                                                  : ResourceState::ShaderRead;
    e.write.bufferBindingRange = {2u, 0u, bytes};
    e.bufferAccessBytes = bytes;
    e.registrationGeneration = object.originalResourceRegistration().generation();
    e.access = type == DescriptorType::StorageBuffer
        ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
        : ResourceAccess::ShaderRead;
    return nullptr;
}

const char* textureElement(
    const CloudPreparedDescriptorInput& c, std::size_t index, bool depth, Element& e) noexcept
{
    Assembly::TextureViewObservation source;
    const auto* id = Assembly::inspectTextureView(c.sources, c.device, index, source);
    if (lookupMiss(id))
        return "CLOUD-PREPARED-DESCRIPTORS-TEXTURE-POSITION";
    if (id)
        return id;
    const auto& view = *source.view->originalObject();
    const auto& texture = *source.parent->originalObject();
    const auto& p = c.resources.plan().raw;
    const auto& d = view.desc();
    const auto format = depth ? TextureFormat::D32Float : TextureFormat::Rgba16Float;
    const auto aspect = depth ? TextureAspect::Depth : TextureAspect::Color;
    if (texture.desc().extent != Extent3D{p.extent.width, p.extent.height, 1u}
        || texture.desc().format != format || texture.desc().sampleCount != 1u
        || texture.desc().mipLevelCount != 1u || texture.desc().arrayLayerCount != 1u
        || (texture.desc().usage & TextureUsage::Sampled) == TextureUsage::None
        || d.format != format || d.aspect != aspect
        || d.dimension != TextureViewDimension::Texture2D || d.baseMipLevel || d.mipLevelCount != 1u
        || d.baseArrayLayer || d.arrayLayerCount != 1u
        || (d.usage & TextureUsage::Sampled) == TextureUsage::None)
        return "CLOUD-PREPARED-DESCRIPTORS-TEXTURE-SHAPE";
    e.sourceIndex = index;
    e.write.type = DescriptorType::SampledTexture;
    e.write.resource = source.view->resource();
    e.write.resourceKind = DescriptorResourceKind::TextureView;
    e.write.resourceState = ResourceState::ShaderRead;
    e.registrationGeneration = view.originalResourceRegistration().generation();
    e.access = ResourceAccess::ShaderRead;
    return nullptr;
}
} // namespace

CloudResult CloudPreparedDescriptors::describe(
    const CloudPreparedDescriptorInput& c, CloudPreparedDescriptorDeclarations& output) noexcept
{
    output = {};
    CloudPreparedFrameBuffers frame;
    if (const auto result = c.resources.pendingBuffers(frame); !result.success)
        return result;
    if (const auto result = CloudPreparedPipelines::validateSelection(c.resources.selection());
        !result.success)
        return result;
    const bool spatial = c.resources.selection().isReconstruction();
    if (bool(frame.reconstructionConstants) != spatial
        || (!spatial && c.locations.reconstructionConstants != SIZE_MAX))
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-POLICY");
    if (c.locations.color == c.locations.depth)
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-TEXTURE-ALIAS");
    CloudPreparedDescriptorDeclarations next;
    const auto& p = c.resources.plan();
    const auto resolvedBytes =
        spatial ? p.reconstruction.resolvedBufferBytes : p.raw.sampleBufferBytes;
    const auto densityBytes = frame.density->originalObject()->desc().sizeBytes;
    const auto& l = c.locations;
    const auto buffer = [&](unsigned pass,
                            unsigned binding,
                            const auto& lease,
                            std::size_t index,
                            std::uint64_t bytes,
                            DescriptorType type) {
        return bufferElement(c, lease, index, bytes, type, next.passes[pass].elements[binding]);
    };
    const auto texture = [&](unsigned pass, unsigned binding, std::size_t index, bool depth) {
        return textureElement(c, index, depth, next.passes[pass].elements[binding]);
    };
    using D = DescriptorType;
    for (unsigned pass = 0u; pass < kCloudPipelineCount; ++pass) {
        next.passes[pass].count = spatial && pass == 1u ? 7u : 4u;
        if (const auto* id = buffer(pass,
                0u,
                frame.constants,
                l.constants,
                sizeof(CloudGpuConstants),
                D::UniformBuffer))
            return CloudResult::failure(id);
    }
    for (const auto* id :
        {buffer(0u, 1u, frame.density, l.density, densityBytes, D::ReadOnlyStorageBuffer),
            texture(0u, 2u, l.depth, true),
            buffer(0u, 3u, frame.raw, l.raw, p.raw.sampleBufferBytes, D::StorageBuffer),
            buffer(1u, 1u, frame.raw, l.raw, p.raw.sampleBufferBytes, D::ReadOnlyStorageBuffer),
            buffer(1u, 2u, frame.previous, l.previous, resolvedBytes, D::ReadOnlyStorageBuffer),
            buffer(1u, 3u, frame.resolved, l.resolved, resolvedBytes, D::StorageBuffer),
            buffer(2u, 1u, frame.resolved, l.resolved, resolvedBytes, D::ReadOnlyStorageBuffer),
            texture(2u, 2u, l.color, false),
            texture(2u, 3u, l.depth, true)})
        if (id)
            return CloudResult::failure(id);
    if (spatial)
        for (const auto* id :
            {buffer(1u, 4u, frame.density, l.density, densityBytes, D::ReadOnlyStorageBuffer),
                texture(1u, 5u, l.depth, true),
                buffer(1u,
                    6u,
                    frame.reconstructionConstants,
                    l.reconstructionConstants,
                    sizeof(CloudReconstructionGpuConstants),
                    D::UniformBuffer)})
            if (id)
                return CloudResult::failure(id);
    for (unsigned pass = 0u; pass < kCloudPipelineCount; ++pass)
        for (unsigned i = 0u; i < next.passes[pass].count; ++i) {
            auto& e = next.passes[pass].elements[i];
            e.write.binding = i;
            e.stages = pass == 2u ? PipelineStage::FragmentShader : PipelineStage::ComputeShader;
        }
    next.historyGeneration = frame.historyGeneration;
    output = next;
    return {};
}

CloudResult CloudPreparedDescriptors::publish(
    const CloudPreparedDescriptorPublishContext& c, CloudPreparedDescriptorOutput& output)
{
    output = {};
    if (c.contractVersion != 1u || !c.prototypeOptIn)
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-OPT-IN");
    if (!c.inventory || c.inventory.use_count() != 1 || Assembly::sealed(*c.inventory))
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-EXCLUSIVE-CANDIDATE");
    if (const auto* id = Assembly::validate(c.inventory, c.device))
        return CloudResult::failure(id);
    CloudPreparedDescriptorDeclarations declarations;
    if (const auto result =
            describe({c.device, c.resources, c.inventory, c.locations}, declarations);
        !result.success)
        return result;
    const bool spatial = c.resources.selection().isReconstruction();
    std::shared_ptr<const ComputePipeline> ray, resolve;
    std::shared_ptr<const RenderPipeline> composite;
    for (const auto* id :
        {Assembly::inspectComputePipeline(c.inventory, c.device, c.pipelineSources[0], ray),
            Assembly::inspectComputePipeline(c.inventory, c.device, c.pipelineSources[1], resolve),
            Assembly::inspectGraphicsPipeline(
                c.inventory, c.device, c.pipelineSources[2], composite)})
        if (id)
            return CloudResult::failure(
                lookupMiss(id) ? "CLOUD-PREPARED-DESCRIPTORS-PIPELINE-POSITION" : id);
    if (c.pipelineSources[0] == c.pipelineSources[1])
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-PIPELINE-POSITION");
    const auto& selected = c.pipelines.selection();
    const auto& expected = c.resources.selection();
    if (!c.pipelines.created() || selected.contractVersion != expected.contractVersion
        || selected.resolve != expected.resolve
        || selected.reconstruction.contractVersion != expected.reconstruction.contractVersion
        || selected.reconstruction.maxRadianceRange != expected.reconstruction.maxRadianceRange
        || selected.reconstruction.maxTransmittanceRange
            != expected.reconstruction.maxTransmittanceRange
        || !core::SharedOwnerProjection::sameOwner(ray, c.pipelines.raymarch())
        || !core::SharedOwnerProjection::sameOwner(resolve, c.pipelines.resolve())
        || !core::SharedOwnerProjection::sameOwner(composite, c.pipelines.composite()))
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-PIPELINE-OWNER");
    const auto& rayPackage = ray->originalComputeCreation()->originalInput()->package();
    const auto& resolvePackage = resolve->originalComputeCreation()->originalInput()->package();
    const auto& compositePackage =
        composite->originalGraphicsCreation()->originalInput()->package();
    if (const auto result = CloudPreparedPipelines::validateInputs(
            c.resources.selection(), rayPackage, resolvePackage, compositePackage);
        !result.success)
        return result;
    for (const auto index : c.pipelineSources) {
        bool present = false;
        if (const auto* id = Assembly::descriptorForPipeline(c.inventory, c.device, index, present))
            return CloudResult::failure(id);
        if (present)
            return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-ALREADY-PUBLISHED");
    }
    if (!c.arena)
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-ARENA");
    if (const auto* id = c.device.validateOriginalResourceRegistration(*c.arena))
        return CloudResult::failure(id);
    const auto stats = c.arena->stats();
    if (stats.maxSets < kCloudPipelineCount || stats.maxDescriptors < (spatial ? 15u : 12u)
        || stats.allocatedSets || stats.allocatedTables || stats.allocatedDescriptors)
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-ARENA-CAPACITY");
    if (!c.maxCpuOwnerBytes || !c.ownerQuota || c.ownerQuota->limitBytes() != c.maxCpuOwnerBytes)
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-OWNER-QUOTA");
    for (std::size_t pass = 0u; pass < kCloudPipelineCount; ++pass) {
        const auto& request = c.requests[pass];
        if (request.contractVersion != (pass == 2u ? 3u : 4u)
            || request.maxBindings != declarations.passes[pass].count)
            return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-WRITE-PROFILE");
        if (const auto* id = OriginalGeneralDescriptorWriteContract::validate(request))
            return CloudResult::failure(id);
        for (std::size_t q = 0u; q < c.quotas[pass].size(); ++q)
            if (!c.quotas[pass][q] || c.quotas[pass][q]->limitBytes() != request.maxCpuBytes[q])
                return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-WRITE-QUOTA");
    }
    // The sole candidate leaves the caller before the first native operation.
    // Any failed prefix dies with this local inventory; no reset or retry of
    // a partially allocated arena changes the original publication contract.
    auto next = std::move(c.inventory);
    try {
        core::CpuMemoryQuotaScope scope(c.ownerQuota);
        auto arena = core::shareUnique(std::move(c.arena));
        const auto freeze = [&](const auto& pipeline, std::size_t pass) -> const char* {
            const auto& layouts = pipeline->originalLayoutOwner()->desc().descriptorLayouts;
            if (layouts.size() != 1u)
                return "CLOUD-PREPARED-DESCRIPTORS-PIPELINE-LAYOUT";
            auto set = c.device.allocateOriginalDescriptorSet(
                arena, {layouts.front().name, layouts.front()});
            if (!set)
                return "CLOUD-PREPARED-DESCRIPTORS-SET-CREATE";
            const auto& declaration = declarations.passes[pass];
            if (const auto* id = c.device.beginOriginalGeneralDescriptorWrite(*set,
                    pipeline,
                    next,
                    0u,
                    declaration.elements.data(),
                    declaration.count,
                    c.requests[pass],
                    c.quotas[pass]))
                return id;
            if (const auto* id = set->finishOriginalGeneralWrite())
                return id;
            OriginalCommandDescriptorSourceBudget budget{pass == 2u ? 5u : 7u,
                true,
                declaration.count,
                declaration.count,
                Assembly::finiteDescriptorRetainedOwners(),
                Assembly::quota(*next)->limitBytes()};
            budget.finitePipelineSourceIndex = c.pipelineSources[pass];
            budget.finiteFootprintContractVersion = 2u;
            budget.finiteFullBoundViewOptIn = true;
            return Assembly::appendDescriptorSet(set,
                arena,
                next,
                declaration.elements.data(),
                declaration.count,
                budget,
                c.device,
                output.descriptors[pass]);
        };
        const auto fail = [&](const char* id) {
            output = {};
            return CloudResult::failure(id);
        };
        if (const auto* id = freeze(c.pipelines.raymarch(), 0u))
            return fail(id);
        if (const auto* id = freeze(c.pipelines.resolve(), 1u))
            return fail(id);
        if (const auto* id = freeze(c.pipelines.composite(), 2u))
            return fail(id);
        std::uint32_t owners = 0u;
        if (const auto* id = Assembly::cpuOwnerCount(next, c.device, 2u, owners))
            return fail(id);
        output.historyGeneration = declarations.historyGeneration;
        output.arena = std::move(arena);
        c.inventory = std::move(next);
        return {};
    } catch (const std::bad_alloc&) {
        output = {};
        return CloudResult::failure("CLOUD-PREPARED-DESCRIPTORS-ALLOCATION");
    }
}
} // namespace arp::renderer::cloud
