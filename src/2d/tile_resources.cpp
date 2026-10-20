#include "2d/tile_resources.h"

#include "core/yaml_contract.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::scene2d {
using Y = core::YamlContract;

TileSetResource::TileSetResource(core::pmr::String id, core::pmr::String path, std::size_t budget)
    : ResourceBase(resource::authoredResourceReference(std::move(id), "TileSetResource")),
      path_(std::move(path)), maxBytes_(budget)
{}

bool TileSetResource::collectFileReadIntents(resource::ResourceManager&,
    core::pmr::Vector<resource::ResourceFileReadIntent>& out,
    core::pmr::String& d) const
{
    return resource::appendResourceReadIntent(path_, maxBytes_, out, d);
}

const TileDefinition* TileSetResource::tile(std::uint32_t id) const
{
    for (const auto& t : tiles_)
        if (t.id == id)
            return &t;
    return nullptr;
}

resource::ResourceLoadResult TileSetResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& context)
{
    auto fail = [&](const char* d) {
        return resource::resourceLoadFailure(*this, d);
    };
    auto source = resource::readResourceSource(path_, context, maxBytes_);
    if (!source.success)
        return fail(source.diagnostics.c_str());
    try {
        auto n = Y::Node::deserialize(core::pmr::String(
            reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()));
        if (Y::text(n, "type") != "arp.tile-set" || Y::uint32(n, "contractVersion") != 1
            || Y::text(n, "resourceId") != reference().id)
            return fail("ARP-2D-TILESET-CONTRACT");
        const float size = float(Y::number(n, "tileWorldSize"));
        if (!std::isfinite(size) || size <= 0)
            return fail("ARP-2D-TILESET-SIZE");
        core::pmr::Vector<TileDefinition> tiles;
        for (const auto& node : Y::sequence(n, "tiles")) {
            TileDefinition t;
            t.id = Y::uint32(node, "id");
            t.visualId = Y::text(node, "visualId");
            t.animated = Y::boolean(node, "animated");
            t.solid = Y::boolean(node, "solid");
            if (!t.id || !resource::isResourceUuid(t.visualId)
                || std::any_of(tiles.begin(), tiles.end(), [&](const auto& v) {
                       return v.id == t.id;
                   }))
                return fail("ARP-2D-TILESET-ID");
            if (t.animated) {
                t.animation = manager.find<SpriteAnimationResource>(t.visualId);
                if (!t.animation.ready())
                    return fail("ARP-2D-TILESET-VISUAL");
            } else {
                t.sprite = manager.find<SpriteResource>(t.visualId);
                if (!t.sprite.ready())
                    return fail("ARP-2D-TILESET-VISUAL");
            }
            if (t.solid) {
                const auto& collision = Y::required(node, "collision");
                if (Y::text(collision, "shape") != "box")
                    return fail("ARP-2D-TILESET-COLLISION-SHAPE");
                t.collision.type = physics2d::ShapeType::Box;
                t.collision.halfSize = {float(Y::number(collision, "halfWidth")),
                    float(Y::number(collision, "halfHeight"))};
                t.collision.center = {
                    float(Y::number(collision, "centerX")), float(Y::number(collision, "centerY"))};
                t.collision.friction = float(Y::number(collision, "friction"));
                t.collision.restitution = float(Y::number(collision, "restitution"));
                t.collision.filter.category = Y::uint64(Y::required(collision, "category"));
                t.collision.filter.mask = Y::uint64(Y::required(collision, "mask"));
                if (t.collision.halfSize.x <= 0 || t.collision.halfSize.y <= 0
                    || t.collision.halfSize.x > size * .5f || t.collision.halfSize.y > size * .5f
                    || std::abs(t.collision.center.x) + t.collision.halfSize.x > size * .5f
                    || std::abs(t.collision.center.y) + t.collision.halfSize.y > size * .5f
                    || t.collision.friction < 0 || t.collision.friction > 1
                    || t.collision.restitution < 0 || t.collision.restitution > 1)
                    return fail("ARP-2D-TILESET-COLLISION");
            }
            tiles.push_back(std::move(t));
        }
        if (tiles.empty())
            return fail("ARP-2D-TILESET-EMPTY");
        for (const auto& t : tiles)
            addDependency({t.visualId, "tile-visual"});
        worldSize_ = size;
        tiles_ = std::move(tiles);
        return manager.admitResourceMetadata(*this, context.policy);
    } catch (...) {
        return fail("ARP-2D-TILESET-YAML");
    }
}

TileMapResource::TileMapResource(
    core::pmr::String id, core::pmr::String set, core::pmr::String path, std::size_t budget)
    : ResourceBase(resource::authoredResourceReference(std::move(id), "TileMapResource")),
      tileSetId_(std::move(set)), path_(std::move(path)), maxBytes_(budget)
{
    addDependency({tileSetId_, "tile-set"});
}

bool TileMapResource::collectFileReadIntents(resource::ResourceManager&,
    core::pmr::Vector<resource::ResourceFileReadIntent>& out,
    core::pmr::String& d) const
{
    return resource::appendResourceReadIntent(path_, maxBytes_, out, d);
}

resource::ResourceLoadResult TileMapResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& context)
{
    auto fail = [&](const char* d) {
        return resource::resourceLoadFailure(*this, d);
    };
    auto set = manager.find<TileSetResource>(tileSetId_);
    if (!set.ready())
        return fail("ARP-2D-TILEMAP-TILESET");
    auto source = resource::readResourceSource(path_, context, maxBytes_);
    if (!source.success)
        return fail(source.diagnostics.c_str());
    try {
        auto n = Y::Node::deserialize(core::pmr::String(
            reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()));
        if (Y::text(n, "type") != "arp.tile-map" || Y::uint32(n, "contractVersion") != 1
            || Y::text(n, "resourceId") != reference().id || Y::text(n, "tileSetId") != tileSetId_)
            return fail("ARP-2D-TILEMAP-CONTRACT");
        const auto count = Y::uint32(n, "chunkTiles");
        if (!count || count > INT32_MAX)
            return fail("ARP-2D-TILEMAP-CHUNK-SIZE");
        const core::Vec2 origin{float(Y::number(n, "originX")), float(Y::number(n, "originY"))};
        core::pmr::Vector<TileChunk> chunks;
        for (const auto& node : Y::sequence(n, "chunks")) {
            TileChunk chunk;
            chunk.x = Y::int32(node, "x");
            chunk.y = Y::int32(node, "y");
            if (std::any_of(chunks.begin(), chunks.end(), [&](const auto& c) {
                    return c.x == chunk.x && c.y == chunk.y;
                }))
                return fail("ARP-2D-TILEMAP-CHUNK-DUPLICATE");
            for (const auto& layerNode : Y::sequence(node, "layers")) {
                TileLayer layer;
                layer.name = Y::text(layerNode, "name");
                layer.sortingLayer = Y::int32(layerNode, "sortingLayer");
                layer.sortingOrder = Y::int32(layerNode, "sortingOrder");
                if (layer.name.empty()
                    || std::any_of(chunk.layers.begin(), chunk.layers.end(), [&](const auto& v) {
                           return v.name == layer.name;
                       }))
                    return fail("ARP-2D-TILEMAP-LAYER");
                for (const auto& cellNode : Y::sequence(layerNode, "cells")) {
                    TileCell cell;
                    cell.x = Y::uint32(cellNode, "x");
                    cell.y = Y::uint32(cellNode, "y");
                    cell.tileId = Y::uint32(cellNode, "tileId");
                    cell.quarterTurns = Y::uint32(cellNode, "quarterTurns");
                    cell.flipX = Y::boolean(cellNode, "flipX");
                    cell.flipY = Y::boolean(cellNode, "flipY");
                    if (cell.x >= count || cell.y >= count || cell.quarterTurns > 3
                        || !set.get()->tile(cell.tileId)
                        || std::any_of(layer.cells.begin(), layer.cells.end(), [&](const auto& c) {
                               return c.x == cell.x && c.y == cell.y;
                           }))
                        return fail("ARP-2D-TILEMAP-CELL");
                    layer.cells.push_back(cell);
                }
                chunk.layers.push_back(std::move(layer));
            }
            chunks.push_back(std::move(chunk));
        }
        if (chunks.empty() || !std::isfinite(origin.x) || !std::isfinite(origin.y))
            return fail("ARP-2D-TILEMAP-EMPTY-OR-ORIGIN");
        chunkTiles_ = count;
        origin_ = origin;
        chunks_ = std::move(chunks);
        return manager.admitResourceMetadata(*this, context.policy);
    } catch (...) {
        return fail("ARP-2D-TILEMAP-YAML");
    }
}
} // namespace arp::scene2d
