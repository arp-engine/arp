#include "editor/workspace/editor_scene_component_registry.h"

#include "2d/components.h"
#include "2d/particle_emitter_component.h"
#include "2d/physics_components.h"
#include "2d/screen_canvas_component.h"
#include "2d/sprite_animation_component.h"
#include "2d/sprite_text_component.h"
#include "2d/tile_map_resource_component.h"
#include "3d/animator_component.h"
#include "3d/authoring_components.h"
#include "3d/collider_component.h"
#include "3d/components.h"
#include "audio/audio_components.h"
#include "resource/resource_request_component.h"
#include "scene/camera.h"
#include "scene/runtime_components.h"
#include "scene/scene_asset_resource.h"
#include "ui/ui.h"

#include <array>

namespace arp::editor {
namespace {
using ComponentFactory = core::UniquePtr<scene::Component> (*)(EditorSceneComponentFactoryContext&);

struct EditorSceneComponentDescriptor {
    SceneComponentCatalogEntry catalogEntry;
    ComponentFactory factory = nullptr;
    bool identityRestorable = false;
};

core::UniquePtr<scene::Component> createMesh(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::MeshComponent>();
}

core::UniquePtr<scene::Component> createAnimator(EditorSceneComponentFactoryContext& context)
{
    return core::makeUnique<scene3d::AnimatorComponent>(
        scene3d::AnimatorComponentDesc{}, context.resourceManagerProvider);
}

core::UniquePtr<scene::Component> createTerrain(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::TerrainComponent>();
}

core::UniquePtr<scene::Component> createCamera(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene::CameraComponent>();
}

core::UniquePtr<scene::Component> createLight(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::LightComponent>();
}

core::UniquePtr<scene::Component> createTerrainAuthoring(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::TerrainAuthoringComponent>();
}

core::UniquePtr<scene::Component> createWaterSurface(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::WaterSurfaceComponent>();
}

core::UniquePtr<scene::Component> createRoadNetwork(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::RoadNetworkComponent>();
}

core::UniquePtr<scene::Component> createCritterField(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::CritterFieldComponent>();
}

core::UniquePtr<scene::Component> createSky(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::SkyComponent>();
}

core::UniquePtr<scene::Component> createCampfire(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::CampfireComponent>();
}

core::UniquePtr<scene::Component> createCollider(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene3d::ColliderComponent>();
}

core::UniquePtr<scene::Component> createSpriteRenderer(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene2d::SpriteRendererComponent>(
        scene2d::SpriteRendererComponentDesc{});
}

core::UniquePtr<scene::Component> createSpriteText(EditorSceneComponentFactoryContext& context)
{
    return core::makeUnique<scene2d::SpriteTextComponent>(context.resourceManagerProvider);
}

core::UniquePtr<scene::Component> createTileMap(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene2d::TileMapComponent>(scene2d::TileMapComponentDesc{});
}

core::UniquePtr<scene::Component> createTileMapResource(EditorSceneComponentFactoryContext& context)
{
    if (!context.physicsWorld2D || !context.physicsWorld2D->valid()
        || !context.resourceManagerProvider || !context.physicsWorld2DProvider)
        return nullptr;
    return core::makeUnique<scene2d::TileMapResourceComponent>(context.resourceManagerProvider,
        context.physicsWorld2DProvider,
        physics2d::ComponentUsage::Authoring);
}

core::UniquePtr<scene::Component> createResourceRequest(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<resource::ResourceRequestComponent>();
}

core::UniquePtr<scene::Component> createAudioSource(EditorSceneComponentFactoryContext& context)
{
    return core::makeUnique<audio::AudioSourceComponent>(
        audio::AudioSourceComponentDesc{&context.audioSystem});
}

core::UniquePtr<scene::Component> createAudioListener(EditorSceneComponentFactoryContext& context)
{
    return core::makeUnique<audio::AudioListenerComponent>(context.audioSystem);
}

core::UniquePtr<scene::Component> createDocument(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<ui::DocumentComponent>();
}

core::UniquePtr<scene::Component> createTimer(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene::TimerComponent>(0u);
}

core::UniquePtr<scene::Component> createTween(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene::TweenComponent>();
}

core::UniquePtr<scene::Component> createScreenCanvas(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene2d::ScreenCanvas2DComponent>();
}

core::UniquePtr<scene::Component> createSpriteAnimation(EditorSceneComponentFactoryContext& context)
{
    if (!context.owner.getComponent<scene2d::SpriteRendererComponent>())
        return nullptr;
    if (context.resourceManagerProvider)
        return core::makeUnique<scene2d::SpriteAnimationComponent>(
            context.resourceManagerProvider, scene2d::kEditorSpriteAnimationEventBudget);
    return core::makeUnique<scene2d::SpriteAnimationComponent>(
        context.resources, scene2d::kEditorSpriteAnimationEventBudget);
}

core::UniquePtr<scene::Component> createParticleEmitter(EditorSceneComponentFactoryContext& context)
{
    const auto capacity = renderer::ParticleEmitterDesc{}.maxParticles;
    if (context.resourceManagerProvider)
        return core::makeUnique<scene2d::ParticleEmitter2DComponent>(
            context.resourceManagerProvider, capacity);
    return core::makeUnique<scene2d::ParticleEmitter2DComponent>(context.resources, capacity);
}

core::UniquePtr<scene::Component> createRigidBody2D(EditorSceneComponentFactoryContext& context)
{
    if (context.schemaOnly)
        return core::makeUnique<physics2d::RigidBody2DComponent>(
            context.physicsWorld2D, physics2d::ComponentUsage::Authoring);
    if (!context.physicsWorld2D || !context.physicsWorld2D->valid())
        return nullptr;
    auto component = core::makeUnique<physics2d::RigidBody2DComponent>(
        context.physicsWorld2D, physics2d::ComponentUsage::Authoring);
    if (!context.schemaOnly
        && !component->initializeForOwner(context.owner.world_transform(), context.owner.id()))
        return nullptr;
    return component;
}

core::UniquePtr<scene::Component> createCollider2D(EditorSceneComponentFactoryContext& context)
{
    physics2d::ShapeDesc desc;
    desc.radius = 0.5f;
    auto* body = context.owner.getComponent<physics2d::RigidBody2DComponent>();
    if (!context.schemaOnly
        && (!body || body->usage() != physics2d::ComponentUsage::Authoring
            || body->world() != context.physicsWorld2D.get()))
        return nullptr;
    return core::makeUnique<physics2d::Collider2DComponent>(context.physicsWorld2D,
        physics2d::BodyHandle{},
        std::move(desc),
        physics2d::ComponentUsage::Authoring);
}

core::UniquePtr<scene::Component> createJoint2D(EditorSceneComponentFactoryContext& context)
{
    if (context.schemaOnly)
        return core::makeUnique<physics2d::Joint2DComponent>(
            context.physicsWorld2D, physics2d::ComponentUsage::Authoring);
    if (!context.physicsWorld2D || !context.physicsWorld2D->valid())
        return nullptr;
    return core::makeUnique<physics2d::Joint2DComponent>(
        context.physicsWorld2D, physics2d::ComponentUsage::Authoring);
}

core::UniquePtr<scene::Component> createObjectLinks(EditorSceneComponentFactoryContext&)
{
    return core::makeUnique<scene::ObjectLinksComponent>();
}

constexpr std::array<EditorSceneComponentDescriptor, kEditorSceneComponentCount> kDescriptors{{
    {{"AnimatorComponent", "Animation"}, &createAnimator, true},
    {{"MeshComponent", "Mesh"}, &createMesh, true},
    {{"TerrainComponent", "Terrain"}, &createTerrain, true},
    {{"CameraComponent", "Camera"}, &createCamera, true},
    {{"LightComponent", "Light"}, &createLight, true},
    {{"TerrainAuthoringComponent", "Terrain Authoring"}, &createTerrainAuthoring, true},
    {{"WaterSurfaceComponent", "Water Surface"}, &createWaterSurface, true},
    {{"RoadNetworkComponent", "Road Network"}, &createRoadNetwork, true},
    {{"CritterFieldComponent", "Critter Field"}, &createCritterField, true},
    {{"SkyComponent", "Sky"}, &createSky, true},
    {{"CampfireComponent", "Campfire"}, &createCampfire, true},
    {{"ColliderComponent", "Collider"}, &createCollider, true},
    {{"SpriteRendererComponent", "Sprite 2D"}, &createSpriteRenderer, true},
    {{"SpriteTextComponent", "Sprite Text 2D"}, &createSpriteText, true},
    {{"TileMapComponent", "TileMap"}, &createTileMap, true},
    {{"TileMapResourceComponent", "TileMap Resource"}, &createTileMapResource, true},
    {{"ResourceRequestComponent", "Resource Requests"}, &createResourceRequest, true},
    {{"AudioSourceComponent", "Audio Source"}, &createAudioSource, true},
    {{"AudioListenerComponent", "Audio Listener"}, &createAudioListener, true},
    {{"DocumentComponent", "UI Document"}, &createDocument, true},
    {{"TimerComponent", "Timer"}, &createTimer, true},
    {{"TweenComponent", "Tween"}, &createTween, true},
    {{"ScreenCanvas2DComponent", "Canvas 2D"}, &createScreenCanvas, true},
    {{"SpriteAnimationComponent", "Sprite Animation 2D"}, &createSpriteAnimation, true},
    {{"ParticleEmitter2DComponent", "Particle Emitter 2D"}, &createParticleEmitter, true},
    {{"RigidBody2DComponent", "Rigid Body 2D"}, &createRigidBody2D, true},
    {{"Collider2DComponent", "Collider 2D"}, &createCollider2D, true},
    {{"Joint2DComponent", "Joint 2D"}, &createJoint2D, true},
    {{"ObjectLinksComponent", "Object Links"}, &createObjectLinks, true},
}};
} // namespace

const std::array<SceneComponentCatalogEntry, kEditorSceneComponentCount>&
editorSceneComponentCatalog() noexcept
{
    static const auto catalog = [] {
        std::array<SceneComponentCatalogEntry, kEditorSceneComponentCount> result{};
        for (std::size_t index = 0; index < kDescriptors.size(); ++index)
            result[index] = kDescriptors[index].catalogEntry;
        return result;
    }();
    return catalog;
}

bool editorSceneComponentIdentityRestorable(std::string_view typeName) noexcept
{
    for (const auto& descriptor : kDescriptors)
        if (typeName == descriptor.catalogEntry.typeName)
            return descriptor.identityRestorable;
    return false;
}

core::UniquePtr<scene::Component> createEditorSceneComponent(
    std::string_view typeName, EditorSceneComponentFactoryContext& context)
{
    for (const auto& descriptor : kDescriptors)
        if (typeName == descriptor.catalogEntry.typeName)
            return descriptor.factory(context);
    return nullptr;
}
} // namespace arp::editor
