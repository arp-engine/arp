#pragma once
#include "2d/sprite_resources.h"
#include "physics2d/physics_world_2d.h"

namespace arp::scene2d {
struct TileDefinition {
    std::uint32_t id = 0;
    core::pmr::String visualId{&core::defaultCpuMemoryResource()};
    bool animated = false, solid = false;
    resource::ResourceHandle<SpriteResource> sprite;
    resource::ResourceHandle<SpriteAnimationResource> animation;
    physics2d::ShapeDesc collision;
};

class TileSetResource final : public resource::ResourceBase {
public:
    TileSetResource(core::pmr::String id, core::pmr::String path, std::size_t maxBytes);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;
    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String&) const override;
    const TileDefinition* tile(std::uint32_t id) const;

    float tileWorldSize() const { return worldSize_; }

private:
    core::pmr::String path_;
    std::size_t maxBytes_;
    float worldSize_ = 0;
    core::pmr::Vector<TileDefinition> tiles_{&core::defaultCpuMemoryResource()};
};

struct TileCell {
    std::uint32_t tileId = 0, x = 0, y = 0, quarterTurns = 0;
    bool flipX = false, flipY = false;
};

struct TileLayer {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    std::int32_t sortingLayer = 0, sortingOrder = 0;
    core::pmr::Vector<TileCell> cells{&core::defaultCpuMemoryResource()};
};

struct TileChunk {
    std::int32_t x = 0, y = 0;
    core::pmr::Vector<TileLayer> layers{&core::defaultCpuMemoryResource()};
};

class TileMapResource final : public resource::ResourceBase {
public:
    TileMapResource(core::pmr::String id,
        core::pmr::String tileSetId,
        core::pmr::String path,
        std::size_t maxBytes);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;
    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String&) const override;

    const auto& chunks() const { return chunks_; }

    const auto& tileSetId() const { return tileSetId_; }

    std::uint32_t chunkTiles() const { return chunkTiles_; }

    core::Vec2 origin() const { return origin_; }

private:
    core::pmr::String tileSetId_, path_;
    std::size_t maxBytes_;
    std::uint32_t chunkTiles_ = 0;
    core::Vec2 origin_{};
    core::pmr::Vector<TileChunk> chunks_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::scene2d
