#pragma once
#include "2d/sprite_draw_plan.h"
#include "2d/tile_resources.h"

namespace arp::scene2d {
struct TileMapRuntimeDesc {
    std::uint32_t maxResidentChunks = 0, maxVisibleCells = 0;
    std::uint64_t firstObjectId = 0;
};

// Resource data remains CPU resident; only intersecting chunks own physics and
// draw entries. Preparation/removal is explicit, outside physics.step().
class TileMapRuntime final {
public:
    TileMapRuntime(resource::ResourceManager&,
        resource::ResourceHandle<TileMapResource>,
        physics2d::PhysicsWorld2D&,
        TileMapRuntimeDesc);
    ~TileMapRuntime();
    TileMapRuntime(const TileMapRuntime&) = delete;
    TileMapRuntime& operator=(const TileMapRuntime&) = delete;
    bool updateResidency(core::Rect2<float> visibleWorld);
    bool extract(double gameSeconds);

    const auto& items() const { return items_; }

    std::size_t residentCount() const { return resident_.size(); }

    const char* diagnostic() const { return diagnostic_; }

private:
    struct Resident {
        std::uint32_t index;
        physics2d::BodyHandle body;
    };

    bool fail(const char* d)
    {
        diagnostic_ = d;
        return false;
    }

    bool admitted();
    core::Vec2 center(const TileChunk&, const TileCell&) const;
    resource::ResourceManager& resources_;
    resource::ResourceHandle<TileMapResource> map_;
    resource::ResourceHandle<TileSetResource> set_;
    physics2d::PhysicsWorld2D& world_;
    TileMapRuntimeDesc desc_;
    const char* diagnostic_ = nullptr;
    core::pmr::Vector<Resident> resident_, pending_;
    core::pmr::Vector<std::uint32_t> wanted_;
    core::pmr::Vector<SpriteDrawItem> items_;
};
} // namespace arp::scene2d
