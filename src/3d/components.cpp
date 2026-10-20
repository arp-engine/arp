#include "3d/components.h"

#include "resource/resource_manager.h"

#include <cmath>
#include <utility>

namespace arp::scene3d {
namespace {

bool isOptionalStableResourceId(const core::pmr::String& id)
{
    return id.empty() || resource::isResourceUuid(std::string_view(id.data(), id.size()));
}

} // namespace

TerrainMetadata TerrainMetadata::explicitGrid(
    uint32_t tileCountX, uint32_t tileCountZ, float metersPerTile)
{
    TerrainMetadata metadata;
    metadata.tileCountX = tileCountX;
    metadata.tileCountZ = tileCountZ;
    metadata.metersPerTile = metersPerTile;
    metadata.hasExplicitMetadata = true;
    return metadata;
}

bool TerrainMetadata::valid() const
{
    return hasExplicitMetadata && tileCountX > 0 && tileCountZ > 0 && metersPerTile > 0.0f;
}

MeshComponent::MeshComponent(MeshComponentDesc desc) : desc_(std::move(desc))
{}

bool MeshComponent::applyEditorAuthoringProperties(const arp::reflection::object& value)
{
    if (!value.is<MeshComponentEditorProperties>())
        return false;
    const auto& properties = value.cast<const MeshComponentEditorProperties&>();
    if (!isOptionalStableResourceId(properties.resourceId)
        || !isOptionalStableResourceId(properties.materialResourceId))
        return false;
    if (!std::isfinite(properties.baseColor.x) || !std::isfinite(properties.baseColor.y)
        || !std::isfinite(properties.baseColor.z) || !std::isfinite(properties.baseColor.w))
        return false;
    resourceId_ = properties.resourceId;
    materialResourceId_ = properties.materialResourceId;
    desc_.baseColorTint = properties.baseColor;
    return true;
}

bool TerrainComponent::applyEditorAuthoringProperties(const arp::reflection::object& value)
{
    if (!value.is<TerrainComponentEditorProperties>())
        return false;
    const auto& properties = value.cast<const TerrainComponentEditorProperties&>();
    if (!isOptionalStableResourceId(properties.resourceId))
        return false;
    resourceId_ = properties.resourceId;
    return true;
}

TerrainComponent::TerrainComponent(TerrainComponentDesc desc) : desc_(std::move(desc))
{}

LightComponent::LightComponent(LightComponentDesc desc) : desc_(std::move(desc))
{}

void MeshComponent::extract(
    const arp::scene::RenderExtractContext& ctx, arp::scene::RenderExtractSink& sink) const
{
    // Self-gate on active/enabled: when disabled, push a disabled marker
    // (counted by the sink) instead of the render payload. This preserves the
    // original is3DComponent()-filtered disabled-component telemetry without
    // the extractor needing to dynamic_cast.
    if (!ctx.activeInHierarchy || !enabled()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene3d.disabled";
        sink.accept(ctx, marker);
        return;
    }
    Scene3DMeshExtractPayload domain{
        desc_.mesh,
        desc_.material,
        desc_.localBounds,
        desc_.baseColorTint,
    };
    arp::scene::RenderExtractPayload payload;
    payload.kind = arp::scene::RenderExtractKind::Custom;
    payload.typeId = "scene3d.mesh";
    payload.data = &domain;
    sink.accept(ctx, payload);
}

void TerrainComponent::extract(
    const arp::scene::RenderExtractContext& ctx, arp::scene::RenderExtractSink& sink) const
{
    if (!ctx.activeInHierarchy || !enabled()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene3d.disabled";
        sink.accept(ctx, marker);
        return;
    }
    Scene3DTerrainExtractPayload domain{
        desc_.terrain, desc_.material, desc_.localBounds, desc_.metadata};
    arp::scene::RenderExtractPayload payload;
    payload.kind = arp::scene::RenderExtractKind::Custom;
    payload.typeId = "scene3d.terrain";
    payload.data = &domain;
    sink.accept(ctx, payload);
}

void LightComponent::extract(
    const arp::scene::RenderExtractContext& ctx, arp::scene::RenderExtractSink& sink) const
{
    if (!ctx.activeInHierarchy || !enabled()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene3d.disabled";
        sink.accept(ctx, marker);
        return;
    }

    const auto& d = desc_;
    Scene3DLightExtractPayload domain{
        d.type,
        d.color,
        d.direction,
        d.intensity,
        d.range,
        d.decay,
        d.spotConeAngle,
        d.spotPenumbra,
        d.castShadow,
    };
    arp::scene::RenderExtractPayload payload;
    payload.kind = arp::scene::RenderExtractKind::Custom;
    payload.typeId = "scene3d.light";
    payload.data = &domain;
    sink.accept(ctx, payload);
}

} // namespace arp::scene3d
