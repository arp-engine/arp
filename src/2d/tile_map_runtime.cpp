#include "2d/tile_map_runtime.h"

#include <algorithm>
#include <limits>

namespace arp::scene2d {
TileMapRuntime::TileMapRuntime(resource::ResourceManager& resources,
    resource::ResourceHandle<TileMapResource> map,
    physics2d::PhysicsWorld2D& world,
    TileMapRuntimeDesc desc)
    : resources_(resources), map_(map), world_(world), desc_(desc)
{
    resident_.reserve(desc.maxResidentChunks);
    pending_.reserve(desc.maxResidentChunks);
    wanted_.reserve(desc.maxResidentChunks);
    items_.reserve(desc.maxVisibleCells);
    if (map_.ready())
        set_ = resources_.find<TileSetResource>(map_.get()->tileSetId());
}

TileMapRuntime::~TileMapRuntime()
{
    for (const auto& r : resident_)
        if (r.body)
            world_.destroyBody(r.body);
}

bool TileMapRuntime::admitted()
{
    if (!map_.ready() || !set_.ready())
        return fail("ARP-2D-TILEMAP-RESOURCE-STALE");
    if (!world_.valid() || !desc_.maxResidentChunks || !desc_.maxVisibleCells
        || !desc_.firstObjectId)
        return fail("ARP-2D-TILEMAP-RUNTIME-CONFIG");
    return true;
}

core::Vec2 TileMapRuntime::center(const TileChunk& chunk, const TileCell& cell) const
{
    const double size = set_.get()->tileWorldSize();
    const auto origin = map_.get()->origin();
    const auto count = map_.get()->chunkTiles();
    return {float(origin.x + (double(chunk.x) * count + cell.x + .5) * size),
        float(origin.y + (double(chunk.y) * count + cell.y + .5) * size)};
}

bool TileMapRuntime::updateResidency(core::Rect2<float> visible)
{
    diagnostic_ = nullptr;
    if (!admitted())
        return false;
    if (!core::rectHasPositiveArea(visible) || !std::isfinite(visible.x)
        || !std::isfinite(visible.y) || !std::isfinite(visible.width)
        || !std::isfinite(visible.height))
        return fail("ARP-2D-TILEMAP-VIEWPORT");
    wanted_.clear();
    pending_.clear();
    const auto origin = map_.get()->origin();
    const float span = float(double(map_.get()->chunkTiles()) * set_.get()->tileWorldSize());
    std::uint64_t cells = 0;
    for (std::uint32_t i = 0; i < map_.get()->chunks().size(); ++i) {
        const auto& chunk = map_.get()->chunks()[i];
        core::Rect2<float> bounds{
            origin.x + float(chunk.x) * span, origin.y + float(chunk.y) * span, span, span};
        if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(span))
            return fail("ARP-2D-TILEMAP-WORLD-RANGE");
        if (!core::rectsIntersect(bounds, visible))
            continue;
        if (wanted_.size() == desc_.maxResidentChunks)
            return fail("ARP-2D-TILEMAP-RESIDENT-CAPACITY");
        wanted_.push_back(i);
        for (const auto& layer : chunk.layers)
            cells += layer.cells.size();
        if (cells > desc_.maxVisibleCells)
            return fail("ARP-2D-TILEMAP-CELL-CAPACITY");
    }
    // Allocate additions first; failure rolls them back and preserves old residency.
    for (auto index : wanted_) {
        if (std::any_of(resident_.begin(), resident_.end(), [&](const auto& r) {
                return r.index == index;
            }))
            continue;
        const auto& chunk = map_.get()->chunks()[index];
        Resident addition{index, {}};
        for (const auto& layer : chunk.layers)
            for (const auto& cell : layer.cells) {
                const auto* tile = set_.get()->tile(cell.tileId);
                if (!tile->solid)
                    continue;
                if (!addition.body) {
                    physics2d::BodyDesc body;
                    addition.body = world_.createBody(body);
                }
                bool success = bool(addition.body);
                if (success) {
                    auto shape = tile->collision;
                    const auto position = center(chunk, cell);
                    const bool odd = cell.quarterTurns % 2 != 0;
                    // Box shape has no local angle; quarter-turn exchanges extents.
                    if (odd)
                        std::swap(shape.halfSize.x, shape.halfSize.y);
                    auto offset = tile->collision.center;
                    if (cell.flipX)
                        offset.x = -offset.x;
                    if (cell.flipY)
                        offset.y = -offset.y;
                    for (std::uint32_t turn = 0; turn < cell.quarterTurns; ++turn)
                        offset = {-offset.y, offset.x};
                    shape.center = {position.x + offset.x, position.y + offset.y};
                    success = bool(world_.createCollider(addition.body, shape));
                }
                if (!success) {
                    if (addition.body)
                        world_.destroyBody(addition.body);
                    for (const auto& r : pending_)
                        if (r.body)
                            world_.destroyBody(r.body);
                    pending_.clear();
                    return fail("ARP-2D-TILEMAP-COLLISION-ADMISSION");
                }
            }
        pending_.push_back(addition);
    }
    for (const auto& r : resident_)
        if (std::find(wanted_.begin(), wanted_.end(), r.index) == wanted_.end() && r.body)
            world_.destroyBody(r.body);
    resident_.erase(std::remove_if(resident_.begin(),
                        resident_.end(),
                        [&](const auto& r) {
                            return std::find(wanted_.begin(), wanted_.end(), r.index)
                                == wanted_.end();
                        }),
        resident_.end());
    resident_.insert(resident_.end(), pending_.begin(), pending_.end());
    pending_.clear();
    items_.clear();
    return true;
}

bool TileMapRuntime::extract(double seconds)
{
    diagnostic_ = nullptr;
    items_.clear();
    if (!admitted())
        return false;
    if (!std::isfinite(seconds) || seconds < 0)
        return fail("ARP-2D-TILEMAP-TIME");
    std::uint64_t ordinal = 0;
    auto failure = [&](const char* d) {
        items_.clear();
        return fail(d);
    };
    for (std::uint32_t index = 0; index < map_.get()->chunks().size(); ++index) {
        const bool resident = std::any_of(resident_.begin(), resident_.end(), [&](const auto& r) {
            return r.index == index;
        });
        const auto& chunk = map_.get()->chunks()[index];
        for (const auto& layer : chunk.layers)
            for (const auto& cell : layer.cells) {
                if (ordinal > UINT64_MAX - desc_.firstObjectId)
                    return failure("ARP-2D-TILEMAP-OBJECT-ID-OVERFLOW");
                const auto objectId = desc_.firstObjectId + ordinal++;
                if (!resident)
                    continue;
                const auto* tile = set_.get()->tile(cell.tileId);
                auto sprite = tile->sprite;
                if (tile->animated) {
                    auto animation = tile->animation;
                    if (!animation.ready() || !animation.get()->loop())
                        return failure("ARP-2D-TILEMAP-ANIMATION-CONTRACT");
                    double duration = 0;
                    for (const auto& f : animation.get()->frames())
                        duration += f.seconds;
                    double time = std::fmod(seconds, duration);
                    sprite = animation.get()->frames().back().sprite;
                    for (const auto& f : animation.get()->frames()) {
                        if (time < f.seconds) {
                            sprite = f.sprite;
                            break;
                        }
                        time -= f.seconds;
                    }
                }
                if (!sprite.ready())
                    return failure("ARP-2D-TILEMAP-VISUAL-STALE");
                const auto* slice = sprite.get()->slice();
                if (!slice)
                    return failure("ARP-2D-TILEMAP-SLICE");
                SpriteDrawItem item;
                item.sprite = sprite;
                const auto position = center(chunk, cell);
                const float size = set_.get()->tileWorldSize();
                constexpr float halfPi = 1.57079632679489661923f;
                const float angle = cell.quarterTurns * halfPi;
                item.world.rotation = {0, 0, std::sin(angle * .5f), std::cos(angle * .5f)};
                const auto offset = core::rotate(item.world.rotation,
                    {(slice->pivot.x - .5f) * size, (slice->pivot.y - .5f) * size, 0});
                item.world.translation = {position.x + offset.x, position.y + offset.y, 0};
                item.world.scale = {size * slice->pixelsPerUnit / slice->width,
                    size * slice->pixelsPerUnit / slice->height,
                    1};
                item.sorting = {
                    layer.sortingLayer, layer.sortingOrder, false, position.y, objectId};
                item.flipX = cell.flipX;
                item.flipY = cell.flipY;
                if (items_.size() == desc_.maxVisibleCells)
                    return failure("ARP-2D-TILEMAP-CELL-CAPACITY");
                items_.push_back(std::move(item));
            }
    }
    return true;
}
} // namespace arp::scene2d
