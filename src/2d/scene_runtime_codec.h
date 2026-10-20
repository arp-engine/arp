#pragma once
#include "2d/scene_sprite_codec.h"
#include "2d/sprite_animator.h"
#include "2d/tile_map_runtime.h"
#include "core/callback.h"

#include <stdexcept>

#include <cmath>

namespace arp::scene2d {
inline constexpr std::uint32_t kEditorSpriteAnimationEventBudget = 32u;
inline constexpr std::uint32_t kMaxEditorSpriteAnimationEventBudget = 1024u;

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)
struct SpriteAnimationEditorProperties {
    APROP(Editor, Doc = "resource:SpriteAnimationResource:optional")
    core::pmr::String animationResourceId{&core::defaultCpuMemoryResource()};
    APROP(Editor)
    float speed = 1.0f;
    APROP(Editor)
    bool paused = false;
    APROP(Editor)
    std::uint32_t eventBudget = kEditorSpriteAnimationEventBudget;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class SpriteAnimationComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "SpriteAnimationComponent"; }

    SpriteAnimationComponent(resource::ResourceManager& resources, std::uint32_t events)
        : SpriteAnimationComponent(&resources, events)
    {}

    SpriteAnimationComponent(resource::ResourceManager* resources, std::uint32_t events)
        : animator_(events), speed_(1), paused_(false), eventBudget_(events), resources_(resources)
    {}

    SpriteAnimationComponent(
        core::SharedCallback<resource::ResourceManager*()> provider, std::uint32_t events)
        : animator_(events), speed_(1), paused_(false), eventBudget_(events),
          resourcesProvider_(std::move(provider)), resources_(resourcesProvider_ ? resourcesProvider_() : nullptr)
    {}
    AFUNC()

    bool play_resource(const core::pmr::String& id)
    {
        refreshResourceBinding();
        if (!resources_)
            return false;
        auto resource = resources_->find<SpriteAnimationResource>(id);
        if (!resource.ready())
            return false;
        animation_ = resource;
        animationResourceId_ = id;
        valid_ = animator_.play(animation_);
        resourceBindingPending_ = false;
        return valid_;
    }
    AFUNC()

    bool set_speed(double speed)
    {
        if (!animator_.setSpeed(speed))
            return false;
        speed_ = speed;
        return true;
    }
    AFUNC()

    void set_paused(bool paused)
    {
        paused_ = paused;
        animator_.setPaused(paused);
    }
    AFUNC()

    bool completed() const { return animator_.completed(); }

    void onFixedUpdate(const scene::SceneUpdateContext& context) override
    {
        refreshResourceBinding();
        if (!valid_)
            return;
        if (!advance(context.deltaSeconds) || !synchronizeSprite(*gameObject()->scene()))
            throw std::runtime_error("ARP-2D-ANIMATION-COMPONENT-ADVANCE");
    }

    SpriteAnimationComponent(resource::ResourceHandle<SpriteAnimationResource> animation,
        double speed,
        bool paused,
        std::uint32_t eventBudget)
        : animation_(animation), animator_(eventBudget), speed_(speed), paused_(paused),
          eventBudget_(eventBudget)
    {
        animationResourceId_ = animation.id();
        valid_ = animator_.play(animation) && animator_.setSpeed(speed);
        animator_.setPaused(paused);
    }

    arp::reflection::object editorAuthoringProperties() const override
    {
        SpriteAnimationEditorProperties properties;
        properties.animationResourceId = animationResourceId_;
        properties.speed = static_cast<float>(speed_);
        properties.paused = paused_;
        properties.eventBudget = eventBudget_;
        return arp::reflection::object(std::move(properties));
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        refreshResourceBinding();
        if (!value.is<SpriteAnimationEditorProperties>())
            return false;
        const auto& properties = value.cast<const SpriteAnimationEditorProperties&>();
        const auto& resourceId = properties.animationResourceId;
        if (resourceId.size() > 512u || !std::isfinite(properties.speed) || properties.speed < 0
            || properties.eventBudget == 0
            || properties.eventBudget > kMaxEditorSpriteAnimationEventBudget)
            return false;

        const bool resourceChanged = resourceId != animationResourceId_;
        const bool budgetChanged = properties.eventBudget != eventBudget_;
        resource::ResourceHandle<SpriteAnimationResource> nextAnimation = animation_;
        if (resourceChanged && !resourceId.empty()) {
            if (resources_) {
                nextAnimation = resources_->find<SpriteAnimationResource>(resourceId);
                if (!nextAnimation.ready())
                    return false;
            } else {
                nextAnimation = {};
            }
        } else if (resourceChanged) {
            nextAnimation = {};
        }

        if (resourceChanged || budgetChanged) {
            SpriteAnimator nextAnimator(properties.eventBudget);
            if (nextAnimation.ready() && !nextAnimator.play(nextAnimation))
                return false;
            nextAnimator.setSpeed(properties.speed);
            nextAnimator.setPaused(properties.paused);
            animator_ = std::move(nextAnimator);
            animation_ = nextAnimation;
            valid_ = nextAnimation.ready();
            animationResourceId_ = resourceId;
            eventBudget_ = properties.eventBudget;
            resourceBindingPending_ = false;
        } else {
            if (!animator_.setSpeed(properties.speed))
                return false;
            animator_.setPaused(properties.paused);
        }
        speed_ = properties.speed;
        paused_ = properties.paused;
        return true;
    }

    bool valid() const
    {
        if (!resourcesProvider_)
            return valid_;
        return resourcesProvider_() == resources_ && valid_ && animation_.ready();
    }

    bool animationReady() const
    {
        if (resourcesProvider_ && resourcesProvider_() != resources_)
            return false;
        return animation_.ready();
    }

    const core::pmr::String& animationResourceId() const noexcept
    {
        return animationResourceId_;
    }

    bool advance(double gameSeconds)
    {
        refreshResourceBinding();
        return valid_ && animator_.advance(gameSeconds);
    }

    bool synchronizeSprite(scene::Scene& scene)
    {
        auto* object = scene.findObject(ownerId());
        auto* sprite = object ? object->getComponent<SpriteRendererComponent>() : nullptr;
        const auto* id = animator_.spriteId();
        if (!valid_ || !sprite || !id)
            return false;
        if (sprite->desc().spriteResourceId != *id)
            sprite->setSpriteResource(*id);
        return true;
    }

    const auto& animator() const { return animator_; }

    double speed() const { return speed_; }

    bool paused() const { return paused_; }

    std::uint32_t eventBudget() const { return eventBudget_; }

private:
    void refreshResourceBinding()
    {
        if (!resourcesProvider_)
            return;
        auto* currentResources = resourcesProvider_();
        const bool managerChanged = currentResources != resources_;
        const bool resourceStale = !managerChanged && animation_.stale();
        if (managerChanged || resourceStale) {
            animation_ = {};
            animator_ = SpriteAnimator(eventBudget_);
            resources_ = currentResources;
            valid_ = false;
            resourceBindingPending_ = !animationResourceId_.empty();
        }
        if (!resourceBindingPending_ || !resources_)
            return;
        const auto nextAnimation = resources_->find<SpriteAnimationResource>(animationResourceId_);
        if (!nextAnimation.ready())
            return;
        animation_ = nextAnimation;
        valid_ = animator_.play(animation_) && animator_.setSpeed(speed_);
        animator_.setPaused(paused_);
        resourceBindingPending_ = !valid_;
    }

    resource::ResourceHandle<SpriteAnimationResource> animation_;
    SpriteAnimator animator_;
    double speed_;
    bool paused_, valid_ = false;
    std::uint32_t eventBudget_;
    core::SharedCallback<resource::ResourceManager*()> resourcesProvider_;
    resource::ResourceManager* resources_ = nullptr;
    bool resourceBindingPending_ = false;
    core::pmr::String animationResourceId_{&core::defaultCpuMemoryResource()};
};

class SpriteAnimationSceneCodec final : public scene::SceneComponentCodec {
    using Y = core::YamlContract;
    using Node = Y::Node;

public:
    const char* type() const override { return "arp.sprite-animation/1"; }

    bool accepts(const scene::Component& c) const override
    {
        return dynamic_cast<const SpriteAnimationComponent*>(&c);
    }

    bool attach(scene::GameObject& object,
        const Node& n,
        const scene::SceneObjectBindings&,
        resource::ResourceManager& resources,
        core::pmr::String& d) const override
    {
        const auto animation = resources.find<SpriteAnimationResource>(Y::text(n, "animationId"));
        const auto budget = Y::uint32(n, "eventBudget");
        const auto speed = Y::number(n, "speed");
        if (!animation.ready() || !budget || speed < 0
            || object.getComponent<SpriteAnimationComponent>()) {
            d = "ARP-SCENE-ANIMATION-BINDING";
            return false;
        }
        return object
            .addComponent<SpriteAnimationComponent>(
                animation, speed, Y::boolean(n, "paused"), budget)
            .valid();
    }

    bool encode(const scene::Component& c,
        const scene::SceneObjectBindings&,
        Node& n,
        core::pmr::String& d) const override
    {
        const auto& a = static_cast<const SpriteAnimationComponent&>(c);
        if (!a.animationReady()) {
            d = "ARP-SCENE-ANIMATION-STALE";
            return false;
        }
        n["animationId"] = a.animationResourceId().c_str();
        n["speed"] = a.speed();
        n["paused"] = a.paused();
        n["eventBudget"] = a.eventBudget();
        return true;
    }
};

inline constexpr std::uint32_t kEditorTileMapMaxResidentChunks = 1024u;
inline constexpr std::uint32_t kEditorTileMapMaxVisibleCells = 65536u;

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)
struct TileMapResourceComponentEditorProperties {
    APROP(Editor, Doc = "resource:TileMapResource:required")
    core::pmr::String tileMapResourceId{&core::defaultCpuMemoryResource()};
    APROP(Editor)
    std::uint32_t maxResidentChunks = 0;
    APROP(Editor)
    std::uint32_t maxVisibleCells = 0;
    APROP(Editor)
    std::uint32_t firstObjectId = 0;
};

// Resource-authoritative tile origins are authored in TileMapResource. Runtime
// ownership changes are prepared before replacing the active TileMapRuntime.
ACLASS(Ownership = Engine, Lifecycle = Explicit)
class TileMapResourceComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "TileMapResourceComponent"; }

    TileMapResourceComponent(
        core::SharedCallback<resource::ResourceManager*()> resourcesProvider,
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> worldProvider)
        : resourcesProvider_(std::move(resourcesProvider)), worldProvider_(std::move(worldProvider))
    {
        synchronizeRuntimeOwner();
    }

    TileMapResourceComponent(resource::ResourceManager& resources,
        resource::ResourceHandle<TileMapResource> map,
        core::SharedPtr<physics2d::PhysicsWorld2D> world,
        TileMapRuntimeDesc desc)
        : resources_(&resources), map_(map), world_(std::move(world)), desc_(desc)
    {
        authoring_.tileMapResourceId = map_.id();
        authoring_.maxResidentChunks = desc_.maxResidentChunks;
        authoring_.maxVisibleCells = desc_.maxVisibleCells;
        authoring_.firstObjectId = desc_.firstObjectId;
        transformWasUnit_ = true;
        if (!map_.ready() || !world_ || !world_->valid())
            throw std::invalid_argument("ARP-2D-TILEMAP-BINDING-OR-TRS");
        resourceGeneration_ = map_.generation();
        const auto tileset = resources.find<TileSetResource>(map_.get()->tileSetId());
        tileSetGeneration_ = tileset.generation();
        runtime_ = core::makeUnique<TileMapRuntime>(resources, map_, *world_, desc_);
        editorDiagnostic_ = nullptr;
    }

    TileMapRuntime* runtime() { return runtime_.get(); }
    const TileMapRuntime* runtime() const { return runtime_.get(); }
    const auto& map() const { return map_; }
    const auto& desc() const { return desc_; }
    const char* editorDiagnostic() const { return editorDiagnostic_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(authoring_);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<TileMapResourceComponentEditorProperties>())
            return false;
        const auto& candidateProperties =
            value.cast<const TileMapResourceComponentEditorProperties&>();
        if (candidateProperties.tileMapResourceId.size() > 512u
            || candidateProperties.maxResidentChunks > kEditorTileMapMaxResidentChunks
            || candidateProperties.maxVisibleCells > kEditorTileMapMaxVisibleCells)
            return false;

        synchronizeRuntimeOwner();
        const bool configured = !candidateProperties.tileMapResourceId.empty()
            && candidateProperties.maxResidentChunks != 0u
            && candidateProperties.maxVisibleCells != 0u
            && candidateProperties.firstObjectId != 0u;
        if (!configured) {
            if (runtime_)
                return false;
            authoring_ = candidateProperties;
            editorDiagnostic_ = "EDITOR-TILEMAP-AUTHORING-INCOMPLETE";
            return true;
        }

        resource::ResourceManager* resources = currentResources();
        auto world = currentWorld();
        resource::ResourceHandle<TileMapResource> map;
        core::UniquePtr<TileMapRuntime> candidateRuntime;
        const auto candidateDesc = TileMapRuntimeDesc{candidateProperties.maxResidentChunks,
            candidateProperties.maxVisibleCells,
            candidateProperties.firstObjectId};
        if (!prepareRuntime(candidateProperties, resources, world, map, candidateRuntime))
            return false;

        auto previousWorld = world_;
        auto previousRuntime = std::move(runtime_);
        authoring_ = candidateProperties;
        resources_ = resources;
        resourceGeneration_ = map.generation();
        const auto tileset = resources->find<TileSetResource>(map.get()->tileSetId());
        tileSetGeneration_ = tileset.generation();
        map_ = map;
        world_ = std::move(world);
        desc_ = candidateDesc;
        transformWasUnit_ = true;
        runtime_ = std::move(candidateRuntime);
        editorDiagnostic_ = nullptr;
        previousRuntime.reset();
        return true;
    }

    void onUpdate(const scene::SceneUpdateContext&) override { synchronizeRuntimeOwner(); }

private:
    resource::ResourceManager* currentResources() const
    {
        return resourcesProvider_ ? resourcesProvider_() : resources_;
    }

    core::SharedPtr<physics2d::PhysicsWorld2D> currentWorld() const
    {
        return worldProvider_ ? worldProvider_() : world_;
    }

    bool ownerTransformIsUnit() const
    {
        const auto* owner = gameObject();
        if (!owner)
            return false;
        const auto transform = owner->world_transform();
        return transform.translation.x == 0.0f && transform.translation.y == 0.0f
            && transform.translation.z == 0.0f && transform.rotation.x == 0.0f
            && transform.rotation.y == 0.0f && transform.rotation.z == 0.0f
            && transform.rotation.w == 1.0f && transform.scale.x == 1.0f
            && transform.scale.y == 1.0f && transform.scale.z == 1.0f;
    }

    bool prepareRuntime(const TileMapResourceComponentEditorProperties& properties,
        resource::ResourceManager* resources,
        const core::SharedPtr<physics2d::PhysicsWorld2D>& world,
        resource::ResourceHandle<TileMapResource>& map,
        core::UniquePtr<TileMapRuntime>& runtime)
    {
        if (!resources || !world || !world->valid() || !ownerTransformIsUnit()) {
            editorDiagnostic_ = "EDITOR-TILEMAP-OWNER-OR-TRANSFORM";
            return false;
        }
        map = resources->find<TileMapResource>(properties.tileMapResourceId);
        if (!map.ready()) {
            editorDiagnostic_ = "EDITOR-TILEMAP-RESOURCE-NOT-READY";
            return false;
        }
        const auto tileset = resources->find<TileSetResource>(map.get()->tileSetId());
        if (!tileset.ready()) {
            editorDiagnostic_ = "EDITOR-TILEMAP-TILESET-NOT-READY";
            return false;
        }
        const TileMapRuntimeDesc candidateDesc{properties.maxResidentChunks,
            properties.maxVisibleCells,
            properties.firstObjectId};
        if (!candidateDesc.maxResidentChunks || !candidateDesc.maxVisibleCells
            || !candidateDesc.firstObjectId) {
            editorDiagnostic_ = "EDITOR-TILEMAP-RUNTIME-BUDGET-INVALID";
            return false;
        }
        runtime = core::makeUnique<TileMapRuntime>(*resources, map, *world, candidateDesc);
        return true;
    }

    void synchronizeRuntimeOwner()
    {
        auto* resources = currentResources();
        auto world = currentWorld();
        const auto generation = resources && !authoring_.tileMapResourceId.empty()
            ? resources->generationOf(authoring_.tileMapResourceId)
            : 0u;
        resource::ResourceGeneration tilesetGeneration = 0u;
        if (resources && !authoring_.tileMapResourceId.empty()) {
            const auto currentMap = resources->find<TileMapResource>(authoring_.tileMapResourceId);
            if (currentMap.ready())
                tilesetGeneration = resources->generationOf(currentMap.get()->tileSetId());
        }
        const bool ownerChanged = resources != resources_ || generation != resourceGeneration_
            || tilesetGeneration != tileSetGeneration_ || world.get() != world_.get();
        const bool transformIsUnit = ownerTransformIsUnit();
        const bool transformChanged = transformIsUnit != transformWasUnit_;
        if (!ownerChanged && !transformChanged)
            return;

        auto previousWorld = world_;
        auto previousRuntime = std::move(runtime_);
        resources_ = resources;
        resourceGeneration_ = generation;
        tileSetGeneration_ = tilesetGeneration;
        world_ = std::move(world);
        map_ = {};
        transformWasUnit_ = transformIsUnit;
        editorDiagnostic_ = nullptr;

        if (!transformIsUnit) {
            editorDiagnostic_ = "EDITOR-TILEMAP-OWNER-TRANSFORM";
        } else if (authoring_.tileMapResourceId.empty()
            || !authoring_.maxResidentChunks || !authoring_.maxVisibleCells
            || !authoring_.firstObjectId) {
            editorDiagnostic_ = "EDITOR-TILEMAP-AUTHORING-INCOMPLETE";
        } else {
            core::UniquePtr<TileMapRuntime> candidateRuntime;
            resource::ResourceHandle<TileMapResource> candidateMap;
            if (prepareRuntime(authoring_, resources_, world_, candidateMap, candidateRuntime)) {
                map_ = candidateMap;
                runtime_ = std::move(candidateRuntime);
                tileSetGeneration_ = resources_->generationOf(map_.get()->tileSetId());
                desc_ = {authoring_.maxResidentChunks,
                    authoring_.maxVisibleCells,
                    authoring_.firstObjectId};
            }
        }
        previousRuntime.reset();
    }

    core::SharedCallback<resource::ResourceManager*()> resourcesProvider_;
    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> worldProvider_;
    resource::ResourceManager* resources_ = nullptr;
    resource::ResourceGeneration resourceGeneration_ = 0;
    resource::ResourceGeneration tileSetGeneration_ = 0;
    resource::ResourceHandle<TileMapResource> map_;
    core::SharedPtr<physics2d::PhysicsWorld2D> world_;
    TileMapRuntimeDesc desc_{};
    TileMapResourceComponentEditorProperties authoring_;
    core::UniquePtr<TileMapRuntime> runtime_;
    const char* editorDiagnostic_ = "EDITOR-TILEMAP-AUTHORING-INCOMPLETE";
    bool transformWasUnit_ = false;
};

class TileMapSceneCodec final : public scene::SceneComponentCodec {
    using Y = core::YamlContract;
    using Node = Y::Node;

public:
    explicit TileMapSceneCodec(core::SharedPtr<physics2d::PhysicsWorld2D> world)
        : world_(std::move(world))
    {}

    const char* type() const override { return "arp.tile-map/1"; }

    bool accepts(const scene::Component& c) const override
    {
        return dynamic_cast<const TileMapResourceComponent*>(&c);
    }

    bool attach(scene::GameObject& object,
        const Node& n,
        const scene::SceneObjectBindings&,
        resource::ResourceManager& resources,
        core::pmr::String& d) const override
    {
        const auto map = resources.find<TileMapResource>(Y::text(n, "tileMapId"));
        TileMapRuntimeDesc desc{Y::uint32(n, "maxResidentChunks"),
            Y::uint32(n, "maxVisibleCells"),
            Y::uint64(Y::required(n, "firstObjectId"))};
        const auto t = object.world_transform();
        if (!world_ || !map.ready() || !desc.maxResidentChunks || !desc.maxVisibleCells
            || !desc.firstObjectId || object.getComponent<TileMapResourceComponent>()
            || t.translation.x != 0 || t.translation.y != 0 || t.translation.z != 0
            || t.rotation.x != 0 || t.rotation.y != 0 || t.rotation.z != 0 || t.rotation.w != 1
            || t.scale.x != 1 || t.scale.y != 1 || t.scale.z != 1) {
            d = "ARP-SCENE-TILEMAP-BINDING-OR-TRS";
            return false;
        }
        object.addComponent<TileMapResourceComponent>(resources, map, world_, desc);
        return true;
    }

    bool encode(const scene::Component& c,
        const scene::SceneObjectBindings&,
        Node& n,
        core::pmr::String& d) const override
    {
        const auto& t = static_cast<const TileMapResourceComponent&>(c);
        if (!t.map().ready()) {
            d = "ARP-SCENE-TILEMAP-STALE";
            return false;
        }
        n["tileMapId"] = t.map().id().c_str();
        n["maxResidentChunks"] = t.desc().maxResidentChunks;
        n["maxVisibleCells"] = t.desc().maxVisibleCells;
        n["firstObjectId"] = t.desc().firstObjectId;
        return true;
    }

private:
    core::SharedPtr<physics2d::PhysicsWorld2D> world_;
};
} // namespace arp::scene2d
