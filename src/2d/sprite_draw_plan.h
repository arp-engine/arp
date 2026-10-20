#pragma once
#include "2d/runtime_contract.h"
#include "2d/sprite_resources.h"
#include "core/geometry.h"
#include "scene/transform.h"

#include <algorithm>

namespace arp::scene2d {
struct SpriteDrawItem {
    resource::ResourceHandle<SpriteResource> sprite;
    scene::Transform world;
    core::Vec4 color{1, 1, 1, 1};
    SpriteOrder sorting;
    bool flipX = false, flipY = false;
};

// Six float4 fields, identical layout in Slang. No matrix crosses this boundary.
struct SpriteInstance {
    core::Vec4 origin, axisX, axisY, uv, color, parameters;
};

static_assert(sizeof(SpriteInstance) == 96);

struct SpriteBatch {
    resource::ResourceHandle<SpriteAtlasResource> atlas;
    std::uint32_t first = 0, count = 0;
};

class SpriteDrawPlan final {
public:
    explicit SpriteDrawPlan(std::uint32_t capacity) : capacity_(capacity)
    {
        items_.reserve(capacity);
        instances_.reserve(capacity);
        batches_.reserve(capacity);
        ids_.reserve(capacity);
    }

    bool build(resource::ResourceManager& manager,
        const core::pmr::Vector<SpriteDrawItem>& input,
        const core::Rect2<float>* visibleWorld = nullptr)
    {
        diagnostic_ = nullptr;
        items_.clear();
        instances_.clear();
        batches_.clear();
        ids_.clear();
        culled_ = 0;
        if (visibleWorld
            && (!core::rectHasPositiveArea(*visibleWorld) || !std::isfinite(visibleWorld->x)
                || !std::isfinite(visibleWorld->y) || !std::isfinite(visibleWorld->width)
                || !std::isfinite(visibleWorld->height)))
            return fail("ARP-2D-VIEWPORT-INVALID");
        if (input.size() > capacity_)
            return fail("ARP-2D-DRAW-BUDGET");
        for (const auto& i : input) {
            if (!i.sprite.ready() || !std::isfinite(i.sorting.worldY) || !i.sorting.objectId)
                return fail("ARP-2D-DRAW-RESOURCE-OR-ORDER");
            // 2D rotations must be explicitly about Z; never project an arbitrary 3D rotation.
            if (!std::isfinite(i.world.rotation.w) || !std::isfinite(i.world.rotation.z)
                || i.world.rotation.x != 0 || i.world.rotation.y != 0
                || std::abs(core::dot(i.world.rotation, i.world.rotation) - 1) > 1e-4f)
                return fail("ARP-2D-ROTATION-NOT-PLANAR");
            for (float f : {i.world.translation.x,
                     i.world.translation.y,
                     i.world.scale.x,
                     i.world.scale.y,
                     i.color.x,
                     i.color.y,
                     i.color.z,
                     i.color.w})
                if (!std::isfinite(f))
                    return fail("ARP-2D-DRAW-NONFINITE");
            if (i.color.w < 0 || i.color.w > 1)
                return fail("ARP-2D-DRAW-ALPHA");
            items_.push_back(i);
            ids_.push_back(i.sorting.objectId);
        }
        std::sort(ids_.begin(), ids_.end());
        if (std::adjacent_find(ids_.begin(), ids_.end()) != ids_.end())
            return fail("ARP-2D-DUPLICATE-OBJECT-ID");
        std::sort(items_.begin(), items_.end(), [](const auto& a, const auto& b) {
            return spriteOrderLess(a.sorting, b.sorting);
        });
        for (std::size_t i = 1; i < items_.size(); ++i)
            if (items_[i].sorting.layer == items_[i - 1].sorting.layer
                && items_[i].sorting.ySort != items_[i - 1].sorting.ySort)
                return fail("ARP-2D-YSORT-LAYER-POLICY");
        for (const auto& item : items_) {
            auto atlas = item.sprite.get()->atlas();
            if (!atlas.ready())
                return fail("ARP-2D-DRAW-ATLAS-STALE");
            auto* s = item.sprite.get()->slice();
            if (!s)
                return fail("ARP-2D-DRAW-SLICE-MISSING");
            auto x = core::rotate(
                item.world.rotation, {item.world.scale.x * s->width / s->pixelsPerUnit, 0, 0});
            auto y = core::rotate(
                item.world.rotation, {0, item.world.scale.y * s->height / s->pixelsPerUnit, 0});
            const auto origin =
                item.world.translation - core::scale(x, s->pivot.x) - core::scale(y, s->pivot.y);
            if (visibleWorld) {
                const float minX = origin.x + std::min(0.f, x.x) + std::min(0.f, y.x);
                const float minY = origin.y + std::min(0.f, x.y) + std::min(0.f, y.y);
                const core::Rect2<float> bounds{
                    minX, minY, std::abs(x.x) + std::abs(y.x), std::abs(x.y) + std::abs(y.y)};
                if (!core::rectsIntersect(bounds, *visibleWorld)) {
                    ++culled_;
                    continue;
                }
            }
            const auto& image = atlas.get()->image();
            SpriteInstance instance;
            instance.origin = {origin.x, origin.y, item.world.translation.z, 0};
            instance.axisX = {x.x, x.y, 0, 0};
            instance.axisY = {y.x, y.y, 0, 0};
            instance.uv = {float(s->x) / image.width,
                float(s->y) / image.height,
                float(s->width) / image.width,
                float(s->height) / image.height};
            instance.color = item.color;
            instance.parameters = {float(item.flipX), float(item.flipY), 0, 0};
            if (batches_.empty() || batches_.back().atlas.get() != atlas.get())
                batches_.push_back({atlas, static_cast<std::uint32_t>(instances_.size()), 0});
            ++batches_.back().count;
            instances_.push_back(instance);
        }
        return true;
    }

    const auto& instances() const { return instances_; }

    const auto& batches() const { return batches_; }

    const char* diagnostic() const { return diagnostic_; }

    std::uint32_t culledCount() const { return culled_; }

private:
    bool fail(const char* text)
    {
        instances_.clear();
        batches_.clear();
        diagnostic_ = text;
        return false;
    }

    std::uint32_t capacity_;
    std::uint32_t culled_ = 0;
    const char* diagnostic_ = nullptr;
    core::pmr::Vector<SpriteDrawItem> items_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SpriteInstance> instances_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SpriteBatch> batches_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint64_t> ids_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::scene2d
