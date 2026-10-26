#include "editor/workspace/editor_scene_reference_focus.h"

#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_inspector_selection.h"

#include <new>
#include <type_traits>

namespace arp::editor {
const char* EditorSceneReferenceSource::prepare(const EditorSceneDocument& document,
    std::uint64_t objectId,
    std::uint64_t componentId,
    std::string_view path,
    std::optional<SceneInspectorProperty>& destination) noexcept
{
    using Profile = EditorSceneInspectorSelection;
    const auto* object = document.scene().findObject(scene::GameObjectId::fromValue(objectId));
    if (!object || !object->findComponent(scene::ComponentId::fromValue(componentId)))
        return "EDITOR-REFERENCE-FOCUS-SOURCE-MISSING";
    if (path.empty() || path.size() > Profile::editorBudget.maxStringBytes)
        return "EDITOR-REFERENCE-FOCUS-PATH-INVALID";
    if (object->components().size() > Profile::editorBudget.maxComponentsPerObject)
        return "EDITOR-REFERENCE-FOCUS-BUDGET";
    try {
        auto quota = core::makeShared<core::CpuMemoryQuota>(Profile::editorObservationBytes);
        core::CpuMemoryQuotaScope scope(quota);
        const auto properties = document.inspectorProperties(object->id());
        if (properties.size() > Profile::editorBudget.maxPropertiesPerObject)
            return "EDITOR-REFERENCE-FOCUS-BUDGET";
        const SceneInspectorProperty* field = nullptr;
        for (const auto& property : properties) {
            if (property.componentId != componentId || property.name != path)
                continue;
            if (field)
                return "EDITOR-REFERENCE-FOCUS-PATH-AMBIGUOUS";
            field = &property;
        }
        if (!field)
            return "EDITOR-REFERENCE-FOCUS-FIELD-MISSING";
        auto copy = *field;
        static_assert(std::is_nothrow_move_constructible_v<SceneInspectorProperty>);
        destination.emplace(std::move(copy));
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-REFERENCE-FOCUS-ALLOCATION-FAILED";
    }
}

const char* EditorSceneReferenceFocus::prepare(const EditorSceneDocument& document,
    std::uint64_t objectId,
    std::uint64_t componentId,
    std::string_view path,
    std::uint64_t& target) noexcept
{
    std::optional<SceneInspectorProperty> field;
    if (const auto* issue =
            EditorSceneReferenceSource::prepare(document, objectId, componentId, path, field))
        return issue;
    using Choices = EditorSceneObjectReferenceChoices;
    switch (Choices::targetStatus(document.scene(), *field)) {
    case Choices::TargetStatus::Valid:
        target = field->sceneObjectReferenceValue;
        return nullptr;
    case Choices::TargetStatus::None:
        return "EDITOR-REFERENCE-FOCUS-NONE";
    case Choices::TargetStatus::Unassigned:
        return "EDITOR-REFERENCE-FOCUS-UNASSIGNED";
    case Choices::TargetStatus::Missing:
        return "EDITOR-REFERENCE-FOCUS-TARGET-MISSING";
    case Choices::TargetStatus::WrongType:
        return "EDITOR-REFERENCE-FOCUS-TARGET-TYPE";
    case Choices::TargetStatus::InvalidMetadata:
        return "EDITOR-REFERENCE-FOCUS-METADATA";
    }
    return "EDITOR-REFERENCE-FOCUS-METADATA";
}
} // namespace arp::editor
