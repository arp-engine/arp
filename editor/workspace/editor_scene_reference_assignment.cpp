#include "editor/workspace/editor_scene_reference_assignment.h"

#include "editor/project/project_resource_locations.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_reference_focus.h"

#include <new>
#include <optional>
#include <utility>

namespace arp::editor {
void EditorSceneReferenceAssignment::Prepared::swap(Prepared& other) noexcept
{
    std::swap(kind, other.kind);
    std::swap(objectId, other.objectId);
    resourceId.swap(other.resourceId);
    std::swap(changed, other.changed);
}

const char* EditorSceneReferenceAssignment::prepare(const EditorSceneDocument& document,
    const resource::ResourceManager& resources,
    const ProjectResourceLocations& locations,
    const Context& context,
    const Intent& intent,
    Prepared& destination) noexcept
{
    if (!intent.documentEpoch || intent.documentEpoch != context.documentEpoch
        || !intent.resourceRevision || intent.resourceRevision != context.resourceRevision
        || !context.sceneOwner.sameCurrentEpoch(document.ownerTicket()))
        return "EDITOR-REFERENCE-ASSIGNMENT-STALE";
    if (context.selectedCount != 1 || !intent.objectId
        || context.selectedObjectId != intent.objectId
        || document.selectedObject().value() != intent.objectId)
        return "EDITOR-REFERENCE-ASSIGNMENT-SELECTION";
    if (intent.property.empty()
        || intent.property.size() > EditorSceneDocument::maxPropertyNameBytes)
        return "EDITOR-REFERENCE-ASSIGNMENT-PATH";
    try {
        std::optional<SceneInspectorProperty> field;
        if (const auto* issue = EditorSceneReferenceSource::prepare(
                document, intent.objectId, intent.componentId, intent.property, field))
            return issue;
        if (!field->editable)
            return "EDITOR-REFERENCE-ASSIGNMENT-READONLY";
        Prepared candidate;
        candidate.kind = field->kind;
        switch (intent.sourceKind) {
        case SourceKind::Object:
            if (!intent.sourceObjectId || !intent.sourceResourceId.empty()
                || field->kind != SceneInspectorPropertyKind::SceneObjectReference)
                return "EDITOR-REFERENCE-ASSIGNMENT-SOURCE";
            candidate.objectId = intent.sourceObjectId;
            candidate.changed = field->sceneObjectReferenceValue != intent.sourceObjectId;
            field->sceneObjectReferenceValue = intent.sourceObjectId;
            if (EditorSceneObjectReferenceChoices::targetStatus(document.scene(), *field)
                != EditorSceneObjectReferenceChoices::TargetStatus::Valid)
                return "EDITOR-REFERENCE-ASSIGNMENT-OBJECT";
            break;
        case SourceKind::Resource: {
            if (intent.sourceObjectId || !resource::isResourceUuid(intent.sourceResourceId)
                || field->kind != SceneInspectorPropertyKind::ResourceReference
                || field->resourceType.empty())
                return "EDITOR-REFERENCE-ASSIGNMENT-SOURCE";
            std::filesystem::path definition;
            if (const auto* issue = locations.resolve(
                    intent.sourceResourceId, field->resourceType, resources, definition))
                return issue;
            const auto* record = resources.recordSnapshot(intent.sourceResourceId);
            if (!record || record->state != resource::ResourceState::Ready
                || record->reloadInProgress)
                return "EDITOR-REFERENCE-ASSIGNMENT-RESOURCE-NOT-READY";
            candidate.resourceId = intent.sourceResourceId;
            candidate.changed = field->stringValue != intent.sourceResourceId;
            break;
        }
        default:
            return "EDITOR-REFERENCE-ASSIGNMENT-SOURCE";
        }
        if (!context.sceneOwner.sameCurrentEpoch(document.ownerTicket()))
            return "EDITOR-REFERENCE-ASSIGNMENT-STALE";
        destination.swap(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-REFERENCE-ASSIGNMENT-ALLOCATION-FAILED";
    }
}
} // namespace arp::editor
