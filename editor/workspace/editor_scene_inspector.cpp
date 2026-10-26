#include "editor/workspace/editor_scene_inspector.h"

#include "editor/workspace/editor_scene_document.h"
#include "resource/resource_manager.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace arp::editor {
bool EditorSceneObjectReferenceChoices::matchesTarget(
    const scene::GameObject& object, std::string_view type) noexcept
{
    return type == "*"
        || (!type.empty()
            && std::any_of(
                object.components().begin(), object.components().end(), [&](const auto& c) {
                    return type == c->scriptTypeName();
                }));
}

EditorSceneObjectReferenceChoices::TargetStatus EditorSceneObjectReferenceChoices::targetStatus(
    const scene::Scene& scene, const SceneInspectorProperty& property) noexcept
{
    if (property.kind != SceneInspectorPropertyKind::SceneObjectReference
        || property.sceneObjectType.empty())
        return TargetStatus::InvalidMetadata;
    if (!property.sceneObjectReferenceValue)
        return property.resourceOptional ? TargetStatus::None : TargetStatus::Unassigned;
    const auto* target =
        scene.findObject(scene::GameObjectId::fromValue(property.sceneObjectReferenceValue));
    if (!target)
        return TargetStatus::Missing;
    return matchesTarget(*target, property.sceneObjectType) ? TargetStatus::Valid
                                                            : TargetStatus::WrongType;
}

SceneInspectorProperty EditorSceneObjectReferenceChoices::build(
    const scene::Scene& scene, const SceneInspectorProperty& source)
{
    auto property = source;
    property.enumOptions.clear();
    property.resourceDiagnostic.clear();
    property.integerValue = -1;
    const auto append = [&](std::string_view label, std::uint64_t objectId) {
        auto& option = property.enumOptions.emplace_back();
        option.name = label;
        char value[32];
        std::snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(objectId));
        option.commandValue = value;
        option.value = static_cast<int>(property.enumOptions.size() - 1u);
        if (objectId == property.sceneObjectReferenceValue)
            property.integerValue = option.value;
    };
    if (targetStatus(scene, property) == TargetStatus::InvalidMetadata) {
        append("<Invalid reference metadata>", property.sceneObjectReferenceValue);
        property.resourceDiagnostic = "EDITOR-INSPECTOR-REFERENCE-METADATA";
        property.editable = false;
        return property;
    }
    if (property.resourceOptional || property.sceneObjectReferenceValue == 0) {
        append(property.resourceOptional ? "<None>" : "<Unassigned>", 0);
        if (!property.resourceOptional)
            property.resourceDiagnostic = "Target is required";
    }
    for (const auto& candidate : scene.objects()) {
        if (matchesTarget(*candidate, property.sceneObjectType))
            append(candidate->name(), candidate->id().value());
    }
    if (property.integerValue == -1) {
        const auto* target =
            scene.findObject(scene::GameObjectId::fromValue(property.sceneObjectReferenceValue));
        char label[64];
        std::snprintf(label,
            sizeof(label),
            target ? "<Wrong type: %llu>" : "<Missing: %llu>",
            static_cast<unsigned long long>(property.sceneObjectReferenceValue));
        append(label, property.sceneObjectReferenceValue);
        property.resourceDiagnostic = target ? "Wrong type: " : "Missing object: ";
        property.resourceDiagnostic +=
            target ? property.sceneObjectType : property.enumOptions.back().commandValue;
    }
    return property;
}

SceneInspectorProperty EditorSceneResourceReferenceChoices::build(
    const resource::ResourceManager* resources, const SceneInspectorProperty& source)
{
    auto property = source;
    property.enumOptions.clear();
    property.resourceDiagnostic.clear();
    const bool invalidId =
        !property.stringValue.empty() && !resource::isResourceUuid(property.stringValue);
    if (invalidId)
        property.resourceDiagnostic = "Invalid resource ID: expected UUID";
    property.integerValue = -1;
    const auto append = [&](std::string_view label, std::string_view id) {
        auto& option = property.enumOptions.emplace_back();
        option.name = label;
        option.commandValue = id;
        option.value = static_cast<int>(property.enumOptions.size() - 1u);
        if (id == property.stringValue)
            property.integerValue = option.value;
    };
    if (property.resourceOptional || property.stringValue.empty()) {
        append(property.resourceOptional ? "<None>" : "<Unassigned>", {});
        if (!property.resourceOptional)
            property.resourceDiagnostic = "Resource is required";
    }
    if (!resources) {
        property.editable = false;
        property.resourceDiagnostic = "EDITOR-RESOURCE-MANAGER-UNAVAILABLE";
    } else {
        const auto snapshot = resources->snapshotReferences(property.resourceType, maxEntries);
        if (!snapshot.success) {
            property.editable = false;
            property.resourceDiagnostic = snapshot.diagnostics;
        } else {
            for (const auto& entry : snapshot.entries)
                if (entry.state == resource::ResourceState::Ready)
                    append(entry.reference.id, entry.reference.id);
        }
    }
    if (property.integerValue == -1) {
        // The exact record is read only to diagnose the current ID. It is never
        // retained and cannot bypass a rejected/over-budget candidate snapshot.
        const auto* record = resources ? resources->recordSnapshot(property.stringValue) : nullptr;
        const char* label = "Unavailable";
        if (property.resourceDiagnostic.empty()) {
            if (!record) {
                label = "Missing";
                property.resourceDiagnostic = "Missing resource: ";
                property.resourceDiagnostic += property.stringValue;
            } else if (record->type != property.resourceType) {
                label = "Wrong type";
                property.resourceDiagnostic = "Wrong type: ";
                property.resourceDiagnostic += property.resourceType;
            } else {
                label = "Not Ready";
                property.resourceDiagnostic = "Resource is not Ready: ";
                property.resourceDiagnostic += property.stringValue;
            }
        }
        core::pmr::String name("<");
        name += label;
        name += ": ";
        name += property.stringValue;
        name += ">";
        append(invalidId ? std::string_view("<Invalid resource ID>") : std::string_view(name),
            property.stringValue);
    }
    return property;
}

std::array<std::string_view, 2> EditorSceneResourceReferenceChoices::identifierLines(
    std::string_view id) noexcept
{
    if (id.empty())
        return {};
    if (!resource::isResourceUuid(id))
        return {"Invalid resource ID", {}};
    // This is the UUID presentation contract, not a guessed font-width wrap.
    constexpr std::size_t prefixGroups = 3;
    std::size_t delimiter = 0;
    for (std::size_t group = 0; group < prefixGroups; ++group)
        delimiter = id.find('-', group == 0 ? 0 : delimiter + 1);
    return {id.substr(0, delimiter), id.substr(delimiter)};
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
        std::snprintf(buffer,
            sizeof(buffer),
            "%llu",
            static_cast<unsigned long long>(property.unsignedIntegerValue));
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
        for (const auto& option : property.enumOptions)
            if (option.value == property.integerValue)
                return option.name;
        return property.stringValue.empty()
            ? text(property.resourceOptional ? "<None>" : "<Unassigned>")
            : property.stringValue;
    case SceneInspectorPropertyKind::SceneObjectReference:
        for (const auto& option : property.enumOptions)
            if (option.value == property.integerValue)
                return option.name;
        if (property.sceneObjectReferenceValue == 0)
            return text(property.resourceOptional ? "<None>" : "<Unassigned>");
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
            property = EditorSceneObjectReferenceChoices::build(document.scene(), property);
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
        if (property.kind == SceneInspectorPropertyKind::ResourceReference)
            property = EditorSceneResourceReferenceChoices::build(resources, property);
    }
    return properties;
}
} // namespace arp::editor
