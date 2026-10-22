#include "2d/scene_component_persistence.h"

#include "2d/components.h"
#include "2d/sprite_animation_component.h"
#include "2d/tile_map_resource_component.h"
#include "scene/scene_asset_resource.h"

namespace arp::scriptvm {
class ModuleRegistration;
}

namespace arp::reflection::generated {
bool register_scene2d_authoring_reflection(arp::scriptvm::ModuleRegistration*);
}

namespace arp::scene2d {
bool registerScene2DPersistence(scene::SceneComponentFactory& factory,
    resource::ResourceManager& resources,
    core::SharedPtr<physics2d::PhysicsWorld2D> world)
{
    static const bool schema =
        reflection::generated::register_scene2d_authoring_reflection(nullptr);
    if (!schema || !scene::initializeSceneAuthoringReflection() || !world || !world->valid())
        return false;
    auto add = [&](const char* name, scene::SceneComponentFactory::Create create) {
        return factory.contains(name) || factory.add(name, std::move(create));
    };
    return add("SpriteRendererComponent",
               [] {
                   return core::makeUnique<SpriteRendererComponent>(SpriteRendererComponentDesc{});
               })
        && add("RigidBody2DComponent",
            [world] {
                return core::makeUnique<physics2d::RigidBody2DComponent>(world);
            })
        && add("Collider2DComponent",
            [world] {
                return core::makeUnique<physics2d::Collider2DComponent>(world);
            })
        && add("Joint2DComponent",
            [world] {
                return core::makeUnique<physics2d::Joint2DComponent>(world);
            })
        && add("SpriteAnimationComponent",
            [&resources] {
                return core::makeUnique<SpriteAnimationComponent>(
                    resources, kEditorSpriteAnimationEventBudget);
            })
        && add("TileMapResourceComponent",
            [&resources, world] {
                return core::makeUnique<TileMapResourceComponent>(
                    [&resources] {
                        return &resources;
                    },
                    [world] {
                        return world;
                    });
            })
        && add("ObjectLinksComponent",
            [] {
                return core::makeUnique<scene::ObjectLinksComponent>();
            })
        && factory.persist<SpriteRendererComponent>("arp.sprite/1", "SpriteRendererComponent")
        && factory.persist<physics2d::RigidBody2DComponent>(
            "arp.rigid-body2d/1", "RigidBody2DComponent")
        && factory.persist<physics2d::Collider2DComponent>(
            "arp.collider2d/1", "Collider2DComponent", {"arp.rigid-body2d/1"})
        && factory.persist<physics2d::Joint2DComponent>(
            "arp.joint2d/1", "Joint2DComponent", {"arp.rigid-body2d/1"})
        && factory.persist<SpriteAnimationComponent>("arp.sprite-animation/1",
            "SpriteAnimationComponent",
            {},
            [](SpriteAnimationComponent& component) {
                return component.animationResourceId().empty() || component.animationReady();
            })
        && factory.persist<TileMapResourceComponent>("arp.tile-map/1",
            "TileMapResourceComponent",
            {},
            [](TileMapResourceComponent& component) {
                return component.runtime() != nullptr;
            })
        && factory.persist<scene::ObjectLinksComponent>(
            "arp.object-links/1", "ObjectLinksComponent");
}
} // namespace arp::scene2d
