#pragma once
#include "2d/components.h"
#include "2d/sprite_resources.h"
#include "core/concurrency/callback.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/particles/particle_system.h"

#include <cmath>

namespace arp::scene2d {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ParticleEmitter2DEditorProperties {
    APROP(Serialize,
        Editor,
        ResourceReference,
        OptionalReference,
        ReferenceTarget = "SpriteResource",
        Doc = "resource:SpriteResource:optional")
    core::pmr::String spriteId{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor)
    float lifetimeSeconds = 1.0f;
    APROP(Serialize, Editor)
    float size = 1.0f;
    APROP(Serialize, Editor)
    core::Vec2 velocitySpread{};
    APROP(Serialize, Editor, Doc = "color:rgba")
    core::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    APROP(Serialize, Editor)
    std::uint32_t seed = 1u;
    APROP(Serialize, Editor)
    std::int32_t layer = 0;
    APROP(Serialize, Editor)
    std::int32_t order = 0;
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ParticleEmitter2DComponent final : public scene::Component {
public:
    ParticleEmitter2DComponent(resource::ResourceManager& resources, std::uint32_t capacity)
        : ParticleEmitter2DComponent(&resources, capacity)
    {}

    ParticleEmitter2DComponent(resource::ResourceManager* resources, std::uint32_t capacity)
        : capacity_(capacity), system_({capacity, 1}), resources_(resources)
    {}

    ParticleEmitter2DComponent(
        core::SharedCallback<resource::ResourceManager*()> provider, std::uint32_t capacity)
        : capacity_(capacity), system_({capacity, 1}), resourcesProvider_(std::move(provider)),
          resources_(resourcesProvider_ ? resourcesProvider_() : nullptr)
    {}

    const char* scriptTypeName() const override { return "ParticleEmitter2DComponent"; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(properties_);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        refreshResourceBinding();
        if (!value.is<ParticleEmitter2DEditorProperties>())
            return false;
        const auto& properties = value.cast<const ParticleEmitter2DEditorProperties&>();
        if (properties.spriteId.size() > 512u || !std::isfinite(properties.lifetimeSeconds)
            || properties.lifetimeSeconds <= 0.0f || !std::isfinite(properties.size)
            || properties.size <= 0.0f || !std::isfinite(properties.velocitySpread.x)
            || !std::isfinite(properties.velocitySpread.y) || properties.velocitySpread.x < 0.0f
            || properties.velocitySpread.y < 0.0f || !std::isfinite(properties.color.x)
            || !std::isfinite(properties.color.y) || !std::isfinite(properties.color.z)
            || !std::isfinite(properties.color.w) || properties.color.w < 0.0f
            || properties.color.w > 1.0f)
            return false;

        if (properties.spriteId.empty()) {
            resetRuntime();
            properties_ = properties;
            return true;
        }
        if (!resources_) {
            properties_ = properties;
            resourceBindingPending_ = true;
            return true;
        }
        auto nextSprite = resources_->find<SpriteResource>(properties.spriteId);
        if (!nextSprite.ready())
            return false;
        return configureRuntime(properties, nextSprite);
    }

    AFUNC()

    bool configure(const core::pmr::String& sprite,
        double lifetime,
        float size,
        core::Vec2 velocity,
        core::Vec4 color,
        std::uint32_t seed)
    {
        auto properties = properties_;
        properties.spriteId = sprite;
        properties.lifetimeSeconds = static_cast<float>(lifetime);
        properties.size = size;
        properties.velocitySpread = velocity;
        properties.color = color;
        properties.seed = seed;
        return applyEditorAuthoringProperties(arp::reflection::object(std::move(properties)));
    }
    AFUNC()

    void set_sorting(std::int32_t layer, std::int32_t order)
    {
        layer_ = layer;
        order_ = order;
        properties_.layer = layer;
        properties_.order = order;
    }

    std::int32_t layer() const { return layer_; }

    std::int32_t order() const { return order_; }
    AFUNC()

    bool burst(core::Vec2 position, std::uint32_t count)
    {
        refreshResourceBinding();
        if (!valid_ || !emitter_.valid())
            return false;
        system_.setEmitterOrigin(emitter_, {position.x, position.y, 0});
        system_.triggerBurst(emitter_, count);
        return true;
    }

    void onFixedUpdate(const scene::SceneUpdateContext& context) override
    {
        refreshResourceBinding();
        if (valid_)
            system_.update({context.deltaSeconds, false, {}});
    }

    const auto& particles() const { return system_.particles(); }

    const auto& sprite() const
    {
        static const resource::ResourceHandle<SpriteResource> empty;
        if (resourcesProvider_ && resourcesProvider_() != resources_)
            return empty;
        return sprite_;
    }

    bool spriteReady() const
    {
        if (resourcesProvider_ && resourcesProvider_() != resources_)
            return false;
        return valid_ && sprite_.ready();
    }

    void extract(
        const scene::RenderExtractContext& context, scene::RenderExtractSink& sink) const override
    {
        if (!enabled() || !spriteReady())
            return;
        scene::RenderExtractPayload payload;
        payload.typeId = "scene2d.particle-sprites/1";
        payload.data = this;
        sink.accept(context, payload);
    }

private:
    bool configureRuntime(const ParticleEmitter2DEditorProperties& properties,
        resource::ResourceHandle<SpriteResource> sprite)
    {
        renderer::ParticleSystem nextSystem({capacity_, 1});
        renderer::ParticleEmitterDesc desc;
        desc.name = "2d component emitter";
        desc.maxParticles = capacity_;
        desc.particleLifetimeSeconds = properties.lifetimeSeconds;
        desc.startSize = properties.size;
        desc.endSize = 0.0f;
        desc.velocityMin = {-properties.velocitySpread.x, -properties.velocitySpread.y, 0.0f};
        desc.velocityMax = {properties.velocitySpread.x, properties.velocitySpread.y, 0.0f};
        desc.startColor = {
            properties.color.x, properties.color.y, properties.color.z, properties.color.w};
        desc.endColor = {properties.color.x, properties.color.y, properties.color.z, 0.0f};
        desc.seed = properties.seed;
        desc.worldSpaceEmission = true;
        const auto result = nextSystem.addEmitter(desc);
        if (!result.success)
            return false;
        system_ = std::move(nextSystem);
        emitter_ = result.handle;
        sprite_ = std::move(sprite);
        properties_ = properties;
        layer_ = properties.layer;
        order_ = properties.order;
        valid_ = true;
        resourceBindingPending_ = false;
        return true;
    }

    void resetRuntime()
    {
        system_ = renderer::ParticleSystem({capacity_, 1});
        emitter_ = {};
        sprite_ = {};
        valid_ = false;
        resourceBindingPending_ = false;
    }

    void refreshResourceBinding()
    {
        if (!resourcesProvider_)
            return;
        auto* currentResources = resourcesProvider_();
        if (currentResources != resources_) {
            resetRuntime();
            resources_ = currentResources;
            resourceBindingPending_ = !properties_.spriteId.empty();
        } else if (valid_ && sprite_.stale()) {
            resetRuntime();
            resourceBindingPending_ = !properties_.spriteId.empty();
        }
        if (!resourceBindingPending_ || !resources_)
            return;
        const auto nextSprite = resources_->find<SpriteResource>(properties_.spriteId);
        if (nextSprite.ready())
            (void)configureRuntime(properties_, nextSprite);
    }

    std::uint32_t capacity_;
    renderer::ParticleSystem system_;
    core::SharedCallback<resource::ResourceManager*()> resourcesProvider_;
    resource::ResourceManager* resources_ = nullptr;
    resource::ResourceHandle<SpriteResource> sprite_;
    renderer::ParticleEmitterHandle emitter_;
    ParticleEmitter2DEditorProperties properties_;
    bool valid_ = false;
    bool resourceBindingPending_ = false;
    std::int32_t layer_ = 0, order_ = 0;
};
} // namespace arp::scene2d
