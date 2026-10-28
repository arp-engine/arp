#pragma once

#include "editor/workspace/editor_scene_document.h"
#include "scene/scene_asset_resource.h"

namespace arp::editor {
struct EditorScriptComponentState;

// Authoring history is owned by Editor, never serialized as a runtime Scene ID.
class EditorScenePresence final {
public:
    struct Cost {
        std::size_t bytes = 0, objects = 0, components = 0, references = 0;
        Cost& operator+=(const Cost&) noexcept;
    };

    static constexpr Cost operationLimit{4u * 1024u * 1024u, 512u, 2048u, 8192u};
    static constexpr Cost historyLimit{16u * 1024u * 1024u, 4096u, 16384u, 65536u};
    static bool fits(Cost, Cost) noexcept;
    static Cost propertyCost(const SceneInspectorProperty&) noexcept;

    struct Component {
        scene::ComponentId id;
        std::size_t index = 0;
        core::pmr::String type;
        core::pmr::Vector<SceneInspectorProperty> properties;
        Cost cost;
        core::SharedPtr<const EditorScriptComponentState> script;
    };

    struct Object {
        scene::GameObjectId id, parent;
        scene::ComponentId transformId;
        std::size_t childIndex = 0;
        core::pmr::String name;
        bool active = true;
        scene::Transform transform;
        bool transformEnabled = true;
        core::pmr::Vector<Component> components;
    };

    struct ExternalLinks {
        scene::GameObjectId owner;
        scene::ComponentId component;
        core::pmr::Vector<scene::SceneObjectLink> before, after;
    };

    struct Subtree {
        struct ExternalJoint {
            scene::GameObjectId owner;
            Component before;
            std::uint64_t afterA = 0, afterB = 0;
        };

        core::pmr::Vector<Object> objects;
        core::pmr::Vector<ExternalLinks> externalLinks;
        core::pmr::Vector<ExternalJoint> externalJoints;
        Cost cost;
    };

    static Component captureComponent(const scene::Component&, std::size_t index);
    // Explicit script branch; native activity commands keep the overload above.
    static Component captureComponent(const EditorSceneDocument&,
        const scene::Component&,
        std::size_t index,
        const scene::Scene::IdentityRestoration* transaction = nullptr);
    static scene::Component& stageComponent(const EditorSceneDocument&,
        scene::Scene::IdentityRestoration&,
        scene::GameObjectId,
        const Component&,
        bool allocateIdentity = false);
    static bool matchesComponent(
        const EditorSceneDocument&, const scene::Component&, const Component&);
    static bool matchesComponent(const scene::Component&, const Component&);
    static void applyComponent(scene::Component&,
        const Component&,
        const EditorSceneDocument&,
        const scene::Scene::IdentityRestoration&);
    static void validateRemoval(const EditorSceneDocument&, const scene::Component&);
    static void validateRestoration(EditorSceneDocument&, scene::GameObjectId, const Component&);
    enum class ScriptPolicy { Reject, Archive };
    static Subtree captureSubtree(
        const EditorSceneDocument&, scene::GameObjectId, ScriptPolicy = ScriptPolicy::Reject);
    static bool matchesSubtree(const EditorSceneDocument&, const Subtree&);
    static void restoreSubtree(EditorSceneDocument&, const Subtree&);
    static void destroySubtree(EditorSceneDocument&, const Subtree&);
};

} // namespace arp::editor
