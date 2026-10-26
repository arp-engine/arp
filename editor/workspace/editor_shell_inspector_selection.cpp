#include "editor/workspace/editor_shell.h"

#include <new>

namespace arp::editor {
EditorSceneInspectorSelection::Snapshot EditorShell::inspectorSelectionSnapshot() const
{
    using Selection = EditorSceneInspectorSelection;
    Selection::Snapshot result;
    const auto count = sceneViewportAdditionalSelection_.size() + 1;
    const char* diagnostic = nullptr;
    if (count > Selection::editorBudget.maxObjects)
        diagnostic = "EDITOR-INSPECTOR-SELECTION-BUDGET";
    else {
        try {
            auto quota = core::makeShared<core::CpuMemoryQuota>(Selection::editorObservationBytes);
            core::CpuMemoryQuotaScope scope(quota);
            core::pmr::Vector<Selection::Object> objects{&core::defaultCpuMemoryResource()};
            objects.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                const auto id = index == 0 ? sceneDocument_.selectedObject()
                                           : sceneViewportAdditionalSelection_[index - 1];
                const auto* object = sceneDocument_.scene().findObject(id);
                if (!object) {
                    diagnostic = "EDITOR-INSPECTOR-SELECTION-MISSING";
                    break;
                }
                if (object->components().size() > Selection::editorBudget.maxComponentsPerObject) {
                    diagnostic = "EDITOR-INSPECTOR-SELECTION-COMPONENT-BUDGET";
                    break;
                }
                auto& observation = objects.emplace_back();
                observation.inspector.objectId = id.value();
                observation.inspector.objectName = object->name();
                for (const auto& component : object->components())
                    observation.components.push_back({component->id().value(),
                        core::pmr::String(
                            component->scriptTypeName(), &core::defaultCpuMemoryResource())});
                // Existing raw metadata is consumed only on the dispatcher owner
                // thread. Candidate menu indices are never value identities.
                observation.inspector.properties = sceneDocument_.inspectorProperties(id);
            }
            if (!diagnostic)
                diagnostic = Selection::prepare(objects, Selection::editorBudget, result);
        } catch (const std::bad_alloc&) {
            diagnostic = "EDITOR-INSPECTOR-SELECTION-ALLOCATION-FAILED";
        }
    }
    result.documentEpoch = sceneDocumentEpoch_;
    result.resourceRevision = resourceRevision_;
    result.selectedCount = count;
    if (diagnostic)
        result.diagnostic = diagnostic;
    return result;
}
} // namespace arp::editor
