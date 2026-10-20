#pragma once
#include "2d/components.h"
#include "2d/sprite_resources.h"
#include "scene/scene_asset_resource.h"

namespace arp::scene2d {
class SceneSpriteCodec final : public scene::SceneComponentCodec {
public:
    const char* type() const override { return "arp.sprite/1"; }

    bool accepts(const scene::Component& component) const override
    {
        return dynamic_cast<const SpriteRendererComponent*>(&component) != nullptr;
    }

    bool attach(scene::GameObject& object,
        const core::YamlContract::Node& node,
        const scene::SceneObjectBindings&,
        resource::ResourceManager& resources,
        core::pmr::String& diagnostic) const override
    {
        using Y = core::YamlContract;
        SpriteRendererComponentDesc desc;
        desc.spriteResourceId = Y::text(node, "spriteId");
        if (!resources.find<SpriteResource>(desc.spriteResourceId).ready()) {
            diagnostic = "ARP-SCENE-SPRITE-RESOURCE";
            return false;
        }
        desc.tint = {float(Y::number(node, "r")),
            float(Y::number(node, "g")),
            float(Y::number(node, "b")),
            float(Y::number(node, "a"))};
        desc.sortingLayer = Y::int32(node, "sortingLayer");
        desc.sortingOrder = Y::int32(node, "sortingOrder");
        desc.ySort = Y::boolean(node, "ySort");
        desc.flipX = Y::boolean(node, "flipX");
        desc.flipY = Y::boolean(node, "flipY");
        if (desc.tint.w < 0 || desc.tint.w > 1) {
            diagnostic = "ARP-SCENE-SPRITE-ALPHA";
            return false;
        }
        object.addComponent<SpriteRendererComponent>(std::move(desc));
        return true;
    }

    bool encode(const scene::Component& component,
        const scene::SceneObjectBindings&,
        core::YamlContract::Node& node,
        core::pmr::String& diagnostic) const override
    {
        const auto& desc = static_cast<const SpriteRendererComponent&>(component).desc();
        if (!resource::isResourceUuid(desc.spriteResourceId)) {
            diagnostic = "ARP-SCENE-SPRITE-LEGACY-NOT-SERIALIZABLE";
            return false;
        }
        node["spriteId"] = desc.spriteResourceId.c_str();
        node["r"] = desc.tint.x;
        node["g"] = desc.tint.y;
        node["b"] = desc.tint.z;
        node["a"] = desc.tint.w;
        node["sortingLayer"] = desc.sortingLayer;
        node["sortingOrder"] = desc.sortingOrder;
        node["ySort"] = desc.ySort;
        node["flipX"] = desc.flipX;
        node["flipY"] = desc.flipY;
        return true;
    }
};
} // namespace arp::scene2d
