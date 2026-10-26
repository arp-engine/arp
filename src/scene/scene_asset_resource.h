#pragma once
#include "core/encoding/yaml_contract.h"
#include "reflection/serialization.h"
#include "resource/resource_manager.h"
#include "scene/component_factory.h"
#include "scene/scene.h"

#include <algorithm>

namespace arp::scene {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SceneObjectAuthoringProperties {
    APROP(Serialize, Editor)
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor)
    bool active = true;
};

ARP_API bool initializeSceneAuthoringReflection();

struct SceneObjectBinding {
    std::uint64_t localId = 0;
    GameObjectId objectId;
};

class SceneObjectBindings final {
public:
    explicit SceneObjectBindings(
        const core::pmr::Vector<SceneObjectBinding>& bindings, const Scene& scene)
        : bindings_(bindings), scene_(scene)
    {}

    const GameObject* findObject(std::uint64_t localId) const
    {
        return scene_.findObject(object(localId));
    }

    const Scene& scene() const { return scene_; }

    GameObjectId object(std::uint64_t local) const;
    std::uint64_t local(GameObjectId object) const;

private:
    const core::pmr::Vector<SceneObjectBinding>& bindings_;
    const Scene& scene_;
};

class SceneDocumentResource : public resource::ResourceBase {
public:
    core::UniquePtr<Scene> instantiate(
        resource::ResourceManager&, const SceneComponentFactory&, core::pmr::String&) const;
    core::UniquePtr<Scene> instantiate(resource::ResourceManager&,
        const SceneComponentFactory&,
        core::pmr::String&,
        SceneExecutionMode) const;
    GameObjectId instantiateInto(Scene&,
        GameObjectId,
        resource::ResourceManager&,
        const SceneComponentFactory&,
        core::pmr::String&) const;
    static bool encode(const Scene&,
        const SceneComponentFactory&,
        const char*,
        const core::pmr::String&,
        core::pmr::String&,
        core::pmr::String&);
    static bool validateDocument(const core::YamlContract::Node&,
        const core::pmr::String& documentType,
        const core::pmr::String& id,
        std::uint32_t maxObjects);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;
    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String&) const override;

    const auto& yaml() const { return yaml_; }

protected:
    SceneDocumentResource(core::pmr::String id,
        core::pmr::String path,
        std::size_t maxBytes,
        std::uint32_t maxObjects,
        const char* resourceType,
        const char* documentType);

private:
    bool populate(Scene&,
        GameObjectId,
        resource::ResourceManager&,
        const SceneComponentFactory&,
        core::pmr::String&) const;
    core::pmr::String path_, documentType_, yaml_;
    std::size_t maxBytes_;
    std::uint32_t maxObjects_;
};

class SceneAssetResource final : public SceneDocumentResource {
public:
    SceneAssetResource(
        core::pmr::String id, core::pmr::String path, std::size_t bytes, std::uint32_t objects)
        : SceneDocumentResource(std::move(id),
              std::move(path),
              bytes,
              objects,
              "SceneAssetResource",
              "arp.scene-asset")
    {}
};

class ScenePrefabResource final : public SceneDocumentResource {
public:
    ScenePrefabResource(
        core::pmr::String id, core::pmr::String path, std::size_t bytes, std::uint32_t objects)
        : SceneDocumentResource(std::move(id),
              std::move(path),
              bytes,
              objects,
              "ScenePrefabResource",
              "arp.scene-prefab")
    {}
};

// Named local references are resolved to stable scene object IDs in a second
// pass, after all objects exist. Ownership remains with Scene.
struct SceneObjectLink {
    core::pmr::String name;
    GameObjectId target;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ObjectLinksEditorEntry {
    APROP(Serialize, Editor)
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    APROP(Serialize,
        Editor,
        SceneObjectReference,
        ReferenceTarget = "*",
        Doc = "sceneObject:*:required")
    std::uint64_t targetObjectId = 0;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ObjectLinksEditorProperties {
    APROP(Serialize, Editor, Doc = "sceneObjectLinkList")
    core::pmr::Vector<ObjectLinksEditorEntry> links{&core::defaultCpuMemoryResource()};
};

class ObjectLinksComponent final : public Component {
public:
    core::pmr::Vector<SceneObjectLink> links;

    const char* scriptTypeName() const override { return "ObjectLinksComponent"; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        ObjectLinksEditorProperties properties;
        properties.links.reserve(links.size());
        for (const auto& link : links) {
            ObjectLinksEditorEntry entry;
            entry.name = link.name;
            entry.targetObjectId = link.target.value();
            properties.links.push_back(std::move(entry));
        }
        return arp::reflection::object(std::move(properties));
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        return applyRestoredAuthoringProperties(value, nullptr);
    }

    // An explicit candidate resolver never changes ordinary Scene visibility.
    bool applyRestoredAuthoringProperties(
        const arp::reflection::object& value, const Scene::IdentityRestoration* transaction)
    {
        if (!value.is<ObjectLinksEditorProperties>())
            return false;
        const auto& properties = value.cast<const ObjectLinksEditorProperties&>();
        if (properties.links.size() > 64u)
            return false;
        core::pmr::Vector<SceneObjectLink> prepared{&core::defaultCpuMemoryResource()};
        prepared.reserve(properties.links.size());
        for (const auto& entry : properties.links) {
            if (entry.name.empty() || entry.name.size() > 128u || entry.targetObjectId == 0u
                || std::any_of(prepared.begin(), prepared.end(), [&](const auto& prior) {
                       return prior.name == entry.name;
                   }))
                return false;
            prepared.push_back({entry.name, GameObjectId::fromValue(entry.targetObjectId)});
        }
        const auto* owner = gameObject();
        const auto* ownerScene = owner ? owner->scene() : nullptr;
        if (!ownerScene)
            return false;
        for (const auto& link : prepared)
            if (!(transaction ? transaction->resolveObject(link.target)
                              : ownerScene->findObject(link.target)))
                return false;
        links = std::move(prepared);
        return true;
    }
};

} // namespace arp::scene
