#include "renderer/pipeline/scene_opaque_producer.h"

#include "core/ownership/shared_owner_projection.h"
#include "renderer/pipeline/detail/scene_raster_contract.h"

namespace arp::renderer {
namespace {
using namespace rhi;
using Lease = OriginalCommandBufferLease;

} // namespace

const char* SceneOpaqueGeometry::create(const scene3d::Scene3DRenderQueue& queue,
    core::ArrayView<SceneOpaqueMeshBinding> bindings,
    const resource::ShaderPackageResource& shader,
    std::string_view shaderId,
    const SceneOpaqueGeometryBudget& b,
    std::shared_ptr<const SceneOpaqueGeometry>& output)
try {
    if (b.contractVersion != 1u || !b.prototypeOptIn)
        return "SCENE-OPAQUE-GEOMETRY-OPT-IN";
    if (!b.maxItems || !b.maxExpandedVertices || !b.maxCpuBytes || !b.quota
        || b.quota->limitBytes() != b.maxCpuBytes)
        return "SCENE-OPAQUE-GEOMETRY-BUDGET";
    if (queue.items().empty() || queue.items().size() > b.maxItems
        || bindings.size() != queue.items().size())
        return "SCENE-OPAQUE-BINDING-COVERAGE";
    core::CpuMemoryQuotaScope scope(b.quota);
    auto next = core::makeShared<SceneOpaqueGeometry>();
    next->quota_ = b.quota;
    next->snapshots_.reserve(bindings.size());
    // Explicit item-index bindings are canonicalized by visible queue order.
    // No sort key, material name, resource path or handle arithmetic is inferred.
    for (std::size_t itemIndex = 0u; itemIndex < queue.items().size(); ++itemIndex) {
        const SceneOpaqueMeshBinding* binding = nullptr;
        for (const auto& candidate : bindings)
            if (candidate.itemIndex == itemIndex) {
                if (binding)
                    return "SCENE-OPAQUE-BINDING-DUPLICATE";
                binding = &candidate;
            }
        if (!binding || !binding->mesh
            || binding->materials.size() != binding->profile.materialCount)
            return "SCENE-OPAQUE-BINDING-MISSING";
        const auto& item = queue.items()[itemIndex];
        if (const auto* id = detail::SceneMeshContract::validateItem(item))
            return id;
        if (binding->profile.worldSpaceStatic)
            return "SCENE-OPAQUE-LOCAL-MESH-PROFILE";
        if (const auto* id = BasicForwardContract::meshData(*binding->mesh, binding->profile))
            return id;
        const auto& mesh = *binding->mesh;
        if (mesh.indices.size() > b.maxExpandedVertices - next->vertices_.size())
            return "SCENE-OPAQUE-VERTEX-BUDGET";
        for (const auto& previous : next->snapshots_)
            if (previous.item.objectId == item.objectId)
                return "SCENE-OPAQUE-OBJECT-DUPLICATE";
        Snapshot snapshot{item, binding->profile, binding->mesh, {}};
        snapshot.materials.assign(binding->materials.begin(), binding->materials.end());
        const auto& t = item.worldTransform;
        const auto inverseRotationLength = 1.f / std::sqrt(core::dot(t.rotation, t.rotation));
        const core::Quat rotation{t.rotation.x * inverseRotationLength,
            t.rotation.y * inverseRotationLength,
            t.rotation.z * inverseRotationLength,
            t.rotation.w * inverseRotationLength};
        for (const auto& section : mesh.submeshes) {
            const auto& material = binding->materials[section.materialSlot];
            if (!material)
                return "SCENE-OPAQUE-MATERIAL-MISSING";
            BasicForwardMaterialConstants surface;
            if (const auto* id =
                    BasicForwardContract::materialSurface(*material, shader, shaderId, surface))
                return id;
            if (surface.flags.x != 0.f || surface.flags.w != 0.f)
                return "SCENE-OPAQUE-MATERIAL-PROFILE";
            for (std::size_t i = section.firstIndex;
                i < std::size_t(section.firstIndex) + section.indexCount;
                ++i) {
                const auto& v = mesh.vertices[mesh.indices[i]];
                const auto p = core::rotate(rotation,
                                   {v.position[0] * t.scale.x,
                                       v.position[1] * t.scale.y,
                                       v.position[2] * t.scale.z})
                    + t.translation;
                auto n = core::rotate(rotation,
                    {v.normal[0] / t.scale.x, v.normal[1] / t.scale.y, v.normal[2] / t.scale.z});
                const auto length = core::length(n);
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
                    || !std::isfinite(length) || length <= 0.f)
                    return "SCENE-OPAQUE-TRANSFORM-RANGE";
                n = core::scale(n, 1.f / length);
                next->vertices_.push_back({{p.x, p.y, p.z, 1.f},
                    {n.x, n.y, n.z, surface.flags.y},
                    {surface.baseColor.x * item.baseColorTint.x,
                        surface.baseColor.y * item.baseColorTint.y,
                        surface.baseColor.z * item.baseColorTint.z,
                        1.f}});
            }
        }
        next->snapshots_.push_back(std::move(snapshot));
    }
    output = std::move(next);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-OPAQUE-GEOMETRY-CPU-BUDGET";
}

const char* SceneOpaqueGeometry::validateCurrent(const scene3d::Scene3DRenderQueue& queue,
    core::ArrayView<SceneOpaqueMeshBinding> bindings) const noexcept
{
    if (queue.items().size() != snapshots_.size() || bindings.size() != snapshots_.size())
        return "SCENE-OPAQUE-SNAPSHOT-CHANGED";
    for (std::size_t i = 0u; i < snapshots_.size(); ++i) {
        if (!detail::SceneMeshContract::sameItem(queue.items()[i], snapshots_[i].item))
            return "SCENE-OPAQUE-SNAPSHOT-CHANGED";
        const SceneOpaqueMeshBinding* binding = nullptr;
        for (const auto& candidate : bindings)
            if (candidate.itemIndex == i) {
                if (binding)
                    return "SCENE-OPAQUE-BINDING-DUPLICATE";
                binding = &candidate;
            }
        const auto& snapshot = snapshots_[i];
        if (binding && !detail::SceneMeshContract::sameProfile(binding->profile, snapshot.profile))
            return "SCENE-OPAQUE-SNAPSHOT-PROFILE";
        if (!binding || !core::SharedOwnerProjection::sameOwner(binding->mesh, snapshot.mesh)
            || binding->materials.size() != snapshot.materials.size())
            return "SCENE-OPAQUE-SNAPSHOT-OWNER";
        for (std::size_t j = 0u; j < snapshot.materials.size(); ++j)
            if (!core::SharedOwnerProjection::sameOwner(
                    binding->materials[j], snapshot.materials[j]))
                return "SCENE-OPAQUE-SNAPSHOT-OWNER";
    }
    return nullptr;
}

const char* SceneOpaqueProducer::buffer(const void* data,
    std::uint64_t size,
    ResourceUsage usage,
    std::shared_ptr<const Lease>& output) const
{
    return detail::SceneRasterContract::makeBuffer(*device_, data, size, usage, budget_, output);
}

const char* SceneOpaqueProducer::create(GpuDevice& device,
    const std::shared_ptr<const SceneOpaqueGeometry>& geometry,
    const SceneOpaqueProducerBudget& b)
try {
    if (device_ || b.contractVersion != 1u || !b.prototypeOptIn || !geometry
        || geometry->vertices().empty())
        return "SCENE-OPAQUE-PRODUCER-PROFILE";
    if (!b.quota || !b.maxCpuBytes || b.quota->limitBytes() != b.maxCpuBytes || !b.sourceQuota
        || !b.sourceQuota->limitBytes() || !b.maxGpuBytes || !b.retainedGpuBytes
        || b.retainedGpuBytes->capacity() != b.maxGpuBytes)
        return "SCENE-OPAQUE-PRODUCER-BUDGET";
    for (const char c : b.shaderSourceSha256)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return "SCENE-OPAQUE-SHADER-IDENTITY";
    if (const auto* id = device.originalDeviceLifetimeTicket().diagnostic())
        return id;
    const auto size = geometry->vertices().size() * sizeof(SceneOpaqueVertex);
    if (size > b.maxGpuBytes || sizeof(SceneOpaqueFrameConstants) > b.maxGpuBytes - size)
        return "SCENE-OPAQUE-GPU-BUDGET";
    core::CpuMemoryQuotaScope scope(b.quota);
    // Publish device/geometry only after a complete original sealed owner exists.
    SceneOpaqueProducer candidate;
    candidate.device_ = &device;
    candidate.budget_ = b;
    if (const auto* id = candidate.buffer(geometry->vertices().data(),
            size,
            ResourceUsage::StorageBuffer,
            candidate.geometryBuffer_))
        return id;
    device_ = &device;
    lifetime_ = device.originalDeviceLifetimeTicket();
    owner_ = std::this_thread::get_id();
    budget_ = b;
    geometry_ = geometry;
    geometryBuffer_ = std::move(candidate.geometryBuffer_);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-OPAQUE-CPU-BUDGET";
}

const char* SceneOpaqueProducer::prepareFrame(const core::Mat4& vp,
    const BasicForwardLighting& light,
    std::shared_ptr<const SceneOpaqueFrame>& output) const
try {
    if (!device_)
        return "SCENE-OPAQUE-NOT-CREATED";
    if (owner_ != std::this_thread::get_id())
        return "SCENE-OPAQUE-OWNER-THREAD";
    if (const auto* id = lifetime_.diagnostic())
        return id;
    SceneOpaqueFrameConstants values{vp, {}};
    if (const auto* id = BasicForwardContract::camera(vp))
        return id;
    if (const auto* id = BasicForwardContract::lighting(
            light, BasicForwardLightingRange::LinearHdr1, values.lighting))
        return id;
    if (light.ambient.x + light.color.x * light.intensity > maxLinearColor
        || light.ambient.y + light.color.y * light.intensity > maxLinearColor
        || light.ambient.z + light.color.z * light.intensity > maxLinearColor)
        return "SCENE-OPAQUE-RGBA16F-RANGE";
    core::CpuMemoryQuotaScope scope(budget_.quota);
    std::shared_ptr<const Lease> constants;
    if (const auto* id = buffer(&values, sizeof(values), ResourceUsage::UniformBuffer, constants))
        return id;
    output = core::makeShared<SceneOpaqueFrame>(
        SceneOpaqueFrame::Key{}, geometryBuffer_, std::move(constants), values);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-OPAQUE-CPU-BUDGET";
}

const char* SceneOpaqueProducer::validatePipeline(const rhi::RenderPipelineDesc& d)
{
    if (const auto* id = detail::SceneRasterContract::validateState(d))
        return id;
    const auto& reflection = d.shaderPackage->reflection;
    if (reflection.pushConstantByteCount || reflection.descriptorBindings.size() != 2u
        || d.vertexShader.entryPoint != "vertexMain"
        || d.fragmentShader.entryPoint != "fragmentMain"
        || d.layout->desc().descriptorSets.size() != 1u
        || d.layout->desc().descriptorSets[0].set != 0u
        || !d.layout->desc().pushConstantRanges.empty())
        return "SCENE-OPAQUE-SHADER-ABI";
    for (unsigned binding = 0u; binding < 2u; ++binding) {
        unsigned count = 0u;
        for (const auto& actual : reflection.descriptorBindings)
            if (actual.set == 0u && actual.binding == binding && actual.arrayCount == 1u
                && actual.type
                    == (binding ? DescriptorType::ReadOnlyStorageBuffer
                                : DescriptorType::UniformBuffer)
                && actual.stages
                    == (binding ? ShaderStage::Vertex
                                : ShaderStage::Vertex | ShaderStage::Fragment))
                ++count;
        if (count != 1u)
            return "SCENE-OPAQUE-SHADER-ABI";
    }
    return nullptr;
}

const char* SceneOpaqueProducer::validate(const SceneOpaqueGraphContext& c) const
try {
    if (!device_ || !core::SharedOwnerProjection::sameOwner(c.frame.geometry(), geometryBuffer_))
        return "SCENE-OPAQUE-FRAME-OWNER";
    if (owner_ != std::this_thread::get_id())
        return "SCENE-OPAQUE-OWNER-THREAD";
    if (const auto* id = lifetime_.diagnostic())
        return id;
    const auto& inventory = c.execution.sources();
    const auto& sources = inventory.sources();
    if (c.pipeline >= sources.size() || c.descriptors >= sources.size())
        return "SCENE-OPAQUE-SOURCE-INDEX";
    const auto* pipeline =
        std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
            &sources[c.pipeline]);
    const auto* set = std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
        &sources[c.descriptors]);
    if (!pipeline || !*pipeline || !set || !*set
        || (*set)->frozenPipelineSourceIndex() != c.pipeline || (*set)->frozenLogicalSet() != 0u
        || (*set)->frozenElements().size() != 2u
        || !(*pipeline)->originalObject()->originalGraphicsCreation())
        return "SCENE-OPAQUE-SOURCE-KIND";
    if (const auto* id = validatePipeline(
            (*pipeline)->originalObject()->originalGraphicsCreation()->definition()))
        return id;
    if ((*pipeline)->originalObject()->shaderReflection().sourceSha256
        != std::string_view(budget_.shaderSourceSha256.data(), budget_.shaderSourceSha256.size()))
        return "SCENE-OPAQUE-SHADER-IDENTITY";
    std::array<bool, 2u> found{};
    auto& graph = c.execution.graph();
    const auto& color = graph.texture(c.color).desc();
    const auto& depth = graph.texture(c.depth).desc();
    for (const auto* d : {&color, &depth})
        if (!d->extent.width || !d->extent.height || d->extent.depth != 1u
            || d->arrayLayerCount != 1u || d->mipLevelCount != 1u || d->sampleCount != 1u
            || (d->usage & TextureUsage::Sampled) == TextureUsage::None)
            return "SCENE-OPAQUE-ATTACHMENTS";
    if (color.format != TextureFormat::Rgba16Float || depth.format != TextureFormat::D32Float
        || color.extent != depth.extent || c.color.id == c.depth.id
        || (color.usage & TextureUsage::RenderAttachment) == TextureUsage::None
        || (depth.usage & TextureUsage::RenderAttachment) == TextureUsage::None)
        return "SCENE-OPAQUE-ATTACHMENTS";
    for (const auto value : {c.clear.r, c.clear.g, c.clear.b, c.clear.a})
        if (!std::isfinite(value) || value < 0.f || value > maxLinearColor)
            return "SCENE-OPAQUE-CLEAR";
    for (const auto& element : (*set)->frozenElements()) {
        const auto& e = element.declaration;
        const auto binding = e.write.binding;
        if (binding > 1u || found[binding] || e.sourceIndex >= sources.size())
            return "SCENE-OPAQUE-DESCRIPTOR";
        const auto* buffer = std::get_if<std::shared_ptr<const Lease>>(&sources[e.sourceIndex]);
        const auto& expected = binding ? geometryBuffer_ : c.frame.constants();
        if (!buffer || !*buffer || !core::SharedOwnerProjection::sameOwner(*buffer, expected)
            || e.write.bufferBindingRange.contractVersion != 2u
            || e.write.bufferBindingRange.offsetBytes != 0u
            || e.write.bufferBindingRange.sizeBytes != expected->originalObject()->desc().sizeBytes
            || e.bufferAccessOffset
            || e.bufferAccessBytes != expected->originalObject()->desc().sizeBytes
            || e.access != rhi::ResourceAccess::ShaderRead
            || e.stages
                != (binding ? PipelineStage::VertexShader
                            : PipelineStage::VertexShader | PipelineStage::FragmentShader))
            return "SCENE-OPAQUE-DESCRIPTOR";
        found[binding] = true;
    }
    if (!found[0] || !found[1])
        return "SCENE-OPAQUE-DESCRIPTOR";
    return nullptr;
} catch (const std::out_of_range&) {
    return "SCENE-OPAQUE-GRAPH-HANDLE";
}

const char* SceneOpaqueProducer::contribute(const SceneOpaqueGraphContext& c) const
try {
    if (const auto* id = validate(c))
        return id;
    auto& graph = c.execution.graph();
    const auto& color = graph.texture(c.color).desc();
    std::array<render_graph::BufferHandle, 2u> buffers;
    std::array<std::shared_ptr<const Lease>, 2u> retained{c.frame.constants(), geometryBuffer_};
    for (std::size_t binding = 0u; binding < retained.size(); ++binding)
        buffers[binding] = graph.importBufferRef(
            {retained[binding], const_cast<GpuBuffer*>(retained[binding]->originalObject())});
    auto pass = c.execution.graphics("SceneOpaque.HdrDepth",
        c.pipeline,
        c.descriptors,
        {0.f, 0.f, float(color.extent.width), float(color.extent.height), 0.f, 1.f},
        {0, 0, color.extent.width, color.extent.height},
        {static_cast<std::uint32_t>(geometry_->vertices().size()), 1u, 0u, 0u});
    pass.writeAttachment(
            c.color, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store, {}, c.clear)
        .writeAttachment(c.depth, ResourceState::DepthAttachment, LoadOp::Clear, StoreOp::Store);
    pass.access(buffers[0],
        {ResourceState::ShaderRead,
            PipelineStage::VertexShader | PipelineStage::FragmentShader,
            rhi::ResourceAccess::ShaderRead},
        0u,
        sizeof(SceneOpaqueFrameConstants));
    pass.access(buffers[1],
        {ResourceState::ShaderRead, PipelineStage::VertexShader, rhi::ResourceAccess::ShaderRead},
        0u,
        geometryBuffer_->originalObject()->desc().sizeBytes);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-OPAQUE-GRAPH-CPU-BUDGET";
} catch (const std::out_of_range&) {
    return "SCENE-OPAQUE-GRAPH-HANDLE";
}
} // namespace arp::renderer
