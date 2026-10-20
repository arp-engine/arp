#pragma once
#include "core/yaml_contract.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"

#include <algorithm>

namespace arp::scene {
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

class SceneComponentCodec {
public:
    virtual ~SceneComponentCodec() = default;
    virtual const char* type() const = 0;
    enum class Phase { Components, Bodies, Colliders, Joints };

    virtual Phase phase() const { return Phase::Components; }

    virtual bool accepts(const Component&) const = 0;
    virtual bool attach(GameObject&,
        const core::YamlContract::Node&,
        const SceneObjectBindings&,
        resource::ResourceManager&,
        core::pmr::String&) const = 0;
    virtual bool encode(const Component&,
        const SceneObjectBindings&,
        core::YamlContract::Node&,
        core::pmr::String&) const = 0;

    // Called only after every component has been attached. Enables local
    // forward references without depending on authored object/component order.
    virtual bool restore(GameObject&,
        const core::YamlContract::Node&,
        const SceneObjectBindings&,
        core::pmr::String&) const
    {
        return true;
    }
};

class SceneCodecRegistry final {
public:
    bool add(core::UniquePtr<SceneComponentCodec>);
    const SceneComponentCodec* find(std::string_view) const;
    const SceneComponentCodec* find(const Component&) const;

private:
    core::pmr::Vector<core::UniquePtr<SceneComponentCodec>> codecs_;
};

class SceneDocumentResource : public resource::ResourceBase {
public:
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;
    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String&) const override;
    // Caller publishes the candidate only after success. No live scene mutation.
    core::UniquePtr<Scene> instantiate(
        resource::ResourceManager&, const SceneCodecRegistry&, core::pmr::String& diagnostic) const;
    GameObjectId instantiateInto(Scene&,
        GameObjectId parent,
        resource::ResourceManager&,
        const SceneCodecRegistry&,
        core::pmr::String& diagnostic) const;

    const auto& yaml() const { return yaml_; }

    static bool encode(const Scene&,
        const SceneCodecRegistry&,
        const char* documentType,
        const core::pmr::String& resourceId,
        core::pmr::String& yaml,
        core::pmr::String& diagnostic);

protected:
    SceneDocumentResource(core::pmr::String id,
        core::pmr::String path,
        std::size_t maxBytes,
        std::uint32_t maxObjects,
        const char* resourceType,
        const char* documentType);

private:
    bool populate(Scene&,
        GameObjectId root,
        resource::ResourceManager&,
        const SceneCodecRegistry&,
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
    APROP(Editor)
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    APROP(Editor, Doc = "sceneObject:*:required")
    std::uint64_t targetObjectId = 0;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)
struct ObjectLinksEditorProperties {
    APROP(Editor, Doc = "sceneObjectLinkList")
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
            if (!ownerScene->findObject(link.target))
                return false;
        links = std::move(prepared);
        return true;
    }
};

class ObjectLinksCodec final : public SceneComponentCodec {
public:
    const char* type() const override { return "arp.object-links/1"; }

    bool accepts(const Component& c) const override
    {
        return dynamic_cast<const ObjectLinksComponent*>(&c) != nullptr;
    }

    bool attach(GameObject&,
        const core::YamlContract::Node&,
        const SceneObjectBindings&,
        resource::ResourceManager&,
        core::pmr::String&) const override;
    bool encode(const Component&,
        const SceneObjectBindings&,
        core::YamlContract::Node&,
        core::pmr::String&) const override;
};
} // namespace arp::scene
