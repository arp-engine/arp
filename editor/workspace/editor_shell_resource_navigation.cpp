#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_scene_reference_focus.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>
#include <charconv>
#include <new>

namespace arp::editor {
EditorCommandOutcome EditorShell::locateSceneResource(const LocateSceneResourceCommand& command)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    if (!command.documentEpoch || command.documentEpoch != sceneDocumentEpoch_
        || !command.resourceRevision || command.resourceRevision != resourceRevision_)
        return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
    try {
        std::optional<SceneInspectorProperty> property;
        const auto* issue = EditorSceneReferenceSource::prepare(
            sceneDocument_, command.objectId, command.componentId, command.property, property);
        if (issue) {
            const std::string_view reason(issue);
            return {false,
                false,
                reason == "EDITOR-REFERENCE-FOCUS-ALLOCATION-FAILED"
                        || reason == "EDITOR-REFERENCE-FOCUS-BUDGET"
                    ? EditorDiagnosticCode::CommandPreparationFailed
                    : EditorDiagnosticCode::ResourceDefinitionUnavailable};
        }
        if (property->kind != SceneInspectorPropertyKind::ResourceReference
            || property->resourceType.empty() || property->stringValue.empty()
            || !resource::isResourceUuid(property->stringValue))
            return {false, false, EditorDiagnosticCode::ResourceDefinitionUnavailable};
        const auto& locations = projectResources_.resourceLocations();
        std::filesystem::path file;
        issue = locations.resolve(
            property->stringValue, property->resourceType, projectResources_.manager(), file);
        if (issue)
            return {false,
                false,
                std::string_view(issue) == "EDITOR-RESOURCE-LOCATION-ALLOCATION-FAILED"
                    ? EditorDiagnosticCode::CommandPreparationFailed
                    : EditorDiagnosticCode::ResourceDefinitionUnavailable};
        ProjectBrowserModel candidate;
        std::size_t row = 0;
        issue = projectBrowser_.prepareReveal(file, candidate, row);
        if (issue)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};

        std::filesystem::path verified;
        if (locations.resolve(property->stringValue,
                property->resourceType,
                projectResources_.manager(),
                verified)
            || verified != file)
            return {false, false, EditorDiagnosticCode::ResourceDefinitionUnavailable};
        const auto offset = row < layout::kFileSlotCount ? 0 : row - layout::kFileSlotCount + 1;
        const bool changed = selectedFilePath_ != file || fileScrollOffset_ != offset
            || projectBrowser_.currentDirectory() != candidate.currentDirectory()
            || projectBrowser_.search() != candidate.search() || filesPresentationFailed_
            || projectBrowser_.entries().size() != candidate.entries().size()
            || !std::equal(projectBrowser_.entries().begin(),
                projectBrowser_.entries().end(),
                candidate.entries().begin(),
                [](const auto& left, const auto& right) {
                    return left.path == right.path && left.directory == right.directory;
                });
        setResourceNavigationRejected(false);
        projectBrowser_.swap(candidate);
        selectedFilePath_.swap(file);
        fileScrollOffset_ = offset;
        lastClickedScriptPath_.clear();
        if (!retryFileBrowserPresentation())
            return {true, true, EditorDiagnosticCode::ResourceNavigationPresentationFailed};
        return {true, changed, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

bool EditorShell::activateSceneResourceLocate(std::string_view source)
{
    constexpr std::string_view prefix = "scene_resource_locate_scene_property_";
    if (source.substr(0, prefix.size()) != prefix)
        return false;
    const auto objectEnd = source.find('_', prefix.size());
    const auto componentEnd =
        objectEnd == std::string_view::npos ? objectEnd : source.find('_', objectEnd + 1);
    if (componentEnd == std::string_view::npos)
        return true;
    LocateSceneResourceCommand command;
    const auto object =
        std::from_chars(source.data() + prefix.size(), source.data() + objectEnd, command.objectId);
    const auto component = std::from_chars(
        source.data() + objectEnd + 1, source.data() + componentEnd, command.componentId);
    const auto path = source.substr(componentEnd + 1);
    if (object.ec != std::errc{} || object.ptr != source.data() + objectEnd
        || component.ec != std::errc{} || component.ptr != source.data() + componentEnd
        || command.objectId != sceneDocument_.selectedObject().value() || !command.componentId
        || path.empty() || path.size() > EditorSceneInspectorSelection::editorBudget.maxStringBytes)
        return true;
    command.documentEpoch = sceneDocumentEpoch_;
    command.resourceRevision = resourceRevision_;
    command.property = path;
    const auto response = submitCommand(std::move(command));
    if (response.status != EditorResponseStatus::Completed) {
        setResourceNavigationRejected(true);
        feedback("Definition unavailable; browser and Scene kept");
        presentationState_.openOutput();
        visible("output_panel", true);
    }
    return true;
}
} // namespace arp::editor
