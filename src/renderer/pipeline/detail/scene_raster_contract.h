#pragma once
#include "core/ownership/shared_owner_projection.h"
#include "renderer/pipeline/scene_opaque_producer.h"

namespace arp::renderer::detail {
// Shared scene-raster contracts; resource/material interpretation stays in renderer.
class SceneMeshContract final {
public:
    static bool sameProfile(const BasicWorldMeshProfile& a, const BasicWorldMeshProfile& b) noexcept
    {
        return a.contractVersion == b.contractVersion && a.worldSpaceStatic == b.worldSpaceStatic
            && a.indexPolicy == b.indexPolicy && a.maxVertices == b.maxVertices
            && a.maxIndices == b.maxIndices && a.maxSections == b.maxSections
            && a.materialCount == b.materialCount && a.instanceCount == b.instanceCount
            && a.maxVertexBytes == b.maxVertexBytes && a.maxIndexBytes == b.maxIndexBytes;
    }

    static bool sameItem(
        const scene3d::Scene3DRenderItem& a, const scene3d::Scene3DRenderItem& b) noexcept
    {
        const auto& x = a.worldTransform;
        const auto& y = b.worldTransform;
        return a.kind == b.kind && a.objectId == b.objectId && a.mesh == b.mesh
            && a.terrain == b.terrain && a.material == b.material
            && x.translation.x == y.translation.x && x.translation.y == y.translation.y
            && x.translation.z == y.translation.z && x.scale.x == y.scale.x
            && x.scale.y == y.scale.y && x.scale.z == y.scale.z && x.rotation.x == y.rotation.x
            && x.rotation.y == y.rotation.y && x.rotation.z == y.rotation.z
            && x.rotation.w == y.rotation.w && a.baseColorTint.x == b.baseColorTint.x
            && a.baseColorTint.y == b.baseColorTint.y && a.baseColorTint.z == b.baseColorTint.z
            && a.baseColorTint.w == b.baseColorTint.w;
    }

    static const char* validateItem(const scene3d::Scene3DRenderItem& item) noexcept
    {
        const auto& t = item.worldTransform;
        if (!item.objectId.valid() || !item.material.valid()
            || (item.kind == scene3d::Scene3DRenderItemKind::Mesh && !item.mesh.valid())
            || (item.kind == scene3d::Scene3DRenderItemKind::Terrain && !item.terrain.valid()))
            return "SCENE-OPAQUE-ITEM-IDENTITY";
        if (item.kind != scene3d::Scene3DRenderItemKind::Mesh
            && item.kind != scene3d::Scene3DRenderItemKind::Terrain)
            return "SCENE-OPAQUE-ITEM-KIND";
        for (const auto value : {t.translation.x,
                 t.translation.y,
                 t.translation.z,
                 t.scale.x,
                 t.scale.y,
                 t.scale.z,
                 t.rotation.x,
                 t.rotation.y,
                 t.rotation.z,
                 t.rotation.w})
            if (!std::isfinite(value))
                return "SCENE-OPAQUE-TRANSFORM-NONFINITE";
        const auto rotationLength = t.rotation.x * t.rotation.x + t.rotation.y * t.rotation.y
            + t.rotation.z * t.rotation.z + t.rotation.w * t.rotation.w;
        if (!std::isfinite(rotationLength) || rotationLength <= 0.f || t.scale.x == 0.f
            || t.scale.y == 0.f || t.scale.z == 0.f)
            return "SCENE-OPAQUE-TRANSFORM-SINGULAR";
        for (const auto value : {item.baseColorTint.x, item.baseColorTint.y, item.baseColorTint.z})
            if (!std::isfinite(value) || value < 0.f || value > 1.f)
                return "SCENE-OPAQUE-TINT";
        return item.baseColorTint.w == 1.f ? nullptr : "SCENE-OPAQUE-TINT";
    }
};

class SceneRasterContract final {
public:
    static const char* validateState(const rhi::RenderPipelineDesc& d)
    {
        using namespace rhi;
        if (!d.layout || !d.shaderPackage || !d.vertexBuffers.empty() || !d.dynamicViewportScissor
            || d.topology != PrimitiveTopology::TriangleList || d.cullMode != CullMode::None
            || d.frontFace != FrontFace::CounterClockwise || d.hasViewport || d.hasScissor
            || d.multisample.sampleCount != 1u || d.multisample.alphaToCoverageEnabled
            || d.colorFormat != TextureFormat::Rgba16Float || d.colorTargets.size() != 1u
            || d.colorTargets[0].format != d.colorFormat || d.colorTargets[0].blendEnabled
            || d.colorTargets[0].writeMask != ColorWriteMask::All
            || d.colorTargets[0].resolveTargetRequired || d.depthFormat != TextureFormat::D32Float
            || !d.depthStencil.depthTestEnabled || !d.depthStencil.depthWriteEnabled
            || d.depthStencil.depthCompare != CompareOp::Less || d.depthStencil.stencilEnabled
            || d.depthStencil.depthBiasEnabled)
            return "SCENE-OPAQUE-PIPELINE-STATE";
        return nullptr;
    }

    static const char* makeBuffer(rhi::GpuDevice& device,
        const void* data,
        std::uint64_t size,
        rhi::ResourceUsage usage,
        const SceneOpaqueProducerBudget& budget,
        std::shared_ptr<const rhi::OriginalCommandBufferLease>& output)
    {
        using namespace rhi;
        using Lease = OriginalCommandBufferLease;
        auto capacity = core::makeShared<core::AtomicCapacityLease>(budget.retainedGpuBytes, size);
        if (!capacity->charged())
            return "SCENE-OPAQUE-GPU-BUDGET";
        auto object = device.createBuffer({"SceneOpaque.Immutable",
            size,
            usage,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead});
        if (!object)
            return "SCENE-OPAQUE-BUFFER-CREATE";
        if (const auto* id = device.validateOriginalResourceRegistration(*object))
            return id;
        const auto initialized = device.writeBuffer(*object, data, size);
        if (!initialized.success)
            return "SCENE-OPAQUE-BUFFER-INITIALIZATION";
        object->sealWrites();
        const char* id = nullptr;
        auto lease = Lease::prepareColdOriginal(object,
            {1u, true, Lease::retainedOwnerCount, budget.sourceQuota->limitBytes()},
            budget.sourceQuota,
            id,
            {capacity, {}});
        if (!lease || (id = Lease::adoptOriginal(lease, object)))
            return id ? id : "SCENE-OPAQUE-BUFFER-LEASE";
        output = std::move(lease);
        return nullptr;
    }
};
} // namespace arp::renderer::detail
