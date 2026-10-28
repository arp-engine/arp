#pragma once

#include "audio/audio_system.h"
#include "core/concurrency/callback.h"
#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"
#include "editor/workspace/editor_scene_component_catalog.h"
#include "editor/workspace/editor_scene_inspector.h"
#include "physics/2d/physics_world_2d.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <variant>

namespace arp::scene {
class SceneDocumentResource;
class SceneComponentFactory;
} // namespace arp::scene

namespace arp::editor {
class EditorSceneServiceSession;
class EditorSceneScriptBindings;
class EditorScriptComponentCatalog;
struct EditorScriptBindingBudget;
struct ProjectManifest;

struct SceneHierarchyEntry {
    scene::GameObjectId objectId;
    std::size_t depth = 0;
};

enum class SceneInspectorWriteResult : std::uint8_t { Rejected, Unchanged, Changed };

class EditorSceneDocument final {
    struct ServiceEpochs {
        core::LifetimeEpoch::Ticket resources, physics;
    };

    struct ServiceProviders {
        core::SharedCallback<resource::ResourceManager*()> resources;
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> physics;
        core::SharedPtr<EditorSceneServiceSession> resourcesSession, physicsSession;
        ServiceEpochs epochs() const noexcept;
        bool matches(const ServiceEpochs&) const noexcept;
    };

public:
    static constexpr std::size_t maxObjectNameBytes = 128u;
    static constexpr std::size_t maxPropertyNameBytes = 128u;
    enum class ReplacementResult : std::uint8_t { Ready, Committed, Incomplete, Stale, Consumed };
    enum class ServicePolicy : std::uint8_t { KeepDestination, AdoptCandidate };

    // Cold, one-shot factory preparation. Documents remain caller-owned;
    // callback/session copies may retain CPU services, never GPU retirement.
    class PreparedReplacement final {
    public:
        PreparedReplacement() = default;
        PreparedReplacement(const PreparedReplacement&) = delete;
        PreparedReplacement& operator=(const PreparedReplacement&) = delete;
        PreparedReplacement(PreparedReplacement&&) = delete;
        PreparedReplacement& operator=(PreparedReplacement&&) = delete;

        bool committed() const noexcept { return committed_; }

    private:
        friend class EditorSceneDocument;
        const EditorSceneDocument* destination_ = nullptr;
        const EditorSceneDocument* candidate_ = nullptr;
        core::LifetimeEpoch::Ticket destinationEpoch_, candidateEpoch_;
        const scene::Scene* destinationScene_ = nullptr;
        const scene::Scene* candidateScene_ = nullptr;
        scene::GameObjectId destinationSelection_, candidateSelection_;
        core::pmr::String destinationIdentity_{&core::defaultCpuMemoryResource()};
        core::pmr::String candidateIdentity_{&core::defaultCpuMemoryResource()};
        scene::Scene::ComponentFactory destinationFactory_, retiredFactory_;
        ServiceProviders retiredServices_, incomingServices_;
        ServiceEpochs destinationServiceEpochs_, candidateServiceEpochs_;
        ServicePolicy servicePolicy_ = ServicePolicy::KeepDestination;
        bool prepared_ = false;
        bool committed_ = false;
    };

    explicit EditorSceneDocument(bool createDefaultScene = true);
    ~EditorSceneDocument();
    EditorSceneDocument(EditorSceneDocument&&);
    EditorSceneDocument& operator=(EditorSceneDocument&&);
    bool prepareScriptAuthoring(const std::filesystem::path&,
        const ProjectManifest&,
        EditorScriptBindingBudget,
        core::pmr::String& diagnostic);
    core::SharedPtr<const EditorScriptComponentCatalog> scriptCatalog() const noexcept;
    bool replaceWith(EditorSceneDocument&& candidate);
    core::UniquePtr<PreparedReplacement> prepareReplacement(EditorSceneDocument& candidate,
        core::pmr::String& diagnostic,
        ServicePolicy = ServicePolicy::KeepDestination) const;
    // Pass live references explicitly; never dereference an address saved in a
    // token whose epoch may have travelled with a moved document.
    ReplacementResult replacementReadiness(
        const PreparedReplacement&, const EditorSceneDocument& candidate) const noexcept;
    ReplacementResult commitReplacement(
        PreparedReplacement&, EditorSceneDocument& candidate) noexcept;
    bool loadRuntimeScene(const scene::SceneDocumentResource&,
        resource::ResourceManager&,
        const scene::SceneComponentFactory&,
        core::pmr::String& diagnostic);

    const core::pmr::String& runtimeSceneId() const noexcept { return runtimeSceneId_; }

    // Empty explicitly means unbound; identity is never inferred by a codec.
    bool setRuntimeSceneId(std::string_view identity);

    // Tracks replacement and provider binding, independent of reusable Scene IDs.
    core::LifetimeEpoch::Ticket ownerTicket() const noexcept
    {
        return ownerEpoch_ ? ownerEpoch_->ticket() : core::LifetimeEpoch::Ticket{};
    }

    void setResourceManagerProvider(core::SharedCallback<resource::ResourceManager*()> provider);
    void setPhysicsWorld2DProvider(
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> provider);
    // Prepare both callbacks before changing a cold document's binding.
    bool bindServiceSession(
        core::SharedPtr<EditorSceneServiceSession>, core::pmr::String& diagnostic);
    void copySceneServicesFrom(const EditorSceneDocument&) noexcept;

    core::SharedPtr<EditorSceneServiceSession> sceneServiceSession() const noexcept
    {
        return services_.resourcesSession == services_.physicsSession ? services_.resourcesSession
                                                                      : nullptr;
    }

    void setSchemaOnlyComponentCreation(bool enabled) noexcept
    {
        if (schemaOnlyComponentCreation_ != enabled && ownerEpoch_)
            ownerEpoch_->advance();
        schemaOnlyComponentCreation_ = enabled;
    }

    core::SharedCallback<resource::ResourceManager*()> resourceManagerProvider() const
    {
        return services_.resources;
    }

    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()>
    physicsWorld2DProvider() const
    {
        return services_.physics;
    }

    static const std::array<SceneComponentCatalogEntry, kEditorSceneComponentCount>&
    componentCatalog() noexcept;
    core::pmr::Vector<SceneHierarchyEntry> hierarchyEntries() const;

    scene::Scene& scene() noexcept { return *scene_; }

    const scene::Scene& scene() const noexcept { return *scene_; }

    scene::GameObjectId selectedObject() const noexcept { return selectedObject_; }

    bool select(scene::GameObjectId id) noexcept;
    static bool validObjectName(std::string_view name) noexcept;
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

    // Authoring candidate access reuses the Inspector metadata and write validator.
    static core::pmr::Vector<SceneInspectorProperty> componentProperties(const scene::Component&);
    static SceneInspectorWriteResult applyComponentProperty(scene::Component&,
        const SceneInspectorProperty&,
        const scene::Scene::IdentityRestoration* = nullptr);
    static bool applyComponentProperties(scene::Component&,
        const core::pmr::Vector<SceneInspectorProperty>&,
        const scene::Scene::IdentityRestoration&);
    using ComponentResolver =
        core::SharedCallback<scene::Component*(scene::GameObjectId, std::string_view)>;
    scene::Scene::ComponentFactory identityRestorationFactory(ComponentResolver = {}) const;

private:
    scene::Scene::ComponentFactory makeComponentFactory(ComponentResolver = {}) const;
    // Owned indirection preserves the document's existing move availability.
    core::UniquePtr<core::LifetimeEpoch> ownerEpoch_ = core::makeUnique<core::LifetimeEpoch>();
    // Keep this service alive until the scene and its components are destroyed.
    core::UniquePtr<audio::AudioSystem> audioSystem_;
    ServiceProviders services_;
    bool schemaOnlyComponentCreation_ = false;
    core::UniquePtr<scene::Scene> scene_;
    core::SharedPtr<scene::MainSceneRegistry> scriptSceneRegistry_ =
        core::makeShared<scene::MainSceneRegistry>();
    // Destruction and moves retire Scene before releasing its script services.
    core::SharedPtr<EditorSceneScriptBindings> scriptBindings_;
    scene::GameObjectId selectedObject_;
    core::pmr::String runtimeSceneId_;
};
} // namespace arp::editor
