#include "editor/workspace/editor_scene_inspector_selection.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace arp::editor {
bool sceneInspectorMetadataEqual(const SceneInspectorProperty& a, const SceneInspectorProperty& b)
{
    if (a.kind != b.kind || a.typeName != b.typeName || a.editable != b.editable
        || a.serializable != b.serializable || a.resourceType != b.resourceType
        || a.sceneObjectType != b.sceneObjectType || a.resourceOptional != b.resourceOptional)
        return false;
    if (a.kind != SceneInspectorPropertyKind::Enumeration)
        return true;
    if (a.enumOptions.size() != b.enumOptions.size())
        return false;
    // Reflection enumeration order is not a component identity or a constraint.
    return std::all_of(a.enumOptions.begin(), a.enumOptions.end(), [&](const auto& option) {
        return std::any_of(b.enumOptions.begin(), b.enumOptions.end(), [&](const auto& other) {
            return option.value == other.value && option.name == other.name
                && option.commandValue == other.commandValue;
        });
    });
}

bool sceneInspectorValuesEqual(const SceneInspectorProperty& a, const SceneInspectorProperty& b)
{
    switch (a.kind) {
    case SceneInspectorPropertyKind::Boolean:
        return a.booleanValue == b.booleanValue;
    case SceneInspectorPropertyKind::Integer:
    case SceneInspectorPropertyKind::Enumeration:
        return a.integerValue == b.integerValue;
    case SceneInspectorPropertyKind::UnsignedInteger:
        return a.unsignedIntegerValue == b.unsignedIntegerValue;
    case SceneInspectorPropertyKind::Number:
        return a.numberValue == b.numberValue;
    case SceneInspectorPropertyKind::Vector2:
        return a.vector2Value.x == b.vector2Value.x && a.vector2Value.y == b.vector2Value.y;
    case SceneInspectorPropertyKind::Vector3:
        return a.vector3Value.x == b.vector3Value.x && a.vector3Value.y == b.vector3Value.y
            && a.vector3Value.z == b.vector3Value.z;
    case SceneInspectorPropertyKind::Color4:
        return a.color4Value.x == b.color4Value.x && a.color4Value.y == b.color4Value.y
            && a.color4Value.z == b.color4Value.z && a.color4Value.w == b.color4Value.w;
    case SceneInspectorPropertyKind::String:
    case SceneInspectorPropertyKind::ResourceReference:
        return a.stringValue == b.stringValue;
    case SceneInspectorPropertyKind::SceneObjectReference:
        return a.sceneObjectReferenceValue == b.sceneObjectReferenceValue;
    case SceneInspectorPropertyKind::Vector2List:
        return a.vector2ListValue.size() == b.vector2ListValue.size()
            && std::equal(a.vector2ListValue.begin(),
                a.vector2ListValue.end(),
                b.vector2ListValue.begin(),
                [](const auto& x, const auto& y) {
                    return x.x == y.x && x.y == y.y;
                });
    case SceneInspectorPropertyKind::SceneObjectLinkList:
        return a.sceneObjectLinkListValue.size() == b.sceneObjectLinkListValue.size()
            && std::equal(a.sceneObjectLinkListValue.begin(),
                a.sceneObjectLinkListValue.end(),
                b.sceneObjectLinkListValue.begin(),
                [](const auto& x, const auto& y) {
                    return x.name == y.name && x.targetObjectId == y.targetObjectId;
                });
    case SceneInspectorPropertyKind::Unsupported:
        return false;
    }
    return false;
}

namespace {
using Selection = EditorSceneInspectorSelection;
using Kind = SceneInspectorPropertyKind;

const char* validate(const Selection::Object& object, Selection::Budget budget)
{
    const auto& source = object.inspector;
    if (!source.objectId || object.components.size() > budget.maxComponentsPerObject
        || source.properties.size() > budget.maxPropertiesPerObject
        || source.objectName.size() > budget.maxStringBytes)
        return "EDITOR-INSPECTOR-SELECTION-OBJECT-INVALID";
    for (std::size_t i = 0; i < object.components.size(); ++i) {
        const auto& component = object.components[i];
        if (!component.id || component.type.empty()
            || component.type.size() > budget.maxStringBytes)
            return "EDITOR-INSPECTOR-SELECTION-COMPONENT-INVALID";
        for (std::size_t j = 0; j < i; ++j) {
            if (object.components[j].type == component.type)
                return "EDITOR-INSPECTOR-SELECTION-COMPONENT-AMBIGUOUS";
            if (object.components[j].id == component.id)
                return "EDITOR-INSPECTOR-SELECTION-COMPONENT-INVALID";
        }
    }
    for (std::size_t i = 0; i < source.properties.size(); ++i) {
        const auto& property = source.properties[i];
        if (static_cast<unsigned>(property.kind) > static_cast<unsigned>(Kind::SceneObjectReference)
            || property.objectId != source.objectId || property.name.empty()
            || !std::any_of(object.components.begin(), object.components.end(), [&](const auto& c) {
                   return c.id == property.componentId && c.type == property.componentType;
               }))
            return "EDITOR-INSPECTOR-SELECTION-PROPERTY-INVALID";
        for (std::size_t j = 0; j < i; ++j)
            if (source.properties[j].componentId == property.componentId
                && source.properties[j].name == property.name)
                return "EDITOR-INSPECTOR-SELECTION-PATH-AMBIGUOUS";
        for (const auto* text : {&property.componentType,
                 &property.name,
                 &property.typeName,
                 &property.resourceType,
                 &property.sceneObjectType,
                 &property.resourceDiagnostic,
                 &property.stringValue})
            if (text->size() > budget.maxStringBytes)
                return "EDITOR-INSPECTOR-SELECTION-TEXT-BUDGET";
        if (property.enumOptions.size() > budget.maxElementsPerValue
            || property.vector2ListValue.size() > budget.maxElementsPerValue
            || property.sceneObjectLinkListValue.size() > budget.maxElementsPerValue)
            return "EDITOR-INSPECTOR-SELECTION-VALUE-BUDGET";
        for (const auto& link : property.sceneObjectLinkListValue)
            if (link.name.size() > budget.maxStringBytes)
                return "EDITOR-INSPECTOR-SELECTION-TEXT-BUDGET";
        for (std::size_t option = 0; option < property.enumOptions.size(); ++option) {
            const auto& value = property.enumOptions[option];
            if (value.name.size() > budget.maxStringBytes
                || value.commandValue.size() > budget.maxStringBytes)
                return "EDITOR-INSPECTOR-SELECTION-TEXT-BUDGET";
            for (std::size_t previous = 0; previous < option; ++previous)
                if (property.enumOptions[previous].name == value.name)
                    return "EDITOR-INSPECTOR-SELECTION-ENUM-AMBIGUOUS";
        }
        if (property.kind == Kind::Enumeration
            && !std::any_of(
                property.enumOptions.begin(), property.enumOptions.end(), [&](const auto& option) {
                    return option.value == property.integerValue;
                }))
            return "EDITOR-INSPECTOR-SELECTION-ENUM-INVALID";
        // Invalid numeric observations cannot be described as a coherent mixed value.
        const auto finite2 = [](core::Vec2 v) {
            return std::isfinite(v.x) && std::isfinite(v.y);
        };
        const auto finite3 = [](core::Vec3 v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if ((property.kind == Kind::Number && !std::isfinite(property.numberValue))
            || (property.kind == Kind::Vector2 && !finite2(property.vector2Value))
            || (property.kind == Kind::Vector3 && !finite3(property.vector3Value))
            || (property.kind == Kind::Color4
                && (!std::isfinite(property.color4Value.x) || !std::isfinite(property.color4Value.y)
                    || !std::isfinite(property.color4Value.z)
                    || !std::isfinite(property.color4Value.w)))
            || (property.kind == Kind::Vector2List
                && !std::all_of(
                    property.vector2ListValue.begin(), property.vector2ListValue.end(), finite2)))
            return "EDITOR-INSPECTOR-SELECTION-NONFINITE";
    }
    return nullptr;
}
} // namespace

const char* EditorSceneInspectorSelection::prepare(
    core::ArrayView<Object> objects, Budget budget, Snapshot& output) noexcept
{
    if (!budget.maxObjects || !budget.maxComponentsPerObject || !budget.maxPropertiesPerObject
        || !budget.maxFields || !budget.maxStringBytes || !budget.maxElementsPerValue
        || objects.empty() || !objects.data() || objects.size() > budget.maxObjects)
        return "EDITOR-INSPECTOR-SELECTION-BUDGET";
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (const auto* diagnostic = validate(objects[i], budget))
            return diagnostic;
        for (std::size_t j = 0; j < i; ++j)
            if (objects[i].inspector.objectId == objects[j].inspector.objectId)
                return "EDITOR-INSPECTOR-SELECTION-OBJECT-DUPLICATE";
    }
    try {
        Snapshot candidate;
        candidate.selectedCount = objects.size();
        candidate.objects.assign(objects.begin(), objects.end());
        for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
            const auto& properties = objects[objectIndex].inspector.properties;
            for (std::size_t propertyIndex = 0; propertyIndex < properties.size();
                ++propertyIndex) {
                const auto& property = properties[propertyIndex];
                auto field = std::find_if(
                    candidate.fields.begin(), candidate.fields.end(), [&](const auto& f) {
                        return f.componentType == property.componentType && f.path == property.name;
                    });
                if (field == candidate.fields.end()) {
                    if (candidate.fields.size() == budget.maxFields)
                        return "EDITOR-INSPECTOR-SELECTION-FIELD-BUDGET";
                    candidate.fields.emplace_back();
                    field = candidate.fields.end() - 1;
                    field->componentType = property.componentType;
                    field->path = property.name;
                }
                field->members.push_back({objectIndex, propertyIndex});
            }
        }
        for (auto& field : candidate.fields) {
            if (field.members.size() != objects.size()) {
                field.status = Status::Missing;
                continue;
            }
            const auto first = field.members.front();
            const auto& value =
                objects[first.objectIndex].inspector.properties[first.propertyIndex];
            if (value.kind == Kind::Unsupported) {
                field.status = Status::Unsupported;
                continue;
            }
            for (const auto member : field.members) {
                const auto& other =
                    objects[member.objectIndex].inspector.properties[member.propertyIndex];
                if (!sceneInspectorMetadataEqual(value, other)) {
                    field.status = Status::Incompatible;
                    field.mixed = false;
                    break;
                }
                field.mixed = field.mixed || !sceneInspectorValuesEqual(value, other);
            }
        }
        output.swap(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-INSPECTOR-SELECTION-ALLOCATION-FAILED";
    }
}
} // namespace arp::editor
