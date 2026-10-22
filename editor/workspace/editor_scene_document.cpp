#include "editor/workspace/editor_scene_document.h"

#include "2d/physics_components.h"
#include "3d/components.h"
#include "editor/workspace/editor_scene_component_registry.h"
#include "reflection/reflection.h"
#include "scene/camera.h"
#include "scene/scene_asset_resource.h"

#include <array>
#include <cmath>
#include <mutex>

namespace arp::reflection::generated {
void register_arp_scene_reflection();
void register_arp_scene2d_reflection();
void register_arp_scene3d_reflection();
void register_arp_resources_reflection();
void register_arp_editor_component_properties_reflection();
} // namespace arp::reflection::generated

namespace arp::editor {
namespace {
using reflection::class_info;
using reflection::object;
using reflection::property;
using reflection::PropertyHint;

bool reflectedEditorEnumValue(const object& value, int& result)
{
    return value.tryGetEnumInt(result);
}

bool isEditorEnumOption(std::string_view name)
{
    return name != "Count";
}

bool referenceMetadata(
    const property& value, reflection::ReferenceKind kind, std::string_view& target, bool& optional)
{
    const auto& metadata = value.reference();
    if (metadata.kind != kind || metadata.targetType.empty())
        return false;
    target = std::string_view(metadata.targetType.data(), metadata.targetType.size());
    optional = !metadata.required;
    return true;
}

void ensureSceneReflectionRegistered()
{
    static std::once_flag registration;
    std::call_once(registration, [] {
        reflection::generated::register_arp_scene_reflection();
        reflection::generated::register_arp_scene2d_reflection();
        reflection::generated::register_arp_scene3d_reflection();
        reflection::generated::register_arp_resources_reflection();
        reflection::generated::register_arp_editor_component_properties_reflection();
    });
}

void appendReflectedProperties(core::pmr::Vector<SceneInspectorProperty>& result,
    std::uint64_t objectId,
    const scene::Component& component,
    const object& reflectedDescriptor,
    std::string_view prefix = {},
    PropertyHint hint = PropertyHint::Editor,
    bool forceReadOnly = false)
{
    const class_info* reflectedClass = reflectedDescriptor.class_();
    if (!reflectedClass)
        return;
    const auto properties = reflectedClass->get_all_properties();
    for (const auto& entry : properties) {
        const auto& reflectedProperty = entry.second.cast<property&>();
        if (!reflectedProperty.check_hints(static_cast<std::uint8_t>(hint)))
            continue;

        const object value = reflectedProperty.get_value(reflectedDescriptor);
        core::pmr::String qualifiedName(prefix, &core::defaultCpuMemoryResource());
        if (!qualifiedName.empty())
            qualifiedName.push_back('.');
        qualifiedName += entry.first;
        if (reflectedProperty.type() && reflectedProperty.type()->is_class() && value.class_()
            && !value.is_native_reference() && !value.is<core::Vec3>() && !value.is<core::Vec2>()
            && !value.is<core::Vec4>() && !value.is<core::pmr::String>()
            && !value.is<core::pmr::Vector<core::Vec2>>()
            && !value.is<core::pmr::Vector<scene::ObjectLinksEditorEntry>>()) {
            appendReflectedProperties(
                result, objectId, component, value, qualifiedName, hint, forceReadOnly);
            continue;
        }

        SceneInspectorProperty item;
        item.serializable =
            reflectedProperty.check_hints(static_cast<std::uint8_t>(PropertyHint::Serialize));
        item.objectId = objectId;
        item.componentId = component.id().value();
        item.componentType = component.scriptTypeName();
        item.name = std::move(qualifiedName);
        item.typeName = reflectedProperty.type() && reflectedProperty.type()->class_()
            ? reflectedProperty.type()->class_()->name()
            : core::pmr::String("scalar", &core::defaultCpuMemoryResource());

        if (value.is<bool>()) {
            item.kind = SceneInspectorPropertyKind::Boolean;
            item.booleanValue = value.cast<bool>();
        } else if (value.is<std::uint32_t>()) {
            item.kind = SceneInspectorPropertyKind::UnsignedInteger;
            item.unsignedIntegerValue = value.cast<std::uint32_t>();
        } else if (value.is<std::uint64_t>()) {
            const std::string_view metadata(
                reflectedProperty.doc().data(), reflectedProperty.doc().size());
            std::string_view targetComponentType;
            if (referenceMetadata(reflectedProperty,
                    reflection::ReferenceKind::SceneObject,
                    targetComponentType,
                    item.resourceOptional)) {
                item.kind = SceneInspectorPropertyKind::SceneObjectReference;
                item.sceneObjectType = targetComponentType;
                item.sceneObjectReferenceValue = value.cast<std::uint64_t>();
            } else {
                item.kind = SceneInspectorPropertyKind::UnsignedInteger;
                item.unsignedIntegerValue = value.cast<std::uint64_t>();
            }
        } else if (value.is<int>()) {
            item.kind = SceneInspectorPropertyKind::Integer;
            item.integerValue = value.cast<int>();
        } else if (value.is<float>() || value.is<double>()) {
            item.kind = SceneInspectorPropertyKind::Number;
            item.numberValue = value.is<float>() ? value.cast<float>() : value.cast<double>();
        } else if (value.is<core::pmr::String>()
            && reflectedProperty.reference().kind == reflection::ReferenceKind::Resource) {
            const std::string_view metadata(
                reflectedProperty.doc().data(), reflectedProperty.doc().size());
            std::string_view resourceType;
            if (!referenceMetadata(reflectedProperty,
                    reflection::ReferenceKind::Resource,
                    resourceType,
                    item.resourceOptional)) {
                item.kind = SceneInspectorPropertyKind::Unsupported;
                item.resourceDiagnostic = "EDITOR-RESOURCE-REFERENCE-METADATA-INVALID";
            } else {
                item.kind = SceneInspectorPropertyKind::ResourceReference;
                item.resourceType = resourceType;
            }
            item.stringValue = value.cast<const core::pmr::String&>();
        } else if (value.is<core::pmr::String>()) {
            item.kind = SceneInspectorPropertyKind::String;
            item.stringValue = value.cast<const core::pmr::String&>();
        } else if (reflectedProperty.type() && reflectedProperty.type()->is_enum()) {
            item.kind = SceneInspectorPropertyKind::Enumeration;
            int currentEnumValue = 0;
            const bool hasCurrentEnumValue = reflectedEditorEnumValue(value, currentEnumValue);
            bool currentOptionFound = false;
            const auto* enumClass = reflectedProperty.type()->class_();
            if (enumClass) {
                const auto fields = enumClass->get_all_fields();
                for (const auto& [fieldName, fieldValue] : fields) {
                    if (!fieldValue.is<reflection::enumeration>())
                        continue;
                    const auto& values = fieldValue.cast<const reflection::enumeration&>().values();
                    for (const auto& [optionName, optionValue] : values) {
                        if (!isEditorEnumOption(optionName))
                            continue;
                        item.enumOptions.emplace_back();
                        item.enumOptions.back().name = optionName;
                        item.enumOptions.back().value = optionValue;
                        if (hasCurrentEnumValue && currentEnumValue == optionValue) {
                            item.integerValue = optionValue;
                            currentOptionFound = true;
                        }
                    }
                }
            }
            if (item.enumOptions.empty() || !currentOptionFound)
                item.kind = SceneInspectorPropertyKind::Unsupported;
        } else if (value.is<core::Vec3>()) {
            item.kind = SceneInspectorPropertyKind::Vector3;
            item.vector3Value = value.cast<core::Vec3>();
        } else if (value.is<core::Vec2>()) {
            item.kind = SceneInspectorPropertyKind::Vector2;
            item.vector2Value = value.cast<core::Vec2>();
        } else if (value.is<core::pmr::Vector<core::Vec2>>()) {
            item.kind = SceneInspectorPropertyKind::Vector2List;
            const auto& points = value.cast<const core::pmr::Vector<core::Vec2>&>();
            item.vector2ListValue.assign(points.begin(), points.end());
        } else if (value.is<core::pmr::Vector<scene::ObjectLinksEditorEntry>>()) {
            const std::string_view metadata(
                reflectedProperty.doc().data(), reflectedProperty.doc().size());
            if (metadata == "sceneObjectLinkList") {
                item.kind = SceneInspectorPropertyKind::SceneObjectLinkList;
                const auto& links =
                    value.cast<const core::pmr::Vector<scene::ObjectLinksEditorEntry>&>();
                item.sceneObjectLinkListValue.reserve(links.size());
                for (const auto& link : links) {
                    item.sceneObjectLinkListValue.emplace_back();
                    auto& target = item.sceneObjectLinkListValue.back();
                    target.name = link.name;
                    target.targetObjectId = link.targetObjectId;
                }
            }
        } else if (value.is<core::Vec4>()
            && std::string_view(reflectedProperty.doc().data(), reflectedProperty.doc().size())
                == "color:rgba") {
            item.kind = SceneInspectorPropertyKind::Color4;
            item.color4Value = value.cast<core::Vec4>();
        } else {
            item.kind = SceneInspectorPropertyKind::Unsupported;
            if (reflectedProperty.type()) {
                if (const auto* propertyClass = reflectedProperty.type()->class_())
                    item.typeName = propertyClass->name();
            }
        }
        item.editable = !forceReadOnly && !reflectedProperty.is_readonly()
            && item.kind != SceneInspectorPropertyKind::Unsupported;
        item.editable = item.editable
            && (item.kind != SceneInspectorPropertyKind::Enumeration || !item.enumOptions.empty());
        result.push_back(std::move(item));
        if (forceReadOnly && result.back().kind == SceneInspectorPropertyKind::Unsupported)
            result.pop_back();
    }
}

SceneInspectorWriteResult writeReflectedObjectProperty(const scene::Component& component,
    object& reflectedDescriptor,
    std::string_view propertyName,
    SceneInspectorPropertyKind valueKind,
    bool booleanValue,
    int integerValue,
    std::uint64_t unsignedIntegerValue,
    double numberValue,
    core::Vec3 vector3Value,
    core::Vec4 color4Value,
    std::string_view stringValue,
    core::Vec2 vector2Value,
    const core::pmr::Vector<core::Vec2>& vector2ListValue,
    std::uint64_t sceneObjectReferenceValue,
    const core::pmr::Vector<SceneInspectorObjectLink>& sceneObjectLinkListValue)
{
    const class_info* reflectedClass = reflectedDescriptor.class_();
    if (!reflectedClass)
        return SceneInspectorWriteResult::Rejected;
    const auto separator = propertyName.find('.');
    const auto parentName =
        separator == std::string_view::npos ? propertyName : propertyName.substr(0, separator);
    const object propertyObject = reflectedClass->get_field(parentName);
    if (!propertyObject.is_property())
        return SceneInspectorWriteResult::Rejected;
    const auto& reflectedProperty = propertyObject.cast<property&>();
    if (!reflectedProperty.check_hints(static_cast<std::uint8_t>(PropertyHint::Editor))
        || reflectedProperty.is_readonly())
        return SceneInspectorWriteResult::Rejected;

    const object currentValue = reflectedProperty.get_value(reflectedDescriptor);
    if (separator != std::string_view::npos) {
        if (!reflectedProperty.type() || !reflectedProperty.type()->is_class()
            || !currentValue.class_() || currentValue.is_native_reference())
            return SceneInspectorWriteResult::Rejected;
        object nestedDescriptor = currentValue;
        const auto nestedResult = writeReflectedObjectProperty(component,
            nestedDescriptor,
            propertyName.substr(separator + 1),
            valueKind,
            booleanValue,
            integerValue,
            unsignedIntegerValue,
            numberValue,
            vector3Value,
            color4Value,
            stringValue,
            vector2Value,
            vector2ListValue,
            sceneObjectReferenceValue,
            sceneObjectLinkListValue);
        if (nestedResult == SceneInspectorWriteResult::Changed)
            reflectedProperty.set_value(reflectedDescriptor, nestedDescriptor);
        return nestedResult;
    }

    object newValue;
    if (valueKind == SceneInspectorPropertyKind::Boolean && currentValue.is<bool>()) {
        if (currentValue.cast<bool>() == booleanValue)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(booleanValue);
    } else if (valueKind == SceneInspectorPropertyKind::Integer && currentValue.is<int>()) {
        if (currentValue.cast<int>() == integerValue)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(integerValue);
    } else if (valueKind == SceneInspectorPropertyKind::UnsignedInteger
        && currentValue.is<std::uint32_t>()) {
        if (currentValue.cast<std::uint32_t>() == unsignedIntegerValue)
            return SceneInspectorWriteResult::Unchanged;
        if (unsignedIntegerValue > UINT32_MAX)
            return SceneInspectorWriteResult::Rejected;
        newValue = object(static_cast<std::uint32_t>(unsignedIntegerValue));
    } else if (valueKind == SceneInspectorPropertyKind::UnsignedInteger
        && currentValue.is<std::uint64_t>()) {
        if (currentValue.cast<std::uint64_t>() == unsignedIntegerValue)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(unsignedIntegerValue);
    } else if (valueKind == SceneInspectorPropertyKind::SceneObjectReference
        && currentValue.is<std::uint64_t>()) {
        std::string_view targetComponentType;
        bool optional = false;
        if (!referenceMetadata(reflectedProperty,
                reflection::ReferenceKind::SceneObject,
                targetComponentType,
                optional))
            return SceneInspectorWriteResult::Rejected;
        if (sceneObjectReferenceValue == 0) {
            (void)optional;
        } else {
            const auto* owner = component.gameObject();
            const auto* scene = owner ? owner->scene() : nullptr;
            const auto* target = scene
                ? scene->findObject(scene::GameObjectId::fromValue(sceneObjectReferenceValue))
                : nullptr;
            if (!target
                || std::none_of(target->components().begin(),
                    target->components().end(),
                    [&](const auto& candidate) {
                        return targetComponentType == "*"
                            || targetComponentType == candidate->scriptTypeName();
                    }))
                return SceneInspectorWriteResult::Rejected;
        }
        if (currentValue.cast<std::uint64_t>() == sceneObjectReferenceValue)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(sceneObjectReferenceValue);
    } else if (valueKind == SceneInspectorPropertyKind::Number && currentValue.is<float>()
        && std::isfinite(numberValue)) {
        if (currentValue.cast<float>() == numberValue)
            return SceneInspectorWriteResult::Unchanged;
        if (std::abs(numberValue) > std::numeric_limits<float>::max())
            return SceneInspectorWriteResult::Rejected;
        newValue = object(static_cast<float>(numberValue));
    } else if (valueKind == SceneInspectorPropertyKind::Number && currentValue.is<double>()
        && std::isfinite(numberValue)) {
        if (currentValue.cast<double>() == numberValue)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(numberValue);
    } else if (valueKind == SceneInspectorPropertyKind::Enumeration && reflectedProperty.type()
        && reflectedProperty.type()->is_enum()) {
        const auto* enumClass = reflectedProperty.type()->class_();
        bool validOption = false;
        if (enumClass) {
            const auto fields = enumClass->get_all_fields();
            for (const auto& [fieldName, fieldValue] : fields) {
                if (!fieldValue.is<reflection::enumeration>())
                    continue;
                const auto& values = fieldValue.cast<const reflection::enumeration&>().values();
                for (const auto& [optionName, optionValue] : values)
                    validOption = validOption
                        || (isEditorEnumOption(optionName) && optionValue == integerValue);
            }
        }
        if (!validOption)
            return SceneInspectorWriteResult::Rejected;
        int currentEnumValue = 0;
        if (reflectedEditorEnumValue(currentValue, currentEnumValue)
            && currentEnumValue == integerValue)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(integerValue);
    } else if (valueKind == SceneInspectorPropertyKind::Vector2 && currentValue.is<core::Vec2>()) {
        const auto current = currentValue.cast<core::Vec2>();
        if (!std::isfinite(vector2Value.x) || !std::isfinite(vector2Value.y))
            return SceneInspectorWriteResult::Rejected;
        if (current.x == vector2Value.x && current.y == vector2Value.y)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(vector2Value);
    } else if (valueKind == SceneInspectorPropertyKind::Vector2List
        && currentValue.is<core::pmr::Vector<core::Vec2>>()
        && vector2ListValue.size() <= kMaxSceneInspectorVector2ListElements) {
        for (const auto& point : vector2ListValue)
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return SceneInspectorWriteResult::Rejected;
        const auto& current = currentValue.cast<const core::pmr::Vector<core::Vec2>&>();
        if (current.size() == vector2ListValue.size()
            && std::equal(
                current.begin(), current.end(), vector2ListValue.begin(), [](auto a, auto b) {
                    return a.x == b.x && a.y == b.y;
                }))
            return SceneInspectorWriteResult::Unchanged;
        core::pmr::Vector<core::Vec2> prepared(&core::defaultCpuMemoryResource());
        prepared.assign(vector2ListValue.begin(), vector2ListValue.end());
        newValue = object(std::move(prepared));
    } else if (valueKind == SceneInspectorPropertyKind::SceneObjectLinkList
        && currentValue.is<core::pmr::Vector<scene::ObjectLinksEditorEntry>>()
        && sceneObjectLinkListValue.size() <= kMaxSceneInspectorObjectLinks) {
        const auto* owner = component.gameObject();
        const auto* ownerScene = owner ? owner->scene() : nullptr;
        if (!ownerScene)
            return SceneInspectorWriteResult::Rejected;
        core::pmr::Vector<scene::ObjectLinksEditorEntry> prepared{
            &core::defaultCpuMemoryResource()};
        prepared.reserve(sceneObjectLinkListValue.size());
        for (const auto& link : sceneObjectLinkListValue) {
            if (link.name.empty() || link.name.size() > 128u || link.targetObjectId == 0u
                || !ownerScene->findObject(scene::GameObjectId::fromValue(link.targetObjectId))
                || std::any_of(prepared.begin(), prepared.end(), [&](const auto& prior) {
                       return prior.name == link.name;
                   }))
                return SceneInspectorWriteResult::Rejected;
            scene::ObjectLinksEditorEntry entry;
            entry.name = link.name;
            entry.targetObjectId = link.targetObjectId;
            prepared.push_back(std::move(entry));
        }
        const auto& current =
            currentValue.cast<const core::pmr::Vector<scene::ObjectLinksEditorEntry>&>();
        if (current.size() == prepared.size()
            && std::equal(current.begin(),
                current.end(),
                prepared.begin(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.name == rhs.name && lhs.targetObjectId == rhs.targetObjectId;
                }))
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(std::move(prepared));
    } else if (valueKind == SceneInspectorPropertyKind::Vector3 && currentValue.is<core::Vec3>()) {
        const auto current = currentValue.cast<core::Vec3>();
        if (!std::isfinite(vector3Value.x) || !std::isfinite(vector3Value.y)
            || !std::isfinite(vector3Value.z))
            return SceneInspectorWriteResult::Rejected;
        if (current.x == vector3Value.x && current.y == vector3Value.y
            && current.z == vector3Value.z)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(vector3Value);
    } else if (valueKind == SceneInspectorPropertyKind::Color4 && currentValue.is<core::Vec4>()
        && std::string_view(reflectedProperty.doc().data(), reflectedProperty.doc().size())
            == "color:rgba") {
        const auto current = currentValue.cast<core::Vec4>();
        if (!std::isfinite(color4Value.x) || !std::isfinite(color4Value.y)
            || !std::isfinite(color4Value.z) || !std::isfinite(color4Value.w))
            return SceneInspectorWriteResult::Rejected;
        if (current.x == color4Value.x && current.y == color4Value.y && current.z == color4Value.z
            && current.w == color4Value.w)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(color4Value);
    } else if ((valueKind == SceneInspectorPropertyKind::String
                   || valueKind == SceneInspectorPropertyKind::ResourceReference)
        && currentValue.is<core::pmr::String>() && stringValue.size() <= 512u) {
        if (valueKind == SceneInspectorPropertyKind::ResourceReference) {
            std::string_view resourceType;
            bool optional = false;
            if (!referenceMetadata(
                    reflectedProperty, reflection::ReferenceKind::Resource, resourceType, optional))
                return SceneInspectorWriteResult::Rejected;
        }
        const auto& current = currentValue.cast<const core::pmr::String&>();
        if (std::string_view(current.data(), current.size()) == stringValue)
            return SceneInspectorWriteResult::Unchanged;
        newValue = object(core::pmr::String(stringValue, &core::defaultCpuMemoryResource()));
    } else
        return SceneInspectorWriteResult::Rejected;

    reflectedProperty.set_value(reflectedDescriptor, newValue);
    return SceneInspectorWriteResult::Changed;
}

} // namespace

EditorSceneDocument::EditorSceneDocument(bool createDefaultScene)
    : audioSystem_(core::makeUnique<audio::AudioSystem>(
          audio::AudioSystemDesc{static_cast<std::uint32_t>(audio::kMaxAudioVoices), false})),
      scene_(core::makeUnique<scene::Scene>(
          core::pmr::String("EditorScene", &core::defaultCpuMemoryResource())))
{
    ensureSceneReflectionRegistered();
    scene_->setComponentFactory([this](scene::GameObject& owner, const core::pmr::String& type) {
        EditorSceneComponentFactoryContext context{owner,
            *audioSystem_,
            resourceManagerProvider_ ? resourceManagerProvider_() : nullptr,
            resourceManagerProvider_,
            physicsWorld2DProvider_ ? physicsWorld2DProvider_() : nullptr,
            schemaOnlyComponentCreation_,
            physicsWorld2DProvider_};
        return createEditorSceneComponent(std::string_view(type.data(), type.size()), context);
    });
    if (createDefaultScene) {
        const auto cameraId = createObject("Camera3D");
        auto* camera = scene_->findObject(cameraId);
        camera->addComponent<scene::CameraComponent>();
        camera->transform().setLocalTranslation({-2.0f, 0.0f, -1.0f});
        auto lightId = createObject("DirectionalLight");
        auto* light = scene_->findObject(lightId);
        light->addComponent<scene3d::LightComponent>();
        light->transform().setLocalTranslation({3.0f, 0.0f, -2.0f});
        selectedObject_ =
            scene_
                ->createObject(core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()))
                .id();
        auto* mesh = scene_->findObject(selectedObject_);
        mesh->addComponent<scene3d::MeshComponent>();
    } else {
        selectedObject_ = scene_->rootId();
    }
}

bool EditorSceneDocument::replaceWith(EditorSceneDocument&& candidate)
{
    if (!candidate.scene_ || !candidate.scene_->findObject(candidate.selectedObject_))
        return false;
    candidate.scene_->setComponentFactory(
        [this](scene::GameObject& owner, const core::pmr::String& type) {
            EditorSceneComponentFactoryContext context{owner,
                *audioSystem_,
                resourceManagerProvider_ ? resourceManagerProvider_() : nullptr,
                resourceManagerProvider_,
                physicsWorld2DProvider_ ? physicsWorld2DProvider_() : nullptr,
                schemaOnlyComponentCreation_,
                physicsWorld2DProvider_};
            return createEditorSceneComponent(std::string_view(type.data(), type.size()), context);
        });
    scene_.swap(candidate.scene_);
    audioSystem_.swap(candidate.audioSystem_);
    selectedObject_ = candidate.selectedObject_;
    return true;
}

void EditorSceneDocument::setResourceManagerProvider(
    core::SharedCallback<resource::ResourceManager*()> provider)
{
    resourceManagerProvider_ = std::move(provider);
}

void EditorSceneDocument::setPhysicsWorld2DProvider(
    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> provider)
{
    physicsWorld2DProvider_ = std::move(provider);
}

const std::array<SceneComponentCatalogEntry, kEditorSceneComponentCount>&
EditorSceneDocument::componentCatalog() noexcept
{
    return editorSceneComponentCatalog();
}

core::pmr::Vector<SceneHierarchyEntry> EditorSceneDocument::hierarchyEntries() const
{
    struct PendingEntry {
        scene::GameObjectId objectId;
        std::size_t depth;
    };

    core::pmr::Vector<SceneHierarchyEntry> result{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<PendingEntry> pending{&core::defaultCpuMemoryResource()};
    result.reserve(scene_->objects().size() + 1u);
    pending.reserve(scene_->objects().size() + 1u);
    pending.push_back({scene_->rootId(), 0u});
    while (!pending.empty()) {
        const auto entry = pending.back();
        pending.pop_back();
        const auto* object = scene_->findObject(entry.objectId);
        if (!object)
            continue;
        result.push_back({entry.objectId, entry.depth});
        const auto& children = object->childIds();
        for (auto child = children.rbegin(); child != children.rend(); ++child)
            pending.push_back({*child, entry.depth + 1u});
    }
    return result;
}

bool EditorSceneDocument::select(scene::GameObjectId id) noexcept
{
    if (!scene_->findObject(id))
        return false;
    selectedObject_ = id;
    return true;
}

scene::GameObjectId EditorSceneDocument::createObject(std::string_view name)
{
    if (name.empty() || name.size() > 128)
        return scene::GameObjectId::invalid();
    core::pmr::String owned(name, &core::defaultCpuMemoryResource());
    const auto id = scene_->createObject(std::move(owned)).id();
    selectedObject_ = id;
    return id;
}

bool EditorSceneDocument::reparentObject(
    scene::GameObjectId objectId, scene::GameObjectId parentId, std::size_t childIndex)
{
    return scene_->reparent(objectId, parentId, childIndex).ok();
}

bool EditorSceneDocument::destroySelected()
{
    for (const auto& object : scene_->objects()) {
        for (const auto& component : object->components()) {
            const auto* joint = dynamic_cast<const physics2d::Joint2DComponent*>(component.get());
            if (joint
                && (joint->bodyAObjectId() == selectedObject_.value()
                    || joint->bodyBObjectId() == selectedObject_.value()))
                return false;
        }
    }
    if (!selectedObject_.valid() || !scene_->destroyObject(selectedObject_).ok())
        return false;
    selectedObject_ = scene_->rootId();
    return true;
}

bool EditorSceneDocument::renameSelected(std::string_view name)
{
    if (name.empty() || name.size() > 128)
        return false;
    auto* object = scene_->findObject(selectedObject_);
    if (!object)
        return false;
    object->setName(core::pmr::String(name, &core::defaultCpuMemoryResource()));
    return true;
}

bool EditorSceneDocument::setSelectedTranslation(float x, float y, float z) noexcept
{
    auto* object = scene_->findObject(selectedObject_);
    if (!object)
        return false;
    auto transform = object->transform().localTransform();
    transform.translation = {x, y, z};
    object->transform().setLocalTransform(transform);
    return true;
}

bool EditorSceneDocument::setSelectedTransform(const scene::Transform& transform) noexcept
{
    auto* object = scene_->findObject(selectedObject_);
    if (!object)
        return false;
    object->transform().setLocalTransform(transform);
    return true;
}

scene::Component* EditorSceneDocument::addComponent(
    std::string_view type, scene::GameObjectId objectId)
{
    if (!objectId.valid())
        objectId = selectedObject_;
    auto* object = scene_->findObject(objectId);
    if (!object || type.empty() || type.size() > 64)
        return nullptr;
    core::pmr::String owned(type, &core::defaultCpuMemoryResource());
    return object->create_component(owned);
}

bool EditorSceneDocument::removeComponent(
    scene::GameObjectId objectId, scene::ComponentId componentId)
{
    auto* object = scene_->findObject(objectId);
    if (!object || !componentId.valid())
        return false;
    const auto* target = object->findComponent(componentId);
    if (!target)
        return false;
    if (dynamic_cast<const physics2d::RigidBody2DComponent*>(target)) {
        for (const auto& sceneObject : scene_->objects()) {
            for (const auto& component : sceneObject->components()) {
                const auto* joint =
                    dynamic_cast<const physics2d::Joint2DComponent*>(component.get());
                if (joint
                    && (joint->bodyAObjectId() == objectId.value()
                        || joint->bodyBObjectId() == objectId.value()))
                    return false;
            }
        }
    }
    return object->removeComponent(componentId);
}

bool EditorSceneDocument::removeLastSelectedComponent()
{
    auto* object = scene_->findObject(selectedObject_);
    if (!object)
        return false;
    const auto& components = object->components();
    for (auto it = components.rbegin(); it != components.rend(); ++it)
        if ((*it)->scriptTypeName() != std::string_view("TransformComponent"))
            return removeComponent(object->id(), (*it)->id());
    return false;
}

core::pmr::Vector<SceneInspectorProperty> EditorSceneDocument::inspectorProperties(
    scene::GameObjectId objectId) const
{
    core::pmr::Vector<SceneInspectorProperty> result{&core::defaultCpuMemoryResource()};
    const auto* object = scene_->findObject(objectId);
    if (!object)
        return result;
    for (const auto& component : object->components()) {
        const bool requiredTransform =
            std::string_view(component->scriptTypeName()) == "TransformComponent";
        if (!requiredTransform) {
            const reflection::object commonProperties = component->editorProperties();
            appendReflectedProperties(result, objectId.value(), *component, commonProperties);
        }
        appendReflectedProperties(
            result, objectId.value(), *component, component->nativeReflectionObject());
        appendReflectedProperties(result,
            objectId.value(),
            *component,
            component->nativeReflectionObject(),
            {},
            PropertyHint::Runtime,
            true);
        const reflection::object scriptInstance = component->scriptInstance();
        if (!scriptInstance.is_undefined()) {
            appendReflectedProperties(result,
                objectId.value(),
                *component,
                scriptInstance,
                {},
                PropertyHint::Editor,
                true);
        }
        appendReflectedProperties(
            result, objectId.value(), *component, component->editorAuthoringProperties());
    }
    return result;
}

SceneInspectorWriteResult EditorSceneDocument::setComponentProperty(scene::GameObjectId objectId,
    scene::ComponentId componentId,
    std::string_view propertyName,
    SceneInspectorPropertyKind valueKind,
    bool booleanValue,
    int integerValue,
    std::uint64_t unsignedIntegerValue,
    double numberValue,
    core::Vec3 vector3Value,
    core::Vec4 color4Value,
    std::string_view stringValue,
    core::Vec2 vector2Value,
    const core::pmr::Vector<core::Vec2>& vector2ListValue,
    std::uint64_t sceneObjectReferenceValue,
    const core::pmr::Vector<SceneInspectorObjectLink>& sceneObjectLinkListValue)
{
    auto* object = scene_->findObject(objectId);
    if (!object || propertyName.empty() || propertyName.size() > 128)
        return SceneInspectorWriteResult::Rejected;
    auto* component = object->findComponent(componentId);
    if (!component)
        return SceneInspectorWriteResult::Rejected;
    if (std::string_view(component->scriptTypeName()) == "TransformComponent")
        return SceneInspectorWriteResult::Rejected;
    auto commonProperties = component->editorProperties();
    const auto componentWrite = writeReflectedObjectProperty(*component,
        commonProperties,
        propertyName,
        valueKind,
        booleanValue,
        integerValue,
        unsignedIntegerValue,
        numberValue,
        vector3Value,
        color4Value,
        stringValue,
        vector2Value,
        vector2ListValue,
        sceneObjectReferenceValue,
        sceneObjectLinkListValue);
    if (componentWrite != SceneInspectorWriteResult::Rejected) {
        if (componentWrite == SceneInspectorWriteResult::Changed
            && !component->applyEditorProperties(commonProperties))
            return SceneInspectorWriteResult::Rejected;
        return componentWrite;
    }
    auto reflectedComponent = component->nativeReflectionObject();
    const auto reflectedWrite = writeReflectedObjectProperty(*component,
        reflectedComponent,
        propertyName,
        valueKind,
        booleanValue,
        integerValue,
        unsignedIntegerValue,
        numberValue,
        vector3Value,
        color4Value,
        stringValue,
        vector2Value,
        vector2ListValue,
        sceneObjectReferenceValue,
        sceneObjectLinkListValue);
    if (reflectedWrite != SceneInspectorWriteResult::Rejected)
        return reflectedWrite;
    auto authoringProperties = component->editorAuthoringProperties();
    const auto authoringWrite = writeReflectedObjectProperty(*component,
        authoringProperties,
        propertyName,
        valueKind,
        booleanValue,
        integerValue,
        unsignedIntegerValue,
        numberValue,
        vector3Value,
        color4Value,
        stringValue,
        vector2Value,
        vector2ListValue,
        sceneObjectReferenceValue,
        sceneObjectLinkListValue);
    if (authoringWrite == SceneInspectorWriteResult::Changed
        && !component->applyEditorAuthoringProperties(authoringProperties)) {
        return SceneInspectorWriteResult::Rejected;
    }
    return authoringWrite;
}

} // namespace arp::editor
