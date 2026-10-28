#include "renderer/pipeline/scene_pbr_producer.h"

#include "core/hash/stable_hash.h"
#include "renderer/pipeline/detail/scene_raster_contract.h"
#include "rhi/frontend/transfer_upload_recording_validation.h"
#include "rhi/semantics/ordinary_sampled_color.h"

namespace arp::renderer {
namespace {
using namespace rhi;
using Buffer = OriginalCommandBufferLease;
using MeshContract = detail::SceneMeshContract;
using RasterContract = detail::SceneRasterContract;

std::uint64_t meshContent(const assets::MeshAsset& m) noexcept
{
    core::StableHash64 hash;
    hash.appendU64(m.vertices.size());
    for (const auto& v : m.vertices) {
        for (float f : v.position)
            hash.appendFloat(f);
        for (float f : v.normal)
            hash.appendFloat(f);
        for (float f : v.uv)
            hash.appendFloat(f);
        for (float f : v.tangent)
            hash.appendFloat(f);
    }
    hash.appendU64(m.indices.size());
    for (auto i : m.indices)
        hash.appendU64(i);
    hash.appendU64(m.submeshes.size());
    for (const auto& s : m.submeshes) {
        hash.appendU64(s.firstIndex);
        hash.appendU64(s.indexCount);
        hash.appendU64(s.materialSlot);
    }
    return hash.value();
}

std::uint64_t materialContent(const resource::MaterialResource& m) noexcept
{
    core::StableHash64 hash;
    m.appendContentFingerprint(hash);
    return hash.value();
}

std::array<std::string_view, 5u> textureIds(const resource::MaterialResource& m) noexcept
{
    return {m.baseColorTextureId,
        m.normalTextureId,
        m.metallicRoughnessTextureId,
        m.occlusionTextureId,
        m.emissiveTextureId};
}

constexpr std::array<DescriptorType, 9u> descriptorTypes{DescriptorType::UniformBuffer,
    DescriptorType::ReadOnlyStorageBuffer,
    DescriptorType::UniformBuffer,
    DescriptorType::SampledTexture,
    DescriptorType::SampledTexture,
    DescriptorType::SampledTexture,
    DescriptorType::SampledTexture,
    DescriptorType::SampledTexture,
    DescriptorType::Sampler};

ShaderStage descriptorStages(std::size_t binding) noexcept
{
    return binding == 0u ? ShaderStage::Vertex | ShaderStage::Fragment
        : binding == 1u  ? ShaderStage::Vertex
                         : ShaderStage::Fragment;
}

PipelineStage accessStages(std::size_t binding) noexcept
{
    return binding == 0u ? PipelineStage::VertexShader | PipelineStage::FragmentShader
        : binding == 1u  ? PipelineStage::VertexShader
                         : PipelineStage::FragmentShader;
}
} // namespace

const char* ScenePbrGeometry::material(const resource::MaterialResource& m,
    const resource::ShaderPackageResource& shader,
    std::string_view shaderId,
    ScenePbrMaterialConstants& output)
{
    if (!resource::validateMaterialContract(m, shader).valid())
        return "SCENE-PBR-MATERIAL-CONTRACT";
    if (!resource::isResourceUuid(shaderId) || std::string_view(m.shaderPackageId) != shaderId)
        return "SCENE-PBR-MATERIAL-SHADER";
    if (m.alphaMode != "OPAQUE" && m.alphaMode != "MASK")
        return "SCENE-PBR-ALPHA-PROFILE";
    if (m.textureBindingCount != 5)
        return "SCENE-PBR-TEXTURE-COUNT";
    for (auto id : textureIds(m))
        if (!resource::isResourceUuid(id))
            return "SCENE-PBR-TEXTURE-IDENTITY";
    for (auto e : {m.emissiveR, m.emissiveG, m.emissiveB})
        if (e > SceneOpaqueProducer::maxLinearColor)
            return "SCENE-PBR-EMISSIVE-RANGE";
    output = {{m.baseColorR, m.baseColorG, m.baseColorB, m.baseColorA},
        {m.emissiveR, m.emissiveG, m.emissiveB, 0.f},
        {m.metallicFactor, m.roughnessFactor, m.alphaCutoff, m.alphaMode == "MASK" ? 1.f : 0.f},
        {m.doubleSided ? 1.f : 0.f, 0.f, 0.f, 0.f}};
    return nullptr;
}

const char* ScenePbrGeometry::create(const scene3d::Scene3DRenderQueue& queue,
    core::ArrayView<SceneOpaqueMeshBinding> bindings,
    const resource::ShaderPackageResource& shader,
    std::string_view shaderId,
    const ScenePbrGeometryBudget& b,
    std::shared_ptr<const ScenePbrGeometry>& output)
try {
    if (b.contractVersion != 1u || !b.prototypeOptIn)
        return "SCENE-PBR-GEOMETRY-OPT-IN";
    if (!b.maxItems || !b.maxExpandedVertices || !b.maxSections || !b.maxCpuBytes || !b.quota
        || b.quota->limitBytes() != b.maxCpuBytes)
        return "SCENE-PBR-GEOMETRY-BUDGET";
    if (queue.items().empty() || queue.items().size() > b.maxItems
        || bindings.size() != queue.items().size())
        return "SCENE-PBR-BINDING-COVERAGE";
    core::CpuMemoryQuotaScope scope(b.quota);
    auto next = core::makeShared<ScenePbrGeometry>();
    next->quota_ = b.quota;
    next->snapshots_.reserve(bindings.size());
    for (std::size_t itemIndex = 0u; itemIndex < queue.items().size(); ++itemIndex) {
        const SceneOpaqueMeshBinding* binding = nullptr;
        for (const auto& candidate : bindings)
            if (candidate.itemIndex == itemIndex) {
                if (binding)
                    return "SCENE-PBR-BINDING-DUPLICATE";
                binding = &candidate;
            }
        if (!binding || !binding->mesh
            || binding->materials.size() != binding->profile.materialCount)
            return "SCENE-PBR-BINDING-MISSING";
        const auto& item = queue.items()[itemIndex];
        if (const auto* id = MeshContract::validateItem(item))
            return id;
        if (binding->profile.worldSpaceStatic)
            return "SCENE-PBR-LOCAL-MESH-PROFILE";
        if (const auto* id = BasicForwardContract::meshData(*binding->mesh, binding->profile))
            return id;
        const auto& mesh = *binding->mesh;
        if (mesh.indices.size() > b.maxExpandedVertices - next->vertices_.size()
            || mesh.submeshes.size() > b.maxSections - next->sections_.size())
            return "SCENE-PBR-GEOMETRY-BUDGET";
        for (const auto& prior : next->snapshots_)
            if (prior.item.objectId == item.objectId)
                return "SCENE-PBR-OBJECT-DUPLICATE";
        Snapshot snapshot{item, binding->profile, binding->mesh, meshContent(mesh), {}, {}};
        snapshot.materials.assign(binding->materials.begin(), binding->materials.end());
        for (const auto& m : snapshot.materials) {
            ScenePbrMaterialConstants constants;
            if (!m)
                return "SCENE-PBR-MATERIAL-MISSING";
            if (const auto* id = material(*m, shader, shaderId, constants))
                return id;
            snapshot.materialContent.push_back(materialContent(*m));
        }
        const auto& t = item.worldTransform;
        const auto inverseRotationLength = 1.f / std::sqrt(core::dot(t.rotation, t.rotation));
        const core::Quat q{t.rotation.x * inverseRotationLength,
            t.rotation.y * inverseRotationLength,
            t.rotation.z * inverseRotationLength,
            t.rotation.w * inverseRotationLength};
        const float handedness =
            (t.scale.x < 0.f) ^ (t.scale.y < 0.f) ^ (t.scale.z < 0.f) ? -1.f : 1.f;
        for (const auto& section : mesh.submeshes) {
            Section draw{static_cast<std::uint32_t>(next->vertices_.size()),
                section.indexCount,
                itemIndex,
                section.materialSlot,
                {},
                snapshot.materials[section.materialSlot]};
            if (const auto* id = material(*draw.material, shader, shaderId, draw.constants))
                return id;
            for (std::size_t i = section.firstIndex;
                i < std::size_t(section.firstIndex) + section.indexCount;
                ++i) {
                const auto& v = mesh.vertices[mesh.indices[i]];
                for (float f : v.tangent)
                    if (!std::isfinite(f))
                        return "SCENE-PBR-TANGENT-NONFINITE";
                if (v.tangent[3] != 1.f && v.tangent[3] != -1.f)
                    return "SCENE-PBR-TANGENT-SIGN";
                const auto p = core::rotate(q,
                                   {v.position[0] * t.scale.x,
                                       v.position[1] * t.scale.y,
                                       v.position[2] * t.scale.z})
                    + t.translation;
                auto n = core::rotate(
                    q, {v.normal[0] / t.scale.x, v.normal[1] / t.scale.y, v.normal[2] / t.scale.z});
                const auto nl = core::length(n);
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
                    || !std::isfinite(nl) || nl <= 0.f)
                    return "SCENE-PBR-TRANSFORM-RANGE";
                n = core::scale(n, 1.f / nl);
                auto tangent = core::rotate(q,
                    {v.tangent[0] * t.scale.x, v.tangent[1] * t.scale.y, v.tangent[2] * t.scale.z});
                tangent = tangent - core::scale(n, core::dot(tangent, n));
                const auto tl = core::length(tangent);
                if (!std::isfinite(tl) || tl <= 0.f)
                    return "SCENE-PBR-TANGENT-SINGULAR";
                tangent = core::scale(tangent, 1.f / tl);
                next->vertices_.push_back({{p.x, p.y, p.z, 1.f},
                    {n.x, n.y, n.z, 0.f},
                    {tangent.x, tangent.y, tangent.z, v.tangent[3] * handedness},
                    {v.uv[0], v.uv[1], 0.f, 0.f},
                    item.baseColorTint});
            }
            next->sections_.push_back(std::move(draw));
        }
        next->snapshots_.push_back(std::move(snapshot));
    }
    output = std::move(next);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-PBR-GEOMETRY-CPU-BUDGET";
}

const char* ScenePbrGeometry::validateCurrent(const scene3d::Scene3DRenderQueue& queue,
    core::ArrayView<SceneOpaqueMeshBinding> bindings) const noexcept
{
    if (queue.items().size() != snapshots_.size() || bindings.size() != snapshots_.size())
        return "SCENE-PBR-SNAPSHOT-CHANGED";
    for (std::size_t i = 0; i < snapshots_.size(); ++i) {
        const auto& s = snapshots_[i];
        if (!MeshContract::sameItem(queue.items()[i], s.item))
            return "SCENE-PBR-SNAPSHOT-CHANGED";
        const SceneOpaqueMeshBinding* b = nullptr;
        for (const auto& candidate : bindings)
            if (candidate.itemIndex == i) {
                if (b)
                    return "SCENE-PBR-BINDING-DUPLICATE";
                b = &candidate;
            }
        if (!b || !core::SharedOwnerProjection::sameOwner(b->mesh, s.mesh)
            || b->materials.size() != s.materials.size())
            return "SCENE-PBR-SNAPSHOT-OWNER";
        if (!MeshContract::sameProfile(b->profile, s.profile))
            return "SCENE-PBR-SNAPSHOT-PROFILE";
        if (meshContent(*b->mesh) != s.content)
            return "SCENE-PBR-MESH-MUTATED";
        for (std::size_t j = 0; j < b->materials.size(); ++j)
            if (!core::SharedOwnerProjection::sameOwner(b->materials[j], s.materials[j]))
                return "SCENE-PBR-SNAPSHOT-OWNER";
            else if (materialContent(*b->materials[j]) != s.materialContent[j])
                return "SCENE-PBR-MATERIAL-MUTATED";
    }
    return nullptr;
}

const char* ScenePbrProducer::validateMaterialBinding(
    const resource::MaterialResource& material, const ScenePbrMaterialBinding& binding)
{
    const auto ids = textureIds(material);
    for (std::size_t i = 0u; i < ids.size(); ++i) {
        const auto& b = binding.textures[i];
        if (std::string_view(b.resourceId) != ids[i] || !resource::isResourceUuid(b.resourceId))
            return "SCENE-PBR-TEXTURE-IDENTITY";
        if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(b.view))
            return id;
        if (!b.publication || b.publication->resourceId() != b.resourceId
            || b.publication->view().get() != b.view.get()
            || !core::SharedOwnerProjection::sameOwner(b.publication->view(), b.view))
            return "SCENE-PBR-TEXTURE-PUBLICATION";
        if (const auto* id = b.publication->validate())
            return id;
        const auto& view = b.view->originalObject()->desc();
        const auto& parent = b.view->originalParentLease()->originalObject()->desc();
        const auto format =
            i == 0u || i == 4u ? TextureFormat::Rgba8UnormSrgb : TextureFormat::Rgba8Unorm;
        if (view.format != format || parent.format != format || view.usage != TextureUsage::Sampled
            || view.dimension != TextureViewDimension::Texture2D
            || view.aspect != TextureAspect::Color || view.baseMipLevel || view.mipLevelCount != 1u
            || view.baseArrayLayer || view.arrayLayerCount != 1u || parent.sampleCount != 1u
            || parent.mipLevelCount != 1u || !parent.extent.width || !parent.extent.height
            || parent.extent.depth != 1u || parent.arrayLayerCount != 1u || parent.cubeCompatible)
            return "SCENE-PBR-TEXTURE-PROFILE";
    }
    if (const auto* id = OriginalCommandSamplerLease::validateOriginal(binding.sampler))
        return id;
    const auto& s = binding.sampler->originalObject()->desc();
    // This version uses explicit LOD0. Anisotropy/mip selection need another profile.
    if (s.compareOp != CompareOp::Never || s.mipFilter != SamplerMipFilter::None
        || s.maxAnisotropy != 0.f)
        return "SCENE-PBR-SAMPLER-PROFILE";
    return nullptr;
}

const char* ScenePbrProducer::create(GpuDevice& device,
    const std::shared_ptr<const ScenePbrGeometry>& geometry,
    core::ArrayView<ScenePbrMaterialBinding> bindings,
    const SceneOpaqueProducerBudget& b)
try {
    if (device_ || b.contractVersion != 1u || !b.prototypeOptIn || !geometry
        || geometry->vertices().empty() || bindings.size() != geometry->sections().size())
        return "SCENE-PBR-PRODUCER-PROFILE";
    if (!b.quota || !b.maxCpuBytes || b.quota->limitBytes() != b.maxCpuBytes || !b.sourceQuota
        || !b.sourceQuota->limitBytes() || !b.maxGpuBytes || !b.retainedGpuBytes
        || b.retainedGpuBytes->capacity() != b.maxGpuBytes)
        return "SCENE-PBR-PRODUCER-BUDGET";
    for (char c : b.shaderSourceSha256)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return "SCENE-PBR-SHADER-IDENTITY";
    if (const auto* id = device.originalDeviceLifetimeTicket().diagnostic())
        return id;
    if (bindings.size() > UINT64_MAX / 5u)
        return "SCENE-PBR-TEXTURE-BUDGET";
    if (const auto* id =
            textureExecutionDiagnostic(device.backend(), bindings.size() * 5u, b.maxGpuBytes))
        return id;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        if (const auto* id =
                validateMaterialBinding(*geometry->sections()[i].material, bindings[i]))
            return id;
        for (const auto& t : bindings[i].textures) {
            if (const auto* id =
                    device.validateOriginalResourceRegistration(*t.view->originalObject()))
                return id;
            if (const auto* id = device.validateOriginalResourceRegistration(
                    *t.view->originalParentLease()->originalObject()))
                return id;
        }
        if (const auto* id =
                device.validateOriginalResourceRegistration(*bindings[i].sampler->originalObject()))
            return id;
    }
    const auto size = geometry->vertices().size() * sizeof(ScenePbrVertex);
    const auto materialBytes = geometry->sections().size() * sizeof(ScenePbrMaterialConstants);
    if (size > b.maxGpuBytes || materialBytes > b.maxGpuBytes - size
        || sizeof(ScenePbrFrameConstants) > b.maxGpuBytes - size - materialBytes)
        return "SCENE-PBR-GPU-BUDGET";
    core::CpuMemoryQuotaScope scope(b.quota);
    std::shared_ptr<const Buffer> vertices;
    core::pmr::Vector<std::shared_ptr<const Buffer>> materials;
    core::pmr::Vector<ScenePbrMaterialBinding> retained(bindings.begin(), bindings.end());
    if (const auto* id = RasterContract::makeBuffer(
            device, geometry->vertices().data(), size, ResourceUsage::StorageBuffer, b, vertices))
        return id;
    materials.reserve(bindings.size());
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& section = geometry->sections()[i];
        std::shared_ptr<const Buffer> buffer;
        if (const auto* id = RasterContract::makeBuffer(device,
                &section.constants,
                sizeof(section.constants),
                ResourceUsage::UniformBuffer,
                b,
                buffer))
            return id;
        materials.push_back(std::move(buffer));
    }
    device_ = &device;
    lifetime_ = device.originalDeviceLifetimeTicket();
    owner_ = std::this_thread::get_id();
    budget_ = b;
    geometry_ = geometry;
    geometryBuffer_ = std::move(vertices);
    materialBuffers_ = std::move(materials);
    bindings_ = std::move(retained);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-PBR-CPU-BUDGET";
}

const char* ScenePbrProducer::textureExecutionDiagnostic(
    Backend backend, std::uint64_t maxTextures, std::uint64_t maxPackedBytes) noexcept
{
    const OriginalOrdinaryTextureUploadContract::Budget budget{
        1u, true, maxTextures, maxTextures, maxPackedBytes};
    const auto decision = OriginalOrdinaryTextureUploadContract::lowering(backend, budget);
    if (!decision.privateRecordingAvailable)
        return decision.diagnostic;
    return semantics::OriginalOrdinarySampledColorContract::lowering(backend, {1u, true})
        .diagnostic;
}

const char* ScenePbrProducer::viewConstants(const ScenePbrView& view, core::Vec4& output) noexcept
{
    auto camera = view.positionOrDirection;
    if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)
        || (view.kind != ScenePbrView::Kind::Perspective
            && view.kind != ScenePbrView::Kind::Orthographic))
        return "SCENE-PBR-CAMERA";
    if (view.kind == ScenePbrView::Kind::Orthographic) {
        const auto length = core::length(camera);
        if (!std::isfinite(length) || length <= 0.f)
            return "SCENE-PBR-CAMERA";
        camera = core::scale(camera, 1.f / length);
    }
    output = {
        camera.x, camera.y, camera.z, view.kind == ScenePbrView::Kind::Perspective ? 1.f : 0.f};
    return nullptr;
}

const char* ScenePbrProducer::frameConstants(const core::Mat4& vp,
    const BasicForwardLighting& light,
    const ScenePbrView& view,
    const ScenePbrGeometry& geometry,
    ScenePbrFrameConstants& output) noexcept
{
    if (const auto* id = BasicForwardContract::camera(vp))
        return id;
    ScenePbrFrameConstants values{vp, {}, {}};
    if (const auto* id = viewConstants(view, values.cameraPosition))
        return id;
    if (const auto* id = BasicForwardContract::lighting(
            light, BasicForwardLightingRange::LinearHdr1, values.lighting))
        return id;
    // Existing GGX denominator floor is .001; D <= sqrt(1/(pi*.001)).
    // Smith Gv <= NdotV/k, k >= 1/8, so direct response <= 1/pi + D/(4/8).
    const auto responseBound = 1.0 / 3.14159265 + 2.0 * std::sqrt(1.0 / (3.14159265 * .001));
    if (geometry.sections().empty())
        return "SCENE-PBR-GEOMETRY-EMPTY";
    const auto& l = values.lighting;
    for (const auto& section : geometry.sections())
        for (auto range : {std::array<double, 3>{l.ambientColor.x,
                               l.lightColor.x * double(l.directionIntensity.w),
                               section.constants.emissive.x},
                 std::array<double, 3>{l.ambientColor.y,
                     l.lightColor.y * double(l.directionIntensity.w),
                     section.constants.emissive.y},
                 std::array<double, 3>{l.ambientColor.z,
                     l.lightColor.z * double(l.directionIntensity.w),
                     section.constants.emissive.z}})
            if (range[0] + responseBound * range[1] + range[2]
                > SceneOpaqueProducer::maxLinearColor)
                return "SCENE-PBR-RGBA16F-RANGE";
    output = values;
    return nullptr;
}

const char* ScenePbrProducer::prepareFrame(const core::Mat4&,
    const BasicForwardLighting&,
    const ScenePbrView&,
    std::shared_ptr<const ScenePbrFrame>&) const
{
    return device_ ? "SCENE-PBR-RESOURCE-VALIDATION-REQUIRED" : "SCENE-PBR-NOT-CREATED";
}

const char* ScenePbrProducer::prepareFrame(const resource::ResourceManager& resources,
    const core::Mat4& vp,
    const BasicForwardLighting& light,
    const ScenePbrView& view,
    std::shared_ptr<const ScenePbrFrame>& output) const
{
    if (!device_)
        return "SCENE-PBR-NOT-CREATED";
    if (owner_ != std::this_thread::get_id())
        return "SCENE-PBR-OWNER-THREAD";
    for (const auto& material : bindings_)
        for (const auto& texture : material.textures) {
            if (!texture.publication)
                return "SCENE-PBR-TEXTURE-PUBLICATION";
            if (const auto* id = texture.publication->validateCurrent(resources))
                return id;
        }
    return prepareValidatedFrame(vp, light, view, output);
}

const char* ScenePbrProducer::prepareValidatedFrame(const core::Mat4& vp,
    const BasicForwardLighting& light,
    const ScenePbrView& view,
    std::shared_ptr<const ScenePbrFrame>& output) const
try {
    if (!device_)
        return "SCENE-PBR-NOT-CREATED";
    if (owner_ != std::this_thread::get_id())
        return "SCENE-PBR-OWNER-THREAD";
    if (const auto* id = lifetime_.diagnostic())
        return id;
    ScenePbrFrameConstants values;
    if (const auto* id = frameConstants(vp, light, view, *geometry_, values))
        return id;
    core::CpuMemoryQuotaScope scope(budget_.quota);
    std::shared_ptr<const Buffer> constants;
    if (const auto* id = RasterContract::makeBuffer(
            *device_, &values, sizeof(values), ResourceUsage::UniformBuffer, budget_, constants))
        return id;
    output = core::makeShared<ScenePbrFrame>(
        ScenePbrFrame::Key{}, geometryBuffer_, std::move(constants), values);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-PBR-CPU-BUDGET";
}

const char* ScenePbrProducer::validatePipeline(const RenderPipelineDesc& d)
{
    if (const auto* id = RasterContract::validateState(d))
        return id;
    if (d.shaderPackage->reflection.pushConstantByteCount
        || d.shaderPackage->reflection.descriptorBindings.size() != 9u
        || d.vertexShader.entryPoint != "vertexMain"
        || d.fragmentShader.entryPoint != "fragmentMain"
        || d.layout->desc().descriptorSets.size() != 1u
        || d.layout->desc().descriptorSets[0].set != 0u
        || d.layout->desc().descriptorLayouts.size() != 1u
        || d.layout->desc().descriptorLayouts[0].bindings.size() != 9u
        || d.layout->desc().descriptorSets[0].layoutName
            != d.layout->desc().descriptorLayouts[0].name
        || !d.layout->desc().pushConstantRanges.empty())
        return "SCENE-PBR-SHADER-ABI";
    for (std::size_t i = 0; i < descriptorTypes.size(); ++i) {
        std::size_t count = 0;
        for (const auto& b : d.shaderPackage->reflection.descriptorBindings)
            if (b.set == 0u && b.binding == i && b.arrayCount == 1u && b.type == descriptorTypes[i]
                && b.stages == descriptorStages(i))
                ++count;
        if (count != 1u)
            return "SCENE-PBR-SHADER-ABI";
        count = 0u;
        for (const auto& b : d.layout->desc().descriptorLayouts[0].bindings)
            if (b.binding == i && b.arrayCount == 1u && b.type == descriptorTypes[i]
                && b.stages == descriptorStages(i))
                ++count;
        if (count != 1u)
            return "SCENE-PBR-SHADER-ABI";
    }
    return nullptr;
}

const char* ScenePbrProducer::validate(const ScenePbrGraphContext& c) const
try {
    if (!device_ || !core::SharedOwnerProjection::sameOwner(c.frame.geometry(), geometryBuffer_))
        return "SCENE-PBR-FRAME-OWNER";
    if (owner_ != std::this_thread::get_id())
        return "SCENE-PBR-OWNER-THREAD";
    if (const auto* id = lifetime_.diagnostic())
        return id;
    const auto& sources = c.execution.sources().sources();
    if (c.pipeline >= sources.size() || c.sectionDescriptors.size() != bindings_.size())
        return "SCENE-PBR-SOURCE-INDEX";
    const auto* pipeline =
        std::get_if<std::shared_ptr<const OriginalCommandGraphicsPipelineSource>>(
            &sources[c.pipeline]);
    if (!pipeline || !*pipeline || !(*pipeline)->originalObject()->originalGraphicsCreation())
        return "SCENE-PBR-SOURCE-KIND";
    if (const auto* id = validatePipeline(
            (*pipeline)->originalObject()->originalGraphicsCreation()->definition()))
        return id;
    if ((*pipeline)->originalObject()->shaderReflection().sourceSha256
        != std::string_view(budget_.shaderSourceSha256.data(), budget_.shaderSourceSha256.size()))
        return "SCENE-PBR-SHADER-IDENTITY";
    const auto& color = c.execution.graph().texture(c.color).desc();
    const auto& depth = c.execution.graph().texture(c.depth).desc();
    for (const auto* d : {&color, &depth})
        if (!d->extent.width || !d->extent.height || d->extent.depth != 1u
            || d->arrayLayerCount != 1u || d->mipLevelCount != 1u || d->sampleCount != 1u
            || (d->usage & TextureUsage::Sampled) == TextureUsage::None
            || (d->usage & TextureUsage::RenderAttachment) == TextureUsage::None)
            return "SCENE-PBR-ATTACHMENTS";
    if (color.format != TextureFormat::Rgba16Float || depth.format != TextureFormat::D32Float
        || color.extent != depth.extent || c.color.id == c.depth.id)
        return "SCENE-PBR-ATTACHMENTS";
    for (float f : {c.clear.r, c.clear.g, c.clear.b, c.clear.a})
        if (!std::isfinite(f) || f < 0.f || f > SceneOpaqueProducer::maxLinearColor)
            return "SCENE-PBR-CLEAR";
    for (std::size_t section = 0; section < bindings_.size(); ++section) {
        const auto index = c.sectionDescriptors[section];
        if (index >= sources.size())
            return "SCENE-PBR-DESCRIPTOR";
        const auto* set =
            std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(&sources[index]);
        if (!set || !*set || (*set)->frozenPipelineSourceIndex() != c.pipeline
            || (*set)->frozenLogicalSet() != 0u || (*set)->frozenElements().size() != 9u)
            return "SCENE-PBR-DESCRIPTOR";
        std::array<bool, 9u> found{};
        for (const auto& item : (*set)->frozenElements()) {
            const auto& e = item.declaration;
            const auto b = e.write.binding;
            if (b >= 9u || found[b] || e.sourceIndex >= sources.size()
                || e.write.type != descriptorTypes[b] || e.stages != accessStages(b))
                return "SCENE-PBR-DESCRIPTOR";
            found[b] = true;
            if (b < 3u) {
                const auto* owner =
                    std::get_if<std::shared_ptr<const Buffer>>(&sources[e.sourceIndex]);
                const auto& expected = b == 0u ? c.frame.constants()
                    : b == 1u                  ? geometryBuffer_
                                               : materialBuffers_[section];
                if (!owner || !core::SharedOwnerProjection::sameOwner(*owner, expected)
                    || e.write.bufferBindingRange.contractVersion != 2u
                    || e.write.bufferBindingRange.offsetBytes
                    || e.write.bufferBindingRange.sizeBytes
                        != expected->originalObject()->desc().sizeBytes
                    || e.bufferAccessOffset
                    || e.bufferAccessBytes != expected->originalObject()->desc().sizeBytes
                    || e.access != rhi::ResourceAccess::ShaderRead)
                    return "SCENE-PBR-DESCRIPTOR";
            } else if (b < 8u) {
                const auto* owner =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &sources[e.sourceIndex]);
                if (!owner
                    || !core::SharedOwnerProjection::sameOwner(
                        *owner, bindings_[section].textures[b - 3u].view)
                    || e.access != rhi::ResourceAccess::ShaderRead)
                    return "SCENE-PBR-DESCRIPTOR";
            } else {
                const auto* owner = std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(
                    &sources[e.sourceIndex]);
                if (!owner
                    || !core::SharedOwnerProjection::sameOwner(*owner, bindings_[section].sampler)
                    || e.access != rhi::ResourceAccess::None
                    || e.write.resourceState != ResourceState::Undefined)
                    return "SCENE-PBR-DESCRIPTOR";
            }
        }
    }
    return nullptr;
} catch (const std::out_of_range&) {
    return "SCENE-PBR-GRAPH-HANDLE";
}

const char* ScenePbrProducer::contribute(const ScenePbrGraphContext& c) const
try {
    if (const auto* id = validate(c))
        return id;
    auto& graph = c.execution.graph();
    const auto& extent = graph.texture(c.color).desc().extent;
    const auto import = [&](const auto& owner) {
        return graph.importBufferRef({owner, const_cast<GpuBuffer*>(owner->originalObject())});
    };
    const auto frame = import(c.frame.constants()), vertices = import(geometryBuffer_);
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        const auto material = import(materialBuffers_[i]);
        const auto& section = geometry_->sections()[i];
        auto pass = c.execution.graphics("ScenePbr.HdrDepth",
            c.pipeline,
            c.sectionDescriptors[i],
            {0.f, 0.f, float(extent.width), float(extent.height), 0.f, 1.f},
            {0, 0, extent.width, extent.height},
            {section.vertexCount, 1u, section.firstVertex, 0u});
        pass.writeAttachment(c.color,
                ResourceState::ColorAttachment,
                i ? LoadOp::Load : LoadOp::Clear,
                StoreOp::Store,
                {},
                c.clear)
            .writeAttachment(c.depth,
                ResourceState::DepthAttachment,
                i ? LoadOp::Load : LoadOp::Clear,
                StoreOp::Store);
        for (auto b : {std::pair{frame, c.frame.constants()},
                 std::pair{vertices, geometryBuffer_},
                 std::pair{material, materialBuffers_[i]}})
            pass.access(b.first,
                {ResourceState::ShaderRead,
                    b.first.id == vertices.id ? PipelineStage::VertexShader
                        : b.first.id == frame.id
                        ? PipelineStage::VertexShader | PipelineStage::FragmentShader
                        : PipelineStage::FragmentShader,
                    rhi::ResourceAccess::ShaderRead},
                0u,
                b.second->originalObject()->desc().sizeBytes);
        for (const auto& t : bindings_[i].textures) {
            const auto handle = graph.importCompletedSampledTexture(
                t.publication->upload(), t.view, PipelineStage::FragmentShader);
            const auto& d = t.view->originalObject()->desc();
            pass.access(handle,
                {ResourceState::ShaderRead,
                    PipelineStage::FragmentShader,
                    rhi::ResourceAccess::ShaderRead},
                {d.baseMipLevel, d.mipLevelCount, d.baseArrayLayer, d.arrayLayerCount, d.aspect});
        }
    }
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-PBR-GRAPH-CPU-BUDGET";
} catch (const std::out_of_range&) {
    return "SCENE-PBR-GRAPH-HANDLE";
}
} // namespace arp::renderer
