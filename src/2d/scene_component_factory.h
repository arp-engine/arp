#pragma once
#include "2d/particle_emitter_component.h"
#include "2d/scene_runtime_codec.h"
#include "2d/screen_canvas_component.h"
#include "2d/sprite_text_component.h"
#include "physics2d/components.h"
#include "resource/audio_source_component.h"
#include "scene/component_factory.h"
#include "scene/runtime_components.h"

namespace arp::scene2d {
struct Scene2DComponentBudget {
    std::uint32_t animationEvents = 0, timerEvents = 0, glyphs = 0, particles = 0;
};

inline bool registerScene2DComponents(scene::SceneComponentFactory& factory,
    resource::ResourceManager& resources,
    core::SharedPtr<physics2d::PhysicsWorld2D> world,
    audio::AudioSystem* audio,
    core::SharedPtr<const core::pmr::Vector<renderer::ui::SpriteGlyph>> font,
    Scene2DComponentBudget budget)
{
    if (!world || !world->valid() || !font || !budget.animationEvents || !budget.timerEvents
        || !budget.glyphs || !budget.particles)
        return false;
    return factory.add("SpriteRendererComponent", [] {
        return core::makeUnique<SpriteRendererComponent>(SpriteRendererComponentDesc{});
    }) && factory.add("RigidBody2DComponent", [world] {
        return core::makeUnique<physics2d::RigidBody2DComponent>(world);
    }) && factory.add("Joint2DComponent", [world] {
        return core::makeUnique<physics2d::Joint2DComponent>(world);
    }) && factory.add("SpriteAnimationComponent", [&resources, budget] {
        return core::makeUnique<SpriteAnimationComponent>(resources, budget.animationEvents);
    }) && factory.add("SpriteTextComponent", [font, budget] {
        return core::makeUnique<SpriteTextComponent>(font, budget.glyphs);
    }) && factory.add("ParticleEmitter2DComponent", [&resources, budget] {
        return core::makeUnique<ParticleEmitter2DComponent>(resources, budget.particles);
    }) && factory.add("AudioResourceSourceComponent", [&resources, audio] {
        return core::makeUnique<resource::AudioResourceSourceComponent>(resources, audio);
    }) && factory.add("ScreenCanvas2DComponent", [] {
        return core::makeUnique<ScreenCanvas2DComponent>();
    }) && factory.add("TimerComponent", [budget] {
        return core::makeUnique<scene::TimerComponent>(budget.timerEvents);
    }) && factory.add("TweenComponent", [] {
        return core::makeUnique<scene::TweenComponent>();
    });
}
} // namespace arp::scene2d
