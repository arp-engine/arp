#pragma once

#include "audio/audio_system.h"
#include "core/concurrency/callback.h"
#include "core/memory/cpu_allocator.h"
#include "editor/workspace/editor_scene_component_catalog.h"
#include "editor/workspace/editor_scene_inspector.h"
#include "physics/2d/physics_world_2d.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <variant>

namespace arp::editor {
struct SceneHierarchyEntry {
    scene::GameObjectId objectId;
    std::size_t depth = 0;
};

enum class SceneInspectorWriteResult : std::uint8_t { Rejected, Unchanged, Changed };

class EditorSceneDocument final {
public:
    explicit EditorSceneDocument(bool createDefaultScene = true);
    bool replaceWith(EditorSceneDocument&& candidate);
    void setResourceManagerProvider(core::SharedCallback<resource::ResourceManager*()> provider);
    void setPhysicsWorld2DProvider(
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> provider);

    void setSchemaOnlyComponentCreation(bool enabled) noexcept
    {
        schemaOnlyComponentCreation_ = enabled;
    }

    core::SharedCallback<resource::ResourceManager*()> resourceManagerProvider() const
    {
        return resourceManagerProvider_;
    }

    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()>
    physicsWorld2DProvider() const
    {
        return physicsWorld2DProvider_;
    }

    static const std::array<SceneComponentCatalogEntry, kEditorSceneComponentCount>&
    componentCatalog() noexcept;
    core::pmr::Vector<SceneHierarchyEntry> hierarchyEntries() const;

    scene::Scene& scene() noexcept { return *scene_; }

    const scene::Scene& scene() const noexcept { return *scene_; }

    scene::GameObjectId selectedObject() const noexcept { return selectedObject_; }

    bool select(scene::GameObjectId id) noexcept;
    scene::GameObjectId createObject(std::string_view name);
    bool reparentObject(
        scene::GameObjectId objectId, scene::GameObjectId parentId, std::size_t childIndex);
    bool destroySelected();
    bool renameSelected(std::string_view name);
    bool setSelectedTranslation(float x, float y, float z) noexcept;
    bool setSelectedTransform(const scene::Transform& transform) noexcept;
    scene::Component* addComponent(std::string_view type, scene::GameObjectId objectId = {});
    bool removeComponent(scene::GameObjectId objectId, scene::ComponentId componentId);
    bool removeLastSelectedComponent();
    core::pmr::Vector<SceneInspectorProperty> inspectorProperties(
        scene::GameObjectId objectId) const;
    SceneInspectorWriteResult setComponentProperty(scene::GameObjectId objectId,
        scene::ComponentId componentId,
        std::string_view propertyName,
        SceneInspectorPropertyKind valueKind,
        bool booleanValue,
        int integerValue,
        std::uint64_t unsignedIntegerValue,
        double numberValue,
        core::Vec3 vector3Value = {},
        core::Vec4 color4Value = {},
        std::string_view stringValue = {},
        core::Vec2 vector2Value = {},
        const core::pmr::Vector<core::Vec2>& vector2ListValue = {},
        std::uint64_t sceneObjectReferenceValue = 0,
        const core::pmr::Vector<SceneInspectorObjectLink>& sceneObjectLinkListValue = {});

private:
    // Keep this service alive until the scene and its components are destroyed.
    core::UniquePtr<audio::AudioSystem> audioSystem_;
    core::SharedCallback<resource::ResourceManager*()> resourceManagerProvider_;
    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> physicsWorld2DProvider_;
    bool schemaOnlyComponentCreation_ = false;
    core::UniquePtr<scene::Scene> scene_;
    scene::GameObjectId selectedObject_;
};
} // namespace arp::editor
