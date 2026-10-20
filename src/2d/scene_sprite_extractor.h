#pragma once
#include "2d/components.h"
#include "2d/particle_emitter_component.h"
#include "2d/sprite_draw_plan.h"
#include "2d/sprite_text_component.h"
#include "scene/scene.h"
#include "scene/scene_traversal_snapshot.h"

namespace arp::scene2d {
class SceneSpriteExtractor final : private scene::RenderExtractSink {
public:
    SceneSpriteExtractor(resource::ResourceManager& manager,
        std::uint32_t capacity,
        bool ignoreScene3DPayloads = false)
        : manager_(manager), capacity_(capacity), ignoreScene3DPayloads_(ignoreScene3DPayloads)
    {
        items_.reserve(capacity);
    }

    bool extract(const scene::Scene& scene, const scene::SceneTraversalSnapshot& snapshot)
    {
        items_.clear();
        diagnostic_ = nullptr;
        if (!snapshot.ok())
            return fail("ARP-2D-SNAPSHOT-INVALID");
        for (const auto& node : snapshot.nodes()) {
            if (!node.activeInHierarchy)
                continue;
            auto* object = scene.findObject(node.objectId);
            if (!object)
                return fail("ARP-2D-SNAPSHOT-OBJECT-MISSING");
            scene::RenderExtractContext context{object, &node.worldTransform, true};
            std::uint64_t ordinal = 0;
            for (const auto& component : object->components()) {
                if (++ordinal > UINT16_MAX || object->id().value() > UINT32_MAX)
                    return fail("ARP-2D-EXTRACT-ID-RANGE");
                extractionId_ = (object->id().value() << 32) | (ordinal << 16);
                component->extract(context, *this);
                if (diagnostic_)
                    return false;
            }
        }
        return true;
    }

    const auto& items() const { return items_; }

    const char* diagnostic() const { return diagnostic_; }

private:
    bool fail(const char* id)
    {
        diagnostic_ = id;
        items_.clear();
        return false;
    }

    void accept(const scene::RenderExtractContext& context,
        const scene::RenderExtractPayload& payload) override
    {
        if (diagnostic_ || payload.typeId == "scene2d.disabled")
            return;
        if (ignoreScene3DPayloads_
            && (payload.typeId == "scene3d.mesh" || payload.typeId == "scene3d.terrain"
                || payload.typeId == "scene3d.animation" || payload.typeId == "scene3d.light"
                || payload.typeId == "scene3d.disabled"))
            return;
        if (payload.typeId == "scene2d.sprite-text/1") {
            if (!payload.data || !context.owner || !context.worldTransform
                || context.owner->id().value() > UINT32_MAX) {
                fail("ARP-2D-TEXT-EXTRACT-CONTRACT");
                return;
            }
            const auto& text = *static_cast<const SpriteTextComponent*>(payload.data);
            auto id = extractionId_;
            const auto first = items_.size();
            if (text.layout().glyphCount() > UINT16_MAX) {
                fail("ARP-2D-TEXT-ID-RANGE");
                return;
            }
            if (!text.layout().append(items_, capacity_, {}, 1, text.color(), text.layer(), id)) {
                fail("ARP-2D-TEXT-EXTRACT-CAPACITY");
                return;
            }
            for (auto i = first; i < items_.size(); ++i)
                items_[i].world =
                    scene::composeTransforms(*context.worldTransform, items_[i].world);
            return;
        }
        if (payload.typeId == "scene2d.particle-sprites/1") {
            if (!payload.data || !context.owner || context.owner->id().value() > UINT32_MAX) {
                fail("ARP-2D-PARTICLE-EXTRACT-CONTRACT");
                return;
            }
            const auto& emitter = *static_cast<const ParticleEmitter2DComponent*>(payload.data);
            if (emitter.particles().size() > UINT16_MAX
                || emitter.particles().size() > capacity_ - items_.size()) {
                fail("ARP-2D-PARTICLE-EXTRACT-CAPACITY");
                return;
            }
            auto id = extractionId_;
            for (const auto& particle : emitter.particles()) {
                SpriteDrawItem item;
                item.sprite = emitter.sprite();
                item.world.translation = particle.position;
                item.world.scale = {particle.size, particle.size, 1};
                item.world.rotation = {
                    0, 0, std::sin(particle.rotation * .5f), std::cos(particle.rotation * .5f)};
                item.color = {
                    particle.color.r, particle.color.g, particle.color.b, particle.color.a};
                item.sorting = {emitter.layer(), emitter.order(), false, 0, id++};
                items_.push_back(std::move(item));
            }
            return;
        }
        if (payload.typeId != "scene2d.resource-sprite/1" || !payload.data || !context.owner
            || !context.worldTransform) {
            fail("ARP-2D-EXTRACT-CONTRACT");
            return;
        }
        if (items_.size() >= capacity_) {
            fail("ARP-2D-EXTRACT-CAPACITY");
            return;
        }
        const auto& desc = *static_cast<const SpriteRendererComponentDesc*>(payload.data);
        SpriteDrawItem item;
        item.sprite = manager_.find<SpriteResource>(desc.spriteResourceId);
        if (!item.sprite.ready()) {
            fail("ARP-2D-EXTRACT-RESOURCE");
            return;
        }
        item.world = *context.worldTransform;
        item.color = desc.tint;
        item.flipX = desc.flipX;
        item.flipY = desc.flipY;
        item.sorting = {desc.sortingLayer,
            desc.sortingOrder,
            desc.ySort,
            item.world.translation.y,
            extractionId_};
        items_.push_back(std::move(item));
    }

    resource::ResourceManager& manager_;
    std::uint32_t capacity_;
    bool ignoreScene3DPayloads_ = false;
    const char* diagnostic_ = nullptr;
    std::uint64_t extractionId_ = 0;
    core::pmr::Vector<SpriteDrawItem> items_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::scene2d
