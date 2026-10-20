#include "editor/workspace/editor_scene_inspector.h"

#include "editor/workspace/editor_scene_document.h"
#include "resource/resource_manager.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace arp::editor {
namespace {
constexpr std::size_t kMaxInspectorResourceChoices = 32u;
}

core::pmr::String scenePropertyControlSource(const SceneInspectorProperty& property)
{
    core::pmr::String source("scene_property_", &core::defaultCpuMemoryResource());
    const auto objectId = std::to_string(property.objectId);
    const auto componentId = std::to_string(property.componentId);
    source.append(objectId.data(), objectId.size());
    source.push_back('_');
    source.append(componentId.data(), componentId.size());
    source.push_back('_');
    source += property.name;
    return source;
}

core::pmr::String sceneInspectorDisplayValue(const SceneInspectorProperty& property)
{
    char buffer[160];
    const auto text = [](std::string_view value) {
        return core::pmr::String(value, &core::defaultCpuMemoryResource());
    };
    switch (property.kind) {
    case SceneInspectorPropertyKind::Boolean:
        return text(property.booleanValue ? "true" : "false");
    case SceneInspectorPropertyKind::Integer:
        std::snprintf(buffer, sizeof(buffer), "%d", property.integerValue);
        return buffer;
    case SceneInspectorPropertyKind::UnsignedInteger:
        std::snprintf(buffer, sizeof(buffer), "%u", property.unsignedIntegerValue);
        return buffer;
    case SceneInspectorPropertyKind::Number:
        std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(property.numberValue));
        return buffer;
    case SceneInspectorPropertyKind::Enumeration:
        for (const auto& option : property.enumOptions)
            if (option.value == property.integerValue)
                return option.name;
        std::snprintf(buffer, sizeof(buffer), "%d", property.integerValue);
        return buffer;
    case SceneInspectorPropertyKind::Vector2:
        std::snprintf(buffer,
            sizeof(buffer),
            "(%.6g, %.6g)",
            static_cast<double>(property.vector2Value.x),
            static_cast<double>(property.vector2Value.y));
        return buffer;
    case SceneInspectorPropertyKind::Vector3:
        std::snprintf(buffer,
            sizeof(buffer),
            "(%.6g, %.6g, %.6g)",
            static_cast<double>(property.vector3Value.x),
            static_cast<double>(property.vector3Value.y),
            static_cast<double>(property.vector3Value.z));
        return buffer;
    case SceneInspectorPropertyKind::Color4:
        std::snprintf(buffer,
            sizeof(buffer),
            "(%.4g, %.4g, %.4g, %.4g)",
            static_cast<double>(property.color4Value.x),
            static_cast<double>(property.color4Value.y),
            static_cast<double>(property.color4Value.z),
            static_cast<double>(property.color4Value.w));
        return buffer;
    case SceneInspectorPropertyKind::String:
        return property.stringValue;
    case SceneInspectorPropertyKind::ResourceReference:
        return property.stringValue.empty() ? "<unassigned>" : property.stringValue;
    case SceneInspectorPropertyKind::SceneObjectReference:
        if (property.sceneObjectReferenceValue == 0)
            return text("<none>");
        std::snprintf(buffer,
            sizeof(buffer),
            "%llu",
            static_cast<unsigned long long>(property.sceneObjectReferenceValue));
        return buffer;
    case SceneInspectorPropertyKind::Vector2List:
        std::snprintf(buffer, sizeof(buffer), "[%zu points]", property.vector2ListValue.size());
        return buffer;
    case SceneInspectorPropertyKind::SceneObjectLinkList:
        std::snprintf(
            buffer, sizeof(buffer), "[%zu links]", property.sceneObjectLinkListValue.size());
        return buffer;
    case SceneInspectorPropertyKind::Unsupported:
        return property.typeName.empty() ? "unsupported type" : property.typeName;
    }
    return "unsupported type";
}

core::pmr::Vector<SceneInspectorProperty> buildSceneInspectorProperties(
    const EditorSceneDocument& document,
    resource::ResourceManager* resources,
    scene::GameObjectId objectId)
{
    auto properties = document.inspectorProperties(objectId);
    for (auto& property : properties) {
        if (property.kind == SceneInspectorPropertyKind::SceneObjectReference) {
            std::uint32_t selectedOption = 0;
            for (const auto& candidate : document.scene().objects()) {
                const bool hasTargetType = std::any_of(candidate->components().begin(),
                    candidate->components().end(),
                    [&](const auto& component) {
                        return property.sceneObjectType == component->scriptTypeName();
                    });
                if (!hasTargetType)
                    continue;
                property.enumOptions.emplace_back();
                auto& option = property.enumOptions.back();
                option.name = candidate->name();
                option.commandValue = std::to_string(candidate->id().value());
                option.value = static_cast<int>(property.enumOptions.size() - 1u);
                if (candidate->id().value() == property.sceneObjectReferenceValue)
                    selectedOption = static_cast<std::uint32_t>(property.enumOptions.size() - 1u);
            }
            if (property.resourceOptional) {
                property.enumOptions.insert(property.enumOptions.begin(), {});
                property.enumOptions.front().name = "<None>";
                property.enumOptions.front().commandValue = "0";
                property.enumOptions.front().value = 0;
                for (std::size_t index = 1; index < property.enumOptions.size(); ++index)
                    property.enumOptions[index].value = static_cast<int>(index);
                if (property.sceneObjectReferenceValue == 0)
                    selectedOption = 0;
                else
                    ++selectedOption;
            }
            property.integerValue = static_cast<int>(selectedOption);
            if (property.enumOptions.empty()) {
                property.editable = false;
                property.resourceDiagnostic = "No scene object matches the required component type";
            }
            continue;
        }
        if (property.kind == SceneInspectorPropertyKind::SceneObjectLinkList) {
            std::uint32_t selectedOption = 0;
            for (const auto& candidate : document.scene().objects()) {
                property.enumOptions.emplace_back();
                auto& option = property.enumOptions.back();
                option.name = candidate->name();
                option.commandValue = std::to_string(candidate->id().value());
                option.value = static_cast<int>(property.enumOptions.size() - 1u);
                if (!property.sceneObjectLinkListValue.empty()
                    && candidate->id().value()
                        == property.sceneObjectLinkListValue.front().targetObjectId)
                    selectedOption = static_cast<std::uint32_t>(property.enumOptions.size() - 1u);
            }
            property.integerValue = static_cast<int>(selectedOption);
            continue;
        }
        if (property.kind != SceneInspectorPropertyKind::ResourceReference)
            continue;

        if (property.resourceOptional) {
            property.enumOptions.emplace_back();
            property.enumOptions.back().name = "<None>";
            property.enumOptions.back().commandValue.clear();
            property.enumOptions.back().value = 0;
        }
        std::uint32_t selectedOption = 0;
        if (!resources) {
            property.editable = false;
            property.resourceDiagnostic = "EDITOR-RESOURCE-MANAGER-UNAVAILABLE";
            continue;
        }
        const auto snapshot = resources->snapshotReferences(
            std::string_view(property.resourceType.data(), property.resourceType.size()),
            kMaxInspectorResourceChoices);
        if (!snapshot.success) {
            property.editable = false;
            property.resourceDiagnostic = snapshot.diagnostics;
        } else {
            bool selectedReferenceFound = property.stringValue.empty();
            for (const auto& entry : snapshot.entries) {
                if (entry.state != resource::ResourceState::Ready)
                    continue;
                property.enumOptions.emplace_back();
                auto& option = property.enumOptions.back();
                option.commandValue = entry.reference.id;
                option.value = static_cast<int>(property.enumOptions.size() - 1u);
                option.name = entry.reference.id;
                if (entry.reference.id == property.stringValue) {
                    selectedOption = static_cast<std::uint32_t>(property.enumOptions.size() - 1u);
                    selectedReferenceFound = true;
                }
            }
            if (!property.stringValue.empty() && !selectedReferenceFound) {
                property.enumOptions.emplace_back();
                auto& unavailable = property.enumOptions.back();
                unavailable.name = "<Unavailable: ";
                unavailable.name += property.stringValue;
                unavailable.name += ">";
                unavailable.commandValue = property.stringValue;
                unavailable.value = static_cast<int>(property.enumOptions.size() - 1u);
                selectedOption = static_cast<std::uint32_t>(property.enumOptions.size() - 1u);
                property.resourceDiagnostic =
                    "Current resource is missing, not Ready, or has the wrong type";
            }
        }
        property.integerValue = static_cast<int>(selectedOption);
    }
    return properties;
}
} // namespace arp::editor
