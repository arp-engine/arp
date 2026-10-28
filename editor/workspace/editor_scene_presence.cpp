#include "editor/workspace/editor_scene_presence.h"

#include "2d/physics_components.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_component_registry.h"
#include "editor/workspace/editor_script_component_archive.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace arp::editor {
namespace {
using Presence = EditorScenePresence;
using Kind = SceneInspectorPropertyKind;

void require(bool condition, const char* diagnostic)
{
    if (!condition)
        throw std::invalid_argument(diagnostic);
}

EditorScriptComponentArchive scriptArchive(const EditorSceneDocument& document)
{
    // History owns explicit operation budgets. Field traversal retains the
    // formal SerializationArchive limits; no gameplay defaults are inferred.
    return EditorScriptComponentArchive(document,
        {Presence::operationLimit.bytes,
            Presence::operationLimit.references,
            Presence::operationLimit.bytes,
            reflection::SerializationOptions{}});
}

bool sameLinks(const core::pmr::Vector<scene::SceneObjectLink>& a,
    const core::pmr::Vector<scene::SceneObjectLink>& b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
               return x.name == y.name && x.target == y.target;
           });
}

bool sameTransform(const scene::Transform& a, const scene::Transform& b)
{
    return a.translation.x == b.translation.x && a.translation.y == b.translation.y
        && a.translation.z == b.translation.z && a.scale.x == b.scale.x && a.scale.y == b.scale.y
        && a.scale.z == b.scale.z && a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y
        && a.rotation.z == b.rotation.z && a.rotation.w == b.rotation.w;
}

bool contains(const Presence::Subtree& tree, scene::GameObjectId id)
{
    return std::any_of(tree.objects.begin(), tree.objects.end(), [id](const auto& object) {
        return object.id == id;
    });
}

void checkBudget(Presence::Cost cost)
{
    if (!Presence::fits(cost, Presence::operationLimit))
        throw std::length_error("EDITOR-SCENE-HISTORY-OPERATION-BUDGET");
}

void validateReferences(const EditorSceneDocument& document,
    const SceneInspectorProperty& property,
    const scene::Scene::IdentityRestoration& transaction)
{
    if (property.kind == Kind::ResourceReference) {
        auto* resources =
            document.resourceManagerProvider() ? document.resourceManagerProvider()() : nullptr;
        require(property.stringValue.empty()
                || EditorSceneAuthoringCodec::isResourceReady(
                    resources, property.resourceType, property.stringValue),
            "EDITOR-SCENE-HISTORY-RESOURCE-NOT-READY");
    } else if (property.kind == Kind::SceneObjectReference) {
        const auto* object = transaction.resolveObject(
            scene::GameObjectId::fromValue(property.sceneObjectReferenceValue));
        require(!property.sceneObjectReferenceValue ? property.resourceOptional
                                                    : object
                    && EditorSceneObjectReferenceChoices::matchesTarget(
                        *object, property.sceneObjectType),
            "EDITOR-SCENE-HISTORY-OBJECT-REFERENCE");
    } else if (property.kind == Kind::SceneObjectLinkList) {
        for (const auto& link : property.sceneObjectLinkListValue)
            require(transaction.resolveObject(scene::GameObjectId::fromValue(link.targetObjectId)),
                "EDITOR-SCENE-HISTORY-OBJECT-LINK");
    }
}

scene::ObjectLinksComponent* externalComponent(
    EditorSceneDocument& document, const Presence::ExternalLinks& snapshot)
{
    auto* owner = document.scene().findObject(snapshot.owner);
    return owner
        ? dynamic_cast<scene::ObjectLinksComponent*>(owner->findComponent(snapshot.component))
        : nullptr;
}

// All external references are prepared and validated before either Scene or
// reference publication. ObjectLinks owns a public vector with noexcept swap.
class PreparedExternalLinks final {
public:
    PreparedExternalLinks(
        EditorSceneDocument& document, const Presence::Subtree& snapshot, bool restoring)
    {
        for (const auto& links : snapshot.externalLinks) {
            auto* component = externalComponent(document, links);
            require(
                component && sameLinks(component->links, restoring ? links.after : links.before),
                "EDITOR-SCENE-HISTORY-EXTERNAL-LINK-STALE");
            replacements_.push_back({component, restoring ? links.before : links.after});
        }
    }

    void commit() noexcept
    {
        for (auto& replacement : replacements_)
            replacement.component->links.swap(replacement.links);
    }

private:
    struct Replacement {
        scene::ObjectLinksComponent* component;
        core::pmr::Vector<scene::SceneObjectLink> links;
    };

    core::pmr::Vector<Replacement> replacements_;
};

class PreparedExternalJoints final {
public:
    PreparedExternalJoints(EditorSceneDocument& document,
        const Presence::Subtree& snapshot,
        bool restoring,
        const scene::Scene::IdentityRestoration* transaction = nullptr)
    {
        replacements_.reserve(snapshot.externalJoints.size());
        for (const auto& entry : snapshot.externalJoints) {
            auto* owner = document.scene().findObject(entry.owner);
            auto* component = owner
                ? dynamic_cast<physics2d::Joint2DComponent*>(owner->findComponent(entry.before.id))
                : nullptr;
            auto expected = entry.before;
            if (restoring)
                for (auto& field : expected.properties) {
                    if (field.name == "bodyA")
                        field.sceneObjectReferenceValue = entry.afterA;
                    if (field.name == "bodyB")
                        field.sceneObjectReferenceValue = entry.afterB;
                }
            require(component && Presence::matchesComponent(*component, expected),
                "EDITOR-SCENE-HISTORY-EXTERNAL-JOINT-STALE");
            auto properties =
                component->editorAuthoringProperties().cast<physics2d::Joint2DEditorProperties>();
            for (const auto& field : entry.before.properties) {
                if (field.name == "bodyA")
                    properties.bodyA = restoring ? field.sceneObjectReferenceValue : entry.afterA;
                if (field.name == "bodyB")
                    properties.bodyB = restoring ? field.sceneObjectReferenceValue : entry.afterB;
            }
            Replacement replacement{component, {}};
            require(component->prepareAuthoringProperties(
                        properties, transaction, replacement.prepared),
                "EDITOR-SCENE-HISTORY-EXTERNAL-JOINT-PREPARE");
            replacements_.push_back(std::move(replacement));
        }
    }

    void commit() noexcept
    {
        for (auto& replacement : replacements_) {
            const bool committed =
                replacement.component->commitAuthoringProperties(std::move(replacement.prepared));
            assert(committed);
            (void)committed;
        }
    }

private:
    struct Replacement {
        physics2d::Joint2DComponent* component;
        physics2d::Joint2DComponent::PreparedAuthoring prepared;
    };

    core::pmr::Vector<Replacement> replacements_;
};
} // namespace

EditorScenePresence::Cost& EditorScenePresence::Cost::operator+=(const Cost& other) noexcept
{
    bytes += other.bytes;
    objects += other.objects;
    components += other.components;
    references += other.references;
    return *this;
}

bool EditorScenePresence::fits(Cost cost, Cost limit) noexcept
{
    return cost.bytes <= limit.bytes && cost.objects <= limit.objects
        && cost.components <= limit.components && cost.references <= limit.references;
}

EditorScenePresence::Cost EditorScenePresence::propertyCost(
    const SceneInspectorProperty& property) noexcept
{
    Cost cost{sizeof(property)};
    for (const auto* text : {&property.componentType,
             &property.name,
             &property.typeName,
             &property.resourceType,
             &property.sceneObjectType,
             &property.resourceDiagnostic,
             &property.stringValue})
        cost.bytes += text->capacity() + 1;
    cost.bytes += property.enumOptions.capacity() * sizeof(SceneInspectorEnumOption)
        + property.vector2ListValue.capacity() * sizeof(core::Vec2)
        + property.sceneObjectLinkListValue.capacity() * sizeof(SceneInspectorObjectLink);
    for (const auto& option : property.enumOptions)
        cost.bytes += option.name.capacity() + option.commandValue.capacity() + 2;
    for (const auto& link : property.sceneObjectLinkListValue)
        cost.bytes += link.name.capacity() + 1;
    if (property.kind == Kind::ResourceReference || property.kind == Kind::SceneObjectReference)
        ++cost.references;
    if (property.kind == Kind::SceneObjectLinkList)
        cost.references += property.sceneObjectLinkListValue.size();
    return cost;
}

EditorScenePresence::Component EditorScenePresence::captureComponent(
    const scene::Component& component, std::size_t index)
{
    require(index && editorSceneComponentIdentityRestorable(component.scriptTypeName())
            && component.scriptInstance().is_undefined() && !component.pendingDestroy(),
        "EDITOR-SCENE-HISTORY-FACTORY-UNSUPPORTED");
    Component result;
    result.id = component.id();
    result.index = index;
    result.type = component.scriptTypeName();
    result.cost = {sizeof(Component) + result.type.capacity() + 1, 0, 1, 0};
    for (auto& property : EditorSceneDocument::componentProperties(component)) {
        if (!property.serializable)
            continue;
        require(property.editable && property.kind != Kind::Unsupported,
            "EDITOR-SCENE-HISTORY-PROPERTY-UNSUPPORTED");
        if (result.properties.size() >= EditorSceneAuthoringCodec::maxPropertiesPerComponent)
            throw std::length_error("EDITOR-SCENE-HISTORY-PROPERTY-CAPACITY");
        result.cost += propertyCost(property);
        checkBudget(result.cost);
        result.properties.push_back(std::move(property));
    }
    // Conservatively charge vector capacity and inline storage as well as values.
    result.cost.bytes += result.properties.capacity() * sizeof(SceneInspectorProperty);
    checkBudget(result.cost);
    return result;
}

EditorScenePresence::Component EditorScenePresence::captureComponent(
    const EditorSceneDocument& document,
    const scene::Component& component,
    std::size_t index,
    const scene::Scene::IdentityRestoration* transaction)
{
    if (component.scriptInstance().is_undefined())
        return captureComponent(component, index);
    require(index, "EDITOR-SCENE-HISTORY-COMPONENT-INDEX");
    Component result;
    result.id = component.id();
    result.index = index;
    result.type = component.scriptTypeName();
    result.script = core::makeShared<EditorScriptComponentState>(
        scriptArchive(document).capture(component, transaction));
    result.cost = {sizeof(Component) + result.type.capacity() + 1
            + EditorScriptComponentArchive::storageBytes(*result.script),
        0,
        1,
        result.script->references.size()};
    checkBudget(result.cost);
    return result;
}

scene::Component& EditorScenePresence::stageComponent(const EditorSceneDocument& document,
    scene::Scene::IdentityRestoration& transaction,
    scene::GameObjectId owner,
    const Component& snapshot,
    bool allocateIdentity)
{
    if (snapshot.script)
        return scriptArchive(document).stage(
            transaction, owner, snapshot.id, snapshot.index, *snapshot.script, allocateIdentity);
    return allocateIdentity
        ? transaction.createComponent(owner, snapshot.type, snapshot.index)
        : transaction.restoreComponent(owner, snapshot.id, snapshot.type, snapshot.index);
}

bool EditorScenePresence::matchesComponent(const EditorSceneDocument& document,
    const scene::Component& component,
    const Component& snapshot)
{
    if (!snapshot.script)
        return matchesComponent(component, snapshot);
    return component.id() == snapshot.id && component.scriptTypeName() == snapshot.type
        && EditorScriptComponentArchive::equal(
            scriptArchive(document).capture(component), *snapshot.script);
}

bool EditorScenePresence::matchesComponent(
    const scene::Component& component, const Component& snapshot)
{
    if (component.id() != snapshot.id || component.scriptTypeName() != snapshot.type)
        return false;
    const auto actual = captureComponent(component, snapshot.index);
    return actual.properties.size() == snapshot.properties.size()
        && std::all_of(
            snapshot.properties.begin(), snapshot.properties.end(), [&](const auto& field) {
                const auto found = std::find_if(
                    actual.properties.begin(), actual.properties.end(), [&](const auto& item) {
                        return item.name == field.name;
                    });
                return found != actual.properties.end()
                    && sceneInspectorMetadataEqual(*found, field)
                    && sceneInspectorValuesEqual(*found, field);
            });
}

void EditorScenePresence::applyComponent(scene::Component& component,
    const Component& snapshot,
    const EditorSceneDocument& document,
    const scene::Scene::IdentityRestoration& transaction)
{
    require(component.id() == snapshot.id && component.scriptTypeName() == snapshot.type,
        "EDITOR-SCENE-HISTORY-COMPONENT-MISMATCH");
    if (snapshot.script) {
        require(snapshot.properties.empty(), "EDITOR-SCENE-HISTORY-MIXED-COMPONENT-RECORD");
        scriptArchive(document).apply(component, *snapshot.script, transaction);
        return;
    }
    require(
        component.scriptInstance().is_undefined(), "EDITOR-SCENE-HISTORY-SCRIPT-RECORD-REQUIRED");
    const auto schema = EditorSceneDocument::componentProperties(component);
    for (const auto& property : snapshot.properties) {
        const auto found = std::find_if(schema.begin(), schema.end(), [&](const auto& item) {
            return item.name == property.name;
        });
        require(found != schema.end() && sceneInspectorMetadataEqual(*found, property),
            "EDITOR-SCENE-HISTORY-SCHEMA-MISMATCH");
        validateReferences(document, property, transaction);
    }
    require(
        EditorSceneDocument::applyComponentProperties(component, snapshot.properties, transaction),
        "EDITOR-SCENE-HISTORY-PROPERTY-REJECTED");
    require(matchesComponent(component, snapshot), "EDITOR-SCENE-HISTORY-ROUNDTRIP-MISMATCH");
}

void EditorScenePresence::validateRemoval(
    const EditorSceneDocument& document, const scene::Component& component)
{
    require(!component.scriptInstance().is_undefined()
            || editorSceneComponentIdentityRestorable(component.scriptTypeName()),
        "EDITOR-SCENE-HISTORY-FACTORY-UNSUPPORTED");
    const auto* owner = component.gameObject();
    require(owner, "EDITOR-SCENE-HISTORY-OWNER");
    // This constructor dependency is already explicit in the component registry.
    if (component.scriptInstance().is_undefined()
        && std::string_view(component.scriptTypeName()) == "RigidBody2DComponent")
        for (const auto& sibling : owner->components())
            require(std::string_view(sibling->scriptTypeName()) != "Collider2DComponent",
                "EDITOR-SCENE-HISTORY-COMPONENT-DEPENDENCY");
    if (component.scriptInstance().is_undefined()
        && std::string_view(component.scriptTypeName()) == "SpriteRendererComponent")
        for (const auto& sibling : owner->components())
            require(std::string_view(sibling->scriptTypeName()) != "SpriteAnimationComponent",
                "EDITOR-SCENE-HISTORY-COMPONENT-DEPENDENCY");
    const auto checkScripts = [&](const scene::GameObject& object) {
        for (const auto& candidate : object.components()) {
            if (candidate.get() == &component || candidate->scriptInstance().is_undefined())
                continue;
            const auto state = scriptArchive(document).capture(*candidate);
            for (const auto& reference : state.references)
                require(reference.owner != owner->id() || reference.component != component.id(),
                    "EDITOR-SCENE-HISTORY-COMPONENT-REFERENCED");
        }
    };
    checkScripts(document.scene());
    for (const auto& object : document.scene().objects())
        checkScripts(*object);
    for (const auto* object : {static_cast<const scene::GameObject*>(&document.scene())})
        for (const auto& field : document.inspectorProperties(object->id()))
            require(!(field.kind == Kind::SceneObjectReference
                        && field.sceneObjectReferenceValue == owner->id().value()
                        && field.sceneObjectType == component.scriptTypeName()),
                "EDITOR-SCENE-HISTORY-COMPONENT-REFERENCED");
    for (const auto& object : document.scene().objects())
        for (const auto& field : document.inspectorProperties(object->id()))
            require(!(field.kind == Kind::SceneObjectReference
                        && field.sceneObjectReferenceValue == owner->id().value()
                        && field.sceneObjectType == component.scriptTypeName()),
                "EDITOR-SCENE-HISTORY-COMPONENT-REFERENCED");
}

void EditorScenePresence::validateRestoration(
    EditorSceneDocument& document, scene::GameObjectId owner, const Component& snapshot)
{
    // Prove that the current typed writer can reconstruct the snapshot before
    // deleting its source. Native clients can author values outside Inspector
    // limits; capturing them alone would leave an irrecoverable Undo entry.
    scene::Scene::IdentityRestoration transaction(
        document.scene(), {0, 1}, document.identityRestorationFactory());
    const auto* object = document.scene().findObject(owner);
    require(object, "EDITOR-SCENE-HISTORY-OWNER");
    auto probe = snapshot;
    probe.index = object->components().size();
    auto& candidate = stageComponent(document, transaction, owner, probe, true);
    probe.id = candidate.id();
    applyComponent(candidate, probe, document, transaction);
    // Abandon the candidate: neither its temporary ID nor its native control is
    // published, and the live source keeps its identity and value.
}

EditorScenePresence::Subtree EditorScenePresence::captureSubtree(
    const EditorSceneDocument& document, scene::GameObjectId root, ScriptPolicy scripts)
{
    require(root != document.scene().rootId() && document.scene().findObject(root),
        "EDITOR-SCENE-HISTORY-SUBTREE-ROOT");
    require(document.scene().objects().size() <= EditorSceneAuthoringCodec::maxObjects,
        "EDITOR-SCENE-HISTORY-SCENE-CAPACITY");
    Subtree result;
    result.cost.bytes = sizeof(Subtree);
    // Preserve exact depth-first sibling order, independent of owned storage order.
    const auto append = [&](const auto& self, scene::GameObjectId id) -> void {
        require(!contains(result, id), "EDITOR-SCENE-HISTORY-HIERARCHY-CYCLE");
        const auto* object = document.scene().findObject(id);
        const auto* parent = object ? document.scene().findObject(object->parentId()) : nullptr;
        require(parent && !object->pendingDestroy(), "EDITOR-SCENE-HISTORY-PARENT");
        const auto sibling = std::find(parent->childIds().begin(), parent->childIds().end(), id);
        require(sibling != parent->childIds().end(), "EDITOR-SCENE-HISTORY-SIBLING");
        Object value;
        value.id = id;
        value.parent = parent->id();
        value.transformId = object->transform().id();
        value.childIndex = static_cast<std::size_t>(sibling - parent->childIds().begin());
        value.name = object->name();
        value.active = object->active();
        value.transform = object->local_transform();
        value.transformEnabled = object->transform().enabled();
        result.cost += {sizeof(Object) + value.name.capacity() + 1, 1, 1, 0};
        checkBudget(result.cost);
        for (std::size_t index = 1; index < object->components().size(); ++index) {
            auto component = scripts == ScriptPolicy::Archive
                ? captureComponent(document, *object->components()[index], index)
                : captureComponent(*object->components()[index], index);
            result.cost += component.cost;
            checkBudget(result.cost);
            value.components.push_back(std::move(component));
        }
        result.cost.bytes += value.components.capacity() * sizeof(Component);
        result.objects.push_back(std::move(value));
        checkBudget(result.cost);
        for (auto child : object->childIds())
            self(self, child);
    };
    append(append, root);
    const auto external = [&](const scene::GameObject& object) {
        if (contains(result, object.id()))
            return;
        const auto properties = document.inspectorProperties(object.id());
        for (const auto& field : properties)
            if (field.componentType != "Joint2DComponent"
                && field.kind == Kind::SceneObjectReference
                && contains(
                    result, scene::GameObjectId::fromValue(field.sceneObjectReferenceValue)))
                throw std::invalid_argument("EDITOR-SCENE-HISTORY-EXTERNAL-REFERENCE-UNSUPPORTED");
        for (std::size_t componentIndex = 0; componentIndex < object.components().size();
            ++componentIndex) {
            const auto& component = object.components()[componentIndex];
            if (!component->scriptInstance().is_undefined()) {
                const auto state = scriptArchive(document).capture(*component);
                for (const auto& reference : state.references)
                    require(!contains(result, reference.owner),
                        "EDITOR-SCENE-HISTORY-EXTERNAL-SCRIPT-REFERENCE-UNSUPPORTED");
            }
            const auto* joint = dynamic_cast<const physics2d::Joint2DComponent*>(component.get());
            if (joint
                && (contains(result, scene::GameObjectId::fromValue(joint->bodyAObjectId()))
                    || contains(result, scene::GameObjectId::fromValue(joint->bodyBObjectId())))) {
                require(joint->usage() == physics2d::ComponentUsage::Authoring,
                    "EDITOR-SCENE-HISTORY-EXTERNAL-JOINT-SIMULATED");
                Subtree::ExternalJoint entry;
                entry.owner = object.id();
                entry.before = captureComponent(*joint, componentIndex);
                entry.afterA =
                    contains(result, scene::GameObjectId::fromValue(joint->bodyAObjectId()))
                    ? 0
                    : joint->bodyAObjectId();
                entry.afterB =
                    contains(result, scene::GameObjectId::fromValue(joint->bodyBObjectId()))
                    ? 0
                    : joint->bodyBObjectId();
                result.cost += entry.before.cost;
                result.cost.bytes += sizeof(Subtree::ExternalJoint);
                result.cost.references += 2;
                checkBudget(result.cost);
                result.externalJoints.push_back(std::move(entry));
            }
            const auto* links = dynamic_cast<const scene::ObjectLinksComponent*>(component.get());
            if (!links)
                continue;
            ExternalLinks entry;
            entry.owner = object.id();
            entry.component = component->id();
            entry.before = links->links;
            for (const auto& link : entry.before)
                if (!contains(result, link.target))
                    entry.after.push_back(link);
            if (entry.before.size() == entry.after.size())
                continue;
            result.cost.references += entry.before.size() + entry.after.size();
            result.cost.bytes += sizeof(ExternalLinks)
                + (entry.before.capacity() + entry.after.capacity())
                    * sizeof(scene::SceneObjectLink);
            for (const auto* list : {&entry.before, &entry.after})
                for (const auto& link : *list)
                    result.cost.bytes += link.name.capacity() + 1;
            checkBudget(result.cost);
            result.externalLinks.push_back(std::move(entry));
        }
    };
    external(document.scene());
    for (const auto& object : document.scene().objects())
        external(*object);
    result.cost.bytes += result.objects.capacity() * sizeof(Object)
        + result.externalLinks.capacity() * sizeof(ExternalLinks)
        + result.externalJoints.capacity() * sizeof(Subtree::ExternalJoint);
    checkBudget(result.cost);
    return result;
}

bool EditorScenePresence::matchesSubtree(
    const EditorSceneDocument& document, const Subtree& snapshot)
{
    if (snapshot.objects.empty())
        return false;
    // Capturing again also detects newly added descendants and external incoming
    // references. No irreversible deletion precedes this comparison.
    const bool hasScripts =
        std::any_of(snapshot.objects.begin(), snapshot.objects.end(), [](const auto& object) {
            return std::any_of(
                object.components.begin(), object.components.end(), [](const auto& component) {
                    return bool(component.script);
                });
        });
    const auto actual = captureSubtree(document,
        snapshot.objects.front().id,
        hasScripts ? ScriptPolicy::Archive : ScriptPolicy::Reject);
    if (actual.objects.size() != snapshot.objects.size()
        || actual.externalLinks.size() != snapshot.externalLinks.size()
        || actual.externalJoints.size() != snapshot.externalJoints.size())
        return false;
    for (std::size_t i = 0; i < snapshot.objects.size(); ++i) {
        const auto& a = actual.objects[i];
        const auto& b = snapshot.objects[i];
        if (a.id != b.id || a.parent != b.parent || a.transformId != b.transformId
            || a.childIndex != b.childIndex || a.name != b.name || a.active != b.active
            || a.transformEnabled != b.transformEnabled || !sameTransform(a.transform, b.transform)
            || a.components.size() != b.components.size())
            return false;
        const auto* object = document.scene().findObject(b.id);
        for (std::size_t c = 0; c < b.components.size(); ++c)
            if (!matchesComponent(document, *object->components()[c + 1], b.components[c]))
                return false;
    }
    for (std::size_t i = 0; i < snapshot.externalLinks.size(); ++i) {
        const auto& a = actual.externalLinks[i];
        const auto& b = snapshot.externalLinks[i];
        if (a.owner != b.owner || a.component != b.component || !sameLinks(a.before, b.before)
            || !sameLinks(a.after, b.after))
            return false;
    }
    for (std::size_t i = 0; i < snapshot.externalJoints.size(); ++i) {
        const auto& a = actual.externalJoints[i];
        const auto& b = snapshot.externalJoints[i];
        const auto* owner = document.scene().findObject(b.owner);
        const auto* component = owner ? owner->findComponent(b.before.id) : nullptr;
        if (a.owner != b.owner || a.afterA != b.afterA || a.afterB != b.afterB || !component
            || !matchesComponent(*component, b.before))
            return false;
    }
    return true;
}

void EditorScenePresence::restoreSubtree(EditorSceneDocument& document, const Subtree& snapshot)
{
    require(!snapshot.objects.empty(), "EDITOR-SCENE-HISTORY-SUBTREE-EMPTY");
    PreparedExternalLinks external(document, snapshot, true);
    scene::Scene::IdentityRestoration transaction(document.scene(),
        {snapshot.cost.objects, snapshot.cost.components},
        document.identityRestorationFactory());
    for (const auto& value : snapshot.objects) {
        auto& object = transaction.restoreObject(
            value.id, value.transformId, value.name, value.parent, value.childIndex);
        object.setActive(value.active);
        object.set_local_transform(value.transform);
        object.transform().setEnabled(value.transformEnabled);
    }
    // Create every component before resolving local object/type references.
    for (const auto& object : snapshot.objects)
        for (const auto& component : object.components)
            stageComponent(document, transaction, object.id, component);
    for (const auto& object : snapshot.objects)
        for (const auto& component : object.components)
            applyComponent(*transaction.resolveComponent(object.id, component.id),
                component,
                document,
                transaction);
    PreparedExternalJoints joints(document, snapshot, true, &transaction);
    transaction.prepare();
    transaction.commit();
    external.commit();
    joints.commit();
}

void EditorScenePresence::destroySubtree(EditorSceneDocument& document, const Subtree& snapshot)
{
    require(matchesSubtree(document, snapshot), "EDITOR-SCENE-HISTORY-SUBTREE-STALE");
    PreparedExternalLinks external(document, snapshot, false);
    PreparedExternalJoints joints(document, snapshot, false);
    for (const auto& object : snapshot.objects)
        for (const auto& component : object.components)
            validateRestoration(document, object.id, component);
    // Passive destruction dispatches no script or native lifecycle hooks. Scene
    // removal and prepared vector swaps have no further allocation boundary.
    const auto result = document.scene().destroyObject(snapshot.objects.front().id);
    require(result.ok(), "EDITOR-SCENE-HISTORY-DESTROY-FAILED");
    external.commit();
    joints.commit();
}

} // namespace arp::editor
