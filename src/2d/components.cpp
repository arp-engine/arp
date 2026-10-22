#include "2d/components.h"

#include <utility>

namespace arp::scene2d {

Scene2DChunkGrid Scene2DChunkGrid::explicitGrid(
    arp::core::Vec3 origin, float chunkWorldSize, int chunkCountX, int chunkCountY)
{
    Scene2DChunkGrid grid;
    grid.origin = origin;
    grid.chunkWorldSize = chunkWorldSize;
    grid.chunkCountX = chunkCountX;
    grid.chunkCountY = chunkCountY;
    grid.hasExplicitMetadata = true;
    return grid;
}

bool Scene2DChunkGrid::valid() const
{
    return hasExplicitMetadata && chunkWorldSize > 0.0f && chunkCountX > 0 && chunkCountY > 0;
}

TileMapMetadata TileMapMetadata::explicitTiles(
    uint32_t tileCountX, uint32_t tileCountY, float worldUnitsPerTile)
{
    TileMapMetadata metadata;
    metadata.tileCountX = tileCountX;
    metadata.tileCountY = tileCountY;
    metadata.worldUnitsPerTile = worldUnitsPerTile;
    metadata.hasExplicitMetadata = true;
    return metadata;
}

bool TileMapMetadata::valid() const
{
    return hasExplicitMetadata && tileCountX > 0 && tileCountY > 0 && worldUnitsPerTile > 0.0f;
}

SpriteRendererComponent::SpriteRendererComponent(SpriteRendererComponentDesc desc)
    : desc_(std::move(desc))
{}

bool SpriteRendererComponent::applyEditorAuthoringProperties(const arp::reflection::object& value)
{
    if (!value.is<SpriteRendererComponentEditorProperties>())
        return false;
    const auto& properties = value.cast<const SpriteRendererComponentEditorProperties&>();
    const auto& bounds = properties.localBounds;
    if (bounds.hasExplicitMetadata && !bounds.valid())
        return false;
    if (!std::isfinite(properties.tint.x) || !std::isfinite(properties.tint.y)
        || !std::isfinite(properties.tint.z) || !std::isfinite(properties.tint.w)
        || properties.tint.w < 0 || properties.tint.w > 1)
        return false;
    desc_ = properties;
    return true;
}

TileMapComponent::TileMapComponent(TileMapComponentDesc desc) : desc_(std::move(desc))
{}

bool TileMapComponent::applyEditorAuthoringProperties(const arp::reflection::object& value)
{
    if (!value.is<TileMapComponentEditorProperties>())
        return false;
    const auto& properties = value.cast<const TileMapComponentEditorProperties&>();
    if ((properties.grid.hasExplicitMetadata && !properties.grid.valid())
        || (properties.metadata.hasExplicitMetadata && !properties.metadata.valid()))
        return false;
    desc_.grid = properties.grid;
    desc_.metadata = properties.metadata;
    return true;
}

void SpriteRendererComponent::extract(
    const arp::scene::RenderExtractContext& ctx, arp::scene::RenderExtractSink& sink) const
{
    if (!ctx.activeInHierarchy || !enabled()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene2d.disabled";
        sink.accept(ctx, marker);
        return;
    }
    Scene2DSpriteExtractPayload domain{
        desc_.sprite, desc_.material, desc_.texture, desc_.localBounds};
    arp::scene::RenderExtractPayload payload;
    payload.kind = arp::scene::RenderExtractKind::Custom;
    payload.typeId =
        desc_.spriteResourceId.empty() ? "scene2d.sprite" : "scene2d.resource-sprite/1";
    payload.data = desc_.spriteResourceId.empty() ? static_cast<const void*>(&domain)
                                                  : static_cast<const void*>(&desc_);
    sink.accept(ctx, payload);
}

void TileMapComponent::extract(
    const arp::scene::RenderExtractContext& ctx, arp::scene::RenderExtractSink& sink) const
{
    if (!ctx.activeInHierarchy || !enabled()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene2d.disabled";
        sink.accept(ctx, marker);
        return;
    }
    Scene2DTileMapExtractPayload domain{
        desc_.tileMap, desc_.material, desc_.texture, desc_.grid, desc_.metadata};
    arp::scene::RenderExtractPayload payload;
    payload.kind = arp::scene::RenderExtractKind::Custom;
    payload.typeId = "scene2d.tilemap";
    payload.data = &domain;
    sink.accept(ctx, payload);
}

} // namespace arp::scene2d
