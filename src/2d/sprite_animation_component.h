#pragma once
#include "2d/components.h"
#include "2d/sprite_animator.h"
#include "core/concurrency/callback.h"
#include "scene/scene.h"

namespace arp::scene2d {
inline constexpr std::uint32_t kEditorSpriteAnimationEventBudget = 32u;
inline constexpr std::uint32_t kMaxEditorSpriteAnimationEventBudget = 1024u;

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SpriteAnimationEditorProperties {
    APROP(Serialize,
        Editor,
        ResourceReference,
        OptionalReference,
        ReferenceTarget = "SpriteAnimationResource",
        Doc = "resource:SpriteAnimationResource:optional")
    core::pmr::String animationResourceId{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor)
    double speed = 1.0;
    APROP(Serialize, Editor)
    bool paused = false;
    APROP(Serialize, Editor)
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
          resourcesProvider_(std::move(provider)),
          resources_(resourcesProvider_ ? resourcesProvider_() : nullptr)
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
        properties.speed = speed_;
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

    const core::pmr::String& animationResourceId() const noexcept { return animationResourceId_; }

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

} // namespace arp::scene2d
