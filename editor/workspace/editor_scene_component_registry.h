#pragma once

#include "audio/audio_system.h"
#include "core/concurrency/callback.h"
#include "core/memory/cpu_allocator.h"
#include "editor/workspace/editor_scene_component_catalog.h"
#include "physics/2d/physics_world_2d.h"
#include "resource/resource_manager.h"
#include "scene/component.h"

#include <array>
#include <string_view>

namespace arp::editor {
struct EditorSceneComponentFactoryContext {
    scene::GameObject& owner;
    audio::AudioSystem& audioSystem;
    resource::ResourceManager* resources = nullptr;
    core::SharedCallback<resource::ResourceManager*()> resourceManagerProvider;
    core::SharedPtr<physics2d::PhysicsWorld2D> physicsWorld2D;
    bool schemaOnly = false;
    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> physicsWorld2DProvider;
};

core::UniquePtr<scene::Component> createEditorSceneComponent(
    std::string_view typeName, EditorSceneComponentFactoryContext& context);

const std::array<SceneComponentCatalogEntry, kEditorSceneComponentCount>&
editorSceneComponentCatalog() noexcept;
} // namespace arp::editor
